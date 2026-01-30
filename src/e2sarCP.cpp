#include "e2sarCP.hpp"

using namespace google::protobuf;
using namespace boost::posix_time;

using loadbalancer::ReserveLoadBalancerReply;
using loadbalancer::ReserveLoadBalancerRequest;

using loadbalancer::FreeLoadBalancerReply;
using loadbalancer::FreeLoadBalancerRequest;

using loadbalancer::GetLoadBalancerRequest;

using loadbalancer::LoadBalancerStatusReply;
using loadbalancer::LoadBalancerStatusRequest;

using loadbalancer::PortRange;
using loadbalancer::RegisterReply;
using loadbalancer::RegisterRequest;

using loadbalancer::DeregisterReply;
using loadbalancer::DeregisterRequest;

using loadbalancer::SendStateReply;
using loadbalancer::SendStateRequest;

using loadbalancer::VersionRequest;
using loadbalancer::VersionReply;

using loadbalancer::OverviewRequest;
using loadbalancer::OverviewReply;

using loadbalancer::AddSendersRequest;
using loadbalancer::AddSendersReply;

using loadbalancer::RemoveSendersRequest;
using loadbalancer::RemoveSendersReply;

using loadbalancer::TimeseriesRequest;
using loadbalancer::TimeseriesResponse;
using loadbalancer::Timeseries;
using loadbalancer::FloatTimeseries;
using loadbalancer::IntegerTimeseries;
using loadbalancer::FloatSample;
using loadbalancer::IntegerSample;

using loadbalancer::CreateTokenRequest;
using loadbalancer::CreateTokenReply;
using loadbalancer::ListTokenPermissionsRequest;
using loadbalancer::ListTokenPermissionsReply;
using loadbalancer::ListChildTokensRequest;
using loadbalancer::ListChildTokensReply;
using loadbalancer::RevokeTokenRequest;
using loadbalancer::RevokeTokenReply;
using loadbalancer::TokenSelector;
using loadbalancer::TokenDetails;
using loadbalancer::TokenPermission;

