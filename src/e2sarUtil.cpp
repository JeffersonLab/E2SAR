#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>

#ifdef AFFINITY_AVAILABLE
#include <sched.h>
#include <unistd.h>
#endif

#ifdef THRD_AFFINITY_AVAILABLE
#include <pthread.h>
#endif

#ifdef NUMA_AVAILABLE
#include <numa.h>
#endif

#include "e2sarUtil.hpp"

namespace e2sar
{

#ifndef E2SAR_VERSION
#define E2SAR_VERSION "Unknown"
#endif

    const std::string E2SARVersion{E2SAR_VERSION};
    const std::string& get_Version() 
    {
        return E2SARVersion;
    }

    // vector of possible options
    std::vector<Optimizations> available_optimizations {
        Optimizations::none
#ifdef SENDMMSG_AVAILABLE
        , Optimizations::sendmmsg
#endif
#ifdef LIBURING_AVAILABLE
        , Optimizations::liburing_send, Optimizations::liburing_recv
#endif
    };

    // this is where we store selected optimizations;
    OptimizationsWord selected_optimizations{optimizationToValue(Optimizations::none)};

    /**
     * List of strings of available optimizations that are compiled in
     */
    const std::vector<std::string> get_OptimizationsAsStrings() noexcept 
    {
        std::vector<std::string> all_strings;
        for(auto o: available_optimizations)
        {
            all_strings.push_back(optimizationToString(o));
        }
        return all_strings;
    }

    /**
     * A word bit-wise ORing all available optimizationsn
     */
    OptimizationsWord get_OptimizationsAsWord() noexcept 
    {
        OptimizationsWord ow{0};
        for(auto o: available_optimizations)
        {
            ow = ow | optimizationToValue(o);
        }
        return ow;
    }

    /**
     * Select optimizations based on names
     */
    result<int> select_Optimizations(std::vector<std::string>& opts) noexcept
    {
        std::vector<Optimizations> v;
        for(auto o: opts) 
        {
            v.push_back(stringToOptimization(o));
        }
        return select_Optimizations(v);
    }

    /**
     * Select optimizations based on enum value names
     */
    result <int> select_Optimizations(std::vector<Optimizations> &opt) noexcept 
    {
        OptimizationsWord ow = get_OptimizationsAsWord();
        for (auto o: opt)
        {
            OptimizationsWord ov= optimizationToValue(o);
            if (not (ov & ow))
            {
                selected_optimizations = optimizationToValue(Optimizations::none);
                return E2SARErrorInfo{E2SARErrorc::NotFound, "Requested optimization "s + optimizationToString(o) + 
                    " is not available on this platform"s};
            }
            selected_optimizations = selected_optimizations | ov;
        }

        // check for conflict
        if (is_SelectedOptimization(Optimizations::sendmmsg) and
            (is_SelectedOptimization(Optimizations::liburing_recv) or 
            is_SelectedOptimization(Optimizations::liburing_send)))
        {
            selected_optimizations = optimizationToValue(Optimizations::none);
            return E2SARErrorInfo{E2SARErrorc::LogicError, "Requested optimizations are incompatible"};
        }

        // remove 'none' if other optimizations are selected
        if (selected_optimizations ^ optimizationToValue(Optimizations::none))
            selected_optimizations = selected_optimizations ^ optimizationToValue(Optimizations::none);
        return 0;
    }

    /**
     * List of strings of selected optimizations
     */
    const std::vector<std::string> get_SelectedOptimizationsAsStrings() noexcept
    {
        std::vector<Optimizations> opts = get_SelectedOptimizations();
        std::vector<std::string> ret;
        for(auto o: opts)
        {
            ret.push_back(optimizationToString(o));
        }
        return ret;
    }

    /**
     * A word bitwise ORing all selected optimizations
     */
    const std::vector<Optimizations> get_SelectedOptimizations() noexcept 
    {
        std::vector<Optimizations> ret;
        for(auto o: available_optimizations) 
        {
            if (is_SelectedOptimization(o))
                ret.push_back(o);
        }
        return ret;
    }

    bool is_SelectedOptimization(Optimizations o) noexcept 
    {
        return optimizationToValue(o) & selected_optimizations;
    }

