#pragma once
#include <span>
#include <cstdint>
#include "fec/fec_block.h"

namespace fec {

// rs_encode() — fills the parity byte (byte 4) of every 5-byte codeword in the
// codeword buffer produced by interleave().
//
// Each 5-byte codeword is an RS(10,8) codeword over GF(16):
//   Bytes 0-3: 8 data symbols packed as nibble pairs (filled by interleaver)
//   Byte 4:    2 parity symbols packed as nibble pair (zeroed by interleaver,
//              computed and written here)
//
// The computation is: parity = P^T * data over GF(16), where P is the 8x2
// parity sub-matrix of the systematic generator matrix G = [I | P].
//
// Precondition: codewords.size() == params.codeword_buf_size()
void rs_encode(std::span<uint8_t> codewords, const FecBlockParams& params);

#if defined(__APPLE__)
// rs_encode_metal() — Metal GPU implementation of rs_encode().
// Same semantics and preconditions as rs_encode().
//
// Operates in-place on the codeword buffer.  Uses a pointer-matched MTLBuffer
// cache so that page-aligned (mmap-backed) caller allocations avoid repeated
// GPU page-table registration overhead across successive calls.
//
// Batching: pass FecBlockParams with col_height = N_BLOCKS * per_block_col_height
// and a contiguous super-buffer codeword array holding N blocks concatenated.
// The dispatch scales linearly with params.num_words().
void rs_encode_metal(std::span<uint8_t> codewords, const FecBlockParams& params);

// rs_encode_metal_gpu_us() — like rs_encode_metal() but returns the GPU
// kernel execution time in microseconds (excludes host-side overhead).
// Uses MTLCommandBuffer GPUStartTime/GPUEndTime for accurate GPU timing.
double rs_encode_metal_gpu_us(std::span<uint8_t> codewords, const FecBlockParams& params);
#endif // __APPLE__

#if defined(__ARM_NEON)
// rs_encode_neon() — ARM NEON SIMD implementation of rs_encode().
// Processes 8 codewords per iteration (one per NEON lane).
// Same semantics and preconditions as rs_encode().
void rs_encode_neon(std::span<uint8_t> codewords, const FecBlockParams& params);
#endif // __ARM_NEON

} // namespace fec
