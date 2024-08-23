#ifndef E2SARDPNETUTILHPP
#define E2SARDPNETUTILHPP

#ifdef NETLINK_CAPABLE
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_io.hpp>

#include "e2sarError.hpp"

using namespace boost::asio;

namespace e2sar
{
    /*
        Network utilities class with static functions
    */
    class NetUtil
    {
    public:
        /**
         * Get MTU of a given interface
         * @param interfaceName - name of the interface
         * @return MTU or 1500 as the best guess
         */
        static u_int16_t getMTU(const std::string &interfaceName);
#ifdef NETLINK_CAPABLE
        /**
         * Get the outgoing interface and its MTU for a given IPv4 or IPv6
         * @param ipaddr - destination IP address string
         * @return a tuple of interface name and its MTU
         */
        static inline result<boost::tuple<std::string, u_int16_t>> getInterfaceAndMTU(const std::string &ipaddr) 
        {
            return getInterfaceAndMTU(ip::make_address(ipaddr));
        }
        /**
         * Get the outgoing interface and its MTU for a given IPv4 or IPv6
         * @param addr - destination IP address object 
         * @return a tuple of interface name and its MTU
         */
        static result<boost::tuple<std::string, u_int16_t>> getInterfaceAndMTU(const ip::address &addr);

#endif
    };
}
#endif
