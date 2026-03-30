#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>
#include <thread>
#include <chrono>

#include "fec/fec_block.h"
#include "fec/interleaver.h"
#include "fec/deinterleaver.h"
#include "fec/pipeline.h"
#include "fec/rs_encode.h"

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT(cond, msg) \
    do { \
        if (cond) { \
            std::printf("  PASS  %s\n", (msg)); \
            ++g_pass; \
        } else { \
            std::printf("  FAIL  %s  (line %d)\n", (msg), __LINE__); \
            ++g_fail; \
        } \
    } while (0)

static void section(const char* name) {
    std::printf("\n=== %s ===\n", name);
}

// ---------------------------------------------------------------------------
// Test 1 — Round-trip: interleave then deinterleave must recover original data
// ---------------------------------------------------------------------------
static void test_roundtrip() {
    section("Round-trip test");

    // Use a small col_height so the test runs fast and is easy to reason about.
    fec::FecBlockParams p;
    p.col_height = 16;

    std::vector<uint8_t> src(p.segment_buf_size());
    std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
    std::vector<uint8_t> dst(p.segment_buf_size(), 0);

    // Fill segments with a recognisable pattern: byte value = segment_index XOR byte_index
    for (uint32_t seg = 0; seg < fec::FecBlockParams::NUM_SEGMENTS; ++seg) {
        for (uint32_t b = 0; b < p.col_height; ++b) {
            src[seg * p.col_height + b] = static_cast<uint8_t>(seg ^ b);
        }
    }

    fec::interleave(src, cw, p);
    fec::deinterleave(cw, dst, p);

    EXPECT(src == dst, "deinterleave(interleave(x)) == x");
}

// ---------------------------------------------------------------------------
// Test 2 — Bit-level: verify a specific bit lands in the correct symbol/word
// ---------------------------------------------------------------------------
static void test_bit_mapping() {
    section("Bit-level mapping test");

    fec::FecBlockParams p;
    p.col_height = 1;   // 1 byte/segment → 8 words

    std::vector<uint8_t> src(p.segment_buf_size(), 0);
    std::vector<uint8_t> cw(p.codeword_buf_size(), 0);

    // Set bit 7 (MSB, word 0) of segment 0 (column 0, bit-position 3 = MSB of symbol 0).
    // Expected: word 0, symbol 0, bit 3 (high nibble of byte 0 has bit 3 set) → nibble = 0x8
    src[0] = 0x80u;  // only bit 7 set in segment 0

    fec::interleave(src, cw, p);

    // Word 0 is at cw[0..4].
    // Symbol 0 occupies the high nibble of cw[0].
    uint8_t sym0_nibble = (cw[0] >> 4u) & 0xFu;
    EXPECT(sym0_nibble == 0x8u,
           "Bit7 of seg0 → word0, symbol0, nibble=0x8 (bit3 set)");

    // All other words (1–7) should have symbol 0 nibble = 0 (bit7 was only in word 0).
    bool others_zero = true;
    for (uint32_t w = 1; w < p.num_words(); ++w) {
        if (((cw[w * fec::FecBlockParams::CODEWORD_BYTES] >> 4u) & 0xFu) != 0u) {
            others_zero = false;
        }
    }
    EXPECT(others_zero, "Other words' symbol 0 nibble = 0");

    // Now test the LSB path: set bit 0 (LSB, word 7) of segment 3
    // (column 0, bit-position 0 = bit 0 of symbol 0).
    // Expected: word 7, symbol 0, bit 0 set → nibble = 0x1
    std::fill(src.begin(), src.end(), 0u);
    std::fill(cw.begin(),  cw.end(),  0u);
    src[3] = 0x01u;  // segment 3, bit 0 → word 7

    fec::interleave(src, cw, p);

    uint8_t word7_sym0 = (cw[7u * fec::FecBlockParams::CODEWORD_BYTES] >> 4u) & 0xFu;
    EXPECT(word7_sym0 == 0x1u,
           "Bit0 of seg3 → word7, symbol0, nibble=0x1 (bit0 set)");

    // Test column 1 / symbol 1: segment 4 (col=1, bit=3).
    // Set bit 7 of segment 4 → word 0, symbol 1, bit 3 → low nibble of cw[0] byte 0 = 0x8
    std::fill(src.begin(), src.end(), 0u);
    std::fill(cw.begin(),  cw.end(),  0u);
    src[4] = 0x80u;

    fec::interleave(src, cw, p);

    uint8_t word0_sym1 = cw[0] & 0xFu;  // symbol 1 is in low nibble of byte 0
    EXPECT(word0_sym1 == 0x8u,
           "Bit7 of seg4 → word0, symbol1, nibble=0x8");
}

