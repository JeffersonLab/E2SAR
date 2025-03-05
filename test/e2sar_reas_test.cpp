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
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/detail/file_parser_error.hpp>

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
    std::string segUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:10000"};
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

        u_int16_t dataId = 0x0505;
        u_int32_t eventSrcId = 0x11223344;

        Segmenter seg(segUri, dataId, eventSrcId, sflags);

        // create reassembler with no control plane
        Reassembler::ReassemblerFlags rflags;

        rflags.useCP = false; // turn off CP
        rflags.withLBHeader = true; // LB header will be attached since there is no LB

        ip::address loopback = ip::make_address("127.0.0.1");
        u_int16_t listen_port = 10000;
        Reassembler reas(reasUri, loopback, listen_port, 1, rflags);

        std::cout << "This reassembler has " << reas.get_numRecvThreads() << " receive threads and is listening on ports " << 
            reas.get_recvPorts().first << ":" << reas.get_recvPorts().second << " using portRange " << reas.get_portRange() << 
            std::endl;

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
                std::cout << "Error encountered receiving event frames " << std::endl;
            if (recvres.value() == -1)
                std::cout << "No message received, continuing" << std::endl;
            else
                std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen << " with event number " << eventNum << " and data id " << recDataId << std::endl;
        }

        auto recvStats = reas.getStats();
        /*
             *  - EventNum_t enqueueLoss;  // number of events received and lost on enqueue
             *  - EventNum_t reassemblyLoss; // number of events lost in reassembly
             *  - EventNum_t eventSuccess; // events successfully processed
             *  - int lastErrno; 
             *  - int grpcErrCnt; 
             *  - int dataErrCnt; 
             *  - E2SARErrorc lastE2SARError; 
        */
        BOOST_CHECK(recvStats.get<0>() == 0); // no enque losses
        BOOST_CHECK(recvStats.get<1>() == 0); // no reassembly losses
        BOOST_CHECK(recvStats.get<2>() == 5); // all succeeded
        BOOST_CHECK(recvStats.get<3>() == 0); // no errno
        BOOST_CHECK(recvStats.get<4>() == 0); // no grpc errors
        BOOST_CHECK(recvStats.get<5>() == 0); // no data errors
        BOOST_CHECK(recvStats.get<6>() == E2SARErrorc::NoError); // no error

        auto lostEvent = reas.get_LostEvent();
        if (lostEvent.has_error())
            std::cout << "NO EVENT LOSS " << std::endl;
        else
            std::cout << "LOST EVENT " << lostEvent.value().first << ":" << lostEvent.value().second << std::endl;
        BOOST_CHECK(lostEvent.has_error() && lostEvent.error().code() == E2SARErrorc::NotFound);

        // stop threads and exit
    }
    catch (E2SARException &ee) {
        std::cout << "Exception encountered: " << static_cast<std::string>(ee) << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::exception &e) {
        std::cout << "STD:EXCEPTION encountered " << typeid(e).name() << ": " << e.what() << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::error_code &e) {
        // This is not an exception object, this is because of this line in Reassembler constructor:
        // dataIP{(rflags.dpV6 ? uri.get_dataAddrv6().value().first : uri.get_dataAddrv4().value().first)}
        // if data address is messed up in the URI it will produce E2SARErrorInfo object with code and message
        std::cout << "STD ERROR CODE " << e.value() << " category " << e.category().name() << " with message " << e.message() << std::endl;
        BOOST_CHECK(false);
    }
    catch (boost::exception &e) {
        std::cout << "BOOST:EXCEPTION encountered " << typeid(e).name() << ": " << *boost::get_error_info<boost::throw_function>(e) << std::endl;
        BOOST_CHECK(false);
    }
    catch (...) {
        std::cout << "Some other exception" << std::endl;
        BOOST_CHECK(false);
    }
}

