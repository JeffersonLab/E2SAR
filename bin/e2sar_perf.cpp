/**
 * In-memory performance estimator for E2SAR. Combined both sender and receiver code. 
 * A simplified iperf-like code to help evaluate E2SAR code.
 */

#include <signal.h>

#include <iostream>
#include <sstream>
#include <vector>
#include <boost/program_options.hpp>
#include <locale>

#include <boost/pool/pool.hpp>

#include "e2sar.hpp"

namespace po = boost::program_options;
namespace pt = boost::posix_time;
using namespace e2sar;

// prepare a pool
boost::pool<> *evtBufferPool;
// to avoid locking the pool we use the return queue
boost::lockfree::queue<u_int8_t*> returnBufferQueue{10000};

// event payload
auto eventPldStart = "This is a start of event payload"s;
auto eventPldEnd = "...the end"s;

bool threadsRunning = true;
u_int16_t reportThreadSleepMs{1000};
Reassembler *reasPtr{nullptr};
Segmenter *segPtr{nullptr};
LBManager *lbmPtr{nullptr};
std::vector<std::string> senders;

void ctrlCHandler(int sig) 
{
    std::cout << "Stopping threads" << std::endl;

    if (segPtr != nullptr) {
        if (lbmPtr != nullptr) {
            auto rmres = lbmPtr->removeSenders(senders);
            if (rmres.has_error()) 
                std::cerr << "Unable to remove sender from list on exit: " << rmres.error().message() << std::endl;
        }
        segPtr->stopThreads();
    }
    if (reasPtr != nullptr)
    {
        auto deregres = reasPtr->deregisterWorker();
        if (deregres.has_error()) 
            std::cerr << "Unable to deregister worker on exit: " << deregres.error().message() << std::endl;
        reasPtr->stopThreads();
    }
    threadsRunning = false;
    // instead of join
    boost::chrono::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);
    exit(0);
}

/* Function used to check that 'opt1' and 'opt2' are not specified
   at the same time. */
void conflicting_options(const po::variables_map &vm,
                         const std::string &opt1, const std::string &opt2)
{
    if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted())
        throw std::logic_error(std::string("Conflicting options '") + opt1 + "' and '" + opt2 + "'.");
}

/* Function used to check that of 'for_what' is specified, then
   'required_option' is specified too. */
void option_dependency(const po::variables_map &vm,
                       const std::string &for_what, const std::string &required_option)
{
    if (vm.count(for_what) && !vm[for_what].defaulted())
        if (vm.count(required_option) == 0 || vm[required_option].defaulted())
            throw std::logic_error(std::string("Option '") + for_what + "' requires option '" + required_option + "'.");
}

// callback
void freeBuffer(boost::any a) 
{
    auto p = boost::any_cast<u_int8_t*>(a);
    returnBufferQueue.push(p);
}

result<int> sendEvents(Segmenter &s, EventNum_t startEventNum, size_t numEvents, 
    size_t eventBufSize, float rateGbps) {

    // convert bit rate to event rate
    float eventRate{rateGbps*1000000000/(eventBufSize*8)};
    u_int64_t interEventSleepUsec{static_cast<u_int64_t>(eventBufSize*8/(rateGbps * 1000))};

    // to help print large integers
    std::cout.imbue(std::locale(""));

    std::cout << "Sending bit rate is " << rateGbps << " Gbps" << std::endl;
    std::cout << "Event size is " << eventBufSize << " bytes or " << eventBufSize*8 << " bits" << std::endl;
    std::cout << "Event rate is " << eventRate << " Hz" << std::endl;
    std::cout << "Inter-event sleep time is " << interEventSleepUsec << " microseconds" << std::endl;
    std::cout << "Sending " << numEvents << " event buffers" << std::endl;
    std::cout << "Using MTU " << s.getMTU() << std::endl;

    if (s.getMaxPldLen() < eventPldStart.size() + eventPldEnd.size())
        return E2SARErrorInfo{E2SARErrorc::LogicError, "MTU is too short to send needed payload"};

    // start threads, open sockets
    auto openRes = s.openAndStart();
    if (openRes.has_error())
        return openRes;

    // initialize a pool of memory buffers  we will be sending (mostly filled with random data)
    evtBufferPool = new boost::pool<>{eventBufSize};

    for(size_t evt = 0; evt < numEvents; evt++)
    {
        // Get the current time point
        auto nowT = boost::chrono::high_resolution_clock::now();

        // send the event
        auto eventBuffer = static_cast<u_int8_t*>(evtBufferPool->malloc());
        // fill in the first part of the buffer with something meaningful and also the end
        memcpy(eventBuffer, eventPldStart.c_str(), eventPldStart.size());
        memcpy(eventBuffer + eventBufSize - eventPldEnd.size(), eventPldEnd.c_str(), eventPldEnd.size());

        // put on queue with a callback to free this buffer
        auto sendRes = s.addToSendQueue(eventBuffer, eventBufSize, evt, 0, 0,
            &freeBuffer, eventBuffer);

        // sleep
        auto until = nowT + boost::chrono::microseconds(interEventSleepUsec);
        if (nowT > until)
        {
            return E2SARErrorInfo{E2SARErrorc::LogicError, 
                "Clock overrun, either event buffer length too short or requested sending rate too high"};
        }
        // free the backlog of empty buffers
        u_int8_t *item{nullptr};
        while (returnBufferQueue.pop(item))
            evtBufferPool->free(item);
        boost::this_thread::sleep_until(until);
    }
    auto stats = s.getSendStats();

    evtBufferPool->purge_memory();
    std::cout << "Completed, " << stats.get<0>() << " frames sent, " << stats.get<1>() << " errors" << std::endl;
    if (stats.get<1>() != 0)
    {
        std::cout << "Last error encountered: " << strerror(stats.get<2>()) << std::endl;
    }
    return 0;
}

