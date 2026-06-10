#include "fec/interleaver.h"
#include <cassert>
#include <cstring>

namespace fec {

void interleave(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(segments.size()  == p.segment_buf_size());
    assert(codewords.size() == p.codeword_buf_size());

    const uint32_t nw  = p.num_words();
    const uint32_t ch  = p.col_height;

    // Walk words in order — each word maps to one bit position across all 32 segments.
    // For word W:
    //   segment byte index = W / 8
    //   bit shift within byte = 7 - (W % 8)   [MSB-first: bit 7 → word 0]
    for (uint32_t w = 0; w < nw; ++w) {
        const uint32_t byte_idx  = w / 8u;
        const uint32_t bit_shift = 7u - (w % 8u);

        uint8_t* cw = codewords.data() + (size_t{w} * FecBlockParams::CODEWORD_BYTES);

        // Build 8 data symbol nibbles and pack 2-per-byte into codeword bytes 0–3.
        for (uint32_t s = 0; s < FecBlockParams::NUM_COLUMNS; ++s) {
            // Segments for symbol s:  s*4+0 (bit3/MSB) .. s*4+3 (bit0/LSB)
            const uint32_t sb = s * FecBlockParams::SEGS_PER_COLUMN;

            uint8_t nibble =
                (((segments[(sb + 0u) * ch + byte_idx] >> bit_shift) & 1u) << 3u) |
                (((segments[(sb + 1u) * ch + byte_idx] >> bit_shift) & 1u) << 2u) |
                (((segments[(sb + 2u) * ch + byte_idx] >> bit_shift) & 1u) << 1u) |
                (((segments[(sb + 3u) * ch + byte_idx] >> bit_shift) & 1u) << 0u);

            // Symbols are packed two-per-byte: even symbol → high nibble, odd → low.
            if (s % 2u == 0u) {
                cw[s / 2u] = static_cast<uint8_t>(nibble << 4u);
            } else {
                cw[s / 2u] |= nibble;
            }
        }

        cw[4] = 0u;  // zero parity slots — RS encoder will write here
    }
}

void interleave_parity(
    std::span<const uint8_t> parity_segs,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(parity_segs.size() == 8u * p.col_height);
    assert(codewords.size()   == p.codeword_buf_size());

    const uint32_t nw = p.num_words();
    const uint32_t ch = p.col_height;

    for (uint32_t w = 0; w < nw; ++w) {
        const uint32_t byte_idx  = w / 8u;
        const uint32_t bit_shift = 7u - (w % 8u);

        uint8_t p0_nibble = 0;
        uint8_t p1_nibble = 0;

        for (uint32_t b = 0; b < 4u; ++b) {
            p0_nibble |= static_cast<uint8_t>(
                ((parity_segs[b * ch + byte_idx] >> bit_shift) & 1u) << (3u - b));
            p1_nibble |= static_cast<uint8_t>(
                ((parity_segs[(4u + b) * ch + byte_idx] >> bit_shift) & 1u) << (3u - b));
        }

        codewords[size_t{w} * FecBlockParams::CODEWORD_BYTES + 4u] =
            static_cast<uint8_t>((p0_nibble << 4u) | p1_nibble);
    }
}

// ---------------------------------------------------------------------------
// interleave_magic
//
// Same byte-major outer loop as interleave_neon: each byte_idx processes 8
// consecutive codewords.  The gather yields 32 bytes (one per segment), which
// are loaded as four uint64_t values.
//
// Core primitive — "magic multiply" bit-packing:
//   Given a uint64_t q where each byte has a single-bit value in bit 0,
//   (q * 0x0102040810204080) >> 56  packs those 8 values into one byte:
//   bit i of the result = bit-0 of byte i of q.
//
//   Proof: magic = sum_{j=0}^{7} 2^(7j+7).
//   The contribution of q_byte_i (= b_i, value 0 or 1) at bit 56+i comes from
//   the pair (i, 7-i): 8i + 7(7-i) + 7 = i + 56.  All other cross-products
//   fall below bit 56 and are discarded by the >> 56 shift.  Since each b_i is
//   0 or 1, there are no carry collisions in the MSByte.
//
// Gather ordering: segments are loaded in reversed bit-position order within
// each column group using the XOR-3 index trick (s ^ 3).  Segment s = c*4+bp
// maps to gathered[(c*4+bp) ^ 3] = gathered[c*4 + (3-bp)].  After the magic
// multiply, plane[3:0] therefore has the nibble for symbol 0 in natural order
// (bit 3 = MSB from seg c*4+0, bit 0 = LSB from seg c*4+3) rather than
// reversed, saving the 7-instruction nibble-bit-reversal step.
//
// Post-processing per codeword: a single nibble-swap within each byte of the
// 32-bit plane reorders sym-pairs into codeword byte layout:
//   packed = ((plane & 0x0F0F0F0F) << 4) | ((plane >> 4) & 0x0F0F0F0F)
//   => cw byte 0 = (sym0 << 4) | sym1, byte 1 = (sym2 << 4) | sym3, etc.
// ---------------------------------------------------------------------------
void interleave_magic(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(segments.size()  == p.segment_buf_size());
    assert(codewords.size() == p.codeword_buf_size());

    const uint32_t ch = p.col_height;

    // bit i of ((q * magic) >> 56)  =  bit-0 of byte i of q.
    constexpr uint64_t magic = 0x0102040810204080ULL;
    // Mask to isolate bit 0 of each byte after a right-shift.
    constexpr uint64_t mask1 = 0x0101010101010101ULL;

    for (uint32_t byte_idx = 0; byte_idx < ch; ++byte_idx) {
        // Gather one byte from each segment, writing to position (s ^ 3) so
        // that within each group of 4 (one column) the LSB-segment lands at
        // the lowest array index and the MSB-segment at the highest.
        uint8_t gathered[32];
        for (uint32_t s = 0; s < 32u; ++s) {
            gathered[s ^ 3u] = segments[s * ch + byte_idx];
        }

        // Reload as four uint64_t (little-endian: gathered[0] → byte 0 of q0).
        uint64_t q0, q1, q2, q3;
        std::memcpy(&q0, gathered,      8);
        std::memcpy(&q1, gathered +  8, 8);
        std::memcpy(&q2, gathered + 16, 8);
        std::memcpy(&q3, gathered + 24, 8);

        // Output: 8 consecutive codewords at codewords[byte_idx * 40].
        // Bit k maps to word (7-k), byte offset (7-k)*5  (MSB-first ordering).
        uint8_t* cw = codewords.data() + size_t{byte_idx} * 40u;

        // Unsigned loop 7..0 using wraparound sentinel (k < 8u terminates after k=0).
        for (uint32_t k = 7u; k < 8u; --k) {
            // Extract bit k of every byte into bit 0, then pack 8 values per uint64.
            uint32_t plane =
                  (uint32_t)((((q0 >> k) & mask1) * magic) >> 56)
                | ((uint32_t)((((q1 >> k) & mask1) * magic) >> 56) <<  8)
                | ((uint32_t)((((q2 >> k) & mask1) * magic) >> 56) << 16)
                | ((uint32_t)((((q3 >> k) & mask1) * magic) >> 56) << 24);

            // Swap nibbles within each byte: (sym_even << 4) | sym_odd.
            uint32_t packed = ((plane & 0x0F0F0F0Fu) << 4)
                            | ((plane >> 4)           & 0x0F0F0F0Fu);

            const uint32_t word_off = (7u - k) * 5u;
            std::memcpy(cw + word_off, &packed, 4);
            cw[word_off + 4] = 0u;  // parity slot — zeroed for RS encoder
        }
    }
}

} // namespace fec

