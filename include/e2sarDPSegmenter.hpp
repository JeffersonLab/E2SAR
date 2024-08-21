#ifndef E2SARDSEGMENTERPHPP
#define E2SARDSEGMENTERPHPP

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
#include <boost/random.hpp>

#ifdef NETLINK_AVAILABLE
#include <linux/rtnetlink.h>
#endif

#include <atomic>

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
            // point (e.g. a DAQ), carried in RE header
            const u_int16_t dataId;
            // unique identifier of an individual LB packet transmitting
            // host/daq, 32-bit to accommodate IP addresses more easily
            // carried in Sync header
            const u_int32_t eventSrcId;

            // Max size of internal queue holding events to be sent. 
            static const size_t QSIZE{2047};

            // how long data send thread spends sleeping
            static const boost::chrono::milliseconds sleepTime;

            // Structure to hold each send-queue item
            struct EventQueueItem {
                uint32_t bytes;
                EventNum_t eventNum;
                u_int8_t *event;
                u_int16_t entropy;  // optional per event entropy
                void (*callback)(boost::any);
                boost::any cbArg;
            };

            // pool from which queue items are allocated as needed 
            boost::object_pool<EventQueueItem> queueItemPool{32, QSIZE + 1};

            // Fast, lock-free, wait-free queue (supports multiple producers/consumers)
            boost::lockfree::queue<EventQueueItem*> eventQueue{QSIZE};
            // to avoid locking the item pool send thread puts processed events 
            // on this queue so they can be freed opportunistically by main thread
            boost::lockfree::queue<EventQueueItem*> returnQueue{QSIZE};

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
                const size_t mtu; // must accommodate typical IP, UDP, LB+RE headers and payload
                const std::string iface; // outgoing interface
                const size_t maxPldLen;

                // UDP sockets
                int socketFd4{0};
                int socketFd6{0};
                // pool of LB+RE headers for sending
                boost::object_pool<LBREHdr> lbreHdrPool{32,0};

                // fast random number generator to create entropy values for events
                // this entropy value is held the same for all packets of a given
                // event guaranteeing the same destination UDP port for all of them
                boost::random::ranlux24_base ranlux;
                boost::random::uniform_int_distribution<> randDist{0, std::numeric_limits<u_int16_t>::max()};

                inline SendThreadState(Segmenter &s, bool v6, bool zc, u_int16_t mtu, bool cnct=true): 
                    seg{s}, connectSocket{cnct}, useV6{v6}, useZerocopy{zc}, mtu{mtu}, 
                    maxPldLen{mtu - TOTAL_HDR_LEN}, ranlux{static_cast<u_int32_t>(std::time(0))} {}

                inline SendThreadState(Segmenter &s, bool v6, bool zc, const std::string &iface, bool cnct=true): 
                    seg{s}, connectSocket{cnct}, useV6{v6}, useZerocopy{zc}, 
                    mtu{NetUtil::getMTU(iface)}, iface{iface},
                    maxPldLen{mtu - TOTAL_HDR_LEN}, ranlux{static_cast<u_int32_t>(std::time(0))} {}

                // open v4/v6 sockets
                result<int> _open();
                // close sockets
                result<int> _close();
                // fragment and send the event
                result<int> _send(u_int8_t *event, size_t bytes, EventNum_t altEventNum, u_int16_t entropy);
                // thread loop
                void _threadBody();
            };
            friend struct SendThreadState;

            SendThreadState sendThreadState;
            // lock with send thread
            boost::mutex sendThreadMtx;
            // condition variable for send thread
            boost::condition_variable sendThreadCond;
            // use control plane (can be disabled for debugging)
            bool useCP;

            /** Threads keep running while this is false */
            bool threadsStop{false};
        public:
            /** 
             * Because of the large number of constructor parameters in Segmenter
             * we make this a structure with sane defaults
             * dpV6 - prefer V6 dataplane {false}
             * zeroCopy - use zeroCopy optimization {false}
             * connectedSocket - use connected sockets {true}
             * useCP - use control plane to send Sync packets {true}
             * syncPeriodMs - sync thread period in milliseconds {1000}
             * syncPerods - number of sync periods to use for averaging reported send rate {2}
             * mtu - size of the MTU to attempt to fit the segmented data in (must accommodate
             * IP, UDP and LBRE headers)
             */
            struct SegmenterFlags 
            {
                bool dpV6;
                bool zeroCopy;
                bool connectedSocket;
                bool useCP;
                u_int16_t syncPeriodMs;
                u_int16_t syncPeriods;
                u_int16_t mtu;

                SegmenterFlags(): dpV6{false}, zeroCopy{false}, connectedSocket{true},
                    useCP{true}, syncPeriodMs{1000}, syncPeriods{2}, mtu{1500} {}
            };
            /**
             * Initialize segmenter state. Call openAndStart() to begin operation.
             * @param uri - EjfatURI initialized for sender with sync address and data address(es)
             * @param dataId - unique identifier of the originating segmentation point (e.g. a DAQ), 
             * carried in SAR header
             * @param eventSrcId - unique identifier of an individual LB packet transmitting host/daq, 
             * 32-bit to accommodate IP addresses more easily, carried in Sync header
             * @param sfalags - SegmenterFlags 
             */
            Segmenter(const EjfatURI &uri, u_int16_t dataId, u_int32_t eventSrcId,  
                const SegmenterFlags &sflags=SegmenterFlags());
