#pragma once
#include <span>
#include <cstdint>
#include <vector>
#include "fec/fec_block.h"

namespace fec {

struct RsDecodeContext {
    void* rs;     // rs_model*
    void* table;  // rs_decode_table*
    bool initialized{false};

    RsDecodeContext();
    ~RsDecodeContext();

    RsDecodeContext(const RsDecodeContext&) = delete;
    RsDecodeContext& operator=(const RsDecodeContext&) = delete;
};

// Decode codewords in-place given a set of erased data columns (0-7).
//
// Each erased column corresponds to 4 missing segments (column c maps to
// segments c*4 .. c*4+3). The RS(10,8) code can recover up to 2 erased
// columns.
//
// Before calling: bytes 0-3 of each codeword must contain the data nibbles
// (from interleave()), and byte 4 must contain the parity nibbles (from
// interleave_parity()). Erased column nibbles should be zeroed.
//
// After calling: the erased nibbles in bytes 0-3 are corrected in-place.
// Call deinterleave() on the corrected codewords to recover the segments.
//
// Returns 0 on success, -1 if too many erasures (>2 columns).
int rs_decode(
    std::span<uint8_t>      codewords,
    const FecBlockParams&   params,
    const std::vector<int>& erasedDataCols,
    RsDecodeContext&        ctx);

} // namespace fec
