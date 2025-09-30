#ifndef __ejfat_rs_neon_h
#define __ejfat_rs_neon_h

#include <arm_neon.h>
#include <stdint.h>

// --------------------------------------------------------------------------------------
// Minimal Reed-Solomon FEC library with NEON SIMD optimizations
// RS(10,8) configuration: 8 data symbols + 2 parity symbols over GF(16)
// --------------------------------------------------------------------------------------

// Include shared RS model with GF(16) lookup tables and encoding matrices
#include "../../prototype/python/rs_model.h"

// Pre-computed encoding matrix in exponent space (P matrix from [I|P])
// This is computed at initialization from _ejfat_rs_G columns 8-9
static char _ejfat_rs_Genc_exp[2][8];

// --------------------------------------------------------------------------------------
// Initialize the NEON encoder by pre-computing the exponent-space encoding matrix
// Must be called once before using the encoder functions
// --------------------------------------------------------------------------------------
static inline void init_ejfat_rs_neon() {
  for (int row = 0; row < _ejfat_rs_p; row++) {
    for (int col = 0; col < _ejfat_rs_n; col++) {
      _ejfat_rs_Genc_exp[row][col] = _ejfat_rs_gf_exp_seq[_ejfat_rs_G[col][_ejfat_rs_n + row]];
    }
  }
}

// --------------------------------------------------------------------------------------
// Single-nibble NEON RS encoder
// Encodes 8 nibble-sized (4-bit) data symbols into 2 parity symbols
// --------------------------------------------------------------------------------------
void neon_rs_encode(const char *data, char *parity) {

  // Load GF(16) lookup tables into NEON registers
  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;

  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);
  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);

  // Load 8 data symbols and convert to exponent space via table lookup
  uint8x8_t indices = vld1_u8((unsigned const char *) data);
  uint8x8_t d_vec = vtbl2_u8(exp_table, indices);

  uint8x8_t mod = vdup_n_u8(15);  // for modulo 15 operation

  // Compute each parity symbol
  for (int i = 0; i < _ejfat_rs_p; i++) {
    // Load pre-computed encoding coefficients
    uint8x8_t enc_vec = vld1_u8((uint8_t *) _ejfat_rs_Genc_exp[i]);

    // Add exponents: exp(d) + exp(coeff)
    uint8x8_t sum = vadd_u8(d_vec, enc_vec);

    // Compute modulo 15: if sum >= 15, subtract 15
    uint8x8_t mask = vcge_u8(sum, mod);
    uint8x8_t mod15 = vand_u8(mod, mask);
    uint8x8_t exp_sum = vsub_u8(sum, mod15);

    // Convert back to normal space via log table lookup
    uint8x8_t sum_vec = vtbl2_u8(log_table, exp_sum);

    // Horizontal XOR reduction to compute final parity symbol
    uint8_t sum_vec_array[8];
    vst1_u8(sum_vec_array, sum_vec);
    parity[i] = 0;
    for (int j = 0; j < 8; j++) {
      parity[i] ^= sum_vec_array[j];
    }
  }
}

// --------------------------------------------------------------------------------------
// Dual-nibble NEON RS encoder
// Processes 8 bytes as two independent RS(10,8) streams (upper and lower nibbles)
// Generates 2 parity bytes (4 parity nibbles combined)
// --------------------------------------------------------------------------------------
void neon_rs_encode_dual_nibble(const char *data_bytes, char *parity_bytes) {

  // Load 8 data bytes into NEON register
  uint8x8_t data_vec = vld1_u8((uint8_t *)data_bytes);

  // Extract upper and lower nibbles using SIMD operations
  uint8x8_t nibble_mask = vdup_n_u8(0x0F);
  uint8x8_t lower_nibbles = vand_u8(data_vec, nibble_mask);
  uint8x8_t upper_nibbles = vshr_n_u8(data_vec, 4);

  // Load GF(16) lookup tables (shared by both nibble streams)
  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;

  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);
  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);

  uint8x8_t mod = vdup_n_u8(15);
  uint8x8_t zero_vec = vdup_n_u8(0);

  // ---- Encode lower nibbles ----

  // Create zero mask: GF(16) multiplication property requires 0*x = 0
  uint8x8_t lower_zero_mask = vceq_u8(lower_nibbles, zero_vec);

  // Convert to exponent space
  uint8x8_t lower_exp = vtbl2_u8(exp_table, lower_nibbles);

  char lower_parity[2];
  for (int i = 0; i < _ejfat_rs_p; i++) {
    uint8x8_t enc_vec = vld1_u8((uint8_t *) _ejfat_rs_Genc_exp[i]);
    uint8x8_t sum = vadd_u8(lower_exp, enc_vec);

    // Modulo 15 operation
    uint8x8_t mask = vcge_u8(sum, mod);
    uint8x8_t mod15 = vand_u8(mod, mask);
    uint8x8_t exp_sum = vsub_u8(sum, mod15);

    // Convert back to normal space
    uint8x8_t sum_vec = vtbl2_u8(log_table, exp_sum);

    // Apply zero mask: preserve GF(16) zero property
    sum_vec = vbic_u8(sum_vec, lower_zero_mask);

    // Horizontal XOR reduction
    uint8_t sum_vec_array[8];
    vst1_u8(sum_vec_array, sum_vec);
    lower_parity[i] = 0;
    for (int j = 0; j < 8; j++) {
      lower_parity[i] ^= sum_vec_array[j];
    }
  }

  // ---- Encode upper nibbles ----

  uint8x8_t upper_zero_mask = vceq_u8(upper_nibbles, zero_vec);
  uint8x8_t upper_exp = vtbl2_u8(exp_table, upper_nibbles);

  char upper_parity[2];
  for (int i = 0; i < _ejfat_rs_p; i++) {
    uint8x8_t enc_vec = vld1_u8((uint8_t *) _ejfat_rs_Genc_exp[i]);
    uint8x8_t sum = vadd_u8(upper_exp, enc_vec);

    // Modulo 15 operation
    uint8x8_t mask = vcge_u8(sum, mod);
    uint8x8_t mod15 = vand_u8(mod, mask);
    uint8x8_t exp_sum = vsub_u8(sum, mod15);

    // Convert back to normal space
    uint8x8_t sum_vec = vtbl2_u8(log_table, exp_sum);

    // Apply zero mask
    sum_vec = vbic_u8(sum_vec, upper_zero_mask);

    // Horizontal XOR reduction
    uint8_t sum_vec_array[8];
    vst1_u8(sum_vec_array, sum_vec);
    upper_parity[i] = 0;
    for (int j = 0; j < 8; j++) {
      upper_parity[i] ^= sum_vec_array[j];
    }
  }

  // ---- Combine parity nibbles into bytes ----
  // Format: [upper_nibble | lower_nibble]
  parity_bytes[0] = ((upper_parity[0] & 0x0F) << 4) | (lower_parity[0] & 0x0F);
  parity_bytes[1] = ((upper_parity[1] & 0x0F) << 4) | (lower_parity[1] & 0x0F);
}

#endif
