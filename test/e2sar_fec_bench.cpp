// e2sar_fec_bench.cpp — B2B throughput benchmark: no-FEC vs FEC (scalar vs NEON)
//
// Sends N events of configurable size through a Segmenter→Reassembler loopback
// (no load balancer, no control plane) and measures end-to-end throughput.
//
// Configurations tested:
//   1. No FEC       — baseline data path
//   2. FEC scalar   — FEC enabled, scalar interleave + rs_encode
//   3. FEC NEON     — FEC enabled, NEON tiled interleave + rs_encode (ARM only)
//
// Since the Segmenter dispatches NEON vs scalar at compile time, configurations
// 2 and 3 are the same binary on ARM (NEON is always used). To compare scalar
// vs NEON, a standalone FEC-only microbenchmark is included that calls both
// code paths directly.
//
// Usage:
//   ./e2sar_fec_bench [--events N] [--size BYTES] [--mtu MTU] [--port PORT]
//                     [--threads N] [--timeout MS]

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include "e2sar.hpp"

#include "fec/fec_block.h"
#include "fec/interleaver.h"
#include "fec/deinterleaver.h"
#include "fec/rs_encode.h"

using namespace e2sar;

struct BenchConfig {
    int      numEvents    = 100;
    size_t   eventSize    = 1024 * 1024;  // 1 MiB
    uint16_t mtu          = 9000;
    uint16_t port         = 11000;
    int      recvThreads  = 1;
    int      timeoutMs    = 5000;
    float    rateGbps     = -1.0f;
};

static BenchConfig parseArgs(int argc, char** argv) {
    BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--events" && i + 1 < argc)       cfg.numEvents   = std::atoi(argv[++i]);
        else if (arg == "--size" && i + 1 < argc)     cfg.eventSize   = std::atol(argv[++i]);
        else if (arg == "--mtu" && i + 1 < argc)      cfg.mtu         = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--port" && i + 1 < argc)     cfg.port        = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--threads" && i + 1 < argc)  cfg.recvThreads = std::atoi(argv[++i]);
        else if (arg == "--timeout" && i + 1 < argc)  cfg.timeoutMs   = std::atoi(argv[++i]);
        else if (arg == "--rate" && i + 1 < argc)     cfg.rateGbps    = std::atof(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: %s [--events N] [--size BYTES] [--mtu MTU] [--port PORT]\n"
                        "       [--threads N] [--timeout MS] [--rate GBPS]\n", argv[0]);
            std::exit(0);
        }
    }
    return cfg;
}

struct BenchResult {
    const char* label;
    int         eventsReceived;
    int         eventsSent;
    double      elapsedMs;
    double      totalBytes;
    size_t      packetsSent;
    size_t      packetsRecvd;
};

static void printResult(const BenchResult& r) {
    double gbps = r.totalBytes * 8.0 / (r.elapsedMs * 1e6);
    double eventRate = r.eventsReceived / (r.elapsedMs / 1000.0);
    std::printf("  %-28s %6d/%d events  %8.1f ms  %7.3f Gbps  %8.0f ev/s  "
                "%zu pkts sent  %zu pkts recv\n",
                r.label, r.eventsReceived, r.eventsSent, r.elapsedMs,
                gbps, eventRate, r.packetsSent, r.packetsRecvd);
}

