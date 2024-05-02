#ifndef E2SARHPP
#define E2SARHPP

#include <boost/asio.hpp>

namespace e2sar{

using EventNum_t = u_int64_t;
/*
The Segmenter class knows how to break up the provided
events into segments consumable by the hardware loadbalancer.
It relies on LBEventHeader and LBHeader structures to 
segment into UDP packets and follows other LB rules while doing it.
*/
class Segmenter {

    friend class Reassembler;
    /*
    This thread sends ticks regularly to Load Balancer
    */
    class LBThread {
        static int threadBody();
    };

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
*/
class Reassembler {
    
    friend class Segmenter;

    int receiveEvents();
    int probeStats()
};

/*
The LBManager knows how to allocate and de-allocate a load balancer
*/
class LBManager {
    // 
};

/*
The Event Header. You should use the provided methods to set
and interrogate fields.
*/
struct LBEventHeader {
    u_int16_t preamble; // 4 bit version + reserved
    u_int16_t dataId; // source identifier
    u_int32_t bufferOffset; 
    u_int32_t bufferLength;
    EventNum_t eventNum;
};

/*
The Load Balancer Header. You should use the provided methods to set
and interrogate fields.
*/
constexpr int lbVersion = 1;
constexpr std::string_view lb_preamble {"LB"};
constexpr std::string_view lc_preamble {"LC"};
struct LBHeader {
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
struct LBSyncPacket {

};

int add(int x, int y);


}
#endif