#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "./ejfat_rs_avx2_encoder.h"

// Utility function to print polynomial vectors
void print_rs_poly_vector(rs_poly_vector *v) {
    printf("[ ");
    for (int i = 0; i < v->len; i++) {
        printf("%d ", v->val[i]);
    }
    printf("]\n");
}

// Test correctness of both implementations
void test_correctness_comparison() {
    printf("\n=============== Testing Correctness Comparison ===============\n");
    
    // Check if we're using AVX2 or fallback
    #if defined(__AVX2__) && (defined(__x86_64__) || defined(_M_X64))
    printf("Using native AVX2 implementations\n");
    #else
    printf("Using scalar fallback implementations (AVX2 not available)\n");
    #endif
    
    // Initialize encoder
    rs_model_avx2 *encoder = init_avx2_rs_encoder();
    if (!encoder) {
        printf("Failed to initialize AVX2 encoder\n");
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
        {"Random 2", {12, 6, 10, 4, 1, 15, 8, 13}},
        {"Sparse", {0, 5, 0, 10, 0, 3, 0, 8}},
        {"Edge case", {14, 0, 1, 15, 2, 0, 13, 7}}
    };
    
    int num_patterns = sizeof(test_patterns) / sizeof(test_patterns[0]);
    int total_tests = 0;
    int passed_tests = 0;
    
    for (int p = 0; p < num_patterns; p++) {
        printf("\n--- Pattern %d: %s ---\n", p + 1, test_patterns[p].name);
        
        // Setup test data
        rs_poly_vector data = { .len = 8 };
        rs_poly_vector parity_orig = { .len = 2 };
        rs_poly_vector parity_opt = { .len = 2 };
        
        memcpy(data.val, test_patterns[p].data, 8);
        
        printf("Input data: ");
        print_rs_poly_vector(&data);
        
        // Test original implementation
        avx2_rs_encode(encoder, &data, &parity_orig);
        printf("Original parity: ");
        print_rs_poly_vector(&parity_orig);
        
        // Test optimized implementation
        avx2_rs_encode_optimized(encoder, &data, &parity_opt);
        printf("Optimized parity: ");
        print_rs_poly_vector(&parity_opt);
        
        // Compare results
        total_tests++;
        int results_match = 1;
        for (int i = 0; i < 2; i++) {
            if (parity_orig.val[i] != parity_opt.val[i]) {
                results_match = 0;
                break;
            }
        }
        
        if (results_match) {
            printf("Result: MATCH ✓\n");
            passed_tests++;
        } else {
            printf("Result: MISMATCH ✗\n");
            printf("Difference detected in parity symbols!\n");
        }
    }
    
    printf("\n=== Correctness Test Summary ===\n");
    printf("Total patterns tested: %d\n", total_tests);
    printf("Matching results: %d\n", passed_tests);
    printf("Mismatched results: %d\n", total_tests - passed_tests);
    printf("Correctness: %.1f%%\n", 100.0 * passed_tests / total_tests);
    
    // Clean up
    free_avx2_rs_encoder(encoder);
    printf("\n=============== Correctness Tests Complete ===============\n");
}

// Performance comparison test
void test_performance_comparison() {
    printf("\n=============== Performance Comparison Test ===============\n");
    
    // Initialize encoder
    rs_model_avx2 *encoder = init_avx2_rs_encoder();
    if (!encoder) {
        printf("Failed to initialize AVX2 encoder\n");
        return;
    }
    
    int test_iterations = 1000000;  // 1 million iterations for good measurement
    rs_poly_vector test_data = { .len = 8, .val = {1, 2, 3, 4, 5, 6, 7, 8} };
    rs_poly_vector parity_orig = { .len = 2 };
    rs_poly_vector parity_opt = { .len = 2 };
    
    printf("Performance test with %d iterations:\n", test_iterations);
    printf("Test data: ");
    print_rs_poly_vector(&test_data);
    
    // Test 1: Original implementation
    printf("\n--- Testing Original Implementation ---\n");
    clock_t start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        avx2_rs_encode(encoder, &test_data, &parity_orig);
    }
    clock_t end_time = clock();
    double time_original = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Original implementation: %.6f seconds (%.1f ops/sec)\n", 
           time_original, test_iterations / time_original);
    
    // Test 2: Optimized implementation
    printf("\n--- Testing Optimized Implementation ---\n");
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        avx2_rs_encode_optimized(encoder, &test_data, &parity_opt);
    }
    end_time = clock();
    double time_optimized = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Optimized implementation: %.6f seconds (%.1f ops/sec)\n", 
           time_optimized, test_iterations / time_optimized);
    
    // Verify final results match
    int final_match = 1;
    for (int i = 0; i < 2; i++) {
        if (parity_orig.val[i] != parity_opt.val[i]) {
            final_match = 0;
            break;
        }
    }
    printf("Final result verification: %s\n", final_match ? "MATCH" : "MISMATCH");
    
    // Performance analysis
    printf("\n=== Performance Analysis ===\n");
    if (time_optimized > 0) {
        double speedup = time_original / time_optimized;
        printf("Speedup: %.2fx ", speedup);
        if (speedup > 1.1) {
            printf("(Optimized version is %.1f%% faster)\n", (speedup - 1.0) * 100.0);
        } else if (speedup < 0.9) {
            printf("(Optimized version is %.1f%% slower)\n", (1.0 / speedup - 1.0) * 100.0);
        } else {
            printf("(Performance is similar)\n");
        }
        
        // Throughput calculations (assuming 8-byte packets)
        double throughput_orig = (test_iterations * 8.0) / time_original / 1e6;  // MB/s
        double throughput_opt = (test_iterations * 8.0) / time_optimized / 1e6;
        
        printf("Original throughput: %.1f MB/s\n", throughput_orig);
        printf("Optimized throughput: %.1f MB/s\n", throughput_opt);
        printf("Throughput improvement: %.1f MB/s (%.1f%% gain)\n", 
               throughput_opt - throughput_orig, 
               (throughput_opt / throughput_orig - 1.0) * 100.0);
    }
    
    // Clean up
    free_avx2_rs_encoder(encoder);
    printf("\n=============== Performance Tests Complete ===============\n");
}

