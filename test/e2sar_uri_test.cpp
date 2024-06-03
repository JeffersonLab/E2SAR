#define BOOST_TEST_MODULE MyTestSuite
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
std::string uri_string5{"ejfat://token@192.188.29.6:18020/lb/36?data=192.188.29.20"};
std::string uri_string6{"ejfat://192.188.29.6:18020/lb/36?sync=192.188.29.6:19020"};

BOOST_AUTO_TEST_SUITE(E2SARTestSuite)

BOOST_AUTO_TEST_CASE(URITest1) {

    BOOST_REQUIRE_NO_THROW(EjfatURI euri(uri_string1));
}

BOOST_AUTO_TEST_CASE(URITest2) {

    EjfatURI euri(uri_string1);
    std::cout << uri_string1 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(!euri.get_AdminToken().has_error());
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddr().value().first == ip::make_address("192.188.29.20"));
    BOOST_TEST(euri.get_dataAddr().value().second == DATAPLANE_PORT);
    BOOST_TEST(euri.get_syncAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_syncAddr().value().second == 19020);
}

BOOST_AUTO_TEST_CASE(URITest2_1) {
    // various URI options

    EjfatURI euri(uri_string3);
    std::cout << uri_string3 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_AdminToken().value() == "token");
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddr().has_error());
    BOOST_TEST(euri.get_syncAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_syncAddr().value().second == 19020);
}

BOOST_AUTO_TEST_CASE(URITest2_2) {
    // various URI options

    EjfatURI euri(uri_string4);
    std::cout << uri_string4 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddr().has_error());
    BOOST_TEST(euri.get_syncAddr().has_error());
}

BOOST_AUTO_TEST_CASE(URITest2_3) {
    // various URI options

    EjfatURI euri(uri_string5);
    std::cout << uri_string5 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_syncAddr().has_error());
    BOOST_TEST(euri.get_dataAddr().value().first == ip::make_address("192.188.29.20"));
    BOOST_TEST(euri.get_dataAddr().value().second == DATAPLANE_PORT);
}

BOOST_AUTO_TEST_CASE(URITest2_4) {
    // various URI options

    EjfatURI euri(uri_string6);
    std::cout << uri_string6 << " vs " << static_cast<std::string>(euri) << std::endl;
    BOOST_TEST(euri.get_AdminToken().has_error());
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddr().has_error());
    BOOST_TEST(euri.get_syncAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_syncAddr().value().second == 19020);
}

BOOST_AUTO_TEST_CASE(URITest3) {
    BOOST_CHECK_THROW(EjfatURI euri(uri_string2), E2SARException);
}

BOOST_AUTO_TEST_CASE(URITest4) {
    // set env variable and read from it
    std::string sv{"EJFAT_URI=ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"}; 
    putenv(sv.data()); 

    outcome::result<EjfatURI> euri = EjfatURI::getFromEnv();

    std::cout << static_cast<std::string>(euri.value()) << std::endl;
}

BOOST_AUTO_TEST_CASE(URITest5) {
    // set env variable with different name and read from it
    std::string sv{"EJFAT_URI_NEW=ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"}; 
    putenv(sv.data()); 

    // try old name and fail
    outcome::result<EjfatURI> euri = EjfatURI::getFromEnv();

    BOOST_TEST(euri.error() == E2SARErrorc::Undefined);

    euri = EjfatURI::getFromEnv("EJFAT_URI_NEW"s);

    // try new name
    BOOST_TEST(euri.has_error() == false);

    std::cout << static_cast<std::string>(euri.value()) << std::endl;
}

BOOST_AUTO_TEST_CASE(URITest6) {

    // test name resolution
    outcome::result<std::vector<ip::address>> addresses = resolveHost("www.jlab.org"s);

    BOOST_TEST(addresses.has_error() == false);

    for (auto addr: addresses.value()) {
        std::cout << "Address is " << addr << std::endl;
    }
}

BOOST_AUTO_TEST_CASE(URITest7) {

    // test name resolution
    outcome::result<std::vector<ip::address>> addresses = resolveHost("fake.jlab.org"s);

    BOOST_TEST(addresses.has_error() == true);
}





BOOST_AUTO_TEST_SUITE_END()