static BenchResult runB2B(const BenchConfig& cfg, bool enableFec, const char* label) {
    uint16_t port = cfg.port;

    std::string segUriStr = "ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:" + std::to_string(port);
    std::string reasUriStr = "ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data=127.0.0.1";

    EjfatURI segUri(segUriStr, EjfatURI::TokenType::instance);
    EjfatURI reasUri(reasUriStr, EjfatURI::TokenType::instance);

    Segmenter::SegmenterFlags sflags;
    sflags.useCP = false;
    sflags.mtu = cfg.mtu;
    sflags.syncPeriodMs = 1000;
    sflags.syncPeriods = 2;
    sflags.warmUpMs = 0;
    sflags.numSendSockets = 1;
    sflags.rateGbps = cfg.rateGbps;
    sflags.enableFec = enableFec;

    uint16_t dataId = 0x0505;
    uint32_t eventSrcId = 0x11223344;
    Segmenter seg(segUri, dataId, eventSrcId, sflags);

    Reassembler::ReassemblerFlags rflags;
    rflags.useCP = false;
    rflags.withLBHeader = true;
    rflags.eventTimeout_ms = cfg.timeoutMs;
    rflags.enableFec = enableFec;

    ip::address loopback = ip::make_address("127.0.0.1");
    Reassembler reas(reasUri, loopback, port, cfg.recvThreads, rflags);

    auto r1 = seg.openAndStart();
    if (r1.has_error()) {
        std::fprintf(stderr, "Segmenter start failed: %s\n", r1.error().message().c_str());
        return {label, 0, 0, 0, 0, 0, 0};
    }

    auto r2 = reas.openAndStart();
    if (r2.has_error()) {
        std::fprintf(stderr, "Reassembler start failed: %s\n", r2.error().message().c_str());
        return {label, 0, 0, 0, 0, 0, 0};
    }

    // Prepare event payload with deterministic pattern
    std::vector<uint8_t> eventData(cfg.eventSize);
    std::mt19937 rng(42);
    for (auto& b : eventData) b = static_cast<uint8_t>(rng());

    // Send all events as fast as possible, timed from first send to last recv
    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < cfg.numEvents; ++i) {
        auto res = seg.sendEvent(eventData.data(), eventData.size(), static_cast<EventNum_t>(i + 1));
        if (res.has_error()) {
            std::fprintf(stderr, "sendEvent[%d] failed: %s\n", i, res.error().message().c_str());
            break;
        }
    }

    // Receive all events
    int received = 0;
    uint8_t* recvBuf = nullptr;
    size_t recvLen;
    EventNum_t recvEventNum;
    uint16_t recvDataId;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.timeoutMs * 2);

    while (received < cfg.numEvents && std::chrono::steady_clock::now() < deadline) {
        auto res = reas.recvEvent(&recvBuf, &recvLen, &recvEventNum, &recvDataId,
                                  static_cast<uint64_t>(cfg.timeoutMs));
        if (res.has_error()) break;
        if (res.value() == 0) {
            received++;
            free(recvBuf);
            recvBuf = nullptr;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    auto sendStats = seg.getSendStats();
    auto recvStats = reas.getStats();

    reas.stopThreads();
    seg.stopThreads();

    BenchResult result;
    result.label = label;
    result.eventsReceived = received;
    result.eventsSent = cfg.numEvents;
    result.elapsedMs = elapsedMs;
    result.totalBytes = static_cast<double>(received) * cfg.eventSize;
    result.packetsSent = sendStats.msgCnt;
    result.packetsRecvd = recvStats.totalPackets;
    return result;
}

// Standalone FEC microbenchmark: measures interleave + rs_encode throughput
// with both scalar and NEON implementations directly.
static void runFecMicrobench(const BenchConfig& cfg) {
    // Derive col_height from MTU the same way the Segmenter does
    const size_t fecOverhead = 20 + 8 + 16 + 8 + 20; // IP + UDP + LBHdr + ECHdr + REHdr (approx)
    const size_t colHeight = cfg.mtu > fecOverhead ? cfg.mtu - fecOverhead : 1400;
    const size_t maxUserData = colHeight - 20; // minus REHdr

    const size_t numSegs = (cfg.eventSize + maxUserData - 1) / maxUserData;
    const size_t numBlocks = (numSegs + 31) / 32;

    fec::FecBlockParams params;
    params.col_height = static_cast<uint32_t>(colHeight);

    std::vector<uint8_t> segBuf(params.segment_buf_size(), 0);
    std::vector<uint8_t> cwBuf(params.codeword_buf_size(), 0);
    std::vector<uint8_t> parityBuf(8 * colHeight, 0);

    // Fill with pattern
    std::mt19937 rng(42);
    for (auto& b : segBuf) b = static_cast<uint8_t>(rng());

    const int N_ITERS = 50;
    const double dataMB = static_cast<double>(params.segment_buf_size()) * numBlocks / (1024.0 * 1024.0);

    auto bench = [&](auto fn, const char* label) {
        fn(); // warm-up
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N_ITERS; ++i) fn();
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / N_ITERS;
        double gbps = (dataMB * 1024.0 * 1024.0 * 8.0) / (ms * 1e6);
        std::printf("    %-40s %8.2f ms  %7.3f Gbps\n", label, ms, gbps);
    };

    std::printf("\n  FEC Microbenchmark (per-event encode pipeline)\n");
    std::printf("    event_size=%zu  col_height=%zu  blocks/event=%zu  segs/event=%zu\n",
                cfg.eventSize, colHeight, numBlocks, numSegs);
    std::printf("    %s\n", std::string(64, '-').c_str());

    // Scalar pipeline
    bench([&] {
        for (size_t b = 0; b < numBlocks; ++b) {
            std::memset(cwBuf.data(), 0, cwBuf.size());
            fec::interleave(segBuf, cwBuf, params);
            fec::rs_encode(cwBuf, params);
            std::memset(parityBuf.data(), 0, parityBuf.size());
            fec::deinterleave_parity(cwBuf, parityBuf, params);
        }
    }, "scalar (interleave+encode+deparity)");

#if defined(__ARM_NEON)
    // NEON pipeline (basic interleave)
    bench([&] {
        for (size_t b = 0; b < numBlocks; ++b) {
            std::memset(cwBuf.data(), 0, cwBuf.size());
            fec::interleave_neon(segBuf, cwBuf, params);
            fec::rs_encode_neon(cwBuf, params);
            std::memset(parityBuf.data(), 0, parityBuf.size());
            fec::deinterleave_parity_neon(cwBuf, parityBuf, params);
        }
    }, "NEON (interleave+encode+deparity)");

    // NEON tiled pipeline
    bench([&] {
        for (size_t b = 0; b < numBlocks; ++b) {
            std::memset(cwBuf.data(), 0, cwBuf.size());
            fec::interleave_neon_tiled(segBuf, cwBuf, params);
            fec::rs_encode_neon(cwBuf, params);
            std::memset(parityBuf.data(), 0, parityBuf.size());
            fec::deinterleave_parity_neon(cwBuf, parityBuf, params);
        }
    }, "NEON tiled (interleave+encode+deparity)");

    // Speedup summary
    {
        auto measure = [&](auto fn) {
            fn();
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < N_ITERS; ++i) fn();
            auto t1 = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(t1 - t0).count() / N_ITERS;
        };

        double ms_scalar = measure([&] {
            for (size_t b = 0; b < numBlocks; ++b) {
                std::memset(cwBuf.data(), 0, cwBuf.size());
                fec::interleave(segBuf, cwBuf, params);
                fec::rs_encode(cwBuf, params);
            }
        });
        double ms_neon = measure([&] {
            for (size_t b = 0; b < numBlocks; ++b) {
                std::memset(cwBuf.data(), 0, cwBuf.size());
                fec::interleave_neon_tiled(segBuf, cwBuf, params);
                fec::rs_encode_neon(cwBuf, params);
            }
        });
        std::printf("    NEON tiled speedup vs scalar:          %.1fx\n", ms_scalar / ms_neon);
    }
