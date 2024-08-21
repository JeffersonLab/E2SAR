#include <boost/thread.hpp>
#include <boost/chrono.hpp>

#include "portable_endian.h"

#include "e2sarDPReassembler.hpp"


namespace e2sar 
{

    /** Function implementing a PID computation. 
     * Returns a pair of <PID, Current Error>
    */
    template<class X>
    boost::tuple<X, X, X> pid(        // Proportional, Integrative, Derivative Controller
            const X& setPoint,  // Desired Operational Set Point
            const X& prcsVal,   // Measure Process Value
            const X& delta_t,   // Time delta (seconds) between determination of last control value
            const X& Kp,        // Konstant for Proprtional Control
            const X& Ki,        // Konstant for Integrative Control
            const X& Kd,        // Konstant for Derivative Control
            const X& prev_err,   // previous error
            const X& prev_integral // previous value of integrative component
    )
    {
        X error = setPoint - prcsVal;
        X integral = error * delta_t + prev_integral;
        X derivative = (error - prev_err) / delta_t;
        return boost::make_tuple<X, X, X>(Kp * error + Ki * integral + Kd * derivative, 
            error, integral);  // control output
    }

    Reassembler::Reassembler(const EjfatURI &uri, std::vector<int> cpuCoreList,
        const ReassemblerFlags &rflags):
        dpuri(uri),
        lbman(dpuri, rflags.validateCert),
        dpV6{rflags.dpV6},
        epochMs{rflags.epoch_ms}, setPoint{rflags.setPoint}, 
        Kp{rflags.Kp}, Ki{rflags.Ki}, Kd{rflags.Kd},
        pidSampleBuffer(rflags.epoch_ms/rflags.period_ms), // ring buffer size (usually 10 = 1sec/100ms)
        cpuCoreList{cpuCoreList}, 
        dataIP{ip::make_address(rflags.dataIPString)},
        dataPort{rflags.dataPort},
        portRange{rflags.portRange != -1 ? rflags.portRange : get_PortRange(cpuCoreList.size())}, 
        numRecvThreads{cpuCoreList.size()}, // as many as there are cores
        numRecvPorts{static_cast<size_t>(portRange > 0 ? 2 << (portRange - 1): 1)},
        portsToThreads(numRecvThreads),
        withLBHeader{rflags.withLBHeader},
        eventTimeout_ms{rflags.eventTimeout_ms},
        condLock{recvThreadMtx},
        sendStateThreadState(*this, rflags.cpV6, rflags.period_ms),
        useCP{rflags.useCP}
    {
        // note if the user chooses to override portRange in rflags, 
        // we can end up in a silly situation where the number of receive ports is smaller
        // than the number of receive threads, but we handle it
        // Need to break up M ports into at most N bins.
        assignPortsToThreads();
    }

    Reassembler::Reassembler(const EjfatURI &uri, size_t numRecvThreads,
        const ReassemblerFlags &rflags):
        dpuri(uri),
        lbman(dpuri, rflags.validateCert),
        dpV6{rflags.dpV6},
        epochMs{rflags.epoch_ms}, setPoint{rflags.setPoint}, 
        Kp{rflags.Kp}, Ki{rflags.Ki}, Kd{rflags.Kd},
        pidSampleBuffer(rflags.epoch_ms/rflags.period_ms), // ring buffer size (usually 10 = 1sec/100ms)
        cpuCoreList{std::vector<int>()}, // no core list given
        dataIP{ip::make_address(rflags.dataIPString)},
        dataPort{rflags.dataPort},
        portRange{rflags.portRange != -1 ? rflags.portRange : get_PortRange(numRecvThreads)}, 
        numRecvThreads{numRecvThreads},
        numRecvPorts{static_cast<size_t>(portRange > 0 ? 2 << (portRange - 1): 1)},
        portsToThreads(numRecvThreads),
        withLBHeader{rflags.withLBHeader},
        eventTimeout_ms{rflags.eventTimeout_ms},
        condLock{recvThreadMtx},
        sendStateThreadState(*this, rflags.cpV6, rflags.period_ms),
        useCP{rflags.useCP}
    {
        // note if the user chooses to override portRange in rflags, 
        // we can end up in a silly situation where the number of receive ports is smaller
        // than the number of receive threads, but we handle it
        // Need to break up M ports into at most N bins.
        assignPortsToThreads();
    }

