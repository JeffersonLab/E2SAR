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

BOOST_AUTO_TEST_SUITE(DPReasTests)

// this is a test that uses local host to send/receive fragments
// it does NOT use control plane
BOOST_AUTO_TEST_CASE(DPReasTest1)
{
    std::cout << "DPReasTest1: Test segmentation and reassembly on local host with no control plane (no segmentation)" << std::endl;

    // create URI for segmenter - since we will turn off CP only the data part of the query is used
    std::string segUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};
    // create URI for reassembler - since we turn off CP, none of it is actually used
    std::string reasUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};

    try {
        EjfatURI segUri(segUriString, EjfatURI::TokenType::instance);
        EjfatURI reasUri(reasUriString, EjfatURI::TokenType::instance);

        // create segmenter with no control plane
        Segmenter::SegmenterFlags sflags;

        sflags.syncPeriodMs= 1000; // in ms
        sflags.syncPeriods = 5; // number of sync periods to use for sync
        sflags.useCP = false; // turn off CP

        u_int16_t entropy = 16;
        u_int16_t dataId = 0x0505;
        u_int32_t eventSrcId = 0x11223344;

        Segmenter seg(segUri, dataId, eventSrcId, entropy, sflags);

        // create reassembler with no control plane
        Reassembler::ReassemblerFlags rflags;

        rflags.useCP = false; // turn off CP
        rflags.dataIPString = "127.0.0.1";
        rflags.dataPort = 19522; // this is a standard LB port
        rflags.withLBHeader = true; // LB header will be attached since there is no LB

        Reassembler reas(reasUri, 1, rflags);

        auto res1 = seg.openAndStart();

        if (res1.has_error())
            std::cout << "Error encountered opening sockets and starting segmenter threads: " << res1.error().message() << std::endl;
        BOOST_CHECK(!res1.has_error());

        auto res2 = reas.openAndStart();
        if (res2.has_error())
            std::cout << "Error encountered opening sockets and starting reassembler threads: " << res2.error().message() << std::endl;
        BOOST_CHECK(!res2.has_error());

        std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 1 SECONDS."s};
        std::cout << "The event data is string '" << eventString << "' of length " << eventString.length() << std::endl;
        //
        // send one event message per 1 seconds that fits into a single frame
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
            boost::this_thread::sleep_for(boost::chrono::seconds(1));
        }

        // check the send stats
        sendStats = seg.getSendStats();

        std::cout << "Sent " << sendStats.get<0>() << " data frames" << std::endl;
        // send 5 event messages and no errors
        BOOST_CHECK(sendStats.get<0>() == 5);
        BOOST_CHECK(sendStats.get<1>() == 0);

        u_int8_t *eventBuf{nullptr};
        size_t eventLen;
        EventNum_t eventNum;
        u_int16_t recDataId;

        // receive data from the queue
        for(auto i=0; i<5; i++)
        {
            auto recvres = reas.getEvent(&eventBuf, &eventLen, &eventNum, &recDataId);
            if (recvres.has_error())
                std::cout << "Error encountered receiving event frames: " << strerror(sendStats.get<2>()) << std::endl;
            if (recvres.value() == -1)
                std::cout << "No message received, continuing" << std::endl;
            else
                std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen << " with event number " << eventNum << " and data id " << recDataId << std::endl;
        }

        // stop threads and exit
    }
    catch (E2SARException &ee) {
        std::cout << "Exception encountered: " << static_cast<std::string>(ee) << std::endl;
    }
    catch (...) {
        std::cout << "Some other exception" << std::endl;
    }
}

BOOST_AUTO_TEST_CASE(DPReasTest2)
{
    std::cout << "DPReasTest2: Test segmentation and reassembly on local host with no control plane (basic segmentation)" << std::endl;

    // create URI for segmenter - since we will turn off CP only the data part of the query is used
    std::string segUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};
    // create URI for reassembler - since we turn off CP, none of it is actually used
    std::string reasUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};

    try {
        EjfatURI segUri(segUriString, EjfatURI::TokenType::instance);
        EjfatURI reasUri(reasUriString, EjfatURI::TokenType::instance);

        // create segmenter with no control plane
        Segmenter::SegmenterFlags sflags;

        sflags.syncPeriodMs= 1000; // in ms
        sflags.syncPeriods = 5; // number of sync periods to use for sync
        sflags.useCP = false; // turn off CP
        sflags.mtu = 80; // make MTU ridiculously small to force SAR to work

        u_int16_t entropy = 16;
        u_int16_t dataId = 0x0505;
        u_int32_t eventSrcId = 0x11223344;

        Segmenter seg(segUri, dataId, eventSrcId, entropy, sflags);

        // create reassembler with no control plane
        Reassembler::ReassemblerFlags rflags;

        rflags.useCP = false; // turn off CP
        rflags.dataIPString = "127.0.0.1";
        rflags.dataPort = 19522; // this is a standard LB port
        rflags.withLBHeader = true; // LB header will be attached since there is no LB

        Reassembler reas(reasUri, 1, rflags);

        auto res1 = seg.openAndStart();

        if (res1.has_error())
            std::cout << "Error encountered opening sockets and starting segmenter threads: " << res1.error().message() << std::endl;
        BOOST_CHECK(!res1.has_error());

        auto res2 = reas.openAndStart();
        if (res2.has_error())
            std::cout << "Error encountered opening sockets and starting reassembler threads: " << res2.error().message() << std::endl;
        BOOST_CHECK(!res2.has_error());

        std::string eventString{"THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 1 SECONDS."s};
        std::cout << "The event data is string '" << eventString << "' of length " << eventString.length() << std::endl;
        //
        // send one event message per 1 seconds that fits into a single frame
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
            boost::this_thread::sleep_for(boost::chrono::seconds(1));
        }

        // check the send stats
        sendStats = seg.getSendStats();

        std::cout << "Sent " << sendStats.get<0>() << " data frames" << std::endl;
        // send 5 event messages and no errors
        BOOST_CHECK(sendStats.get<0>() == 25);
        BOOST_CHECK(sendStats.get<1>() == 0);

        u_int8_t *eventBuf{nullptr};
        size_t eventLen;
        EventNum_t eventNum;
        u_int16_t recDataId;

        // receive data from the queue
        for(auto i=0; i<5; i++)
        {
            auto recvres = reas.getEvent(&eventBuf, &eventLen, &eventNum, &recDataId);
            if (recvres.has_error())
                std::cout << "Error encountered receiving event frames: " << strerror(sendStats.get<2>()) << std::endl;
            if (recvres.value() == -1)
                std::cout << "No message received, continuing" << std::endl;
            else
                std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen << " with event number " << eventNum << " and data id " << recDataId << std::endl;
        }

        // stop threads and exit
    }
    catch (E2SARException &ee) {
        std::cout << "Exception encountered: " << static_cast<std::string>(ee) << std::endl;
    }
    catch (...) {
        std::cout << "Some other exception" << std::endl;
    }
}

BOOST_AUTO_TEST_SUITE_END()