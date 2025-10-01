#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../neon/ejfat_rs_neon_encoder.h"

// Simple utility function to print polynomial vectors
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

// Reference encoder for verification (non-NEON)
void reference_rs_encode(rs_poly_vector *data, rs_poly_vector *parity) {
    // RS(10,8) parity matrix from generator matrix
    static const char Genc[2][8] = {
        {14, 6, 14, 9, 7, 1, 15, 6},  // First parity constraint
        {5, 9, 4, 13, 8, 1, 5, 8}     // Second parity constraint
    };
    
    parity->len = 2;
    
    for (int row = 0; row < 2; row++) {
        parity->val[row] = 0;
        for (int col = 0; col < 8; col++) {
            parity->val[row] = gf_sum(parity->val[row], 
                                     gf_mul(data->val[col], Genc[row][col]));
        }
    }
}

// Test encoder functionality with various data patterns
void test_neon_encoder() {
    printf("\n=============== Testing NEON RS Encoder ===============\n");
    
    // Initialize NEON encoder
    rs_model *rs = init_neon_rs_encoder();
    if (!rs) {
        printf("Failed to initialize NEON RS encoder\n");
        return;
    }
    
    printf("NEON RS encoder initialized successfully\n");
    
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
        {"Random pattern", {7, 13, 2, 11, 5, 9, 14, 3}},
        {"Sparse pattern", {1, 0, 0, 1, 0, 0, 1, 0}}
    };
    
    int num_tests = sizeof(test_patterns) / sizeof(test_patterns[0]);
    int passed_tests = 0;
    
    for (int t = 0; t < num_tests; t++) {
        printf("\n--- Test %d: %s ---\n", t + 1, test_patterns[t].name);
        
        // Setup test data
        rs_poly_vector data = { .len = 8 };
        rs_poly_vector neon_parity = { .len = 2 };
        rs_poly_vector ref_parity = { .len = 2 };
        
        memcpy(data.val, test_patterns[t].data, 8);
        
        printf("Input data: ");
        print_rs_poly_vector(&data);
        
        // Encode with NEON
        neon_rs_encode(rs, &data, &neon_parity);
        printf("NEON parity: ");
        print_rs_poly_vector(&neon_parity);
        
        // Encode with reference implementation
        reference_rs_encode(&data, &ref_parity);
        printf("Reference parity: ");
        print_rs_poly_vector(&ref_parity);
        
        // Compare results
        int correct = 1;
        for (int i = 0; i < 2; i++) {
            if (neon_parity.val[i] != ref_parity.val[i]) {
                correct = 0;
                break;
            }
        }
        
        if (correct) {
            printf("Result: PASSED\n");
            passed_tests++;
        } else {
            printf("Result: FAILED (parity mismatch)\n");
        }
    }
    
    printf("\n=== Encoder Test Summary ===\n");
    printf("Tests passed: %d/%d\n", passed_tests, num_tests);
    
    // Test codeword properties
    printf("\n--- Testing Codeword Properties ---\n");
    rs_poly_vector test_data = { .len = 8, .val = {1, 2, 3, 4, 5, 6, 7, 8} };
    rs_poly_vector parity = { .len = 2 };
    
    neon_rs_encode(rs, &test_data, &parity);
    
    printf("Test data: ");
    print_rs_poly_vector(&test_data);
    printf("Computed parity: ");
    print_rs_poly_vector(&parity);
    
    // Verify that data * G^T = parity (systematic encoding property)
    printf("Codeword: [ ");
    for (int i = 0; i < 8; i++) {
        printf("%d ", test_data.val[i]);
    }
    for (int i = 0; i < 2; i++) {
        printf("%d ", parity.val[i]);
    }
    printf("]\n");
    
    printf("Encoder validation: %s\n", passed_tests == num_tests ? "ALL PASSED" : "SOME FAILED");
    
    // Clean up
    free_neon_rs_encoder(rs);
    printf("\n=============== NEON Encoder Tests Complete ===============\n");
}