namespace e2sar
{
    // reserve load balancer
    result<u_int32_t> LBManager::reserveLB(const std::string &lb_name,
                                     const TimeUntil &until,
                                     const std::vector<std::string> &senders,
                                     int ip_family) noexcept 
    {

        ClientContext context;
        ReserveLoadBalancerRequest req;
        ReserveLoadBalancerReply rep;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        _cpuri.set_lbName(lb_name);
        req.set_name(lb_name);

        // this should cover the 0-duration i.e. infinite reservation case
        if ((until.seconds() != 0) || (until.nanos() != 0))
            // Timestamp type is weird. 'Nuf said.
            req.mutable_until()->CopyFrom(until);

        // set single or dual stack LB
        req.set_ipfamily(static_cast<loadbalancer::IpFamily>(ip_family));

        // add sender IP addresses, but check they are valid
        for (auto s : senders)
        {
            try
            {
                ip::make_address(s);
            }
            catch (...)
            {
                return E2SARErrorInfo{E2SARErrorc::ParameterError, "Invalid sender IP addresses"s};
            }
            req.add_senderaddresses(s);
        }

        // make the RPC call
        Status status = _stub->ReserveLoadBalancer(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in reserveLB(): "s + status.error_message()};
        }

        //
        // Fill the URI with information from the reply
        //
        if (!rep.token().empty())
            _cpuri.set_InstanceToken(rep.token());

        if (!rep.lbid().empty())
            _cpuri.set_lbId(rep.lbid());

        if (!rep.syncipv4address().empty())
        {
            /** protobuf definition uses u_int32 */
            u_int16_t short_port = rep.syncudpport();
            auto o = string_to_ip(rep.syncipv4address());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), short_port);
            _cpuri.set_syncAddr(a);
        }

        if (!rep.dataipv4address().empty())
        {
            auto o = string_to_ip(rep.dataipv4address());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), DATAPLANE_PORT);
            _cpuri.set_dataAddr(a);
        }

        if (!rep.dataipv6address().empty())
        {
            auto o = string_to_ip(rep.dataipv6address());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), DATAPLANE_PORT);
            _cpuri.set_dataAddr(a);
        }

        return rep.fpgalbid();
    }

    // reserve via duration
    result<u_int32_t> LBManager::reserveLB(const std::string &lb_name,
                                     const boost::posix_time::time_duration &duration,
                                     const std::vector<std::string> &senders,
                                     int ip_family) noexcept 
    {
        google::protobuf::Timestamp ts1;
        if (duration.is_zero())
        {
            // duration of 0 means indefinite reservation
            ts1.set_seconds(0);
            ts1.set_nanos(0);

        } else 
        {
            auto pt = second_clock::universal_time();
            auto pt1 = pt + duration;
            ts1 = util::TimeUtil::TimeTToTimestamp(to_time_t(pt1));
        }
        return reserveLB(lb_name, ts1, senders);
    }

    // reserve via duration in seconds
    result<u_int32_t> LBManager::reserveLB(const std::string &lb_name,
                                     const double &durationSeconds,
                                     const std::vector<std::string> &senders,
                                     int ip_family) noexcept 
    {
        /// NOTE: this static casting may lose time accuracy, but should be accepted
        boost::posix_time::time_duration duration = boost::posix_time::seconds(static_cast<long>(durationSeconds));
        return reserveLB(lb_name, duration, senders);
    }


    // free previously allocated lb using explicit lb id
    result<int> LBManager::freeLB(const std::string &lbid) noexcept 
    {

        // we only need lb id from the URI
        ClientContext context;
        FreeLoadBalancerRequest req;
        FreeLoadBalancerReply rep;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        if (lbid.empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID must be provided externally and is empty"s};

        req.set_lbid(lbid);

        // make the RPC call
        Status status = _stub->FreeLoadBalancer(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in freeLB(): "s + status.error_message()};
        }

        return 0;
    }

    // free load balancer using the URI
    result<int> LBManager::freeLB() noexcept 
    {

        if (_cpuri.get_lbId().empty())
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable in the URI"s};
        }

        return freeLB(_cpuri.get_lbId());
    }

    // get load balancer info using lbid in the URI
    result<int> LBManager::getLB() noexcept 
    {

        if (_cpuri.get_lbId().empty())
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable in the URI"s};
        }

        return getLB(_cpuri.get_lbId());
    }

    // get load balancer info using lbid provided externally, in this case the URI only needs
    // to contain the admin token and CP address/port
    result<int> LBManager::getLB(const std::string &lbid) noexcept 
    {
        // we only need lb id from the URI
        ClientContext context;
        GetLoadBalancerRequest req;
        ReserveLoadBalancerReply rep;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        if (!lbid.empty())
            req.set_lbid(lbid);
        else if (!_cpuri.get_lbId().empty())
            req.set_lbid(_cpuri.get_lbId());
        else 
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID was not provided in URI or externally"s};

        // make the RPC call
        Status status = _stub->GetLoadBalancer(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in getLB(): "s + status.error_message()};
        }

        //
        // Fill the URI with information from the reply
        //
        // Note that unlike reserve this call CANNOT return an instance token
        if (!rep.lbid().empty())
            _cpuri.set_lbId(rep.lbid());

        if (!rep.syncipv4address().empty())
        {
            /** protobuf definition uses u_int32 */
            u_int16_t short_port = rep.syncudpport();
            auto o = string_to_ip(rep.syncipv4address());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), short_port);
            _cpuri.set_syncAddr(a);
        }

        if (!rep.dataipv4address().empty())
        {
            auto o = string_to_ip(rep.dataipv4address());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), DATAPLANE_PORT);
            _cpuri.set_dataAddr(a);
        }

        if (!rep.dataipv6address().empty())
        {
            auto o = string_to_ip(rep.dataipv6address());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), DATAPLANE_PORT);
            _cpuri.set_dataAddr(a);
        }

        return 0;
    }

    // get LB Status
    result<std::unique_ptr<LoadBalancerStatusReply>> LBManager::getLBStatus(const std::string &lbid) noexcept 
    {
        auto rep = std::make_unique<LoadBalancerStatusReply>();

        // we only need lb id from the URI
        ClientContext context;
        LoadBalancerStatusRequest req;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        if (!lbid.empty())
            req.set_lbid(lbid);
        else if (!_cpuri.get_lbId().empty())
            req.set_lbid(_cpuri.get_lbId());
        else 
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID was not provided in URI or externally"s};

        // make the RPC call
        Status status = _stub->LoadBalancerStatus(&context, req, rep.get());

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in LoadBalancerStatus(): "s + status.error_message()};
        }
        
        return rep;
    }

    // get overview of allocated LBs
    result<std::unique_ptr<OverviewReply>> LBManager::overview() noexcept 
    {
        auto rep = std::make_unique<OverviewReply>();

        // we only need lb id from the URI
        ClientContext context;
        OverviewRequest req;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        // make the RPC call
        Status status = _stub->Overview(&context, req, rep.get());

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in overview(): "s + status.error_message()};
        }

        return rep;
    }

    result<std::unique_ptr<LoadBalancerStatusReply>> LBManager::getLBStatus() noexcept
    {
        if (_cpuri.get_lbId().empty())
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable in the URI"s};
        }

        return getLBStatus(_cpuri.get_lbId());
    }

    // register worker nodes
    result<int> LBManager::registerWorker(const std::string &node_name, std::pair<ip::address, u_int16_t> node_ip_port, float weight, u_int16_t source_count, 
        float min_factor, float max_factor, bool keep_lb_header) noexcept 
    {
        // we only need lb id from the URI
        ClientContext context;
        RegisterRequest req;
        RegisterReply rep;

        // NOTE: This uses instance token
        auto instanceToken = _cpuri.get_InstanceToken();
        if (!instanceToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + instanceToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Instance token not available in the URI"s};

        if (_cpuri.get_lbId().empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        req.set_weight(weight);

        req.set_minfactor(min_factor);

        req.set_maxfactor(max_factor);

        if (node_name.empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Node name can't be empty"s};
        req.set_name(node_name);

        req.set_portrange(static_cast<PortRange>(get_PortRange(source_count)));

        req.set_ipaddress(node_ip_port.first.to_string());

        req.set_keeplbheader(keep_lb_header);

        if (node_ip_port.second <= 1024)
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Ports below 1024 are generally reserved, worker UDP port too low"s};

        req.set_udpport(static_cast<uint32_t>(node_ip_port.second));

        // make the RPC call
        Status status = _stub->Register(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in Register(): "s + status.error_message()};
        }

        //
        // Fill the URI with information from the reply
        //
        if (!rep.token().empty())
            _cpuri.set_SessionToken(rep.token());

        if (!rep.sessionid().empty())
            _cpuri.set_sessionId(rep.sessionid());

        return 0;
    }

    result<int> LBManager::registerWorkerSelf(const std::string &node_name, u_int16_t node_port, float weight, u_int16_t source_count, float min_factor, 
        float max_factor, bool v6, bool keep_lb_header) noexcept
    {

        auto ipRes = _cpuri.getDataplaneLocalAddresses(v6);

        if (ipRes.has_error())
            return ipRes.error();

        if (ipRes.value().size() == 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to determine outgoing dataplane address"};

        return registerWorker(node_name, std::make_pair(ipRes.value()[0], node_port), weight, source_count, min_factor, max_factor);
    }


    // deregister worker
    result<int> LBManager::deregisterWorker() noexcept 
    {
        // we only need lb id from the URI
        ClientContext context;
        DeregisterRequest req;
        DeregisterReply rep;

        // NOTE: This uses session token
        auto sessionToken = _cpuri.get_SessionToken();
        if (!sessionToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + sessionToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Session token not available in the URI"s};

        if (_cpuri.get_lbId().empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        req.set_sessionid(_cpuri.get_sessionId());

        // make the RPC call
        Status status = _stub->Deregister(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in Deregister(): "s + status.error_message()};
        }
        return 0;
    }

    // send worker queue state with explicit timestamp and attach stats
    result<int> LBManager::sendState(float fill_percent, float control_signal, bool is_ready, const Timestamp &ts,
        const WorkerStats &stats ) noexcept 
    {
        // we only need lb id from the URI
        ClientContext context;
        SendStateRequest req;
        SendStateReply rep;

        // NOTE: This uses session token
        auto sessionToken = _cpuri.get_SessionToken();
        if (!sessionToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + sessionToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Session token not available in the URI"s};

        if (_cpuri.get_lbId().empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        req.set_sessionid(_cpuri.get_sessionId());

        if ((fill_percent < 0.0) || (fill_percent > 1.0))
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Fill percent must be a number between 0.0 and 1.0"s};

        req.set_fillpercent(fill_percent);

        req.set_controlsignal(control_signal);

        req.set_isready(is_ready);

        // all the stats. all of them.
        req.set_totaleventsrecv(stats.total_events_recv);
        req.set_totaleventsreassembled(stats.total_events_reassembled);
        req.set_totaleventsreassemblyerr(stats.total_events_reassembly_err);
        req.set_totaleventsdequeued(stats.total_events_dequeued);
        req.set_totaleventenqueueerr(stats.total_event_enqueue_err);
        req.set_totalbytesrecv(stats.total_bytes_recv);
        req.set_totalpacketsrecv(stats.total_packets_recv);

        // Timestamp type is weird. 'Nuf said.
        req.mutable_timestamp()->CopyFrom(ts);

        // make the RPC call
        Status status = _stub->SendState(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in SendState(): "s + status.error_message()};
        }
        return 0;
    }

    // send worker queue state with using local time and attach stats
    result<int> LBManager::sendState(float fill_percent, float control_signal, bool is_ready,
        const WorkerStats &stats) noexcept 
    {
        return sendState(fill_percent, control_signal, is_ready,
                         util::TimeUtil::TimeTToTimestamp(to_time_t(second_clock::universal_time())),
                         stats);
    }

    // send worker queue state with using local time and no stats
    result<int> LBManager::sendState(float fill_percent, float control_signal, bool is_ready) noexcept 
    {
        WorkerStats stats; // constructor will zero out

        return sendState(fill_percent, control_signal, is_ready,
                         util::TimeUtil::TimeTToTimestamp(to_time_t(second_clock::universal_time())),
                         stats);
    }

    // send worker queue state using explicit time and no stats 
    result<int> LBManager::sendState(float fill_percent, float control_signal, bool is_ready, const Timestamp &ts) noexcept 
    {
        WorkerStats stats; // constructor will zero out

        return sendState(fill_percent, control_signal, is_ready, ts, stats);
    }

    result<boost::tuple<std::string, std::string, std::string>> LBManager::version() noexcept {
       // we only need lb id from the URI
        ClientContext context;
        VersionRequest req;
        VersionReply rep;

        // NOTE: This uses admin token
        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Session token not available in the URI"s};

        // make the RPC call
        Status status = _stub->Version(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in Version(): "s + status.error_message()};
        }
        return boost::make_tuple<std::string, std::string, std::string>(rep.commit(), rep.build(), rep.compattag());
    }

    result<int> LBManager::addSenders(const std::vector<std::string>& senders) noexcept
    {
        // we only need lb id from the URI
        ClientContext context;
        AddSendersRequest req;
        AddSendersReply rep;

        // NOTE: This uses instance token
        auto instanceToken = _cpuri.get_InstanceToken();
        if (!instanceToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + instanceToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Instance token not available in the URI"s};

        if (_cpuri.get_lbId().empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        // add sender IP addresses, but check they are valid
        for (auto s : senders)
        {
            try
            {
                ip::make_address(s);
            }
            catch (...)
            {
                return E2SARErrorInfo{E2SARErrorc::ParameterError, "Invalid sender IP addresses"s};
            }
            req.add_senderaddresses(s);
        }

        // make the RPC call
        Status status = _stub->AddSenders(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in AddSenders(): "s + status.error_message()};
        }
        return 0;
    }

    result<int> LBManager::removeSenders(const std::vector<std::string>& senders) noexcept
    {
        // we only need lb id from the URI
        ClientContext context;
        RemoveSendersRequest req;
        RemoveSendersReply rep;

        // NOTE: This uses instance token
        auto instanceToken = _cpuri.get_InstanceToken();
        if (!instanceToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + instanceToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Instance token not available in the URI"s};

        if (_cpuri.get_lbId().empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        // add sender IP addresses, but check they are valid
        for (auto s : senders)
        {
            try
            {
                ip::make_address(s);
            }
            catch (...)
            {
                return E2SARErrorInfo{E2SARErrorc::ParameterError, "Invalid sender IP addresses"s};
            }
            req.add_senderaddresses(s);
        }

        // make the RPC call
        Status status = _stub->RemoveSenders(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in RemoveSenders(): "s + status.error_message()};
        }
        return 0;
    }

    result<int> LBManager::addSenderSelf(bool v6) noexcept
    {
        auto ipRes = _cpuri.getDataplaneLocalAddresses(v6);

        if (ipRes.has_error())
            return ipRes.error();

        if (ipRes.value().size() == 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to determine outgoing dataplane address"};

        std::vector<std::string> strVec;
        for_each(ipRes.value().begin(), ipRes.value().end(), [&strVec](const ip::address &a)
            {
                strVec.push_back(a.to_string());
            }
        );

        return addSenders(strVec);
    }

    result<int> LBManager::removeSenderSelf(bool v6) noexcept
    {
        auto ipRes = _cpuri.getDataplaneLocalAddresses(v6);

        if (ipRes.has_error())
            return ipRes.error();

        if (ipRes.value().size() == 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, "Unable to determine outgoing dataplane address"};

        std::vector<std::string> strVec;
        for_each(ipRes.value().begin(), ipRes.value().end(), [&strVec](const ip::address &a)
            {
                strVec.push_back(a.to_string());
            }
        );
        return removeSenders(strVec);
    }

    // create a new delegated token with specific permissions
    result<std::string> LBManager::createToken(
        const std::string &name,
        const std::vector<e2sar::TokenPermission> &permissions) noexcept
    {
        ClientContext context;
        CreateTokenRequest req;
        CreateTokenReply rep;

        // NOTE: This uses admin token
        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        // validate parameters
        if (name.empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Token name cannot be empty"s};

        if (permissions.empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Permissions vector cannot be empty"s};

        // set token name
        req.set_name(name);

        // convert e2sar::TokenPermission to protobuf TokenPermission and add
        for (const auto &perm : permissions)
        {
            auto* added_perm = req.add_permissions();
            added_perm->set_resourcetype(static_cast<loadbalancer::TokenPermission::ResourceType>(perm.resourceType));
            added_perm->set_resourceid(perm.resourceId);
            added_perm->set_permission(static_cast<loadbalancer::TokenPermission::PermissionType>(perm.permission));
        }

        // make the RPC call
        Status status = _stub->CreateToken(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in createToken(): "s + status.error_message()};
        }

        return rep.token();
    }

    // list all permissions for a specific token
    result<e2sar::TokenDetails> LBManager::listTokenPermissions(
        const e2sar::TokenSelector &target) noexcept
    {
        ListTokenPermissionsReply rep;
        ClientContext context;
        ListTokenPermissionsRequest req;

        // NOTE: This uses admin token
        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        // Convert variant TokenSelector to protobuf TokenSelector
        loadbalancer::TokenSelector pb_selector;
        if (std::holds_alternative<uint32_t>(target))
        {
            pb_selector.set_id(std::get<uint32_t>(target));
        }
        else
        {
            pb_selector.set_token(std::get<std::string>(target));
        }

        req.mutable_target()->CopyFrom(pb_selector);

        // make the RPC call
        Status status = _stub->ListTokenPermissions(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in listTokenPermissions(): "s + status.error_message()};
        }

        // Convert protobuf TokenDetails to e2sar::TokenDetails
        e2sar::TokenDetails details;
        details.name = rep.token().name();
        details.id = rep.token().id();
        details.created_at = rep.token().created_at();

        // Convert permissions
        for (const auto& pb_perm : rep.token().permissions())
        {
            e2sar::TokenPermission perm;
            perm.resourceType = static_cast<EjfatURI::TokenType>(pb_perm.resourcetype());
            perm.resourceId = pb_perm.resourceid();
            perm.permission = static_cast<EjfatURI::TokenPermission>(pb_perm.permission());
            details.permissions.push_back(perm);
        }

        return details;
    }

    // list all child tokens created by a parent token
    result<std::vector<e2sar::TokenDetails>> LBManager::listChildTokens(
        const e2sar::TokenSelector &target) noexcept
    {
        ClientContext context;
        ListChildTokensRequest req;
        ListChildTokensReply rep;

        // NOTE: This uses admin token
        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        // Convert variant TokenSelector to protobuf TokenSelector
        loadbalancer::TokenSelector pb_selector;
        if (std::holds_alternative<uint32_t>(target))
        {
            pb_selector.set_id(std::get<uint32_t>(target));
        }
        else
        {
            pb_selector.set_token(std::get<std::string>(target));
        }

        req.mutable_target()->CopyFrom(pb_selector);

        // make the RPC call
        Status status = _stub->ListChildTokens(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in listChildTokens(): "s + status.error_message()};
        }

        // Convert protobuf TokenDetails to e2sar::TokenDetails vector
        std::vector<e2sar::TokenDetails> ret;
        ret.reserve(rep.tokens_size());

        for (const auto& pb_token : rep.tokens())
        {
            e2sar::TokenDetails details;
            details.name = pb_token.name();
            details.id = pb_token.id();
            details.created_at = pb_token.created_at();

            // Convert permissions
            for (const auto& pb_perm : pb_token.permissions())
            {
                e2sar::TokenPermission perm;
                perm.resourceType = static_cast<EjfatURI::TokenType>(pb_perm.resourcetype());
                perm.resourceId = pb_perm.resourceid();
                perm.permission = static_cast<EjfatURI::TokenPermission>(pb_perm.permission());
                details.permissions.push_back(perm);
            }

            ret.push_back(std::move(details));
        }

        return ret;
    }

    // revoke a token and all its children
    result<int> LBManager::revokeToken(const e2sar::TokenSelector &target) noexcept
    {
        ClientContext context;
        RevokeTokenRequest req;
        RevokeTokenReply rep;

        // NOTE: This uses admin token
        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        // Convert variant TokenSelector to protobuf TokenSelector
        loadbalancer::TokenSelector pb_selector;
        if (std::holds_alternative<uint32_t>(target))
        {
            pb_selector.set_id(std::get<uint32_t>(target));
        }
        else
        {
            pb_selector.set_token(std::get<std::string>(target));
        }

        req.mutable_target()->CopyFrom(pb_selector);

        // make the RPC call
        Status status = _stub->RevokeToken(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in revokeToken(): "s + status.error_message()};
        }

        return 0;
    }

    // retrieve timeseries data for a specific metric path
    result<TimeseriesResult> LBManager::timeseries(const std::string &path, const Timestamp &since) noexcept
    {
        ClientContext context;
        TimeseriesRequest req;
        TimeseriesResponse rep;

        // NOTE: This uses admin token
        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        // validate path is not empty
        if (path.empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Timeseries path cannot be empty"s};

        // add the path selector
        req.add_seriesselector(path);

        // set the since timestamp
        req.mutable_since()->CopyFrom(since);

        // make the RPC call
        Status status = _stub->Timeseries(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in timeseries(): "s + status.error_message()};
        }

        // check if we got any timeseries back
        if (rep.timeseries_size() == 0)
        {
            return E2SARErrorInfo{E2SARErrorc::NotFound, "No timeseries data returned for path: "s + path};
        }

        // traverse the returned timeseries
        std::vector<TimeseriesData> tsd;
        tsd.reserve(rep.timeseries_size());

        for(auto tsind = 0; tsind < rep.timeseries_size(); tsind++)
        {
            const auto &ts = rep.timeseries(tsind);
            if (ts.has_float_samples())
            {
                std::vector<FloatSample> samples;
                const auto &float_ts = ts.float_samples();

                samples.reserve(float_ts.data_size());
                for (int i = 0; i < float_ts.data_size(); i++)
                {
                    const auto &sample = float_ts.data(i);
                    samples.emplace_back(sample.timestamp(), sample.value());
                }
                tsd.emplace_back(ts.name(), ts.unit(), samples);
            }
            else if (ts.has_integer_samples())
            {
                std::vector<IntegerSample> samples;
                const auto &int_ts = ts.integer_samples();

                samples.reserve(int_ts.data_size());
                for (int i = 0; i < int_ts.data_size(); i++)
                {
                    const auto &sample = int_ts.data(i);
                    samples.emplace_back(sample.timestamp(), sample.value());
                }
                tsd.emplace_back(ts.name(), ts.unit(), samples);
            }
            else
            {
                return E2SARErrorInfo{E2SARErrorc::DataError, "Timeseries has neither float nor integer samples"s};
            }
        }

        TimeseriesResult tsr(google::protobuf::util::TimeUtil::TimestampToMilliseconds(rep.since()),tsd);

        // return the result
        return tsr;
    }

    /**
     * modified from
     * https://stackoverflow.com/questions/116038/how-do-i-read-an-entire-file-into-a-stdstring-in-c
     */
    result<std::string> read_file(std::string_view path)
    {
        constexpr auto read_size = std::size_t(4096);

        if (path.empty())
            return std::string{};

        auto stream = std::ifstream(path.data());
        stream.exceptions(std::ios_base::badbit);

        if (not stream)
        {
            return E2SARErrorInfo{E2SARErrorc::NotFound, "Unable to open file"};
        }

        auto out = std::string();
        auto buf = std::string(read_size, '\0');
        while (stream.read(&buf[0], read_size))
        {
            out.append(buf, 0, stream.gcount());
        }
        out.append(buf, 0, stream.gcount());
        return out;
    }

    result<grpc::SslCredentialsOptions> LBManager::makeSslOptionsFromFiles(std::string_view pem_root_certs,
                                                                           std::string_view pem_private_key,
                                                                           std::string_view pem_cert_chain) noexcept
    {
        auto root = read_file(pem_root_certs);
        auto priv = read_file(pem_private_key);
        auto cert = read_file(pem_cert_chain);

        if (root.has_error() || priv.has_error() || cert.has_error())
            return E2SARErrorInfo{E2SARErrorc::NotFound, "Unable to find certificate, key or root cert files"s};

        return makeSslOptions(std::move(root.value()),
                              std::move(priv.value()),
                              std::move(cert.value()));
    }


    // just the server root cert (useful for self-signed)
    result<grpc::SslCredentialsOptions> LBManager::makeSslOptionsFromFiles(
            std::string_view pem_root_certs) noexcept
    {
        auto root = read_file(pem_root_certs);

        if (root.has_error())
            return E2SARErrorInfo{E2SARErrorc::NotFound, "Unable to find root cert file"s};

        return makeSslOptions(std::move(root.value()),
                              ""s,""s);
    }
}