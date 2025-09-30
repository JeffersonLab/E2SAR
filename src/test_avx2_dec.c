#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "./ejfat_rs_avx2_decoder.h"

// Utility function to print polynomial vectors
void print_rs_poly_vector(rs_poly_vector *v) {
    printf("[ ");
    for (int i = 0; i < v->len; i++) {
        printf("%d ", v->val[i]);
    }
    printf("]\n");
}

// GF(16) arithmetic functions for verification
char gf_mul(char a, char b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    char exp_a = _ejfat_rs_gf_exp_seq[a];
    char exp_b = _ejfat_rs_gf_exp_seq[b];
    char sum = (exp_a + exp_b) % 15;
    return _ejfat_rs_gf_log_seq[sum];
}

char gf_sum(char a, char b) {
    return a ^ b;
}

char gf_div(char a, char b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    char exp_a = _ejfat_rs_gf_exp_seq[a];
    char exp_b = _ejfat_rs_gf_exp_seq[b];
    char diff = (exp_a - exp_b + 15) % 15;
    return _ejfat_rs_gf_log_seq[diff];
}

// Simple 8x8 matrix inversion over GF(16) using Gauss-Jordan elimination
int gf_matrix_invert(char matrix[8][8], char inv[8][8]) {
    // Create augmented matrix [M | I]
    char aug[8][16];
    
    // Initialize augmented matrix
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            aug[i][j] = matrix[i][j];           // Copy original matrix
            aug[i][j + 8] = (i == j) ? 1 : 0;  // Identity matrix
        }
    }
    
    // Gauss-Jordan elimination
    for (int i = 0; i < 8; i++) {
        // Find pivot
        int pivot_row = i;
        for (int k = i + 1; k < 8; k++) {
            if (aug[k][i] != 0) {
                pivot_row = k;
                break;
            }
        }
        
        // Check for singular matrix
        if (aug[pivot_row][i] == 0) {
            return -1;  // Matrix is singular
        }
        
        // Swap rows if needed
        if (pivot_row != i) {
            for (int j = 0; j < 16; j++) {
                char temp = aug[i][j];
                aug[i][j] = aug[pivot_row][j];
                aug[pivot_row][j] = temp;
            }
        }
        
        // Scale pivot row
        char pivot = aug[i][i];
        for (int j = 0; j < 16; j++) {
            aug[i][j] = gf_div(aug[i][j], pivot);
        }
        
        // Eliminate column
        for (int k = 0; k < 8; k++) {
            if (k != i && aug[k][i] != 0) {
                char factor = aug[k][i];
                for (int j = 0; j < 16; j++) {
                    aug[k][j] = gf_sum(aug[k][j], gf_mul(factor, aug[i][j]));
                }
            }
        }
    }
    
    // Extract inverse matrix from right half
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            inv[i][j] = aug[i][j + 8];
        }
    }
    
    return 0;
}

// Initialize comprehensive decoder table for all single erasure patterns
int init_avx2_decode_table(rs_decode_table_avx2 *table) {
    printf("Initializing AVX2 decode table...\n");
    
    // RS(10,8) parity matrix from generator matrix
    static const char Genc[2][8] = {
        {14, 6, 14, 9, 7, 1, 15, 6},  // First parity constraint
        {5, 9, 4, 13, 8, 1, 5, 8}     // Second parity constraint
    };
    
    table->capacity = 20;  // Capacity for all single erasure patterns
    table->entries = malloc(table->capacity * sizeof(rs_decode_table_entry_avx2));
    if (!table->entries) {
        printf("Error: Could not allocate memory for decode table\n");
        return -1;
    }
    
    table->size = 0;
    
    // Pattern 0: No erasures (identity matrix)
    rs_decode_table_entry_avx2 *entry = &table->entries[table->size];
    entry->num_erasures = 0;
    entry->erasure_pattern[0] = -1;
    entry->erasure_pattern[1] = -1;
    entry->valid = 1;
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            entry->inv_matrix[i][j] = (i == j) ? 1 : 0;
        }
    }
    table->size++;
    
    // Generate all single erasure patterns (positions 0-7)
    for (int pos = 0; pos < 8; pos++) {
        entry = &table->entries[table->size];
        entry->num_erasures = 1;
        entry->erasure_pattern[0] = pos;
        entry->erasure_pattern[1] = -1;
        entry->valid = 1;
        
        // Create modified G matrix for erasure at this position
        char g_mod[8][8];
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                if (i == pos) {
                    // Replace erased row with first parity constraint
                    g_mod[i][j] = Genc[0][j];
                } else {
                    // Use identity for non-erased rows
                    g_mod[i][j] = (i == j) ? 1 : 0;
                }
            }
        }
        
        if (gf_matrix_invert(g_mod, entry->inv_matrix) != 0) {
            entry->valid = 0;
            printf("Warning: Could not invert matrix for erasure at position %d\n", pos);
        }
        table->size++;
    }
    
    printf("AVX2 decode table initialized with %d patterns\n", table->size);
    return 0;
}

