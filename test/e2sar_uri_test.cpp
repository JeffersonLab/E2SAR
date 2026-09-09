#define BOOST_TEST_MODULE URITests
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/test/included/unit_test.hpp>

#include "e2sar.hpp"

using namespace e2sar;

std::string uri_string1{"ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};
std::string uri_string2{"ejfact://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};

std::string uri_string3{"ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020"};
std::string uri_string4{"ejfat://token@192.188.29.6:18020/lb/36"};
std::string uri_string4_1{"ejfat://token@192.188.29.6:18020/"};
std::string uri_string4_2{"ejfat://token@192.188.29.6:18020"};
std::string uri_string4_3{"ejfat://token@192.188.29.6:18020/?sync=192.188.29.6:19020"};
std::string uri_string5{"ejfat://token@192.188.29.6:18020/lb/36?data=192.188.29.20"};
std::string uri_string6{"ejfat://192.188.29.6:18020/lb/36?sync=192.188.29.6:19020"};

// IPv6
std::string uri_string7{"ejfat://[2001:4860:0:2001::68]:18020/lb/36?data=[2001:4860:0:2021::68]&sync=[2001:4860:0:2031::68]:19020"};

// with TLS
std::string uri_string8{"ejfats://192.188.29.6:18020/lb/36?sync=192.188.29.6:19020"};

// with TLS and hostname
std::string uri_string9{"ejfats://ejfat-lb.es.net:18020/lb/36?sync=192.188.29.6:19020"};

// with session id
std::string uri_string10{"ejfats://ejfat-lb.es.net:18020/lb/36?sync=192.188.29.6:19020&sessionid=mysessionid"};

// with custom data port
std::string uri_string11{"ejfat://192.188.29.6:18020/lb/36?data=192.188.29.6:19020"};

// IPv6 and custom data port
std::string uri_string12{"ejfats://89f9afdb6972597@ejfat-lb.es.net:18008/lb/17?sync=192.188.29.6:19010&data=192.188.29.10&data=[2001:400:a300::10]:10000"};

BOOST_AUTO_TEST_SUITE(URITestSuite)

BOOST_AUTO_TEST_CASE(URITest1)
{

    BOOST_REQUIRE_NO_THROW(EjfatURI euri(uri_string1));
}

BOOST_AUTO_TEST_CASE(URITest1_1)
{

    BOOST_REQUIRE_NO_THROW(EjfatURI euri(uri_string7));
}

BOOST_AUTO_TEST_CASE(URITest2)
{

    EjfatURI euri(uri_string1);
    std::cout << uri_string1 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(!euri.get_AdminToken().has_error());
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddrv4().value().first == ip::make_address("192.188.29.20"));
    BOOST_CHECK(euri.get_dataAddrv4().value().second == std::make_pair(DATAPLANE_PORT_MIN, DATAPLANE_PORT_MAX));
    BOOST_TEST(euri.get_syncAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_syncAddr().value().second == 19020);
}

BOOST_AUTO_TEST_CASE(URITest2_1)
{
    // various URI options

    EjfatURI euri(uri_string3);
    std::cout << uri_string3 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_AdminToken().value() == "token");
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddrv4().has_error());
    BOOST_TEST(euri.get_syncAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_syncAddr().value().second == 19020);
}

BOOST_AUTO_TEST_CASE(URITest2_2)
{
    // various URI options

    EjfatURI euri(uri_string4);
    std::cout << uri_string4 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddrv4().has_error());
    BOOST_TEST(euri.get_syncAddr().has_error());
}

BOOST_AUTO_TEST_CASE(URITest2_3)
{
    // various URI options

    EjfatURI euri(uri_string5);
    std::cout << uri_string5 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_syncAddr().has_error());
    BOOST_TEST(euri.get_dataAddrv4().value().first == ip::make_address("192.188.29.20"));
    BOOST_CHECK(euri.get_dataAddrv4().value().second == std::make_pair(DATAPLANE_PORT_MIN, DATAPLANE_PORT_MAX));
}