result<int> recvEvents(Reassembler &r, int durationSec) {

    std::cout << "Receiving on ports " << r.get_recvPorts().first << ":" << r.get_recvPorts().second << std::endl;

    u_int8_t *evtBuf{nullptr};
    size_t evtSize;
    EventNum_t evtNum;
    u_int16_t dataId;

    auto openRes = r.openAndStart();
    if (openRes.has_error())
        return openRes;

    // to help print large integers
    std::cout.imbue(std::locale(""));

    auto nowT = boost::chrono::steady_clock::now();

    // register the worker (will be NOOP if withCP is set to false)
    auto hostname_res = NetUtil::getHostName();
    if (hostname_res.has_error()) 
    {
        return E2SARErrorInfo{hostname_res.error().code(), hostname_res.error().message()};
    }
    auto regres = r.registerWorker(hostname_res.value());
    if (regres.has_error())
    {
        return E2SARErrorInfo{E2SARErrorc::RPCError, 
            "Unable to register worker node due to " + regres.error().message()};
    }

    // receive loop
    while(true)
    {
        auto getEvtRes = r.recvEvent(&evtBuf, &evtSize, &evtNum, &dataId, 1000);

        auto nextTimeT = boost::chrono::steady_clock::now();

        if ((durationSec != 0) && (nextTimeT - nowT > boost::chrono::seconds(durationSec)))
        {
            ctrlCHandler(0);
            break;
        }

        if (getEvtRes.has_error())
            return getEvtRes;

        if (getEvtRes.value() == -1)
            continue;

        if (memcmp(evtBuf, eventPldStart.c_str(), eventPldStart.size() - 1))
            return E2SARErrorInfo{E2SARErrorc::MemoryError, "Payload start does not match expected"};

        if (memcmp(evtBuf + evtSize - eventPldEnd.size(), eventPldEnd.c_str(), eventPldEnd.size() - 1))
            return E2SARErrorInfo{E2SARErrorc::MemoryError, "Payload end doesn't match expected"};

        delete evtBuf;
    }
    std::cout << "Completed" << std::endl;
    return 0;
}

void recvStatsThread(Reassembler *r)
{
    while(threadsRunning)
    {
        auto nowT = boost::chrono::high_resolution_clock::now();

        auto stats = r->getStats();
        /*
             *  - EventNum_t enqueueLoss;  // number of events received and lost on enqueue
             *  - EventNum_t eventSuccess; // events successfully processed
             *  - int lastErrno; 
             *  - int grpcErrCnt; 
             *  - int dataErrCnt; 
             *  - E2SARErrorc lastE2SARError; 
        */
        std::cout << "Stats:" << std::endl;
        std::cout << "\tEvents Received: " << stats.get<1>() << std::endl;
        std::cout << "\tEvents Lost: " << stats.get<0>() << std::endl;
        std::cout << "\tData Errors: " << stats.get<4>() << std::endl;
        if (stats.get<4>() > 0)
            std::cout << "\tLast Data Error: " << strerror(stats.get<2>()) << std::endl;
        std::cout << "\tgRPC Errors: " << stats.get<3>() << std::endl;
        if (stats.get<5>() != E2SARErrorc::NoError)
            std::cout << "\tLast E2SARError code: " << stats.get<5>() << std::endl;

        auto until = nowT + boost::chrono::milliseconds(reportThreadSleepMs);
        boost::this_thread::sleep_until(until);
    }
}

