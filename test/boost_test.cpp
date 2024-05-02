#include <iostream>
#include <boost/asio.hpp>

using namespace boost::asio;

int main() {

    ip::address ipv4 = ip::make_address("192.168.1.1");
    ip::address ipv6 = ip::make_address("2001:db8:0000:1:1:1:1:1");

    std::cout << "IPv4 " << ipv4 << '\n';
    std::cout << "IPv6 " << ipv6 << '\n';
}
