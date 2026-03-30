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
// AVX2 implementation
// =============================================================================
#if defined(__AVX2__)
#include <immintrin.h>

namespace fec {

// ---------------------------------------------------------------------------
// interleave_avx2
//
// Byte-major outer loop (same structure as magic-multiply and NEON):
// each byte_idx covers 8 consecutive codewords.
//
// Core primitive — _mm256_movemask_epi8:
//   Extracts the MSB (bit 7) of all 32 bytes in a YMM register into one
//   uint32_t.  Combined with _mm256_slli_epi16(data, n), which shifts each
//   16-bit element left by n, movemask extracts bit (7-n) of every byte.
//   (The 16-bit shift can leak bits across byte boundaries within each pair,
//   but movemask only samples bit 7 of each byte, which is always correct.)
//
// Gather reordering: bytes are loaded in natural segment order (0-31), then
// reordered in-register via _mm256_shuffle_epi8 with a reverse-within-4
// permutation.  This is equivalent to the XOR-3 index trick used by
// interleave_magic: within each column group of 4 bytes, the bit-position
// order is reversed so that movemask produces nibbles with natural bit
// weights (bit 3 = MSB at the highest-numbered position in each group).
//
// Post-processing: identical nibble-swap as interleave_magic.
// ---------------------------------------------------------------------------
void interleave_avx2(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(segments.size()  == p.segment_buf_size());
    assert(codewords.size() == p.codeword_buf_size());

    const uint32_t ch = p.col_height;

    // Reverse bytes within each 4-byte group (equivalent to XOR-3 reorder).
    // Applied per 128-bit lane by _mm256_shuffle_epi8.
    const __m256i rev4 = _mm256_setr_epi8(
        3,  2,  1,  0,   7,  6,  5,  4,  11, 10,  9,  8,  15, 14, 13, 12,
        3,  2,  1,  0,   7,  6,  5,  4,  11, 10,  9,  8,  15, 14, 13, 12);

    uint32_t byte_idx = 0;

    // Helper: emit 8 codewords from a shuffled 256-bit register into cw[0..39].
    // Avoids duplicating the shift/movemask/nibble-swap block four times.
    auto emit8 = [](const __m256i& data, uint8_t* cw) {
        for (uint32_t shift = 0; shift < 8u; ++shift) {
            const __m256i sh = _mm256_slli_epi16(data, static_cast<int>(shift));
            const uint32_t plane  = static_cast<uint32_t>(_mm256_movemask_epi8(sh));
            const uint32_t packed = ((plane & 0x0F0F0F0Fu) << 4)
                                  | ((plane >> 4)           & 0x0F0F0F0Fu);
            const uint32_t off = shift * 5u;
            std::memcpy(cw + off, &packed, 4);
            cw[off + 4] = 0u;
        }
    };

    // ---- 4× unrolled main loop for ILP (four independent byte_idx values) ----
    for (; byte_idx + 3 < ch; byte_idx += 4) {
        alignas(32) uint8_t g0[32], g1[32], g2[32], g3[32];
        for (uint32_t s = 0; s < 32u; ++s) {
            const uint32_t off = s * ch + byte_idx;
            // Prefetch the addresses this segment will need in the next 4-group.
            _mm_prefetch(reinterpret_cast<const char*>(&segments[off + 4]),
                         _MM_HINT_T0);
            g0[s] = segments[off];
            g1[s] = segments[off + 1];
            g2[s] = segments[off + 2];
            g3[s] = segments[off + 3];
        }

        const __m256i d0 = _mm256_shuffle_epi8(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(g0)), rev4);
        const __m256i d1 = _mm256_shuffle_epi8(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(g1)), rev4);
        const __m256i d2 = _mm256_shuffle_epi8(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(g2)), rev4);
        const __m256i d3 = _mm256_shuffle_epi8(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(g3)), rev4);

        uint8_t* cw0 = codewords.data() + size_t{byte_idx} * 40u;
        emit8(d0, cw0);
        emit8(d1, cw0 + 40u);
        emit8(d2, cw0 + 80u);
        emit8(d3, cw0 + 120u);
    }

    // ---- 2× unrolled tail for remaining pairs ----
    for (; byte_idx + 1 < ch; byte_idx += 2) {
        alignas(32) uint8_t g0[32], g1[32];
        for (uint32_t s = 0; s < 32u; ++s) {
            const uint32_t off = s * ch + byte_idx;
            g0[s] = segments[off];
            g1[s] = segments[off + 1];
        }

        const __m256i d0 = _mm256_shuffle_epi8(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(g0)), rev4);
        const __m256i d1 = _mm256_shuffle_epi8(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(g1)), rev4);

        uint8_t* cw0 = codewords.data() + size_t{byte_idx} * 40u;
        emit8(d0, cw0);
        emit8(d1, cw0 + 40u);
    }

    // ---- Single-element tail for odd col_height ----
    for (; byte_idx < ch; ++byte_idx) {
        alignas(32) uint8_t gathered[32];
        for (uint32_t s = 0; s < 32u; ++s) {
            gathered[s] = segments[s * ch + byte_idx];
        }
        const __m256i data = _mm256_shuffle_epi8(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(gathered)), rev4);
        emit8(data, codewords.data() + size_t{byte_idx} * 40u);
    }
}

} // namespace fec