BOOST_AUTO_TEST_CASE(URITest2_4)
{
    // various URI options

    EjfatURI euri(uri_string6);
    std::cout << uri_string6 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_AdminToken().has_error());
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddrv4().has_error());
    BOOST_TEST(euri.get_syncAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_syncAddr().value().second == 19020);
}

BOOST_AUTO_TEST_CASE(URITest2_5)
{
    // various URI options

    EjfatURI euri(uri_string4_1);
    std::cout << uri_string4_1 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddrv4().has_error());
    BOOST_TEST(euri.get_syncAddr().has_error());
    BOOST_TEST(euri.get_lbId().empty());
}

BOOST_AUTO_TEST_CASE(URITest2_6)
{
    // various URI options

    EjfatURI euri(uri_string4_2);
    std::cout << uri_string4_2 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddrv4().has_error());
    BOOST_TEST(euri.get_syncAddr().has_error());
    BOOST_TEST(euri.get_lbId().empty());
}

BOOST_AUTO_TEST_CASE(URITest2_7)
{
    // various URI options

    EjfatURI euri(uri_string4_3);
    std::cout << uri_string4_3 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddrv4().has_error());
    BOOST_TEST(euri.get_lbId().empty());
}

BOOST_AUTO_TEST_CASE(URITest3)
{
    BOOST_CHECK_THROW(EjfatURI euri(uri_string2), E2SARException);
}