// ---------------------------------------------------------------------------
// Test 3 — Codeword layout: parity byte is zero after interleave
// ---------------------------------------------------------------------------
static void test_parity_zeroed() {
    section("Parity byte zeroed after interleave");

    fec::FecBlockParams p;
    p.col_height = 8;

    std::vector<uint8_t> src(p.segment_buf_size(), 0xFFu);
    // Pre-fill codeword buffer with garbage to ensure interleave actually zeroes byte 4.
    std::vector<uint8_t> cw(p.codeword_buf_size(), 0xAAu);

    fec::interleave(src, cw, p);

    bool parity_zeroed = true;
    for (uint32_t w = 0; w < p.num_words(); ++w) {
        if (cw[w * fec::FecBlockParams::CODEWORD_BYTES + 4u] != 0u) {
            parity_zeroed = false;
            break;
        }
    }
    EXPECT(parity_zeroed, "Parity byte (codeword byte 4) is zero for all words");

    // Confirm data bytes 0–3 are non-zero (all-0xFF segments → all nibbles = 0xF).
    bool data_filled = true;
    for (uint32_t w = 0; w < p.num_words(); ++w) {
        const uint8_t* base = cw.data() + w * fec::FecBlockParams::CODEWORD_BYTES;
        if (base[0] != 0xFFu || base[1] != 0xFFu ||
            base[2] != 0xFFu || base[3] != 0xFFu) {
            data_filled = false;
            break;
        }
    }
    EXPECT(data_filled, "Data bytes 0–3 are 0xFF when all segments are 0xFF");
}

// ---------------------------------------------------------------------------
// Test 4 — Pipeline: 3 threads with StageBuffer synchronization
//   Thread A: interleave → marks cw_buf ready
//   Thread B: waits for cw_buf, simulates RS (copies to rs_out), marks rs_buf ready
//   Thread C: waits for rs_buf, deinterleaves → final result
// ---------------------------------------------------------------------------
static void test_pipeline() {
    section("Pipeline synchronization test");

    fec::FecBlockParams p;
    p.col_height = 32;

    // Caller owns all memory.
    std::vector<uint8_t> src(p.segment_buf_size());
    std::vector<uint8_t> cw_mem(p.codeword_buf_size(), 0);
    std::vector<uint8_t> rs_mem(p.codeword_buf_size(), 0);  // after simulated RS
    std::vector<uint8_t> dst(p.segment_buf_size(), 0);

    // Fill source with recognisable pattern.
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint8_t>(i * 7u + 13u);
    }

    fec::StageBuffer cw_buf(cw_mem);
    fec::StageBuffer rs_buf(rs_mem);

    // Thread A — interleaver
    std::thread ta([&]{
        fec::interleave(src, cw_buf.data(), p);
        cw_buf.mark_ready();
    });

    // Thread B — simulated RS encoder/decoder
    // In production this would call rs_encode(); here we just copy bytes 0–3
    // (data nibbles) across and zero byte 4 (parity), i.e. a no-op passthrough.
    std::thread tb([&]{
        cw_buf.wait_ready();
        // Simulate RS: copy codeword buffer as-is (parity already zeroed).
        std::memcpy(rs_buf.data().data(), cw_buf.data().data(), cw_buf.data().size());
        rs_buf.mark_ready();
    });

    // Thread C — deinterleaver
    std::thread tc([&]{
        rs_buf.wait_ready();
        fec::deinterleave(rs_buf.data(), dst, p);
    });

    ta.join();
    tb.join();
    tc.join();

    EXPECT(src == dst, "Pipeline round-trip: src == dst after interleave → RS (passthrough) → deinterleave");

    // Verify StageBuffer::is_ready() and reset().
    EXPECT(cw_buf.is_ready(), "cw_buf.is_ready() == true after pipeline");
    cw_buf.reset();
    EXPECT(!cw_buf.is_ready(), "cw_buf.is_ready() == false after reset()");
}

// ---------------------------------------------------------------------------
// Test 5 — NEON correctness: outputs must match scalar bit-for-bit
// ---------------------------------------------------------------------------
#if defined(__ARM_NEON)
static void test_neon_correctness() {
    section("NEON correctness vs scalar");

    fec::FecBlockParams p;
    p.col_height = 64;

    std::vector<uint8_t> src(p.segment_buf_size());
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint8_t>(i * 37u + 91u);
    }

    // Scalar reference
    std::vector<uint8_t> cw_scalar(p.codeword_buf_size(), 0);
    std::vector<uint8_t> dst_scalar(p.segment_buf_size(), 0);
    fec::interleave(src, cw_scalar, p);
    fec::deinterleave(cw_scalar, dst_scalar, p);

    // NEON
    std::vector<uint8_t> cw_neon(p.codeword_buf_size(), 0xBBu);  // pre-fill with garbage
    std::vector<uint8_t> dst_neon(p.segment_buf_size(), 0xCCu);
    fec::interleave_neon(src, cw_neon, p);
    fec::deinterleave_neon(cw_neon, dst_neon, p);

    EXPECT(cw_neon  == cw_scalar,  "interleave_neon output matches scalar");
    EXPECT(dst_neon == dst_scalar, "deinterleave_neon output matches scalar");
    EXPECT(dst_neon == src,        "NEON round-trip: deinterleave_neon(interleave_neon(x)) == x");
}
#endif // __ARM_NEON

