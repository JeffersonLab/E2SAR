#ifndef E2SARDSEGMENTERPHPP
#define E2SARDSEGMENTERPHPP

#include <sys/types.h>
#include <sys/socket.h>

#ifdef LIBURING_AVAILABLE
#include <liburing.h>
#endif

#include <boost/asio.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/pool/pool.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/any.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/variant.hpp>
#include <boost/random.hpp>
#include <boost/chrono.hpp>

#include <atomic>

#include "e2sar.hpp"
#include "e2sarUtil.hpp"
#include "e2sarHeaders.hpp"
#include "e2sarNetUtil.hpp"
#include "portable_endian.h"

/***
 * Dataplane definitions for E2SAR Segmenter
*/

namespace e2sar
{
    /*
        The Segmenter class knows how to break up the provided
        events into segments consumable by the hardware loadbalancer.
        It relies on header structures to segment into UDP packets and 
        follows other LB rules while doing it.

        It runs on or next to the source of events.
    */
    class Segmenter
    {
        friend class Reassembler;
        private:
            EjfatURI dpuri;
            // unique identifier of the originating segmentation
            // point (e.g. a DAQ), carried in RE header (could be persistent
            // as set here, or specified per event buffer)
            const u_int16_t dataId;
            // unique identifier of an individual LB packet transmitting
            // host/daq, 32-bit to accommodate IP addresses more easily
            // carried in Sync header
            const u_int32_t eventSrcId;

            // number of send sockets we will be using (to help randomize LAG ports on FPGAs)
            const size_t numSendSockets;

            // send socket buffer size for setsockop
            const int sndSocketBufSize;

            // send rate (ignore if negative)
            const float rateGbps;
            // used to avoid floating point comparisons, set to false if rateGbps <= 0
            const bool rateLimit;
            // use multiple destination ports (for back-to-back testing only)
            const bool multiPort;

            // Max size of internal queue holding events to be sent. 
            static constexpr size_t QSIZE{2047};

            // size of CQE batch we peek
            static constexpr unsigned cqeBatchSize{100};

            // how long data send thread spends sleeping
            static constexpr boost::chrono::milliseconds sleepTime{1};

            // Structure to hold each send-queue item
            struct EventQueueItem {
                uint32_t bytes;
                EventNum_t eventNum;
                u_int16_t dataId;
                u_int8_t *event;
                u_int16_t entropy;  // optional per event entropy
                void (*callback)(boost::any);
                boost::any cbArg;
            };

            // Fast, lock-free, wait-free queue (supports multiple producers/consumers)
            boost::lockfree::queue<EventQueueItem*> eventQueue{QSIZE};

#ifdef LIBURING_AVAILABLE
            struct io_uring ring;
            // each ring has to have a predefined size - we want to
            // put at least 2*eventSize/bufferSize entries onto it
            const size_t uringSize = 1000;

            // we need to be able to call the callback from the CQE thread
            // instead of send thread when liburing optimization is turned on
            struct SQEUserData 
            {
                struct msghdr* msghdr;
                void (*callback)(boost::any);
                boost::any cbArg;
            };
#endif

            // structure that maintains send stats
            struct SendStats {
                // Last time a sync message sent to CP in nanosec since epoch.
                UnixTimeNano_t lastSyncTimeNanos;
                // Number of events sent since last sync message sent to CP. 
                EventNum_t eventsSinceLastSync; 
            };
            // event metadata fifo to keep track of stats
            boost::circular_buffer<SendStats> eventStatsBuffer;
            // keep these atomic, as they are accessed by Sync, Send (and maybe main) thread
            boost::atomic<UnixTimeNano_t> currentSyncStartNano{0};
            boost::atomic<EventNum_t> eventsInCurrentSync{0};

            // currently user-assigned or sequential event number at enqueuing and reported in RE header
            boost::atomic<EventNum_t> userEventNum{0};

            // fast random number generator 
            boost::random::ranlux24_base ranlux;
            // to get better entropy in usec clock samples (if needed)
            boost::random::uniform_int_distribution<> lsbDist{0, 255};

            // we RR through these FDs for send thread _send
            size_t roundRobinIndex{0};