#if 0
            /**
             * Initialize segmenter state. Call openAndStart() to begin operation.
             * @param uri - EjfatURI initialized for sender with sync address and data address(es)
             * @param dataId - unique identifier of the originating segmentation point (e.g. a DAQ), 
             * carried in SAR header
             * @param eventSrcId - unique identifier of an individual LB packet transmitting host/daq, 
             * 32-bit to accommodate IP addresses more easily, carried in Sync header
             * @param iface - use the following interface for data.
             * (get MTU from the interface and, if possible, set it as outgoing - available on Linux)
             * @param sflags - SegmenterFlags
             */
            Segmenter(const EjfatURI &uri, u_int16_t dataId, u_int32_t eventSrcId, 
                std::string iface=""s, 
                const SegmenterFlags &sflags=SegmenterFlags());
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

                // pool memory is implicitly freed when pool goes out of scope
            }

            /**
             * Open sockets and start the threads - this marks the moment
             * from which sync packets start being sent.
             */
            result<int> openAndStart() noexcept;

            /**
             * Send immediately using internal event number.
             * @param event - event buffer
             * @param bytes - bytes length of the buffer
             * @param entropy - optional event entropy value (random will be generated otherwise)
             */
            result<int> sendEvent(u_int8_t *event, size_t bytes, u_int16_t entropy=0) noexcept;

            /**
             * Send immediately overriding event number.
             * @param event - event buffer
             * @param bytes - bytes length of the buffer
             * @param eventNumber - override the internal event number
             * @param entropy - optional event entropy value (random will be generated otherwise)
             */
            result<int> sendEvent(u_int8_t *event, size_t bytes, uint64_t eventNumber, 
                u_int16_t entropy=0) noexcept;

            /**
             * Add to send queue in a nonblocking fashion, using internal event number
             * @param event - event buffer
             * @param bytes - bytes length of the buffer
             * @param entropy - optional event entropy value (random will be generated otherwise)
             * @param callback - callback function to call after event is sent (non-blocking)
             * @param cbArg - parameter for callback
             */
            result<int> addToSendQueue(u_int8_t *event, size_t bytes, u_int16_t entropy=0,
                void (*callback)(boost::any) = nullptr, 
                boost::any cbArg = nullptr) noexcept;

            /**
             * Add to send queue in a nonblocking fashion, overriding internal event number
             * @param event - event buffer
             * @param bytes - bytes length of the buffer
             * @param eventNumber - override the internal event number
             * @param entropy - optional event entropy value (random will be generated otherwise)
             * @param callback - callback function to call after event is sent
             * @param cbArg - parameter for callback
             */
            result<int> addToSendQueue(u_int8_t *event, size_t bytes, 
                uint64_t eventNumber, u_int16_t entropy=0,
                void (*callback)(boost::any) = nullptr, 
                boost::any cbArg = nullptr) noexcept;

            /**
             * Get a tuple <sync msg cnt, sync err cnt, last errno> of sync statistics.
             * Stat structures have atomic members, no additional locking needed.
             */
            inline const boost::tuple<u_int64_t, u_int64_t, int> getSyncStats() const noexcept
            {
                return getStats(syncStats);
            }

            /**
             * Get a tuple <event datagrams cnt, event datagrams err cnt, last errno>
             * of send statistics. Stat structure have atomic members, no additional
             * locking needed
             */
            inline const boost::tuple<u_int64_t, u_int64_t, int> getSendStats() const noexcept
            {
                return getStats(sendStats);
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
            inline const int getMaxPldLen() const noexcept
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
            inline void fillSyncHdr(SyncHdr *hdr, EventRate_t eventRate, UnixTimeNano_t tnano) 
            {
                // note that srcId is only 16 bits, but the sync field is 32-bit wide
                hdr->set(eventSrcId, eventNum, eventRate, tnano);
            }

            // process backlog in return queue and free event queue item blocks on it
            inline void freeEventItemBacklog() 
            {
                EventQueueItem *item{nullptr};
                while (returnQueue.pop(item))
                    queueItemPool.free(item);
            }
    };
}
#endif