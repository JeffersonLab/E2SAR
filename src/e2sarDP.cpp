#include <boost/thread.hpp>
#include <boost/chrono.hpp>

#include "portable_endian.h"

#include "e2sarDP.hpp"


namespace e2sar 
{
    // set the sleep timer for the send thread - how long it waits on
    // a condition for new events
    const boost::chrono::milliseconds Segmenter::sleepTime(10);

    Segmenter::Segmenter(const EjfatURI &uri, u_int16_t sid, u_int16_t entropy, 
        u_int16_t sync_period_ms, u_int16_t sync_periods, u_int16_t mtu,
        bool useV6, bool useZerocopy, bool cnct): 
        dpuri{uri}, 
        srcId{sid},
        entropy{entropy},
        eventStatsBuffer{sync_periods},
        syncThreadState(*this, sync_period_ms, cnct), 
        sendThreadState(*this, useV6, useZerocopy, mtu, cnct)
    {
        ;
    }

#if 0
    Segmenter::Segmenter(const EjfatURI &uri, u_int16_t sid, u_int16_t entropy, 
        u_int16_t sync_period_ms, u_int16_t sync_periods, const std::string iface, 
        bool useV6, bool useZerocopy, bool cnct): 
        dpuri{uri}, 
        srcId{sid},
        entropy{entropy},
        eventStatsBuffer{sync_periods},
        syncThreadState(*this, sync_period_ms, cnct), 
        sendThreadState(*this, useV6, useZerocopy, iface, cnct)
    {

        // FIXME: get the MTU from interface and attempt to set as outgoing (on Linux).
        // set maxPldLen
        ;
    }
#endif

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
        boost::thread sendT(&Segmenter::SendThreadState::_threadBody, &sendThreadState);
        sendThreadState.threadObj = std::move(sendT);

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
            SyncHdr hdr{};
            e2sar::EventRate_t evtRate;

            // push the stats onto ring buffer (except the first time) and reset
            // locking is not needed as sync thread is the  
            // only one interacting with the circular buffer
            if (seg.currentSyncStartNano != 0) {
                struct SendStats statsToPush{seg.currentSyncStartNano, seg.eventsInCurrentSync};
                seg.eventStatsBuffer.push_back(statsToPush);
            }
            seg.currentSyncStartNano = currentTimeNanos;
            seg.eventsInCurrentSync = 0;

            // peek at segmenter metadata circular buffer to calculate event rate
            evtRate = seg.eventRate(currentTimeNanos);

            // fill the sync header using a mix of current and segmenter data
            // no locking needed - we only look at one field in seg - srcId
            seg.fillSyncHdr(&hdr, evtRate, currentTimeNanos);

            // send it off
            auto sendRes = _send(&hdr);
            // FIXME: do something with result?