// Micro-benchmark for individual operations
void test_micro_benchmarks() {
    printf("\n=============== Micro-Benchmark Analysis ===============\n");
    
    #if defined(__AVX2__) && (defined(__x86_64__) || defined(_M_X64))
    printf("Running detailed AVX2 micro-benchmarks...\n");
    #else
    printf("AVX2 not available - micro-benchmarks will show scalar performance\n");
    #endif
    
    rs_model_avx2 *encoder = init_avx2_rs_encoder();
    if (!encoder) {
        printf("Failed to initialize encoder\n");
        return;
    }
    
    int micro_iterations = 10000000;  // 10 million for micro-benchmarks
    
    // Test with different data patterns that stress different aspects
    struct {
        const char *name;
        char data[8];
        const char *description;
    } micro_patterns[] = {
        {"All zeros", {0, 0, 0, 0, 0, 0, 0, 0}, "Tests zero-handling optimizations"},
        {"All max", {15, 15, 15, 15, 15, 15, 15, 15}, "Tests modulo operations"},
        {"Mixed", {0, 1, 14, 15, 7, 8, 0, 3}, "Tests mixed zero/non-zero handling"},
        {"Sequential", {1, 2, 3, 4, 5, 6, 7, 8}, "Tests typical data patterns"}
    };
    
    int num_micro = sizeof(micro_patterns) / sizeof(micro_patterns[0]);
    
    for (int p = 0; p < num_micro; p++) {
        printf("\n--- Micro-benchmark: %s ---\n", micro_patterns[p].name);
        printf("Description: %s\n", micro_patterns[p].description);
        
        rs_poly_vector data = { .len = 8 };
        rs_poly_vector parity = { .len = 2 };
        memcpy(data.val, micro_patterns[p].data, 8);
        
        // Original version
        clock_t start = clock();
        for (int i = 0; i < micro_iterations; i++) {
            avx2_rs_encode(encoder, &data, &parity);
        }
        clock_t end = clock();
        double time_orig = (double)(end - start) / CLOCKS_PER_SEC;
        
        // Optimized version  
        start = clock();
        for (int i = 0; i < micro_iterations; i++) {
            avx2_rs_encode_optimized(encoder, &data, &parity);
        }
        end = clock();
        double time_opt = (double)(end - start) / CLOCKS_PER_SEC;
        
        printf("Original: %.6f sec (%.1f M ops/sec)\n", 
               time_orig, micro_iterations / time_orig / 1e6);
        printf("Optimized: %.6f sec (%.1f M ops/sec)\n", 
               time_opt, micro_iterations / time_opt / 1e6);
        
        if (time_opt > 0) {
            double speedup = time_orig / time_opt;
            printf("Speedup: %.2fx\n", speedup);
        }
    }
    
    free_avx2_rs_encoder(encoder);
    printf("\n=============== Micro-Benchmarks Complete ===============\n");
}

// Platform capability detection
void test_platform_info() {
    printf("\n=============== Platform Information ===============\n");
    
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
    
    #if defined(__AVX2__) && (defined(__x86_64__) || defined(_M_X64))
    printf("Expected behavior: Native AVX2 optimizations active\n");
    printf("Optimization features:\n");
    printf("  - Vectorized table lookups with _mm256_i32gather_epi32\n");
    printf("  - Vectorized modulo operations\n");
    printf("  - Vectorized zero masking with _mm256_andnot_si256\n");
    printf("  - Vectorized horizontal XOR reduction\n");
    #else
    printf("Expected behavior: Scalar fallback implementation\n");
    printf("Note: Both functions will use identical scalar code\n");
    #endif
    
    printf("=============== Platform Information Complete ===============\n");
}

int main() {
    printf("AVX2 Reed-Solomon Encoder Optimization Comparison\n");
    printf("=================================================\n");
    
    // Show platform information
    test_platform_info();
    
    // Test correctness first
    test_correctness_comparison();
    
    // Test performance comparison
    test_performance_comparison();
    
    // Run micro-benchmarks
    test_micro_benchmarks();
    
    return 0;
}