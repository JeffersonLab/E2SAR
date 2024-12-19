#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/detail/file_parser_error.hpp>
#include <iostream>

#include "portable_endian.h"

#include "e2sarDPSegmenter.hpp"


namespace e2sar 
{
    // set the sleep timer for the send thread - how long it waits on
    // a condition for new events
    const boost::chrono::milliseconds Segmenter::sleepTime(10);

    Segmenter::Segmenter(const EjfatURI &uri, u_int16_t did, u_int32_t esid, 
        const SegmenterFlags &sflags): 
        dpuri{uri}, 
        dataId{did},
        eventSrcId{esid},
        numSendSockets{sflags.numSendSockets},
        sndSocketBufSize{sflags.sndSocketBufSize},
        eventStatsBuffer{sflags.syncPeriods},
        syncThreadState(*this, sflags.syncPeriodMs, sflags.connectedSocket), 
        sendThreadState(*this, sflags.dpV6, sflags.zeroCopy, sflags.mtu, sflags.connectedSocket),
        useCP{sflags.useCP},
        zeroRate{sflags.zeroRate},
        usecAsEventNum{sflags.usecAsEventNum},
        addEntropy{(clockEntropyTest() > MIN_CLOCK_ENTROPY ? false : true)}
    {
        size_t mtu = 0;
#if NETLINK_CAPABLE
        // determine the outgoing interface and its MTU based on URI data address 
        ip::address dataaddr;
        if (sflags.dpV6) 
        {
            // use dataplane v6 address for a routing query
            auto data = dpuri.get_dataAddrv6();
            if (data.has_error())
                throw E2SARException("IPV6 Data address is not present in the URI");
            dataaddr = data.value().first;
        } else  
        {
            // use dataplane v4 address for a routing query
            auto data = dpuri.get_dataAddrv4();
            if (data.has_error())
                throw E2SARException("IPV4 Data address is not present in the URI");
            dataaddr = data.value().first;
        }
        auto destintfres = NetUtil::getInterfaceAndMTU(dataaddr);

        if (destintfres.has_error())
            throw E2SARException("Unable to determine outgoing interface for ");

        auto destintfmtu = destintfres.value().get<1>();
        sendThreadState.iface = destintfres.value().get<0>();

        if (sflags.mtu == 0)
            mtu = destintfmtu;
        else
            if (sflags.mtu <= destintfmtu)
                mtu = sflags.mtu;
            else
                throw E2SARException("Segmenter flags MTU override value exceeds outgoing interface MTU");
#else
        if (sflags.mtu == 0)
            throw E2SARException("Unable to detect outgoing interface MTU on this platform");
        else
            mtu = sflags.mtu;
#endif
        // override the value set in constructor
        sendThreadState.mtu = mtu;
        sanityChecks();
    }

    result<int> Segmenter::openAndStart() noexcept
    {
        if (useCP)
        {
            // open and connect sync socket
            auto status = syncThreadState._open();
            if (status.has_error()) 
            {
                return E2SARErrorInfo{E2SARErrorc::SocketError, 
                    "Unable to open sync socket: " + status.error().message()};
            }
            // start the sync thread from method
            boost::thread syncT(&Segmenter::SyncThreadState::_threadBody, &syncThreadState);
            syncThreadState.threadObj = std::move(syncT);
        }

        // open and connect send sockets
        auto status = sendThreadState._open();
        if (status.has_error())
        {
            return E2SARErrorInfo{E2SARErrorc::SocketError,
                "Unable to open data socket: " + status.error().message()};
        }

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
            auto nowT = boost::chrono::system_clock::now();
            // Convert the time point to nanoseconds since the epoch
            auto now = boost::chrono::duration_cast<boost::chrono::nanoseconds>(nowT.time_since_epoch()).count();
            UnixTimeNano_t currentTimeNanos = static_cast<UnixTimeNano_t>(now);
            // get sync header buffer
            SyncHdr hdr{};
            e2sar::EventRate_t evtRate;

            // push the stats onto ring buffer (except the first time) and reset
            // locking is not needed as sync thread is the  
            // only one interacting with the circular buffer
            if (seg.currentSyncStartNano != 0) {
                struct SendStats statsToPush{seg.currentSyncStartNano.exchange(currentTimeNanos), 
                    seg.eventsInCurrentSync.exchange(0)};
                seg.eventStatsBuffer.push_back(statsToPush);
            } else {
                // just the first time (eventsInCurrentSync already initialized to 0)
                seg.currentSyncStartNano = currentTimeNanos;
            }

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
                    item->eventNum, item->dataId,
                    item->entropy);
                // FIXME: do something with result? 
                // maybe not - it should be taken care by lastErrno in
                // the stats block

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

#ifdef ZEROCOPY_AVIALABLE
        int zerocopy = 1;
#endif
        // check that MTU setting was sane
        if (mtu <= TOTAL_HDR_LEN)
            return E2SARErrorInfo{E2SARErrorc::SocketError, "Insufficient MTU length to accommodate headers"};