// ---------------------------------------------------------------------------
// Test 5c — Metal correctness: interleave_metal output must match scalar
// ---------------------------------------------------------------------------
#if defined(__APPLE__)
static void test_metal_correctness() {
    section("Metal GPU correctness vs scalar");

    fec::FecBlockParams p;
    p.col_height = 64;

    std::vector<uint8_t> src(p.segment_buf_size());
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint8_t>(i * 37u + 91u);
    }

    // Scalar reference
    std::vector<uint8_t> cw_scalar(p.codeword_buf_size(), 0);
    fec::interleave(src, cw_scalar, p);

    // Metal variant (pre-fill with garbage to catch partial writes)
    std::vector<uint8_t> cw_metal(p.codeword_buf_size(), 0xBBu);
    fec::interleave_metal(src, cw_metal, p);

    EXPECT(cw_metal == cw_scalar, "interleave_metal output matches scalar");

    // Round-trip: scalar deinterleave recovers the original segments.
    std::vector<uint8_t> dst(p.segment_buf_size(), 0);
    fec::deinterleave(cw_metal, dst, p);
    EXPECT(dst == src, "Round-trip: deinterleave(interleave_metal(x)) == x");
}
#endif // __APPLE__

// ---------------------------------------------------------------------------
// Test 5b — Magic multiply correctness: interleave_magic must match scalar
// ---------------------------------------------------------------------------
static void test_magic_correctness() {
    section("Magic-multiply correctness vs scalar");

    fec::FecBlockParams p;
    p.col_height = 64;

    std::vector<uint8_t> src(p.segment_buf_size());
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint8_t>(i * 37u + 91u);
    }

    // Scalar reference
    std::vector<uint8_t> cw_scalar(p.codeword_buf_size(), 0);
    fec::interleave(src, cw_scalar, p);

    // Magic-multiply variant (pre-fill with garbage to catch partial writes)
    std::vector<uint8_t> cw_magic(p.codeword_buf_size(), 0xBBu);
    fec::interleave_magic(src, cw_magic, p);

    EXPECT(cw_magic == cw_scalar, "interleave_magic output matches scalar");

    // Full round-trip via scalar deinterleave
    std::vector<uint8_t> dst(p.segment_buf_size(), 0);
    fec::deinterleave(cw_magic, dst, p);
    EXPECT(dst == src, "Round-trip: deinterleave(interleave_magic(x)) == x");
}

// ---------------------------------------------------------------------------
// Helper: time one function over ITERS iterations, return ms/iter
// ---------------------------------------------------------------------------
template<typename Fn>
static double bench_ms(Fn fn, int iters) {
    // Warm-up
    fn();
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) { fn(); }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / iters;
}