BOOST_AUTO_TEST_CASE(URITest4)
{
    // set env variable and read from it
    std::string sv{"EJFAT_URI=ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};
    putenv(sv.data());

    auto euri = EjfatURI::getFromEnv();

    std::cout << static_cast<std::string>(euri.value()) << std::endl;
}

BOOST_AUTO_TEST_CASE(URITest5)
{
    // set env variable with different name and read from it
    std::string sv{"EJFAT_URI_NEW=ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};
    putenv(sv.data());

    // try old name and fail
    auto euri = EjfatURI::getFromEnv();

    BOOST_TEST(euri.error().code() == E2SARErrorc::Undefined);

    euri = EjfatURI::getFromEnv("EJFAT_URI_NEW"s);

    // try new name
    BOOST_TEST(euri.has_error() == false);

    std::cout << static_cast<std::string>(euri.value()) << std::endl;
}

BOOST_AUTO_TEST_CASE(URITest6)
{

    // test name resolution
    auto addresses = resolveHost("www.jlab.org"s);

    BOOST_TEST(addresses.has_error() == false);

    for (auto addr : addresses.value())
    {
        std::cout << "Address is " << addr << std::endl;
    }
}

BOOST_AUTO_TEST_CASE(URITest7)
{

    // test name resolution
    auto addresses = resolveHost("fake.jlab.org"s);

    BOOST_TEST(addresses.has_error() == true);
}

BOOST_AUTO_TEST_CASE(URITest8)
{

    EjfatURI euri(uri_string7);

    std::cout << static_cast<std::string>(euri) << std::endl;
    BOOST_CHECK(euri.has_dataAddrv4() == false);
    BOOST_CHECK(euri.has_dataAddrv6() == true);
    BOOST_CHECK(euri.has_syncAddr());
    BOOST_CHECK(euri.get_dataAddrv6().value().first == ip::make_address("2001:4860:0:2021::68"));
    BOOST_CHECK(euri.get_dataAddrv6().value().first == ip::make_address("2001:4860:0:2021::68"));
    BOOST_CHECK(euri.get_syncAddr().value().first == ip::make_address("2001:4860:0:2031::68"));
}

BOOST_AUTO_TEST_CASE(URITest9)
{
    EjfatURI euri(uri_string8);

    std::cout << static_cast<std::string>(euri) << std::endl;

    BOOST_CHECK(euri.get_useTls());
}

BOOST_AUTO_TEST_CASE(URITest10)
{
    EjfatURI euri(uri_string9);

    std::cout << static_cast<std::string>(euri) << " " << euri.get_cpAddr().value().first << std::endl;

    BOOST_CHECK(euri.get_useTls());
    BOOST_CHECK(euri.get_cpHost().value().first == "ejfat-lb.es.net"s);
    BOOST_CHECK(euri.get_cpAddr().value().first.is_v4());
}

BOOST_AUTO_TEST_CASE(URITest11)
{
    try 
    {
        EjfatURI euri(uri_string9, EjfatURI::TokenType::admin, true);

        std::cout << static_cast<std::string>(euri) << " " << euri.get_cpAddr().value().first << std::endl;

        BOOST_CHECK(euri.get_useTls());
        BOOST_CHECK(euri.get_cpHost().value().first == "ejfat-lb.es.net"s);
        BOOST_CHECK(euri.get_cpAddr().value().first.is_v6());
    } catch(E2SARException &e) {
        std::cout << "Exception " << static_cast<std::string>(e)  << std::endl;
        std::cout << "Probably the host doesn't resolve to IPv6 from where you are running this test" << std::endl;
    }
}

BOOST_AUTO_TEST_CASE(URITest12)
{
    EjfatURI euri(uri_string10);

    std::cout << static_cast<std::string>(euri) << " " << euri.get_cpAddr().value().first << std::endl;

    BOOST_CHECK(euri.get_sessionId() == "mysessionid"s);
}


BOOST_AUTO_TEST_CASE(PortRangeTest)
{
    int portRange{12};
    size_t numPorts{static_cast<size_t>(2 << (portRange - 1))};

    std::cout << "Port range is " << get_PortRange(numPorts) << std::endl;

    BOOST_CHECK(get_PortRange(numPorts) == portRange);
}

BOOST_AUTO_TEST_CASE(URITest13)
{
    EjfatURI euri(uri_string11);

    std::cout << static_cast<std::string>(euri) << " Dataplane address with custom port:" << euri.get_dataAddrv4().value().first <<
        ":" << euri.get_dataAddrv4().value().second.first << std::endl;

    BOOST_CHECK(euri.get_dataAddrv4().value().second == std::make_pair((u_int16_t)19020, (u_int16_t)19020));
}

BOOST_AUTO_TEST_CASE(URITest14)
{
    EjfatURI euri(uri_string12);

    std::cout << static_cast<std::string>(euri) << "Dataplane address with custom port v6: " << euri.get_dataAddrv6().value().first <<
        ":" << euri.get_dataAddrv6().value().second.first << " v4: " << euri.get_dataAddrv4().value().first <<
        ":" << euri.get_dataAddrv4().value().second.first << std::endl;

    BOOST_CHECK(euri.get_dataAddrv6().value().first == ip::make_address("2001:400:a300::10"));
    BOOST_CHECK(euri.get_dataAddrv6().value().second == std::make_pair((u_int16_t)10000, (u_int16_t)10000));
    BOOST_CHECK(euri.get_dataAddrv4().value().first == ip::make_address("192.188.29.10"));
    BOOST_CHECK(euri.get_dataAddrv4().value().second == std::make_pair((u_int16_t)10000, (u_int16_t)10000));
}

// dual sync: one IPv4, one IPv6 sync address
std::string uri_dual_sync{"ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&sync=[2001:4860::1]:19021"};

BOOST_AUTO_TEST_CASE(URITest_DualSync)
{
    EjfatURI euri(uri_dual_sync);

    BOOST_CHECK(euri.has_syncAddrv4());
    BOOST_CHECK(euri.has_syncAddrv6());
    BOOST_CHECK(euri.get_syncAddrv4().value().first == ip::make_address("192.188.29.6"));
    BOOST_CHECK(euri.get_syncAddrv4().value().second == 19020);
    BOOST_CHECK(euri.get_syncAddrv6().value().first == ip::make_address("2001:4860::1"));
    BOOST_CHECK(euri.get_syncAddrv6().value().second == 19021);

    // get_syncAddr() with preferV6=false (default) picks v4
    BOOST_CHECK(euri.get_syncAddr().value().first == ip::make_address("192.188.29.6"));

    // get_syncAddr() with preferV6=true picks v6
    EjfatURI euri6(uri_dual_sync, EjfatURI::TokenType::admin, true);
    BOOST_CHECK(euri6.get_syncAddr().value().first == ip::make_address("2001:4860::1"));
}

// data port range: min-max
std::string uri_data_range{"ejfat://token@192.188.29.6:18020/lb/36?data=1.2.3.4:1234-5678"};

BOOST_AUTO_TEST_CASE(URITest_DataPortRange)
{
    EjfatURI euri(uri_data_range);

    BOOST_CHECK(!euri.get_dataPortRange().has_error());
    BOOST_CHECK(euri.get_dataPortRange().value().first == 1234);
    BOOST_CHECK(euri.get_dataPortRange().value().second == 5678);
    BOOST_CHECK(euri.get_dataAddrv4().value().second == std::make_pair((u_int16_t)1234, (u_int16_t)5678));
}

// data single port: treated as range (port, port)
std::string uri_data_single{"ejfat://token@192.188.29.6:18020/lb/36?data=1.2.3.4:1234"};

BOOST_AUTO_TEST_CASE(URITest_DataSinglePort)
{
    EjfatURI euri(uri_data_single);

    BOOST_CHECK(euri.get_dataPortRange().value().first == 1234);
    BOOST_CHECK(euri.get_dataPortRange().value().second == 1234);
}

// data default port range: no port specified -> 16384-32767
std::string uri_data_no_port{"ejfat://token@192.188.29.6:18020/lb/36?data=1.2.3.4"};

BOOST_AUTO_TEST_CASE(URITest_DataDefaultRange)
{
    EjfatURI euri(uri_data_no_port);

    BOOST_CHECK(euri.get_dataPortRange().value().first == DATAPLANE_PORT_MIN);
    BOOST_CHECK(euri.get_dataPortRange().value().second == DATAPLANE_PORT_MAX);
}

// full new-format URI: dual sync + dual data + port range
std::string uri_full_new{"ejfat://token@192.188.29.6:18020/lb/99?sync=10.0.0.1:19000&sync=[2001:db8::1]:19001&data=10.0.0.2:16384-32767&data=[2001:db8::2]:16384-32767"};

BOOST_AUTO_TEST_CASE(URITest_FullNewFormat)
{
    EjfatURI euri(uri_full_new);

    BOOST_CHECK(euri.has_syncAddrv4());
    BOOST_CHECK(euri.has_syncAddrv6());
    BOOST_CHECK(euri.has_dataAddrv4());
    BOOST_CHECK(euri.has_dataAddrv6());
    BOOST_CHECK(euri.get_syncAddrv4().value().first == ip::make_address("10.0.0.1"));
    BOOST_CHECK(euri.get_syncAddrv4().value().second == 19000);
    BOOST_CHECK(euri.get_syncAddrv6().value().first == ip::make_address("2001:db8::1"));
    BOOST_CHECK(euri.get_syncAddrv6().value().second == 19001);
    BOOST_CHECK(euri.get_dataAddrv4().value().first == ip::make_address("10.0.0.2"));
    BOOST_CHECK(euri.get_dataPortRange().value().first == 16384);
    BOOST_CHECK(euri.get_dataPortRange().value().second == 32767);
    BOOST_CHECK(euri.get_lbId() == "99");
}

// round-trip: parse -> to_string -> parse -> operator== must hold
BOOST_AUTO_TEST_CASE(URITest_RoundTrip)
{
    EjfatURI euri1(uri_full_new);
    std::string serialized = static_cast<std::string>(euri1);
    std::cout << "Round-trip: " << serialized << std::endl;
    EjfatURI euri2(serialized);
    BOOST_CHECK(euri1 == euri2);
}

BOOST_AUTO_TEST_SUITE_END()