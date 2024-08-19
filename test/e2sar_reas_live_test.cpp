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
    u_int16_t dataId = 0x0505;
    u_int32_t eventSrcId = 0x11223344;

    Reassembler::ReassemblerFlags rflags;
    rflags.validateCert = false;

    // create a reassembler and start the threads
    Reassembler reas(uri, 1, rflags);

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

    // stop threads and exit
}

BOOST_AUTO_TEST_SUITE_END()