void free_avx2_decode_table(rs_decode_table_avx2 *table) {
    if (table->entries) {
        free(table->entries);
        table->entries = NULL;
    }
    table->size = 0;
    table->capacity = 0;
}

// Test AVX2 decoder functionality
void test_avx2_decoder() {
    printf("\n=============== Testing AVX2 RS Decoder ===============\n");
    
    // Check if we're using AVX2 or fallback
    #if defined(__AVX2__) && (defined(__x86_64__) || defined(_M_X64))
    printf("Using native AVX2 implementation\n");
    #else
    printf("Using scalar fallback implementation (AVX2 not available)\n");
    #endif
    
    // Initialize decoder table
    rs_decode_table_avx2 decode_table;
    if (init_avx2_decode_table(&decode_table) != 0) {
        printf("Failed to initialize decoder table\n");
        return;
    }
    
    // Use known test vectors from RS(10,8) encoding
    // Original message: [1, 2, 3, 4, 5, 6, 7, 8]
    // Computed parity: [1, 5] (from our encoder tests)
    rs_poly_vector original_msg = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector codeword = { .len = 10, .val = { 1, 2, 3, 4, 5, 6, 7, 8, 1, 5 }};
    
    printf("Original message: ");
    print_rs_poly_vector(&original_msg);
    printf("Full codeword: ");
    print_rs_poly_vector(&codeword);
    
    // Test 1: No erasures
    printf("\n--- Test 1: No erasures ---\n");
    rs_poly_vector decoded1 = { .len = 8 };
    int erasures1[] = {};
    
    if (avx2_rs_decode_table_lookup_v2(&decode_table, &codeword, erasures1, 0, &decoded1) == 0) {
        printf("Decoded (no erasures): ");
        print_rs_poly_vector(&decoded1);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded1.val[i] != original_msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("AVX2 decoding %s\n", correct ? "PASSED" : "FAILED");
    } else {
        printf("AVX2 decoding FAILED (function returned error)\n");
    }
    
    // Test 2: Single erasures at all positions (0-7)
    printf("\n--- Test 2: Single erasures at all positions ---\n");
    int total_tests = 0;
    int passed_tests = 0;
    
    for (int pos = 0; pos < 8; pos++) {
        printf("Testing single erasure at position %d: ", pos);
        
        rs_poly_vector corrupted = codeword;
        corrupted.val[pos] = 0;  // Erase symbol at this position
        
        rs_poly_vector decoded = { .len = 8 };
        int erasures[] = { pos };
        
        total_tests++;
        
        if (avx2_rs_decode_table_lookup_v2(&decode_table, &corrupted, erasures, 1, &decoded) == 0) {
            // Verify correctness
            int correct = 1;
            for (int i = 0; i < 8; i++) {
                if (decoded.val[i] != original_msg.val[i]) {
                    correct = 0;
                    break;
                }
            }
            
            if (correct) {
                printf("PASSED\n");
                passed_tests++;
            } else {
                printf("FAILED (incorrect result)\n");
                printf("  Expected: ");
                print_rs_poly_vector(&original_msg);
                printf("  Got:      ");
                print_rs_poly_vector(&decoded);
            }
        } else {
            printf("FAILED (function returned error)\n");
        }
    }
    
    printf("\nSingle erasure test summary: %d/%d tests passed\n", passed_tests, total_tests);
    
    // Test 3: Too many erasures (should fail)
    printf("\n--- Test 3: Too many erasures (3 erasures - should fail) ---\n");
    rs_poly_vector corrupted3 = codeword;
    corrupted3.val[0] = 0;
    corrupted3.val[3] = 0;
    corrupted3.val[6] = 0;
    
    rs_poly_vector decoded3 = { .len = 8 };
    int erasures3[] = { 0, 3, 6 };
    
    int result = avx2_rs_decode_table_lookup_v2(&decode_table, &corrupted3, erasures3, 3, &decoded3);
    printf("AVX2 decoding with 3 erasures: %s (expected to fail)\n", 
           result == 0 ? "UNEXPECTEDLY PASSED" : "FAILED as expected");
    
    printf("\n=== AVX2 Decoder Test Summary ===\n");
    printf("Total tests: %d\n", total_tests + 2);  // +2 for no erasures and too many erasures
    printf("Passed: %d\n", passed_tests + 1 + (result != 0 ? 1 : 0));  // +1 for no erasures, +1 if too many failed correctly
    printf("Success rate: %.1f%%\n", 100.0 * (passed_tests + 1 + (result != 0 ? 1 : 0)) / (total_tests + 2));
    
    // Clean up
    free_avx2_decode_table(&decode_table);
    printf("\n=============== AVX2 Decoder Tests Complete ===============\n");
}

