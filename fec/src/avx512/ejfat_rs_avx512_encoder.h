#ifndef __ejfat_rs_avx512_encoder_h
#define __ejfat_rs_avx512_encoder_h

#include "../common/ejfat_rs_common.h"

// --------------------------------------------------------------------------
// AVX-512 Optimized Reed-Solomon Encoder (Standalone Implementation)
// --------------------------------------------------------------------------

// Check for AVX-512 support at compile time
#if defined(__AVX512F__) && defined(__AVX512BW__) && (defined(__x86_64__) || defined(_M_X64))

#include <immintrin.h>

// RS model structure (minimal for encoder)
typedef struct {
    int n;    // number of data symbols
    int p;    // number of parity symbols
    char **Genc_exp;  // Parity matrix in exponent space for AVX-512 encoder
} rs_model_avx512;

// Initialize RS model for AVX-512 encoding
rs_model_avx512 *init_avx512_rs_encoder() {
    rs_model_avx512 *rs = (rs_model_avx512 *) malloc(sizeof(rs_model_avx512));
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
void free_avx512_rs_encoder(rs_model_avx512 *rs) {
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

// AVX-512 optimized RS encoder
void avx512_rs_encode(rs_model_avx512 *rs, rs_poly_vector *d, rs_poly_vector *p) {
    
    // --- For speed reasons, we do not check assumptions. But the following must be true:
    //     d must be exactly 8 data words
    //     p must be exactly 2 parity words
    // ------------------------------------------------------------------------------------

    // Load GF lookup tables into memory for gather operations
    __m512i exp_table = _mm512_loadu_si512((__m512i*)_ejfat_rs_gf_exp_seq);
    __m512i log_table = _mm512_loadu_si512((__m512i*)_ejfat_rs_gf_log_seq);
    
    // Load data vector (8 bytes -> lower 8 elements of 512-bit register)
    __m128i data_128 = _mm_loadl_epi64((__m128i*)d->val);
    __m512i data_vec = _mm512_cvtepu8_epi32(_mm_unpacklo_epi8(data_128, _mm_setzero_si128()));
    
    // Create zero mask for data elements
    __mmask16 data_zero_mask = _mm512_cmpeq_epi32_mask(data_vec, _mm512_setzero_si512());
    
    // Convert data to exponent space using gather
    __m512i d_exp = _mm512_i32gather_epi32(data_vec, _ejfat_rs_gf_exp_seq, 1);
    
    // Constant for modulo 15 operation
    __m512i mod_15 = _mm512_set1_epi32(15);
    
    for (int i = 0; i < rs->p; i++) {
        // Load encoder vector (8 bytes -> convert to 32-bit integers)
        __m128i enc_128 = _mm_loadl_epi64((__m128i*)rs->Genc_exp[i]);
        __m512i enc_vec = _mm512_cvtepu8_epi32(_mm_unpacklo_epi8(enc_128, _mm_setzero_si128()));
        
        // Add exponents
        __m512i sum = _mm512_add_epi32(d_exp, enc_vec);
        
        // Handle modulo 15 operation
        __mmask16 ge_mask = _mm512_cmpge_epi32_mask(sum, mod_15);
        __m512i mod_sub = _mm512_mask_mov_epi32(_mm512_setzero_si512(), ge_mask, mod_15);
        __m512i exp_sum = _mm512_sub_epi32(sum, mod_sub);
        
        // Convert back to normal space using gather
        __m512i sum_vec = _mm512_i32gather_epi32(exp_sum, _ejfat_rs_gf_log_seq, 1);
        
        // Apply zero mask: if data element was zero, result should be zero
        sum_vec = _mm512_mask_mov_epi32(sum_vec, data_zero_mask, _mm512_setzero_si512());
        
        // Horizontal XOR reduction
        // Extract the lower 8 32-bit elements and convert back to bytes
        __m256i sum_256 = _mm512_extracti64x4_epi64(sum_vec, 0);
        __m128i sum_128_lo = _mm256_extracti128_si256(sum_256, 0);
        __m128i sum_128_hi = _mm256_extracti128_si256(sum_256, 1);
        
        // Pack 32-bit integers back to bytes
        __m128i sum_16 = _mm_packus_epi32(sum_128_lo, sum_128_hi);
        __m128i sum_8 = _mm_packus_epi16(sum_16, _mm_setzero_si128());
        
        // Extract bytes and perform XOR reduction
        uint8_t sum_array[8];
        _mm_storel_epi64((__m128i*)sum_array, sum_8);
        
        p->val[i] = 0;
        for (int j = 0; j < 8; j++) {
            p->val[i] ^= sum_array[j];
        }
    }
}

#else
// Fallback implementation when AVX-512 is not available
#warning "AVX-512 not supported on this platform, using scalar fallback"

typedef struct {
    int n;
    int p;
    char **Genc_exp;
} rs_model_avx512;

rs_model_avx512 *init_avx512_rs_encoder() {
    // Use same implementation as NEON version but with different name
    rs_model_avx512 *rs = (rs_model_avx512 *) malloc(sizeof(rs_model_avx512));
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

void free_avx512_rs_encoder(rs_model_avx512 *rs) {
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
void avx512_rs_encode(rs_model_avx512 *rs, rs_poly_vector *d, rs_poly_vector *p) {
    // Scalar implementation using exponent space for consistency with AVX-512 version
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

#endif /* AVX-512 support check */

#endif