// ---------------------------------------------------------------------------
// Test 6 — RS encoder correctness
//
// Constructs codewords directly (without interleaving) with known data symbols
// and verifies parity output against hand-computed GF(16) arithmetic.
//
// Reference values:
//   Encoding matrix parity columns P (from rs_model.h):
//     P col 0 = {14, 6, 14,  9,  7, 1, 15, 6}
//     P col 1 = { 5, 9,  4, 13,  8, 1,  5, 8}
//
//   All-zero data:     parity = {0, 0}   → byte 4 = 0x00
//   All-ones data:     parity = {0, 1}   → byte 4 = 0x01
//                      (XOR of P col 0 = 0, XOR of P col 1 = 1)
//   All-0xF data:      parity = {0, 15}  → byte 4 = 0x0F
//   Data {1..8}:       parity = {1, 5}   → byte 4 = 0x15
// ---------------------------------------------------------------------------
static void test_rs_encode() {
    section("RS encoder correctness");

    // Use col_height=1 → 8 codewords, simple to reason about.
    fec::FecBlockParams p;
    p.col_height = 1;

    const uint32_t nw  = p.num_words();   // 8
    const uint32_t cwb = fec::FecBlockParams::CODEWORD_BYTES;

    // Helper: set all codewords to the same 5-byte pattern and run encoder.
    auto fill_and_encode = [&](std::vector<uint8_t>& cw, uint8_t b0, uint8_t b1,
                                uint8_t b2, uint8_t b3) {
        for (uint32_t w = 0; w < nw; ++w) {
            uint8_t* c = cw.data() + w * cwb;
            c[0] = b0; c[1] = b1; c[2] = b2; c[3] = b3; c[4] = 0u;
        }
        fec::rs_encode(cw, p);
    };

    std::vector<uint8_t> cw(p.codeword_buf_size(), 0u);

    // ---- All-zero data → parity must be zero ----
    fill_and_encode(cw, 0x00, 0x00, 0x00, 0x00);
    bool zero_ok = true;
    for (uint32_t w = 0; w < nw; ++w) {
        if (cw[w * cwb + 4] != 0x00u) { zero_ok = false; break; }
    }
    EXPECT(zero_ok, "All-zero data → parity byte = 0x00");

    // ---- All-ones data (every nibble = 1) → parity byte = 0x01 ----
    fill_and_encode(cw, 0x11, 0x11, 0x11, 0x11);
    bool ones_ok = true;
    for (uint32_t w = 0; w < nw; ++w) {
        if (cw[w * cwb + 4] != 0x01u) { ones_ok = false; break; }
    }
    EXPECT(ones_ok, "All-ones data (nibble=1) → parity byte = 0x01");

    // ---- All-0xF data (every nibble = 15) → parity byte = 0x0F ----
    fill_and_encode(cw, 0xFF, 0xFF, 0xFF, 0xFF);
    bool f_ok = true;
    for (uint32_t w = 0; w < nw; ++w) {
        if (cw[w * cwb + 4] != 0x0Fu) { f_ok = false; break; }
    }
    EXPECT(f_ok, "All-0xF data (nibble=15) → parity byte = 0x0F");

    // ---- Data {1,2,3,4,5,6,7,8} → parity byte = 0x15 ----
    // Symbols packed: byte0=0x12, byte1=0x34, byte2=0x56, byte3=0x78
    fill_and_encode(cw, 0x12, 0x34, 0x56, 0x78);
    EXPECT(cw[4] == 0x15u, "Data {1,2,3,4,5,6,7,8} → parity byte = 0x15");

    // ---- Idempotency: encoding twice produces the same parity ----
    // (rs_encode reads bytes 0-3 only; byte 4 is overwritten, not read)
    fill_and_encode(cw, 0xAB, 0xCD, 0xEF, 0x01);
    std::vector<uint8_t> cw_copy = cw;
    fec::rs_encode(cw, p);  // encode again
    bool idem_ok = true;
    for (uint32_t w = 0; w < nw; ++w) {
        if (cw[w * cwb + 4] != cw_copy[w * cwb + 4]) { idem_ok = false; break; }
    }
    EXPECT(idem_ok, "Encoding is idempotent (second encode gives same parity)");

    // ---- Data bytes 0-3 are not modified ----
    fill_and_encode(cw, 0x12, 0x34, 0x56, 0x78);
    bool data_unchanged = true;
    for (uint32_t w = 0; w < nw; ++w) {
        const uint8_t* c = cw.data() + w * cwb;
        if (c[0] != 0x12u || c[1] != 0x34u || c[2] != 0x56u || c[3] != 0x78u) {
            data_unchanged = false; break;
        }
    }
    EXPECT(data_unchanged, "rs_encode does not modify data bytes 0-3");
}

