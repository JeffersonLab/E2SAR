#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../avx2/ejfat_rs_avx2_encoder.h"
#include "../avx2/ejfat_rs_avx2_decoder.h"

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
int init_comprehensive_avx2_decode_table(rs_decode_table_avx2 *table) {
    printf("Initializing comprehensive AVX2 decode table...\n");
    
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
    
    printf("Comprehensive AVX2 decode table initialized with %d patterns\n", table->size);
    return 0;
}

void free_comprehensive_avx2_decode_table(rs_decode_table_avx2 *table) {
    if (table->entries) {
        free(table->entries);
        table->entries = NULL;
    }
    table->size = 0;
    table->capacity = 0;
}

// Test complete encode/decode cycle with various data patterns
void test_avx2_encode_decode_cycle() {
    printf("\n=============== Testing Complete AVX2 Encode/Decode Cycle ===============\n");
    
    // Check if we're using AVX2 or fallback
    #if defined(__AVX2__) && (defined(__x86_64__) || defined(_M_X64))
    printf("Using native AVX2 implementations\n");
    #else
    printf("Using scalar fallback implementations (AVX2 not available)\n");
    #endif
    
    // Initialize encoder and decoder
    rs_model_avx2 *encoder = init_avx2_rs_encoder();
    if (!encoder) {
        printf("Failed to initialize AVX2 encoder\n");
        return;
    }
    
    rs_decode_table_avx2 decode_table;
    if (init_comprehensive_avx2_decode_table(&decode_table) != 0) {
        printf("Failed to initialize decode table\n");
        free_avx2_rs_encoder(encoder);
        return;
    }
    
    // Test data patterns
    struct {
        const char *name;
        char data[8];
    } test_patterns[] = {
        {"All zeros", {0, 0, 0, 0, 0, 0, 0, 0}},
        {"All ones", {1, 1, 1, 1, 1, 1, 1, 1}},
        {"Sequential", {1, 2, 3, 4, 5, 6, 7, 8}},
        {"Alternating", {1, 0, 1, 0, 1, 0, 1, 0}},
        {"Powers of 2", {1, 2, 4, 8, 3, 6, 12, 11}},
        {"Max values", {15, 15, 15, 15, 15, 15, 15, 15}},
        {"Random 1", {7, 13, 2, 11, 5, 9, 14, 3}},
        {"Random 2", {12, 6, 10, 4, 1, 15, 8, 13}}
    };
    
    int num_patterns = sizeof(test_patterns) / sizeof(test_patterns[0]);
    int total_tests = 0;
    int passed_tests = 0;
    
    for (int p = 0; p < num_patterns; p++) {
        printf("\n--- Pattern %d: %s ---\n", p + 1, test_patterns[p].name);
        
        // Setup original data
        rs_poly_vector original_data = { .len = 8 };
        rs_poly_vector parity = { .len = 2 };
        rs_poly_vector codeword = { .len = 10 };
        
        memcpy(original_data.val, test_patterns[p].data, 8);
        
        printf("Original data: ");
        print_rs_poly_vector(&original_data);
        
        // Encode
        avx2_rs_encode(encoder, &original_data, &parity);
        printf("Parity: ");
        print_rs_poly_vector(&parity);
        
        // Create codeword
        for (int i = 0; i < 8; i++) {
            codeword.val[i] = original_data.val[i];
        }
        for (int i = 0; i < 2; i++) {
            codeword.val[8 + i] = parity.val[i];
        }
        
        printf("Codeword: ");
        print_rs_poly_vector(&codeword);
        
        // Test decoding with no errors
        printf("Testing no errors: ");
        rs_poly_vector decoded = { .len = 8 };
        int erasures[] = {};
        
        total_tests++;
        if (avx2_rs_decode_table_lookup_v2(&decode_table, &codeword, erasures, 0, &decoded) == 0) {
            int correct = 1;
            for (int i = 0; i < 8; i++) {
                if (decoded.val[i] != original_data.val[i]) {
                    correct = 0;
                    break;
                }
            }
            if (correct) {
                printf("PASSED\n");
                passed_tests++;
            } else {
                printf("FAILED (incorrect decode)\n");
            }
        } else {
            printf("FAILED (decode error)\n");
        }
        
        // Test decoding with single errors at each position
        for (int err_pos = 0; err_pos < 8; err_pos++) {
            printf("Testing error at position %d: ", err_pos);
            
            // Create corrupted codeword
            rs_poly_vector corrupted = codeword;
            corrupted.val[err_pos] = 0;  // Erase symbol
            
            rs_poly_vector decoded_err = { .len = 8 };
            int erasures_err[] = { err_pos };
            
            total_tests++;
            if (avx2_rs_decode_table_lookup_v2(&decode_table, &corrupted, erasures_err, 1, &decoded_err) == 0) {
                int correct = 1;
                for (int i = 0; i < 8; i++) {
                    if (decoded_err.val[i] != original_data.val[i]) {
                        correct = 0;
                        break;
                    }
                }
                if (correct) {
                    printf("PASSED\n");
                    passed_tests++;
                } else {
                    printf("FAILED (incorrect decode)\n");
                    printf("  Expected: ");
                    print_rs_poly_vector(&original_data);
                    printf("  Got:      ");
                    print_rs_poly_vector(&decoded_err);
                }
            } else {
                printf("FAILED (decode error)\n");
            }
        }
    }
    
    printf("\n=== Complete AVX2 Encode/Decode Test Summary ===\n");
    printf("Total tests: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", total_tests - passed_tests);
    printf("Success rate: %.1f%%\n", 100.0 * passed_tests / total_tests);
    
    // Clean up
    free_comprehensive_avx2_decode_table(&decode_table);
    free_avx2_rs_encoder(encoder);
    printf("\n=============== AVX2 Encode/Decode Cycle Tests Complete ===============\n");
}

