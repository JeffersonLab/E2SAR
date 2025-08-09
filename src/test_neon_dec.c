#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "./ejfat_rs_neon_decoder.h"

// Simple utility function to print polynomial vectors
void print_rs_poly_vector(rs_poly_vector *v) {
    printf("[ ");
    for (int i = 0; i < v->len; i++) {
        printf("%d ", v->val[i]);
    }
    printf("]\n");
}

// GF(16) arithmetic functions for matrix operations
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

// Properly computed decoder table initialization using actual RS generator matrix
int init_test_decode_table(rs_decode_table *table) {
    printf("Initializing test decode table...\n");
    
    // RS(10,8) generator matrix from rs_model.h 
    static const char G[8][10] = {
        {1,0,0,0,0,0,0,0,14,5},
        {0,1,0,0,0,0,0,0,6,9},
        {0,0,1,0,0,0,0,0,14,4},
        {0,0,0,1,0,0,0,0,9,13},
        {0,0,0,0,1,0,0,0,7,8},
        {0,0,0,0,0,1,0,0,1,1},
        {0,0,0,0,0,0,1,0,15,5},
        {0,0,0,0,0,0,0,1,6,8} 
    };
    
    // Parity part of generator matrix (last 2 columns)
    static const char Genc[2][8] = {
        {14, 6, 14, 9, 7, 1, 15, 6},  // First parity constraint
        {5, 9, 4, 13, 8, 1, 5, 8}     // Second parity constraint
    };
    
    table->capacity = 20;  // Increased capacity for all single erasure patterns
    table->entries = malloc(table->capacity * sizeof(rs_decode_table_entry));
    if (!table->entries) {
        printf("Error: Could not allocate memory for decode table\n");
        return -1;
    }
    
    table->size = 0;
    
    // Pattern 0: No erasures (identity matrix)
    rs_decode_table_entry *entry = &table->entries[table->size];
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
        if (table->size >= table->capacity) {
            printf("Warning: Table capacity exceeded\n");
            break;
        }
        
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
    
    printf("Test decode table initialized with %d patterns\n", table->size);
    return 0;
}

void free_test_decode_table(rs_decode_table *table) {
    if (table->entries) {
        free(table->entries);
        table->entries = NULL;
    }
    table->size = 0;
    table->capacity = 0;
}

void test_neon_decoder() {
    printf("\n=============== Testing NEON RS Decoder ===============\n");
    
    // Initialize decoder table
    rs_decode_table decode_table;
    if (init_test_decode_table(&decode_table) != 0) {
        printf("Failed to initialize decoder table\n");
        return;
    }
    
    // Compute correct parity symbols for test message [1,2,3,4,5,6,7,8]
    rs_poly_vector original_msg = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    
    // Compute parity using generator matrix: p = data * Genc^T
    char parity0 = 0, parity1 = 0;
    static const char Genc[2][8] = {
        {14, 6, 14, 9, 7, 1, 15, 6},  // First parity constraint
        {5, 9, 4, 13, 8, 1, 5, 8}     // Second parity constraint
    };
    
    for (int i = 0; i < 8; i++) {
        parity0 = gf_sum(parity0, gf_mul(original_msg.val[i], Genc[0][i]));
        parity1 = gf_sum(parity1, gf_mul(original_msg.val[i], Genc[1][i]));
    }
    
    rs_poly_vector codeword = { .len = 10, .val = { 1, 2, 3, 4, 5, 6, 7, 8, parity0, parity1 }};
    
    printf("Original message: ");
    print_rs_poly_vector(&original_msg);
    printf("Full codeword: ");
    print_rs_poly_vector(&codeword);
    
    // Test 1: No erasures
    printf("\n--- Test 1: No erasures ---\n");
    rs_poly_vector decoded1 = { .len = 8 };
    int erasures1[] = {};
    
    if (neon_rs_decode_table_lookup_v2(&decode_table, &codeword, erasures1, 0, &decoded1) == 0) {
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
        printf("NEON decoding %s\n", correct ? "PASSED" : "FAILED");
    } else {
        printf("NEON decoding FAILED (function returned error)\n");
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
        
        if (neon_rs_decode_table_lookup_v2(&decode_table, &corrupted, erasures, 1, &decoded) == 0) {
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
    rs_poly_vector corrupted5 = codeword;
    corrupted5.val[0] = 0;
    corrupted5.val[3] = 0;
    corrupted5.val[6] = 0;
    
    rs_poly_vector decoded5 = { .len = 8 };
    int erasures5[] = { 0, 3, 6 };
    
    int result = neon_rs_decode_table_lookup_v2(&decode_table, &corrupted5, erasures5, 3, &decoded5);
    printf("NEON decoding with 3 erasures: %s (expected to fail)\n", 
           result == 0 ? "UNEXPECTEDLY PASSED" : "FAILED as expected");
    
    // Clean up
    free_test_decode_table(&decode_table);
    printf("\n=============== NEON Decoder Tests Complete ===============\n");
}

void test_neon_performance() {
    printf("\n=============== NEON Decoder Performance Test ===============\n");
    
    rs_decode_table decode_table;
    if (init_test_decode_table(&decode_table) != 0) {
        printf("Failed to initialize decoder table\n");
        return;
    }
    
    int test_iterations = 1000000;  // High iteration count for performance measurement
    
    // Create test vectors
    rs_poly_vector original_msg = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    
    // Compute correct parity
    char parity0 = 0, parity1 = 0;
    static const char Genc[2][8] = {
        {14, 6, 14, 9, 7, 1, 15, 6},
        {5, 9, 4, 13, 8, 1, 5, 8}
    };
    for (int i = 0; i < 8; i++) {
        parity0 = gf_sum(parity0, gf_mul(original_msg.val[i], Genc[0][i]));
        parity1 = gf_sum(parity1, gf_mul(original_msg.val[i], Genc[1][i]));
    }
    
    rs_poly_vector corrupted = { .len = 10, .val = { 0, 2, 3, 4, 5, 6, 7, 8, parity0, parity1 }};  // Erasure at position 0
    rs_poly_vector decoded = { .len = 8 };
    int erasures[] = { 0 };
    
    printf("Performance test with %d iterations:\n", test_iterations);
    
    // NEON decoder performance test
    clock_t start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        neon_rs_decode_table_lookup_v2(&decode_table, &corrupted, erasures, 1, &decoded);
    }
    clock_t end_time = clock();
    double time_neon = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("NEON decoder: %f seconds (%.1f ops/sec)\n", 
           time_neon, test_iterations / time_neon);
    
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
    double throughput_neon = (test_iterations * 8.0) / time_neon / 1e6;  // MB/s
    printf("Data throughput: %.1f MB/s\n", throughput_neon);
    
    free_test_decode_table(&decode_table);
    printf("=============== Performance Test Complete ===============\n");
}

int main() {
    printf("NEON Reed-Solomon Decoder Test Program\n");
    printf("======================================\n");
    
    // Test NEON decoder functionality
    test_neon_decoder();
    
    // Test NEON decoder performance
    test_neon_performance();
    
    return 0;
}