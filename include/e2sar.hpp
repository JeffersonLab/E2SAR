#ifndef E2SARHPP
#define E2SARHPP

#include <boost/asio.hpp>

namespace e2sar
{

    using EventNum_t = u_int64_t;
    using TimeUntil = std::string;
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
            int sendEvent();
            int probeStats();
    };

    /*
    The Reassembler class knows how to reassemble the events back. It
    also knows how to register self as a receiving nodes. It relies
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
            int receiveEvents();
            int probeStats();
        protected:
        private:
    };

    /*
    The LBManager knows how to allocate and de-allocate a load balancer.
    It can be run from Segmenter, Reassembler or a third party like the
    workflow manager.
    */
    class LBManager
    {
        private:
        protected:
        public:
            // initialize a manager with configuration
            LBManager();
            // Reserve a new Load Balancer
            int reserveLB(const std::string &lb_name, const TimeUntil &until);
            // get load balancer info (same returns as reserverLB)
            int getLB();
            // get load balancer status
            int getLBStatus();
            // Free a Load Balancer
            int freeLB();
            // register worker
            int registerWorker();
            // send worker queue state
            int sendState();
            // deregister worker
            int deregisterWorker();
            // get updated statistics
            int probeStats();
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

    /*
    Sync packet send by segmenter to LB periodically.
    */
    struct LBSyncPkt
    {
    };

}

int add(int x, int y);
#endif