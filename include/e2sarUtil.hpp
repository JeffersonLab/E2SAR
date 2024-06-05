#ifndef E2SARUTILHPP
#define E2SARUTILHPP

#include <fstream>
#include <vector>
#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>

#include "e2sarError.hpp"

using namespace boost::asio;
using namespace std::string_literals;

/***
 * Supporting classes for E2SAR
 */
namespace e2sar
{
    const u_int16_t DATAPLANE_PORT = 19522;

    /** Structure to hold info parsed from an ejfat URI (and a little extra). */
    class EjfatURI
    {

    private:
        std::string rawURI;
        /** Is there a valid data addr & port? */
        bool haveDatav4;
        bool haveDatav6;
        /** Is there a valid sync addr & port? */
        bool haveSync;
        /** Use TLS */
        bool useTls;

        /** UDP port for event sender to send sync messages to. */
        uint16_t syncPort;
        /** TCP port for grpc communications with CP. */
        uint16_t cpPort;

        /** String given by user, during registration, to label an LB instance. */
        std::string lbName;
        /** String identifier of an LB instance, set by the CP on an LB reservation. */
        std::string lbId;
        /** Admin token for the CP being used. Set from URI string*/
        std::string adminToken;
        /** Instance token set by the CP on an LB reservation. */
        std::string instanceToken;

        /** data plane addresses - there can ever only be one v4 and one v6 */
        ip::address dataAddrv4;
        ip::address dataAddrv6;
        /** address to send sync messages to. Not used, for future expansion. (v4 or v6)*/
        ip::address syncAddr;
        /** IP address (and host if available) for grpc communication with CP. */
        ip::address cpAddr;
        std::string cpHost;

    public:
        /** base constructor, sets instance token from string */
        EjfatURI(const std::string &uri);
        /** rely on implicitly-declared copy constructor as needed */

        /** destructor */
        ~EjfatURI() {}

        /** check if TLS should be used */
        inline bool get_useTls() const
        {
            return useTls;
        }

        /** set instance token based on gRPC return */
        inline void set_InstanceToken(const std::string &t)
        {
            instanceToken = t;
        }

        /** get instance token */
        inline const result<std::string> get_InstanceToken() const
        {
            if (!instanceToken.empty())
                return instanceToken;
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Instance token not available"s};
        }

        /** return the admin token */
        inline const result<std::string> get_AdminToken() const
        {
            if (!adminToken.empty())
                return adminToken;
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Admin token not available"s};
        }

        /** set LB name */
        inline void set_lbName(const std::string &n)
        {
            lbName = n;
        }

        /** set LB Id */
        inline void set_lbId(const std::string &i)
        {
            lbId = i;
        }

        /**
         * Set the sync address (v4 or v6)
        */
        inline void set_syncAddr(const std::pair<ip::address, u_int16_t> &a)
        {
            syncAddr = a.first;
            syncPort = a.second;
            haveSync = true;
        }

        /**
         * Set a dataplane address (v4 or v6)
        */
        inline void set_dataAddr(const std::pair<ip::address, u_int16_t> &a)
        {
            if (a.first.is_v4()) 
                dataAddrv4 = a.first;
            else
                dataAddrv6 = a.first;
        }

        /** get LB name */
        inline const std::string get_lbName()
        {
            return lbName;
        }

        /** get LB ID */
        inline const std::string get_lbId()
        {
            return lbId;
        }

        /** get control plane ip address and port */
        inline const result<std::pair<ip::address, u_int16_t>> get_cpAddr() const
        {
            return std::pair<ip::address, u_int16_t>(cpAddr, cpPort);
        }

        /** get control plane hostname and port */
        inline const result<std::pair<std::string, u_int16_t>> get_cpHost() const
        {
            if (!cpHost.empty())
                return std::pair<std::string, u_int16_t>(cpHost, cpPort);
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Control plane address not available"s};
        }

        /** does the URI contain a v4 dataplane address? */
        inline const bool has_dataAddrv4() const
        {
            return haveDatav4;
        }

        /** does the URI contain a v6 dataplane address? */
        inline const bool has_dataAddrv6() const
        {
            return haveDatav6;
        }

        /** does the URI contain any dataplane address? */
        inline const bool has_dataAddr() const
        {
            return haveDatav4 || haveDatav6;
        }

        /** does the URI contain a sync address */
        inline const bool has_syncAddr() const
        {
            return haveSync;
        }

        /** get data plane v4 address and port */
        inline const result<std::pair<ip::address, u_int16_t>> get_dataAddrv4() const noexcept
        {
            if (haveDatav4)
                return std::pair<ip::address, u_int16_t>(dataAddrv4, DATAPLANE_PORT);
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Data plane address not available"s};
        }

        /** get data plane v6 address and port */
        inline const result<std::pair<ip::address, u_int16_t>> get_dataAddrv6() const noexcept
        {
            if (haveDatav6)
                return std::pair<ip::address, u_int16_t>(dataAddrv6, DATAPLANE_PORT);
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Data plane address not available"s};
        }

        /** get sync address and port */
        inline const result<std::pair<ip::address, u_int16_t>> get_syncAddr() const noexcept
        {
            if (haveSync)
                return std::pair<ip::address, u_int16_t>(syncAddr, syncPort);
            return E2SARErrorInfo{E2SARErrorc::ParameterNotAvailable, "Sync address not available"s};
        }
        /** implicit cast to string */
        operator std::string() const;