int main(int argc, char **argv)
{
    po::options_description od("Command-line options");

    auto opts = od.add_options()("help,h", "show this help message");

    size_t numEvents{0};
    EventNum_t startingEventNum{0};
    size_t eventBufferSize;
    u_int16_t mtu;
    u_int32_t eventSourceId;
    u_int16_t dataId;
    size_t numThreads;
    float rateGbps;
    int sockBufSize;
    int durationSec;
    bool withCP;
    std::string sndrcvIP;
    std::string iniFile;
    u_int16_t recvStartPort;

    // parameters
    opts("send,s", "send traffic");
    opts("recv,r", "receive traffic");

    opts("length,l", po::value<size_t>(&eventBufferSize)->default_value(1024*1024), "event buffer length (defaults to 1024^2) [s]");
    opts("uri,u", po::value<std::string>(), "specify EJFAT_URI on the command-line instead of the environment variable");
    opts("num,n", po::value<size_t>(&numEvents)->default_value(10), "number of event buffers to send (defaults to 10) [s]");
    opts("enum,e", po::value<EventNum_t>(&startingEventNum)->default_value(0), "starting event number (defaults to 0) [s]");
    opts("mtu,m", po::value<u_int16_t>(&mtu)->default_value(1500), "MTU (default 1500) [s]");
    opts("src", po::value<u_int32_t>(&eventSourceId)->default_value(1234), "Event source (default 1234) [s]");
    opts("dataid", po::value<u_int16_t>(&dataId)->default_value(4321), "Data id (default 4321) [s]");
    opts("threads,t", po::value<size_t>(&numThreads)->default_value(1), "number of receive threads (defaults to 1) [r]");
    opts("rate", po::value<float>(&rateGbps)->default_value(1.0), "send rate in Gbps (defaults to 1.0)");
    opts("period,p", po::value<u_int16_t>(&reportThreadSleepMs)->default_value(1000), "receive side reporting thread sleep period in ms (defaults to 1000) [r]");
    opts("bufsize,b", po::value<int>(&sockBufSize)->default_value(1024*1024*3), "send or receive socket buffer size (default to 3MB)");
    opts("duration,d", po::value<int>(&durationSec)->default_value(0), "duration for receiver to run for (defaults to 0 - until Ctrl-C is pressed)");
    opts("withcp,c", po::bool_switch()->default_value(false), "enable control plane interactions");
    opts("ini,i", po::value<std::string>(&iniFile)->default_value(""), "INI file to initialize SegmenterFlags [s]] or ReassemblerFlags [r]."
        " Values found in the file override --withcp, --mtu and --bufsize");
    opts("ip", po::value<std::string>(&sndrcvIP)->default_value("127.0.0.1"), "IP address (IPv4 or IPv6) from which sender sends from or on which receiver listens. Defaults to 127.0.0.1. [s,r]");
    opts("port", po::value<u_int16_t>(&recvStartPort)->default_value(10000), "Starting UDP port number on which receiver listens. Defaults to 10000. [r] ");

    po::variables_map vm;

    po::store(po::parse_command_line(argc, argv, od), vm);
    po::notify(vm);

    // for ctrl-C
    signal(SIGINT, ctrlCHandler);

    try
    {
        conflicting_options(vm, "send", "receive");
        conflicting_options(vm, "recv", "num");
        conflicting_options(vm, "recv", "enum");
        conflicting_options(vm, "recv", "length");
        conflicting_options(vm, "recv", "mtu");
        conflicting_options(vm, "recv", "src");
        conflicting_options(vm, "recv", "dataid");
        conflicting_options(vm, "recv", "rate");
        conflicting_options(vm, "send", "threads");
        conflicting_options(vm, "send", "period");
        option_dependency(vm, "recv", "ip");
        option_dependency(vm, "vm", "port");
        option_dependency(vm, "send", "ip");
        conflicting_options(vm, "recv", "port");
    }
    catch (const std::logic_error &le)
    {
        std::cerr << "Error processing command-line options: " << le.what() << std::endl;
        return -1;
    }

    std::cout << "E2SAR Version: " << get_Version() << std::endl;
    if (vm.count("help"))
    {
        std::cout << od << std::endl;
        return 0;
    }

    withCP = vm["withcp"].as<bool>();

    // make sure the token is interpreted as the correct type, depending on the call
    EjfatURI::TokenType tt{EjfatURI::TokenType::instance};

    std::string ejfat_uri;
    if (vm.count("send") || vm.count("recv")) 
    {
        auto uri_r = (vm.count("uri") ? EjfatURI::getFromString(vm["uri"].as<std::string>(), tt) : 
            EjfatURI::getFromEnv("EJFAT_URI"s, tt));
        if (uri_r.has_error())
        {
            std::cerr << "Error in parsing URI from command-line, error "s + uri_r.error().message() << std::endl;
            return -1;
        }
        auto uri = uri_r.value();
        if (vm.count("send")) {
            // if using control plane
            if (withCP)
            {
                // add to senders list of 1 element
                senders.push_back(sndrcvIP);

                // create LBManager
                lbmPtr = new LBManager(uri, false);

                // register senders
                auto addres = lbmPtr->addSenders(senders);
                if (addres.has_error()) 
                {
                    std::cerr << "Unable to add a sender due to error " << addres.error().message() 
                        << ", exiting" << std::endl;
                    return -1;
                }
            }

            Segmenter::SegmenterFlags sflags;
            if (!iniFile.empty())
            {
                std::cout << "Loading SegmenterFlags from " << iniFile << std::endl;
                auto sflagsRes = Segmenter::SegmenterFlags::getFromINI(iniFile);
                if (sflagsRes.has_error())
                {
                    std::cerr << "Unable to parse SegmenterFlags INI file " << iniFile << std::endl;
                    return -1;
                }
                sflags = sflagsRes.value();
            } else {   
                sflags.useCP = withCP; 
                sflags.mtu = mtu;
                sflags.sndSocketBufSize = sockBufSize;
            }
            std::cout << "Control plane will be " << (sflags.useCP ? "ON" : "OFF") << std::endl;
            std::cout << (sflags.useCP ? "*** Make sure the LB has been reserved and the URI reflects the reserved instance information." :
                "*** Make sure the URI reflects proper data address, other parts are ignored.") << std::endl;

            try {
                Segmenter seg(uri, dataId, eventSourceId, sflags);
                segPtr = &seg;
                auto res = sendEvents(seg, startingEventNum, numEvents, eventBufferSize, rateGbps);

                if (res.has_error()) {
                    std::cerr << "Segmenter encountered an error: " << res.error().message() << std::endl;
                }
            } catch (E2SARException &e) {
                std::cerr << "Unable to create segmenter: " << static_cast<std::string>(e) << std::endl;
            }

        } else if (vm.count("recv")) {
            Reassembler::ReassemblerFlags rflags;

            if (!iniFile.empty())
            {
                std::cout << "Loading ReassemblerFlags from " << iniFile << std::endl;
                auto rflagsRes = Reassembler::ReassemblerFlags::getFromINI(iniFile);
                if (rflagsRes.has_error())
                {
                    std::cerr << "Unable to parse ReassemblerFlags INI file " << iniFile << std::endl;
                    return -1;
                }
                rflags = rflagsRes.value();
            } else 
            {
                rflags.useCP = withCP;
                rflags.withLBHeader = not withCP;
                rflags.rcvSocketBufSize = sockBufSize;
            }
            std::cout << "Control plane will be " << (rflags.useCP ? "ON" : "OFF") << std::endl;
            std::cout << (rflags.useCP ? "*** Make sure the LB has been reserved and the URI reflects the reserved instance information." :
                "*** Make sure the URI reflects proper data address, other parts are ignored.") << std::endl;

            try {
                ip::address ip = ip::make_address(sndrcvIP);
                Reassembler reas(uri, ip, recvStartPort, numThreads, rflags);
                reasPtr = &reas;
                boost::thread statT(&recvStatsThread, &reas);
                auto res = recvEvents(reas, durationSec);

                if (res.has_error()) {
                    std::cerr << "Reassembler encountered an error: " << res.error().message() << std::endl;
                }
            } catch (E2SARException &e) {
                std::cerr << "Unable to create reassembler: " << static_cast<std::string>(e) << std::endl;
            }
        }
    } else
        // print help
        std::cout << od << std::endl;
}