// =============================================================================
// ARM NEON implementations
// =============================================================================
#if defined(__ARM_NEON)
#include <arm_neon.h>

namespace fec {

// ---------------------------------------------------------------------------
// interleave_neon
//
// Outer loop: byte_idx (0 .. col_height-1).  Each byte_idx covers 8 words.
//
// Per byte_idx:
//   1. Gather one byte from each of the 32 segments → two uint8x16_t registers.
//   2. For each of the 8 bit positions (unrolled, so vshrq_n_u8 sees a constant):
//        a. Extract that bit from all 32 segment bytes.
//        b. Weighted reduce via vmulq_u8 + vpadd cascade → 8 nibbles in uint8x8_t.
//        c. Pack nibble pairs into 4 codeword data bytes via vmul_u8 + vpadd.
//        d. Write 5-byte codeword (4 data + 1 zero parity).
// ---------------------------------------------------------------------------

// Macro: process one bit position k inside interleave_neon.
// Writes the codeword at output pointer (cw + word_off) using g_lo/g_hi.
// wt  = nibble weight vector {8,4,2,1} x4  (uint8x16_t)
// pk  = nibble pack  vector {16,1}    x4  (uint8x8_t)
// one = vdupq_n_u8(1)
// Common body shared by both bit-extract macros below.
// Expects bl/bh to be already extracted (each lane is 0 or 1).
#define IL_BIT_BODY(word_off)                                                 \
    do {                                                                       \
        uint8x8_t  p1 = vpadd_u8(vget_low_u8 (vmulq_u8(bl, wt)),             \
                                  vget_high_u8(vmulq_u8(bl, wt)));            \
        uint8x8_t  p2 = vpadd_u8(vget_low_u8 (vmulq_u8(bh, wt)),             \
                                  vget_high_u8(vmulq_u8(bh, wt)));            \
        uint8x8_t  nibs = vpadd_u8(p1, p2);          /* 8 nibbles          */\
        uint8x8_t  pkd  = vpadd_u8(vmul_u8(nibs, pk), vdup_n_u8(0));         \
        vst1_lane_u32(reinterpret_cast<uint32_t*>(cw + (word_off)),             \
                      vreinterpret_u32_u8(pkd), 0);                           \
        cw[(word_off)+4] = 0u;  /* parity slot — zeroed for RS encoder */     \
    } while (0)

// For k in [1,8]: vshrq_n_u8 requires a non-zero compile-time constant.
#define IL_BIT(k, word_off)                                                   \
    do {                                                                       \
        uint8x16_t bl = vandq_u8(vshrq_n_u8(g_lo, (k)), one);                \
        uint8x16_t bh = vandq_u8(vshrq_n_u8(g_hi, (k)), one);                \
        IL_BIT_BODY(word_off);                                                 \
    } while (0)

// For k=0: no shift needed — just AND with 1.
#define IL_BIT0(word_off)                                                     \
    do {                                                                       \
        uint8x16_t bl = vandq_u8(g_lo, one);                                  \
        uint8x16_t bh = vandq_u8(g_hi, one);                                  \
        IL_BIT_BODY(word_off);                                                 \
    } while (0)

void interleave_neon(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(segments.size()  == p.segment_buf_size());
    assert(codewords.size() == p.codeword_buf_size());

    const uint32_t ch = p.col_height;

    // Constant NEON vectors (hoisted from all loops).
    // wt: multiply each bit by its nibble weight [8,4,2,1] repeated across 16 lanes.
    const uint8x16_t wt  = {8,4,2,1, 8,4,2,1, 8,4,2,1, 8,4,2,1};
    // pk: multiply each nibble by [16,1] to pack pairs; vpadd then sums adjacent pairs.
    const uint8x8_t  pk  = {16,1, 16,1, 16,1, 16,1};
    const uint8x16_t one = vdupq_n_u8(1u);

    for (uint32_t byte_idx = 0; byte_idx < ch; ++byte_idx) {
        // Gather one byte from each of the 32 segments.
        // segments[seg * ch + byte_idx]  (segments are ch bytes apart in memory)
        uint8_t gathered[32];
        for (uint32_t s = 0; s < 32u; ++s) {
            gathered[s] = segments[s * ch + byte_idx];
        }
        const uint8x16_t g_lo = vld1q_u8(gathered);       // segs 0-15
        const uint8x16_t g_hi = vld1q_u8(gathered + 16);  // segs 16-31

        // Output: 8 consecutive codewords starting at codewords[byte_idx * 40].
        // MSB-first: bit 7 of each segment byte → word 0, bit 0 → word 7.
        uint8_t* cw = codewords.data() + size_t{byte_idx} * 40u;

        //  bit k → word (7-k) → byte offset (7-k)*5
        IL_BIT (7,  0);   // word 0
        IL_BIT (6,  5);   // word 1
        IL_BIT (5, 10);   // word 2
        IL_BIT (4, 15);   // word 3
        IL_BIT (3, 20);   // word 4
        IL_BIT (2, 25);   // word 5
        IL_BIT (1, 30);   // word 6
        IL_BIT0(   35);   // word 7 (k=0: AND only, vshrq_n_u8 disallows shift=0)
    }
}

// ---------------------------------------------------------------------------
// interleave_neon_tiled
//
// Outer loop: byte_idx_base in steps of 16 (tile width T=16).
// Per tile:
//   Pass A (segs 0-15):
//     1. 16 x vld1q_u8 from segments[s * ch + byte_idx_base], s=0..15
//     2. 4-stage 16x16 byte transpose in-register (vtrn butterfly)
//     3. Spill 16 transposed Q-regs to spill_A[256] (aligned on stack)
//   Pass B (segs 16-31): identical, spill to spill_B[256].
//   Inner loop (j=0..15):
//     g_lo = vld1q_u8(spill_A + j*16)   byte (byte_idx_base+j) from segs 0-15
//     g_hi = vld1q_u8(spill_B + j*16)   byte (byte_idx_base+j) from segs 16-31
//     run existing IL_BIT pipeline → 8 codewords
// Tail: remaining col_height % 16 positions handled by scalar gather.
//
// 4-stage transpose pairs (starting from r0..r15):
//   Stage 1 vtrnq_u8:  (r0,r1)  (r2,r3)  (r4,r5)  (r6,r7)
//                      (r8,r9)  (r10,r11)(r12,r13)(r14,r15)
//   Stage 2 vtrnq_u16: (r0,r2)  (r1,r3)  (r4,r6)  (r5,r7)
//                      (r8,r10) (r9,r11) (r12,r14)(r13,r15)
//   Stage 3 vtrnq_u32: (r0,r4)  (r1,r5)  (r2,r6)  (r3,r7)
//                      (r8,r12) (r9,r13) (r10,r14)(r11,r15)
//   Stage 4 swap64:    (r0,r8)  (r1,r9)  (r2,r10) (r3,r11)
//                      (r4,r12) (r5,r13) (r6,r14) (r7,r15)
// After: register j holds byte (byte_idx_base+j) from all 16 input rows.
// ---------------------------------------------------------------------------

// Transpose helper macros — scoped to this function, undefined below.
#define TRN16(A, B) do {                                                      \
    auto _t = vtrnq_u16(vreinterpretq_u16_u8(A), vreinterpretq_u16_u8(B));   \
    (A) = vreinterpretq_u8_u16(_t.val[0]);                                    \
    (B) = vreinterpretq_u8_u16(_t.val[1]);                                    \
} while (0)