            /**
             * Internal structure of atomic counters to maintain stats on sending
             * and sync messages
             */
            struct AtomicStats {
                // sync messages sent
                std::atomic<u_int64_t> msgCnt{0}; 
                // sync errors seen on send
                std::atomic<u_int64_t> errCnt{0};
                // last error code
                std::atomic<int> lastErrno{0};
                // last e2sar error
                std::atomic<E2SARErrorc> lastE2SARError{E2SARErrorc::NoError};
            };
            // independent stats for each thread
            AtomicStats syncStats;
            AtomicStats sendStats;

            /** 
             * This thread sends a sync header every pre-specified number of milliseconds.
             */
            struct SyncThreadState {
                // owner object
                Segmenter &seg;
                boost::thread threadObj;
                // period in ms
                const u_int16_t period_ms{100};
                // connect socket flag (usually true)
                const bool connectSocket{true};
                // sockaddr_in[6] union (use boost::get<sockaddr_in> or
                // boost::get<sockaddr_in6> to get to the appropriate structure)
#define GET_V4_SYNC_STRUCT(sas) boost::get<sockaddr_in>(sas)
#define GET_V6_SYNC_STRUCT(sas) boost::get<sockaddr_in6>(sas)
                boost::variant<sockaddr_in, sockaddr_in6> syncAddrStruct;
                // flag that tells us we are v4 or v6
                bool isV6{false};
                // UDP sockets
                int socketFd{0};

                inline SyncThreadState(Segmenter &s, u_int16_t time_period_ms, bool cnct=true): 
                    seg{s}, 
                    period_ms{time_period_ms},
                    connectSocket{cnct}
                    {}

                result<int> _open();
                result<int> _close();
                result<int> _send(SyncHdr *hdr);
                void _threadBody();


            };
            friend struct SyncThreadState;

            SyncThreadState syncThreadState;

            /**
             * This thread sends data to the LB to be distributed to different processing
             * nodes.
             */
            struct SendThreadState {
                // owner object
                Segmenter &seg;
                boost::thread threadObj;
                // thread index (to help pick core)
                int threadIndex;
                // connect socket flag (usually true)
                const bool connectSocket{true};

                // flags
                const bool useV6;

                // transmit parameters
                size_t mtu{0}; // must accommodate typical IP, UDP, LB+RE headers and payload; not a const because we may change it
                std::string iface{""}; // outgoing interface - we may set it if possible
                size_t maxPldLen; // not a const because mtu is not a const

                // UDP sockets and matching sockaddr structures (local and remote)
                // <socket fd, local address, remote address>
#define GET_FD(sas, i) boost::get<0>(sas[i])
#define GET_LOCAL_SEND_STRUCT(sas,i) boost::get<1>(sas[i])
#define GET_REMOTE_SEND_STRUCT(sas, i) boost::get<2>(sas[i])
                std::vector<boost::tuple<int, sockaddr_in, sockaddr_in>> socketFd4;
                std::vector<boost::tuple<int, sockaddr_in6, sockaddr_in6>> socketFd6;

                // fast random number generator to create entropy values for events
                // this entropy value is held the same for all packets of a given
                // event guaranteeing the same destination UDP port for all of them
                boost::random::ranlux24_base ranlux;
                boost::random::uniform_int_distribution<> randDist{0, std::numeric_limits<u_int16_t>::max()};
                // to get random port numbers we skip low numbered privileged ports
                boost::random::uniform_int_distribution<> portDist{10000, std::numeric_limits<u_int16_t>::max()};

                inline SendThreadState(Segmenter &s, int idx, bool v6, u_int16_t mtu, bool cnct=true): 
                    seg{s}, threadIndex{idx}, connectSocket{cnct}, useV6{v6}, mtu{mtu}, 
                    maxPldLen{mtu - TOTAL_HDR_LEN}, socketFd4(s.numSendSockets), 
                    socketFd6(s.numSendSockets), ranlux{static_cast<u_int32_t>(std::time(0))} 
                {
                    // this way every segmenter send thread has a unique PRNG sequence
                    auto nowT = boost::chrono::system_clock::now();
                    ranlux.seed(boost::chrono::duration_cast<boost::chrono::nanoseconds>(nowT.time_since_epoch()).count());
                }

                // open v4/v6 sockets
                result<int> _open();
                // close sockets
                result<int> _close();
                // fragment and send the event
                result<int> _send(u_int8_t *event, size_t bytes, EventNum_t altEventNum, u_int16_t dataId, 
                    u_int16_t entropy, size_t roundRobinIndex, 
                    void (*callback)(boost::any) = nullptr, boost::any cbArg = nullptr);
                // thread loop
                void _threadBody();
            };
            friend struct SendThreadState;

