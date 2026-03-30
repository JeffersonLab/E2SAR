#ifndef __ejfat_rs_common_h
#define __ejfat_rs_common_h

#include <stdint.h>
#include <stdlib.h>

// --------------------------------------------------------------------------
// Common Reed-Solomon Definitions (Cross-Platform)
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

#endif