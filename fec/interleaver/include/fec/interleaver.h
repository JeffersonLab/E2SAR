#pragma once
#include <span>
#include <cstdint>
#include "fec/fec_block.h"

namespace fec {

// interleave() — transforms segment-oriented input into codeword-oriented output.
//
// Input  (segments): flat buffer of NUM_SEGMENTS * col_height bytes.
//   Layout: segments[seg_index * col_height + byte_index]
//   Segment indexing: column c (0–7), bit-position b (0–3) → seg = c*4 + b
//     - Segment c*4+0 holds bit 3 (MSB) of symbol c for all words
//     - Segment c*4+1 holds bit 2
//     - Segment c*4+2 holds bit 1
//     - Segment c*4+3 holds bit 0 (LSB) of symbol c for all words
//   Bit ordering within each segment byte: MSB-first.
//     Bit 7 of byte 0 → word 0, bit 0 of byte 0 → word 7, etc.
//
// Output (codewords): flat buffer of num_words * CODEWORD_BYTES bytes.
//   Layout: codewords[word * CODEWORD_BYTES .. +CODEWORD_BYTES-1]
//   Each 5-byte codeword = 10 packed nibbles:
//     Byte 0: [Symbol 0 hi-nibble | Symbol 1 lo-nibble]
//     Byte 1: [Symbol 2 hi-nibble | Symbol 3 lo-nibble]
//     Byte 2: [Symbol 4 hi-nibble | Symbol 5 lo-nibble]
//     Byte 3: [Symbol 6 hi-nibble | Symbol 7 lo-nibble]
//     Byte 4: [Parity 0 hi-nibble | Parity 1 lo-nibble]  ← always zeroed
//
// Precondition: segments.size() == params.segment_buf_size()
//               codewords.size() == params.codeword_buf_size()
void interleave(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);

// interleave_magic() — 64-bit scalar interleave using the magic-multiply bit-packing trick.
// Same semantics and preconditions as interleave().
//
// For each byte_idx, gathers one byte from each of the 32 segments into a 32-byte array,
// then processes it as four uint64_t values. For each of 8 bit positions, four multiplies
// by the constant 0x0102040810204080 pack bit k of all 8 bytes in each uint64_t into the
// MSByte of the product in one instruction, extracting a full bit-plane in ~20 scalar ops.
// Segments are gathered in reversed bit-position order within each column (s^3 index) so
// that after the multiply the nibbles are in natural order, requiring only a nibble-swap.
//
// Works on any 64-bit CPU (no SIMD required).
void interleave_magic(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);

#if defined(__ARM_NEON)
// interleave_neon() — ARM NEON accelerated interleave.
// Same semantics and preconditions as interleave().
// Uses byte-major outer loop with NEON bit-transpose (vmul + vpadd cascade)
// to compute all 8 symbol nibbles per word in one pass per byte_idx.
void interleave_neon(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);

// interleave_neon_tiled() — ARM NEON tiled interleave with in-register transpose.
// Same semantics and preconditions as interleave().
//
// Processes byte positions in tiles of 16.  For each tile, loads 16 bytes from
// each of the 32 segments via vld1q_u8 (amortising the strided loads over 16
// byte positions), transposes two 16x16 sub-matrices in-register using a 4-stage
// vtrn butterfly, spills the results to a pair of 256-byte stack buffers, then
// runs the existing IL_BIT bit-extraction pipeline for each of the 16 columns.
// The scalar gather loop and its store-forwarding penalty are eliminated entirely.
// Falls back to the scalar gather for the final col_height % 16 positions.
void interleave_neon_tiled(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);

#endif // __ARM_NEON

#if defined(__AVX2__)
// interleave_avx2() — AVX2 accelerated interleave using movemask bit-transpose.
// Same semantics and preconditions as interleave().
//
// Uses _mm256_movemask_epi8 to extract one bit-plane from all 32 segment bytes
// in a single instruction, replacing the 4-multiply magic-multiply approach.
// The 32 gathered bytes are loaded into one YMM register and reordered via
// _mm256_shuffle_epi8 (reversing bytes within each 4-byte column group) so that
// movemask produces nibbles in natural bit-weight order.  A scalar nibble-swap
// then packs symbol pairs into codeword bytes.
//
// Per 8 codewords: 1 load + 1 shuffle + 8×(slli + movemask + nibble-swap + store).
void interleave_avx2(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);

// interleave_rs_encode_avx2() — fused interleave + RS encode in a single pass.
//
// Equivalent to calling interleave_avx2() followed by rs_encode_avx2(), but
// processes 32 codewords (4 consecutive byte_idx groups, 160 bytes) per outer
// iteration so that the codeword data written by the interleave phase is read
// back by the RS encode phase from L1 cache rather than L3.
//
// On return, codewords[word*5+4] holds the RS parity byte for every word
// (i.e. the buffer is fully encoded, as if interleave + rs_encode had been called).
void interleave_rs_encode_avx2(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);
#endif // __AVX2__

#if defined(__APPLE__)
// interleave_metal() — Metal GPU implementation of the interleave operation.
// Same semantics and preconditions as interleave().
//
// On Apple Silicon (unified memory architecture) the caller's buffers are
// wrapped directly as MTLResourceStorageModeShared buffers — no copy occurs
// when the pointers are page-aligned (typical for std::vector on macOS).
// For non-aligned buffers a single memcpy is performed on each side.
//
// Metal context (device, pipeline, queue) is initialized once on first call.
void interleave_metal(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);

// interleave_metal_gpu_us() — like interleave_metal() but returns the GPU
// kernel execution time in microseconds (excludes host-side overhead).
// Uses MTLCommandBuffer GPUStartTime/GPUEndTime for accurate GPU timing.
double interleave_metal_gpu_us(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);
#endif // __APPLE__


// interleave_parity() — writes 8 parity segments into byte 4 of each codeword.
//
// This is the inverse of deinterleave_parity(): it reads bits from parity
// segment bytes and packs them into the parity nibbles of each codeword.
//
// Parity column mapping (byte 4 of each codeword):
//   High nibble = parity symbol 0:
//     bit 3 ← segment 0, bit 2 ← segment 1, bit 1 ← segment 2, bit 0 ← segment 3
//   Low nibble = parity symbol 1:
//     bit 3 ← segment 4, bit 2 ← segment 5, bit 1 ← segment 6, bit 0 ← segment 7
//
// Precondition: parity_segs.size() == 8 * params.col_height
//               codewords.size()   == params.codeword_buf_size()
// Note: only byte 4 of each codeword is written; bytes 0-3 are untouched.
void interleave_parity(
    std::span<const uint8_t> parity_segs,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);

#if defined(__ARM_NEON)
void interleave_parity_neon(
    std::span<const uint8_t> parity_segs,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    params);
#endif // __ARM_NEON

} // namespace fec
