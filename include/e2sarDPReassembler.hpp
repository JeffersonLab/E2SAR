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

#ifdef AFFINITY_AVAILABLE
#include <sched.h>
#include <unistd.h>
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

            // hold one receive buffer
            struct Segment {
                u_int8_t *segment; // start of the received buffer including headers
                size_t length; // length of useful payloads minus REHdr (and LBHdr if appropriate)
                Segment(): segment{nullptr}, length{0} {}
                Segment(u_int8_t *s, size_t l): segment{s}, length{l} {}
            };

            // custom comparator of REHdr buffers/segments - placing outside the event queue 
            // item class just not to confuse things - needed for priority queue
            // orders segments in the increasing order of offset
            struct SegmentComparator {
                bool operator() (const Segment &l, const Segment &r) const {
                    REHdr *lhdr = reinterpret_cast<REHdr*>(l.segment);
                    REHdr *rhdr = reinterpret_cast<REHdr*>(r.segment);
                    return lhdr->bufferOffset > rhdr->bufferOffset;
                }   
            };

            // Structure to hold each recv-queue item
            struct EventQueueItem {
                boost::chrono::steady_clock::time_point firstSegment; // when first segment arrived
                size_t bytes;  // total length
                size_t curEnd; // current end during reassembly
                EventNum_t eventNum;
                u_int8_t *event;
                u_int16_t dataId;
                boost::heap::priority_queue<Segment, 
                    boost::heap::compare<SegmentComparator>> oodQueue; // out-of-order segments in a priority queue
                EventQueueItem(): bytes{0}, curEnd{0}, eventNum{0}, event{nullptr}, dataId{0} {}
                /**
                 * Initialize from REHdr
                 */
                EventQueueItem(REHdr *rehdr): curEnd{0}
                {
                    bytes = rehdr->get_bufferLength();
                    dataId = rehdr->get_dataId();
                    eventNum = rehdr->get_eventNum();
                    // use deallocates this, so we don't use a pool
                    event = new u_int8_t[rehdr->get_bufferLength()];
                    // set the timestamp
                    firstSegment = boost::chrono::steady_clock::now();
                }
                // not a proper destructor on purpose
                void cleanup(boost::pool<> &rbp)
                {
                    // clean up memory
                    while(!oodQueue.empty()) 
                    {
                        auto i = oodQueue.top();
                        rbp.free(i.segment);
                        oodQueue.pop();     
                    }
                }
            };

            // stats block
            struct AtomicStats {
                std::atomic<EventNum_t> enqueueLoss{0}; // number of events received and lost on enqueue
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
                boost::lockfree::queue<std::pair<EventNum_t, u_int16_t>*> lostEventsQueue{20};
            };
            AtomicStats recvStats;

            // receive event queue definitions
            static const size_t QSIZE{1000};
            boost::lockfree::queue<EventQueueItem*> eventQueue{QSIZE};
            std::atomic<size_t> eventQueueDepth{0};

            // push event on the common event queue
            // return 1 if event is lost, 0 on success
            inline int enqueue(EventQueueItem* item) noexcept
            {
                int ret = 0;
                if (eventQueue.push(item))
                    eventQueueDepth++;
                else 
                    ret = 1; // event lost, queue was full
                // queue is lock free so we don't lock
                recvThreadCond.notify_all();
                return ret;
            }

            // pop event off the event queue
            inline EventQueueItem* dequeue() noexcept
            {
                EventQueueItem *item{nullptr};
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
                boost::pool<> recvBufferPool{RECV_BUFFER_SIZE};
                // map from <event number, data id> to event queue item
                // for those items that are in assembly. Note that event
                // is uniquely identified by <event number, data id> and
                // so long as the entropy doesn't change while the event
                // segments are transmitted, they are guarangeed to go
                // to the same port
                boost::unordered_map<std::pair<EventNum_t, u_int16_t>, EventQueueItem*, pair_hash, pair_equal> eventsInProgress;
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
                    recvBufferPool.purge_memory();
                }

                // open v4/v6 sockets
                result<int> _open();
                // close sockets
                result<int> _close();
                // thread loop
                void _threadBody();

                // log a lost event via a set of known lost
                // and add to lost queue for external inspection
                inline void logLostEvent(std::pair<EventNum_t, u_int16_t> evt)
                {
                    if (lostEvents.contains(evt))
                        return;
                    // this is thread-local
                    lostEvents.insert(evt);
                    // lockfree queue (only takes trivial types)
                    std::pair<EventNum_t, u_int16_t> *evtPtr = new std::pair<EventNum_t, u_int16_t>(evt.first, evt.second);
                    reas.recvStats.lostEventsQueue.push(evtPtr);
                    // this is atomic
                    reas.recvStats.enqueueLoss++;
                }
            };
            friend struct RecvThreadState;
            std::list<RecvThreadState> recvThreadState;

            // receive related parameters
            const std::vector<int> cpuCoreList;
            const ip::address dataIP; 
            const u_int16_t dataPort; 
            const int portRange; // translates into 2^portRange - 1 ports we listen to
            const size_t numRecvThreads;
            const size_t numRecvPorts;
            std::vector<std::list<int>> portsToThreads;
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
                        portsToThreads[j].push_back(dataPort + i);
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
             * Get a tuple representing all the stats:
             *  - EventNum_t enqueueLoss;  // number of events received and lost on enqueue
             *  - EventNum_t eventSuccess; // events successfully processed
             *  - int lastErrno; 
             *  - int grpcErrCnt; 
             *  - int dataErrCnt; 
             *  - E2SARErrorc lastE2SARError; 
             */
            inline const boost::tuple<EventNum_t, EventNum_t, int, int, int, E2SARErrorc> getStats() const noexcept
            {
                return boost::make_tuple<EventNum_t, EventNum_t, int, int, int, E2SARErrorc>(
                    recvStats.enqueueLoss, recvStats.eventSuccess,
                    recvStats.lastErrno, recvStats.grpcErrCnt, recvStats.dataErrCnt,
                    recvStats.lastE2SARError);
            }

            /**
             * Try to pop an event number of a lost event from the queue that stores them
             * @return result with either (eventNumber,dataId) or E2SARErrorc::NotFound if queue is empty
             */
            inline result<std::pair<EventNum_t, u_int16_t>> get_LostEvent() noexcept
            {
                std::pair<EventNum_t, u_int16_t> *res = nullptr;
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
             * Tell threads to stop. Also causes recvEvent to exit with (-1) 
             */
            void stopThreads() 
            {
                threadsStop = true;
            }
        protected:
        private:

            result<int> setAffinity()
            {
#ifdef AFFINITY_AVAILABLE
                // set this process affinity to the indicated set of cores
                cpu_set_t set;
                CPU_ZERO(&set);

                for(auto core: cpuCoreList)
                    CPU_SET(core, &set);
                if (sched_setaffinity(0, sizeof(set), &set) == -1)
                    return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(errno)};
                return 0;
#else
                return E2SARErrorInfo{E2SARErrorc::SystemError, "Setting thread affinity not supported on this system"};
#endif
            }

    };
}
#endif