// =============================================================================
// interleave_rs_encode_avx2 — fused interleave + RS encode
// =============================================================================

namespace fec {

// GF(16) tables — duplicated here to avoid a link-time dependency on rs_encode.cpp.
static constexpr uint8_t il_gf_log[16]    = { 1,2,4,8,3,6,12,11,5,10,7,14,15,13,9,0 };
static constexpr uint8_t il_gf_exp[16]    = { 15,0,1,4,2,8,5,10,3,14,9,7,6,13,11,12 };
static constexpr uint8_t il_coeff0_exp[8] = { 11, 5, 11, 14, 10, 0, 12, 5 };
static constexpr uint8_t il_coeff1_exp[8] = {  8,14,  2, 13,  3, 0,  8, 3 };

// GF(16) multiply for 32 lanes: product = gf_mul(symbol, alpha^coeff_exp).
// data_exp = shuffle(exp_tbl, symbol)  — pre-computed, shared across parities.
// zm       = cmpeq(symbol, 0)          — pre-computed zero mask.
static inline __m256i il_gf_mul(__m256i log_tbl,
                                 __m256i data_exp, __m256i zm,
                                 uint8_t coeff_exp,
                                 __m256i fifteen)
{
    const __m256i coeff = _mm256_set1_epi8(static_cast<int8_t>(coeff_exp));
    const __m256i sum   = _mm256_add_epi8(data_exp, coeff);
    const __m256i ge15  = _mm256_cmpgt_epi8(sum, _mm256_set1_epi8(14));
    const __m256i mod   = _mm256_sub_epi8(sum, _mm256_and_si256(ge15, fifteen));
    return _mm256_andnot_si256(zm, _mm256_shuffle_epi8(log_tbl, mod));
}

// ---------------------------------------------------------------------------
// interleave_rs_encode_avx2
//
// Fused pipeline: per outer iteration (4 consecutive byte_idx → 32 codewords):
//
//   1. Gather 32 segment bytes × 4 byte_idx values into g0..g3; shuffle → d0..d3.
//   2. Inner loop (sh = 0..7):
//        slli(di, sh) + movemask → plane_i  (uint32_t, 1 bit per segment)
//        nibble-swap             → pk_i     (4 packed data bytes for one codeword)
//        memcpy pk_i  → cw_base + i*40 + sh*5          (store data bytes)
//        byte-extract → rs_b{0..3}[i*8 + sh]           (no stride-5 read-back)
//   3. Load rs_b{0..3} → YMM; nibble-split; GF multiply-accumulate; pack parity.
//   4. Scatter parity[i] → cw_base[i*5 + 4]  for i=0..31.
//
// Eliminated vs. calling interleave_avx2 + rs_encode_avx2 separately:
//   - The stride-5 scatter (interleaver writing byte 4 = 0)
//   - The stride-5 gather  (RS encoder reading bytes 0-3 back from cache)
// Both replaced by scalar byte-extractions from register values pk_i.
// ---------------------------------------------------------------------------
void interleave_rs_encode_avx2(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(segments.size()  == p.segment_buf_size());
    assert(codewords.size() == p.codeword_buf_size());

    const uint32_t ch = p.col_height;

    // ---- Interleave constants ----
    // Reverse bytes within each 4-byte group (XOR-3 reorder for nibble bit-weight).
    const __m256i rev4 = _mm256_setr_epi8(
         3,  2,  1,  0,   7,  6,  5,  4,  11, 10,  9,  8,  15, 14, 13, 12,
         3,  2,  1,  0,   7,  6,  5,  4,  11, 10,  9,  8,  15, 14, 13, 12);

    // ---- RS encode constants ----
    const __m256i exp_tbl = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(il_gf_exp)));
    const __m256i log_tbl = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(il_gf_log)));
    const __m256i zero    = _mm256_setzero_si256();
    const __m256i nib     = _mm256_set1_epi8(0x0F);
    const __m256i fifteen = _mm256_set1_epi8(15);
    // Permutation index for the 4×8 uint32_t → byte-lane transpose in Phase B.
    // After packus_epi32 + packus_epi16 the 8 result dwords are in order
    // {a[0..3], b[0..3], c[0..3], d[0..3], a[4..7], b[4..7], c[4..7], d[4..7]};
    // this permutation interleaves them to {a[0..7], b[0..7], c[0..7], d[0..7]}.
    const __m256i perm_interleave = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
    const __m256i mask_lo         = _mm256_set1_epi32(0xFF);

    uint32_t byte_idx = 0;

    // ---- Main loop: 4 groups (32 codewords) per iteration ----
    for (; byte_idx + 3 < ch; byte_idx += 4) {

        // Step 1: gather + shuffle.
        alignas(32) uint8_t g0[32], g1[32], g2[32], g3[32];
        for (uint32_t s = 0; s < 32u; ++s) {
            const uint32_t off = s * ch + byte_idx;
            _mm_prefetch(reinterpret_cast<const char*>(&segments[off + 4]), _MM_HINT_T0);
            g0[s] = segments[off];
            g1[s] = segments[off + 1];
            g2[s] = segments[off + 2];
            g3[s] = segments[off + 3];
        }
        const __m256i d0 = _mm256_shuffle_epi8(_mm256_load_si256(reinterpret_cast<const __m256i*>(g0)), rev4);
        const __m256i d1 = _mm256_shuffle_epi8(_mm256_load_si256(reinterpret_cast<const __m256i*>(g1)), rev4);
        const __m256i d2 = _mm256_shuffle_epi8(_mm256_load_si256(reinterpret_cast<const __m256i*>(g2)), rev4);
        const __m256i d3 = _mm256_shuffle_epi8(_mm256_load_si256(reinterpret_cast<const __m256i*>(g3)), rev4);

        // Step 2: movemask → packed data bytes → codeword buffer.
        // pk_arr[g*8+sh] = one packed uint32_t per codeword (4 data bytes).
        // Using dword stores avoids the store-to-load forwarding stall that
        // occurred when the old code did 128 scalar byte-stores to rs_b arrays
        // and then read them back with 4 aligned 256-bit loads.
        alignas(32) uint32_t pk_arr[32];
        uint8_t* const cw_base = codewords.data() + size_t{byte_idx} * 40u;

        for (uint32_t sh = 0; sh < 8u; ++sh) {
            const int ishift = static_cast<int>(sh);
            const uint32_t pk0 = [&]{
                const uint32_t pl = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_slli_epi16(d0, ishift)));
                return ((pl & 0x0F0F0F0Fu) << 4) | ((pl >> 4) & 0x0F0F0F0Fu);
            }();
            const uint32_t pk1 = [&]{
                const uint32_t pl = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_slli_epi16(d1, ishift)));
                return ((pl & 0x0F0F0F0Fu) << 4) | ((pl >> 4) & 0x0F0F0F0Fu);
            }();
            const uint32_t pk2 = [&]{
                const uint32_t pl = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_slli_epi16(d2, ishift)));
                return ((pl & 0x0F0F0F0Fu) << 4) | ((pl >> 4) & 0x0F0F0F0Fu);
            }();
            const uint32_t pk3 = [&]{
                const uint32_t pl = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_slli_epi16(d3, ishift)));
                return ((pl & 0x0F0F0F0Fu) << 4) | ((pl >> 4) & 0x0F0F0F0Fu);
            }();

            // Store 4 data bytes; byte 4 (parity) written after RS computation.
            const uint32_t cw_off = sh * 5u;
            std::memcpy(cw_base + 0u * 40u + cw_off, &pk0, 4);
            std::memcpy(cw_base + 1u * 40u + cw_off, &pk1, 4);
            std::memcpy(cw_base + 2u * 40u + cw_off, &pk2, 4);
            std::memcpy(cw_base + 3u * 40u + cw_off, &pk3, 4);

            // Natural-width dword stores (no forwarding stall on the later YMM loads).
            pk_arr[0u * 8u + sh] = pk0;
            pk_arr[1u * 8u + sh] = pk1;
            pk_arr[2u * 8u + sh] = pk2;
            pk_arr[3u * 8u + sh] = pk3;
        }

        // Transpose pk_arr (32 × uint32_t) → rb0..rb3 (one 32-byte YMM per byte lane).
        // Each pk_arr entry = [b0, b1, b2, b3]; we want rb_j[i] = pk_arr[i].b_j.
        // Strategy per byte lane j:
        //   1. Shift each dword right by j*8 and AND with 0xFF → dword-wide scalars.
        //   2. packus_epi32 pairs → 16-bit words (upper byte is 0, no saturation loss).
        //   3. packus_epi16 pairs → 8-bit bytes packed into two 128-bit halves.
        //   4. permutevar8x32 to interleave halves into final [a0..a7,b0..b7,c0..c7,d0..d7].
        const __m256i va = _mm256_load_si256(reinterpret_cast<const __m256i*>(pk_arr));
        const __m256i vb = _mm256_load_si256(reinterpret_cast<const __m256i*>(pk_arr + 8));
        const __m256i vc = _mm256_load_si256(reinterpret_cast<const __m256i*>(pk_arr + 16));
        const __m256i vd = _mm256_load_si256(reinterpret_cast<const __m256i*>(pk_arr + 24));

        auto xpose = [&](__m256i a, __m256i b, __m256i c, __m256i d, int pos) -> __m256i {
            const int sh32 = pos * 8;
            a = _mm256_and_si256(_mm256_srli_epi32(a, sh32), mask_lo);
            b = _mm256_and_si256(_mm256_srli_epi32(b, sh32), mask_lo);
            c = _mm256_and_si256(_mm256_srli_epi32(c, sh32), mask_lo);
            d = _mm256_and_si256(_mm256_srli_epi32(d, sh32), mask_lo);
            const __m256i w01 = _mm256_packus_epi32(a, b);
            const __m256i w23 = _mm256_packus_epi32(c, d);
            return _mm256_permutevar8x32_epi32(_mm256_packus_epi16(w01, w23), perm_interleave);
        };

        const __m256i rb0 = xpose(va, vb, vc, vd, 0);
        const __m256i rb1 = xpose(va, vb, vc, vd, 1);
        const __m256i rb2 = xpose(va, vb, vc, vd, 2);
        const __m256i rb3 = xpose(va, vb, vc, vd, 3);

        // Step 3: RS encode — process one input byte register at a time so that
        // exp/zm temporaries are consumed before the next batch, keeping peak live
        // YMM count ≤14 (fits in Zen 3's 16 physical registers without spills).
        auto gmul = [&](__m256i de, __m256i zm, uint8_t ce) {
            return il_gf_mul(log_tbl, de, zm, ce, fifteen);
        };

        // rb0 → sym0 (high nibble, coeff 11/8) + sym1 (low nibble, coeff 5/14)
        __m256i acc0, acc1;
        {
            const __m256i h = _mm256_and_si256(_mm256_srli_epi16(rb0, 4), nib);
            const __m256i l = _mm256_and_si256(rb0, nib);
            const __m256i eh = _mm256_shuffle_epi8(exp_tbl, h), zmh = _mm256_cmpeq_epi8(h, zero);
            const __m256i el = _mm256_shuffle_epi8(exp_tbl, l), zml = _mm256_cmpeq_epi8(l, zero);
            acc0 =                        gmul(eh, zmh, 11);
            acc0 = _mm256_xor_si256(acc0, gmul(el, zml,  5));
            acc1 =                        gmul(eh, zmh,  8);
            acc1 = _mm256_xor_si256(acc1, gmul(el, zml, 14));
        }
        // rb1 → sym2 (high, coeff 11/2) + sym3 (low, coeff 14/13)
        {
            const __m256i h = _mm256_and_si256(_mm256_srli_epi16(rb1, 4), nib);
            const __m256i l = _mm256_and_si256(rb1, nib);
            const __m256i eh = _mm256_shuffle_epi8(exp_tbl, h), zmh = _mm256_cmpeq_epi8(h, zero);
            const __m256i el = _mm256_shuffle_epi8(exp_tbl, l), zml = _mm256_cmpeq_epi8(l, zero);
            acc0 = _mm256_xor_si256(acc0, gmul(eh, zmh, 11));
            acc0 = _mm256_xor_si256(acc0, gmul(el, zml, 14));
            acc1 = _mm256_xor_si256(acc1, gmul(eh, zmh,  2));
            acc1 = _mm256_xor_si256(acc1, gmul(el, zml, 13));
        }
        // rb2 → sym4 (high, coeff 10/3) + sym5 (low, coeff 0/0)
        {
            const __m256i h = _mm256_and_si256(_mm256_srli_epi16(rb2, 4), nib);
            const __m256i l = _mm256_and_si256(rb2, nib);
            const __m256i eh = _mm256_shuffle_epi8(exp_tbl, h), zmh = _mm256_cmpeq_epi8(h, zero);
            const __m256i el = _mm256_shuffle_epi8(exp_tbl, l), zml = _mm256_cmpeq_epi8(l, zero);
            acc0 = _mm256_xor_si256(acc0, gmul(eh, zmh, 10));
            acc0 = _mm256_xor_si256(acc0, gmul(el, zml,  0));
            acc1 = _mm256_xor_si256(acc1, gmul(eh, zmh,  3));
            acc1 = _mm256_xor_si256(acc1, gmul(el, zml,  0));
        }
        // rb3 → sym6 (high, coeff 12/8) + sym7 (low, coeff 5/3)
        {
            const __m256i h = _mm256_and_si256(_mm256_srli_epi16(rb3, 4), nib);
            const __m256i l = _mm256_and_si256(rb3, nib);
            const __m256i eh = _mm256_shuffle_epi8(exp_tbl, h), zmh = _mm256_cmpeq_epi8(h, zero);
            const __m256i el = _mm256_shuffle_epi8(exp_tbl, l), zml = _mm256_cmpeq_epi8(l, zero);
            acc0 = _mm256_xor_si256(acc0, gmul(eh, zmh, 12));
            acc0 = _mm256_xor_si256(acc0, gmul(el, zml,  5));
            acc1 = _mm256_xor_si256(acc1, gmul(eh, zmh,  8));
            acc1 = _mm256_xor_si256(acc1, gmul(el, zml,  3));
        }

        // Pack parity nibbles: byte = (p0 << 4) | p1.
        const __m256i par = _mm256_or_si256(
            _mm256_slli_epi16(_mm256_and_si256(acc0, nib), 4),
            _mm256_and_si256(acc1, nib));

        // Step 4: scatter parity to byte 4 of each codeword (stride 5).
        alignas(32) uint8_t parity[32];
        _mm256_store_si256(reinterpret_cast<__m256i*>(parity), par);
        for (int i = 0; i < 32; ++i) {
            cw_base[i * 5 + 4] = parity[i];
        }
    }

    // ---- Tail: scalar interleave + encode for up to 3 remaining byte_idx values ----
    for (; byte_idx < ch; ++byte_idx) {
        alignas(32) uint8_t gathered[32];
        for (uint32_t s = 0; s < 32u; ++s)
            gathered[s] = segments[s * ch + byte_idx];

        const __m256i data = _mm256_shuffle_epi8(
            _mm256_load_si256(reinterpret_cast<const __m256i*>(gathered)), rev4);

        uint8_t* cw = codewords.data() + size_t{byte_idx} * 40u;

        for (uint32_t sh = 0; sh < 8u; ++sh) {
            const uint32_t pl = static_cast<uint32_t>(
                _mm256_movemask_epi8(_mm256_slli_epi16(data, static_cast<int>(sh))));
            const uint32_t packed = ((pl & 0x0F0F0F0Fu) << 4) | ((pl >> 4) & 0x0F0F0F0Fu);
            const uint32_t off = sh * 5u;
            std::memcpy(cw + off, &packed, 4);

            // Scalar RS encode for this one codeword.
            // packed byte j = (sym_even << 4) | sym_odd, so:
            //   sym0 = high nibble of byte 0, sym1 = low nibble of byte 0, etc.
            const uint8_t s0 = static_cast<uint8_t>((packed >>  4) & 0xFu);
            const uint8_t s1 = static_cast<uint8_t>( packed        & 0xFu);
            const uint8_t s2 = static_cast<uint8_t>((packed >> 12) & 0xFu);
            const uint8_t s3 = static_cast<uint8_t>((packed >>  8) & 0xFu);
            const uint8_t s4 = static_cast<uint8_t>((packed >> 20) & 0xFu);
            const uint8_t s5 = static_cast<uint8_t>((packed >> 16) & 0xFu);
            const uint8_t s6 = static_cast<uint8_t>((packed >> 28) & 0xFu);
            const uint8_t s7 = static_cast<uint8_t>((packed >> 24) & 0xFu);
            const uint8_t syms[8] = { s0, s1, s2, s3, s4, s5, s6, s7 };

            uint8_t p0 = 0u, p1 = 0u;
            for (int j = 0; j < 8; ++j) {
                if (syms[j] != 0u) {
                    const uint8_t es = il_gf_exp[syms[j]];
                    p0 ^= il_gf_log[static_cast<uint8_t>((es + il_coeff0_exp[j]) % 15u)];
                    p1 ^= il_gf_log[static_cast<uint8_t>((es + il_coeff1_exp[j]) % 15u)];
                }
            }
            cw[off + 4] = static_cast<uint8_t>((p0 << 4u) | p1);
        }
    }
}

} // namespace fec

#endif // __AVX2__

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
