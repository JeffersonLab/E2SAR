#ifndef E2SARDPHPP
#define E2SARDPHPP

#include <boost/asio.hpp>
#include "e2sarError.hpp"

/***
 * Dataplane definitions for E2SAR
*/

namespace e2sar
{
    using EventNum_t = u_int64_t;
    /*
    The Segmenter class knows how to break up the provided
    events into segments consumable by the hardware loadbalancer.
    It relies on LBEventHeader and LBHeader structures to
    segment into UDP packets and follows other LB rules while doing it.

    It runs on or next to the source of events.
    */
    class Segmenter
    {
        private:
        protected:
            /*
            This thread sends ticks regularly to Load Balancer
            */
            class LBThread
            {
                static int threadBody();
            };
        public:
            Segmenter();
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
    The Event Header. You should use the provided methods to set
    and interrogate fields.
    */
    struct LBEventHdr
    {
        u_int16_t preamble; // 4 bit version + reserved
        u_int16_t dataId;   // source identifier
        u_int32_t bufferOffset;
        u_int32_t bufferLength;
        EventNum_t eventNum;
    };

    /*
    The Load Balancer Header. You should use the provided methods to set
    and interrogate fields.
    */
    constexpr int lbVersion = 1;
    constexpr std::string_view lb_preamble{"LB"};
    constexpr std::string_view lc_preamble{"LC"};
    struct LBHdr
    {
        u_int8_t preamble[2]; // "LB"
        u_int8_t version;
        u_int8_t nextProto;
        u_int16_t rsvd;
        u_int16_t entropy;
        EventNum_t eventNum;
    };


}
#endif