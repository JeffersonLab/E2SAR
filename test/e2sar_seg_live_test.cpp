#define BOOST_TEST_MODULE DPSegLiveTests
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

BOOST_AUTO_TEST_SUITE(DPSegLiveTests)

// these tests test the sync thread and the sending of
// the sync messages against live UDPLBd. 
BOOST_AUTO_TEST_CASE(DPSegLiveTest1)
{
    std::cout << "DPSegLiveTest1: test segmenter (and sync thread) against UDPLBd by sending 5 events via event queue with default MTU so 5 frames are sent" << std::endl;

    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "URI Error: " << uri_r.error().message() << std::endl;
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
    Segmenter::SegmenterFlags sflags;
    sflags.syncPeriodMs= 1000; // in ms
    sflags.syncPeriods = 5; // number of sync periods to use for sync

    // create a segmenter and start the threads
    // using the updated URI with sync info
    std::cout << "Creating segmenter using returned URI: " << 
        lbman.get_URI().to_string(EjfatURI::TokenType::instance) << std::endl;
    Segmenter seg(lbman.get_URI(), dataId, eventSrcId, sflags);

    auto res1 = seg.openAndStart();

    if (res1.has_error())
        std::cout << "Error encountered opening sockets and starting threads: " << res1.error().message() << std::endl;
    BOOST_CHECK(!res1.has_error());

    std::cout << "Running data test for 10 seconds against sync " << 
        lbman.get_URI().get_syncAddr().value().first << ":" << 
        lbman.get_URI().get_syncAddr().value().second << " and data " <<
        lbman.get_URI().get_dataAddrv4().value().first << ":" <<
        lbman.get_URI().get_dataAddrv4().value().second << 
        std::endl;
    
    std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 2 SECONDS."s};
    std::cout << "The event data is string '" << eventString << "' of length " << eventString.length() << std::endl;
    //
    // send one event message per 2 seconds that fits into a single frame
    //
    auto sendStats = seg.getSendStats();
    if (sendStats.errCnt != 0) 
    {
        std::cout << "Error encountered after opening send socket: " << strerror(sendStats.lastErrno) << std::endl;
    }
    for(auto i=0; i<5;i++) {
        auto sendres = seg.addToSendQueue(reinterpret_cast<u_int8_t*>(eventString.data()), eventString.length());
        BOOST_CHECK(!sendres.has_error());
        sendStats = seg.getSendStats();
        if (sendStats.errCnt != 0) 
        {
            std::cout << "Error encountered sending event frames: " << strerror(sendStats.lastErrno) << std::endl;
        }
        // sleep for a second
        boost::this_thread::sleep_for(boost::chrono::seconds(2));
    }

    // check the sync stats
    auto syncStats = seg.getSyncStats();
    sendStats = seg.getSendStats();

    if (syncStats.errCnt != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.lastErrno) << std::endl;
    }
    // send 10 sync messages and no errors
    std::cout << "Sent " << syncStats.msgCnt << " sync frames" << std::endl;
    BOOST_CHECK(syncStats.msgCnt >= 10);
    BOOST_CHECK(syncStats.errCnt == 0);

    // check the send stats
    std::cout << "Sent " << sendStats.msgCnt << " data frames" << std::endl;
    // send 5 event messages and no errors
    BOOST_CHECK(sendStats.msgCnt == 5);
    BOOST_CHECK(sendStats.errCnt == 0);

    // call free - this will correctly use the admin token (even though instance token
    // is added by reserve call and updated URI inside with LB ID added to it
    auto res2 = lbman.freeLB();

    BOOST_CHECK(!res2.has_error());

    // stop threads and exit
}

// these tests test the sync thread and the sending of
// the sync messages against live UDPLBd. 
BOOST_AUTO_TEST_CASE(DPSegLiveTest2)
{
    std::cout << "DPSegLiveTest2: test segmenter (and sync thread) against UDPLBd by sending 5 events via event queue small MTU so 20 frames are sent" << std::endl;

    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "URI Error encountered: " << uri_r.error().message() << std::endl;
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
    Segmenter::SegmenterFlags sflags;
    sflags.syncPeriodMs = 500; // in ms
    sflags.syncPeriods = 5; // number of sync periods to use for sync
    sflags.mtu = 64 + 40;

    // create a segmenter using URI sync and data info
    // and start the threads, send MTU is set to force
    // breaking up event payload into multiple frames
    // 64 is the length of all headers (IP, UDP, LB, RE)
    Segmenter seg(lbman.get_URI(), dataId, eventSrcId, sflags);
    std::cout << "Creating segmenter using returned URI: " << 
        lbman.get_URI().to_string(EjfatURI::TokenType::instance) << std::endl;

    auto res1 = seg.openAndStart();

    if (res1.has_error())
        std::cout << "Error encountered opening sockets and starting threads: " << res1.error().message() << std::endl;
    BOOST_CHECK(!res1.has_error());

    std::cout << "Running data test against sync " << 
        lbman.get_URI().get_syncAddr().value().first << ":" << 
        lbman.get_URI().get_syncAddr().value().second << " and data " <<
        lbman.get_URI().get_dataAddrv4().value().first << ":" <<
        lbman.get_URI().get_dataAddrv4().value().second << 
        std::endl;
    
    std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 1/2 SECONDS."s};
    std::cout << "The event data is string '" << eventString << "' of length " << eventString.length() << std::endl;
    //
    // send one event message per 2 seconds that fits into a single frame
    //
    auto sendStats = seg.getSendStats();
    if (sendStats.errCnt != 0) 
    {
        std::cout << "Error encountered after opening send socket: " << strerror(sendStats.lastErrno) << std::endl;
    }
    for(auto i=0; i<10;i++) {
        auto sendres = seg.addToSendQueue(reinterpret_cast<u_int8_t*>(eventString.data()), eventString.length());
        BOOST_CHECK(!sendres.has_error());
        sendStats = seg.getSendStats();
        if (sendStats.errCnt != 0) 
        {
            std::cout << "Error encountered sending event frames: " << strerror(sendStats.lastErrno) << std::endl;
        }
        // sleep for a second
        boost::this_thread::sleep_for(boost::chrono::milliseconds(500));
    }

    // check the sync stats
    auto syncStats = seg.getSyncStats();
    sendStats = seg.getSendStats();

    if (syncStats.errCnt != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.lastErrno) << std::endl;
    }
    // send 10 sync messages and no errors
    std::cout << "Sent " << syncStats.msgCnt << " sync frames" << std::endl;
    BOOST_CHECK(syncStats.msgCnt >= 10);
    BOOST_CHECK(syncStats.errCnt == 0);

    // check the send stats
    std::cout << "Sent " << sendStats.msgCnt << " data frames" << std::endl;
    // send 5 event messages and no errors
    BOOST_CHECK(sendStats.msgCnt == 20);
    BOOST_CHECK(sendStats.errCnt == 0);
    
    // call free - this will correctly use the admin token (even though instance token
    // is added by reserve call and updated URI inside with LB ID added to it
    auto res2 = lbman.freeLB();

    BOOST_CHECK(!res2.has_error());

    // stop threads and exit
}

BOOST_AUTO_TEST_SUITE_END()