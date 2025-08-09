#ifndef __ejfat_rs_avx2_encoder_h
#define __ejfat_rs_avx2_encoder_h

#include "ejfat_rs_common.h"

// --------------------------------------------------------------------------
// AVX2-Optimized Reed-Solomon Encoder (Cross-Platform Implementation)
// --------------------------------------------------------------------------

// Check for AVX2 support at compile time
#if defined(__AVX2__) && (defined(__x86_64__) || defined(_M_X64))

#include <immintrin.h>

// RS model structure (minimal for encoder)
typedef struct {
    int n;    // number of data symbols
    int p;    // number of parity symbols
    char **Genc_exp;  // Parity matrix in exponent space for AVX2 encoder
} rs_model_avx2;

// Initialize RS model for AVX2 encoding
rs_model_avx2 *init_avx2_rs_encoder() {
    rs_model_avx2 *rs = (rs_model_avx2 *) malloc(sizeof(rs_model_avx2));
    if (!rs) {
        return NULL;
    }
    
    rs->n = _ejfat_rs_n;
    rs->p = _ejfat_rs_p;
    
    // Parity part of generator matrix (last 2 columns from G matrix)
    static const char Genc[2][8] = {
        {14, 6, 14, 9, 7, 1, 15, 6},  // First parity constraint
        {5, 9, 4, 13, 8, 1, 5, 8}     // Second parity constraint
    };
    
    // Allocate memory for Genc_exp (exponent space version)
    rs->Genc_exp = malloc(rs->p * sizeof(char *));
    if (rs->Genc_exp == NULL) {
        free(rs);
        return NULL;
    }
    
    // Allocate each row
    for (int i = 0; i < rs->p; i++) {
        rs->Genc_exp[i] = malloc(rs->n * sizeof(char));
        if (rs->Genc_exp[i] == NULL) {
            // Clean up on failure
            for (int j = 0; j < i; j++) {
                free(rs->Genc_exp[j]);
            }
            free(rs->Genc_exp);
            free(rs);
            return NULL;
        }
    }
    
    // Convert Genc to exponent space
    for (int row = 0; row < rs->p; row++) {
        for (int col = 0; col < rs->n; col++) {
            rs->Genc_exp[row][col] = _ejfat_rs_gf_exp_seq[Genc[row][col]];
        }
    }
    
    return rs;
}

// Free RS model
void free_avx2_rs_encoder(rs_model_avx2 *rs) {
    if (rs) {
        if (rs->Genc_exp) {
            for (int row = 0; row < rs->p; row++) {
                free(rs->Genc_exp[row]);
            }
            free(rs->Genc_exp);
        }
        free(rs);
    }
}

