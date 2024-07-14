#define BOOST_TEST_MODULE DPSegTests
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

BOOST_AUTO_TEST_SUITE(DPSegTests)

// these tests test the send thread and the sending of
// the event messages. They generaly require external capture
// to verify sent data. It doesn't require having UDPLBd running
// as it takes the sync address directly from the EJFAT_URI
BOOST_AUTO_TEST_CASE(DPSegTest1)
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
    u_int16_t entropy = 16;

    // create a segmenter and start the threads
    Segmenter seg(uri, srcId, entropy, syncPeriodMS, syncPeriods);

    auto res = seg.openAndStart();

    if (res.has_error())
        std::cout << "ERROR: " << res.error().message() << std::endl;
    BOOST_CHECK(!res.has_error());

    std::cout << "Running test for 10 seconds" << std::endl;

    std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 2 SECONDS."s};
    // send one event message per 2 seconds that fits into a single frame
    auto sendStats = seg.getSendStats();
    if (sendStats.get<1>() != 0) 
    {
        std::cout << "Error encountered after opening send socket: " << strerror(sendStats.get<2>()) << std::endl;
    }
    for(auto i=0; i<5;i++) {
        auto sendres = seg.addToSendQueue(reinterpret_cast<u_int8_t*>(eventString.data()), eventString.length());
        BOOST_CHECK(!sendres.has_error());
        sendStats = seg.getSendStats();
        if (sendStats.get<1>() != 0) 
        {
            std::cout << "Error encountered sending event frames: " << strerror(sendStats.get<2>()) << std::endl;
        }
        // sleep for a second
        boost::this_thread::sleep_for(boost::chrono::seconds(2));
    }


    // check the sync stats
    auto syncStats = seg.getSyncStats();

    if (syncStats.get<1>() != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.get<2>()) << std::endl;
    }
    // send 10 sync messages and no errors
    BOOST_CHECK(syncStats.get<0>() == 10);
    BOOST_CHECK(syncStats.get<1>() == 0);

    // check the send stats

    // send 5 event messages and no errors
    BOOST_CHECK(sendStats.get<0>() == 5);
    BOOST_CHECK(sendStats.get<1>() == 0);

    // stop threads and exit
}

BOOST_AUTO_TEST_SUITE_END()