// Comprehensive error correction test with systematic error injection
void test_avx2_systematic_error_correction() {
    printf("\n=============== AVX2 Systematic Error Correction Test ===============\n");
    
    // Initialize encoder and decoder
    rs_model_avx2 *encoder = init_avx2_rs_encoder();
    if (!encoder) {
        printf("Failed to initialize AVX2 encoder\n");
        return;
    }
    
    rs_decode_table_avx2 decode_table;
    if (init_comprehensive_avx2_decode_table(&decode_table) != 0) {
        printf("Failed to initialize decode table\n");
        free_avx2_rs_encoder(encoder);
        return;
    }
    
    // Use a fixed test message for systematic testing
    rs_poly_vector test_data = { .len = 8, .val = {1, 2, 3, 4, 5, 6, 7, 8} };
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    
    // Encode test message
    avx2_rs_encode(encoder, &test_data, &parity);
    
    // Create codeword
    for (int i = 0; i < 8; i++) {
        codeword.val[i] = test_data.val[i];
    }
    for (int i = 0; i < 2; i++) {
        codeword.val[8 + i] = parity.val[i];
    }
    
    printf("Test message: ");
    print_rs_poly_vector(&test_data);
    printf("Encoded codeword: ");
    print_rs_poly_vector(&codeword);
    
    // Test all possible single error patterns
    printf("\n--- Testing All Single Error Patterns ---\n");
    int single_error_tests = 0;
    int single_error_passed = 0;
    
    for (int error_pos = 0; error_pos < 8; error_pos++) {
        for (int error_val = 1; error_val < 16; error_val++) {  // All non-zero error values
            // Create corrupted codeword by introducing error
            rs_poly_vector corrupted = codeword;
            char original_val = corrupted.val[error_pos];
            corrupted.val[error_pos] = gf_sum(original_val, error_val);  // Add error
            
            // If the error doesn't change the symbol, skip (no actual error)
            if (corrupted.val[error_pos] == original_val) {
                continue;
            }
            
            // Treat as erasure for decoding
            corrupted.val[error_pos] = 0;  // Mark as erased
            
            rs_poly_vector decoded = { .len = 8 };
            int erasures[] = { error_pos };
            
            single_error_tests++;
            
            if (avx2_rs_decode_table_lookup_v2(&decode_table, &corrupted, erasures, 1, &decoded) == 0) {
                int correct = 1;
                for (int i = 0; i < 8; i++) {
                    if (decoded.val[i] != test_data.val[i]) {
                        correct = 0;
                        break;
                    }
                }
                if (correct) {
                    single_error_passed++;
                } else {
                    printf("FAILED: Error at pos %d, value %d -> %d\n", 
                           error_pos, original_val, corrupted.val[error_pos]);
                }
            }
        }
    }
    
    printf("Single error correction: %d/%d passed (%.1f%%)\n", 
           single_error_passed, single_error_tests, 
           100.0 * single_error_passed / single_error_tests);
    
    // Test burst error patterns (multiple positions)
    printf("\n--- Testing Burst Error Patterns ---\n");
    int burst_tests = 0;
    int burst_passed = 0;
    
    // Test all combinations of 2 erasures
    for (int err1 = 0; err1 < 7; err1++) {
        for (int err2 = err1 + 1; err2 < 8; err2++) {
            rs_poly_vector corrupted = codeword;
            corrupted.val[err1] = 0;  // Erase first position
            corrupted.val[err2] = 0;  // Erase second position
            
            // For 2 erasures, we would need 2 parity symbols, but our table only handles 1
            // So we expect these to fail - this tests the error handling
            rs_poly_vector decoded = { .len = 8 };
            int erasures[] = { err1, err2 };
            
            burst_tests++;
            
            // This should fail since we only have 1 parity constraint in our table
            int result = avx2_rs_decode_table_lookup_v2(&decode_table, &corrupted, erasures, 2, &decoded);
            if (result != 0) {
                burst_passed++;  // Expected to fail
            }
        }
    }
    
    printf("Burst error handling: %d/%d correctly failed (%.1f%%)\n", 
           burst_passed, burst_tests, 100.0 * burst_passed / burst_tests);
    
    printf("\n=== AVX2 Systematic Error Correction Summary ===\n");
    printf("Single errors corrected: %d/%d (%.1f%%)\n", 
           single_error_passed, single_error_tests, 
           100.0 * single_error_passed / single_error_tests);
    printf("Burst errors properly rejected: %d/%d (%.1f%%)\n", 
           burst_passed, burst_tests, 100.0 * burst_passed / burst_tests);
    
    // Clean up
    free_comprehensive_avx2_decode_table(&decode_table);
    free_avx2_rs_encoder(encoder);
    printf("\n=============== AVX2 Systematic Error Correction Tests Complete ===============\n");
}

