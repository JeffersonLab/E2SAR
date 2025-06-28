#ifndef E2SARDREASSEMBLERPHPP
#define E2SARDREASSEMBLERPHPP

#include <boost/asio.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/pool/pool.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/unordered_map.hpp>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/any.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/variant.hpp>
#include <boost/heap/priority_queue.hpp>
#include <boost/container/flat_set.hpp>

#include <sys/select.h>

#ifdef NETLINK_AVAILABLE
#include <linux/rtnetlink.h>
#endif

#include <atomic>

#include "e2sarError.hpp"
#include "e2sarUtil.hpp"
#include "e2sarHeaders.hpp"
#include "e2sarNetUtil.hpp"
#include "e2sarCP.hpp"
#include "portable_endian.h"

/***
 * Dataplane definitions for E2SAR Reassembler
*/

namespace e2sar
{
    const size_t RECV_BUFFER_SIZE{9000};
    /*
        The Reassembler class knows how to reassemble the events back. It relies
        on the RE header structure to reassemble the event, because the LB portion
        of LBRE header is stripped off by the load balancer. 

        It runs on or next to the worker performing event processing.
    */
    class Reassembler
    {

        friend class Segmenter;
        private:
            EjfatURI dpuri;
            LBManager lbman;

            // Segementer queue state - we use lockfree queue
            // and an associated atomic variable that reflects
            // how many event entries are in it.

            // Structure to hold each recv-queue item
            struct EventQueueItem {
                boost::chrono::steady_clock::time_point firstSegment; // when first segment arrived
                std::atomic<size_t> numFragments; // how many fragments received (in and out of order)
                size_t bytes;  // total length
                std::atomic<size_t> curBytes; // current bytes accumulated (could be scattered across fragments)
                EventNum_t eventNum;
                u_int8_t *event;
                u_int16_t dataId;

                EventQueueItem(): numFragments{0},  bytes{0}, curBytes{0},  
                    eventNum{0}, event{nullptr}, dataId{0}  {}

                ~EventQueueItem() {}

                EventQueueItem& operator=(const EventQueueItem &i) = delete;

                EventQueueItem(const EventQueueItem &i): firstSegment{i.firstSegment}, 
                    numFragments{i.numFragments.load()},               
                    bytes{i.bytes}, curBytes{i.curBytes.load()}, 
                    eventNum{i.eventNum},   event{i.event},  dataId{i.dataId} {}
                /**
                 * Initialize from REHdr
                 */
                EventQueueItem(REHdr *rehdr): EventQueueItem()
                {
                    this->initFromHeader(rehdr);
                }

                inline void initFromHeader(REHdr *rehdr)
                {
                    bytes = rehdr->get_bufferLength();
                    dataId = rehdr->get_dataId();
                    eventNum = rehdr->get_eventNum();
                    // user deallocates this, so we don't use a pool
                    event = new u_int8_t[rehdr->get_bufferLength()];
                    // set the timestamp
                    firstSegment = boost::chrono::steady_clock::now();  
                }
            };

            // stats block
            struct AtomicStats {
                std::atomic<EventNum_t> enqueueLoss{0}; // number of events received and lost on enqueue
                std::atomic<EventNum_t> reassemblyLoss{0}; // number of events lost in reassembly (missing segments)
                std::atomic<EventNum_t> eventSuccess{0}; // events successfully processed
                // last error code
                std::atomic<int> lastErrno{0};
                // gRPC error count
                std::atomic<int> grpcErrCnt{0};
                // data socket error count
                std::atomic<int> dataErrCnt{0};
                // last e2sar error
                std::atomic<E2SARErrorc> lastE2SARError{E2SARErrorc::NoError};
                // a limited queue to push lost event numbers to
                //boost::lockfree::queue<std::pair<EventNum_t, u_int16_t>*> lostEventsQueue{20};
                boost::lockfree::queue<boost::tuple<EventNum_t, u_int16_t, size_t>*> lostEventsQueue{20};
                // this array is accessed by different threads using fd as an index (so no collisions)
                std::vector<size_t> fragmentsPerFd;
                std::vector<u_int16_t> portPerFd; // which port assigned to this FD - initialized at the start
            };
            AtomicStats recvStats;