// Performance test for AVX2 decoder
void test_avx2_decoder_performance() {
    printf("\n=============== AVX2 Decoder Performance Test ===============\n");
    
    rs_decode_table_avx2 decode_table;
    if (init_avx2_decode_table(&decode_table) != 0) {
        printf("Failed to initialize decoder table\n");
        return;
    }
    
    int test_iterations = 100000;
    rs_poly_vector original_msg = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector corrupted = { .len = 10, .val = { 0, 2, 3, 4, 5, 6, 7, 8, 1, 5 }};  // Erasure at position 0
    rs_poly_vector decoded = { .len = 8 };
    int erasures[] = { 0 };
    
    printf("Performance test with %d iterations:\n", test_iterations);
    
    // AVX2 decoder performance test
    clock_t start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        avx2_rs_decode_table_lookup_v2(&decode_table, &corrupted, erasures, 1, &decoded);
    }
    clock_t end_time = clock();
    double time_avx2 = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("AVX2 decoder: %f seconds (%.1f ops/sec)\n", 
           time_avx2, test_iterations / time_avx2);
    
    // Verify final result
    int correct = 1;
    for (int i = 0; i < 8; i++) {
        if (decoded.val[i] != original_msg.val[i]) {
            correct = 0;
            break;
        }
    }
    printf("Final decode result: %s\n", correct ? "CORRECT" : "INCORRECT");
    
    // Throughput calculation (assuming 8-byte packets)
    double throughput_avx2 = (test_iterations * 8.0) / time_avx2 / 1e6;  // MB/s
    printf("Data throughput: %.1f MB/s\n", throughput_avx2);
    
    free_avx2_decode_table(&decode_table);
    printf("=============== Performance Test Complete ===============\n");
}

// Test platform detection and capability reporting
void test_platform_capabilities() {
    printf("\n=============== Platform Capabilities Test ===============\n");
    
    // Check compile-time capabilities
    #if defined(__AVX2__)
    printf("Compile-time AVX2 support: YES\n");
    #else
    printf("Compile-time AVX2 support: NO\n");
    #endif
    
    #if defined(__x86_64__) || defined(_M_X64)
    printf("Target architecture: x86_64\n");
    #else
    printf("Target architecture: Other (fallback mode)\n");
    #endif
    
    // Test basic functionality
    rs_decode_table_avx2 decode_table;
    if (init_avx2_decode_table(&decode_table) == 0) {
        printf("AVX2 decoder initialization: SUCCESS\n");
        free_avx2_decode_table(&decode_table);
    } else {
        printf("AVX2 decoder initialization: FAILED\n");
    }
    
    printf("=============== Platform Capabilities Test Complete ===============\n");
}

int main() {
    printf("AVX2 Reed-Solomon Decoder Test Program\n");
    printf("======================================\n");
    
    // Test platform capabilities
    test_platform_capabilities();
    
    // Test decoder functionality
    test_avx2_decoder();
    
    // Test decoder performance
    test_avx2_decoder_performance();
    
    return 0;
}