    result<int> Reassembler::openAndStart() noexcept
    {
        for(int i=0; i<numRecvThreads; i++)
        {
            std::vector<int> portVec = std::vector<int>(portsToThreads[i].begin(), 
                portsToThreads[i].end());

            // this constructor uses move semantics for port vector (not for cpu list tho)
            auto it = recvThreadState.emplace(recvThreadState.end(), *this, dpV6,
                std::move(portVec), cpuCoreList);

            // open the sockets for all ports for this thread
            auto open_stat = it->_open();
            if (open_stat.has_error())
            {
               return E2SARErrorInfo{E2SARErrorc::SocketError, 
                    "Unable to open receive sockets: " + open_stat.error().message()};
            }

            // start receive thread
            boost::thread syncT(&Reassembler::RecvThreadState::_threadBody, &(*it));
            it->threadObj = std::move(syncT);
        }

        // open is not needed for this thread because gRPC
        if (useCP)
        {
            // start sendstate thread
            boost::thread sendstateT(&Reassembler::SendStateThreadState::_threadBody, 
                &sendStateThreadState);
            sendStateThreadState.threadObj = std::move(sendstateT);
        }
        return 0;
    }

    void Reassembler::RecvThreadState::_threadBody()
    {

        std::set<EventNum_t> activeEventNumbers{};

        while(!reas.threadsStop)
        {
            fd_set curSet{fdSet};

            // do select/wait on open sockets
            auto select_retval = select(maxFdPlusOne, &curSet, NULL, NULL, &sleep_tv);

            if (select_retval == -1)
            {
                reas.recvStats.dataErrCnt++;
                reas.recvStats.lastErrno = errno;
                continue;
            }

            // receive a event fragment on one or more of them
            for(auto fd: sockets) 
            {
                if (!FD_ISSET(fd, &curSet))
                    continue;

                // allocate receive buffer from the pool
                bool freeRecvBuffer = true;
                auto recvBuffer = static_cast<u_int8_t*>(recvBufferPool.malloc());

                struct sockaddr_in client_addr{};
                socklen_t client_addr_len = sizeof(client_addr);

                ssize_t nbytes = recvfrom(fd, recvBuffer, RECV_BUFFER_SIZE, 0,
                    (struct sockaddr*)&client_addr, &client_addr_len);

                if (nbytes == -1) {
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    continue;
                }
                // start a new event if offset 0 (check for event number collisions)
                // or attach to existing event
                REHdr *rehdr{nullptr};
                // for testing we may leave LB header attached, so it needs to be
                // subtracted
                if (reas.withLBHeader)
                {
                    rehdr = reinterpret_cast<REHdr*>(recvBuffer + sizeof(LBHdr));
                    nbytes -= sizeof(LBHdr) + sizeof(REHdr);
                }
                else
                {
                    rehdr = reinterpret_cast<REHdr*>(recvBuffer);
                    nbytes -= sizeof(REHdr);
                }

                EventQueueItem *item{nullptr};
                if (rehdr->get_bufferOffset() == 0)
                {
                    // new event - start a new event item and new event buffer 
                    // since this is done by many threads, can't use object_pool easily
                    item = new EventQueueItem(rehdr);
                    // add to in progress map
                    eventsInProgress[rehdr->get_eventNum()] = item;
                } else 
                {
                    // try to locate in the in progress map
                    auto it = eventsInProgress.find(rehdr->get_eventNum());
                    if (it != eventsInProgress.end()) 
                        item = eventsInProgress[rehdr->get_eventNum()];
                    else
                    {
                        // out of order delivery and we haven't seen this event
                        // start a new event item and new event buffer and put segment
                        // on out-of-order queue
                        item = new EventQueueItem(rehdr);
                        // add to in progress map
                        eventsInProgress[rehdr->get_eventNum()] = item;
                        // add segment into event out-of-order queue
                        item->oodQueue.push(Segment(recvBuffer, nbytes));
                        // done for now
                        continue;
                    }
                }

                // copy it into event buffer 
                // if it is in sequence OR attach to out of order priority queue.
                // note that with or without LB header, our REhdr should be set now
                if (item->curEnd == rehdr->get_bufferOffset())
                {
                    memcpy(item->event + item->curEnd, 
                        reinterpret_cast<u_int8_t*>(rehdr) + sizeof(REHdr), nbytes);
                    // nbytes by now is less REHdr and LBHdr (as needed)
                    item->curEnd += nbytes;
                } else
                {
                    // add to out-of-order queue
                    item->oodQueue.push(Segment(recvBuffer, nbytes));
                    freeRecvBuffer = false;
                }

                // free the recv buffer if possible (not attached into priority queue)
                if (freeRecvBuffer)
                    recvBufferPool.free(recvBuffer);

                // check the priority queue if it can be emptied by copying segments into
                // event buffer and freeing them
                while(!item->oodQueue.empty())
                {
                    auto e = item->oodQueue.top();
                    if (reas.withLBHeader)
                        rehdr = reinterpret_cast<REHdr*>(e.segment + sizeof(LBHdr));
                    else
                        rehdr = reinterpret_cast<REHdr*>(e.segment);
                    if (item->curEnd == rehdr->get_bufferOffset())
                    {
                        memcpy(item->event + item->curEnd,
                            reinterpret_cast<u_int8_t*>(rehdr) + sizeof(REHdr), nbytes);
                        item->curEnd += nbytes;
                        recvBufferPool.free(e.segment);
                        // pop the item off and try to continue
                        item->oodQueue.pop();
                    } else 
                    {
                        // there are still missing segments, so stop
                        break;
                    }
                }

                // check if this event is completed, if so put on queue
                // make sure oodQueue of the event is empty
                if (item->curEnd == item->bytes)
                {
                    // check ood queue is empty
                    if (!item->oodQueue.empty())
                    {
                        reas.recvStats.lastE2SARError = E2SARErrorc::MemoryError;
                        // clean up ood queue
                        item->cleanup(recvBufferPool);
                    }

                    // remove this item from in progress map
                    eventsInProgress.erase(item->eventNum);

                    // queue it up for the user to receive
                    reas.enqueue(item);

                    // update statistics
                    reas.recvStats.eventSuccess++;
                }

                // sweep the map of in progress events and see if any should be given up on
                // because we waited too long
                auto nowT = boost::chrono::steady_clock::now();
                for (auto it = eventsInProgress.begin(); it != eventsInProgress.end(); ) {
                    auto inWaiting = nowT - it->second->firstSegment;
                    auto inWaiting_ms = boost::chrono::duration_cast<boost::chrono::milliseconds>(inWaiting);
                    if (inWaiting_ms > boost::chrono::milliseconds(reas.eventTimeout_ms)) {
                        // deallocate event (ood queue and event buffer)
                        it->second->cleanup(recvBufferPool);
                        delete it->second->event;
                        // deallocate queue item
                        delete it->second;
                        it = eventsInProgress.erase(it);  // erase returns the next element (or end())
                        reas.recvStats.enqueueLoss++;
                    } else {
                        ++it;  // Just advance the iterator if no deletion
                    }
                }
            }
        }

        // close on exit
        auto res = _close();
    }