BOOST_AUTO_TEST_CASE(DPReasTest2)
{
    std::cout << "DPReasTest2: Test segmentation and reassembly on local host with no control plane (basic segmentation)" << std::endl;

    // create URI for segmenter - since we will turn off CP only the data part of the query is used
    std::string segUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:10000"};
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

        u_int16_t dataId = 0x0505;
        u_int32_t eventSrcId = 0x11223344;

        Segmenter seg(segUri, dataId, eventSrcId, sflags);

        // create reassembler with no control plane
        Reassembler::ReassemblerFlags rflags;

        rflags.useCP = false; // turn off CP
        rflags.withLBHeader = true; // LB header will be attached since there is no LB

        ip::address loopback = ip::make_address("127.0.0.1");
        u_int16_t listen_port = 10000;
        Reassembler reas(reasUri, loopback, listen_port, 1, rflags);

        std::cout << "This reassmebler has " << reas.get_numRecvThreads() << " receive threads and is listening on ports " << 
            reas.get_recvPorts().first << ":" << reas.get_recvPorts().second << " using portRange " << reas.get_portRange() << 
            std::endl;

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
                std::cout << "Error encountered receiving event frames" << std::endl;
            if (recvres.value() == -1)
                std::cout << "No message received, continuing" << std::endl;
            else
                std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen << " with event number " << eventNum << " and data id " << recDataId << std::endl;
        }
        auto recvStats = reas.getStats();
        /*
             *  - EventNum_t enqueueLoss;  // number of events received and lost on enqueue
             *  - EventNum_t reassemblyLoss; // number of events lost in reassmebly due to missing segments
             *  - EventNum_t eventSuccess; // events successfully processed
             *  - int lastErrno; 
             *  - int grpcErrCnt; 
             *  - int dataErrCnt; 
             *  - E2SARErrorc lastE2SARError; 
        */
        BOOST_CHECK(recvStats.get<0>() == 0); // no enqueue losses
        BOOST_CHECK(recvStats.get<1>() == 0); // no reassembly losses
        BOOST_CHECK(recvStats.get<2>() == 5); // all succeeded
        BOOST_CHECK(recvStats.get<3>() == 0); // no errno
        BOOST_CHECK(recvStats.get<4>() == 0); // no grpc errors
        BOOST_CHECK(recvStats.get<5>() == 0); // no data errors
        BOOST_CHECK(recvStats.get<6>() == E2SARErrorc::NoError); // no error

        auto lostEvent = reas.get_LostEvent();
        if (lostEvent.has_error())
            std::cout << "NO EVENT LOSS " << std::endl;
        else
            std::cout << "LOST EVENT " << lostEvent.value().first << ":" << lostEvent.value().second << std::endl;
        BOOST_CHECK(lostEvent.has_error() && lostEvent.error().code() == E2SARErrorc::NotFound);

        // stop threads and exit
    }
    catch (E2SARException &ee) {
        std::cout << "Exception encountered: " << static_cast<std::string>(ee) << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::exception &e) {
        std::cout << "STD:EXCEPTION encountered " << typeid(e).name() << ": " << e.what() << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::error_code &e) {
        std::cout << "STD ERROR CODE " << e.value() << " category " << e.category().name() << " with message " << e.message() << std::endl;
        BOOST_CHECK(false);
    }
    catch (boost::exception &e) {
        std::cout << "BOOST:EXCEPTION encountered " << typeid(e).name() << ": " << *boost::get_error_info<boost::throw_function>(e) << std::endl;
        BOOST_CHECK(false);
    }
    catch (...) {
        std::cout << "Some other exception" << std::endl;
        BOOST_CHECK(false);
    }
}