        /** from environment variable or file */
        static inline result<EjfatURI> getFromEnv(const std::string &envVar = "EJFAT_URI"s) noexcept
        {
            const char *envStr = std::getenv(envVar.c_str());
            if (envStr != nullptr)
            {
                try
                {
                    return EjfatURI(envStr);
                }
                catch (const E2SARException &e)
                {
                    return E2SARErrorInfo{E2SARErrorc::CaughtException, "Unable to parse URI from environment variable"s};
                }
            }
            return E2SARErrorInfo{E2SARErrorc::Undefined, "Environment variable "s + envVar + " not defined."s};
        }

        /** from a file */
        static inline result<EjfatURI> getFromFile(const std::string &fileName = "/tmp/ejfat_uri"s) noexcept
        {
            if (!fileName.empty())
            {
                std::ifstream file(fileName);
                if (file.is_open())
                {
                    std::string uriLine;
                    if (std::getline(file, uriLine))
                    {
                        file.close();
                        try
                        {
                            return EjfatURI(uriLine);
                        }
                        catch (const E2SARException &e)
                        {
                            return E2SARErrorInfo{E2SARErrorc::CaughtException, "Unable to parse URI."s};
                        }
                    }
                    file.close();
                    return E2SARErrorInfo{E2SARErrorc::Undefined, "Unable to parse URI."s};
                }
            }
            return E2SARErrorInfo{E2SARErrorc::NotFound, "Unable to find file "s + fileName};
        }
    };

    /**
     * Method to map max # of data sources a backend will see to
     * the corressponding PortRange (enum) value in loadbalancer.proto.
     *
     * @param sourceCount max # of data sources backend will see.
     * @return corressponding PortRange.
     */
    static inline int getPortRange(int sourceCount) noexcept
    {
        // Based on the proto file enum for the load balancer, seen below,
        // map the max # of sources a backend will see to the PortRange value.
        // This is necessay to provide the control plane when registering.

        // Handle edge cases
        if (sourceCount < 2)
        {
            return 0;
        }
        else if (sourceCount > 16384)
        {
            return 14;
        }

        int maxCount = 2;
        int iteration = 1;

        while (sourceCount > maxCount)
        {
            iteration++;
            maxCount >>= 1;
        }

        return iteration;
    }

    /**
     * Convert a string into an IPv4 or v6 address throwing E2SARException if a problem is encountered
     */
    static inline const result<ip::address> string_to_ip(const std::string &addr) noexcept
    {
        try
        {
            if (addr[0] == '[')
            {
                // strip '[]' from IPv6
                try
                {
                    ip::make_address(addr.substr(1, addr.length() - 2));
                }
                catch (...)
                {
                    ;
                }
                return ip::make_address(addr.substr(1, addr.length() - 2));
            }
            else
                return ip::make_address(addr);
        }
        catch (boost::system::system_error &e)
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Unable to convert IP address from "s + addr};
        }
    }

    /**
     * Convert a string to a port number, checking range
     */
    static inline const result<u_int16_t> string_to_port(const std::string &port_string) noexcept
    {
        try
        {
            u_int16_t port = std::stoi(port_string);
            if (port < 1024 || port > 65535)
            {
                // port is out of range
                return E2SARErrorInfo{E2SARErrorc::OutOfRange, "Port value "s + port_string + " is out of range"s};
            }
            return port;
        }
        catch (const std::exception &e)
        {
            return E2SARErrorInfo{E2SARErrorc::ParameterError, "Unable to convert "s + port_string + " to integer"s};
        }
    }

    /**
     * Convert a colon-separated tuple into ip address and port. Note that IPv6 in [] can contain colons.
     */
    static inline const result<std::pair<ip::address, u_int16_t>> string_tuple_to_ip_and_port(const std::string &t) noexcept
    {
        // search for last ":" (ip:port) or "]" (ipv6 by itself) whichever comes last
        auto const pos = t.find_last_of("]:");

        // IPv4 or IPv6 by itself
        if ((pos == std::string::npos) || (t[pos] == ']'))
        {
            auto r1 = string_to_ip(t);
            if (r1)
                return std::pair<ip::address, u_int16_t>(r1.value(), 0);
            else
                return E2SARErrorInfo{E2SARErrorc::ParameterError, "Unable to convert "s + t + " to ip address and port"s};
        }

        // port with either IPv4 or IPv6 address x.x.x.x:num or [x:x:x:x::y]:num
        auto r1 = string_to_ip(t.substr(0, pos));
        auto r2 = string_to_port(t.substr(pos + 1));
        if (r1 && r2)
            return std::pair<ip::address, int>(r1.value(), r2.value());
        return E2SARErrorInfo{E2SARErrorc::ParameterError, "Unable to convert "s + t + " to ip address and port"s};
    }

    /**
     * Function to take a host name and turn it into IP addresses, IPv4 and IPv6.
     *
     * @param host_name name of host to examine.
     * @return outcome variable with either has_error() set or value() returning a list of ip::address
    */
    static inline result<std::vector<ip::address>> resolveHost(const std::string &host_name) noexcept
    {

        std::vector<ip::address> addresses;
        boost::asio::io_service io_service;

        try
        {
            ip::udp::resolver resolver(io_service);
            ip::udp::resolver::query query(host_name, "443");
            ip::udp::resolver::iterator iter = resolver.resolve(query);
            ip::udp::resolver::iterator end; // End marker.

            while (iter != end)
            {
                ip::udp::endpoint endpoint = *iter++;
                addresses.push_back(endpoint.address());
            }
            return addresses;
        }
        catch (...)
        {
            // anything happens - we can't find the host
            return E2SARErrorInfo{E2SARErrorc::NotFound, "Unable to convert "s + host_name + " to ip address"s};
        }
    }
};
#endif