    result<int> Reassembler::RecvThreadState::_open()
    {
        // get appropriate IP address to talk to
        auto dataAddr = (reas.dpV6 ? reas.dpuri.get_dataAddrv6() : reas.dpuri.get_dataAddrv4());
            
        if (dataAddr.has_error())
            return dataAddr.error();

        FD_ZERO(&sockets);
        maxFdPlusOne = 0;
        // open socket on each of the ports
        for(auto port: udpPorts)
        {
            int socketFd;
            // Socket for receiving data messages from LB, one for each port
            if (dataAddr.value().first.is_v6()) {
                if ((socketFd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }

                sockaddr_in6 dataAddrStruct6{};
                dataAddrStruct6.sin6_family = AF_INET6;
                dataAddrStruct6.sin6_port = htobe16(port);
                inet_pton(AF_INET6, dataAddr.value().first.to_string().c_str(), &dataAddrStruct6.sin6_addr);

                int err = bind(socketFd, (const sockaddr *) &dataAddrStruct6, sizeof(struct sockaddr_in6));
                if (err < 0) {
                    close(socketFd);
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
            }
            else {
                if ((socketFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }

                sockaddr_in dataAddrStruct4{};
                dataAddrStruct4.sin_family = AF_INET;
                dataAddrStruct4.sin_port = htobe16(port);
                inet_pton(AF_INET, dataAddr.value().first.to_string().c_str(), &dataAddrStruct4.sin_addr);

                int err = bind(socketFd, (const sockaddr *) &dataAddrStruct4, sizeof(struct sockaddr_in));
                if (err < 0) {
                    close(socketFd);
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }
            }
            sockets.push_back(socketFd);
            FD_SET(socketFd, &fdSet);
            maxFdPlusOne = (maxFdPlusOne > socketFd ? maxFdPlusOne : socketFd);
        }

        // make it plus one
        maxFdPlusOne++;
        return 0;
    }

    result<int> Reassembler::RecvThreadState::_close()
    {
        for(auto fd: sockets)
            close(fd);
        return 0;
    }

    void Reassembler::SendStateThreadState::_threadBody()
    {
        while(!reas.threadsStop)
        {
            // periodically send state to control plane sampling the queue state

            // Get the current time point
            auto nowT = boost::chrono::high_resolution_clock::now();

            // principle of operation:
            // CP requires PID signal and queue fill state in order to come up
            // with a schedule  for a new epoch. The CP and receiver nodes running
            // Reassembler are not synchronized so the start of the epoch is not known
            // to the worker node. For this reason the Segmenter
            // or rather this thread running in Segmenter context on a worker node
            // samples the queue and computes the PID at 10Hz/100ms 
            // over a sliding 1 sec window sending the update to CP each time.
            //
            // So this thread maintains 10 samples and computes the PID based on oldest
            // and current sample, and then the oldest sample is removed. In
            // this case deltaT is always ~1sec (modulo the accuracy of the sleeps).
            // The depth of circular buffer is always set to epoch_length/period_of_this_thread.
            //
            // fillPercent is always reported as sampled in the current moment

            // if there are no samples in the buffer just skip
            if (reas.pidSampleBuffer.end() != reas.pidSampleBuffer.begin())
            {
                auto nowUsec = boost::chrono::duration_cast<boost::chrono::microseconds>(nowT.time_since_epoch()).count();
                UnixTimeMicro_t currentTimeMicros = static_cast<UnixTimeMicro_t>(nowUsec);

                // at 100msec period and depth of 10 this should normally be about 1 sec
                auto deltaTfloat = static_cast<float>(currentTimeMicros - 
                    reas.pidSampleBuffer.begin()->sampleTime)/1000000.;

                // sample queue state
                auto fillPercent = static_cast<float>(static_cast<float>(reas.eventQueueDepth)/static_cast<float>(reas.QSIZE));
                // get PID terms (PID value, error, integral accumulator)
                auto PIDTuple = pid<float>(reas.setPoint, fillPercent,  
                    deltaTfloat, reas.Kp, reas.Ki, reas.Kd, 
                    reas.pidSampleBuffer.begin()->error,
                    reas.pidSampleBuffer.end()->integral);

                // create new PID sample using last error and integral accumulated value
                PIDSample newSample{currentTimeMicros, PIDTuple.get<1>(), PIDTuple.get<2>()};
                // push a new entry onto the circular buffer ejecting the oldest
                reas.pidSampleBuffer.push_back(newSample);

                // send update to CP
                auto res = reas.lbman.sendState(fillPercent, PIDTuple.get<0>(), true);
                if (res.has_error())
                {
                    // update error counts
                    reas.recvStats.grpcErrCnt++;
                    reas.recvStats.lastE2SARError = res.error().code();
                }
            }

            // sleep approximately so we wake up every ~100ms
            auto until = nowT + boost::chrono::milliseconds(period_ms);
            boost::this_thread::sleep_until(until);
        }
    }

    result<int> Reassembler::registerWorker(const std::string &node_name, float weight, 
        float min_factor, float max_factor) noexcept 
    {
        if (useCP)
        {
            registeredWorker = true;
            return lbman.registerWorker(node_name, std::make_pair(dataIP, dataPort), weight, numRecvPorts, 
                min_factor, max_factor);
        }
        return 0;
    }

    result<int> Reassembler::deregisterWorker() noexcept
    {
        if (registeredWorker)
        {
            registeredWorker = false;
            return lbman.deregisterWorker();
        } else
            if (useCP)
                return E2SARErrorInfo{E2SARErrorc::LogicError, "Attempting to unregister a worker when it hasn't been registered."};
        return 0;
    }

    result<int> Reassembler::getEvent(uint8_t **event, size_t *bytes, uint64_t* eventNum, uint16_t *dataId) noexcept
    {
        auto eventItem = dequeue();

        if (eventItem == nullptr)
            return -1;

        *event = eventItem->event;
        *bytes = eventItem->bytes;
        *eventNum = eventItem->eventNum;
        *dataId = eventItem->dataId;

        // just rely on its locking
        delete eventItem;

        return 0;
    }

    result<int> Reassembler::recvEvent(uint8_t **event, size_t *bytes, EventNum_t* eventNum, uint16_t *dataId) noexcept
    {
        recvThreadCond.wait(condLock);

        auto eventItem = dequeue();

        if (eventItem == nullptr)
            return -1;

        *event = eventItem->event;
        *bytes = eventItem->bytes;
        *eventNum = eventItem->eventNum;
        *dataId = eventItem->dataId;

        // just rely on its locking
        delete eventItem;

        return 0;
    }
}
