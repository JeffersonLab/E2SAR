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

namespace e2sar
{
    // reserve load balancer
    result<int> LBManager::reserveLB(const std::string &lb_name,
                                     const TimeUntil &until,
                                     const std::vector<std::string> &senders)
    {

        ClientContext context;
        ReserveLoadBalancerRequest req;
        ReserveLoadBalancerReply rep;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
#if TOKEN_IN_BODY
            // set bearer token in body (the old way)
            req.set_token(_cpuri.get_AdminToken());
#else
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
#endif
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available in the URI"s};

        _cpuri.set_lbName(lb_name);
        req.set_name(lb_name);

        // Timestamp type is weird. 'Nuf said.
        req.mutable_until()->CopyFrom(until);

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

        if (!rep.syncipaddress().empty())
        {
            /** protobuf definition uses u_int32 */
            u_int16_t short_port = rep.syncudpport();
            auto o = string_to_ip(rep.syncipaddress());
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

    // reserve via duration
    result<int> LBManager::reserveLB(const std::string &lb_name,
                                     const boost::posix_time::time_duration &duration,
                                     const std::vector<std::string> &senders)
    {

        auto pt = second_clock::local_time();
        auto pt1 = pt + duration;
        auto ts1 = util::TimeUtil::TimeTToTimestamp(to_time_t(pt1));
        return reserveLB(lb_name, ts1, senders);
    }

    // free previously allocated lb using explicit lb id
    result<int> LBManager::freeLB(const std::string &lbid)
    {

        // we only need lb id from the URI
        ClientContext context;
        FreeLoadBalancerRequest req;
        FreeLoadBalancerReply rep;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
#if TOKEN_IN_BODY
            // set bearer token in body (the old way)
            req.set_token(_cpuri.get_AdminToken());
#else
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
#endif
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
    result<int> LBManager::freeLB()
    {

        if (_cpuri.get_lbId().empty())
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable in the URI"s};
        }

        return freeLB(_cpuri.get_lbId());
    }

    // get load balancer info using lbid in the URI
    result<int> LBManager::getLB()
    {

        if (_cpuri.get_lbId().empty())
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable in the URI"s};
        }

        return getLB(_cpuri.get_lbId());
    }

    // get load balancer info using lbid provided externally, in this case the URI only needs
    // to contain the admin token and CP address/port
    result<int> LBManager::getLB(const std::string &lbid)
    {
        // we only need lb id from the URI
        ClientContext context;
        GetLoadBalancerRequest req;
        ReserveLoadBalancerReply rep;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
#if TOKEN_IN_BODY
            // set bearer token in body (the old way)
            req.set_token(_cpuri.get_AdminToken());
#else
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
#endif
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

        if (!rep.syncipaddress().empty())
        {
            /** protobuf definition uses u_int32 */
            u_int16_t short_port = rep.syncudpport();
            auto o = string_to_ip(rep.syncipaddress());
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
    result<std::unique_ptr<LoadBalancerStatusReply>> LBManager::getLBStatus(const std::string &lbid)
    {
        auto rep = std::make_unique<LoadBalancerStatusReply>();

        // we only need lb id from the URI
        ClientContext context;
        LoadBalancerStatusRequest req;

        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
#if TOKEN_IN_BODY
            // set bearer token in body (the old way)
            req.set_token(_cpuri.get_AdminToken());
#else
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
#endif
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

    result<std::unique_ptr<LoadBalancerStatusReply>> LBManager::getLBStatus()
    {
        if (_cpuri.get_lbId().empty())
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable in the URI"s};
        }

        return getLBStatus(_cpuri.get_lbId());
    }

    // register worker nodes
    result<int> LBManager::registerWorker(const std::string &node_name, std::pair<ip::address, u_int16_t> node_ip_port, float weight, u_int16_t source_count)
    {
        // we only need lb id from the URI
        ClientContext context;
        RegisterRequest req;
        RegisterReply rep;

        // NOTE: This uses instance token
        auto instanceToken = _cpuri.get_InstanceToken();
        if (!instanceToken.has_error())
        {
#if TOKEN_IN_BODY
            // set bearer token in body (the old way)
            req.set_token(_cpuri.get_AdminToken());
#else
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + instanceToken.value());
#endif
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Instance token not available in the URI"s};

        if (_cpuri.get_lbId().empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        req.set_weight(weight);

        if (node_name.empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Node name can't be empty"s};
        req.set_name(node_name);

        req.set_portrange(static_cast<PortRange>(get_PortRange(source_count)));

        req.set_ipaddress(node_ip_port.first.to_string());

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

    // deregister worker
    result<int> LBManager::deregisterWorker()
    {
        // we only need lb id from the URI
        ClientContext context;
        DeregisterRequest req;
        DeregisterReply rep;

        // NOTE: This uses session token
        auto sessionToken = _cpuri.get_SessionToken();
        if (!sessionToken.has_error())
        {
#if TOKEN_IN_BODY
            // set bearer token in body (the old way)
            req.set_token(_cpuri.get_AdminToken());
#else
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + sessionToken.value());
#endif
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

    // send worker queue state with explicit timestamp
    result<int> LBManager::sendState(float fill_percent, float control_signal, bool is_ready, const Timestamp &ts)
    {
        // we only need lb id from the URI
        ClientContext context;
        SendStateRequest req;
        SendStateReply rep;

        // NOTE: This uses session token
        auto sessionToken = _cpuri.get_SessionToken();
        if (!sessionToken.has_error())
        {
#if TOKEN_IN_BODY
            // set bearer token in body (the old way)
            req.set_token(_cpuri.get_AdminToken());
#else
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + sessionToken.value());
#endif
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Session token not available in the URI"s};

        if (_cpuri.get_lbId().empty())
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        req.set_sessionid(_cpuri.get_sessionId());

        if ((fill_percent < 0.0) || (fill_percent > 10))
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Fill percent must be a number between 0.0 and 1.0"s};

        req.set_fillpercent(fill_percent);

        req.set_controlsignal(control_signal);

        req.set_isready(is_ready);

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

    // send worker queue state with using local time
    result<int> LBManager::sendState(float fill_percent, float control_signal, bool is_ready)
    {
        return sendState(fill_percent, control_signal, is_ready,
                         util::TimeUtil::TimeTToTimestamp(to_time_t(second_clock::local_time())));
    }

    result<std::string> LBManager::version() {
       // we only need lb id from the URI
        ClientContext context;
        VersionRequest req;
        VersionReply rep;

        // NOTE: This uses admin token
        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error())
        {
#if TOKEN_IN_BODY
            // set bearer token in body (the old way)
            req.set_token(_cpuri.get_AdminToken());
#else
            // set bearer token in header
            context.AddMetadata("authorization"s, "Bearer "s + adminToken.value());
#endif
        }
        else
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Session token not available in the URI"s};

        // make the RPC call
        Status status = _stub->Version(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in Version(): "s + status.error_message()};
        }
        return rep.commit();
    }

    // get updated statistics
    result<int> probeStats();

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
                                                                           std::string_view pem_cert_chain)
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
            std::string_view pem_root_certs)
    {
        auto root = read_file(pem_root_certs);

        if (root.has_error())
            return E2SARErrorInfo{E2SARErrorc::NotFound, "Unable to find root cert file"s};

        return makeSslOptions(std::move(root.value()),
                              ""s,""s);
    }
}