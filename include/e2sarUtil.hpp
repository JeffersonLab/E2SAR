#ifndef E2SARUTILHPP
#define E2SARUTILHPP

#include <fstream>
#include <boost/url.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>

#include "e2sarError.hpp"

using namespace boost::asio;
using namespace std::string_literals;

/***
 * Supporting classes for E2SAR
*/
namespace e2sar {
    const int DATAPLANE_PORT = 19522;

    /** Structure to hold info parsed from an ejfat URI (and a little extra). */
    class EjfatURI {

        private:
            std::string rawURI; 
            /** Is there a valid instance token? */
            bool haveInstanceToken;
            /** Is there a valid data addr & port? */
            bool haveData;
            /** Is there a valid sync addr & port? */
            bool haveSync;

            /** UDP port to send events (data) to. */
            uint16_t dataPort;
            /** UDP port for event sender to send sync messages to. */
            uint16_t syncPort;
            /** TCP port for grpc communications with CP. */
            uint16_t cpPort;

            /** String given by user, during registration, to label an LB instance. */
            std::string lbName;
            /** String identifier of an LB instance, set by the CP on an LB reservation. */
            std::string lbId;
            /** Admin token for the CP being used. */
            std::string adminToken;
            /** Instance token set by the CP on an LB reservation. */
            std::string instanceToken;
            /** address to send events (data) to (v4 or v6). */
            ip::address dataAddr;
            /** address to send sync messages to. Not used, for future expansion. (v4 or v6)*/
            ip::address syncAddr;

            /** IP address for grpc communication with CP. */
            ip::address cpAddr;
        
        public:
            /** base constructor */
            EjfatURI(const std::string& uri);
            /** rely on implicitly-declared copy constructor as needed */

            /** destructor */
            ~EjfatURI() {}

            /** set admin token */
            inline void set_AdminToken(const std::string &t) {
                adminToken = t;
            }

            /** set LB name */
            inline void set_lbName(const std::string &n) {
                lbName = n;
            }

            /** get LB name */
            inline const std::string get_lbName() {
                return lbName;
            }

            /** get LB ID */
            inline const std::string get_lbId() {
                return lbId;
            }

            /** get control plane ip address and port */ 
            inline const outcome::result<std::pair<ip::address, int>> get_cpAddr() const {
                return std::pair<ip::address, int>(cpAddr, cpPort);
            }

            /** does the URI contain a dataplane address? */
            inline const bool has_dataAddr() const {
                return haveData;
            }

            /** does the URI contain a sync address */
            inline const bool has_syncAddr() const {
                return haveSync;
            }

            /** get data plane address and port */ 
            inline const outcome::result<std::pair<ip::address, int>> get_dataAddr() const noexcept {
                if (haveData) 
                    return std::pair<ip::address, int>(dataAddr, dataPort);
                return E2SARErrorc::ParameterNotAvailable;
            }

            /** get sync address and port */
            inline const outcome::result<std::pair<ip::address, int>> get_syncAddr() const noexcept {
                if (haveSync)
                    return std::pair<ip::address, int>(syncAddr, syncPort);
                return E2SARErrorc::ParameterNotAvailable;
            }
            /** implicit cast to string */
            operator std::string() const;

            /** from environment variable or file */
            static inline outcome::result<EjfatURI> getFromEnv(const std::string& envVar = "EJFAT_URI"s) noexcept {
                const char *envStr = std::getenv(envVar.c_str());
                if (envStr != nullptr) {
                    try {
                        return EjfatURI(envStr);
                    } catch (const E2SARException &e) {
                        return E2SARErrorc::CaughtException;
                    }
                }
                return E2SARErrorc::Undefined;
            }