            // receive event queue definitions
            static const size_t QSIZE{1000};
            boost::lockfree::queue<EventQueueItem*> eventQueue{QSIZE};
            std::atomic<size_t> eventQueueDepth{0};

            // push event on the common event queue
            // return 1 if event is lost, 0 on success
            inline int enqueue(const std::shared_ptr<EventQueueItem> &item) noexcept
            {
                int ret = 0;
                // get rid of the shared object here
                // lockfree queue uses atomic operations and cannot use shared_ptr type
                auto newItem = new EventQueueItem(*item.get());
                if (eventQueue.push(newItem))
                    eventQueueDepth++;
                else 
                {
                    delete newItem; // the shared ptr object will be released by caller
                    ret = 1; // event lost, queue was full
                }
                // queue is lock free so we don't lock
                recvThreadCond.notify_all();
                return ret;
            }

            // pop event off the event queue
            inline EventQueueItem* dequeue() noexcept
            {
                EventQueueItem* item{nullptr};
                auto a = eventQueue.pop(item);
                if (a) 
                {
                    eventQueueDepth--;
                    return item;
                } else 
                    return nullptr; // queue was empty
            }

            // PID-related parameters
            const u_int32_t epochMs; // length of a schedule epoch - 1 sec
            const float setPoint; 
            const float Kp; // PID proportional
            const float Ki; // PID integral
            const float Kd; // PID derivative
            const float weight; // processing power factor
            const float min_factor;  
            const float max_factor;
            struct PIDSample {
                UnixTimeMicro_t sampleTime; // in usec since epoch
                float error;
                float integral;

                PIDSample(UnixTimeMicro_t st, float er, float intg): 
                    sampleTime{st}, error{er}, integral{intg} {}
            };
            boost::circular_buffer<PIDSample> pidSampleBuffer;

            // have we registered a worker
            bool registeredWorker{false};


            /**
             * This thread does GC for all of the recv threads disposing of partially
             * assembled events that have been there too long.
             */
            struct GCThreadState {
                Reassembler &reas;
                boost::thread threadObj;

                GCThreadState(Reassembler &r): reas{r} {}

                // Go through the list of threads on the recv thread list and
                // do garbage collection on their partially assembled events
                void _threadBody(); 
            };
            GCThreadState gcThreadState;

            /**
             * This thread receives data, reassembles into events and puts them onto the 
             * event queue for getEvent()
             */
            struct RecvThreadState {
                // owner object
                Reassembler &reas;
                boost::thread threadObj;

                // timers
                struct timeval sleep_tv;

                // UDP sockets
                std::vector<int> udpPorts;
                std::vector<int> sockets;
                int maxFdPlusOne;
                fd_set fdSet;

                // object pool from which receive frames come from
                // template parameter is allocator
                //boost::pool<> recvBufferPool{RECV_BUFFER_SIZE};
                // map from <event number, data id> to event queue item (shared pointer)
                // for those items that are in assembly. Note that event
                // is uniquely identified by <event number, data id> and
                // so long as the entropy doesn't change while the event
                // segments are transmitted, they are guarangeed to go
                // to the same port
                boost::unordered_map<std::pair<EventNum_t, u_int16_t>, std::shared_ptr<EventQueueItem>, pair_hash, pair_equal> eventsInProgress;
                // mutex for guarding access to events in progress (recv thread, gc thread)
                boost::mutex evtsInProgressMutex;
                // thread local instance of events we lost
                boost::container::flat_set<std::pair<EventNum_t, u_int16_t>> lostEvents;

                // CPU core ids
                std::vector<int> cpuCoreList;

                // this constructor deliberately uses move semantics for uports
                inline RecvThreadState(Reassembler &r, std::vector<int> &&uports, 
                    const std::vector<int> &ccl): 
                    reas{r}, udpPorts{uports}, cpuCoreList{ccl}
                {
                    sleep_tv.tv_sec = 0;
                    sleep_tv.tv_usec = 10000; // 10 msec max
                }

                inline ~RecvThreadState()
                {
                    //recvBufferPool.purge_memory();
                }

                // open v4/v6 sockets
                result<int> _open();
                // close sockets
                result<int> _close();
                // thread loop
                void _threadBody();

