#ifndef E2SARDREASSEMBLERPHPP
#define E2SARDREASSEMBLERPHPP

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
#include "e2sarCP.hpp"
#include "portable_endian.h"

/***
 * Dataplane definitions for E2SAR Reassembler
*/

namespace e2sar
{
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

            bool threadsStop{false};
            /**
             * This thread receives data, reassembles into events and puts them onto the 
             * queue for getEvent()
             */
            struct RecvThreadState {
                // owner object
                Reassembler &reas;
                boost::thread threadObj;

                // flags
                const bool useV6;

                // UDP socket
                int socketFd{0};

                inline RecvThreadState(Reassembler &r, bool v6): 
                    reas{r}, useV6{v6}
                {}

                // open v4/v6 sockets
                result<int> _open();
                // close sockets
                result<int> _close();
                // receive the events
                result<int> _recv();
                // thread loop
                void _threadBody();
            };
            friend struct RecvThreadState;
            RecvThreadState recvThreadState;

            /**
             * This thread sends CP gRPC SendState messages using session token and id
             */
            struct SendStateThreadState {
                // owner object
                Reassembler &reas;
                boost::thread threadObj;

                const u_int16_t period_ms{100};
                // flags
                const bool useV6;

                // UDP sockets
                int socketFd{0};

                inline SendStateThreadState(Reassembler &r, bool v6, u_int16_t period_ms): 
                    reas{r}, useV6{v6}, period_ms{period_ms}
                {}

                // thread loop. all important behavior is encapsulated inside LBManager
                void _threadBody();
            };
            friend struct sendStateThreadState;
            SendStateThreadState sendStateThreadState;            
        public:
            /**
             * Structure for flags governing Reassembler behavior
             * with sane defaults
             * dpV6 - prefer IPv6 dataplane {false}
             * period_ms - period of the send state thread in milliseconds {100}
             * validateCert - validate control plane TLS certificate {true}
             */
            struct ReassemblerFlags 
            {
                bool dpV6; 
                u_int16_t period_ms;
                bool validateCert;
                ReassemblerFlags(): dpV6{false}, period_ms{100}, validateCert{true} {}
            };
            /**
             * Create a reassembler object
             * @param uri - EjfatURI with session token and session id and datap
             */
            Reassembler(const EjfatURI &uri, const ReassemblerFlags &rflags);
            Reassembler(const Reassembler &r) = delete;
            Reassembler & operator=(const Reassembler &o) = delete;
            ~Reassembler()
            {
                stopThreads();

                // wait to exit
                recvThreadState.threadObj.join();
                sendStateThreadState.threadObj.join();

                // pool memory is implicitly freed when pool goes out of scope
            }
            
            /**
             * Open sockets and start the threads - this marks the moment
             * from which we are listening for incoming packets, assembling
             * them into event buffers and putting them into the queue.
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
            result<int> getEvent(uint8_t **event, size_t *bytes, uint64_t* eventNum, uint16_t *dataId);

#if 0
            /**
             * Blocking variant of getEvent() with same parameter semantics
             */
            result<int> recvEvent(uint8_t **event, size_t *bytes, uint64_t* eventNum, uint16_t *dataId);
#endif

            int getEventStats();
        protected:
        private:
            void stopThreads() 
            {
                threadsStop = true;
            }
    };


}
#endif