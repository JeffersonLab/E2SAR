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
    EjfatURI::EjfatURI(const std::string &uri, TokenType tt) : rawURI{uri}, haveDatav4{false}, haveDatav6{false}, haveSync(false), useTls{false}
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
            switch (tt)
            {
            case TokenType::admin:
                adminToken = u.userinfo();
                break;
            case TokenType::instance:
                instanceToken = u.userinfo();
                break;
            case TokenType::session:
                sessionToken = u.userinfo();
                break;
            }
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
                    throw E2SARException("Unable to resolve host name to IP address in URL "s + u.host() +
                                         " due to "s + ip_list_r.error().message());
            }
            else
                throw E2SARException("Unable to parse CP address and/or port in URL "s + rawURI);
        }

        // extract the lb ID
        std::vector<std::string> lb_path;
        boost::split(lb_path, u.path(), boost::is_any_of("/"));
        // if there is lb
        if (lb_path.size())
            lbId = lb_path.back();

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
                    if (p.first.is_v4())
                    {
                        haveDatav4 = true;
                        dataAddrv4 = p.first;
                    }
                    else
                    {
                        haveDatav6 = true;
                        dataAddrv6 = p.first;
                    }
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
        // select which token to print
        auto token = std::cref(adminToken);

        if (!instanceToken.empty())
            token = std::cref(instanceToken);

        if (!sessionToken.empty())
            token = std::cref(sessionToken);

        return (useTls ? "ejfats"s : "ejfat"s) + "://"s + (!token.get().empty() ? token.get() + "@"s : ""s) +
               (cpHost.empty() ? (cpAddr.is_v6() ? "[" + cpAddr.to_string() + "]" : cpAddr.to_string()) + ":"s + std::to_string(cpPort) : cpHost + ":"s + std::to_string(cpPort)) +
               "/"s +
               (!lbId.empty() ? "lb/"s + lbId : ""s) +
               (haveSync || haveDatav4 || haveDatav6 ? "?"s : ""s) +
               (haveSync ? "sync="s + (syncAddr.is_v6() ? "[" + syncAddr.to_string() + "]" : syncAddr.to_string()) + ":"s + std::to_string(syncPort) : ""s) +
               (haveSync && (haveDatav4 || haveDatav6) ? "&"s : ""s) +
               (haveDatav4 ? "data="s + dataAddrv4.to_string() + (haveDatav6 ? "&"s : ""s) : ""s) +
               (haveDatav6 ? "data="s + "[" + dataAddrv6.to_string() + "]" : ""s);
    }

    bool operator==(const EjfatURI &u1, const EjfatURI &u2)
    {
        return (u1.adminToken == u2.adminToken &&
                u1.instanceToken == u2.instanceToken &&
                u1.sessionToken == u2.sessionToken &&
                u1.cpAddr == u2.cpAddr &&
                u1.cpPort == u2.cpPort &&
                u1.dataAddrv4 == u2.dataAddrv4 &&
                u1.dataAddrv6 == u2.dataAddrv6 &&
                u1.syncAddr == u2.syncAddr &&
                u1.syncPort == u2.syncPort &&
                u1.lbId == u2.lbId &&
                u1.sessionId == u2.sessionId &&
                u1.lbName == u2.lbName);
    }

    const std::string EjfatURI::to_string(TokenType tt) const
    {
        // select which token to print
        auto token = std::cref(adminToken);

        switch (tt)
        {
        case TokenType::instance:
            token = std::cref(instanceToken);
            break;
        case TokenType::session:
            token = std::cref(sessionToken);
        case TokenType::admin:;
            ;
        }

        return (useTls ? "ejfats"s : "ejfat"s) + "://"s + (!token.get().empty() ? token.get() + "@"s : ""s) +
               (cpHost.empty() ? (cpAddr.is_v6() ? "[" + cpAddr.to_string() + "]" : cpAddr.to_string()) + ":"s + std::to_string(cpPort) : cpHost + ":"s + std::to_string(cpPort)) +
               "/"s +
               (!lbId.empty() ? "lb/"s + lbId : ""s) +
               (haveSync || haveDatav4 || haveDatav6 ? "?"s : ""s) +
               (haveSync ? "sync="s + (syncAddr.is_v6() ? "[" + syncAddr.to_string() + "]" : syncAddr.to_string()) + ":"s + std::to_string(syncPort) : ""s) +
               (haveSync && (haveDatav4 || haveDatav6) ? "&"s : ""s) +
               (haveDatav4 ? "data="s + dataAddrv4.to_string() + (haveDatav6 ? "&"s : ""s) : ""s) +
               (haveDatav6 ? "data="s + "[" + dataAddrv6.to_string() + "]" : ""s);
    }
}