                // log a lost event and add to lost queue for external inspection
                // boolean flag discriminates between enqueue losses (true)
                // and reassembly losses (false)
                inline void logLostEvent(std::shared_ptr<EventQueueItem> item, bool enqueLoss)
                {
                    std::pair<EventNum_t, u_int16_t> evt(item->eventNum, item->dataId);

                    if (lostEvents.contains(evt))
                        return;
                    // this is thread-local
                    lostEvents.insert(evt);
                    // lockfree queue (only takes trivial types)
                    boost::tuple<EventNum_t, u_int16_t, size_t> *evtPtr = 
                        new boost::tuple<EventNum_t, u_int16_t, size_t>(evt.first, evt.second, item->numFragments);
                    reas.recvStats.lostEventsQueue.push(evtPtr);
                    // this is atomic
                    if (enqueLoss)
                        reas.recvStats.enqueueLoss++;
                    else
                        reas.recvStats.reassemblyLoss++;                    
                }
            };
            friend struct RecvThreadState;
            std::list<RecvThreadState> recvThreadState;

            // receive related parameters
            const std::vector<int> cpuCoreList;
            ip::address dataIP; 
            const u_int16_t dataPort; 
            const int portRange; // translates into 2^portRange - 1 ports we listen to
            const size_t numRecvThreads;
            const size_t numRecvPorts;
            std::vector<std::list<int>> threadsToPorts;
            const bool withLBHeader;
            const int eventTimeout_ms; // how long we allow events to linger 'in progress' before we give up
            const int recvWaitTimeout_ms{10}; // how long we wait on condition variable before we come up for air
            // recv socket buffer size for setsockop
            const int rcvSocketBufSize;

            // lock with recv thread
            boost::mutex recvThreadMtx;
            // condition variable for recv thread queue
            boost::condition_variable recvThreadCond;
            // lock for the mutex
            //thread_local boost::unique_lock<boost::mutex> condLock(recvThreadMtx, boost::defer_lock);

            /**
             * Use port range and starting port to assign UDP ports to threads evenly
             */
            inline void assignPortsToThreads()
            {
                // O(numRecvPorts)
                for(size_t i=0; i<numRecvPorts;)
                {
                    for(size_t j=0; i<numRecvPorts && j<numRecvThreads; i++, j++)
                    {
                        threadsToPorts[j].push_back(dataPort + i);
                    } 
                }
            }

            /**
             * This thread sends CP gRPC SendState messages using session token and id
             */
            struct SendStateThreadState {
                // owner object
                Reassembler &reas;
                boost::thread threadObj;

                const u_int16_t period_ms;

                // UDP sockets
                int socketFd{0};

                inline SendStateThreadState(Reassembler &r, u_int16_t period_ms): 
                    reas{r}, period_ms{period_ms}
                {}

                // thread loop. all important behavior is encapsulated inside LBManager
                void _threadBody();
            };
            friend struct sendStateThreadState;
            SendStateThreadState sendStateThreadState;
            bool useCP; // for debugging we may not want to have CP running
            // global thread stop signal
            bool threadsStop{false};

            /**
             * Check the sanity of constructor parameters
             */
            inline void sanityChecks()
            {
                if (numRecvThreads > 128)
                    throw E2SARException("Too many reassembly threads requested, limit 128");

                if (numRecvPorts > (2 << 13))
                    throw E2SARException("Too many receive ports reqiuested, limit 2^14");

                if (eventTimeout_ms > 5000)
                    throw E2SARException("Event timeout exception unreasonably long, limit 5s");

                if (dataPort < 1024)
                    throw E2SARException("Base receive port in the privileged range (<1024)");

                if (portRange > 14)
                    throw E2SARException("Port range out of bounds: [0, 14]");

                if (!dpuri.has_dataAddr())
                    throw E2SARException("Data address not present in the URI");
            }
        public:
            /**
             * Structure in which statistics are reported back to user. 
             *  - EventNum_t enqueueLoss;  // number of events received and lost on enqueue
             *  - EventNum_t reassemblyLoss; // number of events lost in reassembly due to missing segments
             *  - EventNum_t eventSuccess; // events successfully processed
             *  - int lastErrno; // last reported errno (use strerror() to get error message)
             *  - int grpcErrCnt; // number of gRPC errors
             *  - int dataErrCnt; // number of dataplae errors
             *  - E2SARErrorc lastE2SARError; // last recorded E2SAR error (use make_error_code(stats.lastE2SARError).message())
             */
            struct ReportedStats {
                EventNum_t enqueueLoss;  // number of events received and lost on enqueue
                EventNum_t reassemblyLoss; // number of events lost in reassembly due to missing segments
                EventNum_t eventSuccess; // events successfully processed
                int lastErrno; 
                int grpcErrCnt; 
                int dataErrCnt; 
                E2SARErrorc lastE2SARError; 

