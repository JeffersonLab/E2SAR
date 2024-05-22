#ifndef E2SARCPHPP
#define E2SARCPHPP
#include <boost/asio.hpp>
/***
 * Control Plane definitions for E2SAR
*/

namespace e2sar
{
    /*
    The LBManager knows how to speak to load balancer control plane over gRPC.
    It can be run from Segmenter, Reassembler or a third party like the
    workflow manager.
    */
    using TimeUntil = std::string;
    
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
    Sync packet sent by segmenter to LB periodically.
    */
    struct LBSyncPkt
    {
    };
}
#endif