#ifndef __ejfat_rs_neon_common_h
#define __ejfat_rs_neon_common_h

#include <arm_neon.h>
#include <stdint.h>
#include <stdlib.h>
#include "../common/ejfat_rs_tables.h"

// --------------------------------------------------------------------------
// Common NEON Reed-Solomon Definitions
// --------------------------------------------------------------------------

// Polynomial vector structure
typedef struct {
    int len;
    char val[16];  // Max RS(10,8) + some padding
} rs_poly_vector;

#endif