        // create numSendSocket bound sockets either v6 or v4. With each socket
        // we save the sockaddr structure in case they are of not connected variety
        // so we can use in sendmsg
        if (useV6) 
        {
            auto dataAddr6 = seg.dpuri.get_dataAddrv6();
            if (dataAddr6.has_error())
                return dataAddr6.error();
            for(auto i = socketFd6.begin(); i != socketFd6.end(); ++i) 
            {
                int fd{0};
                bool done{false};
                int numTries{5};
                sockaddr_in6 localAddrStruct6{};
                localAddrStruct6.sin6_family = AF_INET6;
                localAddrStruct6.sin6_addr = in6addr_any; // Bind to any local address

                while(!done && (numTries > 0)) 
                {
                    // get random source port
                    u_int16_t randomPort = portDist(ranlux);

                    // open and bind a socket to this port (try)
                    if ((fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
                        seg.sendStats.errCnt++;
                        seg.sendStats.lastErrno = errno;
                        return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                    }

                    localAddrStruct6.sin6_port = htobe16(randomPort); // Set the source port

                    if (bind(fd, (struct sockaddr *)&localAddrStruct6, sizeof(localAddrStruct6)) < 0) {
                        close(fd);
                        numTries--;
                        // try another port
                        continue;
                    }
                    done = true;
                }
                if (!done)
                {
                    // failed to bind socket after N tries
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }

                // set sndBufSize
                if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &seg.sndSocketBufSize, sizeof(seg.sndSocketBufSize)) < 0) {
                    close(fd);
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
                
                // set SO_ZEROCOPY on Linux
                if (useZerocopy) {
#ifdef ZEROCOPY_AVAILABLE
                    int zerocopy = 1;
                    int errz = setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &zerocopy, sizeof(zerocopy));
                    if (errz < 0) {
                        close(fd);
                        seg.sendStats.errCnt++;
                        seg.sendStats.lastErrno = errno;
                        return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                    }
#else
                    close(fd);
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, "Zerocopy not available on this platform"};
#endif
                }

                sockaddr_in6 dataAddrStruct6{};
                dataAddrStruct6.sin6_family = AF_INET6;
                dataAddrStruct6.sin6_port = htobe16(dataAddr6.value().second);
                inet_pton(AF_INET6, dataAddr6.value().first.to_string().c_str(), &dataAddrStruct6.sin6_addr);