            /** from a file */
            static inline outcome::result<EjfatURI> getFromFile(const std::string& fileName = "/tmp/ejfat_uri"s) noexcept {
                if (!fileName.empty()) {
                    std::ifstream file(fileName);
                    if (file.is_open()) {
                        std::string uriLine;
                        if (std::getline(file, uriLine)) {
                            file.close();
                            try {
                                return EjfatURI(uriLine);
                            } catch (const E2SARException &e) {
                                return E2SARErrorc::CaughtException;
                            }
                        }
                        file.close();
                        return E2SARErrorc::Undefined;
                    }
                }
                return E2SARErrorc::NotFound;
            }
    };

    /**
     * Method to map max # of data sources a backend will see to
     * the corressponding PortRange (enum) value in loadbalancer.proto.
     *
     * @param sourceCount max # of data sources backend will see.
     * @return corressponding PortRange.
     */
    static inline int getPortRange(int sourceCount) noexcept {

        // Based on the proto file enum for the load balancer, seen below,
        // map the max # of sources a backend will see to the PortRange value.
        // This is necessay to provide the control plane when registering.

        // Handle edge cases
        if (sourceCount < 2) {
            return 0;
        }
        else if (sourceCount > 16384) {
            return 14;
        }

        int maxCount  = 2;
        int iteration = 1;

        while (sourceCount > maxCount) {
            iteration++;
            maxCount >>= 1;
        }

        return iteration;
    }

    /**
     * Convert a string into an IPv4 or v6 address throwing E2SARException if a problem is encountered
    */
    static inline const outcome::result<ip::address> string_to_ip(const std::string& addr) noexcept {
        try {
            return ip::make_address(addr);
        } catch (boost::system::system_error &e) {
            return E2SARErrorc::ParameterError;
        }
    }

    /**
     * Convert a string to a port number, checking range
    */
    static inline const outcome::result<int> string_to_port(const std::string& port_string) {
        try {
            int port = std::stoi(port_string);
            if (port < 1024 || port > 65535) {
                // port is out of range
                return E2SARErrorc::OutOfRange;
            }
            return port;
        } catch (const std::exception &e) {
            return E2SARErrorc::ParameterError;
        }
    }

    /**
     * Convert a colon-separated tuple into ip address and port
    */
    static inline const outcome::result<std::pair<ip::address, int>> string_tuple_to_ip_and_port(const std::string &t) {
        std::vector<std::string> ipPort;

        boost::algorithm::split(ipPort, t, boost::is_any_of(":"));
        if (ipPort.size() == 2) {
            outcome::result<ip::address> r1 = string_to_ip(ipPort[0]);
            outcome::result<int> r2 = string_to_port(ipPort[1]);
            if (r1 && r2) 
                return std::pair<ip::address, int> (r1.value(), r2.value());
            else
                return E2SARErrorc::ParameterError;
        } else if (ipPort.size() == 1) {
            outcome::result<ip::address> r1 = string_to_ip(ipPort[0]);
            if (r1)
                return std::pair<ip::address, int> (r1.value(), 0);
            else 
                return E2SARErrorc::ParameterError;
        }
        else
            return E2SARErrorc::ParameterError;
    }

        /**
     * Function to take a host name and turn it into IP addresses, IPv4 and IPv6.
     *
     * @param host_name name of host to examine.
     * @return outcome variable with either has_error() set or value() returning a list of ip::address
     */
    static inline outcome::result<std::vector<ip::address>> resolveHost(const std::string& host_name) noexcept {

        std::vector<ip::address> addresses;
        boost::asio::io_service io_service;

        try {
            ip::udp::resolver resolver(io_service);
            ip::udp::resolver::query query(host_name, "443");
            ip::udp::resolver::iterator iter = resolver.resolve(query);
            ip::udp::resolver::iterator end; // End marker.

            while (iter != end) {
                ip::udp::endpoint endpoint = *iter++;
                addresses.push_back(endpoint.address());
            }
            return addresses;
        } catch (...) {
            // anything happens - we can't find the host
            return E2SARErrorc::NotFound;
        }
    }
};
#endif