// ---------------------------------------------------------------------------
// Test 7 — Metal RS encoder correctness: output must match scalar bit-for-bit
// ---------------------------------------------------------------------------
#if defined(__APPLE__)
static void test_rs_encode_metal() {
    section("Metal RS encoder correctness vs scalar");

    fec::FecBlockParams p;
    p.col_height = 64;

    std::vector<uint8_t> src(p.segment_buf_size());
    for (size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<uint8_t>(i * 37u + 91u);

    // Scalar reference: interleave then encode.
    std::vector<uint8_t> cw_ref(p.codeword_buf_size(), 0);
    fec::interleave(src, cw_ref, p);
    fec::rs_encode(cw_ref, p);

    // Metal: interleave then Metal encode (pre-fill with garbage to catch partial writes).
    std::vector<uint8_t> cw_metal(p.codeword_buf_size(), 0xBBu);
    fec::interleave(src, cw_metal, p);
    fec::rs_encode_metal(cw_metal, p);

    EXPECT(cw_metal == cw_ref, "rs_encode_metal parity bytes match scalar");

    // Larger block to exercise all codeword positions.
    p.col_height = 8192;
    cw_ref.assign(p.codeword_buf_size(), 0);
    src.resize(p.segment_buf_size());
    for (size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<uint8_t>(i * 13u + 7u);
    cw_metal.assign(p.codeword_buf_size(), 0);

    fec::interleave(src, cw_ref, p);
    fec::interleave(src, cw_metal, p);
    fec::rs_encode(cw_ref, p);
    fec::rs_encode_metal(cw_metal, p);

    EXPECT(cw_metal == cw_ref,
           "rs_encode_metal matches scalar at col_height=8192");
}
#endif // __APPLE__

// ---------------------------------------------------------------------------
// Test 7b — NEON RS encoder correctness: output must match scalar bit-for-bit
// ---------------------------------------------------------------------------
#if defined(__ARM_NEON)
static void test_rs_encode_neon() {
    section("NEON RS encoder correctness vs scalar");

    // Small block
    fec::FecBlockParams p;
    p.col_height = 64;

    std::vector<uint8_t> src(p.segment_buf_size());
    for (size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<uint8_t>(i * 37u + 91u);

    // Scalar reference: interleave then encode.
    std::vector<uint8_t> cw_ref(p.codeword_buf_size(), 0);
    fec::interleave(src, cw_ref, p);
    fec::rs_encode(cw_ref, p);

    // NEON: interleave then NEON encode (pre-fill byte 4 with garbage).
    std::vector<uint8_t> cw_neon(p.codeword_buf_size(), 0);
    fec::interleave(src, cw_neon, p);
    // Corrupt parity bytes to confirm NEON actually overwrites them.
    for (uint32_t w = 0; w < p.num_words(); ++w)
        cw_neon[w * fec::FecBlockParams::CODEWORD_BYTES + 4] = 0xBBu;
    fec::rs_encode_neon(cw_neon, p);

    EXPECT(cw_neon == cw_ref, "rs_encode_neon parity bytes match scalar (col_height=64)");

    // Larger block to exercise all codeword positions.
    p.col_height = 8192;
    src.resize(p.segment_buf_size());
    for (size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<uint8_t>(i * 13u + 7u);

    cw_ref.assign(p.codeword_buf_size(), 0);
    fec::interleave(src, cw_ref, p);
    fec::rs_encode(cw_ref, p);

    cw_neon.assign(p.codeword_buf_size(), 0);
    fec::interleave(src, cw_neon, p);
    fec::rs_encode_neon(cw_neon, p);

    EXPECT(cw_neon == cw_ref,
           "rs_encode_neon matches scalar at col_height=8192");
}
#endif // __ARM_NEON

// ---------------------------------------------------------------------------
// Test 8 — Combined interleave + rs_encode pipeline correctness
//
// Single-block: verifies each interleaver×encoder variant combo produces an
// identical codeword buffer (bytes 0-4) against the scalar+scalar reference.
//
// Batch (super-buffer): verifies that a super-buffer with col_height = N *
// per_block_ch extracts per-block results that match the scalar per-block
// reference.  The super-buffer layout is:
//   codewords[blk * p.codeword_buf_size() + byte_idx*40 + word_off]
//   = block[blk].codewords[byte_idx*40 + word_off]
// (same layout used by stream_bench.cpp and the Metal batch kernel)
// ---------------------------------------------------------------------------
static void test_pipeline_combined() {
    section("Combined interleave + rs_encode pipeline");

    static constexpr int N_BATCH = 4;  // small enough for fast unit test

    for (int pass = 0; pass < 2; ++pass) {
        fec::FecBlockParams p;
        p.col_height = (pass == 0) ? 64u : 8192u;

        std::printf("\n  --- Single-block, col_height = %u ---\n", p.col_height);

        std::vector<uint8_t> src(p.segment_buf_size());
        for (size_t i = 0; i < src.size(); ++i)
            src[i] = static_cast<uint8_t>(i * 37u + 91u);

        // Build scalar+scalar reference.
        std::vector<uint8_t> cw_ref(p.codeword_buf_size(), 0);
        fec::interleave(src, cw_ref, p);
        fec::rs_encode(cw_ref, p);

        char label[64];

        // ---- magic interleave + scalar encode ----
        {
            std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
            fec::interleave_magic(src, cw, p);
            fec::rs_encode(cw, p);
            std::snprintf(label, sizeof(label),
                          "magic+scalar: matches scalar ref (ch=%u)", p.col_height);
            EXPECT(cw == cw_ref, label);
        }

#if defined(__ARM_NEON)
        // ---- NEON interleave + NEON encode ----
        {
            std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
            fec::interleave_neon(src, cw, p);
            fec::rs_encode_neon(cw, p);
            std::snprintf(label, sizeof(label),
                          "neon+neon: matches scalar ref (ch=%u)", p.col_height);
            EXPECT(cw == cw_ref, label);
        }
        // ---- NEON interleave + scalar encode ----
        {
            std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
            fec::interleave_neon(src, cw, p);
            fec::rs_encode(cw, p);
            std::snprintf(label, sizeof(label),
                          "neon+scalar: matches scalar ref (ch=%u)", p.col_height);
            EXPECT(cw == cw_ref, label);
        }
        // ---- scalar interleave + NEON encode ----
        {
            std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
            fec::interleave(src, cw, p);
            fec::rs_encode_neon(cw, p);
            std::snprintf(label, sizeof(label),
                          "scalar+neon: matches scalar ref (ch=%u)", p.col_height);
            EXPECT(cw == cw_ref, label);
        }
#endif // __ARM_NEON

#if defined(__APPLE__)
        // ---- Metal interleave + Metal encode ----
        {
            std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
            fec::interleave_metal(src, cw, p);
            fec::rs_encode_metal(cw, p);
            std::snprintf(label, sizeof(label),
                          "metal+metal: matches scalar ref (ch=%u)", p.col_height);
            EXPECT(cw == cw_ref, label);
        }
        // ---- Metal interleave + scalar encode ----
        {
            std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
            fec::interleave_metal(src, cw, p);
            fec::rs_encode(cw, p);
            std::snprintf(label, sizeof(label),
                          "metal+scalar: matches scalar ref (ch=%u)", p.col_height);
            EXPECT(cw == cw_ref, label);
        }
        // ---- scalar interleave + Metal encode ----
        {
            std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
            fec::interleave(src, cw, p);
            fec::rs_encode_metal(cw, p);
            std::snprintf(label, sizeof(label),
                          "scalar+metal: matches scalar ref (ch=%u)", p.col_height);
            EXPECT(cw == cw_ref, label);
        }
#endif // __APPLE__
    } // pass (col_height 64, 8192)

    // -----------------------------------------------------------------------
    // Batch (super-buffer) mode — N_BATCH blocks
    // -----------------------------------------------------------------------
    std::printf("\n  --- Batch mode, %d blocks, col_height = 64 ---\n", N_BATCH);
    {
        fec::FecBlockParams p;
        p.col_height = 64u;  // keep fast for a unit test

        const uint32_t total_ch = uint32_t{N_BATCH} * p.col_height;

        // Per-block source data with distinct patterns.
        std::vector<std::vector<uint8_t>> src(N_BATCH,
            std::vector<uint8_t>(p.segment_buf_size()));
        for (int blk = 0; blk < N_BATCH; ++blk)
            for (size_t i = 0; i < src[blk].size(); ++i)
                src[blk][i] = static_cast<uint8_t>((i + size_t(blk) * 17u) * 37u + 91u);

        // Per-block scalar reference (interleave + rs_encode).
        std::vector<std::vector<uint8_t>> ref(N_BATCH,
            std::vector<uint8_t>(p.codeword_buf_size(), 0));
        for (int blk = 0; blk < N_BATCH; ++blk) {
            fec::interleave(src[blk], ref[blk], p);
            fec::rs_encode(ref[blk], p);
        }

        // Build segment super-buffer:
        //   super_seg[s * total_ch + blk * col_height .. +col_height-1]
        //   = src[blk][s * col_height .. +col_height-1]
        const size_t super_seg_sz =
            size_t{fec::FecBlockParams::NUM_SEGMENTS} * total_ch;
        const size_t super_cw_sz  =
            size_t{total_ch} * 8u * fec::FecBlockParams::CODEWORD_BYTES;

        auto build_super_seg = [&](std::vector<uint8_t>& ss) {
            ss.assign(super_seg_sz, 0u);
            for (int blk = 0; blk < N_BATCH; ++blk)
                for (uint32_t s = 0; s < fec::FecBlockParams::NUM_SEGMENTS; ++s)
                    std::memcpy(ss.data() + size_t{s} * total_ch
                                          + size_t(blk) * p.col_height,
                                src[blk].data() + size_t{s} * p.col_height,
                                p.col_height);
        };

        // Helper: check that each block's slice in the super-codeword buffer
        // matches the per-block scalar reference.
        auto check_batch = [&](const std::vector<uint8_t>& scw,
                               const char* tag) {
            bool ok = true;
            for (int blk = 0; blk < N_BATCH && ok; ++blk) {
                const uint8_t* blk_cw =
                    scw.data() + size_t(blk) * p.codeword_buf_size();
                if (std::memcmp(blk_cw, ref[blk].data(),
                                p.codeword_buf_size()) != 0)
                    ok = false;
            }
            char label[128];
            std::snprintf(label, sizeof(label),
                          "batch %s: all %d blocks match scalar ref", tag, N_BATCH);
            EXPECT(ok, label);
        };

        // ---- scalar per-block loop (reference; verifies layout assumptions) ----
        {
            std::vector<uint8_t> scw(super_cw_sz, 0u);
            for (int blk = 0; blk < N_BATCH; ++blk) {
                uint8_t* blk_cw = scw.data() + size_t(blk) * p.codeword_buf_size();
                std::span<uint8_t> sp{blk_cw, p.codeword_buf_size()};
                fec::interleave(src[blk], sp, p);
                fec::rs_encode(sp, p);
            }
            check_batch(scw, "scalar+scalar");
        }

#if defined(__ARM_NEON)
        // ---- NEON per-block loop ----
        {
            std::vector<uint8_t> scw(super_cw_sz, 0u);
            for (int blk = 0; blk < N_BATCH; ++blk) {
                uint8_t* blk_cw = scw.data() + size_t(blk) * p.codeword_buf_size();
                std::span<uint8_t> sp{blk_cw, p.codeword_buf_size()};
                fec::interleave_neon(src[blk], sp, p);
                fec::rs_encode_neon(sp, p);
            }
            check_batch(scw, "neon+neon");
        }
#endif // __ARM_NEON

#if defined(__APPLE__)
        // ---- Metal batch: super-buffer interleave + encode ----
        {
            fec::FecBlockParams p_batch;
            p_batch.col_height = total_ch;

            std::vector<uint8_t> super_seg;
            build_super_seg(super_seg);
            std::vector<uint8_t> scw(super_cw_sz, 0u);
            fec::interleave_metal(super_seg, scw, p_batch);
            fec::rs_encode_metal(scw, p_batch);
            check_batch(scw, "metal+metal batch");
        }
#endif // __APPLE__
    } // batch block
}

// ---------------------------------------------------------------------------
// Test 9 — Performance: scalar vs NEON comparison at col_height = 8192
// ---------------------------------------------------------------------------
static void test_performance() {
    section("Performance comparison (col_height = 8192)");

    fec::FecBlockParams p;
    p.col_height = 8192;

    std::vector<uint8_t> src(p.segment_buf_size());
    std::vector<uint8_t> cw(p.codeword_buf_size(), 0);
    std::vector<uint8_t> dst(p.segment_buf_size(), 0);

    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint8_t>(i);
    }

    const int ITERS = 10;
    const double mb    = double(p.segment_buf_size()) / (1024.0 * 1024.0);
    // Gbit/s = segment_bytes * 8 bits / (ms * 1e-3 s) / 1e9
    //        = segment_bytes * 8 / (ms * 1e6)
    const double bytes = double(p.segment_buf_size());
    auto gbits = [&](double ms) { return bytes * 8.0 / (ms * 1e6); };
    auto mibs  = [&](double ms) { return mb / (ms / 1000.0); };

    // ---- Scalar ----
    double ms_il  = bench_ms([&]{ fec::interleave(src, cw, p); },   ITERS);
    double ms_dil = bench_ms([&]{ fec::deinterleave(cw, dst, p); }, ITERS);

    // ---- Magic multiply ----
    double ms_il_mm = bench_ms([&]{ fec::interleave_magic(src, cw, p); }, ITERS);

    std::printf("\n  %-28s %8s %10s %9s\n",
                "Implementation", "ms/iter", "MiB/s", "Gbit/s");
    std::printf("  %-28s %8.2f %10.1f %9.3f\n",
                "interleave (scalar)", ms_il, mibs(ms_il), gbits(ms_il));
    std::printf("  %-28s %8.2f %10.1f %9.3f\n",
                "deinterleave (scalar)", ms_dil, mibs(ms_dil), gbits(ms_dil));
    std::printf("  %-28s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                "interleave (magic)", ms_il_mm, mibs(ms_il_mm), gbits(ms_il_mm),
                ms_il / ms_il_mm);

#if defined(__ARM_NEON)
    double ms_il_n  = bench_ms([&]{ fec::interleave_neon(src, cw, p); },   ITERS);
    double ms_dil_n = bench_ms([&]{ fec::deinterleave_neon(cw, dst, p); }, ITERS);

    std::printf("  %-28s %8.2f %10.1f %9.3f   (%.1fx scalar, %.1fx magic)\n",
                "interleave (NEON)", ms_il_n, mibs(ms_il_n), gbits(ms_il_n),
                ms_il / ms_il_n, ms_il_mm / ms_il_n);
    std::printf("  %-28s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                "deinterleave (NEON)", ms_dil_n, mibs(ms_dil_n), gbits(ms_dil_n),
                ms_dil / ms_dil_n);

#else
    std::printf("  (NEON not available on this platform)\n");
#endif

#if defined(__APPLE__)
    // ---- Metal GPU ----
    // Wall-clock time: includes command-buffer encoding, commit, and
    // waitUntilCompleted — the realistic end-to-end cost per call.
    double ms_il_metal = bench_ms([&]{ fec::interleave_metal(src, cw, p); }, ITERS);

    // GPU-only kernel time from MTLCommandBuffer GPUStartTime/GPUEndTime.
    // GPU is already warm from bench_ms above.
    std::vector<uint8_t> cw_gpu(p.codeword_buf_size(), 0);
    double gpu_us = fec::interleave_metal_gpu_us(src, cw_gpu, p);

    std::printf("  %-28s %8.2f %10.1f %9.3f   (%.1fx scalar, wall-clock)\n",
                "interleave (Metal, wall)", ms_il_metal, mibs(ms_il_metal),
                gbits(ms_il_metal), ms_il / ms_il_metal);
    std::printf("  %-28s %8.2f %10.1f %9.3f   (GPU kernel only)\n",
                "interleave (Metal, GPU-only)",
                gpu_us / 1000.0,
                mibs(gpu_us / 1000.0),
                gbits(gpu_us / 1000.0));
#endif // __APPLE__

    // ---- RS encoder ----
    // Pre-interleave so the codeword buffer has valid data bytes 0-3.
    fec::interleave(src, cw, p);
    double ms_enc = bench_ms([&]{ fec::rs_encode(cw, p); }, ITERS);
    std::printf("  %-28s %8.2f %10.1f %9.3f\n",
                "rs_encode (scalar)", ms_enc, mibs(ms_enc), gbits(ms_enc));

#if defined(__ARM_NEON)
    {
        std::vector<uint8_t> cw_n(p.codeword_buf_size(), 0);
        fec::interleave(src, cw_n, p);
        double ms_enc_neon = bench_ms([&]{ fec::rs_encode_neon(cw_n, p); }, ITERS);
        std::printf("  %-28s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                    "rs_encode (NEON)", ms_enc_neon, mibs(ms_enc_neon),
                    gbits(ms_enc_neon), ms_enc / ms_enc_neon);
    }
#endif // __ARM_NEON

#if defined(__APPLE__)
    // Metal RS encoder: codeword buffer is already interleaved from above.
    double ms_enc_metal = bench_ms([&]{ fec::rs_encode_metal(cw, p); }, ITERS);
    std::vector<uint8_t> cw_rs_gpu(p.codeword_buf_size(), 0);
    fec::interleave(src, cw_rs_gpu, p);
    double enc_gpu_us = fec::rs_encode_metal_gpu_us(cw_rs_gpu, p);

    std::printf("  %-28s %8.2f %10.1f %9.3f   (%.1fx scalar, wall-clock)\n",
                "rs_encode (Metal, wall)", ms_enc_metal, mibs(ms_enc_metal),
                gbits(ms_enc_metal), ms_enc / ms_enc_metal);
    std::printf("  %-28s %8.2f %10.1f %9.3f   (GPU kernel only)\n",
                "rs_encode (Metal, GPU-only)",
                enc_gpu_us / 1000.0, mibs(enc_gpu_us / 1000.0),
                gbits(enc_gpu_us / 1000.0));
#endif // __APPLE__

    // ---- Combined pipeline (interleave + rs_encode together) ----
    std::printf("\n  Combined pipeline (interleave + rs_encode)\n");

    double ms_combined_scalar = bench_ms([&]{
        fec::interleave(src, cw, p);
        fec::rs_encode(cw, p);
    }, ITERS);
    std::printf("  %-28s %8.2f %10.1f %9.3f\n",
                "scalar+scalar", ms_combined_scalar,
                mibs(ms_combined_scalar), gbits(ms_combined_scalar));

#if defined(__ARM_NEON)
    double ms_combined_neon = bench_ms([&]{
        fec::interleave_neon(src, cw, p);
        fec::rs_encode_neon(cw, p);
    }, ITERS);
    std::printf("  %-28s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                "neon+neon", ms_combined_neon,
                mibs(ms_combined_neon), gbits(ms_combined_neon),
                ms_combined_scalar / ms_combined_neon);
#endif // __ARM_NEON

#if defined(__APPLE__)
    {
        std::vector<uint8_t> cw_m(p.codeword_buf_size(), 0);
        double ms_combined_metal = bench_ms([&]{
            fec::interleave_metal(src, cw_m, p);
            fec::rs_encode_metal(cw_m, p);
        }, ITERS);
        std::printf("  %-28s %8.2f %10.1f %9.3f   (%.1fx scalar, wall-clock)\n",
                    "metal+metal", ms_combined_metal,
                    mibs(ms_combined_metal), gbits(ms_combined_metal),
                    ms_combined_scalar / ms_combined_metal);
    }
#endif // __APPLE__

    std::printf("\n");

    // Round-trip sanity at full size
    fec::interleave(src, cw, p);
    fec::deinterleave(cw, dst, p);
    EXPECT(src == dst, "Full-size scalar round-trip: src == dst");

    std::fill(cw.begin(), cw.end(), 0u);
    fec::interleave_magic(src, cw, p);
    fec::deinterleave(cw, dst, p);
    EXPECT(src == dst, "Full-size magic round-trip: src == dst");

#if defined(__ARM_NEON)
    std::fill(cw.begin(),  cw.end(),  0u);
    std::fill(dst.begin(), dst.end(), 0u);
    fec::interleave_neon(src, cw, p);
    fec::deinterleave_neon(cw, dst, p);
    EXPECT(src == dst, "Full-size NEON round-trip: src == dst");
#endif

#if defined(__APPLE__)
    std::fill(cw.begin(),  cw.end(),  0u);
    std::fill(dst.begin(), dst.end(), 0u);
    fec::interleave_metal(src, cw, p);
    fec::deinterleave(cw, dst, p);
    EXPECT(src == dst, "Full-size Metal round-trip: src == dst");
#endif
}


// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::printf("FEC Interleaver / Deinterleaver Testbench\n");

    test_roundtrip();
    test_bit_mapping();
    test_parity_zeroed();
    test_pipeline();
#if defined(__ARM_NEON)
    test_neon_correctness();
#endif
#if defined(__APPLE__)
    test_metal_correctness();
#endif
    test_magic_correctness();
    test_rs_encode();
#if defined(__ARM_NEON)
    test_rs_encode_neon();
#endif
#if defined(__APPLE__)
    test_rs_encode_metal();
#endif
    test_pipeline_combined();
    test_performance();

    std::printf("\n--- Results: %d passed, %d failed ---\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
