#include <boost/thread.hpp>
#include <boost/chrono.hpp>

#include "portable_endian.h"

#include "e2sarDPReassembler.hpp"


namespace e2sar 
{

    Reassembler::Reassembler(const EjfatURI &uri, const ReassemblerFlags &rflags):
        dpuri(uri),
        lbman(dpuri, rflags.validateCert),
        recvThreadState(*this, rflags.dpV6),
        sendStateThreadState(*this, rflags.dpV6, rflags.period_ms)
    {
        ;
    }

    result<int> Reassembler::openAndStart() noexcept
    {

        // open receive socket(s)
        auto status = recvThreadState._open();
        if (status.has_error()) 
        {
            return E2SARErrorInfo{E2SARErrorc::SocketError, 
                "Unable to open receive sockets: " + status.error().message()};
        }

        // start receive thread
        boost::thread syncT(&Reassembler::RecvThreadState::_threadBody, &recvThreadState);
        recvThreadState.threadObj = std::move(syncT);

        // start sendstate thread
        boost::thread sendstateT(&Reassembler::SendStateThreadState::_threadBody, &sendStateThreadState);
        sendStateThreadState.threadObj = std::move(sendstateT);
        return 0;
    }

    void Reassembler::RecvThreadState::_threadBody()
    {
        while(!reas.threadsStop)
        {
            // do select/wait on open sockets

            // receive a event fragment on one of them

            // start a new event if offset 0 (check for event number collisions)
            // or attach to existing event

            // check if this event is completed, if so put on queue
        }
    }

    result<int> Reassembler::RecvThreadState::_open()
    {
        return 0;
    }

    result<int> Reassembler::RecvThreadState::_close()
    {
        return 0;
    }

    result<int> Reassembler::RecvThreadState::_recv()
    {
        return 0;
    }

    void Reassembler::SendStateThreadState::_threadBody()
    {
        while(!reas.threadsStop)
        {
            // periodically send state to control plane

            // Get the current time point
            auto nowT = boost::chrono::high_resolution_clock::now();

            // fillPercent, control signal and isReady flag must be provided
            
            reas.lbman.sendState();

            // sleep approximately
            // FIXME: we should probably check until > now after send completes
            auto until = nowT + boost::chrono::milliseconds(period_ms);
            boost::this_thread::sleep_until(until);
        }
    }
}