            // sleep approximately
            // FIXME: we should probably check until > now after send completes
            auto until = nowT + boost::chrono::milliseconds(period_ms);
            boost::this_thread::sleep_until(until);
        }
        auto res = _close();
    }

    result<int> Segmenter::SyncThreadState::_open()
    {
        auto syncAddr = seg.dpuri.get_syncAddr();
        if (syncAddr.has_error())
            return syncAddr.error();

        // Socket for sending sync message to CP
        if (syncAddr.value().first.is_v6()) {
            if ((socketFd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
                seg.syncStats.errCnt++;
                seg.syncStats.lastErrno = errno;
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
                    seg.syncStats.errCnt++;
                    seg.syncStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
            }
        }
        else {
            if ((socketFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                seg.syncStats.errCnt++;
                seg.syncStats.lastErrno = errno;
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
                    seg.syncStats.errCnt++;
                    seg.syncStats.lastErrno = errno;
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
            seg.syncStats.msgCnt++;
            err = (int) send(socketFd, static_cast<void*>(hdr), sizeof(SyncHdr), 0);
        }
        else {
            if (isV6) {
                seg.syncStats.msgCnt++;
                err = (int) sendto(socketFd, static_cast<void*>(hdr), sizeof(SyncHdr), 0, 
                    (sockaddr * ) & GET_V6_SYNC_STRUCT(syncAddrStruct), sizeof(struct sockaddr_in6));
            }
            else {
                seg.syncStats.msgCnt++;
                err = (int) sendto(socketFd, static_cast<void*>(hdr), sizeof(SyncHdr), 0,  
                    (sockaddr * ) & GET_V4_SYNC_STRUCT(syncAddrStruct), sizeof(struct sockaddr_in));
            }
        }

        if (err == -1)
        {
            seg.syncStats.errCnt++;
            seg.syncStats.lastErrno = errno;
            return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
        }

        return 0;
    }

    void Segmenter::SendThreadState::_threadBody()
    {
        boost::unique_lock<boost::mutex> condLock(seg.sendThreadMtx);
        while(!seg.threadsStop)
        {
            // block to get event off the queue and send
            seg.sendThreadCond.wait_for(condLock, sleepTime);

            // try to pop off eventQueue
            EventQueueItem *item{nullptr};
            while(seg.eventQueue.pop(item))
            {
                // call send overriding event number as needed
                auto res = _send(item->event, item->bytes, 
                    (item->eventNum != 0 ? item->eventNum : seg.eventNum.value()));
                // FIXME: do something with result? 
                // maybe not - it should be taken care by lastErrno in
                // the stats block

                // update event counter
                seg.eventNum++;

                // call the callback
                if (item->callback != nullptr)
                    item->callback(item->cbArg);

                // push the item back to return queue
                seg.returnQueue.push(item);    
            }
        }
        auto res = _close();
    }

    result<int> Segmenter::SendThreadState::_open()
    {

        // Open v4 and v6 sockets for sending data message via DP
        //
        // First v6
        //
        sockaddr_in6 dataAddrStruct6{};
#ifdef ZEROCOPY_AVIALABLE
        int zerocopy = 1;
#endif
        // check that MTU setting was sane
        if (mtu <= TOTAL_HDR_LEN)
            return E2SARErrorInfo{E2SARErrorc::SocketError, "Insufficient MTU length to accommodate headers"};

        if (useV6)
        {
            auto dataAddr6 = seg.dpuri.get_dataAddrv6();
            if (dataAddr6.has_error())
                return dataAddr6.error();

            if ((socketFd6 = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
                seg.sendStats.errCnt++;
                seg.sendStats.lastErrno = errno;
                return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
            }

            // set SO_ZEROCOPY on Linux
            if (useZerocopy) {
#ifdef ZEROCOPY_AVAILABLE
                int errz = setsockopt(socketFd6, SOL_SOCKET, SO_ZEROCOPY, &zerocopy, sizeof(zerocopy));
                if (errz < 0) {
                    close(socketFd6);
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
#else
                close(socketFd6);
                seg.sendStats.errCnt++;
                seg.sendStats.lastErrno = errno;
                return E2SARErrorInfo{E2SARErrorc::SocketError, "Zerocopy not available on this platform"};
#endif
            }

            dataAddrStruct6.sin6_family = AF_INET6;
            dataAddrStruct6.sin6_port = htobe16(dataAddr6.value().second);
            inet_pton(AF_INET6, dataAddr6.value().first.to_string().c_str(), &dataAddrStruct6.sin6_addr);

            if (connectSocket) {
                int err = connect(socketFd6, (const sockaddr *) &dataAddrStruct6, sizeof(struct sockaddr_in6));
                if (err < 0) {
                    close(socketFd6);
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
            }
        }

        //
        // the v4 (always)
        //
        auto dataAddr4 = seg.dpuri.get_dataAddrv4();
        if (dataAddr4.has_error())
            return dataAddr4.error();

        if ((socketFd4 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            seg.sendStats.errCnt++;
            seg.sendStats.lastErrno = errno;
            return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
        }

        // set SO_ZEROCOPY if needed
        if (useZerocopy) {
#ifdef ZEROCOPY_AVAILABLE
            int errz = setsockopt(socketFd4, SOL_SOCKET, SO_ZEROCOPY, &zerocopy, sizeof(zerocopy));
            if (errz < 0) {
                close(socketFd4);
                seg.sendStats.errCnt++;
                seg.sendStats.lastErrno = errno;
                return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
            }
#else
            close(socketFd4);
            seg.sendStats.errCnt++;
            seg.sendStats.lastErrno = errno;
            return E2SARErrorInfo{E2SARErrorc::SocketError, "Zerocopy not available on this platform"};
#endif
        }

        sockaddr_in dataAddrStruct4{};
        dataAddrStruct4.sin_family = AF_INET;
        dataAddrStruct4.sin_port = htobe16(dataAddr4.value().second);
        inet_pton(AF_INET, dataAddr4.value().first.to_string().c_str(), &dataAddrStruct4.sin_addr);

        if (connectSocket) {
            int err = connect(socketFd4, (const sockaddr *) &dataAddrStruct4, sizeof(struct sockaddr_in));
            if (err < 0) {
                seg.sendStats.errCnt++;
                seg.sendStats.lastErrno = errno;
                close(socketFd4);
                return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
            }
        }

        dataAddrStruct = boost::make_tuple<sockaddr_in, sockaddr_in6>(dataAddrStruct4, dataAddrStruct6);

        return 0;
    }

    // fragment and send the event
    result<int> Segmenter::SendThreadState::_send(u_int8_t *event, size_t bytes, EventNum_t altEventNum)
    {
        int err;
        struct iovec iov[2];
        int sendSocket{0};
        // having our own copy on the stack means we can call _send off-thread
        struct msghdr sendhdr{};
#ifdef ZEROCOPY_AVAILABLE
        int flags{(useZerocopy ? MSG_ZEROCOPY: 0)};
#else   
        int flags{0};
#endif

        sendSocket = (useV6 ? socketFd6 : socketFd4);
        // prepare msghdr
        if (connectSocket) {
            // prefill with blank
            sendhdr.msg_name = nullptr;
            sendhdr.msg_namelen = 0;
        } else {
            // prefill from persistent struct
            if (useV6) {
                sendhdr.msg_name = (sockaddr_in *)& GET_V6_SEND_STRUCT(dataAddrStruct);
            } else {
                sendhdr.msg_name = (sockaddr_in *)& GET_V4_SEND_STRUCT(dataAddrStruct);
            }
            // prefill - always the same
            sendhdr.msg_namelen = sizeof(sockaddr_in);
        }

        // fragment event, update/set iov and send in a loop
        u_int8_t *curOffset = event;
        u_int8_t *eventEnd = event + bytes;
        size_t curLen = (bytes <= maxPldLen ? bytes : maxPldLen);

        // break up event into a series of datagrams prepended with LB+RE header
        while (curOffset < eventEnd)
        {
            // fill out LB and RE headers
            auto hdr = lbreHdrPool.construct();

            // are we overwriting the event number?
            EventNum_t finalEventNum = (altEventNum == 0LL ? seg.eventNum.value() : altEventNum);
            hdr->re.set(seg.srcId, curOffset - event, curLen, finalEventNum);
            hdr->lb.set(seg.entropy, finalEventNum);

            // fill in iov and attach to msghdr
            // LB+RE header
            iov[0].iov_base = hdr;
            iov[0].iov_len = sizeof(LBREHdr);
            // event segment
            iov[1].iov_base = curOffset;
            iov[1].iov_len = curLen;
            sendhdr.msg_iov = iov;
            sendhdr.msg_iovlen = 2;

            // update offset and length for next segment
            curOffset += curLen;
            curLen = (eventEnd > curOffset + maxPldLen ? maxPldLen : eventEnd - curOffset);

#ifdef ZEROCOPY_AVIALABLE
            if (useZerocopy)
            {
                // do recvmsg to get notification of successful transmission so header buffers
                // can be freed up. Also to signal user via callback this is finished and event buffer
                // can be reclaimed. Take care of both immediate send and queued send scenarios.
                // To be continued.
                err = (int) sendmsg(sendSocket, &sendhdr, flags);
            }
            else
#endif
            // Note: we don't need to check useZerocopy here - socket would've been closed
            // in _open() call
            {
                // non zerocopy method
                seg.sendStats.msgCnt++;
                err = (int) sendmsg(sendSocket, &sendhdr, flags);
                if (err == -1)
                {
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
                // free the header here for this situation
                lbreHdrPool.free(hdr);
            } 
        }
        // update the event send stats
        seg.eventsInCurrentSync++;

        return 0;
    }

    result<int> Segmenter::SendThreadState::_close()
    {
        close(socketFd4);
        if (useV6)
            close(socketFd6);
        return 0;
    }

    // Blocking call. Event number automatically set.
    // Any core affinity needs to be done by caller.
    result<int> Segmenter::sendEvent(u_int8_t *event, size_t bytes) noexcept
    {
        freeEventItemBacklog();
        return sendThreadState._send(event, bytes, 0LL);
    }

    // Blocking call specifying event number.
    result<int> Segmenter::sendEvent(u_int8_t *event, size_t bytes, uint64_t eventNumber) noexcept
    {
        freeEventItemBacklog();
        return sendThreadState._send(event, bytes, eventNumber);
    }

    // Non-blocking call to place event on internal queue.
    // Event number automatically set at send time
    result<int> Segmenter::addToSendQueue(u_int8_t *event, size_t bytes, 
        void* (*callback)(boost::any), 
        boost::any cbArg) noexcept
    {
        return addToSendQueue(event, bytes, 0, callback, cbArg);
    }

    // Non-blocking call specifying explicit event number.
    result<int> Segmenter::addToSendQueue(u_int8_t *event, size_t bytes, 
        uint64_t eventNumber, 
        void* (*callback)(boost::any), 
        boost::any cbArg) noexcept
    {
        freeEventItemBacklog();
        EventQueueItem *item = queueItemPool.construct();
        item->bytes = bytes;
        item->event = event;
        item->callback = callback;
        item->cbArg = cbArg;
        item->eventNum = eventNumber;
        eventQueue.push(item);
        // wake up send thread (no need to hold the lock as queue is lock_free)
        sendThreadCond.notify_one();
        return 0;
    }
}
