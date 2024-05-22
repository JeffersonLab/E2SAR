#include <string>
#include <regex>

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
     * @param uriInfo ref to ejfatURI struct to fill with parsed values.
     * @return true if parse successful, else false.
     */
    EjfatURI(const std::string_view &uri) {

        // URI must match this regex pattern
        // Note: the pattern (\[?[a-fA-F\d:.]+\]?) matches either IPv6 or IPv4 addresses
        // in which the addr may be surrounded by [] and thus is stripped off.
        std::regex pattern(R"regex(ejfat://(?:([^@]+)@)?(\[?[a-fA-F\d:.]+\]?):(\d+)/lb/([^?]+)(?:\?(?:(?:data=(\[?[a-fA-F\d:.]+\]?):(\d+)){1}(?:&sync=(\[?[a-fA-F\d:.]+\]?):(\d+))?|(?:sync=(\[?[a-fA-F\d:.]+\]?):(\d+)){1}))?)regex");

        std::smatch match;
        if (std::regex_match(uri, match, pattern)) {
            // we're here if uri is in the proper format ...

            // optional token
            std::string token = match[1];

            if (!token.empty()) {
                uriInfo.instanceToken = token;
                uriInfo.haveInstanceToken = true;
            }
            else {
                uriInfo.haveInstanceToken = false;
            }

            // Remove square brackets from address if present
            std::string addr = match[2];
            if (!addr.empty() && addr.front() == '[' && addr.back() == ']') {
                addr = addr.substr(1, addr.size() - 2);
            }

            uriInfo.cpAddr = addr;
            uriInfo.cpPort = std::stoi(match[3]);
            uriInfo.lbId   = match[4];

            if (isIPv6(addr)) {
                uriInfo.useIPv6Cp = true;
            }

                // in this case only syncAddr and syncPort defined
            if (!match[9].str().empty()) {
                uriInfo.haveSync = true;
                uriInfo.haveData = false;

                // Remove square brackets if present
                std::string addr = match[9];
                if (!addr.empty() && addr.front() == '[' && addr.back() == ']') {
                    addr = addr.substr(1, addr.size() - 2);
                }

                // decide if this is IPv4 or IPv6 or neither
                if (isIPv6(addr)) {
                    uriInfo.syncAddrV6  = addr;
                    uriInfo.useIPv6Sync = true;
                }
                else if (isIPv4(addr)) {
                    uriInfo.syncAddrV4 = addr;
                }
                else {
                    // invalid IP addr
                    uriInfo.haveSync = false;
                }

                try {
                    // look at the sync port
                    int port = std::stoi(match[10]);
                    if (port < 1024 || port > 65535) {
                        // port is out of range
                        uriInfo.haveSync = false;
                    }
                    else {
                        uriInfo.syncPort = port;
                    }

                } catch (const std::exception& e) {
                    // string is not a valid integer
                    uriInfo.haveSync = false;
                }
            }
            else {
                // if dataAddr and dataPort defined
                if (!match[5].str().empty()) {
                    uriInfo.haveData = true;

                    std::string addr = match[5];
                    if (!addr.empty() && addr.front() == '[' && addr.back() == ']') {
                        addr = addr.substr(1, addr.size() - 2);
                    }

                    if (isIPv6(addr)) {
                        uriInfo.dataAddrV6  = addr;
                        uriInfo.useIPv6Data = true;
                    }
                    else if (isIPv4(addr)) {
                        uriInfo.dataAddrV4 = addr;
                    }
                    else {
                        uriInfo.haveData = false;
                    }

                    try {
                        // look at the data port
                        int port = std::stoi(match[6]);
                        if (port < 1024 || port > 65535) {
                            // port is out of range
                            uriInfo.haveData = false;
                        }
                        else {
                            uriInfo.dataPort = port;
                        }

                    } catch (const std::exception& e) {
                        // string is not a valid integer
                        uriInfo.haveData = false;
                    }

                }
                else {
                    uriInfo.haveData = false;
                }

                // if syncAddr and syncPort defined
                if (!match[7].str().empty()) {
                    uriInfo.haveSync = true;

                    std::string addr = match[7];
                    if (!addr.empty() && addr.front() == '[' && addr.back() == ']') {
                        addr = addr.substr(1, addr.size() - 2);
                    }

                    // decide if this is IPv4 or IPv6 or neither
                    if (isIPv6(addr)) {
                        uriInfo.syncAddrV6  = addr;
                        uriInfo.useIPv6Sync = true;
                    }
                    else if (isIPv4(addr)) {
                        uriInfo.syncAddrV4 = addr;
                    }
                    else {
                        uriInfo.haveSync = false;
                    }

                    try {
                        // look at the sync port
                        int port = std::stoi(match[8]);
                        if (port < 1024 || port > 65535) {
                            // port is out of range
                            uriInfo.haveSync = false;
                        }
                        else {
                            uriInfo.syncPort = port;
                        }

                    } catch (const std::exception& e) {
                        // string is not a valid integer
                        uriInfo.haveSync = false;
                    }
                }
                else {
                    uriInfo.haveSync = false;
                }
            }
            return true;
        }

        return false;
    }
}