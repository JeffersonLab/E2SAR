#include "fec/deinterleaver.h"
#include <cassert>
#include <cstring>

namespace fec {

void deinterleave(
    std::span<const uint8_t> codewords,
    std::span<uint8_t>       segments,
    const FecBlockParams&    p)
{
    assert(codewords.size() == p.codeword_buf_size());
    assert(segments.size()  == p.segment_buf_size());

    // Zero the output before OR-ing in individual bits.
    std::memset(segments.data(), 0, segments.size());

    const uint32_t nw = p.num_words();
    const uint32_t ch = p.col_height;

    for (uint32_t w = 0; w < nw; ++w) {
        const uint32_t byte_idx  = w / 8u;
        const uint32_t bit_shift = 7u - (w % 8u);

        const uint8_t* cw = codewords.data() + (size_t{w} * FecBlockParams::CODEWORD_BYTES);

        // Unpack each data symbol nibble and scatter its bits back to 4 segments.
        for (uint32_t s = 0; s < FecBlockParams::NUM_COLUMNS; ++s) {
            uint8_t nibble;
            if (s % 2u == 0u) {
                nibble = (cw[s / 2u] >> 4u) & 0xFu;
            } else {
                nibble = cw[s / 2u] & 0xFu;
            }

            // Bit 3 (MSB of nibble) → segment s*4+0, bit 2 → s*4+1, ...
            const uint32_t sb = s * FecBlockParams::SEGS_PER_COLUMN;
            for (uint32_t b = 0u; b < FecBlockParams::BITS_PER_SYMBOL; ++b) {
                // b=0 → bit 3 (MSB), b=3 → bit 0 (LSB)  maps to segment sb+b
                const uint8_t bit_val = (nibble >> (3u - b)) & 1u;
                segments[(sb + b) * ch + byte_idx] |=
                    static_cast<uint8_t>(bit_val << bit_shift);
            }
        }
    }
}

void deinterleave_parity(
    std::span<const uint8_t> codewords,
    std::span<uint8_t>       parity_segs,
    const FecBlockParams&    p)
{
    assert(codewords.size()   == p.codeword_buf_size());
    assert(parity_segs.size() == 8u * p.col_height);

    std::memset(parity_segs.data(), 0, parity_segs.size());

    const uint32_t nw = p.num_words();
    const uint32_t ch = p.col_height;

    for (uint32_t w = 0; w < nw; ++w) {
        const uint32_t byte_idx  = w / 8u;
        const uint32_t bit_shift = 7u - (w % 8u);

        const uint8_t parity_byte = codewords[size_t{w} * FecBlockParams::CODEWORD_BYTES + 4u];
        const uint8_t p0_nibble = (parity_byte >> 4u) & 0xFu;
        const uint8_t p1_nibble =  parity_byte        & 0xFu;

        for (uint32_t b = 0; b < 4u; ++b) {
            const uint8_t bit0 = (p0_nibble >> (3u - b)) & 1u;
            parity_segs[b * ch + byte_idx] |= static_cast<uint8_t>(bit0 << bit_shift);

            const uint8_t bit1 = (p1_nibble >> (3u - b)) & 1u;
            parity_segs[(4u + b) * ch + byte_idx] |= static_cast<uint8_t>(bit1 << bit_shift);
        }
    }
}

} // namespace fec

// =============================================================================
// ARM NEON implementation
// =============================================================================
#if defined(__ARM_NEON)
#include <arm_neon.h>

