#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/detail/file_parser_error.hpp>
#include <iostream>

#include "portable_endian.h"

#include "e2sarAffinity.hpp"
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

    Reassembler::Reassembler(const EjfatURI &uri, ip::address data_ip, u_int16_t starting_port,
        std::vector<int> cpuCoreList,
        const ReassemblerFlags &rflags):
        dpuri(uri),
        lbman(dpuri, rflags.validateCert, rflags.useHostAddress),
        epochMs{rflags.epoch_ms}, setPoint{rflags.setPoint}, 
        Kp{rflags.Kp}, Ki{rflags.Ki}, Kd{rflags.Kd},
        weight{rflags.weight}, min_factor{rflags.min_factor}, max_factor{rflags.max_factor},
        pidSampleBuffer(rflags.epoch_ms/rflags.period_ms), // ring buffer size (usually 10 = 1sec/100ms)
        gcThreadState(*this),
        cpuCoreList{cpuCoreList}, 
        dataIP{data_ip},
        dataPort{starting_port},
        portRange{rflags.portRange != -1 ? rflags.portRange : get_PortRange(cpuCoreList.size())}, 
        numRecvThreads{cpuCoreList.size()}, // as many as there are cores
        numRecvPorts{static_cast<size_t>(portRange > 0 ? 2 << (portRange - 1): 1)},
        threadsToPorts(numRecvThreads),
        withLBHeader{rflags.withLBHeader},
        eventTimeout_ms{rflags.eventTimeout_ms},
        rcvSocketBufSize{rflags.rcvSocketBufSize},
        sendStateThreadState(*this, rflags.period_ms),
        useCP{rflags.useCP},
        reportStats{rflags.reportStats}
    {
        sanityChecks();
        auto afres = Affinity::setProcess(cpuCoreList);
        if (afres.has_error())
            throw E2SARException(afres.error().message());
        // note if the user chooses to override portRange in rflags, 
        // we can end up in a silly situation where the number of receive ports is smaller
        // than the number of receive threads, but we handle it
        // Need to break up M ports into at most N bins.
        assignPortsToThreads();
    }

    Reassembler::Reassembler(const EjfatURI &uri,  ip::address data_ip, u_int16_t starting_port,
        size_t numRecvThreads, const ReassemblerFlags &rflags):
        dpuri(uri),
        lbman(dpuri, rflags.validateCert, rflags.useHostAddress),
        epochMs{rflags.epoch_ms}, setPoint{rflags.setPoint}, 
        Kp{rflags.Kp}, Ki{rflags.Ki}, Kd{rflags.Kd},
        weight{rflags.weight}, min_factor{rflags.min_factor}, max_factor{rflags.max_factor},
        pidSampleBuffer(rflags.epoch_ms/rflags.period_ms), // ring buffer size (usually 10 = 1sec/100ms)
        gcThreadState(*this),
        cpuCoreList{std::vector<int>()}, // no core list given
        dataIP{data_ip},
        dataPort{starting_port},
        portRange{rflags.portRange != -1 ? rflags.portRange : get_PortRange(numRecvThreads)}, 
        numRecvThreads{numRecvThreads},
        numRecvPorts{static_cast<size_t>(portRange > 0 ? 2 << (portRange - 1): 1)},
        threadsToPorts(numRecvThreads),
        withLBHeader{rflags.withLBHeader},
        eventTimeout_ms{rflags.eventTimeout_ms},
        rcvSocketBufSize{rflags.rcvSocketBufSize},
        sendStateThreadState(*this, rflags.period_ms),
        useCP{rflags.useCP},
        reportStats{rflags.reportStats}
    {
        sanityChecks();
        // note if the user chooses to override portRange in rflags, 
        // we can end up in a silly situation where the number of receive ports is smaller
        // than the number of receive threads, but we handle it
        // Need to break up M ports into at most N bins.
        assignPortsToThreads();
    }

    Reassembler::Reassembler(const EjfatURI &uri,  u_int16_t starting_port,
        std::vector<int> cpuCoreList,
        const ReassemblerFlags &rflags, bool v6):
        dpuri(uri),
        lbman(dpuri, rflags.validateCert, rflags.useHostAddress),
        epochMs{rflags.epoch_ms}, setPoint{rflags.setPoint}, 
        Kp{rflags.Kp}, Ki{rflags.Ki}, Kd{rflags.Kd},
        weight{rflags.weight}, min_factor{rflags.min_factor}, max_factor{rflags.max_factor},
        pidSampleBuffer(rflags.epoch_ms/rflags.period_ms), // ring buffer size (usually 10 = 1sec/100ms)
        gcThreadState(*this),
        cpuCoreList{cpuCoreList}, 
        dataPort{starting_port},
        portRange{rflags.portRange != -1 ? rflags.portRange : get_PortRange(cpuCoreList.size())}, 
        numRecvThreads{cpuCoreList.size()}, // as many as there are cores
        numRecvPorts{static_cast<size_t>(portRange > 0 ? 2 << (portRange - 1): 1)},
        threadsToPorts(numRecvThreads),
        withLBHeader{rflags.withLBHeader},
        eventTimeout_ms{rflags.eventTimeout_ms},
        rcvSocketBufSize{rflags.rcvSocketBufSize},
        sendStateThreadState(*this, rflags.period_ms),
        useCP{rflags.useCP},
        reportStats{rflags.reportStats}
    {
        auto dpRes = dpuri.getDataplaneLocalAddresses(v6);
        if (dpRes.has_error())
            throw E2SARException(dpRes.error().message());

        if (dpRes.value().size() == 0)
            throw E2SARException("Unable to determine outgoing dataplane address");

        dataIP = dpRes.value()[0];
        sanityChecks();
        auto afres = Affinity::setProcess(cpuCoreList);
        if (afres.has_error())
            throw E2SARException(afres.error().message());
        // note if the user chooses to override portRange in rflags, 
        // we can end up in a silly situation where the number of receive ports is smaller
        // than the number of receive threads, but we handle it
        // Need to break up M ports into at most N bins.
        assignPortsToThreads();
    }

    Reassembler::Reassembler(const EjfatURI &uri,  u_int16_t starting_port,
        size_t numRecvThreads, const ReassemblerFlags &rflags, bool v6):
        dpuri(uri),
        lbman(dpuri, rflags.validateCert, rflags.useHostAddress),
        epochMs{rflags.epoch_ms}, setPoint{rflags.setPoint}, 
        Kp{rflags.Kp}, Ki{rflags.Ki}, Kd{rflags.Kd},
        weight{rflags.weight}, min_factor{rflags.min_factor}, max_factor{rflags.max_factor},
        pidSampleBuffer(rflags.epoch_ms/rflags.period_ms), // ring buffer size (usually 10 = 1sec/100ms)
        gcThreadState(*this),
        cpuCoreList{std::vector<int>()}, // no core list given
        dataPort{starting_port},
        portRange{rflags.portRange != -1 ? rflags.portRange : get_PortRange(numRecvThreads)}, 
        numRecvThreads{numRecvThreads},
        numRecvPorts{static_cast<size_t>(portRange > 0 ? 2 << (portRange - 1): 1)},
        threadsToPorts(numRecvThreads),
        withLBHeader{rflags.withLBHeader},
        eventTimeout_ms{rflags.eventTimeout_ms},
        rcvSocketBufSize{rflags.rcvSocketBufSize},
        sendStateThreadState(*this, rflags.period_ms),
        useCP{rflags.useCP},
        reportStats{rflags.reportStats}
    {
        auto dpRes = dpuri.getDataplaneLocalAddresses(v6);
        if (dpRes.has_error())
            throw E2SARException(dpRes.error().message());

        if (dpRes.value().size() == 0)
            throw E2SARException("Unable to determine outgoing dataplane address");
            
        dataIP = dpRes.value()[0];
        sanityChecks();
        // note if the user chooses to override portRange in rflags, 
        // we can end up in a silly situation where the number of receive ports is smaller
        // than the number of receive threads, but we handle it
        // Need to break up M ports into at most N bins.
        assignPortsToThreads();
    }


    result<int> Reassembler::openAndStart() noexcept
    {
        int maxFDPlusOne{0};
        // open all file descriptors in all threads
        for(size_t i=0; i<numRecvThreads; i++)
        {
            std::vector<int> portVec = std::vector<int>(threadsToPorts[i].begin(), 
                threadsToPorts[i].end());

            // this constructor uses move semantics for port vector (not for cpu list tho)
            auto it = recvThreadState.emplace(recvThreadState.end(), *this, 
                std::move(portVec), cpuCoreList);

            // open the sockets for all ports for this thread
            auto open_stat = it->_open();
            if (open_stat.has_error())
            {
               return E2SARErrorInfo{E2SARErrorc::SocketError, 
                    "Unable to open receive sockets: " + open_stat.error().message()};
            }
            // figure out the largest fd + 1 across all threads
            maxFDPlusOne = (open_stat.value() > maxFDPlusOne ? open_stat.value() : maxFDPlusOne);
        }

        // create a place for per-fd stats - avoid reallocation
        recvStats.fragmentsPerFd.resize(maxFDPlusOne);
        recvStats.fragmentsPerFd.reserve(maxFDPlusOne); 

        // now ready to start threads
        for(auto it = recvThreadState.begin(); it != recvThreadState.end(); ++it)
        {
            // start receive thread
            // it is an iterator and we need a pointer, hence &(*it)
            boost::thread recvT(&Reassembler::RecvThreadState::_threadBody, &(*it));
            it->threadObj = std::move(recvT);
        }

        // start the GC thread
        boost::thread gcT(&Reassembler::GCThreadState::_threadBody, &gcThreadState);
        gcThreadState.threadObj = std::move(gcT);

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

    void Reassembler::GCThreadState::_threadBody()
    {
        auto eventTimeout_ms = boost::chrono::milliseconds(reas.eventTimeout_ms);

        // TODO: move to a more granular affinity setting for main
        // threads and then set this thread to anything other than
        // the cores specified by user
        // set affinity to anything other than cores we are using
        // if (reas.cpuCoreList.size() > 0)
        // {
        //     auto res = Affinity::setThreadXOR(reas.cpuCoreList);
        //     if (res.has_error())
        //     reas.recvStats.lastE2SARError = res.error().code();
        // }

        while (!reas.threadsStop)
        {
            auto nowT = boost::chrono::steady_clock::now();

            // iterate over threads
            for(auto i = reas.recvThreadState.begin(); i != reas.recvThreadState.end(); ++i)
            {
                i->evtsInProgressMutex.lock();
                for (auto it = i->eventsInProgress.begin(); it != i->eventsInProgress.end(); ) {
                    // we save time by looking at the first segment time of arrival
                    // this way we avoid querying time on every segment arrival
                    auto inWaiting = nowT - it->second->firstSegment;
                    auto inWaiting_ms = boost::chrono::duration_cast<boost::chrono::milliseconds>(inWaiting);
                    if (inWaiting_ms > eventTimeout_ms) {
                        i->logLostEvent(it->second, false);
                        delete[] it->second->event;
                        // deallocate queue item
                        it->second.reset();
                        it = i->eventsInProgress.erase(it);  // erase returns the next element (or end())
                    } else {
                        ++it;  // Just advance the iterator if no deletion
                    }
                }
                i->evtsInProgressMutex.unlock();
            }
            // sleep approximately for eventTimeout msecs
            auto until = nowT + boost::chrono::milliseconds(eventTimeout_ms);
            boost::this_thread::sleep_until(until);
        }
    }

    void Reassembler::RecvThreadState::_threadBody()
    {
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

                // allocate receive buffer 
                auto recvBuffer = static_cast<u_int8_t*>(malloc(RECV_BUFFER_SIZE));

                struct sockaddr_in client_addr{};
                socklen_t client_addr_len = sizeof(client_addr);

                ssize_t nbytes = recvfrom(fd, recvBuffer, RECV_BUFFER_SIZE, 0,
                    (struct sockaddr*)&client_addr, &client_addr_len);

                if (nbytes == -1) {
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    free(recvBuffer);
                    continue;
                }
                // count fragment received by socket
                reas.recvStats.fragmentsPerFd[fd]++;
                reas.recvStats.totalPacketsReceived++;
                reas.recvStats.totalBytesReceived += nbytes;

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

                std::shared_ptr<EventQueueItem> item;

                if (rehdr->get_bufferOffset() == 0)
                {
                    // new event - start a new event item and new event buffer 
                    // since this is done by many threads, can't use object_pool easily
                    item = std::make_shared<EventQueueItem>(rehdr);
                    // add to in progress map based on <event number, data id> tuple
                    evtsInProgressMutex.lock();
                    eventsInProgress[std::make_pair(rehdr->get_eventNum(), rehdr->get_dataId())] = item;
                    evtsInProgressMutex.unlock();
                } else 
                {
                    // try to locate the event in the in progress map
                    evtsInProgressMutex.lock();
                    auto it = eventsInProgress.find(std::make_pair(rehdr->get_eventNum(), rehdr->get_dataId()));
                    if (it != eventsInProgress.end()) 
                        item = it->second;
                    else
                    {
                        // out of order delivery and we haven't seen this event
                        // start a new event item and new event buffer 
                        item = std::make_shared<EventQueueItem>(rehdr);
                        // add to in progress map
                        eventsInProgress[std::make_pair(rehdr->get_eventNum(), rehdr->get_dataId())] = item;
                    }
                    evtsInProgressMutex.unlock();
                }


                // copy segment into event buffer into its proper place 
                // note that with or without LB header, our REhdr should be set now
                memcpy(item->event + rehdr->get_bufferOffset(), 
                    reinterpret_cast<u_int8_t*>(rehdr) + sizeof(REHdr), nbytes);

                // free the recv buffer
                free(recvBuffer);

                // count this fragment received (it could be anywhere in the event)
                item->numFragments++;

                item->curBytes += nbytes;

                // check if this event is completed, if so put on queue
                if (item->curBytes == item->bytes )
                {
                    // remove this item from in progress map
                    evtsInProgressMutex.lock();
                    // TODO: a bit inefficient as this searches for all keys equal to this.
                    // If we can get ahold of an iterator in advance we can precisely erase the element
                    eventsInProgress.erase(std::make_pair(item->eventNum, item->dataId));
                    evtsInProgressMutex.unlock();

                    // queue it up for the user to receive
                    auto ret = reas.enqueue(item);
                    // event lost on enqueuing
                    if (ret == 1) 
                    {
                        // log this lost event
                        logLostEvent(item, true);
                        // delete event buffer
                        delete[] item->event;
                    }
                    // deallocate queue item - either we put a copy of the item
                    // on the queue or we couldn't, either way we release
                    item.reset();
                    // update statistics
                    reas.recvStats.eventSuccess++;
                }
            }
        }

        // close on exit
        auto res = _close();
    }

    result<int> Reassembler::RecvThreadState::_open()
    {
        sockets.clear();
        FD_ZERO(&fdSet);
        maxFdPlusOne = 0;
        // open socket on each of the ports
        for(auto port: udpPorts)
        {
            int socketFd;
            // Socket for receiving data messages from LB, one for each port
            if (reas.dataIP.is_v6()) {
                if ((socketFd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }

                // set recv socket buffer size
                if (setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, &reas.rcvSocketBufSize, sizeof(reas.rcvSocketBufSize)) < 0) {
                    close(socketFd);
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }

                sockaddr_in6 dataAddrStruct6{};
                dataAddrStruct6.sin6_family = AF_INET6;
                dataAddrStruct6.sin6_port = htobe16(port);
                inet_pton(AF_INET6, reas.dataIP.to_string().c_str(), &dataAddrStruct6.sin6_addr);

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

                // set recv socket buffer size
                if (setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, &reas.rcvSocketBufSize, sizeof(reas.rcvSocketBufSize)) < 0) {
                    close(socketFd);
                    reas.recvStats.dataErrCnt++;
                    reas.recvStats.lastErrno = errno;
                    return E2SARErrorInfo{E2SARErrorc::SocketError, strerror(errno)};
                }

                sockaddr_in dataAddrStruct4{};
                dataAddrStruct4.sin_family = AF_INET;
                dataAddrStruct4.sin_port = htobe16(port);
                inet_pton(AF_INET, reas.dataIP.to_string().c_str(), &dataAddrStruct4.sin_addr);

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
            reas.recvStats.portPerFd.resize(socketFd+1);
            reas.recvStats.portPerFd[socketFd] = port; // which port is for this FD?
        }

        // make it plus one
        return ++maxFdPlusOne;
    }

    result<int> Reassembler::RecvThreadState::_close()
    {
        for(auto fd: sockets)
            close(fd);
        return 0;
    }

    void Reassembler::SendStateThreadState::_threadBody()
    {
        // get the time
        auto nowT = boost::chrono::system_clock::now();
        auto nowUsec = boost::chrono::duration_cast<boost::chrono::microseconds>(nowT.time_since_epoch()).count();
        UnixTimeMicro_t currentTimeMicros = static_cast<UnixTimeMicro_t>(nowUsec);

        // create first PID sample with 0 error and integral values
        PIDSample newSample{currentTimeMicros, 0.0, 0.0};
        // push a new entry onto the circular buffer ejecting the oldest
        reas.pidSampleBuffer.push_back(newSample);

        // wait before entering the loop
        auto until = nowT + boost::chrono::milliseconds(period_ms);
        boost::this_thread::sleep_until(until);

        while(!reas.threadsStop)
        {
            // periodically send state to control plane sampling the queue state

            // principle of operation:
            // CP requires PID signal and queue fill state in order to come up
            // with a schedule  for a new epoch. The CP and receiver nodes running
            // Reassembler are not synchronized so the start of the epoch is not known
            // to the worker node. For this reason the Reassembler
            // or rather this thread running in Reassembler context on a worker node
            // samples the queue and computes the PID at 10Hz/100ms 
            // over a sliding 1 sec window sending the update to CP each time.
            //
            // So this thread maintains 10 samples and computes the PID based on oldest
            // and current sample, and then the oldest sample is removed. In
            // this case deltaT is always ~1sec (modulo the accuracy of the sleeps).
            // The depth of circular buffer is always set to epoch_length/period_of_this_thread.
            //
            // fillPercent is always reported as sampled in the current moment

            // Get the current time point
            auto nowT = boost::chrono::system_clock::now();
            auto nowUsec = boost::chrono::duration_cast<boost::chrono::microseconds>(nowT.time_since_epoch()).count();
            UnixTimeMicro_t currentTimeMicros = static_cast<UnixTimeMicro_t>(nowUsec);

            // at 100msec period and depth of 10 this should normally be about 1 sec
            auto deltaTfloat = static_cast<float>(currentTimeMicros - 
                reas.pidSampleBuffer.front().sampleTime)/1000000.;

            // sample queue state
            auto fillPercent = static_cast<float>(static_cast<float>(reas.eventQueueDepth)/static_cast<float>(reas.QSIZE));
            // get PID terms (PID value, error, integral accumulator)
            auto PIDTuple = pid<float>(reas.setPoint, fillPercent,  
                deltaTfloat, reas.Kp, reas.Ki, reas.Kd, 
                reas.pidSampleBuffer.front().error,
                reas.pidSampleBuffer.back().integral);

            // create new PID sample using last error and integral accumulated value
            PIDSample newSample{currentTimeMicros, PIDTuple.get<1>(), PIDTuple.get<2>()};
            // push a new entry onto the circular buffer ejecting the oldest
            reas.pidSampleBuffer.push_back(newSample);

            // send update to CP
            // gather the stats
            WorkerStats stats;
            if (reas.reportStats)
            {
                stats.total_events_reassembly_err = reas.recvStats.reassemblyLoss;
                stats.total_events_reassembled = reas.recvStats.eventSuccess;
                stats.total_event_enqueue_err = reas.recvStats.enqueueLoss;
                stats.total_bytes_recv = reas.recvStats.totalBytesReceived;
                stats.total_packets_recv = reas.recvStats.totalPacketsReceived;
            }

            auto res = reas.lbman.sendState(fillPercent, PIDTuple.get<0>(), true, stats);
            if (res.has_error())
            {
                // update error counts
                reas.recvStats.grpcErrCnt++;
                reas.recvStats.lastE2SARError = res.error().code();
            }

            // sleep approximately so we wake up every ~100ms
            auto until = nowT + boost::chrono::milliseconds(period_ms);
            boost::this_thread::sleep_until(until);
        }
    }

    result<int> Reassembler::registerWorker(const std::string &node_name) noexcept 
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

        delete eventItem;

        return 0;
    }

    result<int> Reassembler::recvEvent(uint8_t **event, size_t *bytes, EventNum_t* eventNum, uint16_t *dataId, u_int64_t wait_ms) noexcept
    {
        // lock for the mutex (must be thread-local)
        thread_local boost::unique_lock<boost::mutex> condLock(recvThreadMtx, boost::defer_lock);

        auto nowT = boost::chrono::steady_clock::now();
        boost::chrono::steady_clock::time_point nextTimeT;
        bool overtime = false;

        auto eventItem = dequeue();

        // try to dequeue for a bit
        while (eventItem == nullptr && !threadsStop && !overtime)
        {
            condLock.lock();
            recvThreadCond.wait_for(condLock, boost::chrono::milliseconds(recvWaitTimeout_ms));
            condLock.unlock();
 
            eventItem = dequeue();
            nextTimeT = boost::chrono::steady_clock::now();

            if ((wait_ms != 0) && (nextTimeT - nowT > boost::chrono::milliseconds(wait_ms))) 
                overtime = true;
        }
        if (eventItem == nullptr)
            return -1;
        *event = eventItem->event;
        *bytes = eventItem->bytes;
        *eventNum = eventItem->eventNum;
        *dataId = eventItem->dataId;

        delete eventItem;
        return 0;
    }

    result<Reassembler::ReassemblerFlags> Reassembler::ReassemblerFlags::getFromINI(const std::string &iniFile) noexcept
    {
        boost::property_tree::ptree paramTree;
        Reassembler::ReassemblerFlags rFlags;

        // Expand tilde in file path
        std::string expandedPath = expandTilde(iniFile);

        try {
            boost::property_tree::ini_parser::read_ini(expandedPath, paramTree);
        } catch(boost::property_tree::ini_parser_error &ie) {
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, 
                "Unable to parse the reassembler flags configuration file "s + iniFile};
        }

        // general
        rFlags.useCP = paramTree.get<bool>("general.useCP", rFlags.useCP);

        // control plane
        rFlags.useHostAddress = paramTree.get<bool>("control-plane.useHostAddress", rFlags.useHostAddress);
        rFlags.validateCert = paramTree.get<bool>("control-plane.validateCert", rFlags.validateCert);
        rFlags.reportStats = paramTree.get<bool>("control-plane.reportStats", rFlags.reportStats);

        // data plane
        rFlags.portRange = paramTree.get<int>("data-plane.portRange", rFlags.portRange);
        rFlags.withLBHeader = paramTree.get<bool>("data-plane.withLBHeader", rFlags.withLBHeader);
        rFlags.eventTimeout_ms = paramTree.get<int>("data-plane.eventTimeoutMS", rFlags.eventTimeout_ms);
        rFlags.rcvSocketBufSize = paramTree.get<int>("data-plane.rcvSocketBufSize", rFlags.rcvSocketBufSize);
        rFlags.epoch_ms = paramTree.get<u_int32_t>("data-plane.epochMS", rFlags.epoch_ms);
        rFlags.period_ms = paramTree.get<u_int16_t>("data-plane.periodMS", rFlags.period_ms);

        // PID parameters
        rFlags.setPoint = paramTree.get<float>("pid.setPoint", rFlags.setPoint);
        rFlags.Ki = paramTree.get<float>("pid.Ki", rFlags.Ki);
        rFlags.Kp = paramTree.get<float>("pid.Kp", rFlags.Kp);
        rFlags.Kd = paramTree.get<float>("pid.Kd", rFlags.Kd);
        rFlags.Kd = paramTree.get<float>("pid.weight", rFlags.Kd);
        rFlags.Kd = paramTree.get<float>("pid.min_factor", rFlags.Kd);
        rFlags.Kd = paramTree.get<float>("pid.max_factor", rFlags.Kd);

        return rFlags;
    }
}
