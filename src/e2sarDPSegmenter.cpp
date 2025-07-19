#include <sys/ioctl.h>

#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/detail/file_parser_error.hpp>
#include <iostream>

#include "portable_endian.h"

#include "e2sarDPSegmenter.hpp"
#include "e2sarUtil.hpp"
#include "e2sarAffinity.hpp"


namespace e2sar 
{
    Segmenter::Segmenter(const EjfatURI &uri, u_int16_t did, u_int32_t esid, 
        const SegmenterFlags &sflags):
        Segmenter(uri, did, esid, std::vector<int>(), sflags)
    {
    }

    Segmenter::Segmenter(const EjfatURI &uri, u_int16_t did, u_int32_t esid, 
        std::vector<int> cpuCoreList,
        const SegmenterFlags &sflags): 
        dpuri{uri}, 
        dataId{did},
        eventSrcId{esid},
        numSendSockets{sflags.numSendSockets},
        sndSocketBufSize{sflags.sndSocketBufSize},
        rateGbps{sflags.rateGbps},
        rateLimit{(sflags.rateGbps > 0.0 ? true: false)},
        smooth{sflags.smooth},
        multiPort{sflags.multiPort},
        eventStatsBuffer{sflags.syncPeriods},
        syncThreadState(*this, sflags.syncPeriodMs, sflags.connectedSocket), 
        // set thread index to 0 for a single send thread
        sendThreadState(*this, 0, sflags.dpV6, sflags.mtu, sflags.connectedSocket),
        cpuCoreList{cpuCoreList},
#ifdef LIBURING_AVAILABLE
        rings(sflags.numSendSockets),
        ringMtxs(sflags.numSendSockets),
#endif
        warmUpMs{sflags.warmUpMs},
        useCP{sflags.useCP},
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
                throw E2SARException("IPV6 Data address is not present in the URI"s);
            dataaddr = data.value().first;
        } else
        {
            // use dataplane v4 address for a routing query
            auto data = dpuri.get_dataAddrv4();
            if (data.has_error())
                throw E2SARException("IPV4 Data address is not present in the URI"s);
            dataaddr = data.value().first;
        }
        auto destintfres = NetUtil::getInterfaceAndMTU(dataaddr);

        if (destintfres.has_error())
            throw E2SARException("Unable to determine outgoing interface for LB destination address "s + 
                dataaddr.to_string());

        auto destintfmtu = destintfres.value().get<1>();
        sendThreadState.iface = destintfres.value().get<0>();

        if (sflags.mtu == 0)
        {
            if (destintfmtu > 0)
                // if reported MTU size is non-zero we can use it
                mtu = destintfmtu;
            else
                throw E2SARException("Outgoing interface MTU is reported as 0, please use manual override of MTU size");
        }
        else
            if (destintfmtu > 0 )
            {
                // if destination interface reports proper non 0 MTU (lo doesn't)
                // then we can check the override against the reported size
                if (sflags.mtu <= destintfmtu)
                    mtu = sflags.mtu;
                else
                    throw E2SARException("Segmenter flags MTU override value exceeds outgoing interface MTU of "s + 
                        destintfres.value().get<0>());
            }
            else
                // if destination interface reports 0 MTU we just use the override
                mtu = sflags.mtu;
#else
        if (sflags.mtu == 0)
            throw E2SARException("Unable to detect outgoing interface MTU on this platform"s);
        else
            mtu = sflags.mtu;
#endif
        // override the values set in constructor
        sendThreadState.mtu = mtu;
        sendThreadState.maxPldLen = mtu - TOTAL_HDR_LEN;
        
#ifdef LIBURING_AVAILABLE
        if (Optimizations::isSelected(Optimizations::Code::liburing_send))
        {
            // probe for SENDMSG
            struct io_uring_probe *probe = io_uring_get_probe();

            if (probe == nullptr)
                throw E2SARException("Unable to allocate io_uring probe, unable to proceed using liburing"s);

            if (not io_uring_opcode_supported(probe, IORING_OP_SENDMSG))
            {
                free(probe);
                throw E2SARException("Your kernel does not support the expected IO_URING operations (IORING_OP_SENDMSG)"s);
            }
            free(probe);
            // init ring
            struct io_uring_params params;
            memset(&params, 0, sizeof(params));
            // setup the kernel polling thread (it goes to sleep and restarts as needed under the covers)
            // note we will register the file descriptors with the ring a bit later
            params.flags |= IORING_SETUP_SQPOLL;
            params.sq_thread_idle = pollWaitTime;

            // set the ring vector to the number of sockets (there will be one thread per)
            for(size_t i = 0; i < rings.size(); ++i)
            {
                int err = io_uring_queue_init_params(uringSize, &rings[i], &params);
                if (err)
                    throw E2SARException("Unable to allocate uring due to "s + strerror(errno));
            }
        }
