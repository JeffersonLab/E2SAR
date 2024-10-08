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

BOOST_AUTO_TEST_SUITE(DPReasTests)

// these tests test the reassembler background functionality
// no dataplane traffic is exchanged, but it requires the UDPLBd to be up to send messages to
BOOST_AUTO_TEST_CASE(DPReasTest1)
{
    std::cout << "DPReasTest1: test reassembler and send state thread " << std::endl;
    // parse URI from env variable
    // it needs to have the sync address/port
    auto uri_r = EjfatURI::getFromEnv();

    if (uri_r.has_error())
        std::cout << "URI Error: " << uri_r.error().message() << std::endl;
    BOOST_CHECK(!uri_r.has_error());

    auto uri = uri_r.value();

    Reassembler::ReassemblerFlags rflags;
    rflags.validateCert = false;

    ip::address loopback = ip::make_address("127.0.0.1");
    u_int16_t listen_port = 10000;
    // create a reassembler and start the threads
    Reassembler reas(uri, loopback, listen_port, 1, rflags);

    auto oas_r = reas.openAndStart();

    if (oas_r.has_error())
        std::cout << "Error encountered opening sockets and starting threads: " << oas_r.error().message() << std::endl;
    BOOST_CHECK(!oas_r.has_error());

    // sleep for 5 seconds
    boost::this_thread::sleep_for(boost::chrono::seconds(5));

    // check the sync stats
    auto recvStats = reas.getStats();

    if (recvStats.get<0>() != 0) 
    {
        std::cout << "Unexpected enqueue loss: " << strerror(recvStats.get<0>()) << std::endl;
    }
    // enqueue loss
    BOOST_CHECK(recvStats.get<0>() == 0);
    // gRPC error count
    BOOST_CHECK(recvStats.get<3>() == 0);
    // data error count
    BOOST_CHECK(recvStats.get<4>() == 0);

    auto lostEvent = reas.get_LostEvent();
    if (lostEvent.has_error())
        std::cout << "NO EVENT LOSS " << std::endl;
    else
        std::cout << "LOST EVENT " << lostEvent.value().first << ":" << lostEvent.value().second << std::endl;
    BOOST_CHECK(lostEvent.has_error() && lostEvent.error().code() == E2SARErrorc::NotFound);

    // stop threads and exit
}

BOOST_AUTO_TEST_SUITE_END()