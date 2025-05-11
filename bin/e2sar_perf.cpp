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
// app-level stats
std::atomic<u_int64_t> mangledEvents{0};
std::atomic<u_int64_t> receivedWithError{0};

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
    threadsRunning = false;
    boost::chrono::milliseconds duration(1000);
    boost::this_thread::sleep_for(duration);
    if (segPtr != nullptr) {
        if (lbmPtr != nullptr) {
            std::cout << "Removing senders: ";
            if (senders.size() > 0)
            {
                for (auto s: senders)
                    std::cout << s << " ";
                std::cout << std::endl;
                auto rmres = lbmPtr->removeSenders(senders);
                if (rmres.has_error()) 
                    std::cerr << "Unable to remove sender from list on exit: " << rmres.error().message() << std::endl;
            } else
            {
                auto rmres = lbmPtr->removeSenderSelf();
                std::cout << "self" << std::endl;
                if (rmres.has_error()) 
                    std::cerr << "Unable to remove auto-detected sender from list on exit: " << rmres.error().message() << std::endl;
            }
        }
        segPtr->stopThreads();
        delete segPtr;
    }
    if (reasPtr != nullptr)
    {
        std::cout << "Deregistering worker" << std::endl;
        auto deregres = reasPtr->deregisterWorker();
        if (deregres.has_error()) 
            std::cerr << "Unable to deregister worker on exit: " << deregres.error().message() << std::endl;
        reasPtr->stopThreads();

        // get per-fd stats
        auto fdStats = reasPtr->get_FDStats();
        if (fdStats.has_error())
            std::cout << "Unable to get per FD stats: " << fdStats.error().message() << std::endl;

        std::cout << "Port Stats: " << std::endl;
        size_t totalFragments{0};
        for (auto fds: fdStats.value())
        {
            totalFragments += fds.second;
            std::cout << "\tPort: " << fds.first << " Received: " << fds.second << std::endl;
        }
        std::cout << "Total: " << totalFragments << std::endl;
        delete reasPtr;
    }

    // instead of join
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
        if (vm.count(required_option) == 0)
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

    // to help print large integers
    std::cout.imbue(std::locale(""));

    std::cout << "Sending average bit rate is " << rateGbps << " Gbps" << std::endl;
    std::cout << "Event size is " << eventBufSize << " bytes or " << eventBufSize*8 << " bits" << std::endl;
    std::cout << "Sending " << numEvents << " event buffers" << std::endl;
    std::cout << "Using interface " << (s.getIntf() == "" ? "unknown"s : s.getIntf()) << std::endl;
    std::cout << "Using MTU " << s.getMTU() << std::endl;
    u_int32_t expectedFrames = numEvents * ((eventBufSize + s.getMTU() - TOTAL_HDR_LEN - 1)/ (s.getMTU() - TOTAL_HDR_LEN));

    if (s.getMaxPldLen() < eventPldStart.size() + eventPldEnd.size())
        return E2SARErrorInfo{E2SARErrorc::LogicError, "MTU is too short to send needed payload"};

    // start threads, open sockets
    auto openRes = s.openAndStart();
    if (openRes.has_error())
        return openRes;

    // initialize a pool of memory buffers  we will be sending (mostly filled with random data)
    evtBufferPool = new boost::pool<>{eventBufSize};

    auto sendStartTime = boost::chrono::high_resolution_clock::now();
    for(size_t evt = 0; evt < numEvents; evt++)
    {
        // send the event
        auto eventBuffer = static_cast<u_int8_t*>(evtBufferPool->malloc());
        // fill in the first part of the buffer with something meaningful and also the end
        memcpy(eventBuffer, eventPldStart.c_str(), eventPldStart.size());
        memcpy(eventBuffer + eventBufSize - eventPldEnd.size(), eventPldEnd.c_str(), eventPldEnd.size());

        // put on queue with a callback to free this buffer
        while(true)
        {
            auto sendRes = s.addToSendQueue(eventBuffer, eventBufSize, evt, 0, 0,
                &freeBuffer, eventBuffer);
            if (sendRes.has_error()) 
            {
                if (sendRes.error().code() == E2SARErrorc::MemoryError)
                {
                    // unable to send, queue full, try again
                    ;
                } else 
                {
                    std::cout << "Unexpected error submitting event into the queue: " << sendRes.error().message() << std::endl;
                }
            } else
                break;
        }
        // free the backlog of empty buffers
        u_int8_t *item{nullptr};
        while (returnBufferQueue.pop(item))
            evtBufferPool->free(item);
    }

    // measure this right after we exit the send loop
    while(true)
    {
        auto stats = s.getSendStats();

        // check if we can exit - either all events left the queue or
        // there are errors
        if ((expectedFrames == stats.msgCnt) ||
            (stats.errCnt > 0))
            break;
    }
    auto sendEndTime = boost::chrono::high_resolution_clock::now();

    auto stats = s.getSendStats();
    if (expectedFrames > stats.msgCnt)
        std::cout << "WARNING: Fewer frames than expected have been sent (" << stats.msgCnt << " of " << 
            expectedFrames << ")." << std::endl;

    evtBufferPool->purge_memory();
    std::cout << "Completed, " << stats.msgCnt << " frames sent, " << stats.errCnt << " errors" << std::endl;
    if (stats.errCnt != 0)
    {
        if (stats.lastE2SARError != E2SARErrorc::NoError)
        {
            std::cout << "Last E2SARError code: " << make_error_code(stats.lastE2SARError).message() << std::endl;
        }
        else
            std::cout << "Last error encountered: " << strerror(stats.lastErrno) << std::endl;
    } 

    // estimate the goodput
    auto elapsedUsec = boost::chrono::duration_cast<boost::chrono::microseconds>(sendEndTime - sendStartTime);
    std::cout << "Elapsed usecs: " << elapsedUsec << std::endl;
    // *8 for bits, *1000 to convert to Gbps
    std::cout << "Estimated effective throughput (Gbps): " << 
        (stats.msgCnt * s.getMTU() * 8.0) / (elapsedUsec.count() * 1000) << std::endl;

    return 0;
}

