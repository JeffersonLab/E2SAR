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

#undef IL_BIT_BODY
#undef IL_BIT
#undef IL_BIT0

} // namespace fec

#endif // __ARM_NEON
