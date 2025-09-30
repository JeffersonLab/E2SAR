#ifndef __ejfat_rs_avx512_decoder_h
#define __ejfat_rs_avx512_decoder_h

#include <immintrin.h>
#include <stdint.h>
#include <stdlib.h>

// --------------------------------------------------------------------------------------
// Minimal Reed-Solomon Decoder with AVX-512 SIMD optimizations
// RS(10,8) configuration: 8 data symbols + 2 parity symbols over GF(16)
// --------------------------------------------------------------------------------------

// Include shared RS model with GF(16) lookup tables
#include "../prototype/python/rs_model.h"

// --------------------------------------------------------------------------------------
// Decoder table structures
// --------------------------------------------------------------------------------------

// Decoder table entry structure for pre-computed inverse matrices
typedef struct {
  int erasure_pattern[2];  // Up to 2 erasures for RS(10,8)
  int num_erasures;        // Number of erasures in this pattern
  char inv_matrix[8][8];   // Pre-computed 8x8 inverse matrix
  int valid;               // 1 if this entry is valid, 0 otherwise
} rs_decode_table_entry_avx512;

// Decoder table structure
typedef struct {
  rs_decode_table_entry_avx512 *entries;  // Dynamic array of table entries
  int size;                                // Number of entries in table
  int capacity;                            // Allocated capacity
} rs_decode_table_avx512;

// --------------------------------------------------------------------------------------
// Helper: Scalar GF multiplication (AVX-512 version to be optimized later)
// --------------------------------------------------------------------------------------
static inline char avx512_gf_mul(char a, char b) {
  if (a == 0 || b == 0) return 0;
  char a_exp = _ejfat_rs_gf_exp_seq[a];
  char b_exp = _ejfat_rs_gf_exp_seq[b];
  char sum_exp = (a_exp + b_exp) % 15;
  return _ejfat_rs_gf_log_seq[sum_exp];
}

// --------------------------------------------------------------------------------------
// Single-nibble AVX-512 RS decoder
// Decodes received symbols with erasures using pre-computed inverse matrices
// Parameters:
//   - table: Pre-computed decoder table with inverse matrices
//   - received: 10 symbols (8 data + 2 parity)
//   - erasure_locations: Array of erased symbol indices (0-7)
//   - num_erasures: Number of erasures (0-2)
//   - decoded: Output buffer for 8 decoded data symbols
// Returns: 0 on success, -1 on error
// --------------------------------------------------------------------------------------
int avx512_rs_decode(rs_decode_table_avx512 *table, const char *received,
                     int *erasure_locations, int num_erasures,
                     char *decoded) {

  if (num_erasures > 2) {
    return -1;
  }

  // Find matching table entry
  rs_decode_table_entry_avx512 *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry_avx512 *candidate = &table->entries[t];

    if (!candidate->valid || candidate->num_erasures != num_erasures) {
      continue;
    }

    int match = 0;
    if (num_erasures == 0) {
      match = 1;
    } else if (num_erasures == 1) {
      match = (candidate->erasure_pattern[0] == erasure_locations[0]);
    } else if (num_erasures == 2) {
      match = ((candidate->erasure_pattern[0] == erasure_locations[0] &&
                candidate->erasure_pattern[1] == erasure_locations[1]) ||
               (candidate->erasure_pattern[0] == erasure_locations[1] &&
                candidate->erasure_pattern[1] == erasure_locations[0]));
    }

    if (match) {
      entry = candidate;
      break;
    }
  }

  if (!entry) {
    return -1;
  }

  // Prepare received vector with substitutions (10 symbols: 8 data + 2 parity)
  char rx_modified[8];
  for (int i = 0; i < 8; i++) {
    rx_modified[i] = received[i];
  }
  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      rx_modified[erasure_locations[i]] = received[8 + i];
    }
  }

  // Matrix-vector multiplication in GF(16)
  for (int i = 0; i < 8; i++) {
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= avx512_gf_mul(entry->inv_matrix[i][j], rx_modified[j]);
    }
    decoded[i] = result;
  }

  return 0;
}

// --------------------------------------------------------------------------------------
// Dual-nibble AVX-512 RS decoder
// Decodes received bytes with erasures for both upper and lower nibble streams
// Parameters:
//   - table: Pre-computed decoder table with inverse matrices
//   - received_bytes: 10 bytes (8 data + 2 parity)
//   - erasure_locations: Array of erased byte indices (0-7)
//   - num_erasures: Number of erasures (0-2)
//   - decoded_bytes: Output buffer for 8 decoded data bytes
// Returns: 0 on success, -1 on error
// --------------------------------------------------------------------------------------
int avx512_rs_decode_dual_nibble(rs_decode_table_avx512 *table, const char *received_bytes,
                                  int *erasure_locations, int num_erasures,
                                  char *decoded_bytes) {

  if (num_erasures > 2) {
    return -1;
  }

  // Extract lower and upper nibbles from data bytes
  char lower_data[8];
  char upper_data[8];
  for (int i = 0; i < 8; i++) {
    lower_data[i] = received_bytes[i] & 0x0F;
    upper_data[i] = (received_bytes[i] >> 4) & 0x0F;
  }

  // Extract parity nibbles
  char lower_parity[2] = {received_bytes[8] & 0x0F, received_bytes[9] & 0x0F};
  char upper_parity[2] = {(received_bytes[8] >> 4) & 0x0F, (received_bytes[9] >> 4) & 0x0F};

  // Find matching table entry for erasure pattern
  rs_decode_table_entry_avx512 *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry_avx512 *candidate = &table->entries[t];

    if (!candidate->valid || candidate->num_erasures != num_erasures) {
      continue;
    }

    int match = 0;
    if (num_erasures == 0) {
      match = 1;
    } else if (num_erasures == 1) {
      match = (candidate->erasure_pattern[0] == erasure_locations[0]);
    } else if (num_erasures == 2) {
      match = ((candidate->erasure_pattern[0] == erasure_locations[0] &&
                candidate->erasure_pattern[1] == erasure_locations[1]) ||
               (candidate->erasure_pattern[0] == erasure_locations[1] &&
                candidate->erasure_pattern[1] == erasure_locations[0]));
    }

    if (match) {
      entry = candidate;
      break;
    }
  }

  if (!entry) {
    return -1;
  }

  // ---- Decode lower nibble stream ----
  char lower_rx_modified[8];
  for (int i = 0; i < 8; i++) {
    lower_rx_modified[i] = lower_data[i];
  }
  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      lower_rx_modified[erasure_locations[i]] = lower_parity[i];
    }
  }

  char lower_decoded[8];
  for (int i = 0; i < 8; i++) {
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= avx512_gf_mul(entry->inv_matrix[i][j], lower_rx_modified[j]);
    }
    lower_decoded[i] = result & 0x0F;
  }

  // ---- Decode upper nibble stream ----
  char upper_rx_modified[8];
  for (int i = 0; i < 8; i++) {
    upper_rx_modified[i] = upper_data[i];
  }
  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      upper_rx_modified[erasure_locations[i]] = upper_parity[i];
    }
  }

  char upper_decoded[8];
  for (int i = 0; i < 8; i++) {
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= avx512_gf_mul(entry->inv_matrix[i][j], upper_rx_modified[j]);
    }
    upper_decoded[i] = result & 0x0F;
  }

  // ---- Combine decoded nibbles ----
  for (int i = 0; i < 8; i++) {
    decoded_bytes[i] = ((upper_decoded[i] & 0x0F) << 4) | (lower_decoded[i] & 0x0F);
  }

  return 0;
}

#endif
