/** E2SAR-based file sender/receiver */

#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iostream>
#include <sstream>
#include <vector>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>

#include "e2sar.hpp"

namespace po = boost::program_options;
namespace pt = boost::posix_time;
namespace bfs = boost::filesystem;

using namespace e2sar;

bool threadsRunning = true;
u_int16_t reportThreadSleepMs{1000};
Reassembler *reasPtr{nullptr};
Segmenter *segPtr{nullptr};
LBManager *lbmPtr{nullptr};
std::vector<std::string> senders;

// app-level stats
std::atomic<u_int64_t> receivedWithError{0};

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

        auto stats = reasPtr->getStats();
        std::vector<boost::tuple<EventNum_t, u_int16_t, size_t>> lostEvents;

        // get the lost events
        while(true)
        {
            auto res = reasPtr->get_LostEvent();
            if (res.has_error())
                break;
            lostEvents.push_back(res.value());
        }

        std::cout << "Stats:" << std::endl;
        std::cout << "\tEvents Received: " << stats.eventSuccess << std::endl;
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

result<int> prepareToSend(Segmenter *seg)
{
    // start threads, open sockets
    auto openRes = seg->openAndStart();
    if (openRes.has_error())
        return openRes;
    return 0;
}

struct callbackParams {
    void *ptr;
    size_t len;
    int fd;

    callbackParams(void* p, size_t l, int f):ptr{p}, len{l}, fd{f} {}
};

// send callback
void unmapFileCallback(boost::any a) 
{
    auto p = boost::any_cast<callbackParams>(a);
    // unmap and close
    munmap(p.ptr, p.len);
    close(p.fd);
}

/**
 * Send a single file by mmapping it 
 */
result<int> sendFile(Segmenter* s, const bfs::path& path, EventNum_t num) {

    // mmap a file
    // get file permissions and length
    struct stat inFileStat; 

    int fdIn = open(path.c_str(), O_RDONLY);

    if (fdIn < 0)
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to open file"};

    auto inFileRes = fstat(fdIn, &inFileStat);
    if (inFileRes < 0) 
    {
        close(fdIn);
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to stat file"};
    }

    size_t inFileSize = inFileStat.st_size;

    void *inPtr = mmap(NULL, inFileSize, PROT_READ, MAP_PRIVATE, fdIn, 0);

    if (inPtr == MAP_FAILED)
    {
        close(fdIn);
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to mmap file"};
    }

    // put on queue with a callback to free this buffer
    while(true)
    {
        std::cout << "Sending file " << path << "  as event " << num << std::endl;
        auto sendRes = s->addToSendQueue(static_cast<u_int8_t*>(inPtr), inFileSize, 0, 0, 0,
            &unmapFileCallback, callbackParams(inPtr, inFileSize, fdIn));
        if (sendRes.has_error()) 
        {
            if (sendRes.error().code() == E2SARErrorc::MemoryError)
            {
                // unable to send, queue full, try again
                ;
            } else 
            {
                std::cout << "Unexpected error submitting file into the queue: " << sendRes.error().message() << std::endl;
            }
        } else
            break;
    }

    return 0;
}

/**
 * Convenience function for posting to thread pool if extension matches
 */
inline size_t postToThreadPool(Segmenter* seg, const bfs::path& p, EventNum_t evt,
    boost::asio::thread_pool& tp)
{
    boost::asio::post(tp,
        [seg, p, evt]() {
            auto res = sendFile(seg, p, evt);
    });

    return bfs::file_size(p);
}

/**
 * Check that the file can be sent
 */
inline bool checkPath(const bfs::path& p, const std::string &extension)
{

    //std::cout << "Checking path " << p << std::boolalpha << bfs::exists(p) << " " <<
    //    bfs::is_regular_file(p) << " " << bfs::is_directory(p) << " " << p.extension() << " " << extension << std::endl;
    return bfs::exists(p) and
        bfs::is_regular_file(p) and not bfs::is_directory(p) and 
        (extension == "." or (extension != "." and p.extension() == extension));
}

/**
 * Traverse a set of paths looking for files with specific extensions (if provided) optionally
 * recursing into the tree and posting to send thread pool on regular files that match 
 */
