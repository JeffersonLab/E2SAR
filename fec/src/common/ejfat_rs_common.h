#ifndef __ejfat_rs_common_h
#define __ejfat_rs_common_h

#include <stdint.h>
#include <stdlib.h>
#include "ejfat_rs_tables.h"

// --------------------------------------------------------------------------
// Common Reed-Solomon Definitions (Cross-Platform)
// --------------------------------------------------------------------------

// Polynomial vector structure
typedef struct {
    int len;
    char val[16];  // Max RS(10,8) + some padding
} rs_poly_vector;

#endif