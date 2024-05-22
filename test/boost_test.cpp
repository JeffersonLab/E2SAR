#include <iostream>
#include <boost/asio.hpp>

using namespace boost::asio;

int main() {

    // test boost IP addresses
    ip::address ipv4 = ip::make_address("192.168.1.1");
    ip::address ipv6 = ip::make_address("2001:db8:0000:1:1:1:1:1");

    std::cout << "IPv4 " << ipv4 << ' ' << ipv4.is_v4() << '\n';
    std::cout << "IPv6 " << ipv6 << ' ' << ipv6.is_v6() << '\n';

    // test name resolution
    boost::asio::io_service io_service;

    ip::udp::resolver resolver(io_service);
    ip::udp::resolver::query query("www.renci.org", "445");
    ip::udp::resolver::iterator iter = resolver.resolve(query);
    ip::udp::resolver::iterator end; // End marker.
    while (iter != end)
    {
        ip::udp::endpoint endpoint = *iter++;
        ip::address address = endpoint.address();
        std::cout << address << ' ' << address.is_v4() << ' ' << std::endl;
    }
}