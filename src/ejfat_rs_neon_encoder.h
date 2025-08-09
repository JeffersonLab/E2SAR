#ifndef __ejfat_rs_neon_encoder_h
#define __ejfat_rs_neon_encoder_h

#include <arm_neon.h>
#include <stdint.h>
#include <stdlib.h>

// --------------------------------------------------------------------------
// NEON-Optimized Reed-Solomon Encoder (Standalone Implementation)
// --------------------------------------------------------------------------

// GF(16) lookup tables (from rs_model.h)
static const char _ejfat_rs_gf_log_seq[16] = { 1,2,4,8,3,6,12,11,5,10,7,14,15,13,9,0 }; 
static const char _ejfat_rs_gf_exp_seq[16] = { 15,0,1,4,2,8,5,10,3,14,9,7,6,13,11,12 };

// RS(10,8) constants
static const int _ejfat_rs_n = 8; // data words 
static const int _ejfat_rs_p = 2; // parity words  

// Polynomial vector structure
typedef struct {
    int len;
    char val[16];  // Max RS(10,8) + some padding
} rs_poly_vector;

// RS model structure (minimal for encoder)
typedef struct {
    int n;    // number of data symbols
    int p;    // number of parity symbols
    char **Genc_exp;  // Parity matrix in exponent space for NEON encoder
} rs_model;

// Initialize RS model for NEON encoding
rs_model *init_neon_rs_encoder() {
    rs_model *rs = (rs_model *) malloc(sizeof(rs_model));
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
void free_neon_rs_encoder(rs_model *rs) {
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

// NEON-optimized RS encoder
void neon_rs_encode(rs_model *rs, rs_poly_vector *d, rs_poly_vector *p) {
    
    // --- For speed reasons, we do not check assumptions. But the following must be true:
    //     d must be exactly 8 data words
    //     p must be exactly 2 parity words
    // ------------------------------------------------------------------------------------

    uint8x8x2_t exp_table;
    uint8x8x2_t log_table;  
    
    exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);   // t[0] to t[7]
    exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);   // t[8] to t[15]

    log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);   // t[0] to t[7]
    log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);   // t[8] to t[15]
    
    // Load data vector
    uint8x8_t data_vec = vld1_u8((unsigned const char *) d->val);
    
    // Create zero mask for data elements
    uint8x8_t zero_vec = vdup_n_u8(0);
    uint8x8_t data_zero_mask = vceq_u8(data_vec, zero_vec);

    // Convert data to exponent space
    uint8x8_t d_exp = vtbl2_u8(exp_table, data_vec);

    uint8x8_t mod = vdup_n_u8(15);  // used by mod instruction
    
    for (int i = 0; i < rs->p; i++) {
        uint8x8_t enc_vec = vld1_u8((uint8_t *) rs->Genc_exp[i]);
        uint8x8_t sum = vadd_u8(d_exp, enc_vec);

        // Handle modulo 15
        uint8x8_t mask = vcge_u8(sum, mod);  // returns 0xFF where sum >= 15
        uint8x8_t mod15 = vand_u8(mod, mask); // get 15 where needed, 0 elsewhere
        uint8x8_t exp_sum = vsub_u8(sum, mod15);

        // Convert back to normal space
        uint8x8_t sum_vec = vtbl2_u8(log_table, exp_sum);
        
        // Apply zero mask: if data element was zero, result should be zero
        sum_vec = vbic_u8(sum_vec, data_zero_mask);

        // Horizontal XOR reduction
        uint8_t sum_vec_array[8];
        vst1_u8(sum_vec_array, sum_vec);
        p->val[i] = 0;
        for (int j = 0; j < 8; j++) {
            p->val[i] ^= sum_vec_array[j];
        }
    }
}

#endif