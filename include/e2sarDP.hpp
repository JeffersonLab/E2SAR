#ifndef E2SARDPHPP
#define E2SARDPHPP

#include <boost/asio.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/any.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/variant.hpp>

#ifdef NETLINK_AVAILABLE
#include <linux/rtnetlink.h>
#endif

#include <atomic>

#include "e2sarError.hpp"
#include "e2sarUtil.hpp"
#include "e2sarHeaders.hpp"
#include "e2sarNetUtil.hpp"
#include "portable_endian.h"

/***
 * Dataplane definitions for E2SAR
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
        private:
            EjfatURI dpuri;
            // id of this data source. Note that in the sync msg it uses 32 bits
            // and in the RE header - 16
            const u_int16_t srcId;
            const u_int8_t nextProto;
            const u_int16_t entropy;

            // Structure to hold each send-queue item
            struct EventQueueItem {
                uint32_t bytes;
                uint64_t eventNum;
                u_int8_t *event;
                void* (*callback)(boost::any);
                boost::any cbArg;
            };

            // Max size of internal queue holding events to be sent. 
            static const size_t QSIZE = 2047;
            // various useful header lengths
            static const int IPHDRLEN = 20;
            static const int UDPHDRLEN = 8;

            // pool from which queue items are allocated as needed 
            boost::object_pool<EventQueueItem> queueItemPool{32, QSIZE + 1};

            // Fast, lock-free, wait-free queue (supports multiple producers/consumers)
            boost::lockfree::queue<EventQueueItem*> eventQueue;

            // structure that maintains send stats
            struct SendStats {
                // Last time a sync message sent to CP in nanosec since epoch.
                u_int64_t lastSyncTimeNanos;
                // Number of events sent since last sync message sent to CP. 
                u_int64_t eventsSinceLastSync; 
            };
            // event metadata fifo to keep track of stats
            boost::circular_buffer<SendStats> eventStatsBuffer;
            // keep these atomic, as they are accessed by Sync, Send (and maybe main) thread
            boost::atomic<u_int64_t> currentSyncStartNano{0};
            boost::atomic<u_int64_t> eventsInCurrentSync{0};

            // current event number
            boost::atomic<EventNum_t> eventNum{0};

            //
            // atomic struct for stat information for sync and send threads
            //
            struct AtomicStats {
                // sync messages sent
                std::atomic<u_int64_t> msgCnt{0}; 
                // sync errors seen on send
                std::atomic<u_int64_t> errCnt{0};
                // last error code
                std::atomic<int> lastErrno{0};
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
            // lock with sync thread
            //boost::mutex syncThreadMtx;

            /**
             * This thread sends data to the LB to be distributed to different processing
             * nodes.
             */
            struct SendThreadState {
                // owner object
                Segmenter &seg;
                boost::thread threadObj;
                // connect socket flag (usually true)
                const bool connectSocket{true};
                // tuple containing v4 and v6 send address structures
                // use boost::get<0> to get to v4 and boost::get<1> to
                // get to v6 address struct
#define GET_V4_SEND_STRUCT(sas) boost::get<0>(sas)
#define GET_V6_SEND_STRUCT(sas) boost::get<1>(sas)
                boost::tuple<sockaddr_in, sockaddr_in6> dataAddrStruct;

                // flags
                const bool useV6;
                const bool useZerocopy;

                // transmit parameters
                const u_int16_t mtu; // must accommodate typical IP, UDP, LB+RE headers and payload
                const std::string iface; // outgoing interface
                const size_t maxPldLen;

                // UDP sockets
                int socketFd4{0};
                int socketFd6{0};
                // pool of LB+RE headers for sending
                boost::object_pool<LBREHdr> lbreHdrPool{32,0};

                inline SendThreadState(Segmenter &s, bool v6, bool zc, u_int16_t mtu, bool cnct=true): 
                    seg{s}, connectSocket{cnct}, useV6{v6}, useZerocopy{zc}, mtu{mtu}, 
                    maxPldLen{mtu - IPHDRLEN - UDPHDRLEN - sizeof(LBHdr) - sizeof(REHdr)} {}

                inline SendThreadState(Segmenter &s, bool v6, bool zc, const std::string &iface, bool cnct=true): 
                    seg{s}, connectSocket{cnct}, useV6{v6}, useZerocopy{zc}, 
                    mtu{NetUtil::getMTU(iface)}, iface{iface},
                    maxPldLen{mtu - IPHDRLEN - UDPHDRLEN - sizeof(LBHdr) - sizeof(REHdr)} {}

                // open v4/v6 sockets
                result<int> _open();
                // close sockets
                result<int> _close();
                // fragment and send the event
                result<int> _send(u_int8_t *event, size_t bytes);
                // thread loop
                void _threadBody();
            };
            friend struct SendThreadState;

            SendThreadState sendThreadState;
            // lock with send thread
            boost::mutex sendThreadMtx;
            // condition variable for send thread
            boost::condition_variable sendThreadCond;

            /** Threads keep running while this is false */
            bool threadsStop{false};
        public:
            /**
             * Initialize segmenter state. Call openAndStart() to begin operation.
             * @param uri - EjfatURI initialized for sender with sync address and data address(es)
             * @param srcId - id of this source for load balancer
             * @param entropy - entropy value of this sender
             * @param nextProto -
             * @param sync_period_ms - sync period in milliseconds - how often sync messages are sent
             * @param sync_periods - number of sync periods to use for averaging send rate
             * @param mtu - use the following MTU for data (must accommodate MAC, IP, UDP, LB, RE headers
             * and payload)
             * @param useV6 - use dataplane v6 connection
             * @param useZerocopy - utilize Zerocopy if available
             * @param cnct - use connected sockets (default)
             */
            Segmenter(const EjfatURI &uri, u_int16_t srcId, u_int16_t entropy,  
                u_int16_t sync_period_ms=300, u_int16_t sync_periods=3, u_int8_t nextProto=rehdrVersion,
                u_int16_t mtu=1500, bool useV6=true, bool useZerocopy=false, bool cnct=true);
