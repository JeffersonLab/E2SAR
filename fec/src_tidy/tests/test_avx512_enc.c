#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../avx512/ejfat_rs_avx512_encoder.h"

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

// Reference encoder for verification (non-AVX)
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
void test_avx512_encoder() {
    printf("\n=============== Testing AVX-512 RS Encoder ===============\n");
    
    // Check if we're using AVX-512 or fallback
    #if defined(__AVX512F__) && defined(__AVX512BW__)
    printf("Using native AVX-512 implementation\n");
    #else
    printf("Using scalar fallback implementation (AVX-512 not available)\n");
    #endif
    
    // Initialize AVX-512 encoder
    rs_model_avx512 *rs = init_avx512_rs_encoder();
    if (!rs) {
        printf("Failed to initialize AVX-512 RS encoder\n");
        return;
    }
    
    printf("AVX-512 RS encoder initialized successfully\n");
    
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
        rs_poly_vector avx512_parity = { .len = 2 };
        rs_poly_vector ref_parity = { .len = 2 };
        
        memcpy(data.val, test_patterns[t].data, 8);
        
        printf("Input data: ");
        print_rs_poly_vector(&data);
        
        // Encode with AVX-512
        avx512_rs_encode(rs, &data, &avx512_parity);
        printf("AVX-512 parity: ");
        print_rs_poly_vector(&avx512_parity);
        
        // Encode with reference implementation
        reference_rs_encode(&data, &ref_parity);
        printf("Reference parity: ");
        print_rs_poly_vector(&ref_parity);
        
        // Compare results
        int correct = 1;
        for (int i = 0; i < 2; i++) {
            if (avx512_parity.val[i] != ref_parity.val[i]) {
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
    
    printf("\n=== AVX-512 Encoder Test Summary ===\n");
    printf("Tests passed: %d/%d\n", passed_tests, num_tests);
    
    // Clean up
    free_avx512_rs_encoder(rs);
    printf("Encoder validation: %s\n", passed_tests == num_tests ? "ALL PASSED" : "SOME FAILED");
    printf("\n=============== AVX-512 Encoder Tests Complete ===============\n");
}

// Performance test comparing AVX-512 vs reference encoder
void test_avx512_encoder_performance() {
    printf("\n=============== AVX-512 Encoder Performance Test ===============\n");
    
    rs_model_avx512 *rs = init_avx512_rs_encoder();
    if (!rs) {
        printf("Failed to initialize AVX-512 RS encoder\n");
        return;
    }
    
    int test_iterations = 1000000;
    rs_poly_vector test_data = { .len = 8, .val = {1, 2, 3, 4, 5, 6, 7, 8} };
    rs_poly_vector avx512_parity = { .len = 2 };
    rs_poly_vector ref_parity = { .len = 2 };
    
    printf("Performance test with %d iterations:\n", test_iterations);
    
    // Test AVX-512 encoder performance
    clock_t start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        avx512_rs_encode(rs, &test_data, &avx512_parity);
    }
    clock_t end_time = clock();
    double time_avx512 = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("AVX-512 encoder: %f seconds (%.1f ops/sec)\n", 
           time_avx512, test_iterations / time_avx512);
    
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
    printf("\nPerformance improvement: %.2fx faster\n", time_ref / time_avx512);
    
    // Verify final results match
    int correct = 1;
    for (int i = 0; i < 2; i++) {
        if (avx512_parity.val[i] != ref_parity.val[i]) {
            correct = 0;
            break;
        }
    }
    printf("Final result verification: %s\n", correct ? "CORRECT" : "INCORRECT");
    
    // Throughput calculations (assuming 8-byte packets)
    double throughput_avx512 = (test_iterations * 8.0) / time_avx512 / 1e6;  // MB/s
    double throughput_ref = (test_iterations * 8.0) / time_ref / 1e6;
    
    printf("\nData throughput (8-byte packets):\n");
    printf("AVX-512 encoder: %.1f MB/s\n", throughput_avx512);
    printf("Reference encoder: %.1f MB/s\n", throughput_ref);
    
    free_avx512_rs_encoder(rs);
    printf("=============== Performance Test Complete ===============\n");
}

// Test platform detection and capability reporting
void test_platform_capabilities() {
    printf("\n=============== Platform Capabilities Test ===============\n");
    
    // Check compile-time capabilities
    #if defined(__AVX512F__)
    printf("Compile-time AVX-512F support: YES\n");
    #else
    printf("Compile-time AVX-512F support: NO\n");
    #endif
    
    #if defined(__AVX512BW__)
    printf("Compile-time AVX-512BW support: YES\n");
    #else
    printf("Compile-time AVX-512BW support: NO\n");
    #endif
    
    // Test basic functionality
    rs_model_avx512 *rs = init_avx512_rs_encoder();
    if (rs) {
        printf("AVX-512 encoder initialization: SUCCESS\n");
        
        // Quick functionality test
        rs_poly_vector test_data = { .len = 8, .val = {1, 2, 3, 4, 5, 6, 7, 8} };
        rs_poly_vector parity = { .len = 2 };
        
        avx512_rs_encode(rs, &test_data, &parity);
        printf("Quick encode test result: [%d, %d]\n", parity.val[0], parity.val[1]);
        
        free_avx512_rs_encoder(rs);
    } else {
        printf("AVX-512 encoder initialization: FAILED\n");
    }
    
    printf("=============== Platform Capabilities Test Complete ===============\n");
}

int main() {
    printf("AVX-512 Reed-Solomon Encoder Test Program\n");
    printf("=========================================\n");
    
    // Test platform capabilities
    test_platform_capabilities();
    
    // Test encoder functionality
    test_avx512_encoder();
    
    // Test encoder performance
    test_avx512_encoder_performance();
    
    return 0;
}