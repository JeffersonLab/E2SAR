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
    std::cout << "DPSegTest1: test segmenter (and sync thread) by sending 5 events via event queue with default MTU" << std::endl;
    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "URI Error: " << uri_r.error().message() << std::endl;
    BOOST_CHECK(!uri_r.has_error());

    auto uri = uri_r.value();
    u_int16_t dataId = 0x0505;
    u_int32_t eventSrcId = 0x11223344;
    Segmenter::SegmenterFlags sflags;
    sflags.syncPeriodMs = 1000; // in ms
    sflags.syncPeriods = 5; // number of sync periods to use for sync

    // create a segmenter and start the threads
    Segmenter seg(uri, dataId, eventSrcId, sflags);

    auto res = seg.openAndStart();

    if (res.has_error())
        std::cout << "Error encountered opening sockets and starting threads: " << res.error().message() << std::endl;
    BOOST_CHECK(!res.has_error());

    std::cout << "Running data test for 10 seconds against sync " << 
        uri.get_syncAddr().value().first << ":" << 
        uri.get_syncAddr().value().second << " and data " <<
        uri.get_dataAddrv4().value().first << ":" <<
        uri.get_dataAddrv4().value().second << 
        std::endl;

    std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 2 SECONDS."s};
    std::cout << "The event data is string '" << eventString << "' of length " << eventString.length() << std::endl;
    //
    // send one event message per 2 seconds that fits into a single frame using event queue
    //
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
    sendStats = seg.getSendStats();

    if (syncStats.get<1>() != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.get<2>()) << std::endl;
    }
    // send 10 sync messages and no errors
    std::cout << "Sent " << syncStats.get<0>() << " sync frames" << std::endl;
    BOOST_CHECK(syncStats.get<0>() == 10);
    BOOST_CHECK(syncStats.get<1>() == 0);

    // check the send stats
    std::cout << "Sent " << sendStats.get<0>() << " data frames" << std::endl;
    // send 5 event messages and no errors
    BOOST_CHECK(sendStats.get<0>() == 5);
    BOOST_CHECK(sendStats.get<1>() == 0);

    // stop threads and exit
}

BOOST_AUTO_TEST_CASE(DPSegTest2)
{
    std::cout << "DPSegTest2: test segmenter (and sync thread) by sending 5 events via event queue with small MTU so 10 frames are sent" << std::endl;

    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "URI Error: " << uri_r.error().message() << std::endl;
    BOOST_CHECK(!uri_r.has_error());

    auto uri = uri_r.value();
    u_int16_t dataId = 0x0505;
    u_int32_t eventSrcId = 0x11223344;
    Segmenter::SegmenterFlags sflags;
    sflags.syncPeriodMs = 1000; // in ms
    sflags.syncPeriods = 5; // number of sync periods to use for sync
    sflags.mtu = 64 + 40;

    // create a segmenter and start the threads, send MTU is set to force
    // breaking up event payload into multiple frames
    // 64 is the length of all headers (IP, UDP, LB, RE)
    Segmenter seg(uri, dataId, eventSrcId, sflags);

    auto res = seg.openAndStart();

    if (res.has_error())
        std::cout << "Error encountered opening sockets and starting threads: " << res.error().message() << std::endl;
    BOOST_CHECK(!res.has_error());

    std::cout << "Running data test for 10 seconds against sync " << 
        uri.get_syncAddr().value().first << ":" << 
        uri.get_syncAddr().value().second << " and data " <<
        uri.get_dataAddrv4().value().first << ":" <<
        uri.get_dataAddrv4().value().second << 
        std::endl;

    std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 2 SECONDS."s};
    std::cout << "The event data is string '" << eventString << "' of length " << eventString.length() << std::endl;
    //
    // send one event message per 2 seconds that fits into two frames using event queue
    //
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
    sendStats = seg.getSendStats();

    if (syncStats.get<1>() != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.get<2>()) << std::endl;
    }
    // send 10 sync messages and no errors
    std::cout << "Sent " << syncStats.get<0>() << " sync frames" << std::endl;
    BOOST_CHECK(syncStats.get<0>() == 10);
    BOOST_CHECK(syncStats.get<1>() == 0);

    // check the send stats
    std::cout << "Sent " << sendStats.get<0>() << " data frames" << std::endl;
    // send 5 event messages and no errors
    BOOST_CHECK(sendStats.get<0>() == 10);
    BOOST_CHECK(sendStats.get<1>() == 0);

    // stop threads and exit
}