                ReportedStats() = delete;
                ReportedStats(const AtomicStats &as): enqueueLoss{as.enqueueLoss}, 
                    reassemblyLoss{as.reassemblyLoss}, eventSuccess{as.eventSuccess},
                    lastErrno{as.lastErrno}, grpcErrCnt{as.grpcErrCnt}, dataErrCnt{as.dataErrCnt},
                    lastE2SARError{as.lastE2SARError} 
                    {}
            };

            /**
             * Structure for flags governing Reassembler behavior with sane defaults
             * - useCP - whether to use the control plane (sendState, registerWorker) {true}
             * - useHostAddress - use IPv4 or IPv6 address for gRPC even if hostname is specified (disables cert validation) {false}
             * - period_ms - period of the send state thread in milliseconds {100}
             * - epoch_ms - period of one epoch in milliseconds {1000}
             * - Ki, Kp, Kd - PID gains (integral, proportional and derivative) {0., 0., 0.}
             * - setPoint - setPoint queue occupied percentage to which to drive the PID controller {0.0}
             * - validateCert - validate control plane TLS certificate {true}
             * - portRange - 2^portRange (0<=portRange<=14) listening ports will be open starting from dataPort. If -1, 
             * then the number of ports matches either the number of CPU cores or the number of threads. Normally
             * this value is calculated based on the number of cores or threads requested, but
             * it can be overridden here. Use with caution. {-1}
             * - withLBHeader - expect LB header to be included (mainly for testing, as normally LB strips it off in
             * normal operation) {false}
             * - eventTimeout_ms - how long (in ms) we allow events to remain in assembly before we give up {500}
             * - rcvSocketBufSize - socket buffer size for receiving set via SO_RCVBUF setsockopt. Note
             * that this requires systemwide max set via sysctl (net.core.rmem_max) to be higher. {3MB}
             * - weight - weight given to this node in terms of processing power
             * - min_factor - multiplied with the number of slots that would be assigned evenly to determine min number of slots
             * for example, 4 nodes with a minFactor of 0.5 = (512 slots / 4) * 0.5 = min 64 slots
             * - max_factor - multiplied with the number of slots that would be assigned evenly to determine max number of slots
             * for example, 4 nodes with a maxFactor of 2 = (512 slots / 4) * 2 = max 256 slots set to 0 to specify no maximum
             */
            struct ReassemblerFlags 
            {
                bool useCP;
                bool useHostAddress;
                u_int16_t period_ms;
                bool validateCert;
                float Ki, Kp, Kd, setPoint;
                u_int32_t epoch_ms;
                int portRange; 
                bool withLBHeader;
                int eventTimeout_ms;
                int rcvSocketBufSize; 
                float weight, min_factor, max_factor;
                ReassemblerFlags(): useCP{true}, useHostAddress{false},
                    period_ms{100}, validateCert{true}, Ki{0.}, Kp{0.}, Kd{0.}, setPoint{0.}, 
                    epoch_ms{1000}, portRange{-1}, withLBHeader{false}, eventTimeout_ms{500},
                    rcvSocketBufSize{1024*1024*3}, weight{1.0}, min_factor{0.5}, max_factor{2.0} {}
                /**
                 * Initialize flags from an INI file
                 * @param iniFile - path to the INI file
                 */
                static result<Reassembler::ReassemblerFlags> getFromINI(const std::string &iniFile) noexcept;
            };
            /**
             * Create a reassembler object to run receive on a specific set of CPU cores
             * We assume you picked the CPU core list by studying CPU-to-NUMA affinity for the receiver
             * NIC on the target system. The number of started receive threads will match
             * the number of cores on the list. For the started receive threads affinity will be 
             * set to these cores. 
             * @param uri - EjfatURI with lb_id and instance token, so we can register a worker and then SendState
             * @param data_ip - IP address (v4 or v6) on which we are listening
             * @param starting_port - starting port number on which we are listening
             * @param cpuCoreList - list of core identifiers to be used for receive threads
             * @param rflags - optional ReassemblerFlags structure with additional flags
             */
            Reassembler(const EjfatURI &uri, ip::address data_ip, u_int16_t starting_port,
                        std::vector<int> cpuCoreList, 
                        const ReassemblerFlags &rflags = ReassemblerFlags());
            /**
             * Create a reassembler object to run on a specified number of receive threads
             * without taking into account thread-to-CPU and CPU-to-NUMA affinity.
             * @param uri - EjfatURI with lb_id and instance token, so we can register a worker and then SendState
             * @param data_ip - IP address (v4 or v6) on which we are listening
             * @param starting_port - starting port number on which we are listening
             * @param numRecvThreads - number of threads 
             * @param rflags - optional ReassemblerFlags structure with additional flags
             */
            Reassembler(const EjfatURI &uri, ip::address data_ip, u_int16_t starting_port,
                        size_t numRecvThreads = 1, const ReassemblerFlags &rflags = ReassemblerFlags());

