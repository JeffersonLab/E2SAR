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

} // namespace fec