result<int> prepareToReceive(Reassembler &r)
{
    std::cout << "Receiving on ports " << r.get_recvPorts().first << ":" << r.get_recvPorts().second << std::endl;

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
    if (regres.value() == 1)
        std::cout << "Registered the worker" << std::endl;

    // NOTE: if we switch the order of registerWorker and openAndStart
    // you get into a race condition where the sendState thread starts and tries
    // to send queue updates, however session token is not yet available...
    auto openRes = r.openAndStart();
    if (openRes.has_error())
        return openRes;

    return 0;
}

int recvEvents(Reassembler *r, int *durationSec) {

    u_int8_t *evtBuf{nullptr};
    size_t evtSize;
    EventNum_t evtNum;
    u_int16_t dataId;

    // to help print large integers
    std::cout.imbue(std::locale(""));

    auto nowT = boost::chrono::steady_clock::now();

    // receive loop
    while(threadsRunning)
    {
        auto getEvtRes = r->recvEvent(&evtBuf, &evtSize, &evtNum, &dataId, 1000);

        auto nextTimeT = boost::chrono::steady_clock::now();

        if ((*durationSec != 0) && (nextTimeT - nowT > boost::chrono::seconds(*durationSec)))
            break;

        if (getEvtRes.has_error())
        {
            receivedWithError++;
            continue;
        }

        if (getEvtRes.value() == -1)
            continue;

        if (memcmp(evtBuf, eventPldStart.c_str(), eventPldStart.size() - 1) || 
            memcmp(evtBuf + evtSize - eventPldEnd.size(), eventPldEnd.c_str(), eventPldEnd.size() - 1))
            mangledEvents++;

        delete evtBuf;
        evtBuf = nullptr;
    }
    std::cout << "Completed" << std::endl;
    return 0;
}