BOOST_AUTO_TEST_CASE(DPReasTest3)
{
    std::cout << "DPReasTest3: Test creationg of reassemblers with different parameters" << std::endl;

    // create URI for reassembler - since we turn off CP, none of it is actually used
    std::string reasUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};

    try {
        EjfatURI reasUri(reasUriString, EjfatURI::TokenType::instance);

        // create reassembler with no control plane
        Reassembler::ReassemblerFlags rflags;

        ip::address loopback = ip::make_address("127.0.0.1");
        u_int16_t listen_port = 19522;
        {
            // one thread
            Reassembler reas(reasUri, loopback, listen_port,  1, rflags);

            std::cout << "This reassmebler has " << reas.get_numRecvThreads() << " receive threads and is listening on ports " << 
                reas.get_recvPorts().first << ":" << reas.get_recvPorts().second << " using portRange " << reas.get_portRange() << 
                std::endl;
            BOOST_CHECK(reas.get_numRecvThreads() == 1);
            BOOST_CHECK(reas.get_recvPorts().first == 19522);
            BOOST_CHECK(reas.get_recvPorts().second == 19522);
            BOOST_CHECK(reas.get_portRange() == 0);
        }

        {
            // 4 threads
            Reassembler reas(reasUri, loopback, listen_port, 4, rflags);

            std::cout << "This reassmebler has " << reas.get_numRecvThreads() << " receive threads and is listening on ports " << 
                reas.get_recvPorts().first << ":" << reas.get_recvPorts().second << " using portRange " << reas.get_portRange() << 
                std::endl;
            BOOST_CHECK(reas.get_numRecvThreads() == 4);
            BOOST_CHECK(reas.get_recvPorts().first == 19522);
            BOOST_CHECK(reas.get_recvPorts().second == 19525);
            BOOST_CHECK(reas.get_portRange() == 2);
        }

         {
            // 7 threads
            Reassembler reas(reasUri, loopback, listen_port, 7, rflags);

            std::cout << "This reassmebler has " << reas.get_numRecvThreads() << " receive threads and is listening on ports " << 
                reas.get_recvPorts().first << ":" << reas.get_recvPorts().second << " using portRange " << reas.get_portRange() << 
                std::endl;
            BOOST_CHECK(reas.get_numRecvThreads() == 7);
            BOOST_CHECK(reas.get_recvPorts().first == 19522);
            BOOST_CHECK(reas.get_recvPorts().second == 19529);
            BOOST_CHECK(reas.get_portRange() == 3);
        }

        {
            // 4 threads with portRange override
            rflags.portRange = 10;
            Reassembler reas(reasUri, loopback, listen_port, 4, rflags);

            std::cout << "This reassmebler has " << reas.get_numRecvThreads() << " receive threads and is listening on ports " << 
                reas.get_recvPorts().first << ":" << reas.get_recvPorts().second << " using portRange " << reas.get_portRange() << 
                std::endl;         
            BOOST_CHECK(reas.get_numRecvThreads() == 4);
            BOOST_CHECK(reas.get_recvPorts().first == 19522);
            BOOST_CHECK(reas.get_recvPorts().second == 20545);
            BOOST_CHECK(reas.get_portRange() == 10);
        }
        {
            // 4 threads with low portRange override
            rflags.portRange = 1;
            Reassembler reas(reasUri, loopback, listen_port, 4, rflags);

            std::cout << "This reassmebler has " << reas.get_numRecvThreads() << " receive threads and is listening on ports " << 
                reas.get_recvPorts().first << ":" << reas.get_recvPorts().second << " using portRange " << reas.get_portRange() << 
                std::endl;         
            BOOST_CHECK(reas.get_numRecvThreads() == 4);
            BOOST_CHECK(reas.get_recvPorts().first == 19522);
            BOOST_CHECK(reas.get_recvPorts().second == 19523);
            BOOST_CHECK(reas.get_portRange() == 1);
        }
    }
    catch (E2SARException &ee) {
        std::cout << "Exception encountered: " << static_cast<std::string>(ee) << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::exception &e) {
        std::cout << "STD:EXCEPTION encountered " << typeid(e).name() << ": " << e.what() << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::error_code &e) {
        std::cout << "STD ERROR CODE " << e.value() << " category " << e.category().name() << " with message " << e.message() << std::endl;
        BOOST_CHECK(false);
    }
    catch (boost::exception &e) {
        std::cout << "BOOST:EXCEPTION encountered " << typeid(e).name() << ": " << *boost::get_error_info<boost::throw_function>(e) << std::endl;
        BOOST_CHECK(false);
    }
    catch (...) {
        std::cout << "Some other exception" << std::endl;
        BOOST_CHECK(false);
    }
}