#define TRN32(A, B) do {                                                      \
    auto _t = vtrnq_u32(vreinterpretq_u32_u8(A), vreinterpretq_u32_u8(B));   \
    (A) = vreinterpretq_u8_u32(_t.val[0]);                                    \
    (B) = vreinterpretq_u8_u32(_t.val[1]);                                    \
} while (0)

// Swap the 64-bit halves of A and B between each other.
// After: A = [lo(A_orig) | lo(B_orig)], B = [hi(A_orig) | hi(B_orig)]
#define SWAP64(A, B) do {                                                     \
    uint8x16_t _a = (A), _b = (B);                                            \
    (A) = vcombine_u8(vget_low_u8(_a),  vget_low_u8(_b));                    \
    (B) = vcombine_u8(vget_high_u8(_a), vget_high_u8(_b));                   \
} while (0)

// Transpose a set of 16 uint8x16_t registers in-place via the 4-stage butterfly.
// Uses a block macro to keep the tiled function body readable.
#define TRANSPOSE_16x16(r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15) \
    do {                                                                           \
        /* Stage 1: vtrnq_u8 — interleave bytes within adjacent pairs */           \
        { auto _t = vtrnq_u8(r0,  r1);  r0  = _t.val[0]; r1  = _t.val[1]; }      \
        { auto _t = vtrnq_u8(r2,  r3);  r2  = _t.val[0]; r3  = _t.val[1]; }      \
        { auto _t = vtrnq_u8(r4,  r5);  r4  = _t.val[0]; r5  = _t.val[1]; }      \
        { auto _t = vtrnq_u8(r6,  r7);  r6  = _t.val[0]; r7  = _t.val[1]; }      \
        { auto _t = vtrnq_u8(r8,  r9);  r8  = _t.val[0]; r9  = _t.val[1]; }      \
        { auto _t = vtrnq_u8(r10, r11); r10 = _t.val[0]; r11 = _t.val[1]; }      \
        { auto _t = vtrnq_u8(r12, r13); r12 = _t.val[0]; r13 = _t.val[1]; }      \
        { auto _t = vtrnq_u8(r14, r15); r14 = _t.val[0]; r15 = _t.val[1]; }      \
        /* Stage 2: vtrnq_u16 — interleave 16-bit pairs */                        \
        TRN16(r0, r2);  TRN16(r1, r3);                                            \
        TRN16(r4, r6);  TRN16(r5, r7);                                            \
        TRN16(r8, r10); TRN16(r9, r11);                                           \
        TRN16(r12,r14); TRN16(r13,r15);                                           \
        /* Stage 3: vtrnq_u32 — interleave 32-bit pairs */                        \
        TRN32(r0, r4);  TRN32(r1, r5);  TRN32(r2, r6);  TRN32(r3, r7);           \
        TRN32(r8, r12); TRN32(r9, r13); TRN32(r10,r14); TRN32(r11,r15);          \
        /* Stage 4: swap 64-bit halves across stride-8 pairs */                   \
        SWAP64(r0, r8);  SWAP64(r1, r9);  SWAP64(r2, r10); SWAP64(r3, r11);      \
        SWAP64(r4, r12); SWAP64(r5, r13); SWAP64(r6, r14); SWAP64(r7, r15);      \
    } while (0)