void recvStatsThread(Reassembler *r)
{
    std::vector<boost::tuple<EventNum_t, u_int16_t, size_t>> lostEvents;

    while(threadsRunning)
    {
        auto nowT = boost::chrono::high_resolution_clock::now();

        auto stats = r->getStats();

        while(true)
        {
            auto res = r->get_LostEvent();
            if (res.has_error())
                break;
            lostEvents.push_back(res.value());
        }
        /*
             *  - 0 EventNum_t enqueueLoss;  // number of events received and lost on enqueue
             *  - 1 EventNum_t reassemblyLoss; // number of events lost in reassembly due to missing segments
             *  - 2 EventNum_t eventSuccess; // events successfully processed
             *  - 3 int lastErrno; 
             *  - 4 int grpcErrCnt; 
             *  - 5 int dataErrCnt; 
             *  - 6 E2SARErrorc lastE2SARError; 
        */
        std::cout << "Stats:" << std::endl;
        std::cout << "\tEvents Received: " << stats.eventSuccess << std::endl;
        std::cout << "\tEvents Mangled: " << mangledEvents << std::endl;
        std::cout << "\tEvents Lost in reassembly: " << stats.reassemblyLoss << std::endl;
        std::cout << "\tEvents Lost in enqueue: " << stats.enqueueLoss << std::endl;
        std::cout << "\tData Errors: " << stats.dataErrCnt << std::endl;
        if (stats.dataErrCnt > 0)
            std::cout << "\tLast Data Error: " << strerror(stats.lastErrno) << std::endl;
        std::cout << "\tgRPC Errors: " << stats.grpcErrCnt << std::endl;
        if (stats.lastE2SARError != E2SARErrorc::NoError)
            std::cout << "\tLast E2SARError code: " << make_error_code(stats.lastE2SARError).message() << std::endl;

        std::cout << "\tEvents lost so far (<Evt ID:Data ID/num frags rcvd>): ";
        for(auto evt: lostEvents)
        {
            std::cout << "<" << evt.get<0>() << ":" << evt.get<1>() << "/" << evt.get<2>() << "> ";
        }
        std::cout << std::endl;

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
    size_t numThreads, numSockets, readThreads;
    float rateGbps;
    int sockBufSize;
    int durationSec;
    bool withCP;
    bool zeroRate;
    bool usecAsEventNum;
    bool autoIP;
    std::string sndrcvIP;
    std::string iniFile;
    u_int16_t recvStartPort;
    std::vector<int> coreList;
    std::vector<std::string> optimizations;
    int numaNode;

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
    opts("threads", po::value<size_t>(&numThreads)->default_value(1), "number of receive threads (defaults to 1) [r]");
    opts("sockets", po::value<size_t>(&numSockets)->default_value(4), "number of send sockets (defaults to 4) [r]");
    opts("rate", po::value<float>(&rateGbps)->default_value(1.0), "send rate in Gbps (defaults to 1.0)");
    opts("period,p", po::value<u_int16_t>(&reportThreadSleepMs)->default_value(1000), "receive side reporting thread sleep period in ms (defaults to 1000) [r]");
    opts("bufsize,b", po::value<int>(&sockBufSize)->default_value(1024*1024*3), "send or receive socket buffer size (default to 3MB)");
    opts("duration,d", po::value<int>(&durationSec)->default_value(0), "duration for receiver to run for (defaults to 0 - until Ctrl-C is pressed)[s]");
    opts("withcp,c", po::bool_switch()->default_value(false), "enable control plane interactions");
    opts("ini,i", po::value<std::string>(&iniFile)->default_value(""), "INI file to initialize SegmenterFlags [s] or ReassemblerFlags [r]."
        " Values found in the file override --withcp, --mtu, --sockets, --zerorate, --seq, --novalidate, --ip[46] and --bufsize");
    opts("ip", po::value<std::string>(&sndrcvIP)->default_value(""), "IP address (IPv4 or IPv6) from which sender sends from or on which receiver listens (conflicts with --autoip) [s,r]");
    opts("port", po::value<u_int16_t>(&recvStartPort)->default_value(10000), "Starting UDP port number on which receiver listens. Defaults to 10000. [r] ");
    opts("ipv6,6", "force using IPv6 control plane address if URI specifies hostname (disables cert validation) [s,r]");
    opts("ipv4,4", "force using IPv4 control plane address if URI specifies hostname (disables cert validation) [s,r]");
    opts("novalidate,v", "don't validate server certificate [s,r]");
    opts("autoip", po::bool_switch()->default_value(false), "auto-detect dataplane outgoing ip address (conflicts with --ip; doesn't work for reassembler in back-to-back testing) [s,r]");
    opts("zerorate,z", po::bool_switch()->default_value(false),"report zero event number change rate in Sync messages [s]");
    opts("seq", po::bool_switch()->default_value(false),"use sequential numbers as event numbers in Sync and LB messages instead of usec [s]");
    opts("deq", po::value<size_t>(&readThreads)->default_value(1), "number of event dequeue threads in receiver (defaults to 1) [r]");
    opts("cores", po::value<std::vector<int>>(&coreList)->multitoken(), "optional list of cores to bind sender or receiver threads to; number of receiver threads is equal to the number of cores [s,r]");
    opts("optimize,o", po::value<std::vector<std::string>>(&optimizations)->multitoken(), "a list of optimizations to turn on [s]");
    opts("numa", po::value<int>(&numaNode)->default_value(-1), "bind all memory allocation to this NUMA node (if >= 0) [s,r]");

    po::variables_map vm;

    try {
        po::store(po::parse_command_line(argc, argv, od), vm);
        po::notify(vm);
    } catch (const boost::program_options::error &e) {
        std::cout << "Unable to parse command line: " << e.what() << std::endl;
        return -1;
    }

    try
    {
        conflicting_options(vm, "send", "recv");
        conflicting_options(vm, "recv", "num");
        conflicting_options(vm, "recv", "enum");
        conflicting_options(vm, "recv", "length");
        conflicting_options(vm, "recv", "mtu");
        conflicting_options(vm, "recv", "src");
        conflicting_options(vm, "recv", "dataid");
        conflicting_options(vm, "recv", "rate");
        conflicting_options(vm, "send", "threads");
        conflicting_options(vm, "send", "period");
        conflicting_options(vm, "ipv4", "ipv6");
        option_dependency(vm, "recv", "ip");
        option_dependency(vm, "recv", "port");
        option_dependency(vm, "send", "ip");
        // these are optional
        conflicting_options(vm, "send", "duration");
        conflicting_options(vm, "send", "port");
        conflicting_options(vm, "deq", "send");
        conflicting_options(vm, "seq", "recv");
        conflicting_options(vm, "cores", "threads");
    }
    catch (const std::logic_error &le)
    {
        std::cerr << "Error processing command-line options: " << le.what() << std::endl;
        return -1;
    }

    // for ctrl-C
    signal(SIGINT, ctrlCHandler);

    std::cout << "E2SAR Version:                 " << get_Version() << std::endl;
    std::cout << "E2SAR Available Optimizations: " << 
        concatWithSeparator(Optimizations::availableAsStrings()) << std::endl;
    
    if (vm.count("help"))
    {
        std::cout << od << std::endl;

        std::cout << std::endl;

        std::cout << "A trivial loopback invocation sending 10 1MB events at 1Gbps looks like this" << std::endl;
        std::cout << "(start the receiver first, stop it with Ctrl-C when done): " << std::endl;
        std::cout << "Receiver: e2sar_perf --ip '127.0.0.1' -r -u 'ejfat://token@127.0.0.1:18020/lb/36?data=127.0.0.1:10000'" << std::endl;
        std::cout << "Sender:   e2sar_perf --ip '127.0.0.1' -s -u 'ejfat://token@127.0.0.1:18020/lb/36?data=127.0.0.1:10000' --rate 1" << std::endl;
        return 0;
    }

    auto ropt = Optimizations::select(optimizations);
    if (ropt.has_error())
    {
        std::cerr << ropt.error().message() << std::endl;
        return -1;
    }
    std::cout << "E2SAR Selected Optimizations:  " << 
        concatWithSeparator(Optimizations::selectedAsStrings()) << std::endl;

    // deal with NUMA memory binding before going further
    if (numaNode >= 0)
    {
        auto numaRes = Affinity::setNUMABind(numaNode);
        if (numaRes.has_error())
        {
            std::cout << "Unable to bind to specified NUMA node: " << numaRes.error().message() << std::endl;
            return -1;
        }
    }

    withCP = vm["withcp"].as<bool>();
    zeroRate = vm["zerorate"].as<bool>();
    usecAsEventNum = not vm["seq"].as<bool>();
    autoIP = vm["autoip"].as<bool>();

    if (not autoIP and (vm["ip"].as<std::string>().length() == 0))
    {
        std::cout << "One of --ip or --autoip must be specified. --autoip attempts to auto-detect the address" <<
            " of the outgoing or incoming interface using 'data=' portion of the EJFAT_URI" << std::endl;
        return -1;
    }

    bool preferV6 = false;
    if (vm.count("ipv6"))
    {
        preferV6 = true;
    }

    // if ipv4 or ipv6 requested explicitly
    bool preferHostAddr = false;
    if (vm.count("ipv6") || vm.count("ipv4"))
        preferHostAddr = true;

    bool validate = true;
    if (vm.count("novalidate"))
        validate = false;

    // make sure the token is interpreted as the correct type, depending on the call
    EjfatURI::TokenType tt{EjfatURI::TokenType::instance};

    std::string ejfat_uri;
    if (vm.count("send") || vm.count("recv")) 
    {
        auto uri_r = (vm.count("uri") ? EjfatURI::getFromString(vm["uri"].as<std::string>(), tt, preferV6) : 
            EjfatURI::getFromEnv("EJFAT_URI"s, tt, preferV6));
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
                // create LBManager
                lbmPtr = new LBManager(uri, validate, preferHostAddr);

                // register senders
                std::cout << "Adding senders to LB: ";
                if (not autoIP)
                {
                    senders.push_back(sndrcvIP);
                    for (auto s: senders)
                        std::cout << s << " ";
                    std::cout << std::endl;
                    auto addres = lbmPtr->addSenders(senders);
                    if (addres.has_error()) 
                    {
                        std::cerr << "Unable to add a sender due to error " << addres.error().message() 
                            << ", exiting" << std::endl;
                        return -1;
                    }
                } else
                {
                    std::cout << "autodetected" << std::endl;
                    auto addres = lbmPtr->addSenderSelf();
                    if (addres.has_error()) 
                    {
                        std::cerr << "Unable to add a auto-detected sender address due to error " << addres.error().message() 
                            << ", exiting" << std::endl;
                        return -1;
                    }
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
                sflags.numSendSockets = numSockets;
                sflags.zeroRate = zeroRate;
                sflags.usecAsEventNum = usecAsEventNum;
                sflags.rateGbps = rateGbps;
            }
            std::cout << "Control plane:                 " << (sflags.useCP ? "ON" : "OFF") << std::endl;
            std::cout << "Thread assignment to cores:    " << (vm.count("cores") ? "ON" : "OFF") << std::endl;
            std::cout << "Explicit NUMA memory binding:  " << (numaNode >= 0 ? "ON" : "OFF") << std::endl;
            std::cout << "Event rate reporting in Sync:  " << (sflags.zeroRate ? "OFF" : "ON") << std::endl;
            std::cout << "Using usecs as event numbers:  " << (sflags.usecAsEventNum ? "ON" : "OFF") << std::endl;
            std::cout << (sflags.useCP ? "*** Make sure the LB has been reserved and the URI reflects the reserved instance information." :
                "*** Make sure the URI reflects proper data address, other parts are ignored.") << std::endl;

            try {
                segPtr = new Segmenter(uri, dataId, eventSourceId, coreList, sflags);
                auto res = sendEvents(*segPtr, startingEventNum, numEvents, eventBufferSize, rateGbps);

                if (res.has_error()) {
                    std::cerr << "Segmenter encountered an error: " << res.error().message() << std::endl;
                }
            } catch (E2SARException &e) {
                std::cerr << "Unable to create segmenter: " << static_cast<std::string>(e) << std::endl;
            }
            ctrlCHandler(0);
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
                rflags.useHostAddress = preferHostAddr;
                rflags.validateCert = validate;
            }
            std::cout << "Control plane:                 " << (rflags.useCP ? "ON" : "OFF") << std::endl;
            std::cout << "Thread assignment to cores:    " << (vm.count("cores") ? "ON" : "OFF") << std::endl;
            std::cout << "Explicit NUMA memory binding:  " << (numaNode >= 0 ? "ON" : "OFF") << std::endl;
            std::cout << "Will run for:                  " << (durationSec ? std::to_string(durationSec) + " sec": "until Ctrl-C") << std::endl;

            try {

                if (vm.count("cores"))
                {
                    if (not autoIP)
                    {
                        ip::address ip = ip::make_address(sndrcvIP);
                        reasPtr = new Reassembler(uri, ip, recvStartPort, coreList, rflags);
                    }
                    else
                        reasPtr = new Reassembler(uri, recvStartPort, coreList, rflags);
                } else
                {
                    if (not autoIP)
                    {
                        ip::address ip = ip::make_address(sndrcvIP);
                        reasPtr = new Reassembler(uri, ip, recvStartPort, numThreads, rflags);
                    }
                    else
                        reasPtr = new Reassembler(uri, recvStartPort, numThreads, rflags);
                }

                std::cout << "Using IP address:              " << reasPtr->get_dataIP() << std::endl;
                std::cout << (rflags.useCP ? "*** Make sure the LB has been reserved and the URI reflects the reserved instance information." :
                    "*** Make sure the URI reflects proper data address, other parts are ignored.") << std::endl;

                boost::thread statT(&recvStatsThread, reasPtr);
                auto res = prepareToReceive(*reasPtr);

                if (res.has_error()) {
                    std::cerr << "Reassembler encountered an error: " << res.error().message() << std::endl;
                    ctrlCHandler(0);
                    exit(-1);
                }
                // start dequeue read threads
                boost::thread lastThread;
                for(size_t i=0; i < readThreads; i++)
                {
                    boost::thread syncT(recvEvents, reasPtr, &durationSec);
                    lastThread = std::move(syncT);
                }
                // join the last one
                lastThread.join();
                // unregister/stop threads as needed
                ctrlCHandler(0);
            } catch (E2SARException &e) {
                std::cerr << "Unable to create reassembler: " << static_cast<std::string>(e) << std::endl;
                ctrlCHandler(0);
                exit(-1);
            }
        }
    } else
        // print help
        std::cout << od << std::endl;
}