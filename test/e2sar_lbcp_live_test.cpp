#define BOOST_TEST_MODULE CPLiveTests
#include <stdlib.h>
#include <iostream>
#include <cmath>
#include <boost/asio.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>
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
    auto lbman = LBManager(uri, false);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> senders{"192.168.100.1"s, "192.168.100.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    // call free - this will correctly use the admin token (even though instance token
    // is added by reserve call and updated URI inside with LB ID added to it
    auto res1 = lbman.freeLB();

    BOOST_CHECK(!res1.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest2)
{
    // reseserve, get, then free

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri, false);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> senders{"192.168.100.1"s, "192.168.100.2"s};

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

    auto lbman1 = LBManager(uri1, false);

    // get lb ID from other URI object
    auto res1 = lbman1.getLB(lbman.get_URI().get_lbId());

    BOOST_CHECK(!res1.has_error());

    BOOST_CHECK(lbman.get_URI().get_syncAddr().value() == lbman1.get_URI().get_syncAddr().value());

    res1 = lbman.freeLB();

    BOOST_CHECK(!res1.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest2_1)
{
    // reseserve, add senders, get, remove senders, get, then free

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri, false);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> emptySenders;
    std::vector<std::string> senders{"192.168.100.1"s, "192.168.100.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, emptySenders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    // call addSenders and don't fail (but don't check if they are added)
    auto res_as = lbman.addSenders(senders);
    BOOST_CHECK(!res_as.has_error());

    // call get on a new uri object so we can compare
    uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri1 = uri_r.value();

    auto lbman1 = LBManager(uri1, false);

    // get lb ID from other URI object
    auto res1 = lbman1.getLB(lbman.get_URI().get_lbId());

    BOOST_CHECK(!res1.has_error());

    BOOST_CHECK(lbman.get_URI().get_syncAddr().value() == lbman1.get_URI().get_syncAddr().value());

    // call removeSenders and don't fail (but don't check if they are removed)
    auto res_rs = lbman.removeSenders(senders);
    BOOST_CHECK(!res_rs.has_error());

    res1 = lbman.freeLB();

    BOOST_CHECK(!res1.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest3)
{
    // reserve, register worker, send state, unregister worker, free

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri, false);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> senders{"192.168.100.1"s, "192.168.100.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    // register (should internally set session token and session id)
    auto res1 = lbman.registerWorker("my_node"s, std::pair<ip::address, u_int16_t>(ip::make_address("192.168.101.5"), 10000), 0.5, 10, 1.0, 1.0);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_SessionToken().value().empty());
    BOOST_CHECK(!lbman.get_URI().get_sessionId().empty());

    // send state - every registered worker must do that every 100ms or be auto-deregistered
    res1 = lbman.sendState(0.8, 1.0, true);

    BOOST_CHECK(!res.has_error());

    // unregister (should use session token and session id)
    res1 = lbman.deregisterWorker();

    BOOST_CHECK(!res1.has_error());

    // free
    res1 = lbman.freeLB();

    BOOST_CHECK(!res1.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest4)
{
    // reserve, register worker, get status, unregister worker, get status, free

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri, false);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> senders{"192.168.20.1"s, "192.168.20.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    // register (should internally set session token and session id)
    auto res1 = lbman.registerWorker("my_node"s, std::pair<ip::address, u_int16_t>(ip::make_address("192.168.101.5"), 10000), 0.5, 10, 1.0, 1.0);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_SessionToken().value().empty());
    BOOST_CHECK(!lbman.get_URI().get_sessionId().empty());

    // send state - every registered worker must do that every 100ms or be auto-deregistered after 10s of silence
    // first 2 seconds of the state are discarded as too noisy
    for (int i = 25; i > 0; i--)
    {
        res1 = lbman.sendState(0.8, 1.0, true);
        BOOST_CHECK(!res.has_error());
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
    }

    // get LB status
    auto status_res = lbman.getLBStatus();

    BOOST_CHECK(!status_res.has_error());
    auto saddrs = LBManager::get_SenderAddressVector(status_res.value());
    BOOST_CHECK(saddrs.size() == 2);
    BOOST_CHECK(saddrs[0] == "192.168.20.1"s);
    auto workers = LBManager::get_WorkerStatusVector(status_res.value());
    BOOST_CHECK(workers.size() == 1);
    BOOST_CHECK(workers[0].name() == "my_node"s);
#define DELTAD 0.000001
    BOOST_CHECK(std::abs(workers[0].fillpercent() - 0.8) < DELTAD);
    BOOST_CHECK(std::abs(workers[0].controlsignal() - 1.0) < DELTAD);
    std::cout << "Last Updated " << workers[0].lastupdated() << std::endl;

    auto lbstatus = LBManager::asLBStatus(status_res.value());
    BOOST_CHECK(lbstatus->senderAddresses.size() == 2);
    BOOST_CHECK(lbstatus->senderAddresses[0] == "192.168.20.1"s);
    BOOST_CHECK(lbstatus->workers.size() == 1);
    BOOST_CHECK(lbstatus->workers[0].name() == "my_node"s);
    BOOST_CHECK(std::abs(lbstatus->workers[0].fillpercent() - 0.8) < DELTAD);
    BOOST_CHECK(std::abs(lbstatus->workers[0].controlsignal() - 1.0) < DELTAD);

    // unregister (should use session token and session id)
    res1 = lbman.deregisterWorker();
    BOOST_CHECK(!res1.has_error());

    // free
    res1 = lbman.freeLB();

    BOOST_CHECK(!res1.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest4_1)
{
    // reserve with empty senders, register worker, add senders, get status, 
    // remove senders, get_status, unregister worker, get status, free

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri, false);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> emptySenders;
    std::vector<std::string> senders{"192.168.20.1"s, "192.168.20.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, emptySenders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    // register (should internally set session token and session id)
    auto res1 = lbman.registerWorker("my_node"s, std::pair<ip::address, u_int16_t>(ip::make_address("192.168.101.5"), 10000), 0.5, 10, 1.0, 1.0);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_SessionToken().value().empty());
    BOOST_CHECK(!lbman.get_URI().get_sessionId().empty());

    // send state - every registered worker must do that every 100ms or be auto-deregistered after 10s of silence
    // first 2 seconds of the state are discarded as too noisy
    for (int i = 25; i > 0; i--)
    {
        res1 = lbman.sendState(0.8, 1.0, true);
        BOOST_CHECK(!res.has_error());
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
    }

    // call addSenders and don't fail
    auto res_as = lbman.addSenders(senders);
    BOOST_CHECK(!res_as.has_error());   

    // get LB status
    auto status_res = lbman.getLBStatus();

    BOOST_CHECK(!status_res.has_error());
    auto saddrs = LBManager::get_SenderAddressVector(status_res.value());
    BOOST_CHECK(saddrs.size() == 2);
    BOOST_CHECK(saddrs[0] == "192.168.20.1"s);
    auto workers = LBManager::get_WorkerStatusVector(status_res.value());
    BOOST_CHECK(workers.size() == 1);
    BOOST_CHECK(workers[0].name() == "my_node"s);
#define DELTAD 0.000001
    BOOST_CHECK(std::abs(workers[0].fillpercent() - 0.8) < DELTAD);
    BOOST_CHECK(std::abs(workers[0].controlsignal() - 1.0) < DELTAD);
    std::cout << "Last Updated " << workers[0].lastupdated() << std::endl;

    auto lbstatus = LBManager::asLBStatus(status_res.value());
    BOOST_CHECK(lbstatus->senderAddresses.size() == 2);
    BOOST_CHECK(lbstatus->senderAddresses[0] == "192.168.20.1"s);
    BOOST_CHECK(lbstatus->workers.size() == 1);
    BOOST_CHECK(lbstatus->workers[0].name() == "my_node"s);
    BOOST_CHECK(std::abs(lbstatus->workers[0].fillpercent() - 0.8) < DELTAD);
    BOOST_CHECK(std::abs(lbstatus->workers[0].controlsignal() - 1.0) < DELTAD);

    // call remove senders and don't fail
    auto res_rs = lbman.removeSenders(senders);
    BOOST_CHECK(!res_rs.has_error()); 

    // get LB status again and check workers are empty  
    status_res = lbman.getLBStatus();
    BOOST_CHECK(!status_res.has_error());
    saddrs = LBManager::get_SenderAddressVector(status_res.value());
    BOOST_CHECK(saddrs.size() == 0);

    // unregister (should use session token and session id)
    res1 = lbman.deregisterWorker();
    BOOST_CHECK(!res1.has_error());

    // free
    res1 = lbman.freeLB();

    BOOST_CHECK(!res1.has_error());
}

BOOST_AUTO_TEST_CASE(LBMLiveTest5)
{
    // version

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri, false);

    // call version
    auto res = lbman.version();

    BOOST_CHECK(!res.has_error());

    BOOST_CHECK(!res.value().get<0>().empty());
    BOOST_CHECK(!res.value().get<1>().empty());
    BOOST_CHECK(!res.value().get<2>().empty());

    std::cout << "Version string " << res.value() << std::endl;
}

