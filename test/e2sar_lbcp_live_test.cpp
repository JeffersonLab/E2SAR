#define BOOST_TEST_MODULE CPLiveTests
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

BOOST_AUTO_TEST_SUITE(CPLiveTestSuite)

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
    // reserve, register worker, send state, unregister worker, free

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

    // register (should internally set session token and session id)
    res = lbman.registerWorker("my_node"s, std::pair<ip::address, u_int16_t>(ip::make_address("192.168.101.5"), 10000), 0.5, 10);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_SessionToken().value().empty());
    BOOST_CHECK(!lbman.get_URI().get_sessionId().empty());

    // send state - every registered worker must do that every 100ms or be auto-deregistered
    res = lbman.sendState(0.8, 1.0, true);

    BOOST_CHECK(!res.has_error());

    // unregister (should use session token and session id)
    res = lbman.deregisterWorker();

    BOOST_CHECK(!res.has_error());

    // free
    res = lbman.freeLB();

    BOOST_CHECK(!res.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest4)
{
    // reserve, register worker, get status, unregister worker, get status, free

        // reserve, register worker, unregister worker, free

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

    // register (should internally set session token and session id)
    res = lbman.registerWorker("my_node"s, std::pair<ip::address, u_int16_t>(ip::make_address("192.168.101.5"), 10000), 0.5, 10);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_SessionToken().value().empty());
    BOOST_CHECK(!lbman.get_URI().get_sessionId().empty());

    // send state - every registered worker must do that every 100ms or be auto-deregistered
    res = lbman.sendState(0.8, 1.0, true);

    BOOST_CHECK(!res.has_error());

    // get LB status
    auto status_res = lbman.getLBStatus();

    BOOST_CHECK(!status_res.has_error());
    auto saddrs = LBManager::get_SenderAddressVector(status_res.value());
    BOOST_CHECK(saddrs.size() == 1);
    BOOST_CHECK(saddrs[0] == "192.168.101.5"s);
    auto workers = LBManager::get_WorkerStatusVector(status_res.value());
    BOOST_CHECK(workers.size() == 1);
    BOOST_CHECK(workers[0].name() == "my_node"s);
    BOOST_CHECK(workers[0].fillpercent() == 0.5);
    std::cout << "Last Updated " << workers[0].lastupdated() << std::endl;

    // unregister (should use session token and session id)
    res = lbman.deregisterWorker();

    BOOST_CHECK(!res.has_error());

    // free
    res = lbman.freeLB();

    BOOST_CHECK(!res.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest5)
{
    // version

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri);

    // call version
    auto res = lbman.version();

    BOOST_CHECK(!res.has_error());

    BOOST_CHECK(!res.value().empty());

    std::cout << "Version string " << res.value() << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()