namespace fec {

// ---------------------------------------------------------------------------
// deinterleave_neon
//
// Segment-major outer loop (cache-friendly) with TBL-based gather.
//
// For each segment (0..31), for each byte_idx (0..col_height-1):
//   1. Determine: column c = seg/4, bit-position bp = seg%4.
//      cw_off = c/2 (which byte within the codeword holds this symbol).
//      high_nib = (c % 2 == 0).
//      nib_shift = -(3 - bp)  (right-shift to isolate the target bit).
//
//   2. Load the 40-byte codeword block as three 16-byte registers:
//        reg0 = block[0..15], reg1 = block[16..31], reg2 = block[24..39]
//      The third register overlaps at offset 24 (not 32) so all loads stay
//      within the 40-byte block; reg2[k] = block[24+k] → table index 32+k.
//
//   3. vqtbl3_u8 selects 8 bytes (one per codeword) using a per-cw_off
//      compile-time index vector — replaces 8 dependent vld1_lane_u8 loads.
//
//   4. Extract nibble (high or low half of each byte).
//
//   5. Shift right by (3-bp) to bring the target bit to position 0, AND with 1.
//
//   6. Multiply by bit-position weights {128,64,32,16,8,4,2,1} and horizontal-add
//      (vaddv_u8) to assemble the 8 bits into one output byte.
//
// Cache profile: codeword reads sweep linearly (stride 40 per byte_idx).
//               segment writes are purely sequential (stride 1 per byte_idx).
// ---------------------------------------------------------------------------
void deinterleave_neon(
    std::span<const uint8_t> codewords,
    std::span<uint8_t>       segments,
    const FecBlockParams&    p)
{
    assert(codewords.size() == p.codeword_buf_size());
    assert(segments.size()  == p.segment_buf_size());

    const uint32_t ch = p.col_height;

    // Bit-position weights for packing 8 single-bit values into one byte.
    // Lane 0 = word 0 in block = output bit 7 (MSB-first).
    const uint8x8_t pack_wt = {128u, 64u, 32u, 16u, 8u, 4u, 2u, 1u};

    // TBL index vectors for vqtbl3_u8, one per cw_off (0..3).
    //
    // The 48-byte lookup table is the concatenation of three 16-byte registers:
    //   reg0 = block[0..15]   → table indices 0-15
    //   reg1 = block[16..31]  → table indices 16-31
    //   reg2 = block[24..39]  → table indices 32-47  (reg2[k] = block[24+k])
    //
    // For each codeword byte at actual block offset o:
    //   o < 32  → table index = o           (direct: reg0 or reg1)
    //   32 ≤ o < 40 → table index = o + 8  (reg2: index = 32 + (o-24) = o+8)
    //
    // cw_off = c/2 ∈ {0,1,2,3}; byte offsets = {cw_off, cw_off+5, ..., cw_off+35}
    static const uint8x8_t tbl_idx[4] = {
        { 0,  5, 10, 15, 20, 25, 30, 43},  // cw_off=0: offset 35 → idx 43
        { 1,  6, 11, 16, 21, 26, 31, 44},  // cw_off=1: offset 36 → idx 44
        { 2,  7, 12, 17, 22, 27, 40, 45},  // cw_off=2: offsets 32→40, 37→45
        { 3,  8, 13, 18, 23, 28, 41, 46},  // cw_off=3: offsets 33→41, 38→46
    };

    for (uint32_t seg = 0; seg < 32u; ++seg) {
        const uint32_t c        = seg / 4u;
        const uint32_t bp       = seg % 4u;
        const uint32_t cw_off   = c / 2u;          // byte offset within codeword
        const bool     high_nib = (c % 2u == 0u);  // which nibble carries this symbol
        // Right-shift the nibble by (3-bp) to bring the target bit to bit 0.
        // For bp=0: shift right 3;  bp=1: shift right 2;  bp=2: shift right 1;  bp=3: shift 0.
        const int8x8_t nib_shift = vdup_n_s8(-(int8_t)(3u - bp));
        const uint8x8_t idx = tbl_idx[cw_off];

        uint8_t* seg_out = segments.data() + size_t{seg} * ch;

        for (uint32_t byte_idx = 0; byte_idx < ch; ++byte_idx) {
            // Base of the 40-byte codeword block for this byte_idx.
            const uint8_t* blk = codewords.data() + size_t{byte_idx} * 40u;

            // Load three 16-byte registers covering the 40-byte block.
            // Third load starts at offset 24 (not 32): blk[24..39] fits exactly,
            // so no read ever exceeds the valid buffer range.
            const uint8x16x3_t block = {{
                vld1q_u8(blk),
                vld1q_u8(blk + 16),
                vld1q_u8(blk + 24),
            }};

            // Single TBL replaces 8 dependent vld1_lane_u8 instructions.
            // Three independent loads can execute in parallel; TBL resolves in 2 cycles.
            uint8x8_t cb = vqtbl3_u8(block, idx);

            // Extract the nibble for this symbol from the codeword byte.
            uint8x8_t nibble;
            if (high_nib) {
                nibble = vshr_n_u8(cb, 4);
            } else {
                nibble = vand_u8(cb, vdup_n_u8(0xFu));
            }

            // Shift right by (3-bp) to isolate the target bit in position 0.
            uint8x8_t bit_val = vand_u8(
                vreinterpret_u8_s8(vshl_s8(vreinterpret_s8_u8(nibble), nib_shift)),
                vdup_n_u8(1u));

            // Horizontal weighted sum: bit_val[i] * pack_wt[i] → output byte.
            // vaddv_u8 is AArch64; safe on Apple Silicon and any ARMv8-A target.
            seg_out[byte_idx] = vaddv_u8(vmul_u8(bit_val, pack_wt));
        }
    }
}

void deinterleave_parity_neon(
    std::span<const uint8_t> codewords,
    std::span<uint8_t>       parity_segs,
    const FecBlockParams&    p)
{
    assert(codewords.size()   == p.codeword_buf_size());
    assert(parity_segs.size() == 8u * p.col_height);

    const uint32_t ch = p.col_height;

    const uint8x8_t pack_wt = {128u, 64u, 32u, 16u, 8u, 4u, 2u, 1u};

    // TBL index for byte 4 of each codeword in an 8-codeword (40-byte) block.
    // Byte 4 offsets: 4, 9, 14, 19, 24, 29, 34, 39
    // For the 3-register table (reg0=0..15, reg1=16..31, reg2=24..39 → idx 32..47):
    //   offsets < 32 use direct index; offset 34 → 42, offset 39 → 47
    static const uint8x8_t par_idx = { 4, 9, 14, 19, 24, 29, 42, 47 };

    for (uint32_t seg = 0; seg < 8u; ++seg) {
        // seg 0..3 come from parity symbol 0 (high nibble of byte 4)
        // seg 4..7 come from parity symbol 1 (low nibble of byte 4)
        const bool     high_nib = (seg < 4u);
        const uint32_t bp       = seg % 4u;
        const int8x8_t nib_shift = vdup_n_s8(-(int8_t)(3u - bp));

        uint8_t* seg_out = parity_segs.data() + size_t{seg} * ch;

        for (uint32_t byte_idx = 0; byte_idx < ch; ++byte_idx) {
            const uint8_t* blk = codewords.data() + size_t{byte_idx} * 40u;

            const uint8x16x3_t block = {{
                vld1q_u8(blk),
                vld1q_u8(blk + 16),
                vld1q_u8(blk + 24),
            }};

            uint8x8_t cb = vqtbl3_u8(block, par_idx);

            uint8x8_t nibble;
            if (high_nib) {
                nibble = vshr_n_u8(cb, 4);
            } else {
                nibble = vand_u8(cb, vdup_n_u8(0xFu));
            }

            uint8x8_t bit_val = vand_u8(
                vreinterpret_u8_s8(vshl_s8(vreinterpret_s8_u8(nibble), nib_shift)),
                vdup_n_u8(1u));

            seg_out[byte_idx] = vaddv_u8(vmul_u8(bit_val, pack_wt));
        }
    }
}

} // namespace fec

#endif // __ARM_NEON
