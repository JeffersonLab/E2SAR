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
using namespace std::chrono_literals;

BOOST_AUTO_TEST_SUITE(DPSyncLiveTests)

// these tests test the sync thread and the sending of
// the sync messages. They generaly require external capture
// to verify sent data
BOOST_AUTO_TEST_CASE(DPSyncLiveTest1)
{
    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "ERROR: " << uri_r.error().message() << std::endl;
    BOOST_CHECK(!uri_r.has_error());

    auto uri = uri_r.value();
    u_int16_t srcId = 0x05;
    u_int16_t syncPeriodMS = 1000; // in ms
    u_int16_t syncPeriods = 5; // number of sync periods to use for sync

    // create a segmenter and start the threads
    Segmenter seg{uri, srcId, syncPeriodMS, syncPeriods};

    auto res = seg.openAndStart();

    if (res.has_error())
        std::cout << "ERROR: " << res.error().message() << std::endl;
    BOOST_CHECK(!res.has_error());

    std::cout << "Running sync test for 10 seconds" << std::endl;
    // run for 10 seconds
    std::this_thread::sleep_for(10000ms);

    auto syncStats = seg.getSyncStats();

    // send 10 sync messages and no errors
    BOOST_CHECK(syncStats.get<0>() == 10);
    BOOST_CHECK(syncStats.get<1>() == 0);

    // stop threads and exit
}

BOOST_AUTO_TEST_SUITE_END()