                if (connectSocket) {
                    int err = connect(fd, (const sockaddr *) &dataAddrStruct6, sizeof(struct sockaddr_in6));
                    if (err < 0) {
                        close(fd);
                        seg.sendStats.errCnt++;
                        seg.sendStats.lastErrno = errno;
                        return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                    }
                }
                *i = boost::make_tuple<int, sockaddr_in6, sockaddr_in6>(fd, localAddrStruct6, dataAddrStruct6);
            }
        } else 
        {
            auto dataAddr4 = seg.dpuri.get_dataAddrv4();
            if (dataAddr4.has_error())
                return dataAddr4.error();
            for(auto i = socketFd4.begin(); i != socketFd4.end(); ++i) 
            {
                int fd{0};
                bool done{false};
                int numTries{5};
                sockaddr_in localAddrStruct4{};
                localAddrStruct4.sin_family = AF_INET;
                localAddrStruct4.sin_addr.s_addr = htobe16(INADDR_ANY); // Bind to any local address

                u_int16_t randomPort{0};
                while(!done && (numTries > 0)) 
                {
                    // get random source port
                    randomPort = portDist(ranlux);

                    // open and bind a socket to this port (try)
                    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                        seg.sendStats.errCnt++;
                        seg.sendStats.lastErrno = errno;
                        return E2SARErrorInfo{E2SARErrorc::SocketError, "Unable to open socket: "s + strerror(errno)};
                    }

                    localAddrStruct4.sin_port = htobe16(randomPort); // Set the source port

                    if (bind(fd, (struct sockaddr *)&localAddrStruct4, sizeof(localAddrStruct4)) < 0) {
                        close(fd);
                        numTries--;
                        // try another port
                        continue;
                    }
                    done = true;
                }
                if (!done)
                {
                    // failed to bind socket after N tries
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, "Unable to bind: "s + strerror(errno)};
                }

                // set sndBufSize
                if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &seg.sndSocketBufSize, sizeof(seg.sndSocketBufSize)) < 0) {
                    close(fd);
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }

                // set SO_ZEROCOPY if needed
                if (useZerocopy) {
#ifdef ZEROCOPY_AVAILABLE
                   int zerocopy = 1;
                    int errz = setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &zerocopy, sizeof(zerocopy));
                    if (errz < 0) {
                        close(fd);
                        seg.sendStats.errCnt++;
                        seg.sendStats.lastErrno = errno;
                        return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                    }
