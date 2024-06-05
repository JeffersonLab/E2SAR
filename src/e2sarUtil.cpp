#include <iostream>
#include <string>
#include <vector>
#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>

#include "e2sarUtil.hpp"

namespace e2sar
{

    /**
     * <p>
     * This is a method to parse an EJFAT URI. This URI contains information which both
     * an event sender and event consumer can use to interact with the LB and CP.
     * </p></p>
     * The URI is of the format:
     * ejfat://[<token>@]<cp_host>:<cp_port>/lb/<lb_id>[?[data=<data_host>:<data_port>][&sync=<sync_host>:<sync_port>]].
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
     * </p><p>
     * Addresses may be either ipV6 or ipV4, and a distinction is made between them.
     * IPv6 address is surrounded with square brackets [] which are stripped off.
     * </p>
     *
     * @param uri URI to parse.
     */
    EjfatURI::EjfatURI(const std::string &uri) : rawURI{uri}, haveData{false}, haveSync(false), useTls{false}
    {

        // parse the URI
        auto r = boost::urls::parse_uri(rawURI);

        if (!r)
        {
            throw E2SARException("Unable to parse the provided URL "s + rawURI);
        }

        boost::url_view u = r.value();

        if (!u.scheme().compare("ejfat"))
        {
            ;
        }
        else if (!u.scheme().compare("ejfats"))
        {
            useTls = true;
        }
        else
            throw E2SARException("Invalid EJFAT URL scheme: "s + std::string(u.scheme()) + " in URI"s + rawURI);

        if (u.userinfo().length() > 0)
        {
            adminToken = u.userinfo();
        }

        // see if the host needs resolving
        auto cpAddr_r = string_to_ip(u.host());
        auto cpPort_r = string_to_port(u.port());

        if (!cpAddr_r.has_error() && !cpPort_r.has_error())
        {
            cpAddr = cpAddr_r.value();
            cpPort = cpPort_r.value();
        }
        else
        {
            if (cpAddr_r.has_error() && !cpPort_r.has_error())
            {
                // maybe we were given a hostname, not an IP address
                auto ip_list_r = resolveHost(u.host());
                if (!ip_list_r.has_error())
                {
                    // take the first IP address to assign to cpAddr
                    cpAddr = ip_list_r.value()[0];
                    cpPort = cpPort_r.value();
                    cpHost = u.host();
                }
                else
                    throw E2SARException("Unable to resolve host name to IP address in URL "s + u.host());
            }
            else
                throw E2SARException("Unable to parse CP address and/or port in URL "s + rawURI);
        }

        // extract the lb ID
        std::vector<std::string> lb_path;
        boost::split(lb_path, u.path(), boost::is_any_of("/"));
        lbId = lb_path.back();

        if (lbId.length() == 0)
            throw E2SARException("Invalid LB Id: "s + rawURI);

        // deal with the query portion
        for (auto param : u.params())
        {
            result<std::pair<ip::address, u_int16_t>> r = string_tuple_to_ip_and_port(param.value);
            if (r)
            {
                std::pair<ip::address, int> p = r.value();
                if (!param.key.compare("sync"s))
                {
                    haveSync = true;
                    syncAddr = p.first;
                    syncPort = p.second;
                }
                else if (!param.key.compare("data"s))
                {
                    haveData = true;
                    dataAddr = p.first;
                    dataPort = DATAPLANE_PORT;
                }
                else
                    throw E2SARException("Unknown parameter "s + param.key + " in URL "s + rawURI);
            }
            else
                throw E2SARException("Unable to parse "s + param.key + " address in URL"s + rawURI);
        }
    }

    /** implicit conversion operator */
    EjfatURI::operator std::string() const
    {
        std::string token;
        // prefer instance token to admin token for printing out
        if (!adminToken.empty())
            token = adminToken;
        if (!instanceToken.empty())
            token = instanceToken;

        return (useTls ? "ejfats"s : "ejfat"s) + "://"s + (!token.empty() ? token + "@"s : ""s) +
               (cpHost.empty() ? (cpAddr.is_v6() ? "[" + cpAddr.to_string() + "]" : cpAddr.to_string()) + ":"s + std::to_string(cpPort) : cpHost + ":"s + std::to_string(cpPort)) +
               "/lb/"s + lbId +
               (haveSync || haveData ? "?"s : ""s) +
               (haveSync ? "sync="s + (syncAddr.is_v6() ? "[" + syncAddr.to_string() + "]" : syncAddr.to_string()) + ":"s + std::to_string(syncPort) : ""s) +
               (haveSync && haveData ? "&"s : ""s) +
               (haveData ? "data="s + (dataAddr.is_v6() ? "[" + dataAddr.to_string() + "]" : dataAddr.to_string()) : ""s);
    }
}