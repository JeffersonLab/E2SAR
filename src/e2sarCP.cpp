#include "e2sarCP.hpp"

using namespace google::protobuf;
using namespace boost::posix_time;

using loadbalancer::ReserveLoadBalancerRequest;
using loadbalancer::ReserveLoadBalancerReply;

using loadbalancer::FreeLoadBalancerReply;
using loadbalancer::FreeLoadBalancerRequest;

using loadbalancer::GetLoadBalancerRequest;

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

    // free previously allocated lb
    result<int> LBManager::freeLB() 
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

        if (_cpuri.get_lbId().empty()) 
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        // make the RPC call
        Status status = _stub->FreeLoadBalancer(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in freeLB(): "s + status.error_message()};
        }

        return 0;
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

        if (_cpuri.get_lbId().empty()) 
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "LB ID not avialable - have you reserved it previously?"s};

        req.set_lbid(_cpuri.get_lbId());

        // make the RPC call
        Status status = _stub->GetLoadBalancer(&context, req, &rep);

        if (!status.ok())
        {
            return E2SARErrorInfo{E2SARErrorc::RPCError, "Error connecting to LB CP in getLB(): "s + status.error_message()};
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

    /**
     * Get load balancer extended info
     * Uses admin token.
     * @return - 0 on success or error code with message on failure
     */
    result<int> LBManager::getLBStatus()
    {
        return 0;
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

}