#endif
        sanityChecks();

        // set process affinity to the set of threads provided
        if (cpuCoreList.size() > 0)
        {
            auto res = Affinity::setProcess(cpuCoreList);
            if (res.has_error())
            {
                throw E2SARException("Unable to set process affinity to indicated threads: "s + res.error().message());
            }
        }
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
            // provide a 1 second warm-up period of sending SYNCs and no data
            boost::this_thread::sleep_for(boost::chrono::milliseconds(warmUpMs));
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

#ifdef LIBURING_AVAILABLE
    void Segmenter::SendThreadState::_reap(size_t roundRobinIndex)
    {
        static thread_local struct io_uring_cqe *cqes[cqeBatchSize];

        // loop while CQEs are available
        while(true)
        {
            memset(static_cast<void*>(cqes), 0, sizeof(struct io_uring_cqe *) * cqeBatchSize);
            int ret = io_uring_peek_batch_cqe(&seg.rings[roundRobinIndex], cqes, cqeBatchSize);
            // error or returned nothing
            if (ret <= 0)
            {
                // don't log error here - it is usually a temporary resource availability
                return;
            }
            for(int idx = 0; idx < ret; ++idx)
            {
                // we have a CQE now
                // check for errors from sendmsg
                if (cqes[idx]->res < 0)
                {
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = cqes[idx]->res;
                }
                auto sqeUserData = reinterpret_cast<SQEUserData*>(cqes[idx]->user_data);
                auto callback = sqeUserData->callback;
                auto cbArg = std::move(sqeUserData->cbArg);
                // free the memory
                auto sqeMsgHdr = sqeUserData->msghdr;
                // deallocate the LBRE header
                free(sqeMsgHdr->msg_iov[0].iov_base);
                // deallocate the iovec itself
                free(sqeMsgHdr->msg_iov);
                // deallocate struct msghdr
                free(sqeMsgHdr);
                // deallocate sqeUserData
                free(sqeUserData);
                // mark CQE as done
                io_uring_cqe_seen(&seg.rings[roundRobinIndex], cqes[idx]);
                // call the callback if not null (would happen at the end of the event buffer)
                if (callback != nullptr)
                    callback(cbArg);
            }
            seg.outstandingSends -= ret;
        }
    }
#endif

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

            // fill the sync header using a mix of current and segmenter data
            // no locking needed - we only look at one field in seg - srcId
            seg.fillSyncHdr(&hdr, currentTimeNanos);

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

        // create a thread pool for sending events 
        thread_local boost::asio::thread_pool threadPool(seg.numSendSockets);
        boost::chrono::high_resolution_clock::time_point nowTE;
        int64_t interEventSleepUsec{0};
        // per thread rate and inter-frame sleep if needed
        const float smoothThreadRateGbps{seg.smooth ? seg.rateGbps/seg.numSendSockets : (float)-1.0};
        int64_t interFrameSleepUsec{static_cast<int64_t>(seg.smooth ? seg.getMTU()*8/(smoothThreadRateGbps * 1000): 0)};

        while(!seg.threadsStop)
        {
            // block to get event off the queue and send
            //seg.sendThreadCond.wait_for(condLock, sleepTime);

            // try to pop off eventQueue
            EventQueueItem *item{nullptr};
            while(seg.eventQueue.pop(item))
            {
                if (not seg.smooth && seg.rateLimit)
                {
                    // if rate limiting is enabled, we will use high-res clock for inter-event and inter-frame sleep
                    nowTE = boost::chrono::high_resolution_clock::now();
                    // convert send rate into inter-event sleep time 
                    interEventSleepUsec = static_cast<int64_t>(item->bytes*8/(seg.rateGbps * 1000));
                }
                // round robin through sending sockets
                seg.roundRobinIndex = (seg.roundRobinIndex + 1) % seg.numSendSockets;

                auto rri = seg.roundRobinIndex;

                // FIXME: do something with result? 
                // it IS  taken care by lastErrno in the stats block. 
                // Note that in the case of liburing
                // result cannot reflect the status of any of the send
                // operations since it is asynchronous - that is collected 
                // by a separate thread that looks at completion queue
                // and reflects into the stats block
                boost::asio::post(threadPool,
                    [this, rri, item, interFrameSleepUsec]() {
#ifdef LIBURING_AVAILABLE
                        if (Optimizations::isSelected(Optimizations::Code::liburing_send)) 
                            seg.ringMtxs[rri].lock();
#endif 
                        auto res = _send(item->event, item->bytes, 
                            item->eventNum, item->dataId,
                            item->entropy, rri, interFrameSleepUsec,
                            item->callback, item->cbArg);

#ifdef LIBURING_AVAILABLE
                        if (Optimizations::isSelected(Optimizations::Code::liburing_send)) 
                        {
                            // reap the CQEs and will call the callback if needed
                            _reap(rri);
                            seg.ringMtxs[rri].unlock();
                        }
                        else
#endif
                        {
                            // call the callback on the worker thread (except with liburing)
                            if (item->callback != nullptr)
                                item->callback(item->cbArg);
                        }
                        
                        // free item here
                        free(item);
                });

                // busy wait if needed for inter-event period 
                // with smoothing on the waiting is in each send thread
                if (not seg.smooth && seg.rateLimit && interEventSleepUsec > 0)
                {
                    busyWaitUsecs(nowTE, interEventSleepUsec);       
                }
            }
        }
        // wait for all threads to complete
        threadPool.join();