void interleave_neon_tiled(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(segments.size()  == p.segment_buf_size());
    assert(codewords.size() == p.codeword_buf_size());

    const uint32_t ch = p.col_height;

    // Constant NEON vectors (same as interleave_neon).
    const uint8x16_t wt  = {8,4,2,1, 8,4,2,1, 8,4,2,1, 8,4,2,1};
    const uint8x8_t  pk  = {16,1, 16,1, 16,1, 16,1};
    const uint8x16_t one = vdupq_n_u8(1u);

    const uint8_t* seg_base = segments.data();

    const uint32_t full_tiles = ch / 16u;
    const uint32_t tail_start = full_tiles * 16u;

    // Stack spill buffers: 256 bytes each, 16-byte aligned.
    // 512 bytes total = 8 cache lines, trivially L1-resident throughout the tile.
    alignas(16) uint8_t spill_A[256];
    alignas(16) uint8_t spill_B[256];

    // ---- Tiled outer loop: 16 byte positions per iteration ----
    for (uint32_t tile = 0; tile < full_tiles; ++tile) {
        const uint32_t bi = tile * 16u;   // byte_idx_base

        // === Pass A: load and transpose segments 0-15 ===
        uint8x16_t r0  = vld1q_u8(seg_base +  0u * ch + bi);
        uint8x16_t r1  = vld1q_u8(seg_base +  1u * ch + bi);
        uint8x16_t r2  = vld1q_u8(seg_base +  2u * ch + bi);
        uint8x16_t r3  = vld1q_u8(seg_base +  3u * ch + bi);
        uint8x16_t r4  = vld1q_u8(seg_base +  4u * ch + bi);
        uint8x16_t r5  = vld1q_u8(seg_base +  5u * ch + bi);
        uint8x16_t r6  = vld1q_u8(seg_base +  6u * ch + bi);
        uint8x16_t r7  = vld1q_u8(seg_base +  7u * ch + bi);
        uint8x16_t r8  = vld1q_u8(seg_base +  8u * ch + bi);
        uint8x16_t r9  = vld1q_u8(seg_base +  9u * ch + bi);
        uint8x16_t r10 = vld1q_u8(seg_base + 10u * ch + bi);
        uint8x16_t r11 = vld1q_u8(seg_base + 11u * ch + bi);
        uint8x16_t r12 = vld1q_u8(seg_base + 12u * ch + bi);
        uint8x16_t r13 = vld1q_u8(seg_base + 13u * ch + bi);
        uint8x16_t r14 = vld1q_u8(seg_base + 14u * ch + bi);
        uint8x16_t r15 = vld1q_u8(seg_base + 15u * ch + bi);

        TRANSPOSE_16x16(r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15);

        // Spill: after transpose, register j holds byte (bi+j) from segs 0-15.
        vst1q_u8(spill_A +   0, r0);  vst1q_u8(spill_A +  16, r1);
        vst1q_u8(spill_A +  32, r2);  vst1q_u8(spill_A +  48, r3);
        vst1q_u8(spill_A +  64, r4);  vst1q_u8(spill_A +  80, r5);
        vst1q_u8(spill_A +  96, r6);  vst1q_u8(spill_A + 112, r7);
        vst1q_u8(spill_A + 128, r8);  vst1q_u8(spill_A + 144, r9);
        vst1q_u8(spill_A + 160, r10); vst1q_u8(spill_A + 176, r11);
        vst1q_u8(spill_A + 192, r12); vst1q_u8(spill_A + 208, r13);
        vst1q_u8(spill_A + 224, r14); vst1q_u8(spill_A + 240, r15);

        // === Pass B: load and transpose segments 16-31 ===
        r0  = vld1q_u8(seg_base + 16u * ch + bi);
        r1  = vld1q_u8(seg_base + 17u * ch + bi);
        r2  = vld1q_u8(seg_base + 18u * ch + bi);
        r3  = vld1q_u8(seg_base + 19u * ch + bi);
        r4  = vld1q_u8(seg_base + 20u * ch + bi);
        r5  = vld1q_u8(seg_base + 21u * ch + bi);
        r6  = vld1q_u8(seg_base + 22u * ch + bi);
        r7  = vld1q_u8(seg_base + 23u * ch + bi);
        r8  = vld1q_u8(seg_base + 24u * ch + bi);
        r9  = vld1q_u8(seg_base + 25u * ch + bi);
        r10 = vld1q_u8(seg_base + 26u * ch + bi);
        r11 = vld1q_u8(seg_base + 27u * ch + bi);
        r12 = vld1q_u8(seg_base + 28u * ch + bi);
        r13 = vld1q_u8(seg_base + 29u * ch + bi);
        r14 = vld1q_u8(seg_base + 30u * ch + bi);
        r15 = vld1q_u8(seg_base + 31u * ch + bi);

        TRANSPOSE_16x16(r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15);

        // Spill: register j holds byte (bi+j) from segs 16-31.
        vst1q_u8(spill_B +   0, r0);  vst1q_u8(spill_B +  16, r1);
        vst1q_u8(spill_B +  32, r2);  vst1q_u8(spill_B +  48, r3);
        vst1q_u8(spill_B +  64, r4);  vst1q_u8(spill_B +  80, r5);
        vst1q_u8(spill_B +  96, r6);  vst1q_u8(spill_B + 112, r7);
        vst1q_u8(spill_B + 128, r8);  vst1q_u8(spill_B + 144, r9);
        vst1q_u8(spill_B + 160, r10); vst1q_u8(spill_B + 176, r11);
        vst1q_u8(spill_B + 192, r12); vst1q_u8(spill_B + 208, r13);
        vst1q_u8(spill_B + 224, r14); vst1q_u8(spill_B + 240, r15);

        // === Bit-extract: 16 byte positions, using spill buffers as g_lo/g_hi ===
        for (uint32_t j = 0; j < 16u; ++j) {
            const uint8x16_t g_lo = vld1q_u8(spill_A + j * 16u);
            const uint8x16_t g_hi = vld1q_u8(spill_B + j * 16u);
            uint8_t* cw = codewords.data() + size_t{bi + j} * 40u;

            IL_BIT (7,  0);
            IL_BIT (6,  5);
            IL_BIT (5, 10);
            IL_BIT (4, 15);
            IL_BIT (3, 20);
            IL_BIT (2, 25);
            IL_BIT (1, 30);
            IL_BIT0(   35);
        }
    }

    // ---- Tail loop: remaining col_height % 16 positions ----
    for (uint32_t byte_idx = tail_start; byte_idx < ch; ++byte_idx) {
        uint8_t gathered[32];
        for (uint32_t s = 0; s < 32u; ++s)
            gathered[s] = seg_base[s * ch + byte_idx];
        const uint8x16_t g_lo = vld1q_u8(gathered);
        const uint8x16_t g_hi = vld1q_u8(gathered + 16);
        uint8_t* cw = codewords.data() + size_t{byte_idx} * 40u;

        IL_BIT (7,  0);
        IL_BIT (6,  5);
        IL_BIT (5, 10);
        IL_BIT (4, 15);
        IL_BIT (3, 20);
        IL_BIT (2, 25);
        IL_BIT (1, 30);
        IL_BIT0(   35);
    }
}

