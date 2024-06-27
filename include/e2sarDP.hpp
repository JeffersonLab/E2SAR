#ifndef E2SARDPHPP
#define E2SARDPHPP

#include <boost/asio.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/thread.hpp>
#include "e2sarError.hpp"
#include "e2sarUtil.hpp"
#include "portable_endian.h"

/***
 * Dataplane definitions for E2SAR
*/

namespace e2sar
{
    using EventNum_t = u_int64_t;
    /*
    The Segmenter class knows how to break up the provided
    events into segments consumable by the hardware loadbalancer.
    It relies on EventHeader and LBHeader structures to
    segment into UDP packets and follows other LB rules while doing it.

    It runs on or next to the source of events.
    */
    class Segmenter
    {
        private:
            // Structure to hold each send-queue item
            struct EventQueueItem {
                uint32_t bytes;
                uint64_t tick;
                u_int8_t *event;
                void     *cbArg;
                void* (*callback)(void *);
            };

            /** Max size of internal queue holding events to be sent. */
            static const size_t QSIZE = 2047;

            /** pool from which queue items are allocated as needed */
            boost::object_pool<EventQueueItem> queueItemPool{32,QSIZE + 1};

            /** Fast, lock-free, wait-free queue for single producer and single consumer. */
            boost::lockfree::spsc_queue<EventQueueItem*, boost::lockfree::capacity<QSIZE>> eventQueue;

            /** Sync thread object */
            boost::thread syncThread;

            /** Send thread object */
            boost::thread sendThread;
        public:
            Segmenter(const EjfatURI &uri);
            Segmenter(const Segmenter &s) = delete;
            Segmenter & operator=(const Segmenter &o) = delete;
            ~Segmenter();

            // Blocking call. Event number automatically set.
            // Any core affinity needs to be done by caller.
            E2SARErrorc sendEvent(u_int8_t *event, size_t bytes);

            // Blocking call specifying event number.
            E2SARErrorc sendEvent(u_int8_t *event, size_t bytes, uint64_t eventNumber);

            // Non-blocking call to place event on internal queue.
            // Event number automatically set. 
            E2SARErrorc addToSendQueue(u_int8_t *event, size_t bytes, 
                void* (*callback)(void *) = nullptr, 
                void *cbArg = nullptr);

            // Non-blocking call specifying event number.
            E2SARErrorc addToSendQueue(u_int8_t *event, size_t bytes, 
                uint64_t eventNumber, 
                void* (*callback)(void *) = nullptr, 
                void *cbArg = nullptr);
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

    /*
        The Reassembly Header. You should always use the provided methods to set
        and interrogate fields as the structure maintains Big-Endian order
        internally.
    */
    constexpr u_int8_t lbVersion = 1 << 4; // shifted up by 4 bits to be in the upper nibble
    struct LBReHdr
    {
        u_int8_t preamble[2] {lbVersion, 0}; // 4 bit version + reserved
        u_int16_t dataId;   // source identifier
        u_int32_t bufferOffset;
        u_int32_t bufferLength;
        EventNum_t eventNum;
        void set(u_int16_t data_id, u_int32_t buff_off, u_int32_t buff_len, EventNum_t event_num)
        {
            dataId = htobe16(data_id);
            bufferOffset = htobe32(buff_off);
            bufferLength = htobe32(buff_len);
            eventNum = htobe64(event_num);
        }
        EventNum_t get_eventNum() 
        {
            return be64toh(eventNum);
        }
        u_int32_t get_bufferLength() 
        {
            return be32toh(bufferLength);
        }
        u_int32_t get_bufferOffset()
        {
            return be32toh(bufferOffset);
        }
        u_int16_t get_dataId()
        {
            return be16toh(dataId);
        }
    };

    /*
    The Load Balancer Header. You should always use the provided methods to set
    and interrogate fields as the structure maintains Big-Endian order
    internally.
    */

   constexpr u_int8_t lbehVersion = 2;
    struct LBHdr
    {
        char preamble[2] {'L', 'B'};
        u_int8_t version{lbehVersion};
        u_int8_t nextProto{0};
        u_int16_t rsvd{0};
        u_int16_t entropy{0};
        EventNum_t eventNum{0L};
        void set(u_int8_t proto, u_int16_t ent, EventNum_t event_num) 
        {
            nextProto = proto;
            entropy = htobe16(ent);
            eventNum = htobe64(event_num);
        }
        u_int8_t get_version() 
        {
            return version;
        }
        u_int8_t get_nextProto() 
        {
            return nextProto;
        }
        u_int16_t get_entropy()
        {
            return be16toh(entropy);
        }
        EventNum_t get_eventNum() 
        {
            return be64toh(eventNum);
        }
    };


}
#endif