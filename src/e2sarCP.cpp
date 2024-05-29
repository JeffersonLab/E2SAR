#include "e2sarCP.hpp"

namespace e2sar 
{

outcome::result<int> LBManager::reserveLB(const std::string &lb_name, const TimeUntil &until) {

    ClientContext context;
    ReserveLoadBalancerRequest req;
    ReserveLoadBalancerReply *rep = nullptr;


    _cpuri.set_lbName(lb_name);
    req.set_name(lb_name);
    req.set_token("token"s);

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

}