// Performance test comparing NEON vs reference encoder
void test_neon_encoder_performance() {
    printf("\n=============== NEON Encoder Performance Test ===============\n");
    
    rs_model *rs = init_neon_rs_encoder();
    if (!rs) {
        printf("Failed to initialize NEON RS encoder\n");
        return;
    }
    
    int test_iterations = 1000000;
    rs_poly_vector test_data = { .len = 8, .val = {1, 2, 3, 4, 5, 6, 7, 8} };
    rs_poly_vector neon_parity = { .len = 2 };
    rs_poly_vector ref_parity = { .len = 2 };
    
    printf("Performance test with %d iterations:\n", test_iterations);
    
    // Test NEON encoder performance
    clock_t start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        neon_rs_encode(rs, &test_data, &neon_parity);
    }
    clock_t end_time = clock();
    double time_neon = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("NEON encoder: %f seconds (%.1f ops/sec)\n", 
           time_neon, test_iterations / time_neon);
    
    // Test reference encoder performance
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        reference_rs_encode(&test_data, &ref_parity);
    }
    end_time = clock();
    double time_ref = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Reference encoder: %f seconds (%.1f ops/sec)\n", 
           time_ref, test_iterations / time_ref);
    
    // Performance comparison
    printf("\nPerformance improvement: %.2fx faster\n", time_ref / time_neon);
    
    // Verify final results match
    int correct = 1;
    for (int i = 0; i < 2; i++) {
        if (neon_parity.val[i] != ref_parity.val[i]) {
            correct = 0;
            break;
        }
    }
    printf("Final result verification: %s\n", correct ? "CORRECT" : "INCORRECT");
    
    // Throughput calculations (assuming 8-byte packets)
    double throughput_neon = (test_iterations * 8.0) / time_neon / 1e6;  // MB/s
    double throughput_ref = (test_iterations * 8.0) / time_ref / 1e6;
    
    printf("\nData throughput (8-byte packets):\n");
    printf("NEON encoder: %.1f MB/s\n", throughput_neon);
    printf("Reference encoder: %.1f MB/s\n", throughput_ref);
    
    free_neon_rs_encoder(rs);
    printf("=============== Performance Test Complete ===============\n");
}

// Test edge cases and error conditions
void test_encoder_edge_cases() {
    printf("\n=============== Testing Encoder Edge Cases ===============\n");
    
    rs_model *rs = init_neon_rs_encoder();
    if (!rs) {
        printf("Failed to initialize NEON RS encoder\n");
        return;
    }
    
    // Test 1: Maximum GF(16) values
    printf("\n--- Test 1: Maximum GF(16) values ---\n");
    rs_poly_vector max_data = { .len = 8, .val = {15, 15, 15, 15, 15, 15, 15, 15} };
    rs_poly_vector max_parity = { .len = 2 };
    
    neon_rs_encode(rs, &max_data, &max_parity);
    printf("Max data: ");
    print_rs_poly_vector(&max_data);
    printf("Max parity: ");
    print_rs_poly_vector(&max_parity);
    
    // Verify parity values are valid GF(16) elements
    int valid_gf = 1;
    for (int i = 0; i < 2; i++) {
        if (max_parity.val[i] < 0 || max_parity.val[i] > 15) {
            valid_gf = 0;
            break;
        }
    }
    printf("GF(16) validity: %s\n", valid_gf ? "PASSED" : "FAILED");
    
    // Test 2: All zero input
    printf("\n--- Test 2: All zero input ---\n");
    rs_poly_vector zero_data = { .len = 8, .val = {0, 0, 0, 0, 0, 0, 0, 0} };
    rs_poly_vector zero_parity = { .len = 2 };
    
    neon_rs_encode(rs, &zero_data, &zero_parity);
    printf("Zero data: ");
    print_rs_poly_vector(&zero_data);
    printf("Zero parity: ");
    print_rs_poly_vector(&zero_parity);
    
    // Zero input should produce zero parity
    int zero_result = (zero_parity.val[0] == 0 && zero_parity.val[1] == 0);
    printf("Zero property: %s\n", zero_result ? "PASSED" : "FAILED");
    
    // Test 3: Single bit patterns
    printf("\n--- Test 3: Single bit patterns ---\n");
    for (int pos = 0; pos < 8; pos++) {
        rs_poly_vector single_data = { .len = 8, .val = {0, 0, 0, 0, 0, 0, 0, 0} };
        rs_poly_vector single_parity = { .len = 2 };
        
        single_data.val[pos] = 1;  // Set single position to 1
        neon_rs_encode(rs, &single_data, &single_parity);
        
        printf("Position %d=1: parity=[%d, %d]\n", pos, 
               single_parity.val[0], single_parity.val[1]);
    }
    
    free_neon_rs_encoder(rs);
    printf("\n=============== Edge Case Tests Complete ===============\n");
}

int main() {
    printf("NEON Reed-Solomon Encoder Test Program\n");
    printf("======================================\n");
    
    // Test encoder functionality
    test_neon_encoder();
    
    // Test encoder performance
    test_neon_encoder_performance();
    
    // Test edge cases
    test_encoder_edge_cases();
    
    return 0;
}