BOOST_AUTO_TEST_CASE(LBMLiveTest6)
{
    // reserve, register worker, get status, unregister worker, get status, overview, free

    // parse URI from env variable
    auto uri_r = EjfatURI::getFromEnv();
    BOOST_CHECK(!uri_r.has_error());
    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri, false);

    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> senders{"192.168.20.1"s, "192.168.20.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    // register (should internally set session token and session id)
    auto res1 = lbman.registerWorker("my_node"s, std::pair<ip::address, u_int16_t>(ip::make_address("192.168.101.5"), 10000), 0.5, 10, 1.0, 1.0);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_SessionToken().value().empty());
    BOOST_CHECK(!lbman.get_URI().get_sessionId().empty());

    // send state - every registered worker must do that every 100ms or be auto-deregistered after 10s of silence
    // first 2 seconds of the state are discarded as too noisy
    for (int i = 25; i > 0; i--)
    {
        res1 = lbman.sendState(0.8, 1.0, true);
        BOOST_CHECK(!res.has_error());
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
    }

    // get LB status
    auto status_res = lbman.getLBStatus();

    BOOST_CHECK(!status_res.has_error());
    auto saddrs = LBManager::get_SenderAddressVector(status_res.value());
    BOOST_CHECK(saddrs.size() == 2);
    BOOST_CHECK(saddrs[0] == "192.168.20.1"s);
    auto workers = LBManager::get_WorkerStatusVector(status_res.value());
    BOOST_CHECK(workers.size() == 1);
    BOOST_CHECK(workers[0].name() == "my_node"s);