#ifdef LIBURING_AVAILABLE
        // reap the remaining CQEs
        while(seg.outstandingSends > 0) 
        {
            for(size_t rri{0}; rri < seg.numSendSockets; ++rri)
                _reap(rri);
        }
#endif

        auto res = _close();
    }

    result<int> Segmenter::SendThreadState::_open()
    {
#ifdef LIBURING_AVAILABLE
        // allocate int[] for passing FDs into the rings
        int ringFds[socketFd4.size()];
#endif
        unsigned int fdCount{0};

        // Open v4 and v6 sockets for sending data message via DP

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

                sockaddr_in6 dataAddrStruct6{};
                dataAddrStruct6.sin6_family = AF_INET6;
                // use consecutive destination ports of requested
                if (seg.multiPort)
                    dataAddrStruct6.sin6_port = htobe16(dataAddr6.value().second + fdCount);
                else
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
#ifdef LIBURING_AVAILABLE
                ringFds[fdCount] = fd;
#endif
                fdCount++;
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

                sockaddr_in dataAddrStruct4{};
                dataAddrStruct4.sin_family = AF_INET;
                // use consecutive destination ports of requested
                if (seg.multiPort)
                    dataAddrStruct4.sin_port = htobe16(dataAddr4.value().second + fdCount);
                else
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
#ifdef LIBURING_AVAILABLE
                ringFds[fdCount] = fd;
#endif
                fdCount++;
            }
        }

#ifdef LIBURING_AVAILABLE
        // register open file descriptors with the rings
        if (Optimizations::isSelected(Optimizations::Code::liburing_send))
        {
            for (size_t i = 0; i < seg.rings.size(); ++i)
            {
                // register all fds with each ring
                int ret = io_uring_register_files(&seg.rings[i], ringFds, fdCount);
                if (ret < 0)
                {
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = ret;
                }
            }
        }