result<int> traversePaths(Segmenter* seg, const std::vector<std::string>& filePaths, 
    const std::string& extension, bool recurse, unsigned tpSize)
{

    // create a send thread pool
    boost::asio::thread_pool fileThreadPool(tpSize);
    size_t bytesSent{0};
    EventNum_t evt{0};

    auto sendStartTime = boost::chrono::high_resolution_clock::now();

    for(auto p: filePaths)
    {
        if (bfs::exists(p))
        {
            if (checkPath(p, extension)) 
            {
                bytesSent += postToThreadPool(seg, bfs::path(p), evt++, fileThreadPool);
            } else if (bfs::is_directory(p))
            {
                if (recurse) 
                {
                    BOOST_FOREACH(auto it, bfs::recursive_directory_iterator(p))
                        if (checkPath(it.path(), extension))
                        {
                            bytesSent += postToThreadPool(seg, it.path(), evt++, fileThreadPool);
                        }
                } else
                {
                    BOOST_FOREACH(auto it, bfs::directory_iterator(p))
                        if (checkPath(it.path(), extension))
                        {
                            bytesSent += postToThreadPool(seg, it.path(), evt++, fileThreadPool);
                        }
                }
            }
        }
    }
    fileThreadPool.join();
    seg->stopThreads();

    auto sendEndTime = boost::chrono::high_resolution_clock::now();

    // estimate the goodput
    auto elapsedUsec = boost::chrono::duration_cast<boost::chrono::microseconds>(sendEndTime - sendStartTime);

    std::cout << "Estimated goodput (Gbps): " <<
        (bytesSent * 8.0) / (elapsedUsec.count() * 1000) << std::endl;

    auto stats = seg->getSendStats();

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


result<int> recvFiles(Reassembler *r, const std::string &path, const std::string &prefix, const std::string &extension) {

    u_int8_t *evtBuf{nullptr};
    size_t evtSize;
    EventNum_t evtNum;
    u_int16_t dataId;
    mode_t mode{S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH};

    // receive loop
    while(threadsRunning)
    {
        auto getEvtRes = r->recvEvent(&evtBuf, &evtSize, &evtNum, &dataId, 1000);

        if (getEvtRes.has_error())
        {
            receivedWithError++;
            continue;
        }

        if (getEvtRes.value() == -1)
            continue;

        // generate file name
        std::ostringstream fileName;
        fileName << prefix << "_" << evtNum << "_" << dataId;
        if (extension != ".")
            fileName << extension;
        bfs::path filePath{path};
        // construct a path
        filePath /= fileName.str();

        // create, truncate to size and mmap destination file
        int fdOut = open(filePath.c_str(), O_RDWR|O_CREAT|O_EXCL, mode);
        if (fdOut < 0)
        {
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to open output file"};
        }

        if (ftruncate(fdOut, evtSize) == -1)
        {
            close(fdOut);
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to open output file"};
        }

        void *outPtr = mmap(NULL, evtSize, PROT_WRITE, MAP_SHARED, fdOut, 0);

        if (outPtr == MAP_FAILED)
        {
            close(fdOut);
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to mmap output file"};
        }

        // copy the data into file
        memcpy(outPtr, evtBuf, evtSize);

        // unmap and close
        auto ret = munmap(outPtr, evtSize);
        if (ret == -1)
        {
            close(fdOut);
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to unmap output file"};
        }

        // delete event
        delete[] evtBuf;
        evtBuf = nullptr;
    }
    std::cout << "Completed" << std::endl;
    return 0;
}

int main(int argc, char **argv)
{
    po::options_description od("Command-line options");

    u_int16_t mtu;
    size_t numThreads, numSockets;
    float rateGbps;
    int sockBufSize;
    std::string sndrcvIP;
    u_int16_t recvStartPort;
    std::vector<int> coreList;
    std::vector<std::string> optimizations;
    int numaNode;
    bool withCP, autoIP, recurse, smooth;
    u_int32_t eventSourceId;
    u_int16_t dataId;
    std::vector<std::string> filePaths;
    std::string fileExtension, filePrefix;
    size_t writeThreads, readThreads;

    auto opts = od.add_options()("help,h", "show this help message");

    // parameters
    opts("send,s", "send files");
    opts("recv,r", "receive receive files");

    opts("uri,u", po::value<std::string>(), "specify EJFAT_URI on the command-line instead of the environment variable");
    opts("mtu,m", po::value<u_int16_t>(&mtu)->default_value(1500), "MTU (default 1500) [s]");
    opts("threads", po::value<size_t>(&numThreads)->default_value(1), "number of receive threads (defaults to 1) [r]");
    opts("sockets", po::value<size_t>(&numSockets)->default_value(4), "number of send sockets (defaults to 4) [r]");
    opts("rate", po::value<float>(&rateGbps)->default_value(1.0), "send rate in Gbps (defaults to 1.0, negative value means no limit)");
    opts("src", po::value<u_int32_t>(&eventSourceId)->default_value(1234), "Event source (default 1234) [s]");
    opts("dataid", po::value<u_int16_t>(&dataId)->default_value(4321), "Data id (default 4321) [s]");
    opts("withcp,c", po::bool_switch()->default_value(false), "enable control plane interactions");
    opts("bufsize,b", po::value<int>(&sockBufSize)->default_value(1024*1024*3), "send or receive socket buffer size (default to 3MB)");
    opts("ip", po::value<std::string>(&sndrcvIP)->default_value(""), "IP address (IPv4 or IPv6) from which sender sends from or on which receiver listens (conflicts with --autoip) [s,r]");
    opts("port", po::value<u_int16_t>(&recvStartPort)->default_value(10000), "Starting UDP port number on which receiver listens. Defaults to 10000. [r] ");
    opts("ipv6,6", "force using IPv6 control plane address if URI specifies hostname (disables cert validation) [s,r]");
    opts("ipv4,4", "force using IPv4 control plane address if URI specifies hostname (disables cert validation) [s,r]");
    opts("novalidate,v", "don't validate server certificate [s,r]");
    opts("autoip", po::bool_switch()->default_value(false), "auto-detect dataplane outgoing ip address (conflicts with --ip; doesn't work for reassembler in back-to-back testing) [s,r]");
    opts("cores", po::value<std::vector<int>>(&coreList)->multitoken(), "optional list of cores to bind sender or receiver threads to; number of receiver threads is equal to the number of cores [s,r]");
    opts("optimize,o", po::value<std::vector<std::string>>(&optimizations)->multitoken(), "a list of optimizations to turn on [s]");
    opts("numa", po::value<int>(&numaNode)->default_value(-1), "bind all memory allocation to this NUMA node (if >= 0) [s,r]");
    opts("path,p", po::value<std::vector<std::string>>(&filePaths)->multitoken(), "path containing the files need to be sent or to save to. For send more than one can be specified, for receive only the first path is used. Files can be narrowed down by extension [s]");
    opts("extension,e", po::value<std::string>(&fileExtension), "extension of the files on the path that need to be sent or created [s,r]");
    opts("deq", po::value<size_t>(&writeThreads)->default_value(1), "number of dequeue threads in receiver writing files (defaults to 1) [r]");
    opts("enq", po::value<size_t>(&readThreads)->default_value(1), "number of enqueue threads in sender reading files (defaults to 1) [s]");
    opts("recurse", po::bool_switch()->default_value(false), "recurse into specified directories looking for files [s]");
    opts("prefix", po::value<std::string>(&filePrefix)->default_value("e2sar_out"), "prefix of the files to create [r]");
    opts("smooth", po::bool_switch()->default_value(false), "use smooth shaping in the sender (only works without optimizations and at low sub 3-5Gbps rates!) [s]");

    po::positional_options_description p;
    // path is a positional argument as well
    p.add("path", -1);

    po::variables_map vm;

    try {
        po::store(po::command_line_parser(argc, argv).
          options(od).positional(p).run(), vm);
        po::notify(vm);
    } catch (const boost::program_options::error &e) {
        std::cout << "Unable to parse command line: " << e.what() << std::endl;
        return -1;
    }

    try
    {
        conflicting_options(vm, "send", "recv");
        conflicting_options(vm, "recv", "num");
        conflicting_options(vm, "recv", "mtu");
        conflicting_options(vm, "recv", "rate");
        conflicting_options(vm, "recv", "src");
        conflicting_options(vm, "recv", "dataid");
        conflicting_options(vm, "send", "threads");
        conflicting_options(vm, "ipv4", "ipv6");
        conflicting_options(vm, "recv", "smooth");
        option_dependency(vm, "recv", "ip");
        option_dependency(vm, "recv", "port");
        option_dependency(vm, "send", "ip");
        option_dependency(vm, "send", "path");
        option_dependency(vm, "recv", "path");
        // these are optional
        conflicting_options(vm, "send", "port");
        conflicting_options(vm, "deq", "send");
        conflicting_options(vm, "enq", "recv");
        conflicting_options(vm, "cores", "threads");
        conflicting_options(vm, "cores", "sockets");
        conflicting_options(vm, "recv", "sockets");
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

        std::cout << "File paths to traverse can be specified as a space-separated list. Globbing can be used as well" << std::endl;
        std::cout << "keeping in mind the length of the command line is limited" << std::endl;
        std::cout << "Send examples:\n $ e2sar_ft -s -u 'ejfats://token@ctrl-plane:18347/id/5/?sync=...' --withcp --extension root /path/to/root/" << std::endl;
        std::cout << "e2sar_ft -s -u 'ejfats://token@ctrl-plane:18347/id/5/?sync=...' --withcp /path/to/root/*.root" << std::endl;
        std::cout << "e2sar_ft -s -u 'ejfats://token@ctrl-plane:18347/id/5/?sync=...' --withcp --extension root --recurse /path/to/root/" << std::endl;
        std::cout << "Receive examples:\n e2sar_ft -r -u 'ejfats://token@ctrl-plane:18347/id/5/?sync=...' --withcp --extension root /path/to/save" << std::endl;
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
    autoIP = vm["autoip"].as<bool>();
    recurse  = vm["recurse"].as<bool>();
    smooth = vm["smooth"].as<bool>();

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

    if (rateGbps < 0. and smooth)
    {
        std::cerr << "Smoothing turned on, while the rate is unlimited." << std::endl;
        return -1;
    }
    // prepend extension with a '.'
    fileExtension.insert(fileExtension.begin(), '.');

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
            sflags.useCP = withCP; 
            sflags.mtu = mtu;
            sflags.sndSocketBufSize = sockBufSize;
            sflags.numSendSockets = numSockets;
            sflags.rateGbps = rateGbps; // unlimited
            sflags.smooth = smooth;

            std::cout << "Control plane:                 " << (sflags.useCP ? "ON" : "OFF") << std::endl;
            std::cout << "Per frame rate smoothing:      " << (sflags.smooth ? "ON" : "OFF") << std::endl;
            std::cout << "Thread assignment to cores:    " << (vm.count("cores") ? "ON" : "OFF") << std::endl;
            std::cout << "Sending sockets/threads:       " << numSockets << std::endl;
            std::cout << "Enqueue file reading threads:  " << readThreads << std::endl;
            std::cout << "Explicit NUMA memory binding:  " << (numaNode >= 0 ? "ON" : "OFF") << std::endl;
            std::cout << (sflags.useCP ? "*** Make sure the LB has been reserved and the URI reflects the reserved instance information." :
                "*** Make sure the URI reflects proper data address, other parts are ignored.") << std::endl;

            try {
                if (vm.count("cores"))
                    segPtr = new Segmenter(uri, dataId, eventSourceId, coreList, sflags);
                else
                    segPtr = new Segmenter(uri, dataId, eventSourceId, sflags);

                auto openRes = prepareToSend(segPtr);
                if (openRes.has_error()) {
                    std::cerr<< "Segmenter unable to open ports: " << openRes.error().message() << std::endl;
                    return -1;
                }

                auto res = traversePaths(segPtr, filePaths, fileExtension, recurse, readThreads);

                if (res.has_error()) {
                    std::cerr << "Segmenter encountered an error: " << res.error().message() << std::endl;
                }
            } catch (E2SARException &e) {
                std::cerr << "Unable to create segmenter: " << static_cast<std::string>(e) << std::endl;
                ctrlCHandler(0);
                exit(-1);
            }
            ctrlCHandler(0);
        } else if (vm.count("recv")) {
            Reassembler::ReassemblerFlags rflags;

            // test path for writability
            if (filePaths.size() != 1) {
                std::cerr << "Only one output path must be specified" << std::endl;
                exit(-1);
            }

            if (not bfs::is_directory(filePaths[0])) {
                std::cerr << "Path must be a directory" << std::endl;
                exit(-1);
            }

            struct stat outDirStat{}; 
            auto sres = stat(filePaths[0].c_str(), &outDirStat);
            if (sres < 0) {
                std::cerr << "Unable to stat output directory " << filePaths[0] << ": " << strerror(errno) << std::endl;
                exit(-1);
            }

            if (((outDirStat.st_mode & S_IRWXU) != S_IRWXU) and 
                ((outDirStat.st_mode & S_IRWXG) != S_IRWXG) and 
                ((outDirStat.st_mode & S_IRWXO) != S_IRWXO)) {
                std::cerr << "Output directory " << filePaths[0] << " not writable by user" << std::endl;
                exit(-1);
            }

            rflags.useCP = withCP;
            rflags.withLBHeader = not withCP;
            rflags.rcvSocketBufSize = sockBufSize;
            rflags.useHostAddress = preferHostAddr;
            rflags.validateCert = validate;
            std::cout << "Control plane:                 " << (rflags.useCP ? "ON" : "OFF") << std::endl;
            std::cout << "Thread assignment to cores:    " << (vm.count("cores") ? "ON" : "OFF") << std::endl;
            std::cout << "Explicit NUMA memory binding:  " << (numaNode >= 0 ? "ON" : "OFF") << std::endl;

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

                auto res = prepareToReceive(*reasPtr);

                if (res.has_error()) {
                    std::cerr << "Reassembler encountered an error: " << res.error().message() << std::endl;
                    ctrlCHandler(0);
                    exit(-1);
                }
                // start dequeue read threads
                boost::thread lastThread;
                for(size_t i=0; i < writeThreads; i++)
                {
                    boost::thread syncT(recvFiles, reasPtr, filePaths[0], filePrefix, fileExtension);
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