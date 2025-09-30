#ifndef __ejfat_rs_avx2_decoder_h
#define __ejfat_rs_avx2_decoder_h

#include "ejfat_rs_common.h"

// --------------------------------------------------------------------------
// AVX2-Optimized Reed-Solomon Decoder (Cross-Platform Implementation)
// --------------------------------------------------------------------------

// Check for AVX2 support at compile time
#if defined(__AVX2__) && (defined(__x86_64__) || defined(_M_X64))

#include <immintrin.h>

// Table entry for pre-computed inverse matrices
typedef struct {
  int erasure_pattern[2];  // Up to 2 erasures for RS(10,8)
  int num_erasures;        // Number of erasures in this pattern
  char inv_matrix[8][8];   // Pre-computed 8x8 inverse matrix
  int valid;               // 1 if this entry is valid, 0 otherwise
} rs_decode_table_entry_avx2;

// Decoder table structure
typedef struct {
  rs_decode_table_entry_avx2 *entries;  // Dynamic array of table entries
  int size;                             // Number of entries in table
  int capacity;                         // Allocated capacity
} rs_decode_table_avx2;

// Vectorized GF multiplication for 8 elements at once using AVX2
static inline void avx2_gf_mul_vec(uint8_t *a, uint8_t *b, uint8_t *result) {
  // Load 8 bytes into 256-bit registers (with zero extension)
  __m128i a_128 = _mm_loadl_epi64((__m128i*)a);
  __m128i b_128 = _mm_loadl_epi64((__m128i*)b);
  __m256i a_vec = _mm256_cvtepu8_epi32(a_128);
  __m256i b_vec = _mm256_cvtepu8_epi32(b_128);
  
  // Handle zero case
  __m256i zero_vec = _mm256_setzero_si256();
  __m256i a_zero_mask = _mm256_cmpeq_epi32(a_vec, zero_vec);
  __m256i b_zero_mask = _mm256_cmpeq_epi32(b_vec, zero_vec);
  __m256i zero_mask = _mm256_or_si256(a_zero_mask, b_zero_mask);
  
  // Convert to exponent space (manual lookup)
  uint32_t a_array[8], b_array[8];
  _mm256_storeu_si256((__m256i*)a_array, a_vec);
  _mm256_storeu_si256((__m256i*)b_array, b_vec);
  
  uint32_t a_exp_array[8], b_exp_array[8];
  for (int i = 0; i < 8; i++) {
    a_exp_array[i] = _ejfat_rs_gf_exp_seq[a_array[i]];
    b_exp_array[i] = _ejfat_rs_gf_exp_seq[b_array[i]];
  }
  
  __m256i a_exp = _mm256_loadu_si256((__m256i*)a_exp_array);
  __m256i b_exp = _mm256_loadu_si256((__m256i*)b_exp_array);
  
  // Add exponents (mod 15)
  __m256i sum_exp = _mm256_add_epi32(a_exp, b_exp);
  __m256i mod_15 = _mm256_set1_epi32(15);
  __m256i ge_mask = _mm256_cmpgt_epi32(sum_exp, _mm256_sub_epi32(mod_15, _mm256_set1_epi32(1)));
  __m256i mod_sub = _mm256_and_si256(ge_mask, mod_15);
  sum_exp = _mm256_sub_epi32(sum_exp, mod_sub);
  
  // Convert back to normal space (manual lookup)
  uint32_t sum_exp_array[8];
  _mm256_storeu_si256((__m256i*)sum_exp_array, sum_exp);
  
  uint8_t result_array[8];
  for (int i = 0; i < 8; i++) {
    result_array[i] = _ejfat_rs_gf_log_seq[sum_exp_array[i]];
  }
  
  // Apply zero mask
  uint32_t zero_mask_array[8];
  _mm256_storeu_si256((__m256i*)zero_mask_array, zero_mask);
  
  for (int i = 0; i < 8; i++) {
    if (zero_mask_array[i] != 0) {
      result_array[i] = 0;
    }
  }
  
  // Store result
  memcpy(result, result_array, 8);
}

// AVX2-optimized decoder with full vectorization
int avx2_rs_decode_table_lookup_v2(rs_decode_table_avx2 *table, rs_poly_vector *received,
                                   int *erasure_locations, int num_erasures,
                                   rs_poly_vector *decoded) {
  
  if (num_erasures > 2) {
    return -1;
  }
  
  // Find matching table entry
  rs_decode_table_entry_avx2 *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry_avx2 *candidate = &table->entries[t];
    
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
  
  // Prepare received vector with substitutions
  uint8_t rx_modified[8] __attribute__((aligned(32)));
  for (int i = 0; i < 8; i++) {
    rx_modified[i] = received->val[i];
  }
  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      rx_modified[erasure_locations[i]] = received->val[8 + i];
    }
  }
  
  // Vectorized matrix-vector multiplication
  decoded->len = 8;
  for (int i = 0; i < 8; i++) {
    uint8_t matrix_row[8];
    memcpy(matrix_row, entry->inv_matrix[i], 8);
    
    uint8_t prod_vec[8];
    avx2_gf_mul_vec(matrix_row, rx_modified, prod_vec);
    
    // Horizontal XOR reduction
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= prod_vec[j];
    }
    
    decoded->val[i] = result;
  }
  
  return 0;
}

#else
// Fallback implementation when AVX2 is not available
#warning "AVX2 not supported on this platform, using scalar fallback"

typedef struct {
  int erasure_pattern[2];
  int num_erasures;
  char inv_matrix[8][8];
  int valid;
} rs_decode_table_entry_avx2;

typedef struct {
  rs_decode_table_entry_avx2 *entries;
  int size;
  int capacity;
} rs_decode_table_avx2;

// Helper functions for scalar fallback
static char gf_mul_scalar(char a, char b) {
    if (a == 0 || b == 0) return 0;
    char exp_a = _ejfat_rs_gf_exp_seq[a];
    char exp_b = _ejfat_rs_gf_exp_seq[b];
    char sum = (exp_a + exp_b) % 15;
    return _ejfat_rs_gf_log_seq[sum];
}

static char gf_sum_scalar(char a, char b) {
    return a ^ b;
}

// Scalar fallback implementation
int avx2_rs_decode_table_lookup_v2(rs_decode_table_avx2 *table, rs_poly_vector *received,
                                   int *erasure_locations, int num_erasures,
                                   rs_poly_vector *decoded) {
  
  if (num_erasures > 2) {
    return -1;
  }
  
  // Find matching table entry (same as AVX2 version)
  rs_decode_table_entry_avx2 *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry_avx2 *candidate = &table->entries[t];
    
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
  
  // Prepare received vector with substitutions
  uint8_t rx_modified[8];
  for (int i = 0; i < 8; i++) {
    rx_modified[i] = received->val[i];
  }
  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      rx_modified[erasure_locations[i]] = received->val[8 + i];
    }
  }
  
  // Scalar matrix-vector multiplication
  decoded->len = 8;
  for (int i = 0; i < 8; i++) {
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result = gf_sum_scalar(result, gf_mul_scalar(entry->inv_matrix[i][j], rx_modified[j]));
    }
    decoded->val[i] = result;
  }
  
  return 0;
}

#endif /* AVX2 support check */

#endif