#endif
}

int main(int argc, char** argv) {
    BenchConfig cfg = parseArgs(argc, argv);

    std::printf("E2SAR B2B FEC Throughput Benchmark\n");
    std::printf("  events=%d  event_size=%zu bytes (%.1f MiB)  mtu=%u  port=%u\n",
                cfg.numEvents, cfg.eventSize,
                static_cast<double>(cfg.eventSize) / (1024.0 * 1024.0),
                cfg.mtu, cfg.port);
    std::printf("  recv_threads=%d  timeout=%d ms  rate=%.1f Gbps\n",
                cfg.recvThreads, cfg.timeoutMs, cfg.rateGbps);
#if defined(__ARM_NEON)
    std::printf("  NEON: enabled (compile-time)\n");
#else
    std::printf("  NEON: not available\n");
#endif
    std::printf("\n");

    // Run the FEC microbenchmark first (no networking)
    runFecMicrobench(cfg);

    // Run B2B throughput tests
    std::printf("\n  B2B End-to-End Throughput (Segmenter -> loopback -> Reassembler)\n");
    std::printf("  %s\n", std::string(100, '-').c_str());

    // Test 1: No FEC
    {
        auto r = runB2B(cfg, false, "No FEC");
        printResult(r);
    }

    // Small delay to let sockets clear TIME_WAIT
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cfg.port += 10;

    // Test 2: With FEC (on ARM this uses NEON tiled, on x86 uses scalar)
    {
#if defined(__ARM_NEON)
        const char* label = "FEC (NEON tiled)";
#else
        const char* label = "FEC (scalar)";
#endif
        auto r = runB2B(cfg, true, label);
        printResult(r);

        // Print FEC overhead
        if (r.eventsReceived > 0) {
            // Compare packet counts: FEC sends data + parity segments
            std::printf("\n  FEC wire overhead:\n");
            // Expected: for each block of 32 data segs, 8 parity segs are added = 25%
            std::printf("    Expected overhead: ~25%% (8 parity per 32 data segments)\n");
        }
    }

    std::printf("\n");
    return 0;
}
