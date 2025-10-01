#ifndef __ejfat_rs_avx2_h
#define __ejfat_rs_avx2_h

#include <immintrin.h>
#include <stdint.h>

// --------------------------------------------------------------------------------------
// Minimal Reed-Solomon FEC library with AVX2 SIMD optimizations
// RS(10,8) configuration: 8 data symbols + 2 parity symbols over GF(16)
// --------------------------------------------------------------------------------------

// Include shared RS model with GF(16) lookup tables and encoding matrices
#include "../../prototype/python/rs_model.h"

// Pre-computed encoding matrix in exponent space (P matrix from [I|P])
// This is computed at initialization from _ejfat_rs_G columns 8-9
static char _ejfat_rs_Genc_exp_avx2[2][8];

// --------------------------------------------------------------------------------------
// Initialize the AVX2 encoder by pre-computing the exponent-space encoding matrix
// Must be called once before using the encoder functions
// --------------------------------------------------------------------------------------
static inline void init_ejfat_rs_avx2() {
  for (int row = 0; row < _ejfat_rs_p; row++) {
    for (int col = 0; col < _ejfat_rs_n; col++) {
      _ejfat_rs_Genc_exp_avx2[row][col] = _ejfat_rs_gf_exp_seq[_ejfat_rs_G[col][_ejfat_rs_n + row]];
    }
  }
}

// --------------------------------------------------------------------------------------
// Single-nibble AVX2 RS encoder
// Encodes 8 nibble-sized (4-bit) data symbols into 2 parity symbols
// --------------------------------------------------------------------------------------
void avx2_rs_encode(const char *data, char *parity) {
  // Scalar fallback for now - can be optimized with AVX2 in future
  // Load encoding matrix from exponent space

  // Compute each parity symbol: p[i] = sum(data[j] * Genc[i][j]) in GF(16)
  for (int i = 0; i < _ejfat_rs_p; i++) {
    parity[i] = 0;
    for (int j = 0; j < 8; j++) {
      char d = data[j];
      if (d == 0) continue;  // GF(16) property: 0 * x = 0

      char d_exp = _ejfat_rs_gf_exp_seq[d];
      char coeff_exp = _ejfat_rs_Genc_exp_avx2[i][j];

      // Multiply in GF(16): add exponents mod 15
      char prod_exp = (d_exp + coeff_exp) % 15;
      char prod = _ejfat_rs_gf_log_seq[prod_exp];

      // Add in GF(16): XOR
      parity[i] ^= prod;
    }
  }
}

// --------------------------------------------------------------------------------------
// Dual-nibble AVX2 RS encoder
// Processes 8 bytes as two independent RS(10,8) streams (upper and lower nibbles)
// Generates 2 parity bytes (4 parity nibbles combined)
// --------------------------------------------------------------------------------------
void avx2_rs_encode_dual_nibble(const char *data_bytes, char *parity_bytes) {

  // Extract lower and upper nibbles from each byte
  char lower_nibbles[8];
  char upper_nibbles[8];

  for (int i = 0; i < 8; i++) {
    lower_nibbles[i] = data_bytes[i] & 0x0F;
    upper_nibbles[i] = (data_bytes[i] >> 4) & 0x0F;
  }

  // Encode lower nibbles
  char lower_parity[2];
  for (int i = 0; i < _ejfat_rs_p; i++) {
    lower_parity[i] = 0;
    for (int j = 0; j < 8; j++) {
      char d = lower_nibbles[j];
      if (d == 0) continue;

      char d_exp = _ejfat_rs_gf_exp_seq[d];
      char coeff_exp = _ejfat_rs_Genc_exp_avx2[i][j];
      char prod_exp = (d_exp + coeff_exp) % 15;
      char prod = _ejfat_rs_gf_log_seq[prod_exp];

      lower_parity[i] ^= prod;
    }
  }

  // Encode upper nibbles
  char upper_parity[2];
  for (int i = 0; i < _ejfat_rs_p; i++) {
    upper_parity[i] = 0;
    for (int j = 0; j < 8; j++) {
      char d = upper_nibbles[j];
      if (d == 0) continue;

      char d_exp = _ejfat_rs_gf_exp_seq[d];
      char coeff_exp = _ejfat_rs_Genc_exp_avx2[i][j];
      char prod_exp = (d_exp + coeff_exp) % 15;
      char prod = _ejfat_rs_gf_log_seq[prod_exp];

      upper_parity[i] ^= prod;
    }
  }

  // Combine parity nibbles into bytes: [upper_nibble | lower_nibble]
  parity_bytes[0] = ((upper_parity[0] & 0x0F) << 4) | (lower_parity[0] & 0x0F);
  parity_bytes[1] = ((upper_parity[1] & 0x0F) << 4) | (lower_parity[1] & 0x0F);
}

#endif
