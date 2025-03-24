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
        " and the MTU is " << res.value().get<1>() << std::endl;
    #else
    std::cout << "Skipping test for getting outgoing interface and MTU - platform not supported" << std::endl;
    #endif
}

BOOST_AUTO_TEST_SUITE_END()