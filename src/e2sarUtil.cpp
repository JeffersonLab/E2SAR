#include <iostream>
#include <string>
#include <vector>
#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>

#include "e2sarUtil.hpp"

namespace e2sar {


    /**
     * <p>
     * This is a method to parse a URI which was obtained with the reservation
     * of a load balancer. This URI contains information which both an event sender
     * and event consumer can use to interact with the LB and CP.
     * </p><p>
     * The URI is of the format:
     * ejfat://[<token>@]<cp_host>:<cp_port>/lb/<lb_id>[?data=<data_host>:<data_port>][&sync=<sync_host>:<sync_port>].
     * </p><p>
     * The token is optional and is the instance token with which a consumer can
     * register with the control plane. If the instance token is not available,
     * the administration token can be used to register. A sender will not need
     * either token. The cp_host and cp_port are the host and port used to talk
     * to the control plane. They are exactly the host and port used to reserve an LB.
     * </p><p>
     * The data_host & data_port are the IP address and UDP port to send events/data to.
     * They are optional and not used by the consumer. Likewise the sync_host & sync_port
     * are the IP address and UDP port to which the sender send sync messages.
     * They're also optional and not used by the consumer.
     * The order of data= and sync= must be kept, data first, sync second.
     * If data= is not there, &sync must become ?sync.
     * </p><p>
     * Addresses may be either ipV6 or ipV4, and a distinction is made between them.
     * Each address may be surrounded with square brackets [] which are stripped off.
     * This is nice for ipV6 which includes colons (:) as part of the address and
     * which may get confusing when separated from the port by another colon.
     * </p>
     *
     * @param uri URI to parse.
     */
    EjfatURI::EjfatURI(const std::string &uri) {
        rawURI = uri;

        // parse the URI
        boost::system::result<boost::url_view> r = boost::urls::parse_uri(rawURI);

        if (!r) {
            throw E2SARException("Unable to parse the provided URL "s + rawURI);
        }

        boost::url_view u = r.value();

        if (u.scheme().compare("ejfat")) 
            throw E2SARException("Invalid EJFAT URL scheme: "s + std::string(u.scheme()) + " in URI"s + rawURI);

        if (u.userinfo().length() > 0) {
            adminToken = u.userinfo();
        }

        outcome::result<ip::address> cpAddr_r = string_to_ip(u.host());
        outcome::result<u_int16_t> cpPort_r = string_to_port(u.port());

        if (cpAddr_r && cpPort_r) {
            cpAddr = cpAddr_r.value();
            cpPort = cpPort_r.value();
        } else 
            throw E2SARException("Unable to parse CP address and/or port in URL "s + rawURI);

        // extract the lb ID
        std::vector<std::string> lb_path;
        boost::split(lb_path, u.path(), boost::is_any_of("/"));
        lbId = lb_path.back();

        if (lbId.length() == 0) 
            throw E2SARException("Invalid LB Id: "s + rawURI);

        // deal with the query portion 
        for (auto param: u.params()) {
            outcome::result<std::pair<ip::address, u_int16_t>> r = string_tuple_to_ip_and_port(param.value);
            if (r) {
                std::pair<ip::address, int> p = r.value();
                if (!param.key.compare("sync"s)) {
                    haveSync = true;
                    syncAddr = p.first;
                    syncPort = p.second;
                } else if (!param.key.compare("data"s)) {
                    haveData = true;
                    dataAddr = p.first;
                    dataPort = DATAPLANE_PORT;
                } else 
                    throw E2SARException("Unknown parameter "s + param.key + " in URL "s + rawURI);
            } else 
                throw E2SARException("Unable to parse "s + param.key + " address is URL"s + rawURI);
        }
    }

    /** implicit conversion operator */
    EjfatURI::operator std::string() const {
        return "uri: " + rawURI + " cpAdddr: " + cpAddr.to_string() + ":" + std::to_string(cpPort) +
            (haveSync ? " syncAddr: " + syncAddr.to_string() + ":" + std::to_string(syncPort) : "") +
            (haveData ? " dataAddr: " + dataAddr.to_string() + ":" + std::to_string(dataPort) : "") + 
            " lbName: " + lbName + " lbId: " + lbId + " adminToken: " + adminToken + 
            " instanceToken: " + instanceToken;
    }
}