#define DELTAD 0.000001
    BOOST_CHECK(std::abs(workers[0].fillpercent() - 0.8) < DELTAD);
    BOOST_CHECK(std::abs(workers[0].controlsignal() - 1.0) < DELTAD);
    std::cout << "Last Updated " << workers[0].lastupdated() << std::endl;

    auto lbstatus = LBManager::asLBStatus(status_res.value());
    BOOST_CHECK(lbstatus->senderAddresses.size() == 2);
    BOOST_CHECK(lbstatus->senderAddresses[0] == "192.168.20.1"s);
    BOOST_CHECK(lbstatus->workers.size() == 1);
    BOOST_CHECK(lbstatus->workers[0].name() == "my_node"s);
    BOOST_CHECK(std::abs(lbstatus->workers[0].fillpercent() - 0.8) < DELTAD);
    BOOST_CHECK(std::abs(lbstatus->workers[0].controlsignal() - 1.0) < DELTAD);

    // overview
    auto overview_res = lbman.overview();
    BOOST_CHECK(!overview_res.has_error());

    auto overview = LBManager::asOverviewMessage(overview_res.value());
    BOOST_CHECK(overview[0].name == "mylb");
    BOOST_CHECK(overview[0].status.senderAddresses.size() == 2);
    BOOST_CHECK(overview[0].status.senderAddresses[0] == "192.168.20.1"s);
    BOOST_CHECK(overview[0].status.workers.size() == 1);
    BOOST_CHECK(overview[0].status.workers[0].name() == "my_node"s);
    BOOST_CHECK(std::abs(overview[0].status.workers[0].fillpercent() - 0.8) < DELTAD);
    BOOST_CHECK(std::abs(overview[0].status.workers[0].controlsignal() - 1.0) < DELTAD);
    
    // unregister (should use session token and session id)
    res1 = lbman.deregisterWorker();
    BOOST_CHECK(!res1.has_error());

    // free
    res1 = lbman.freeLB();

    BOOST_CHECK(!res1.has_error());
}

BOOST_AUTO_TEST_SUITE_END()