#ifndef E2SARHEADERS_HPP
#define E2SARHEADERS_HPP

#include <boost/thread.hpp>
#include <boost/chrono.hpp>

#include "portable_endian.h"

#include "e2sarDP.hpp"

namespace e2sar 
{
    Segmenter::Segmenter(const EjfatURI &uri, u_int16_t sid, u_int16_t sync_period_ms, u_int16_t sync_periods): 
        _dpuri{uri}, 
        eventStatsBuffer{sync_periods},
        srcId{sid},
        syncThreadState(*this, sync_period_ms), 
        sendThreadState(*this)
    {
        ;
    }

    result<int> Segmenter::openAndStart() noexcept
    {
        // open and connect sync and send sockets
        auto status = syncThreadState._open();
        if (status.has_error()) 
        {
            return E2SARErrorInfo{E2SARErrorc::SocketError, 
                "Unable to open sync socket: " + status.error().message()};
        }

        status = sendThreadState._open();
        if (status.has_error())
        {
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to open data socket: " + status.error().message()};
        }

        // start the sync thread from method
        boost::thread syncT(&Segmenter::SyncThreadState::_threadBody, &syncThreadState);
        syncThreadState.threadObj = std::move(syncT);

        // start the data sending thread from method
        //boost::thread sendT(&Segmenter::SendThreadState::_threadBody, &sendThreadState);
        //sendThreadState.threadObj = std::move(sendT);

        return 0;
    }

    void Segmenter::SyncThreadState::_threadBody()
    {
        while(!seg.threadsStop)
        {
            // Get the current time point
            auto nowT = boost::chrono::high_resolution_clock::now();
            // Convert the time point to nanoseconds since the epoch
            auto now = boost::chrono::duration_cast<boost::chrono::nanoseconds>(nowT.time_since_epoch()).count();
            uint64_t currentTimeNanos = static_cast<uint64_t>(now);
                
            // get sync header buffer
            auto hdr = syncHdrPool.construct();
            e2sar::EventRate_t evtRate;
            {
                boost::lock_guard<boost::mutex> guard(seg.syncThreadMtx);
                // peek at segmenter metadata fifo to calculate event rate
                evtRate = seg.eventRate(currentTimeNanos);
            }
            // fill the sync header using a mix of current and segmenter data
            // no locking needed - we only look at one field in seg - srcId
            seg.fillSyncHdr(hdr, evtRate, currentTimeNanos);

            // send it off
            auto sendRes = _send(hdr);

            // free header
            syncHdrPool.free(hdr);

            // sleep approximately
            auto until = nowT + boost::chrono::milliseconds(period_ms);
            boost::this_thread::sleep_until(until);
        }
    }

    result<int> Segmenter::SyncThreadState::_open()
    {
        auto syncAddr = seg._dpuri.get_syncAddr();
        if (syncAddr.has_error())
            return syncAddr.error();

        // Socket for sending sync message to CP
        if (syncAddr.value().first.is_v6()) {
            if ((socketFd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
                seg.stats.syncErrCnt++;
                return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
            }

            sockaddr_in6 syncAddrStruct6{};
            syncAddrStruct6.sin6_family = AF_INET6;
            syncAddrStruct6.sin6_port = htobe16(syncAddr.value().second);
            inet_pton(AF_INET6, syncAddr.value().first.to_string().c_str(), &syncAddrStruct6.sin6_addr);
            isV6 = true;
            syncAddrStruct = syncAddrStruct6;

            if (connectSocket) {
                int err = connect(socketFd, (const sockaddr *) &syncAddrStruct6, sizeof(struct sockaddr_in6));
                if (err < 0) {
                    close(socketFd);
                    seg.stats.syncErrCnt++;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
            }
        }
        else {
            if ((socketFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                seg.stats.syncErrCnt++;
                return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
            }

            sockaddr_in syncAddrStruct4{};
            syncAddrStruct4.sin_family = AF_INET;
            syncAddrStruct4.sin_port = htobe16(syncAddr.value().second);
            inet_pton(AF_INET, syncAddr.value().first.to_string().c_str(), &syncAddrStruct4.sin_addr);
            syncAddrStruct = syncAddrStruct4;

            if (connectSocket) {
                int err = connect(socketFd, (const sockaddr *) &syncAddrStruct4, sizeof(struct sockaddr_in));
                if (err < 0) {
                    close(socketFd);
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
            }
        }
        return 0;
    }

    result<int> Segmenter::SyncThreadState::_close()
    {
        close(socketFd);
        return 0;
    }

    result<int> Segmenter::SyncThreadState::_send(SyncHdr *hdr)
    {
        int err;
        if (connectSocket) {
            seg.stats.syncMsgCnt++;
            err = (int) send(socketFd, static_cast<void*>(hdr), sizeof(SyncHdr), 0);
        }
        else {
            if (isV6) {
                seg.stats.syncMsgCnt++;
                err = (int) sendto(socketFd, static_cast<void*>(hdr), sizeof(SyncHdr), 0, 
                    (sockaddr * ) & GET_V6_SYNC_STRUCT(syncAddrStruct), sizeof(struct sockaddr_in6));
            }
            else {
                seg.stats.syncMsgCnt++;
                err = (int) sendto(socketFd, static_cast<void*>(hdr), sizeof(SyncHdr), 0,  
                    (sockaddr * ) & GET_V4_SYNC_STRUCT(syncAddrStruct), sizeof(struct sockaddr_in));
            }
        }

        if (err == -1)
        {
            seg.stats.syncErrCnt++;
            return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
        }

        return 0;
    }

    void Segmenter::SendThreadState::_threadBody()
    {
        while(!seg.threadsStop)
        {
            // update event numbers

            // update the circular buffer for rate calculation
            {
                boost::lock_guard<boost::mutex> guard(seg.syncThreadMtx);
                SendStats sendStatsItem;
                seg.eventStatsBuffer.push_back(sendStatsItem);
            }
            //
            // temporary code
            //
            auto nowT = boost::chrono::high_resolution_clock::now();
            // sleep approximately
            auto until = nowT + boost::chrono::milliseconds(1000);
            boost::this_thread::sleep_until(until);
        }
    }

    result<int> Segmenter::SendThreadState::_open()
    {
        return 0;
    }

    result<int> Segmenter::SendThreadState::_send()
    {
        return 0;
    }

    result<int> Segmenter::SendThreadState::_close()
    {
        close(socketFd);
        return 0;
    }
}

#endif