BOOST_AUTO_TEST_CASE(DPReasTest4)
{
    std::cout << "DPReasTest4: Test segmentation and reassembly on local host with no control plane (with segmentation and multiple senders)" << std::endl;

    // create URIs for segmenters - since we will turn off CP only the data part of the query is used
    std::string segUriString1{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:19522"};
    std::string segUriString2{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:19523"};
    std::string segUriString3{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:19524"};
    std::string segUriString4{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:19525"};
    // create URI for reassembler - since we turn off CP, none of it is actually used
    std::string reasUriString{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};

    try {
        EjfatURI segUri1(segUriString1, EjfatURI::TokenType::instance);
        EjfatURI segUri2(segUriString2, EjfatURI::TokenType::instance);
        EjfatURI segUri3(segUriString3, EjfatURI::TokenType::instance);
        EjfatURI segUri4(segUriString4, EjfatURI::TokenType::instance);

        EjfatURI reasUri(reasUriString, EjfatURI::TokenType::instance);

        // create several segmenters with no control plane
        Segmenter::SegmenterFlags sflags;

        sflags.syncPeriodMs= 1000; // in ms
        sflags.syncPeriods = 5; // number of sync periods to use for sync
        sflags.useCP = false; // turn off CP

        u_int16_t dataId = 0x0505;
        u_int32_t eventSrcId = 0x11223344;

        Segmenter seg1(segUri1, dataId, eventSrcId, sflags);
        Segmenter seg2(segUri2, dataId, eventSrcId, sflags);
        Segmenter seg3(segUri3, dataId, eventSrcId, sflags);
        Segmenter seg4(segUri4, dataId, eventSrcId, sflags);

        // create reassembler with no control plane
        Reassembler::ReassemblerFlags rflags;

        rflags.useCP = false; // turn off CP
        rflags.withLBHeader = true; // LB header will be attached since there is no LB
        rflags.portRange = 2;

        ip::address loopback = ip::make_address("127.0.0.1");
        u_int16_t listen_port = 19522;
        // 1 thread for 4 ports
        Reassembler reas(reasUri, loopback, listen_port,  1, rflags);

        std::cout << "This reassmebler has " << reas.get_numRecvThreads() << " receive threads and is listening on ports " << 
            reas.get_recvPorts().first << ":" << reas.get_recvPorts().second << " using portRange " << reas.get_portRange() << 
            std::endl;

        std::cout << "Seg1.openAndStart()" << std::endl;
        auto res1 = seg1.openAndStart();
        if (res1.has_error())
            std::cout << "Error encountered opening sockets and starting segmenter1 threads: " << res1.error().message() << std::endl;
        BOOST_CHECK(!res1.has_error());

        std::cout << "Seg2.openAndStart()" << std::endl;
        res1 = seg2.openAndStart();
        if (res1.has_error())
            std::cout << "Error encountered opening sockets and starting segmenter2 threads: " << res1.error().message() << std::endl;
        BOOST_CHECK(!res1.has_error());

        std::cout << "Seg3.openAndStart()" << std::endl;
        res1 = seg3.openAndStart();
        if (res1.has_error())
            std::cout << "Error encountered opening sockets and starting segmenter3 threads: " << res1.error().message() << std::endl;
        BOOST_CHECK(!res1.has_error());

        std::cout << "Seg4.openAndStart()" << std::endl;
        res1 = seg4.openAndStart();
        if (res1.has_error())
            std::cout << "Error encountered opening sockets and starting segmenter4 threads: " << res1.error().message() << std::endl;
        BOOST_CHECK(!res1.has_error());

        std::cout << "Reas.openAndStart()" << std::endl;
        auto res2 = reas.openAndStart();
        if (res2.has_error())
            std::cout << "Error encountered opening sockets and starting reassembler threads: " << res2.error().message() << std::endl;
        BOOST_CHECK(!res2.has_error());

        std::string eventString1{"THIS IS A VERY LONG EVENT MESSAGE FROM SEGMENTER1 WE WANT TO SEND EVERY 1 SECONDS."s};
        std::string eventString2{"THIS IS A VERY LONG EVENT MESSAGE FROM SEGMENTER2 WE WANT TO SEND EVERY 1 SECONDS."s};
        std::string eventString3{"THIS IS A VERY LONG EVENT MESSAGE FROM SEGMENTER3 WE WANT TO SEND EVERY 1 SECONDS."s};
        std::string eventString4{"THIS IS A VERY LONG EVENT MESSAGE FROM SEGMENTER4 WE WANT TO SEND EVERY 1 SECONDS."s};

        //
        // send one event message per 1 seconds that fits into a single frame
        //
        auto sendStats = seg1.getSendStats();
        if (sendStats.get<1>() != 0) 
        {
            std::cout << "Error encountered after opening send socket in segmenter1: " << strerror(sendStats.get<2>()) << std::endl;
        }
        sendStats = seg2.getSendStats();
        if (sendStats.get<1>() != 0) 
        {
            std::cout << "Error encountered after opening send socket in segmenter2: " << strerror(sendStats.get<2>()) << std::endl;
        }
        sendStats = seg3.getSendStats();
        if (sendStats.get<1>() != 0) 
        {
            std::cout << "Error encountered after opening send socket in segmenter3: " << strerror(sendStats.get<2>()) << std::endl;
        }
        sendStats = seg4.getSendStats();
        if (sendStats.get<1>() != 0) 
        {
            std::cout << "Error encountered after opening send socket in segmenter4: " << strerror(sendStats.get<2>()) << std::endl;
        }

        EventNum_t seg1Base = 1000LL, seg2Base = 2000LL, seg3Base = 3000LL, seg4Base = 4000LL;
        for(auto i=0; i<5;i++) {
            auto sendres = seg1.addToSendQueue(reinterpret_cast<u_int8_t*>(eventString1.data()), eventString1.length(), seg1Base + i);
            BOOST_CHECK(!sendres.has_error());
            sendStats = seg1.getSendStats();
            if (sendStats.get<1>() != 0) 
            {
                std::cout << "Error encountered sending event frames in segmenter1: " << strerror(sendStats.get<2>()) << std::endl;
            }
            sendres = seg2.addToSendQueue(reinterpret_cast<u_int8_t*>(eventString2.data()), eventString2.length(), seg2Base + i);
            BOOST_CHECK(!sendres.has_error());
            sendStats = seg1.getSendStats();
            if (sendStats.get<1>() != 0) 
            {
                std::cout << "Error encountered sending event frames in segmenter2: " << strerror(sendStats.get<2>()) << std::endl;
            }
            sendres = seg3.addToSendQueue(reinterpret_cast<u_int8_t*>(eventString3.data()), eventString3.length(), seg3Base + i);
            BOOST_CHECK(!sendres.has_error());
            sendStats = seg1.getSendStats();
            if (sendStats.get<1>() != 0) 
            {
                std::cout << "Error encountered sending event frames in segmenter3: " << strerror(sendStats.get<2>()) << std::endl;
            }
            sendres = seg4.addToSendQueue(reinterpret_cast<u_int8_t*>(eventString4.data()), eventString4.length(), seg4Base + i);
            BOOST_CHECK(!sendres.has_error());
            sendStats = seg1.getSendStats();
            if (sendStats.get<1>() != 0) 
            {
                std::cout << "Error encountered sending event frames in segmenter4: " << strerror(sendStats.get<2>()) << std::endl;
            }
            // sleep for a 100ms
            boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
        }

        // check the send stats
        sendStats = seg1.getSendStats();

        std::cout << "Segmenter 1 sent " << sendStats.get<0>() << " data frames" << std::endl;
        // send 5 event messages and no errors
        BOOST_CHECK(sendStats.get<0>() == 5);
        BOOST_CHECK(sendStats.get<1>() == 0);

        // check the send stats
        sendStats = seg2.getSendStats();

        std::cout << "Segmenter 2 sent " << sendStats.get<0>() << " data frames" << std::endl;
        // send 5 event messages and no errors
        BOOST_CHECK(sendStats.get<0>() == 5);
        BOOST_CHECK(sendStats.get<1>() == 0);

        // check the send stats
        sendStats = seg3.getSendStats();

        std::cout << "Segmenter 3 sent " << sendStats.get<0>() << " data frames" << std::endl;
        // send 5 event messages and no errors
        BOOST_CHECK(sendStats.get<0>() == 5);
        BOOST_CHECK(sendStats.get<1>() == 0);

        // check the send stats
        sendStats = seg4.getSendStats();

        std::cout << "Segmenter 4 sent " << sendStats.get<0>() << " data frames" << std::endl;
        // send 5 event messages and no errors
        BOOST_CHECK(sendStats.get<0>() == 5);
        BOOST_CHECK(sendStats.get<1>() == 0);

        u_int8_t *eventBuf{nullptr};
        size_t eventLen;
        EventNum_t eventNum;
        u_int16_t recDataId;

        // receive data from the queue (20 events expected)
        std::cout << "Expecting 20 events to be received" << std::endl;
        for(auto i=0; i<20; i++)
        {
            auto recvres = reas.getEvent(&eventBuf, &eventLen, &eventNum, &recDataId);
            if (recvres.has_error())
                std::cout << "Error encountered receiving event frames" << std::endl;
            if (recvres.value() == -1)
                std::cout << "No message received, continuing" << std::endl;
            else
                std::cout << "Received message: " << reinterpret_cast<char*>(eventBuf) << " of length " << eventLen << " with event number " << eventNum << " and data id " << recDataId << std::endl;
        }

        auto recvStats = reas.getStats();
        /*
             *  - EventNum_t enqueueLoss;  // number of events received and lost on enqueue
             *  - EventNum_t reassemblyLoss; // number of events lost in reassembly
             *  - EventNum_t eventSuccess; // events successfully processed
             *  - int lastErrno; 
             *  - int grpcErrCnt; 
             *  - int dataErrCnt; 
             *  - E2SARErrorc lastE2SARError; 
        */
        BOOST_CHECK(recvStats.get<0>() == 0); // no enque losses
        BOOST_CHECK(recvStats.get<1>() == 0); // no enque losses
        BOOST_CHECK(recvStats.get<2>() == 20); // all succeeded
        BOOST_CHECK(recvStats.get<3>() == 0); // no errno
        BOOST_CHECK(recvStats.get<4>() == 0); // no grpc errors
        BOOST_CHECK(recvStats.get<5>() == 0); // no data errors
        BOOST_CHECK(recvStats.get<6>() == E2SARErrorc::NoError); // no error

        auto lostEvent = reas.get_LostEvent();
        if (lostEvent.has_error())
            std::cout << "NO EVENT LOSS " << std::endl;
        else
            std::cout << "LOST EVENT " << lostEvent.value().first << ":" << lostEvent.value().second << std::endl;
        BOOST_CHECK(lostEvent.has_error() && lostEvent.error().code() == E2SARErrorc::NotFound);

        // stop threads and exit
    }
    catch (E2SARException &ee) {
        std::cout << "Exception encountered: " << static_cast<std::string>(ee) << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::exception &e) {
        std::cout << "STD:EXCEPTION encountered " << typeid(e).name() << ": " << e.what() << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::error_code &e) {
        std::cout << "STD ERROR CODE " << e.value() << " category " << e.category().name() << " with message " << e.message() << std::endl;
        BOOST_CHECK(false);
    }
    catch (boost::exception &e) {
        std::cout << "BOOST:EXCEPTION encountered " << typeid(e).name() << ": " << *boost::get_error_info<boost::throw_function>(e) << std::endl;
        BOOST_CHECK(false);
    }
    catch (...) {
        std::cout << "Some other exception" << std::endl;
        BOOST_CHECK(false);
    }
}

BOOST_AUTO_TEST_CASE(DPReasTest5)
{
    // test reading SegmenterFlags from INI files
    // generate a file, read it in and compare expected values
    boost::property_tree::ptree paramTree;
    Reassembler::ReassemblerFlags rFlags;
    std::string iniFileName = "/tmp/reassembler.ini";

    // fill in the parameters
    paramTree.put<bool>("general.useCP", false);
    paramTree.put<bool>("control-plane.useHostAddress", true);
    paramTree.put<int>("data-plane.rcvSocketBufSize", 10000);

    try {
        boost::property_tree::ini_parser::write_ini(iniFileName, paramTree);
    } catch(boost::property_tree::ini_parser_error &ie) {
        std::cout << "Unable to parse the segmenter flags configuration file "s + iniFileName << std::endl;
        BOOST_CHECK(false);
    }

    Reassembler::ReassemblerFlags segDefaults;
    Reassembler::ReassemblerFlags readFlags;
    auto res = Reassembler::ReassemblerFlags::getFromINI(iniFileName);
    BOOST_CHECK(!res.has_error());
    readFlags = res.value();

    BOOST_CHECK(readFlags.useCP == paramTree.get<bool>("general.useCP"));
    BOOST_CHECK(readFlags.useHostAddress == paramTree.get<bool>("control-plane.useHostAddress"));
    BOOST_CHECK(readFlags.validateCert == segDefaults.validateCert);
    BOOST_CHECK(readFlags.rcvSocketBufSize == paramTree.get<int>("data-plane.rcvSocketBufSize"));

    std::remove(iniFileName.c_str());
}

BOOST_AUTO_TEST_SUITE_END()