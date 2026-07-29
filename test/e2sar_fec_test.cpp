#define BOOST_TEST_MODULE DPFecTests
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>
#include <boost/test/included/unit_test.hpp>

#include "e2sar.hpp"

using namespace e2sar;
namespace ip = boost::asio::ip;

// ---------------------------------------------------------------------------
// Proxy: sits between segmenter and reassembler, drops FEC data segment 0
// from each FEC block to force FEC recovery.
//
// Packet layout on the wire (UDP payload, after kernel strips IP+UDP):
//   [0..15]  LBHdrU  (16 bytes)
//   [16..31] ECHdr   (16 bytes) — magic 'E','C' at [16..17],
//                                  pFrameNum at [22] (P bit | ecFrameNum)
//   [32..]   payload (data or parity slice)
// ---------------------------------------------------------------------------
struct FecColumnDropProxy {
    int listenfd{-1};
    int fwdfd{-1};
    uint16_t fwdPort;
    std::atomic<bool> running{true};
    boost::thread worker;

    FecColumnDropProxy(uint16_t listenPort, uint16_t forwardPort)
        : fwdPort(forwardPort)
    {
        listenfd = ::socket(AF_INET, SOCK_DGRAM, 0);
        fwdfd    = ::socket(AF_INET, SOCK_DGRAM, 0);
        BOOST_REQUIRE(listenfd >= 0);
        BOOST_REQUIRE(fwdfd >= 0);

        int one = 1;
        ::setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        // 100 ms receive timeout so the loop checks running periodically
        struct timeval tv{};
        tv.tv_usec = 100000;
        ::setsockopt(listenfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(listenPort);
        addr.sin_addr.s_addr = INADDR_ANY;
        int rc = ::bind(listenfd,
                        reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        BOOST_REQUIRE(rc == 0);

        worker = boost::thread([this]{ loop(); });
    }

    ~FecColumnDropProxy() {
        running = false;
        worker.join();
        if (listenfd >= 0) ::close(listenfd);
        if (fwdfd    >= 0) ::close(fwdfd);
    }

    void loop() {
        uint8_t buf[65536];
        sockaddr_in dst{};
        dst.sin_family      = AF_INET;
        dst.sin_port        = htons(fwdPort);
        dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        while (running) {
            ssize_t n = ::recv(listenfd, buf, sizeof(buf), 0);
            if (n <= 0) continue;

            // FEC data packet: magic 'E','C' at LBHdrU-end (offset 16,17)
            // pFrameNum at offset 22: bit7=parity, bits6-0=ecFrameNum (segment#)
            if (n >= 23 && buf[16] == 'E' && buf[17] == 'C') {
                bool isParity = (buf[22] >> 7) & 1u;
                uint8_t seg   =  buf[22] & 0x7Fu;
                if (!isParity && seg == 0) continue; // drop column-0 data segment
            }

            ::sendto(fwdfd, buf, static_cast<size_t>(n), 0,
                     reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
        }
    }
};

BOOST_AUTO_TEST_SUITE(DPFecTests)

// ---------------------------------------------------------------------------
// DPFecTest1: loopback, both sides FEC=true, no loss.
// Verifies the FEC encode/decode path completes without errors.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(DPFecTest1)
{
    std::cout << "DPFecTest1: FEC loopback no-loss, 64 KB, MTU=1500, both sides FEC=true" << std::endl;

    std::string segUri {"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:20200"};
    std::string reasUri{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};

    try {
        EjfatURI su(segUri,  EjfatURI::TokenType::instance);
        EjfatURI ru(reasUri, EjfatURI::TokenType::instance);

        Segmenter::SegmenterFlags sflags;
        sflags.useCP     = false;
        sflags.mtu       = 1500;
        sflags.rateGbps  = -1.0;  // unlimited
        sflags.enableFec = true;

        Reassembler::ReassemblerFlags rflags;
        rflags.useCP          = false;
        rflags.withLBHeader   = true;
        rflags.enableFec      = true;
        rflags.eventTimeout_ms = 1000;

        Segmenter   seg (su, 0x0FEC, 0xFEC00001, sflags);
        Reassembler reas(ru, ip::make_address("127.0.0.1"), 20200, 1, rflags);

        auto r1 = seg.openAndStart();
        if (r1.has_error()) std::cout << "openAndStart seg: " << r1.error().message() << std::endl;
        BOOST_REQUIRE(!r1.has_error());

        auto r2 = reas.openAndStart();
        if (r2.has_error()) std::cout << "openAndStart reas: " << r2.error().message() << std::endl;
        BOOST_REQUIRE(!r2.has_error());

        const size_t payloadSize = 64 * 1024;
        std::vector<uint8_t> payload(payloadSize, 0xAB);

        auto sr = seg.addToSendQueue(payload.data(), payloadSize, 1);
        if (sr.has_error()) std::cout << "addToSendQueue: " << sr.error().message() << std::endl;
        BOOST_REQUIRE(!sr.has_error());

        uint8_t  *evtBuf{nullptr};
        size_t    evtLen{0};
        EventNum_t evtNum{0};
        uint16_t  recDataId{0};

        auto rr = reas.recvEvent(&evtBuf, &evtLen, &evtNum, &recDataId, 5000);
        if (rr.has_error()) std::cout << "recvEvent: " << rr.error().message() << std::endl;
        BOOST_REQUIRE(!rr.has_error());
        BOOST_CHECK_EQUAL(rr.value(), 0);
        BOOST_CHECK_EQUAL(evtLen, payloadSize);

        if (evtBuf) {
            bool ok = true;
            for (size_t i = 0; i < evtLen; ++i)
                if (evtBuf[i] != 0xAB) { ok = false; break; }
            BOOST_CHECK_MESSAGE(ok, "payload corrupted");
            delete[] evtBuf;
        }

        auto st = reas.getStats();
        BOOST_CHECK_EQUAL(st.eventSuccess,    1);
        BOOST_CHECK_EQUAL(st.enqueueLoss,     0);
        BOOST_CHECK_EQUAL(st.reassemblyLoss,  0);
    }
    catch (E2SARException &e) {
        std::cout << "E2SARException: " << static_cast<std::string>(e) << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::exception &e) {
        std::cout << "std::exception: " << e.what() << std::endl;
        BOOST_CHECK(false);
    }
    catch (...) {
        std::cout << "Unknown exception" << std::endl;
        BOOST_CHECK(false);
    }
}

// ---------------------------------------------------------------------------
// DPFecTest2: loopback through a proxy that drops FEC data segment 0 of
// every FEC block.  Verifies the reassembler's FEC recovery path fires
// (fecRecoveries > 0) and the event is reassembled correctly.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(DPFecTest2)
{
    std::cout << "DPFecTest2: FEC loopback via proxy dropping seg-0, 64 KB, both sides FEC=true" << std::endl;

    // segmenter → proxy:20211 → reassembler:20201
    const uint16_t proxyPort = 20211;
    const uint16_t reasPort  = 20201;

    std::string segUri {"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:" + std::to_string(proxyPort)};
    std::string reasUri{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};

    try {
        FecColumnDropProxy proxy(proxyPort, reasPort);

        EjfatURI su(segUri,  EjfatURI::TokenType::instance);
        EjfatURI ru(reasUri, EjfatURI::TokenType::instance);

        Segmenter::SegmenterFlags sflags;
        sflags.useCP     = false;
        sflags.mtu       = 1500;
        sflags.rateGbps  = -1.0;
        sflags.enableFec = true;

        Reassembler::ReassemblerFlags rflags;
        rflags.useCP           = false;
        rflags.withLBHeader    = true;
        rflags.enableFec       = true;
        rflags.eventTimeout_ms = 1000;

        Segmenter   seg (su, 0x0FEC, 0xFEC00002, sflags);
        Reassembler reas(ru, ip::make_address("127.0.0.1"), reasPort, 1, rflags);

        auto r1 = seg.openAndStart();
        if (r1.has_error()) std::cout << "openAndStart seg: " << r1.error().message() << std::endl;
        BOOST_REQUIRE(!r1.has_error());

        auto r2 = reas.openAndStart();
        if (r2.has_error()) std::cout << "openAndStart reas: " << r2.error().message() << std::endl;
        BOOST_REQUIRE(!r2.has_error());

        const size_t payloadSize = 64 * 1024;
        std::vector<uint8_t> payload(payloadSize, 0xCD);

        auto sr = seg.addToSendQueue(payload.data(), payloadSize, 1);
        if (sr.has_error()) std::cout << "addToSendQueue: " << sr.error().message() << std::endl;
        BOOST_REQUIRE(!sr.has_error());

        uint8_t   *evtBuf{nullptr};
        size_t     evtLen{0};
        EventNum_t evtNum{0};
        uint16_t   recDataId{0};

        // Allow extra time for GC thread to fire and recover the missing segments
        auto rr = reas.recvEvent(&evtBuf, &evtLen, &evtNum, &recDataId, 6000);
        if (rr.has_error()) std::cout << "recvEvent: " << rr.error().message() << std::endl;
        BOOST_REQUIRE(!rr.has_error());
        BOOST_CHECK_EQUAL(rr.value(), 0);
        BOOST_CHECK_EQUAL(evtLen, payloadSize);

        if (evtBuf) {
            bool ok = true;
            for (size_t i = 0; i < evtLen; ++i)
                if (evtBuf[i] != 0xCD) { ok = false; break; }
            BOOST_CHECK_MESSAGE(ok, "payload corrupted after FEC recovery");
            delete[] evtBuf;
        }

        auto st = reas.getStats();
        BOOST_CHECK_EQUAL(st.eventSuccess,   1);
        BOOST_CHECK_EQUAL(st.enqueueLoss,    0);
        BOOST_CHECK_GT(st.fecRecoveries, static_cast<EventNum_t>(0));
        std::cout << "  fecRecoveries=" << st.fecRecoveries
                  << " fecFailures="   << st.fecFailures << std::endl;
    }
    catch (E2SARException &e) {
        std::cout << "E2SARException: " << static_cast<std::string>(e) << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::exception &e) {
        std::cout << "std::exception: " << e.what() << std::endl;
        BOOST_CHECK(false);
    }
    catch (...) {
        std::cout << "Unknown exception" << std::endl;
        BOOST_CHECK(false);
    }
}

// ---------------------------------------------------------------------------
// DPFecTest3: sender FEC=false, receiver FEC=true.
// Verifies graceful fallback: non-FEC packets are reassembled normally even
// when the receiver has FEC enabled.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(DPFecTest3)
{
    std::cout << "DPFecTest3: sender FEC=false, receiver FEC=true — graceful fallback" << std::endl;

    std::string segUri {"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:20202"};
    std::string reasUri{"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"};

    try {
        EjfatURI su(segUri,  EjfatURI::TokenType::instance);
        EjfatURI ru(reasUri, EjfatURI::TokenType::instance);

        Segmenter::SegmenterFlags sflags;
        sflags.useCP     = false;
        sflags.mtu       = 1500;
        sflags.rateGbps  = -1.0;
        sflags.enableFec = false;  // no FEC on sender

        Reassembler::ReassemblerFlags rflags;
        rflags.useCP           = false;
        rflags.withLBHeader    = true;
        rflags.enableFec       = true;  // FEC enabled on receiver
        rflags.eventTimeout_ms = 1000;

        Segmenter   seg (su, 0x0FEC, 0xFEC00003, sflags);
        Reassembler reas(ru, ip::make_address("127.0.0.1"), 20202, 1, rflags);

        auto r1 = seg.openAndStart();
        if (r1.has_error()) std::cout << "openAndStart seg: " << r1.error().message() << std::endl;
        BOOST_REQUIRE(!r1.has_error());

        auto r2 = reas.openAndStart();
        if (r2.has_error()) std::cout << "openAndStart reas: " << r2.error().message() << std::endl;
        BOOST_REQUIRE(!r2.has_error());

        const size_t payloadSize = 64 * 1024;
        std::vector<uint8_t> payload(payloadSize, 0xEF);

        auto sr = seg.addToSendQueue(payload.data(), payloadSize, 1);
        if (sr.has_error()) std::cout << "addToSendQueue: " << sr.error().message() << std::endl;
        BOOST_REQUIRE(!sr.has_error());

        uint8_t   *evtBuf{nullptr};
        size_t     evtLen{0};
        EventNum_t evtNum{0};
        uint16_t   recDataId{0};

        auto rr = reas.recvEvent(&evtBuf, &evtLen, &evtNum, &recDataId, 5000);
        if (rr.has_error()) std::cout << "recvEvent: " << rr.error().message() << std::endl;
        BOOST_REQUIRE(!rr.has_error());
        BOOST_CHECK_EQUAL(rr.value(), 0);
        BOOST_CHECK_EQUAL(evtLen, payloadSize);

        if (evtBuf) {
            bool ok = true;
            for (size_t i = 0; i < evtLen; ++i)
                if (evtBuf[i] != 0xEF) { ok = false; break; }
            BOOST_CHECK_MESSAGE(ok, "payload corrupted in FEC-off sender / FEC-on receiver test");
            delete[] evtBuf;
        }

        auto st = reas.getStats();
        BOOST_CHECK_EQUAL(st.eventSuccess,   1);
        BOOST_CHECK_EQUAL(st.enqueueLoss,    0);
        BOOST_CHECK_EQUAL(st.reassemblyLoss, 0);
        BOOST_CHECK_EQUAL(st.fecRecoveries,  static_cast<EventNum_t>(0));
    }
    catch (E2SARException &e) {
        std::cout << "E2SARException: " << static_cast<std::string>(e) << std::endl;
        BOOST_CHECK(false);
    }
    catch (std::exception &e) {
        std::cout << "std::exception: " << e.what() << std::endl;
        BOOST_CHECK(false);
    }
    catch (...) {
        std::cout << "Unknown exception" << std::endl;
        BOOST_CHECK(false);
    }
}

BOOST_AUTO_TEST_SUITE_END()
