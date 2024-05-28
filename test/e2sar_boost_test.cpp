#define BOOST_TEST_MODULE MyTestSuite
#include <stdlib.h>
#include <boost/asio.hpp>
#include <boost/test/included/unit_test.hpp>

#include "e2sar.hpp"

using namespace e2sar;

std::string uri_string1{"ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};
std::string uri_string2{"ejfact://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"};

BOOST_AUTO_TEST_SUITE(E2SARTestSuite)

BOOST_AUTO_TEST_CASE(Test1) {

    BOOST_REQUIRE_NO_THROW(EjfatURI euri(uri_string1));
}

BOOST_AUTO_TEST_CASE(Test2) {

    EjfatURI euri(uri_string1);
    BOOST_TEST(euri.get_lbId() == "36");
    BOOST_TEST(euri.get_cpAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_cpAddr().value().second == 18020);
    BOOST_TEST(euri.get_dataAddr().value().first == ip::make_address("192.188.29.20"));
    BOOST_TEST(euri.get_dataAddr().value().second == DATAPLANE_PORT);
    BOOST_TEST(euri.get_syncAddr().value().first == ip::make_address("192.188.29.6"));
    BOOST_TEST(euri.get_syncAddr().value().second == 19020);
}

BOOST_AUTO_TEST_CASE(Test3) {
    BOOST_CHECK_THROW(EjfatURI euri(uri_string2), E2SARException);
}

BOOST_AUTO_TEST_CASE(Test4) {
    // set env variable and read from it
    std::string sv{"EJFAT_URI=ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"}; 
    putenv(sv.data()); 

    outcome::result<EjfatURI> euri = EjfatURI::getFromEnv();

    std::cout << static_cast<std::string>(euri.value()) << std::endl;
}

BOOST_AUTO_TEST_CASE(Test5) {
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
BOOST_AUTO_TEST_SUITE_END()