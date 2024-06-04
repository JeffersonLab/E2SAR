#include "e2sarCP.hpp"

using namespace google::protobuf;
using namespace boost::posix_time;

namespace e2sar
{
    outcome::result<int> LBManager::reserveLB(const std::string &lb_name,
                                              TimeUntil *until,
                                              const std::vector<std::string> &senders)
    {

        ClientContext context;
        ReserveLoadBalancerRequest req;
        ReserveLoadBalancerReply *rep = nullptr;

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
            return E2SARErrorc::ParameterNotAvailable;

        _cpuri.set_lbName(lb_name);
        req.set_name(lb_name);

        if (until != nullptr)
        {
            req.set_allocated_until(until);
        }
        else
        {
            // if until is null, set until to now + 24 hours
            auto pt = second_clock::local_time();
            auto td = hours(DEFAULT_LB_RESERVE_DURATION);
            ptime pt1 = pt + td;
            auto ts1 = util::TimeUtil::TimeTToTimestamp(to_time_t(pt1));
            req.set_allocated_until(&ts1);
        }

        // add sender IP addresses, but check they are valid
        for(auto s: senders) {
            try {
                auto s_ip = ip::make_address(s);
            } catch (...) {
                return E2SARErrorc::ParameterError;
            }
            req.add_senderaddresses(s);
        }

        // make the RPC call
        Status status = _stub->ReserveLoadBalancer(&context, req, rep);

        if (!status.ok())
            return E2SARErrorc::RPCError;

        if (!rep->token().empty())
            _cpuri.set_InstanceToken(rep->token());

        if (!rep->lbid().empty())
            _cpuri.set_lbId(rep->lbid());

        if (!rep->syncipaddress().empty())
        {
            /** protobuf definition uses u_int32 */
            u_int16_t short_port = rep->syncudpport();
            auto o = string_to_ip(rep->syncipaddress());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), short_port);
            _cpuri.set_syncAddr(a);
        }

        if (!rep->dataipv4address().empty())
        {
            auto o = string_to_ip(rep->dataipv4address());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), DATAPLANE_PORT);
            _cpuri.set_dataAddr(a);
        }

        if (!rep->dataipv6address().empty())
        {
            auto o = string_to_ip(rep->dataipv6address());
            if (o.has_error())
                return o.error();
            std::pair<ip::address, u_int16_t> a(o.value(), DATAPLANE_PORT);
            _cpuri.set_dataAddr(a);
        }

        return 0;
    }

    outcome::result<int> LBManager::reserveLB(const std::string &lb_name,
                                              const boost::posix_time::time_duration &duration,
                                              const std::vector<std::string> &senders)
    {

        auto pt = second_clock::local_time();
        auto pt1 = pt + duration;
        auto ts1 = util::TimeUtil::TimeTToTimestamp(to_time_t(pt1));
        return reserveLB(lb_name, &ts1, senders);
    }

    /**
     * modified from
     * https://stackoverflow.com/questions/116038/how-do-i-read-an-entire-file-into-a-stdstring-in-c
     */
    outcome::result<std::string> read_file(std::string_view path)
    {
        constexpr auto read_size = std::size_t(4096);

        if (path.empty())
            return std::string{};

        auto stream = std::ifstream(path.data());
        stream.exceptions(std::ios_base::badbit);

        if (not stream)
        {
            return E2SARErrorc::NotFound;
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

    outcome::result<grpc::SslCredentialsOptions> LBManager::makeSslOptionsFromFiles(std::string_view pem_root_certs,
                                                                                    std::string_view pem_private_key,
                                                                                    std::string_view pem_cert_chain)
    {
        auto root = read_file(pem_root_certs);
        auto priv = read_file(pem_private_key);
        auto cert = read_file(pem_cert_chain);

        if (root.has_error() || priv.has_error() || cert.has_error())
            return E2SARErrorc::NotFound;

        return makeSslOptions(std::move(root.value()),
                              std::move(priv.value()),
                              std::move(cert.value()));
    }

}