#endif

        return 0;
    }

    // fragment and send the event
    result<int> Segmenter::SendThreadState::_send(u_int8_t *event, size_t bytes, 
        EventNum_t eventNum, u_int16_t dataId, u_int16_t entropy, size_t roundRobinIndex,
        int64_t interFrameSleepUsec, void (*callback)(boost::any), boost::any cbArg)
    {
        int err;
        int sendSocket{0};
        // having our own copy on the stack means we can call _send off-thread
        struct msghdr sendhdr{};
        int flags{0};
        // number of buffers we will send
        size_t numBuffers{(bytes + maxPldLen - 1)/ maxPldLen}; // round up
        // if needed for interframe wait
        boost::chrono::high_resolution_clock::time_point nowTF;

#ifdef SENDMMSG_AVAILABLE 
        // allocate mmsg vector based on event buffer size using fast int ceiling
        struct mmsghdr *mmsgvec = nullptr;
        size_t packetIndex = 0;
        if (Optimizations::isSelected(Optimizations::Code::sendmmsg))
            // don't need calloc to spend time initializing it
            mmsgvec = static_cast<struct mmsghdr*>(malloc(numBuffers*sizeof(struct mmsghdr)));
#endif

        // new random entropy generated for each event buffer, unless user specified it
        if (entropy == 0)
            entropy = randDist(ranlux);

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

        // fragment event, update/set iov and send in a loop
        u_int8_t *curOffset = event;
        u_int8_t *eventEnd = event + bytes;
        size_t curLen = (bytes <= maxPldLen ? bytes : maxPldLen);

        // Get the current time point of event start
        auto nowT = boost::chrono::system_clock::now();

        // update the event number being reported in Sync and LB packets
        // use microseconds since the UNIX Epoch, but make sure the entropy
        // of the 8lsb is sufficient (for LB)

        // Convert the time point to microseconds since the epoch
        auto now = boost::chrono::duration_cast<boost::chrono::microseconds>(nowT.time_since_epoch()).count();
        EventNum_t lbEventNum{0};
        if (seg.addEntropy) 
            lbEventNum = seg.addClockEntropy(now);
        else
            lbEventNum = now;

        // break up event into a series of datagrams prepended with LB+RE header
        while (curOffset < eventEnd)
        {
            if (interFrameSleepUsec > 0)
                nowTF = boost::chrono::high_resolution_clock::now();
            // fill out LB and RE headers
            void *hdrspace = malloc(sizeof(LBREHdr));
            // placement-new to construct the headers
            auto hdr = new (hdrspace) LBREHdr();
            // allocate iov (this returns two entries)
            auto iov = static_cast<struct iovec*>(malloc(2*sizeof(struct iovec)));

            // note that buffer length is in fact event length, hence 3rd parameter is 'bytes'
            hdr->re.set(dataId, curOffset - event, bytes, eventNum);
            hdr->lb.set(entropy, lbEventNum);

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

#ifdef SENDMMSG_AVAILABLE
            if (Optimizations::isSelected(Optimizations::Code::sendmmsg))
            {
                // copy the contents of sendhdr into appropriate index of mmsgvec
                memcpy(&mmsgvec[packetIndex].msg_hdr, &sendhdr, sizeof(sendhdr));
                packetIndex++;
            }
            else 
#endif
#ifdef LIBURING_AVAILABLE
            if (Optimizations::isSelected(Optimizations::Code::liburing_send))
            {
                seg.sendStats.msgCnt++;
                // get an SQE and fill it out
                struct io_uring_sqe *sqe{nullptr};
                // busy-wait for a free sqe to become available
                while(not(sqe = io_uring_get_sqe(&seg.rings[roundRobinIndex])));
                // allocate struct msghdr - we can't use the stack version here
                struct msghdr *sqeMsgHdr = static_cast<struct msghdr*>(malloc(sizeof(struct msghdr)));
                memcpy(sqeMsgHdr, &sendhdr, sizeof(struct msghdr));
                // allocate SQEUserData, make sure allocated memory is zeroed out
                SQEUserData *sqeUserData = static_cast<SQEUserData*>(calloc(1, sizeof(SQEUserData)));
                sqeUserData->msghdr = sqeMsgHdr;
                if (curOffset >= eventEnd)
                {
                    // this is the last segment, so we give this to CQE
                    sqeUserData->callback = callback;
                    sqeUserData->cbArg = std::move(cbArg);
                } else
                {
                    // this is not the last segment
                    sqeUserData->callback = nullptr;
                    sqeUserData->cbArg = nullptr;
                }
                io_uring_prep_sendmsg(sqe, roundRobinIndex, sqeMsgHdr, 0);
                // so we can free up the memory later
                io_uring_sqe_set_data(sqe, sqeUserData);
                // index to previously registered fds, not fds themselves
                io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
                // submit for processing
                io_uring_submit(&seg.rings[roundRobinIndex]);
            }
            else
#endif
            {
                // just regular sendmsg
                seg.sendStats.msgCnt++;
                err = (int) sendmsg(sendSocket, &sendhdr, flags);
                // free the header here for this situation
                free(hdr);
                free(iov);
                if (err == -1)
                {
                    seg.sendStats.errCnt++;
                    seg.sendStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
                // this is only set if smoothing is on. only works at low rates with reasonable MTUs
                if (interFrameSleepUsec > 0)
                    busyWaitUsecs(nowTF, interFrameSleepUsec);
            } 
        }
#ifdef SENDMMSG_AVAILABLE
        if (Optimizations::isSelected(Optimizations::Code::sendmmsg))
        {
            // send using vector of msg_hdrs via sendmmsg
            seg.sendStats.msgCnt += numBuffers;
            // this is a blocking version so send everything or error out
            err = (int) sendmmsg(sendSocket, mmsgvec, numBuffers, 0);
            // free up mmsgvec and included headers and iovecs
            for(size_t i = 0; i < numBuffers; i++)
            {
                free(mmsgvec[i].msg_hdr.msg_iov[0].iov_base);
                free(mmsgvec[i].msg_hdr.msg_iov);
            }
            free(mmsgvec);
            // sendmmsg returns the number of updated mmsgvec[i].msg_len entries
            if (err != (int)numBuffers)
            {
                seg.sendStats.errCnt += numBuffers - err;
                // don't override with ESUCCESS
                if (errno != 0)
                    seg.sendStats.lastErrno = errno;
                return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
            }
        }
        else
#endif
#ifdef LIBURING_AVAILABLE
        if (Optimizations::isSelected(Optimizations::Code::liburing_send))
        {
            seg.outstandingSends += numBuffers;
        }
#endif
        // update the event send stats
        seg.eventsInCurrentSync++;

        // keeps compiler quiet about unused variables in default optimizations
        return numBuffers * 0;
    }

    // in Linux use an ioctl to read socket send buffer state
    // otherwise just close
    result<int> Segmenter::SendThreadState::_waitAndCloseFd(int fd)
    {
        int outq = 0;
        bool stop{false};
        // busy wait while the socket has outstanding data
        while(!stop)
        {
            if (ioctl(fd, TIOCOUTQ, &outq) == 0)
                stop = (outq == 0);
            else
                stop = true;
        }
        close(fd);
        return 0;
    }

    result<int> Segmenter::SendThreadState::_close()
    {
        if (useV6)
            for (auto f: socketFd6)
                auto res = _waitAndCloseFd(f.get<0>());
        else
            for(auto f: socketFd4)
                auto res = _waitAndCloseFd(f.get<0>());
        return 0;
    }

    // Blocking call specifying event number.
    result<int> Segmenter::sendEvent(u_int8_t *event, size_t bytes, 
        EventNum_t _eventNum, u_int16_t _dataId, u_int16_t entropy) noexcept
    {
        // reset local event number to override
        if (_eventNum != 0)
            userEventNum.exchange(_eventNum);

        roundRobinIndex = (roundRobinIndex + 1) % numSendSockets;

        // use specified event number and dataId
        return sendThreadState._send(event, bytes, 
            // continue incrementing
            userEventNum++, 
            (_dataId  == 0 ? dataId : _dataId), 
            entropy, roundRobinIndex
        );
    }

    // Non-blocking call specifying explicit event number.
    result<int> Segmenter::addToSendQueue(u_int8_t *event, size_t bytes, 
        EventNum_t _eventNum, u_int16_t _dataId, u_int16_t entropy,
        void (*callback)(boost::any), 
        boost::any cbArg) noexcept
    {
        // reset local event number to override
        if (_eventNum != 0)
            userEventNum.exchange(_eventNum);

        // have to zero out or else destructor is called in the assignment of cbArg below
        EventQueueItem *item = reinterpret_cast<EventQueueItem*>(calloc(1, sizeof(EventQueueItem)));
        item->bytes = bytes;
        item->event = event;
        item->entropy = entropy;
        item->callback = callback;
        item->cbArg = std::move(cbArg);
        // continue incrementing 
        item->eventNum = userEventNum++;
        item->dataId = (_dataId  == 0 ? dataId : _dataId);
        auto res = eventQueue.push(item);
        // wake up send thread (no need to hold the lock as queue is lock_free)
        //sendThreadCond.notify_one();
        if (res)
            return 0;
        else
            return E2SARErrorInfo{E2SARErrorc::MemoryError, "Send queue is temporarily full, try again later"};
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
        sFlags.warmUpMs = paramTree.get<bool>("control-plane.warmUpMS", sFlags.warmUpMs);
        sFlags.syncPeriods = paramTree.get<u_int16_t>("control-plane.syncPeriods", 
            sFlags.syncPeriods);
        sFlags.syncPeriodMs = paramTree.get<u_int16_t>("control-plane.syncPeriodMS", 
            sFlags.syncPeriodMs);

        // data plane
        sFlags.dpV6 = paramTree.get<bool>("data-plane.dpV6", sFlags.dpV6);
        sFlags.connectedSocket = paramTree.get<bool>("data-plane.connectedSocket", 
            sFlags.connectedSocket);
        sFlags.mtu = paramTree.get<u_int16_t>("data-plane.mtu", sFlags.mtu);
        sFlags.numSendSockets = paramTree.get<size_t>("data-plane.numSendSockets", 
            sFlags.numSendSockets);
        sFlags.sndSocketBufSize = paramTree.get<int>("data-plane.sndSocketBufSize", 
            sFlags.sndSocketBufSize);
        sFlags.rateGbps = paramTree.get<float>("data-plane.rateGbps",
            sFlags.rateGbps);
        sFlags.smooth = paramTree.get<bool>("data-plane.smooth",
            sFlags.smooth);
        sFlags.multiPort = paramTree.get<bool>("data-plane.multiPort",
            sFlags.multiPort);

        return sFlags;
    }
}
