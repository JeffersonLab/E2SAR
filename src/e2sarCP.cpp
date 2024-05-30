#include "e2sarCP.hpp"

namespace e2sar 
{

outcome::result<int> LBManager::reserveLB(const std::string &lb_name, const TimeUntil &until, bool bearerToken) {

    ClientContext context;
    ReserveLoadBalancerRequest req;
    ReserveLoadBalancerReply *rep = nullptr;

    if (bearerToken) {
        auto adminToken = _cpuri.get_AdminToken();
        if (!adminToken.has_error()) 
            // set bearer token in header
            context.AddMetadata("Authorization", "Bearer "s + adminToken.value());
        else
            return E2SARErrorc::ParameterNotAvailable;
    } else
#if OLD_UDPLBD
        req.set_token(_cpuri.get_AdminToken());
#endif

    _cpuri.set_lbName(lb_name);
    req.set_name(lb_name);

    // make the RPC call
    Status status = _stub->ReserveLoadBalancer(&context, req, rep);

    if (!status.ok())
        return E2SARErrorc::RPCError;

    if (!rep->token().empty()) 
        _cpuri.set_InstanceToken(rep->token());

    if (!rep->lbid().empty())
        _cpuri.set_lbId(rep->lbid());

    if (!rep->syncipaddress().empty()) {
        /** protobuf definition uses u_int32 */
        u_int16_t short_port = rep->syncudpport();
        outcome::result<ip::address> o = string_to_ip(rep->syncipaddress());
        if (o.error()) 
            return o.error();
        std::pair<ip::address, u_int16_t> a(o.value(), short_port);
        _cpuri.set_syncAddr(a);
    }

    if (!rep->dataipv4address().empty()) {
        outcome::result<ip::address> o = string_to_ip(rep->dataipv4address());
        if (o.error()) 
            return o.error();
        std::pair<ip::address, u_int16_t> a(o.value(), DATAPLANE_PORT);
        _cpuri.set_dataAddr(a);
    }

    if (!rep->dataipv6address().empty()) {
        outcome::result<ip::address> o = string_to_ip(rep->dataipv6address());
        if (o.error()) 
            return o.error();
        std::pair<ip::address, u_int16_t> a(o.value(), DATAPLANE_PORT);
        _cpuri.set_dataAddr(a);
    }    

    return E2SARErrorc::NoError;
}

/**
 * modified from 
 * https://stackoverflow.com/questions/116038/how-do-i-read-an-entire-file-into-a-stdstring-in-c
*/
outcome::result<std::string> read_file(std::string_view path) {
    constexpr auto read_size = std::size_t(4096);

    if (path.empty())
        return std::string{};

    auto stream = std::ifstream(path.data());
    stream.exceptions(std::ios_base::badbit);

    if (not stream) {
        return E2SARErrorc::NotFound;
    }
    
    auto out = std::string();
    auto buf = std::string(read_size, '\0');
    while (stream.read(& buf[0], read_size)) {
        out.append(buf, 0, stream.gcount());
    }
    out.append(buf, 0, stream.gcount());
    return out;
}

outcome::result<grpc::SslCredentialsOptions> LBManager::makeSslOptionsFromFiles(std::string_view pem_root_certs,
                                            std::string_view pem_private_key,
                                            std::string_view pem_cert_chain) {
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