#if 0
            /**
             * Initialize segmenter state. Call openAndStart() to begin operation.
             * @param uri - EjfatURI initialized for sender with sync address and data address(es)
             * @param srcId - id of this source for load balancer
             * @param entropy - entropy value of this sender
             * @param nextProto -
             * @param sync_period_ms - sync period in milliseconds - how often sync messages are sent
             * @param sync_periods - number of sync periods to use for averaging send rate
             * @param iface - use the following interface for data.
             * (get MTU from the interface and, if possible, set it as outgoing - available on Linux)
             * @param useV6 - use dataplane v6 connection
             * @param useZerocopy - utilize Zerocopy if available
             * @param cnct - use connected sockets (default)
             */
            Segmenter(const EjfatURI &uri, u_int16_t srcId, u_int16_t entropy, 
                u_int16_t sync_period_ms=300, u_int16_t sync_periods=3, u_int8_t nextProto=rehdrVersion,
                std::string iface=""s, bool useV6=true, bool useZerocopy=false, bool cnct=true);
#endif

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

                // wait to exit
                syncThreadState.threadObj.join();
                sendThreadState.threadObj.join();
            }

            /**
             * Open sockets and start the threads - this marks the moment
             * from which sync packets start being sent.
             */
            result<int> openAndStart() noexcept;

            // Blocking call. Event number automatically set.
            // Any core affinity needs to be done by caller.
            result<int> sendEvent(u_int8_t *event, size_t bytes) noexcept;

            // Blocking call specifying event number.
            result<int> sendEvent(u_int8_t *event, size_t bytes, uint64_t eventNumber) noexcept;

            // Non-blocking call to place event on internal queue.
            // Event number automatically set. 
            result<int> addToSendQueue(u_int8_t *event, size_t bytes, 
                void* (*callback)(boost::any) = nullptr, 
                boost::any cbArg = nullptr) noexcept;

            // Non-blocking call specifying event number.
            result<int> addToSendQueue(u_int8_t *event, size_t bytes, 
                uint64_t eventNumber, 
                void* (*callback)(boost::any) = nullptr, 
                boost::any cbArg = nullptr) noexcept;

            /**
             * Get a tuple <sync msg cnt, sync err cnt, last errno> of sync statistics.
             * Stat structures have atomic members, no additional locking needed.
             */
            inline const boost::tuple<u_int64_t, u_int64_t, int> getSyncStats() const
            {
                return getStats(syncStats);
            }

            /**
             * Get a tuple <event datagrams cnt, event datagrams err cnt, last errno>
             * of send statistics. Stat structure have atomic members, no additional
             * locking needed
             */
            inline const boost::tuple<u_int64_t, u_int64_t, int> getSendStats() const 
            {
                return getStats(sendStats);
            }

            /**
             * Get the MTU currently in use by segmenter
             */
            inline const u_int16_t getMTU() const 
            {
                return sendThreadState.mtu;
            }

            /**
             * get the maximum payload length used by the segmenter
             */
            inline const int getMaxPldLen() const
            {
                return sendThreadState.maxPldLen;
            }
        private:
            inline const boost::tuple<u_int64_t, u_int64_t, int> getStats(const AtomicStats &s) const
            {
                return boost::make_tuple<u_int64_t, u_int64_t, int>(s.msgCnt, 
                    s.errCnt, s.lastErrno);
            }

            // Tell threads to stop
            inline void stopThreads() 
            {
                threadsStop = true;
            }

            // Calculate the average event rate from circular buffer
            // note that locking lives outside this function, as needed.
            inline EventRate_t eventRate(UnixTimeNano_t currentTimeNanos)
            {
                // each element of circular buffer states the start of the sync period
                // and the number of events sent during that period
                UnixTimeNano_t  firstSyncStart{std::numeric_limits<unsigned long long>::max()};
                EventNum_t eventTotal{0LL};
                // walk the circular buffer
                for(auto el: eventStatsBuffer)
                {
                    // find earliest time
                    firstSyncStart = (el.lastSyncTimeNanos < firstSyncStart ? el.lastSyncTimeNanos : firstSyncStart);
                    // add up the events
                    eventTotal += el.eventsSinceLastSync;
                }
                auto timeDiff = currentTimeNanos - firstSyncStart;

                // convert to Hz and return
                return eventTotal/(timeDiff/1000000000UL);
            }

            // doesn't require locking as it looks at only srcId in segmenter, which
            // never changes past initialization
            inline void fillSyncHdr(SyncHdr *hdr, EventRate_t eventRate, UnixTimeNano_t tnano)
            {
                // note that srcId is only 16 bits, but the sync field is 32-bit wide
                hdr->set(srcId, eventNum, eventRate, tnano);
            }
    };

    /*
        The Reassembler class knows how to reassemble the events back. It
        also knows how to optionally register self as a receiving node. It relies
        on the LBEventHeader structure to reassemble the event. It can
        use multiple ways of reassembly:
        - To a memory region
        - To a shared memory
        - To a file descriptor
        - ...

        It runs on or next to the worker performing event processing.
    */
    class Reassembler
    {

        friend class Segmenter;
        public:
            Reassembler();
            Reassembler(const Reassembler &r) = delete;
            Reassembler & operator=(const Reassembler &o) = delete;
            ~Reassembler();
            
            // Non-blocking call to get events
            E2SARErrorc getEvent(uint8_t **event, size_t *bytes, uint64_t* eventNum, uint16_t *srcId);

            int probeStats();
        protected:
        private:
    };


}
#endif