            SendThreadState sendThreadState;
            const size_t numSendThreads{1};
            // list of cores we can use to run threads
            // can be longer than the number of threads
            // thread at index i uses core cpuCoreList[i]
            // we don't check cores are unique
            const std::vector<int> cpuCoreList;

#ifdef LIBURING_AVAILABLE
            // this thread reaps completions off the uring
            // and updates stat counters
            struct CQEThreadState {
                // owner object
                Segmenter &seg;
                boost::thread threadObj;

                inline CQEThreadState(Segmenter &s): seg{s} 
                {
                    ;
                }
                void _threadBody();
            };
            friend struct CQEThreadState;

            CQEThreadState cqeThreadState;

            // between send and CQE reaping thread
            boost::mutex cqeThreadMtx;
            // condition variable for CQE thread waiting on new submissions
            boost::condition_variable cqeThreadCond;
            // wait time in ms before CQE thread checks if its time to stop
            // we can't wait for too long - it only takes about 300usec
            // to exhaust 256 SQEs using 1500 byte MTU at 10Gbps
            static constexpr boost::chrono::microseconds cqeWaitTime{200};
            // this is the sleep time for kernel thread in poll mode
            // it is in milliseconds
            static constexpr unsigned pollWaitTime{2000};
            // atomic counter of outstanging sends
            boost::atomic<u_int32_t> outstandingSends{0};
#endif

            // lock with send thread
            boost::mutex sendThreadMtx;
            // condition variable for send thread
            //boost::condition_variable sendThreadCond;
            // warm up period in MS between sync thread starting and data being allowed to be sent
            u_int16_t warmUpMs;
            // use control plane (can be disabled for debugging)
            bool useCP;
#define MIN_CLOCK_ENTROPY 6
            bool addEntropy;

            /**
             * Check the sanity of constructor parameters
             */
            inline void sanityChecks()
            {
                if (numSendSockets > 128)
                    throw E2SARException("Too many sending sockets threads requested, limit 128");

                if (syncThreadState.period_ms > 10000)
                    throw E2SARException("Sync period too long, limit 10s");

                if (sendThreadState.mtu > 9000)
                    throw E2SARException("MTU set too long, limit 9000");

                if (useCP and not dpuri.has_syncAddr())
                    throw E2SARException("Sync address not present in the URI");

                if (not dpuri.has_dataAddr())
                    throw E2SARException("Data address is not present in the URI");

                if (sendThreadState.mtu <= TOTAL_HDR_LEN)
                    throw E2SARErrorInfo{E2SARErrorc::SocketError, "Insufficient MTU length to accommodate headers"};
            }

            /** Threads keep running while this is false */
            bool threadsStop{false};
        public:
            /**
             * Structure in which statistics are reported back to user, sync
             * and send stats are identical so same structure is used.
             *  - u_int64_t msgCnt; // fragments sent
             *  - u_int64_t errCnt; // errors encountered on send
             *  - int lastErrno; // last errno recorded, use strerror() to get error message
             *  - E2SARErrorc lastE2SARError; // last recorded E2SAR error (use make_error_code(stats.lastE2SARError).message())
             */
            struct ReportedStats {
                u_int64_t msgCnt;
                u_int64_t errCnt;
                int lastErrno;
                E2SARErrorc lastE2SARError;

                ReportedStats() = delete;
                ReportedStats(const AtomicStats &as): msgCnt{as.msgCnt}, errCnt{as.errCnt},
                    lastErrno{as.lastErrno}, lastE2SARError{as.lastE2SARError}
                    {}
            };