    /**
     * <p>
     * This is a class to maintain EJFAT URI. This URI contains information which both
     * an event sender and event consumer can use to interact with the LB and CP as well
     * as to send/receive packets in the dataplane. Different parts of the URI are used
     * for different purposes in different contexts.
     * </p></p>
     * The URI is of the format:
     * ejfat[s]://[<token>@]<cp_host>:<cp_port>/[lb/<lb_id>][?[data=<data_host>][&sync=<sync_host>:<sync_port>][&sessionid=<session id>]].
     * </p><p>
     * The token could be administrative, instance or session, depending on the context
     * of the call. Constructor allows you to specify which token is in the URI string.
     * </p><p>
     * Load balancer ID can be specified if known (after the LB is reserved).
     * </p><p>
     * The data_host is the IP address to send events/data to.
     * Likewise the sync_host & sync_port are the IP address and UDP port to which the sender 
     * sends sync messages.
     * </p><p>
     * Session id is used primarily for unregistering worker nodes and sending worker node
     * queue state.
     * </p><p>
     * Addresses may be either ipV6 or ipV4, and a distinction is made between them.
     * IPv6 address is surrounded with square brackets [] which are stripped off, as needed.
     * </p><p>
     * More information on the interpretation of different URI fields can be found in
     * https://github.com/JeffersonLab/E2SAR/wiki/Integration
     * 
     * </p>
     *
     * @param uri URI to parse.
     */
    EjfatURI::EjfatURI(const std::string &uri, TokenType tt, bool pV6) : 
        rawURI{uri}, haveDatav4{false}, haveDatav6{false}, 
        haveSync(false), useTls{false}, preferV6{pV6}
    {

        // parse the URI
        auto r = boost::urls::parse_uri(rawURI);

        if (!r)
        {
            throw E2SARException("Unable to parse the provided URI "s + rawURI);
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
            throw E2SARException("Invalid EJFAT URL scheme: "s + std::string(u.scheme()) + " in URI "s + rawURI);

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
                    // find the first IPv4 or IPv6 address based on preference
                    bool assigned = false;
                    for(auto a: ip_list_r.value())
                    {
                        if (preferV6 && a.is_v6())
                        {
                            cpAddr = a;
                            assigned = true;
                            break;
                        }
                        if (!preferV6 && a.is_v4())
                        {
                            cpAddr = a;
                            assigned = true;
                            break;
                        }
                    }
                    if (!assigned)
                        throw E2SARException("Unable to find "s + (preferV6? "IPv6": "IPv4") + 
                            " address for host " + u.host() + " in URI "s + rawURI);
                    cpPort = cpPort_r.value();
                    cpHost = u.host();
                }
                else
                    throw E2SARException("Unable to resolve host name to IP address in URI "s + u.host() +
                                         " due to "s + ip_list_r.error().message());
            }
            else
                throw E2SARException("Unable to parse CP address and/or port in URI "s + rawURI);
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
            if (!param.key.compare("sessionid"s))
            {
                sessionId = param.value;
                continue;
            }
            else 
            {
                // sync or data
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
                        dataPort = (p.second == 0 ? DATAPLANE_PORT : p.second);
                    }
                    else
                        throw E2SARException("Unknown parameter "s + param.key + " in URI "s + rawURI);
                }
                else
                    throw E2SARException("Unable to parse "s + param.key + " address in URI "s + rawURI);
            }
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
               (haveDatav6 ? "data="s + "[" + dataAddrv6.to_string() + "]" : ""s) +
               (!sessionId.empty() ? "&sessionid="s + sessionId : ""s);
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
               (cpHost.empty() ? (cpAddr.is_v6() ? "[" + cpAddr.to_string() + "]" : cpAddr.to_string())  : cpHost) + ":"s +
               std::to_string(cpPort) +
               "/"s +
               (!lbId.empty() ? "lb/"s + lbId : ""s) +
               (haveSync || haveDatav4 || haveDatav6 ? "?"s : ""s) +
               (haveSync ? "sync="s + (syncAddr.is_v6() ? "[" + syncAddr.to_string() + "]" : syncAddr.to_string()) + ":"s + std::to_string(syncPort) : ""s) +
               (haveSync && (haveDatav4 || haveDatav6) ? "&"s : ""s) +
               (haveDatav4 ? "data="s + dataAddrv4.to_string() + (haveDatav6 ? "&"s : ""s) : ""s) +
               (haveDatav6 ? "data="s + "[" + dataAddrv6.to_string() + "]" : ""s) +
               (!sessionId.empty() ? "&sessionid="s + sessionId : ""s);
    }

    result<int> setProcessAffinity(const std::vector<int> &cores) noexcept
    {
#ifdef AFFINITY_AVAILABLE
        // set this process affinity to the indicated set of cores
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);

        for(auto core: cores)
            CPU_SET(core, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(errno)};
        return 0;
#else
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Setting process affinity not available on this system"};
#endif
    }

    result<int> setThreadAffinity(int core) noexcept
    {
#ifdef THRD_AFFINITY_AVAILABLE
        cpu_set_t cpuset;

        CPU_ZERO(&cpuset);
        CPU_SET(core, &cpuset);
        int err{0};
        if ((err = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset)) < 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(err)};
        return err;
#else
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Setting thread affinity not available on this system"};
#endif
    }

    result<int> setThreadAffinityXOR(const std::vector<int> &cores) noexcept
    {
#ifdef THRD_AFFINITY_AVAILABLE
        cpu_set_t cpuset, curset, xorset;

        // set the mast that we will XOR with
        CPU_ZERO(&cpuset);
        for(int core: cores) 
            CPU_SET(core, &cpuset);
        
        // get the current mask of the thread
        auto thread = pthread_self();
        CPU_ZERO(&curset);
        int err{0};
        if ((err = pthread_getaffinity_np(thread, sizeof(curset), &curset)) < 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(err)};

        // do the XOR
        CPU_ZERO(&xorset);
        CPU_XOR(&xorset, &cpuset, &curset);
        // set thread to use the xorset which now excludes cores we don't want
        if ((err = pthread_setaffinity_np(thread, sizeof(xorset), &xorset)) < 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, strerror(err)};
        return err;
#else
        return E2SARErrorInfo{E2SARErrorc::SystemError, "Setting thread affinity not available on this system"};
#endif
    }

    result<int> setNUMABind(int node) noexcept
    {
#ifdef NUMA_AVAILABLE
        struct bitmask *numa_mask;

        int nr_nodes = numa_max_node() + 1;

        if (numa_available() < 0)
            return E2SARErrorInfo{E2SARErrorc::SystemError, "NUMA management not supported on this system"};
        
        if ((node > nr_nodes - 1) or (node < 0))
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Requested NUMA node not valid"};

        numa_mask = numa_bitmask_alloc(numa_max_node() + 1);
        numa_bitmask_setbit(numa_mask, node);

        // makes sure all memory allocations come from the specified NUMA node
        numa_set_membind(numa_mask);

        numa_bitmask_free(numa_mask);
        return 0;
#else
        return E2SARErrorInfo{E2SARErrorc::SystemError, "NUMA management not available on this system"};
#endif
    }
}