            /**
             * Create a reassembler object to run receive on a specific set of CPU cores
             * We assume you picked the CPU core list by studying CPU-to-NUMA affinity for the receiver
             * NIC on the target system. The number of started receive threads will match
             * the number of cores on the list. For the started receive threads affinity will be 
             * set to these cores. 
             * This method attempts to auto-detect the outgoing IP address 
             * @param uri - EjfatURI with lb_id and instance token, so we can register a worker and then SendState
             * @param starting_port - starting port number on which we are listening
             * @param cpuCoreList - list of core identifiers to be used for receive threads
             * @param rflags - optional ReassemblerFlags structure with additional flags
             * @param v6 - use IPv6 dataplane
             */
            Reassembler(const EjfatURI &uri, u_int16_t starting_port,
                        std::vector<int> cpuCoreList, 
                        const ReassemblerFlags &rflags = ReassemblerFlags(),
                        bool v6 = false);
            /**
             * Create a reassembler object to run on a specified number of receive threads
             * without taking into account thread-to-CPU and CPU-to-NUMA affinity.
             * This method attempts to auto-detect the outgoing IP address 
             * @param uri - EjfatURI with lb_id and instance token, so we can register a worker and then SendState
             * @param starting_port - starting port number on which we are listening
             * @param numRecvThreads - number of threads 
             * @param rflags - optional ReassemblerFlags structure with additional flags
             * @param v6 - use IPv6 dataplane
             */
            Reassembler(const EjfatURI &uri, u_int16_t starting_port,
                        size_t numRecvThreads = 1, 
                        const ReassemblerFlags &rflags = ReassemblerFlags(),
                        bool v6 = false);

            Reassembler(const Reassembler &r) = delete;
            Reassembler & operator=(const Reassembler &o) = delete;
            ~Reassembler()
            {
                if (useCP && registeredWorker)
                    auto res = lbman.deregisterWorker();

                stopThreads();
                recvThreadCond.notify_all();

                // wait to exit
                if (useCP)
                    sendStateThreadState.threadObj.join();

                for(auto i = recvThreadState.begin(); i != recvThreadState.end(); ++i)
                    i->threadObj.join();

                gcThreadState.threadObj.join();

                // pool memory is implicitly freed when pool goes out of scope
            }
            
            /**
             * Register a worker with the control plane 
             * @param node_name - name of this node (any unique string)
             * @return - 0 on success or an error condition
             */
            result<int> registerWorker(const std::string &node_name) noexcept;

            /**
             * Deregister this worker
             * @return - 0 on success or an error condition 
             */ 
            result<int> deregisterWorker() noexcept;

            /**
             * Open sockets and start the threads - this marks the moment
             * from which we are listening for incoming packets, assembling
             * them into event buffers and putting them into the queue.
             * @return - 0 on success, otherwise error condition
             */
            result<int> openAndStart() noexcept;