            /** 
             * Because of the large number of constructor parameters in Segmenter
             * we make this a structure with sane defaults
             * - dpV6 - prefer V6 dataplane if the URI specifies both data=<ipv4>&data=<ipv6> addresses {false}
             * - connectedSocket - use connected sockets {true}
             * - useCP - enable control plane to send Sync packets {true}
             * - warmUpMs - a period of sending sync messages before data is allowed {1000}
             * - syncPeriodMs - sync thread period in milliseconds {1000}
             * - syncPerods - number of sync periods to use for averaging reported send rate {2}
             * - mtu - size of the MTU to attempt to fit the segmented data in (must accommodate
             * IP, UDP and LBRE headers). Value of 0 means auto-detect based on MTU of outgoing interface
             * - Linux only {1500}
             * - numSendSockets - number of sockets/source ports we will be sending data from. The
             * more, the more randomness the LAG will see in delivering to different FPGA ports. {4}
             * - sndSocketBufSize - socket buffer size for sending set via SO_SNDBUF setsockopt. Note
             * that this requires systemwide max set via sysctl (net.core.wmem_max) to be higher. {3MB}
             * - rateGbps - send rate as floating point expression in Gbps. Negative value means unlimited. {-1.0}
             * - multiPort - use numSendSockets consecutive destination ports starting from EjfatURI data port, 
             * rather than a single port; source ports are still randomized (incompatible with a load balancer, 
             * only useful in back-to-back testing) {false}
             */
            struct SegmenterFlags 
            {
                bool dpV6; 
                bool connectedSocket;
                bool useCP;
                u_int16_t warmUpMs;
                u_int16_t syncPeriodMs;
                u_int16_t syncPeriods;
                u_int16_t mtu;
                size_t numSendSockets;
                int sndSocketBufSize;
                float rateGbps; 
                bool multiPort; 

                SegmenterFlags(): dpV6{false}, connectedSocket{true},
                    useCP{true}, warmUpMs{1000}, syncPeriodMs{1000}, syncPeriods{2}, mtu{1500},
                    numSendSockets{4}, sndSocketBufSize{1024*1024*3}, rateGbps{-1.0}, multiPort{false} {}
                /**
                 * Initialize flags from an INI file
                 * @param iniFile - path to the INI file
                 */
                static result<SegmenterFlags> getFromINI(const std::string &iniFile) noexcept;
            };
            /**
             * Initialize segmenter state. Call openAndStart() to begin operation.
             * @param uri - EjfatURI initialized for sender with sync address and data address(es)
             * @param dataId - unique identifier of the originating segmentation point (e.g. a DAQ), 
             * carried in SAR header
             * @param eventSrcId - unique identifier of an individual LB packet transmitting host/daq, 
             * 32-bit to accommodate IP addresses more easily, carried in Sync header
             * @param cpuCoreList - list of cores that can be used by sending threads
             * @param sflags - SegmenterFlags 
             */
            Segmenter(const EjfatURI &uri, u_int16_t dataId, u_int32_t eventSrcId,  
                std::vector<int> cpuCoreList,
                const SegmenterFlags &sflags=SegmenterFlags());

            /**
             * Initialize segmenter state. Call openAndStart() to begin operation.
             * @param uri - EjfatURI initialized for sender with sync address and data address(es)
             * @param dataId - unique identifier of the originating segmentation point (e.g. a DAQ), 
             * carried in SAR header
             * @param eventSrcId - unique identifier of an individual LB packet transmitting host/daq, 
             * 32-bit to accommodate IP addresses more easily, carried in Sync header
             * @param sflags - SegmenterFlags 
             */
            Segmenter(const EjfatURI &uri, u_int16_t dataId, u_int32_t eventSrcId,  
                const SegmenterFlags &sflags=SegmenterFlags());

            /**
             * Don't want to be able to copy these objects normally
             */
            Segmenter(const Segmenter &s) = delete;
            /**
             * Don't want to be able to copy these objects normally
             */
            Segmenter & operator=(const Segmenter &o) = delete;

            /**
             * Close the sockets, free/purge memory pools and stop the threads.
             */
            ~Segmenter()
            {
                stopThreads();
#ifdef LIBURING_AVAILABLE
                // release CQE thread one last time so it can quit in peace
                if (Optimizations::isSelected(Optimizations::Code::liburing_send))
                    cqeThreadCond.notify_all();
#endif

                // wait to exit
                syncThreadState.threadObj.join();
                sendThreadState.threadObj.join();
#ifdef LIBURING_AVAILABLE
                if (Optimizations::isSelected(Optimizations::Code::liburing_send))
                {
                    cqeThreadState.threadObj.join();
                    io_uring_unregister_files(&ring);
                    // deallocate the ring
                    io_uring_queue_exit(&ring);
                }
#endif
                // pool memory is implicitly freed when pool goes out of scope
            }

            /**
             * Open sockets and start the threads - this marks the moment
             * from which sync packets start being sent.
             * @return - 0 on success, otherwise error condition
             */
            result<int> openAndStart() noexcept;