#else
                    close(fd);
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
                    int err = connect(fd, (const sockaddr *) &dataAddrStruct4, sizeof(struct sockaddr_in));
                    if (err < 0) {
                        seg.sendStats.errCnt++;
                        seg.sendStats.lastErrno = errno;
                        close(fd);
                        return E2SARErrorInfo{E2SARErrorc::SocketError, "Unable to connect: "s + strerror(errno)};
                    }
                }
                *i = boost::make_tuple<int, sockaddr_in, sockaddr_in>(fd, localAddrStruct4, dataAddrStruct4);
            }
        }

        return 0;
    }

    // fragment and send the event
    result<int> Segmenter::SendThreadState::_send(u_int8_t *event, size_t bytes, 
        EventNum_t eventNum, u_int16_t dataId, u_int16_t entropy)
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
        // new random entropy generated for each event buffer, unless user specified it
        if (entropy == 0)
            entropy = randDist(ranlux);

        if (roundRobinIndex == socketFd4.size())
            roundRobinIndex = 0;

        // randomize source port using round robin
        sendSocket = (useV6 ? GET_FD(socketFd6, roundRobinIndex) : GET_FD(socketFd4, roundRobinIndex));
        // prepare msghdr
        if (connectSocket) {
            // prefill with blank
            sendhdr.msg_name = nullptr;
            sendhdr.msg_namelen = 0;
        } else {
            // prefill from persistent struct
            if (useV6) {
                sendhdr.msg_name = (sockaddr_in *)& GET_REMOTE_SEND_STRUCT(socketFd6, roundRobinIndex);
            } else {
                sendhdr.msg_name = (sockaddr_in *)& GET_REMOTE_SEND_STRUCT(socketFd4, roundRobinIndex);
            }
            // prefill - always the same
            sendhdr.msg_namelen = sizeof(sockaddr_in);
        }
        roundRobinIndex++;

        // fragment event, update/set iov and send in a loop
        u_int8_t *curOffset = event;
        u_int8_t *eventEnd = event + bytes;
        size_t curLen = (bytes <= maxPldLen ? bytes : maxPldLen);

        // update the event number being reported in Sync and LB packets
        if (seg.usecAsEventNum)
        {
            // use microseconds since the UNIX Epoch, but make sure the entropy
            // of the 8lsb is sufficient (for LB)
            // Get the current time point
            auto nowT = boost::chrono::system_clock::now();
            // Convert the time point to microseconds since the epoch
            auto now = boost::chrono::duration_cast<boost::chrono::microseconds>(nowT.time_since_epoch()).count();
            if (seg.addEntropy) 
                seg.lbEventNum = seg.addClockEntropy(now);
            else
                seg.lbEventNum = now;
        } else
            // use current user-specified or sequential event number
            seg.lbEventNum = eventNum;

        // break up event into a series of datagrams prepended with LB+RE header
        while (curOffset < eventEnd)
        {
            // fill out LB and RE headers
            auto hdr = lbreHdrPool.construct();

            // note that buffer length is in fact event length, hence 3rd parameter is 'bytes'
            hdr->re.set(dataId, curOffset - event, bytes, eventNum);
            hdr->lb.set(entropy, seg.lbEventNum);

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
        for(auto f: socketFd4)
            close(f.get<0>());
        if (useV6)
            for (auto f: socketFd6)
                close(f.get<0>());
        return 0;
    }

    // Blocking call specifying event number.
    result<int> Segmenter::sendEvent(u_int8_t *event, size_t bytes, 
        EventNum_t _eventNum, u_int16_t _dataId, u_int16_t entropy) noexcept
    {
        freeEventItemBacklog();
        // reset local event number to override
        if (_eventNum != 0)
            userEventNum.exchange(_eventNum);

        // use specified event number and dataId
        return sendThreadState._send(event, bytes, 
        // continue incrementing
            userEventNum++, 
            (_dataId  == 0 ? dataId : _dataId), 
            entropy);
    }

    // Non-blocking call specifying explicit event number.
    result<int> Segmenter::addToSendQueue(u_int8_t *event, size_t bytes, 
        EventNum_t _eventNum, u_int16_t _dataId, u_int16_t entropy,
        void (*callback)(boost::any), 
        boost::any cbArg) noexcept
    {
        freeEventItemBacklog();
        // reset local event number to override
        if (_eventNum != 0)
            userEventNum.exchange(_eventNum);
        EventQueueItem *item = queueItemPool.construct();
        item->bytes = bytes;
        item->event = event;
        item->entropy = entropy;
        item->callback = callback;
        item->cbArg = cbArg;
        // continue incrementing 
        item->eventNum = userEventNum++;
        item->dataId = (_dataId  == 0 ? dataId : _dataId);
        eventQueue.push(item);
        // wake up send thread (no need to hold the lock as queue is lock_free)
        sendThreadCond.notify_one();
        return 0;
    }

    result<Segmenter::SegmenterFlags> Segmenter::SegmenterFlags::getFromINI(const std::string &iniFile) noexcept
    {
        boost::property_tree::ptree paramTree;
        Segmenter::SegmenterFlags sFlags;

        try {
            boost::property_tree::ini_parser::read_ini(iniFile, paramTree);
        } catch(boost::property_tree::ini_parser_error &ie) {
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, 
                "Unable to parse the segmenter flags configuration file "s + iniFile};
        }

        // general
        sFlags.useCP = paramTree.get<bool>("general.useCP", sFlags.useCP);

        // control plane
        sFlags.syncPeriods = paramTree.get<u_int16_t>("control-plane.syncPeriods", 
            sFlags.syncPeriods);
        sFlags.syncPeriodMs = paramTree.get<u_int16_t>("control-plane.syncPeriodMS", 
            sFlags.syncPeriodMs);
        sFlags.zeroRate = paramTree.get<bool>("control-plane.zeroRate",
            sFlags.zeroRate);
        sFlags.usecAsEventNum = paramTree.get<bool>("control-plane.usecAsEventNum",
            sFlags.usecAsEventNum);

        // data plane
        sFlags.dpV6 = paramTree.get<bool>("data-plane.dpV6", sFlags.dpV6);
        sFlags.zeroCopy = paramTree.get<bool>("data-plane.zeroCopy", sFlags.zeroCopy);
        sFlags.connectedSocket = paramTree.get<bool>("data-plane.connectedSocket", 
            sFlags.connectedSocket);
        sFlags.mtu = paramTree.get<u_int16_t>("data-plane.mtu", sFlags.mtu);
        sFlags.numSendSockets = paramTree.get<size_t>("data-plane.numSendSockets", 
            sFlags.numSendSockets);
        sFlags.sndSocketBufSize = paramTree.get<int>("data-plane.sndSocketBufSize", 
            sFlags.sndSocketBufSize);

        return sFlags;
    }
}