BOOST_AUTO_TEST_CASE(DPSegTest3)
{
    std::cout << "DPSegTest3: test segmenter (and sync thread) by sending 5 events via sendEvent() with small MTU so 10 frames are sent" << std::endl;

    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "URI Error: " << uri_r.error().message() << std::endl;
    BOOST_CHECK(!uri_r.has_error());

    auto uri = uri_r.value();
    u_int16_t dataId = 0x0505;
    u_int32_t eventSrcId = 0x11223344;
    Segmenter::SegmenterFlags sflags;
    sflags.syncPeriodMs = 1000; // in ms
    sflags.syncPeriods = 5; // number of sync periods to use for sync
    sflags.mtu = 64 + 40;

    // create a segmenter and start the threads, send MTU is set to force
    // breaking up event payload into multiple frames
    // 64 is the length of all headers (IP, UDP, LB, RE)
    Segmenter seg(uri, dataId, eventSrcId, sflags);

    auto res = seg.openAndStart();

    if (res.has_error())
        std::cout << "Error encountered opening sockets and starting threads: " << res.error().message() << std::endl;
    BOOST_CHECK(!res.has_error());

    std::cout << "Running data test for 10 seconds against sync " << 
        uri.get_syncAddr().value().first << ":" << 
        uri.get_syncAddr().value().second << " and data " <<
        uri.get_dataAddrv4().value().first << ":" <<
        uri.get_dataAddrv4().value().second << 
        std::endl;

    std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 2 SECONDS."s};
    std::cout << "The event data is string '" << eventString << "' of length " << eventString.length() << std::endl;
    //
    // send one event message per 2 seconds that fits into two frames, but use direct send
    // not the event queue
    //
    auto sendStats = seg.getSendStats();
    if (sendStats.get<1>() != 0) 
    {
        std::cout << "Error encountered after opening send socket: " << strerror(sendStats.get<2>()) << std::endl;
    }
    for(auto i=0; i<5;i++) {
        // Use direct send instead of the queue
        auto sendres = seg.sendEvent(reinterpret_cast<u_int8_t*>(eventString.data()), eventString.length());
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
    sendStats = seg.getSendStats();

    if (syncStats.get<1>() != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.get<2>()) << std::endl;
    }
    // send 10 sync messages and no errors
    std::cout << "Sent " << syncStats.get<0>() << " sync frames" << std::endl;
    BOOST_CHECK(syncStats.get<0>() == 10);
    BOOST_CHECK(syncStats.get<1>() == 0);

    // check the send stats
    std::cout << "Sent " << sendStats.get<0>() << " data frames" << std::endl;
    // send 5 event messages and no errors
    BOOST_CHECK(sendStats.get<0>() == 10);
    BOOST_CHECK(sendStats.get<1>() == 0);

    // stop threads and exit
}

int parameter = 5;
void fakeCB(boost::any val) {
    std::cout << "Callback invoked with parameter " << *(boost::any_cast<int*>(val))<< std::endl;
}

BOOST_AUTO_TEST_CASE(DPSegTest4)
{
    std::cout << "DPSegTest4: test segmenter (and sync thread) by sending 5 events via event queue with callbacks and default MTU so 5 frames are sent" << std::endl;

    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "URI Error: " << uri_r.error().message() << std::endl;
    BOOST_CHECK(!uri_r.has_error());

    auto uri = uri_r.value();
    u_int16_t dataId = 0x0505;
    u_int32_t eventSrcId = 0x11223344;
    Segmenter::SegmenterFlags sflags;
    sflags.syncPeriodMs = 1000; // in ms
    sflags.syncPeriods = 5; // number of sync periods to use for sync
    sflags.mtu = 64 + 40;

    // create a segmenter and start the threads, send MTU is set to force
    // breaking up event payload into multiple frames
    // 64 is the length of all headers (IP, UDP, LB, RE)
    Segmenter seg(uri, dataId, eventSrcId, sflags);

    auto res = seg.openAndStart();

    if (res.has_error())
        std::cout << "Error encountered opening sockets and starting threads: " << res.error().message() << std::endl;
    BOOST_CHECK(!res.has_error());

    std::cout << "Running data test for 10 seconds against sync " << 
        uri.get_syncAddr().value().first << ":" << 
        uri.get_syncAddr().value().second << " and data " <<
        uri.get_dataAddrv4().value().first << ":" <<
        uri.get_dataAddrv4().value().second << 
        std::endl;

    std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 2 SECONDS."s};
    std::cout << "The event data is string '" << eventString << "' of length " << eventString.length() << std::endl;
    //
    // send one event message per 2 seconds that fits into two frames using event queue with callback
    //
    auto sendStats = seg.getSendStats();
    if (sendStats.get<1>() != 0) 
    {
        std::cout << "Error encountered after opening send socket: " << strerror(sendStats.get<2>()) << std::endl;
    }
    for(auto i=0; i<5;i++) {
        auto sendres = seg.addToSendQueue(reinterpret_cast<u_int8_t*>(eventString.data()), 
            eventString.length(), 0, &fakeCB, &parameter);
        parameter++;
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
    sendStats = seg.getSendStats();

    if (syncStats.get<1>() != 0) 
    {
        std::cout << "Error encountered sending sync frames: " << strerror(syncStats.get<2>()) << std::endl;
    }
    // send 10 sync messages and no errors
    std::cout << "Sent " << syncStats.get<0>() << " sync frames" << std::endl;
    BOOST_CHECK(syncStats.get<0>() == 10);
    BOOST_CHECK(syncStats.get<1>() == 0);

    // check the send stats
    std::cout << "Sent " << sendStats.get<0>() << " data frames" << std::endl;
    // send 5 event messages and no errors
    BOOST_CHECK(sendStats.get<0>() == 10);
    BOOST_CHECK(sendStats.get<1>() == 0);

    // stop threads and exit
}


BOOST_AUTO_TEST_SUITE_END()