#undef TRANSPOSE_16x16
#undef SWAP64
#undef TRN32
#undef TRN16

#undef IL_BIT_BODY
#undef IL_BIT
#undef IL_BIT0

void interleave_parity_neon(
    std::span<const uint8_t> parity_segs,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(parity_segs.size() == 8u * p.col_height);
    assert(codewords.size()   == p.codeword_buf_size());

    const uint32_t ch = p.col_height;

    for (uint32_t byte_idx = 0; byte_idx < ch; ++byte_idx) {
        uint8_t par_bytes[8];
        for (uint32_t s = 0; s < 8u; ++s)
            par_bytes[s] = parity_segs[s * ch + byte_idx];

        uint8x8_t seg_data = vld1_u8(par_bytes);

        for (uint32_t bit = 0; bit < 8u; ++bit) {
            const uint32_t w = byte_idx * 8u + bit;
            const int shift = 7 - static_cast<int>(bit);

            uint8x8_t shifted = vreinterpret_u8_s8(
                vshl_s8(vreinterpret_s8_u8(seg_data), vdup_n_s8(-shift)));
            uint8x8_t bits = vand_u8(shifted, vdup_n_u8(1u));

            uint8_t p0_nibble =
                (vget_lane_u8(bits, 0) << 3u) |
                (vget_lane_u8(bits, 1) << 2u) |
                (vget_lane_u8(bits, 2) << 1u) |
                (vget_lane_u8(bits, 3) << 0u);

            uint8_t p1_nibble =
                (vget_lane_u8(bits, 4) << 3u) |
                (vget_lane_u8(bits, 5) << 2u) |
                (vget_lane_u8(bits, 6) << 1u) |
                (vget_lane_u8(bits, 7) << 0u);

            codewords[size_t{w} * FecBlockParams::CODEWORD_BYTES + 4u] =
                static_cast<uint8_t>((p0_nibble << 4u) | p1_nibble);
        }
    }
}

} // namespace fec

#endif // __ARM_NEON