// Performance test for complete encode/decode pipeline
void test_avx2_pipeline_performance() {
    printf("\n=============== AVX2 Pipeline Performance Test ===============\n");
    
    // Initialize encoder and decoder
    rs_model_avx2 *encoder = init_avx2_rs_encoder();
    if (!encoder) {
        printf("Failed to initialize AVX2 encoder\n");
        return;
    }
    
    rs_decode_table_avx2 decode_table;
    if (init_comprehensive_avx2_decode_table(&decode_table) != 0) {
        printf("Failed to initialize decode table\n");
        free_avx2_rs_encoder(encoder);
        return;
    }
    
    int test_iterations = 100000;
    rs_poly_vector test_data = { .len = 8, .val = {1, 2, 3, 4, 5, 6, 7, 8} };
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };
    
    printf("Pipeline performance test with %d iterations:\n", test_iterations);
    
    // Test 1: Complete pipeline without errors
    clock_t start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        // Encode
        avx2_rs_encode(encoder, &test_data, &parity);
        
        // Create codeword
        for (int j = 0; j < 8; j++) {
            codeword.val[j] = test_data.val[j];
        }
        for (int j = 0; j < 2; j++) {
            codeword.val[8 + j] = parity.val[j];
        }
        
        // Decode (no errors)
        int erasures[] = {};
        avx2_rs_decode_table_lookup_v2(&decode_table, &codeword, erasures, 0, &decoded);
    }
    clock_t end_time = clock();
    double time_no_errors = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Complete pipeline (no errors): %f seconds (%.1f ops/sec)\n", 
           time_no_errors, test_iterations / time_no_errors);
    
    // Test 2: Complete pipeline with single error correction
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        // Encode
        avx2_rs_encode(encoder, &test_data, &parity);
        
        // Create corrupted codeword (error at position 3)
        for (int j = 0; j < 8; j++) {
            codeword.val[j] = test_data.val[j];
        }
        for (int j = 0; j < 2; j++) {
            codeword.val[8 + j] = parity.val[j];
        }
        codeword.val[3] = 0;  // Introduce erasure
        
        // Decode with error correction
        int erasures[] = { 3 };
        avx2_rs_decode_table_lookup_v2(&decode_table, &codeword, erasures, 1, &decoded);
    }
    end_time = clock();
    double time_with_errors = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Complete pipeline (with error correction): %f seconds (%.1f ops/sec)\n", 
           time_with_errors, test_iterations / time_with_errors);
    
    // Verify final result
    int correct = 1;
    for (int i = 0; i < 8; i++) {
        if (decoded.val[i] != test_data.val[i]) {
            correct = 0;
            break;
        }
    }
    printf("Final pipeline result: %s\n", correct ? "CORRECT" : "INCORRECT");
    
    // Throughput calculations
    double throughput_no_errors = (test_iterations * 8.0) / time_no_errors / 1e6;  // MB/s
    double throughput_with_errors = (test_iterations * 8.0) / time_with_errors / 1e6;
    
    printf("\nPipeline throughput (8-byte packets):\n");
    printf("No errors: %.1f MB/s\n", throughput_no_errors);
    printf("With error correction: %.1f MB/s\n", throughput_with_errors);
    printf("Error correction overhead: %.2fx slowdown\n", time_with_errors / time_no_errors);
    
    // Clean up
    free_comprehensive_avx2_decode_table(&decode_table);
    free_avx2_rs_encoder(encoder);
    printf("=============== AVX2 Pipeline Performance Tests Complete ===============\n");
}

int main() {
    printf("AVX2 Reed-Solomon Encoder/Decoder Integration Test\n");
    printf("==================================================\n");
    
    // Test complete encode/decode cycle
    test_avx2_encode_decode_cycle();
    
    // Test systematic error correction
    test_avx2_systematic_error_correction();
    
    // Test pipeline performance
    test_avx2_pipeline_performance();
    
    return 0;
}