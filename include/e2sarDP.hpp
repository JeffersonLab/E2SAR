#ifndef E2SARDPHPP
#define E2SARDPHPP

#include <boost/asio.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/any.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/variant.hpp>

#include <atomic>

#include "e2sarError.hpp"
#include "e2sarUtil.hpp"
#include "e2sarHeaders.hpp"
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
            EjfatURI _dpuri;

            // Structure to hold each send-queue item
            struct EventQueueItem {
                uint32_t bytes;
                uint64_t tick;
                u_int8_t *event;
                boost::any cbArg;
                void* (*callback)(boost::any);
            };

            // Max size of internal queue holding events to be sent. 
            static const size_t QSIZE = 2047;

            // pool from which queue items are allocated as needed 
            boost::object_pool<EventQueueItem> queueItemPool{32, QSIZE + 1};

            // Fast, lock-free, wait-free queue for single producer and single consumer. 
            boost::lockfree::spsc_queue<EventQueueItem*, boost::lockfree::capacity<QSIZE>> eventQueue;

            // structure that maintains send stats
            struct SendStats {
                /** Last time a sync message sent to CP in nanosec since epoch. */
                uint64_t lastSyncTimeNanos;
                /** Number of events sent since last sync message sent to CP. */
                uint64_t eventsSinceLastSync; 
            };
            // event metadata fifo
            boost::circular_buffer<SendStats> eventStatsBuffer;
            SendStats currentPeriodStats;

            //Starting event number for LB header in a UDP packet.
            u_int64_t tick{0LL};

            // id of this data source
            const u_int16_t srcId;

            // current event number
            EventNum_t eventNum{0};

            // struct for stat information
            struct Stats {
                // sync messages sent
                std::atomic<u_int64_t> syncMsgCnt{0}; 
                // sync errors seen on send
                std::atomic<u_int64_t> syncErrCnt{0};
                // last error code
                std::atomic<int> lastSyncErrno{0};
                // event datagrams sent
                std::atomic<u_int64_t> eventDatagramsCnt{0};
                // event datagram send errors
                std::atomic<u_int64_t> eventDatagramsErrCnt{0};
                // last error code
                std::atomic<int> lastSendErrno{0};
            };
            Stats stats;

            /** 
             * This thread sends a sync header every pre-specified number of milliseconds.
             */
            struct SyncThreadState {
                // owner object
                Segmenter &seg;
                boost::thread threadObj;
                // period in ms
                u_int16_t period_ms{100};
                // pool of headers to use for sending
                boost::object_pool<SyncHdr> syncHdrPool{32,0};
                // connect socket flag (usually true)
                bool connectSocket{true};
                // sockaddr_in[6] union (use boost::get<sockaddr_in> or
                // boost::get<sockaddr_in6> to get to the appropriate structure)
#define GET_V4_SYNC_STRUCT(sas) boost::get<sockaddr_in>(sas)
#define GET_V6_SYNC_STRUCT(sas) boost::get<sockaddr_in6>(sas)
                boost::variant<sockaddr_in, sockaddr_in6> syncAddrStruct;
                // flag that tells us we are v4 or v6
                bool isV6{false};
                // UDP socket
                int socketFd{0};

                inline SyncThreadState(Segmenter &s, u_int16_t time_period_ms): 
                    seg{s}, 
                    period_ms{time_period_ms}
                    {}

                result<int> _open();
                result<int> _close();
                result<int> _send(SyncHdr *hdr);
                void _threadBody();
            };
            friend struct SyncThreadState;

            SyncThreadState syncThreadState;
            // lock with sync thread
            boost::mutex syncThreadMtx;

            /**
             * This thread sends data to the LB to be distributed to different processing
             * nodes.
             */
            struct SendThreadState {
                // owner object
                Segmenter &seg;
                boost::thread threadObj;
                // connect socket flag (usually true)
                bool connectSocket{true};
                // tuple containing v4 and v6 send address structures
                // use boost::get<0> to get to v4 and boost::get<1> to
                // get to v6 address struct
#define GET_V4_SEND_STRUCT(sas) boost::get<0>(sas)
#define GET_V6_SEND_STRUCT(sas) boost::get<1>(sas)
                boost::tuple<sockaddr_in, sockaddr_in6> syncAddrStruct;

                // UDP socket
                int socketFd{0};

                inline SendThreadState(Segmenter &s): 
                    seg{s} {}
                result<int> _open();
                result<int> _close();
                result<int> _send();
                void _threadBody();
            };
            friend struct SendThreadState;

            SendThreadState sendThreadState;
            // lock with send thread
            boost::mutex sendThreadMtx;

            /** Threads keep running while this is false */
            bool threadsStop{false};
        public:
            /**
             * Open and connect sync and data sockets, start sync and data threads,
             * initializes the data event queue and allocates user-space memory pools.
             * @param uri - EjfatURI initialized for sender with sync address and data address(es)
             * @param srcId - id of this source for load balancer
             * @param sync_period_ms - sync period in milliseconds - how often sync messages are sent
             * @param sync_periods - number of sync periods to use for averaging send rate
             */
            Segmenter(const EjfatURI &uri, u_int16_t srcId, u_int16_t sync_period_ms=300, u_int16_t sync_periods=3);

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
             * Get a tuple <sync msg cnt, sync err cnt, last errno> of sync statistics
             */
            inline const boost::tuple<u_int64_t, u_int64_t, int> getSyncStats() const
            {
                return boost::make_tuple<u_int64_t, u_int64_t, int>(stats.syncMsgCnt, 
                    stats.syncErrCnt, stats.lastSyncErrno);
            }

            /**
             * Get a tuple <event datagrams cnt, event datagrams err cnt, last errno>
             * of send statistics
             */
            inline const boost::tuple<u_int64_t, u_int64_t, int> getSendStats() const 
            {
                return boost::make_tuple<u_int64_t, u_int64_t, int>(stats.eventDatagramsCnt, 
                    stats.eventDatagramsErrCnt, stats.lastSendErrno);
            }
        private:
            // Tell threads to stop
            inline void stopThreads() 
            {
                threadsStop = true;
            }

            // Calculate the average event rate from circular buffer
            // note that locking lives outside this function
            inline EventRate_t eventRate(UnixTimeNano_t  currentTimeNanos)
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