            /**
             * Send immediately overriding event number.
             * @param event - event buffer
             * @param bytes - bytes length of the buffer
             * @param eventNumber - optionally override the internal event number
             * @param dataId - optionally override the dataId
             * @param entropy - optional event entropy value (random will be generated otherwise)
             * @return - 0 on success, otherwise error condition
             */
            result<int> sendEvent(u_int8_t *event, size_t bytes, EventNum_t _eventNumber=0LL, 
                u_int16_t _dataId=0, u_int16_t _entropy=0) noexcept;

            /**
             * Add to send queue in a nonblocking fashion, overriding internal event number
             * @param event - event buffer
             * @param bytes - bytes length of the buffer
             * @param eventNum - optionally override the internal event number
             * @param dataId - optionally override the dataId
             * @param entropy - optional event entropy value (random will be generated otherwise)
             * @param callback - optional callback function to call after event is sent
             * @param cbArg - optional parameter for callback
             * @return - 0 on success, otherwise error condition
             */
            result<int> addToSendQueue(u_int8_t *event, size_t bytes, 
                EventNum_t _eventNum=0LL, u_int16_t _dataId = 0, u_int16_t entropy=0,
                void (*callback)(boost::any) = nullptr, 
                boost::any cbArg = nullptr) noexcept;

            /**
             * Get a ReportedStats structure of sync statistics.
             */
            inline const ReportedStats getSyncStats() const noexcept
            {
                return ReportedStats(syncStats);
            }

            /**
             * Get a ReportedStats structure of send statistics. 
             */
            inline const ReportedStats getSendStats() const noexcept
            {
                return ReportedStats(sendStats);
            }

            /**
             * Get the outgoing interface (if available)
             */
            inline const std::string getIntf() const noexcept
            {
                return sendThreadState.iface;
            }

            /**
             * Get the MTU currently in use by segmenter
             */
            inline const u_int16_t getMTU() const noexcept
            {
                return sendThreadState.mtu;
            }

            /**
             * get the maximum payload length used by the segmenter
             */
            inline const size_t getMaxPldLen() const noexcept
            {
                return sendThreadState.maxPldLen;
            }
            /*
            * Tell threads to stop
            */ 
            inline void stopThreads() 
            {
                threadsStop = true;
            }
        private:
            // Calculate the average event rate from circular buffer
            // note that locking lives outside this function, as needed.
            // NOTE: this is only useful to sync messages if sequential
            // event IDs are used. When usec timestamp is used for LB event numbers
            // the event is constant 1 MHz
            inline EventRate_t eventRate(UnixTimeNano_t currentTimeNanos) 
            {
                // no rate to report
                if (eventStatsBuffer.size() == 0)
                    return 1;
                EventNum_t eventTotal{0LL};
                // walk the circular buffer
                for(auto el: eventStatsBuffer)
                {
                    // add up the events
                    eventTotal += el.eventsSinceLastSync;
                }
                auto timeDiff = currentTimeNanos - 
                    eventStatsBuffer.begin()->lastSyncTimeNanos;

                // convert to Hz and return
                //return (eventTotal*1000000000UL)/timeDiff;
                // this uses floating point but is more accurate at low rates
                return std::round(static_cast<float>(eventTotal*1000000000UL)/timeDiff);
            }

            // doesn't require locking as it looks at only srcId in segmenter, which
            // never changes past initialization
            inline void fillSyncHdr(SyncHdr *hdr, UnixTimeNano_t tnano) 
            {
                EventRate_t reportedRate{1000000};
                // figure out what event number would be at this moment, don't worry about its entropy
                auto nowT = boost::chrono::system_clock::now();
                // Convert the time point to microseconds since the epoch
                EventNum_t reportedEventNum = boost::chrono::duration_cast<boost::chrono::microseconds>(nowT.time_since_epoch()).count();
                hdr->set(eventSrcId, reportedEventNum, reportedRate, tnano);
            }

            /**
             * Add entropy to a clock sample by randomizing the least 8 bits. Runs in the 
             * context of send thread.
             * @param clockSample - the sample value
             * @param ranlux - random number generator
             * @return 
             */
            inline int_least64_t addClockEntropy(int_least64_t clockSample)
            {
                return (clockSample & ~0xFF) | lsbDist(ranlux);
            }
    };
}
#endif