            /**
             * A non-blocking call to get an assembled event off a reassembled event queue
             * @param event - event buffer
             * @param bytes - size of the event in the buffer
             * @param eventNum - the assembled event number
             * @param dataId - dataId from the reassembly header identifying the DAQ
             * @return - result structure, check has_error() method or value() which is 0
             * on success and -1 if the queue was empty.
             */
            result<int> getEvent(uint8_t **event, size_t *bytes, EventNum_t* eventNum, uint16_t *dataId) noexcept;

            /**
             * Blocking variant of getEvent() with same parameter semantics
             * @param event - event buffer
             * @param bytes - size of the event in the buffer
             * @param eventNum - the assembled event number
             * @param dataId - dataId from the reassembly header identifying the DAQ
             * @param wait_ms - how long to block before giving up, defaults to 0 - forever
             * @return - result structure, check has_error() method or value() which is 0
             */
            result<int> recvEvent(uint8_t **event, size_t *bytes, EventNum_t* eventNum, uint16_t *dataId, u_int64_t wait_ms=0) noexcept;

            /**
             * Get a struct representing all the stats:
             *  - EventNum_t enqueueLoss;  // number of events received and lost on enqueue
             *  - EventNum_t reassemblyLoss; // number of events lost in reassembly due to missing segments
             *  - EventNum_t eventSuccess; // events successfully processed
             *  - int lastErrno; 
             *  - int grpcErrCnt; 
             *  - int dataErrCnt; 
             *  - E2SARErrorc lastE2SARError; 
             */
            inline const ReportedStats getStats() const noexcept
            {
                return ReportedStats(recvStats);
            }

            /**
             * Try to pop an event number of a lost event from the queue that stores them
             * @return result with either (eventNumber,dataId) or E2SARErrorc::NotFound if queue is empty
             */
            inline result<boost::tuple<EventNum_t, u_int16_t, size_t>> get_LostEvent() noexcept
            {
                boost::tuple<EventNum_t, u_int16_t, size_t> *res = nullptr;
                if (recvStats.lostEventsQueue.pop(res))
                {
                    auto ret{*res};
                    delete res;
                    return ret;
                }
                else
                    return E2SARErrorInfo{E2SARErrorc::NotFound, "Lost event queue is empty"};
            }

            /**
             * Get per-port/per-fd fragments received stats. Only call this after the threads have been 
             * stopped, otherwise error is returned. 
             * @return - list of pairs <port, number of received fragments> or error
             */
            inline result<std::list<std::pair<u_int16_t, size_t>>> get_FDStats() noexcept
            {
                if (not threadsStop)
                    return E2SARErrorInfo{E2SARErrorc::LogicError, "This method should only be called after the threads have been stopped."};

                int i{0};
                std::list<std::pair<u_int16_t, size_t>> ret;
                for(auto count: recvStats.fragmentsPerFd)
                {
                    if (recvStats.portPerFd[i] != 0)
                        ret.push_back(std::make_pair<>(recvStats.portPerFd[i], count));
                    ++i;
                }
                return ret;
            }

            /**
             * Get the number of threads this Reassembler is using
             */
            inline const size_t get_numRecvThreads() const noexcept
            {
                return numRecvThreads;
            }

            /**
             * Get the ports this reassembler is listening on, returned as a pair <start port, end port>
             */
            inline const std::pair<int, int> get_recvPorts() const noexcept
            {
                return std::make_pair(dataPort, dataPort + numRecvPorts - 1);
            }

            /**
             * Get the port range that will be communicated to CP (this is either specified explicitly
             * as part of ReassemblerFlags or computed from the number of cores or threads requested)
             */
            inline const int get_portRange() const noexcept
            {
                return portRange;
            }

            /**
             * Get the data IP address to be used for communicating to the dataplane
             */
            inline const ip::address get_dataIP() const noexcept
            {
                return dataIP;
            }
            /**
             * Tell threads to stop. Also causes recvEvent to exit with (-1) 
             */
            void stopThreads() 
            {
                threadsStop = true;
            }
        protected:
        private:



    };
}
#endif
