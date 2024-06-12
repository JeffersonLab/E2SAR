#define BOOST_TEST_MODULE MyTestSuite
#include <stdlib.h>
#include <iostream>
#include <boost/asio.hpp>
#include <vector>
#include <boost/test/included/unit_test.hpp>
#include <boost/program_options.hpp>

#include "e2sar.hpp"

using namespace e2sar;

namespace po = boost::program_options;
namespace pt = boost::posix_time;

BOOST_AUTO_TEST_SUITE(E2SARTestSuite)

// these tests necessarily depend on EJFAT_URI environment variable setting
// as information about live LBCP can't be baked into the test itself
BOOST_AUTO_TEST_CASE(LBMLiveTest1)
{
    // reserve then free
    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();

    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> senders {"192.168.100.1"s, "192.168.100.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    // call free - this will correctly use the admin token (even though instance token
    // is added by reserve call and updated URI inside with LB ID added to it
    res = lbman.freeLB();

    BOOST_CHECK(!res.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest2)
{
    // reseserve, get, then free

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> senders {"192.168.100.1"s, "192.168.100.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    // call get on a new uri object so we can compare
    uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri1 = uri_r.value();

    auto lbman1 = LBManager(uri1);

    // get lb ID from other URI object
    res = lbman1.getLB(lbman.get_URI().get_lbId());

    BOOST_CHECK(!res.has_error());

    BOOST_CHECK(lbman.get_URI() == lbman1.get_URI());

    res = lbman.freeLB();

    BOOST_CHECK(!res.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest3)
{
    // reserve, register worker, unregister worker, free
}

BOOST_AUTO_TEST_CASE(LBMLiveTest4)
{
    // reserve, register worker, get status, unregister worker, get status, free
    
}

BOOST_AUTO_TEST_SUITE_END()