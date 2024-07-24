#define BOOST_TEST_MODULE DPSyncLiveTests
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

BOOST_AUTO_TEST_SUITE(DPSyncLiveTests)

// these tests test the sync thread and the sending of
// the sync messages against live UDPLBd. 
BOOST_AUTO_TEST_CASE(DPSyncLiveTest1)
{
    std::cout << "DPSyncLiveTest1: test sync thread against UDPLBd by sending 10 sync frames (once a second for 10 seconds)" << std::endl;

    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "ERROR: " << uri_r.error().message() << std::endl;
    BOOST_CHECK(!uri_r.has_error());

    auto uri = uri_r.value();

    // create LBManager
    auto lbman = LBManager(uri, false);

    // reserve an LB to get sync address
    auto duration_v = pt::duration_from_string("01");
    std::string lbname{"mylb"};
    std::vector<std::string> senders{"192.168.100.1"s, "192.168.100.2"s};

    // call reserve
    auto res = lbman.reserveLB(lbname, duration_v, senders);

    BOOST_CHECK(!res.has_error());
    BOOST_CHECK(!lbman.get_URI().get_InstanceToken().value().empty());
    BOOST_CHECK(lbman.get_URI().has_syncAddr());
    BOOST_CHECK(lbman.get_URI().has_dataAddr());

    u_int16_t dataId = 0x0505;
    u_int32_t eventSrcId = 0x11223344;
    u_int16_t syncPeriodMS = 1000; // in ms
    u_int16_t syncPeriods = 5; // number of sync periods to use for sync
    u_int16_t entropy = 16;

    // create a segmenter and start the threads
    // using the updated URI with sync info
    std::cout << "Creating segmenter using returned URI: " << 
        lbman.get_URI().to_string(EjfatURI::TokenType::instance) << std::endl;
    Segmenter seg(lbman.get_URI(), dataId, eventSrcId, entropy, syncPeriodMS, syncPeriods);

    auto res1 = seg.openAndStart();

    if (res1.has_error())
        std::cout << "ERROR: " << res1.error().message() << std::endl;
    BOOST_CHECK(!res1.has_error());

    std::cout << "Running sync test for 10 seconds" << std::endl;
    // run for 10 seconds
    boost::this_thread::sleep_for(boost::chrono::seconds(10));

    auto syncStats = seg.getSyncStats();

    if (syncStats.get<1>() != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.get<2>()) << std::endl;
    }
    // send 10 sync messages and no errors
    BOOST_CHECK(syncStats.get<0>() == 10);
    BOOST_CHECK(syncStats.get<1>() == 0);

    // call free - this will correctly use the admin token (even though instance token
    // is added by reserve call and updated URI inside with LB ID added to it
    auto res2 = lbman.freeLB();

    BOOST_CHECK(!res2.has_error());

    // stop threads and exit
}

BOOST_AUTO_TEST_SUITE_END()