// AVX2-optimized RS encoder
void avx2_rs_encode(rs_model_avx2 *rs, rs_poly_vector *d, rs_poly_vector *p) {
    
    // --- For speed reasons, we do not check assumptions. But the following must be true:
    //     d must be exactly 8 data words
    //     p must be exactly 2 parity words
    // ------------------------------------------------------------------------------------

    // Load GF lookup tables into 256-bit registers (16 elements each)
    __m256i exp_table = _mm256_loadu_si256((__m256i*)_ejfat_rs_gf_exp_seq);
    __m256i log_table = _mm256_loadu_si256((__m256i*)_ejfat_rs_gf_log_seq);
    
    // Load data vector (8 bytes -> lower 8 elements of 256-bit register)
    __m128i data_128 = _mm_loadl_epi64((__m128i*)d->val);
    __m256i data_vec = _mm256_cvtepu8_epi32(data_128);
    
    // Create zero mask for data elements
    __m256i zero_mask = _mm256_cmpeq_epi32(data_vec, _mm256_setzero_si256());
    
    // Convert data to exponent space using permute (AVX2 doesn't have gather on older CPUs)
    // We'll use a different approach - extract and lookup individually then pack back
    uint8_t data_array[8];
    _mm_storel_epi64((__m128i*)data_array, data_128);
    
    uint8_t d_exp_array[8];
    for (int j = 0; j < 8; j++) {
        d_exp_array[j] = _ejfat_rs_gf_exp_seq[data_array[j]];
    }
    
    __m128i d_exp_128 = _mm_loadl_epi64((__m128i*)d_exp_array);
    __m256i d_exp = _mm256_cvtepu8_epi32(d_exp_128);
    
    // Constant for modulo 15 operation
    __m256i mod_15 = _mm256_set1_epi32(15);
    
    for (int i = 0; i < rs->p; i++) {
        // Load encoder vector (8 bytes)
        __m128i enc_128 = _mm_loadl_epi64((__m128i*)rs->Genc_exp[i]);
        __m256i enc_vec = _mm256_cvtepu8_epi32(enc_128);
        
        // Add exponents
        __m256i sum = _mm256_add_epi32(d_exp, enc_vec);
        
        // Handle modulo 15 operation
        __m256i ge_mask = _mm256_cmpgt_epi32(sum, _mm256_sub_epi32(mod_15, _mm256_set1_epi32(1)));
        __m256i mod_sub = _mm256_and_si256(ge_mask, mod_15);
        __m256i exp_sum = _mm256_sub_epi32(sum, mod_sub);
        
        // Convert back to normal space (extract and lookup individually)
        uint32_t exp_sum_array[8];
        _mm256_storeu_si256((__m256i*)exp_sum_array, exp_sum);
        
        uint8_t sum_array[8];
        for (int j = 0; j < 8; j++) {
            sum_array[j] = _ejfat_rs_gf_log_seq[exp_sum_array[j]];
        }
        
        // Apply zero mask: if data element was zero, result should be zero
        for (int j = 0; j < 8; j++) {
            if (data_array[j] == 0) {
                sum_array[j] = 0;
            }
        }
        
        // Horizontal XOR reduction
        p->val[i] = 0;
        for (int j = 0; j < 8; j++) {
            p->val[i] ^= sum_array[j];
        }
    }
}

// AVX2-optimized RS encoder with vectorized table lookups and reductions
void avx2_rs_encode_optimized(rs_model_avx2 *rs, rs_poly_vector *d, rs_poly_vector *p) {
    
    // --- For speed reasons, we do not check assumptions. But the following must be true:
    //     d must be exactly 8 data words
    //     p must be exactly 2 parity words
    // ------------------------------------------------------------------------------------

    // Load data vector (8 bytes -> lower 8 elements of 256-bit register)
    __m128i data_128 = _mm_loadl_epi64((__m128i*)d->val);
    __m256i data_vec = _mm256_cvtepu8_epi32(data_128);
    
    // Create zero mask for data elements (will be used later for masking)
    __m256i zero_mask = _mm256_cmpeq_epi32(data_vec, _mm256_setzero_si256());
    
    // Vectorized table lookup: Convert data to exponent space using gather
    __m256i d_exp = _mm256_i32gather_epi32((const int*)_ejfat_rs_gf_exp_seq, data_vec, sizeof(char));
    
    for (int i = 0; i < rs->p; i++) {
        // Load encoder vector (8 bytes)
        __m128i enc_128 = _mm_loadl_epi64((__m128i*)rs->Genc_exp[i]);
        __m256i enc_vec = _mm256_cvtepu8_epi32(enc_128);
        
        // Add exponents (vectorized)
        __m256i sum = _mm256_add_epi32(d_exp, enc_vec);
        
        // Optimized modulo 15 operation: if sum > 14, subtract 15
        __m256i gt_14_mask = _mm256_cmpgt_epi32(sum, _mm256_set1_epi32(14));
        __m256i mod_correction = _mm256_and_si256(gt_14_mask, _mm256_set1_epi32(15));
        __m256i exp_sum = _mm256_sub_epi32(sum, mod_correction);
        
        // Vectorized table lookup: Convert back to normal space using gather
        __m256i result_vec = _mm256_i32gather_epi32((const int*)_ejfat_rs_gf_log_seq, exp_sum, sizeof(char));
        
        // Apply zero mask: if original data element was zero, result should be zero
        // Use andnot to clear bits where zero_mask is true (inverted logic)
        result_vec = _mm256_andnot_si256(zero_mask, result_vec);
        
        // Vectorized horizontal XOR reduction
        // Step 1: XOR upper 128 bits with lower 128 bits
        __m128i low_128 = _mm256_castsi256_si128(result_vec);
        __m128i high_128 = _mm256_extracti128_si256(result_vec, 1);
        __m128i xor_128 = _mm_xor_si128(low_128, high_128);
        
        // Step 2: XOR 64-bit halves within the 128-bit result
        __m128i xor_64 = _mm_xor_si128(xor_128, _mm_srli_si128(xor_128, 8));
        
        // Step 3: XOR 32-bit halves within the 64-bit result
        __m128i xor_32 = _mm_xor_si128(xor_64, _mm_srli_si128(xor_64, 4));
        
        // Extract final result
        p->val[i] = (char)_mm_cvtsi128_si32(xor_32);
    }
}

