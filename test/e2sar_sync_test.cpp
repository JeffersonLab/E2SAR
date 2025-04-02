#define BOOST_TEST_MODULE DPSyncTests
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

BOOST_AUTO_TEST_SUITE(DPSyncTests)

// these tests test the sync thread and the sending of
// the sync messages. They generaly require external capture
// to verify sent data. It doesn't require having UDPLBd running
// as it takes the sync address directly from the EJFAT_URI
BOOST_AUTO_TEST_CASE(DPSyncTest1)
{
    std::cout << "DPSyncTest1: test sync thread sending 11 sync frames (once a second for 10 seconds + 1 sec warm up)" << std::endl;

    std::string segUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=10.251.100.122:12345&data=10.250.100.123"};
    EjfatURI uri(segUriString);

    u_int16_t dataId = 0x0505;
    u_int32_t eventSrcId = 0x11223344;
    Segmenter::SegmenterFlags sflags;
    sflags.syncPeriodMs = 1000; // in ms
    sflags.syncPeriods = 5; // number of sync periods to use for sync

    // create a segmenter and start the threads
    Segmenter seg(uri, dataId, eventSrcId, sflags);

    // remember this will send one sync packet because of warm up
    auto res = seg.openAndStart();

    if (res.has_error())
        std::cout << "ERROR: " << res.error().message() << std::endl;
    BOOST_CHECK(!res.has_error());

    std::cout << "Running sync test for 10 seconds " << 
        uri.get_syncAddr().value().first << ": " << 
        uri.get_syncAddr().value().second << 
        std::endl;
    // run for 10 seconds
    boost::this_thread::sleep_for(boost::chrono::seconds(10));

    auto syncStats = seg.getSyncStats();

    if (syncStats.errCnt != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.lastErrno) << std::endl;
    }
    std::cout << "Sent " << syncStats.msgCnt << " sync frames" << std::endl;
    // send 10+1 (one second sleep before data frame is sent) sync messages and no errors
    BOOST_CHECK(syncStats.msgCnt == 11);
    BOOST_CHECK(syncStats.errCnt == 0);

    // stop threads and exit
}

BOOST_AUTO_TEST_SUITE_END()