#ifndef __decoder_test_common_h
#define __decoder_test_common_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../common/ejfat_rs.h"
#include "../common/ejfat_rs_decoder.h"

// Test result tracking
typedef struct {
    int total;
    int passed;
    int failed;
} test_results;

// Initialize test results
static inline void init_test_results(test_results *results) {
    results->total = 0;
    results->passed = 0;
    results->failed = 0;
}

// Record test result
static inline void record_test(test_results *results, int passed, const char *test_name) {
    results->total++;
    if (passed) {
        results->passed++;
        printf("  [PASS] %s\n", test_name);
    } else {
        results->failed++;
        printf("  [FAIL] %s\n", test_name);
    }
}

// Print test summary
static inline void print_test_summary(const char *suite_name, test_results *results) {
    printf("\n========== %s Summary ==========\n", suite_name);
    printf("Total:  %d\n", results->total);
    printf("Passed: %d\n", results->passed);
    printf("Failed: %d\n", results->failed);
    printf("Success rate: %.1f%%\n",
           results->total > 0 ? (100.0 * results->passed / results->total) : 0.0);
    printf("=====================================\n\n");
}

// Verify decoded message matches original
static inline int verify_decode(rs_poly_vector *decoded, rs_poly_vector *original) {
    if (decoded->len != original->len) return 0;
    for (int i = 0; i < decoded->len; i++) {
        if (decoded->val[i] != original->val[i]) return 0;
    }
    return 1;
}

// Create codeword from data and parity
static inline void create_codeword(rs_poly_vector *data, rs_poly_vector *parity,
                                   rs_poly_vector *codeword) {
    codeword->len = 10;
    for (int i = 0; i < 8; i++) {
        codeword->val[i] = data->val[i];
    }
    for (int i = 0; i < 2; i++) {
        codeword->val[8 + i] = parity->val[i];
    }
}

// Erase symbols at specified positions
static inline void erase_symbols(rs_poly_vector *codeword, int *positions, int num) {
    for (int i = 0; i < num; i++) {
        if (positions[i] < codeword->len) {
            codeword->val[positions[i]] = 0;
        }
    }
}

// Generate random RS symbol (0-15 for GF(16))
static inline char random_symbol() {
    return (char)(rand() % 16);
}

// Generate random test data
static inline void generate_random_data(rs_poly_vector *data) {
    data->len = 8;
    for (int i = 0; i < 8; i++) {
        data->val[i] = random_symbol();
    }
}

#endif
