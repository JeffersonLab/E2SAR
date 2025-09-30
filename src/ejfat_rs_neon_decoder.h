#ifndef __ejfat_rs_neon_decoder_h
#define __ejfat_rs_neon_decoder_h

#include <arm_neon.h>
#include <stdint.h>
#include <stdlib.h>

// --------------------------------------------------------------------------------------
// Minimal Reed-Solomon Decoder with NEON SIMD optimizations
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
} rs_decode_table_entry;

// Decoder table structure
typedef struct {
  rs_decode_table_entry *entries;  // Dynamic array of table entries
  int size;                        // Number of entries in table
  int capacity;                    // Allocated capacity
} rs_decode_table;

// --------------------------------------------------------------------------------------
// Helper: Vectorized GF multiplication for 8 elements at once
// --------------------------------------------------------------------------------------
static inline uint8x8_t neon_gf_mul_vec(uint8x8_t a, uint8x8_t b,
                                        uint8x8x2_t exp_table, uint8x8x2_t log_table) {
  // Handle zero case
  uint8x8_t zero_vec = vdup_n_u8(0);
  uint8x8_t a_zero_mask = vceq_u8(a, zero_vec);
  uint8x8_t b_zero_mask = vceq_u8(b, zero_vec);
  uint8x8_t zero_mask = vorr_u8(a_zero_mask, b_zero_mask);

  // Convert to exponent space
  uint8x8_t a_exp = vtbl2_u8(exp_table, a);
  uint8x8_t b_exp = vtbl2_u8(exp_table, b);

  // Add exponents (mod 15)
  uint8x8_t sum_exp = vadd_u8(a_exp, b_exp);
  uint8x8_t mod = vdup_n_u8(15);
  uint8x8_t mask = vcge_u8(sum_exp, mod);
  uint8x8_t mod15 = vand_u8(mod, mask);
  sum_exp = vsub_u8(sum_exp, mod15);

  // Convert back to normal space
  uint8x8_t result = vtbl2_u8(log_table, sum_exp);

  // Apply zero mask
  result = vbic_u8(result, zero_mask);

  return result;
}

// --------------------------------------------------------------------------------------
// Single-nibble NEON RS decoder
// Decodes received symbols with erasures using pre-computed inverse matrices
// Parameters:
//   - table: Pre-computed decoder table with inverse matrices
//   - received: 10 symbols (8 data + 2 parity)
//   - erasure_locations: Array of erased symbol indices (0-7)
//   - num_erasures: Number of erasures (0-2)
//   - decoded: Output buffer for 8 decoded data symbols
// Returns: 0 on success, -1 on error
// --------------------------------------------------------------------------------------
int neon_rs_decode(rs_decode_table *table, const char *received,
                   int *erasure_locations, int num_erasures,
                   char *decoded) {

  if (num_erasures > 2) {
    return -1;
  }

  // Load GF lookup tables
  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;
  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);
  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);

  // Find matching table entry
  rs_decode_table_entry *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry *candidate = &table->entries[t];

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
  uint8_t rx_modified[8] __attribute__((aligned(16)));
  for (int i = 0; i < 8; i++) {
    rx_modified[i] = received[i];
  }
  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      rx_modified[erasure_locations[i]] = received[8 + i];
    }
  }

  uint8x8_t rx_vec = vld1_u8(rx_modified);

  // Vectorized matrix-vector multiplication
  for (int i = 0; i < 8; i++) {
    uint8x8_t matrix_row = vld1_u8((uint8_t *)entry->inv_matrix[i]);
    uint8x8_t prod_vec = neon_gf_mul_vec(matrix_row, rx_vec, exp_table, log_table);

    // Horizontal XOR reduction
    uint8_t temp[8];
    vst1_u8(temp, prod_vec);
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= temp[j];
    }

    decoded[i] = result;
  }

  return 0;
}

// --------------------------------------------------------------------------------------
// Dual-nibble NEON RS decoder
// Decodes received bytes with erasures for both upper and lower nibble streams
// Parameters:
//   - table: Pre-computed decoder table with inverse matrices
//   - received_bytes: 10 bytes (8 data + 2 parity)
//   - erasure_locations: Array of erased byte indices (0-7)
//   - num_erasures: Number of erasures (0-2)
//   - decoded_bytes: Output buffer for 8 decoded data bytes
// Returns: 0 on success, -1 on error
// --------------------------------------------------------------------------------------
int neon_rs_decode_dual_nibble(rs_decode_table *table, const char *received_bytes,
                                int *erasure_locations, int num_erasures,
                                char *decoded_bytes) {

  if (num_erasures > 2) {
    return -1;
  }

  // Load 10 received bytes (8 data + 2 parity) into NEON registers
  uint8x8_t data_vec = vld1_u8((uint8_t *)received_bytes);

  // SIMD nibble extraction from data bytes
  uint8x8_t nibble_mask = vdup_n_u8(0x0F);
  uint8x8_t lower_data = vand_u8(data_vec, nibble_mask);
  uint8x8_t upper_data = vshr_n_u8(data_vec, 4);

  // Extract parity nibbles
  uint8_t lower_parity[2] = {received_bytes[8] & 0x0F, received_bytes[9] & 0x0F};
  uint8_t upper_parity[2] = {(received_bytes[8] >> 4) & 0x0F, (received_bytes[9] >> 4) & 0x0F};

  // Load GF lookup tables
  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;
  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);
  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);

  // Find matching table entry for erasure pattern
  rs_decode_table_entry *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry *candidate = &table->entries[t];

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
  uint8_t lower_rx_modified[8] __attribute__((aligned(16)));
  vst1_u8(lower_rx_modified, lower_data);

  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      lower_rx_modified[erasure_locations[i]] = lower_parity[i];
    }
  }

  uint8x8_t lower_rx_vec = vld1_u8(lower_rx_modified);

  uint8_t lower_decoded[8];
  for (int i = 0; i < 8; i++) {
    uint8x8_t matrix_row = vld1_u8((uint8_t *)entry->inv_matrix[i]);
    uint8x8_t prod_vec = neon_gf_mul_vec(matrix_row, lower_rx_vec, exp_table, log_table);

    uint8_t temp[8];
    vst1_u8(temp, prod_vec);
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= temp[j];
    }

    lower_decoded[i] = result & 0x0F;
  }

  // ---- Decode upper nibble stream ----
  uint8_t upper_rx_modified[8] __attribute__((aligned(16)));
  vst1_u8(upper_rx_modified, upper_data);

  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      upper_rx_modified[erasure_locations[i]] = upper_parity[i];
    }
  }

  uint8x8_t upper_rx_vec = vld1_u8(upper_rx_modified);

  uint8_t upper_decoded[8];
  for (int i = 0; i < 8; i++) {
    uint8x8_t matrix_row = vld1_u8((uint8_t *)entry->inv_matrix[i]);
    uint8x8_t prod_vec = neon_gf_mul_vec(matrix_row, upper_rx_vec, exp_table, log_table);

    uint8_t temp[8];
    vst1_u8(temp, prod_vec);
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= temp[j];
    }

    upper_decoded[i] = result & 0x0F;
  }

  // ---- SIMD combine decoded nibbles ----
  uint8x8_t lower_vec = vld1_u8(lower_decoded);
  uint8x8_t upper_vec = vld1_u8(upper_decoded);
  uint8x8_t upper_shifted = vshl_n_u8(upper_vec, 4);
  uint8x8_t combined = vorr_u8(upper_shifted, lower_vec);

  vst1_u8((uint8_t *)decoded_bytes, combined);

  return 0;
}

#endif
