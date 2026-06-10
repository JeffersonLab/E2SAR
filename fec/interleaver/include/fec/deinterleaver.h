#pragma once
#include <span>
#include <cstdint>
#include "fec/fec_block.h"

namespace fec {

// deinterleave() — inverse of interleave().
//
// Reads data symbol nibbles from each codeword (bytes 0–3) and
// reconstructs the segment-oriented buffer.  Parity nibbles (byte 4)
// are ignored.  The segments buffer is fully overwritten.
//
// Precondition: codewords.size() == params.codeword_buf_size()
//               segments.size()  == params.segment_buf_size()
void deinterleave(
    std::span<const uint8_t> codewords,
    std::span<uint8_t>       segments,
    const FecBlockParams&    params);

#if defined(__ARM_NEON)
// deinterleave_neon() — ARM NEON accelerated deinterleave.
// Same semantics and preconditions as deinterleave().
// Uses segment-major outer loop (cache-friendly sequential output writes)
// with NEON gather+horizontal-reduce for each output byte.
void deinterleave_neon(
    std::span<const uint8_t> codewords,
    std::span<uint8_t>       segments,
    const FecBlockParams&    params);
#endif // __ARM_NEON

// deinterleave_parity() — extracts 8 parity segments from encoded codewords.
//
// Reads parity nibbles from byte 4 of each 5-byte codeword and reconstructs
// 8 parity segments.  This is the inverse of the parity portion of the
// interleave/RS encode pipeline.
//
// Parity column mapping (byte 4 of each codeword):
//   High nibble = parity symbol 0:
//     bit 3 → segment 0, bit 2 → segment 1, bit 1 → segment 2, bit 0 → segment 3
//   Low nibble = parity symbol 1:
//     bit 3 → segment 4, bit 2 → segment 5, bit 1 → segment 6, bit 0 → segment 7
//
// Precondition: codewords.size()   == params.codeword_buf_size()
//               parity_segs.size() == 8 * params.col_height
void deinterleave_parity(
    std::span<const uint8_t> codewords,
    std::span<uint8_t>       parity_segs,
    const FecBlockParams&    params);

#if defined(__ARM_NEON)
// deinterleave_parity_neon() — ARM NEON accelerated parity extraction.
// Same semantics and preconditions as deinterleave_parity().
void deinterleave_parity_neon(
    std::span<const uint8_t> codewords,
    std::span<uint8_t>       parity_segs,
    const FecBlockParams&    params);
#endif // __ARM_NEON

} // namespace fec
