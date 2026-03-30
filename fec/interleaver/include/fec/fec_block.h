#pragma once
#include <cstdint>
#include <cstddef>

namespace fec {

// Parameters describing one FEC block.
//
// Fixed topology (matches the diagram):
//   - 8 data columns, each column provides one 4-bit symbol per output word
//   - 4 segments per column (one bit of the symbol per segment)
//   - RS(10,8): 8 data symbols + 2 parity symbols per codeword = 5 bytes packed
//
// Runtime parameter:
//   - col_height: bytes per segment (default 8192 per the diagram)
//
// Codeword memory layout (5 bytes = 10 nibbles):
//   Byte 0: [Symbol 0 hi] [Symbol 1 lo]
//   Byte 1: [Symbol 2 hi] [Symbol 3 lo]
//   Byte 2: [Symbol 4 hi] [Symbol 5 lo]
//   Byte 3: [Symbol 6 hi] [Symbol 7 lo]
//   Byte 4: [Parity 0 hi] [Parity 1 lo]  <- zeroed by interleave, filled by RS
struct FecBlockParams {
    uint32_t col_height = 8192;  // bytes per segment

    static constexpr uint32_t NUM_COLUMNS      = 8;   // data symbols per word
    static constexpr uint32_t BITS_PER_SYMBOL  = 4;   // GF(16)
    static constexpr uint32_t SEGS_PER_COLUMN  = 4;   // one bit per segment
    static constexpr uint32_t NUM_SEGMENTS     = 32;  // NUM_COLUMNS * SEGS_PER_COLUMN
    static constexpr uint32_t DATA_SYMBOLS     = 8;
    static constexpr uint32_t PARITY_SYMBOLS   = 2;
    static constexpr uint32_t CODEWORD_SYMBOLS = 10;
    static constexpr uint32_t CODEWORD_BYTES   = 5;   // ceil(10 nibbles / 2)

    // Number of output words (one per bit of each segment column).
    // Each segment has col_height bytes * 8 bits = col_height*8 bits total.
    uint32_t num_words()     const noexcept { return col_height * 8u; }

    // Flat size of the segment input buffer (bit-packed layout).
    size_t   segment_buf_size()  const noexcept { return size_t{NUM_SEGMENTS} * col_height; }

    // Flat size of the codeword output buffer.
    size_t   codeword_buf_size() const noexcept { return size_t{num_words()} * CODEWORD_BYTES; }

};

} // namespace fec