#else
// Fallback implementation when AVX2 is not available
#warning "AVX2 not supported on this platform, using scalar fallback"

typedef struct {
    int n;
    int p;
    char **Genc_exp;
} rs_model_avx2;

rs_model_avx2 *init_avx2_rs_encoder() {
    rs_model_avx2 *rs = (rs_model_avx2 *) malloc(sizeof(rs_model_avx2));
    if (!rs) {
        return NULL;
    }
    
    rs->n = _ejfat_rs_n;
    rs->p = _ejfat_rs_p;
    
    static const char Genc[2][8] = {
        {14, 6, 14, 9, 7, 1, 15, 6},
        {5, 9, 4, 13, 8, 1, 5, 8}
    };
    
    rs->Genc_exp = malloc(rs->p * sizeof(char *));
    if (rs->Genc_exp == NULL) {
        free(rs);
        return NULL;
    }
    
    for (int i = 0; i < rs->p; i++) {
        rs->Genc_exp[i] = malloc(rs->n * sizeof(char));
        if (rs->Genc_exp[i] == NULL) {
            for (int j = 0; j < i; j++) {
                free(rs->Genc_exp[j]);
            }
            free(rs->Genc_exp);
            free(rs);
            return NULL;
        }
    }
    
    for (int row = 0; row < rs->p; row++) {
        for (int col = 0; col < rs->n; col++) {
            rs->Genc_exp[row][col] = _ejfat_rs_gf_exp_seq[Genc[row][col]];
        }
    }
    
    return rs;
}

void free_avx2_rs_encoder(rs_model_avx2 *rs) {
    if (rs) {
        if (rs->Genc_exp) {
            for (int row = 0; row < rs->p; row++) {
                free(rs->Genc_exp[row]);
            }
            free(rs->Genc_exp);
        }
        free(rs);
    }
}

// Scalar fallback implementation
void avx2_rs_encode(rs_model_avx2 *rs, rs_poly_vector *d, rs_poly_vector *p) {
    // Scalar implementation using exponent space for consistency with AVX2 version
    for (int row = 0; row < rs->p; row++) {
        p->val[row] = 0;
        for (int col = 0; col < rs->n; col++) {
            if (d->val[col] != 0) {  // Handle zero case properly
                char exp_d = _ejfat_rs_gf_exp_seq[d->val[col]];
                char exp_sum = (exp_d + rs->Genc_exp[row][col]) % 15;
                p->val[row] ^= _ejfat_rs_gf_log_seq[exp_sum];
            }
        }
    }
}

// Fallback optimized version (same as regular version for non-AVX2 platforms)
void avx2_rs_encode_optimized(rs_model_avx2 *rs, rs_poly_vector *d, rs_poly_vector *p) {
    // Use the same scalar implementation
    avx2_rs_encode(rs, d, p);
}

#endif /* AVX2 support check */

#endif