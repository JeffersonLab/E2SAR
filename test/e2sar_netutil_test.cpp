#define BOOST_TEST_MODULE NetUtilTests
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/test/included/unit_test.hpp>

#include "e2sar.hpp"

using namespace e2sar;

BOOST_AUTO_TEST_SUITE(NetUtilTestSuite)

BOOST_AUTO_TEST_CASE(NetUtilTest1)
{
    // get MTU for looopback
    std::string intname{"lo"};
    auto mtu = NetUtil::getMTU(intname);

    std::cout << "MTU of " << intname << " is " << mtu << std::endl;
}

BOOST_AUTO_TEST_CASE(NetUtilTest2)
{
    // test getting hostname
    auto res = NetUtil::getHostName();

    BOOST_CHECK(!res.has_error());
    std::cout << "Hostname is " << res.value() << std::endl;
}

BOOST_AUTO_TEST_CASE(NetUtilTest3)
{
    #ifdef NETLINK_CAPABLE
    // test getting outgoing interface and MTU for a given destination
    std::string destination{"8.8.8.8"};
    auto res = NetUtil::getInterfaceAndMTU(ip::make_address(destination));

    BOOST_CHECK(!res.has_error());

    std::cout << "Outgoing interface to reach " << destination << " is " << res.value().get<0>() << 
        " and the MTU is " << res.value().get<1>() << " and src address is " << res.value().get<2>() << std::endl;

    BOOST_CHECK(res.value().get<1>() > 0);
    BOOST_CHECK(!res.value().get<2>().is_unspecified());
    #else
    std::cout << "Skipping test for getting outgoing interface and MTU - platform not supported" << std::endl;
    #endif
}

BOOST_AUTO_TEST_CASE(IsNonRoutableIPv4)
{
    // RFC1918
    BOOST_CHECK(NetUtil::isNonRoutable("10.0.0.1"));
    BOOST_CHECK(NetUtil::isNonRoutable("10.255.255.255"));
    BOOST_CHECK(NetUtil::isNonRoutable("172.16.0.1"));
    BOOST_CHECK(NetUtil::isNonRoutable("172.31.255.255"));
    BOOST_CHECK(NetUtil::isNonRoutable("192.168.0.1"));
    BOOST_CHECK(NetUtil::isNonRoutable("192.168.255.255"));

    // loopback and unspecified
    BOOST_CHECK(NetUtil::isNonRoutable("127.0.0.1"));
    BOOST_CHECK(NetUtil::isNonRoutable("0.0.0.0"));

    // link-local
    BOOST_CHECK(NetUtil::isNonRoutable("169.254.1.1"));

    // routable addresses should return false
    BOOST_CHECK(!NetUtil::isNonRoutable("8.8.8.8"));
    BOOST_CHECK(!NetUtil::isNonRoutable("1.1.1.1"));
    BOOST_CHECK(!NetUtil::isNonRoutable("198.51.100.1"));
    BOOST_CHECK(!NetUtil::isNonRoutable("172.15.255.255"));
    BOOST_CHECK(!NetUtil::isNonRoutable("172.32.0.0"));

    // invalid strings return false
    BOOST_CHECK(!NetUtil::isNonRoutable("not-an-ip"));
    BOOST_CHECK(!NetUtil::isNonRoutable(""));
}

BOOST_AUTO_TEST_CASE(IsNonRoutableIPv6)
{
    // loopback and unspecified
    BOOST_CHECK(NetUtil::isNonRoutable("::1"));
    BOOST_CHECK(NetUtil::isNonRoutable("::"));

    // ULA (fc00::/7)
    BOOST_CHECK(NetUtil::isNonRoutable("fc00::1"));
    BOOST_CHECK(NetUtil::isNonRoutable("fd12:3456:789a::1"));

    // link-local (fe80::/10)
    BOOST_CHECK(NetUtil::isNonRoutable("fe80::1"));

    // v4-mapped IPv6 with private v4
    BOOST_CHECK(NetUtil::isNonRoutable("::ffff:192.168.1.1"));
    BOOST_CHECK(NetUtil::isNonRoutable("::ffff:10.0.0.1"));

    // routable v6
    BOOST_CHECK(!NetUtil::isNonRoutable("2001:db8::1"));
    BOOST_CHECK(!NetUtil::isNonRoutable("2607:f8b0:4004:800::200e"));

    // v4-mapped IPv6 with routable v4
    BOOST_CHECK(!NetUtil::isNonRoutable("::ffff:8.8.8.8"));
}

BOOST_AUTO_TEST_CASE(IsNonRoutableVector)
{
    // empty vector
    BOOST_CHECK(!NetUtil::isNonRoutable(std::vector<std::string>{}));

    // all routable
    BOOST_CHECK(!NetUtil::isNonRoutable(std::vector<std::string>{"8.8.8.8", "1.1.1.1"}));

    // one non-routable among routable
    BOOST_CHECK(NetUtil::isNonRoutable(std::vector<std::string>{"8.8.8.8", "192.168.1.1"}));

    // all non-routable
    BOOST_CHECK(NetUtil::isNonRoutable(std::vector<std::string>{"10.0.0.1", "172.16.0.1"}));
}

BOOST_AUTO_TEST_SUITE_END()