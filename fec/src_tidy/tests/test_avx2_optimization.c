#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "../avx2/ejfat_rs_avx2_encoder.h"

// Pure scalar implementation for comparison (no SIMD)
void scalar_rs_encode(rs_model_avx2 *rs, rs_poly_vector *d, rs_poly_vector *p) {
    // Pure scalar implementation without any SIMD optimizations
    // This provides a baseline for performance comparison
    
    for (int row = 0; row < rs->p; row++) {
        p->val[row] = 0;
        for (int col = 0; col < rs->n; col++) {
            if (d->val[col] != 0) {  // Handle zero case properly
                // GF multiplication using lookup tables in scalar form
                char exp_d = _ejfat_rs_gf_exp_seq[d->val[col]];
                char exp_enc = rs->Genc_exp[row][col];
                char exp_sum = (exp_d + exp_enc) % 15;
                char result = _ejfat_rs_gf_log_seq[exp_sum];
                p->val[row] ^= result;
            }
        }
    }
}

// Utility function to print polynomial vectors
void print_rs_poly_vector(rs_poly_vector *v) {
    printf("[ ");
    for (int i = 0; i < v->len; i++) {
        printf("%d ", v->val[i]);
    }
    printf("]\n");
}

// Test correctness of all four implementations
void test_correctness_comparison() {
    printf("\n=============== Testing Correctness Comparison (4 Versions) ===============\n");
    
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
        rs_poly_vector parity_scalar = { .len = 2 };
        rs_poly_vector parity_orig = { .len = 2 };
        rs_poly_vector parity_opt = { .len = 2 };
        rs_poly_vector parity_ultra = { .len = 2 };
        
        memcpy(data.val, test_patterns[p].data, 8);
        
        printf("Input data: ");
        print_rs_poly_vector(&data);
        
        // Test scalar implementation (baseline)
        scalar_rs_encode(encoder, &data, &parity_scalar);
        printf("Scalar parity:    ");
        print_rs_poly_vector(&parity_scalar);
        
        // Test original AVX2 implementation
        avx2_rs_encode(encoder, &data, &parity_orig);
        printf("Original parity:  ");
        print_rs_poly_vector(&parity_orig);
        
        // Test optimized AVX2 implementation
        avx2_rs_encode_optimized(encoder, &data, &parity_opt);
        printf("Optimized parity: ");
        print_rs_poly_vector(&parity_opt);
        
        // Test ultra-optimized AVX2 implementation
        avx2_rs_encode_ultra(encoder, &data, &parity_ultra);
        printf("Ultra parity:     ");
        print_rs_poly_vector(&parity_ultra);
        
        // Compare all results
        total_tests++;
        int all_match = 1;
        
        for (int i = 0; i < 2; i++) {
            if (parity_scalar.val[i] != parity_orig.val[i] ||
                parity_scalar.val[i] != parity_opt.val[i] ||
                parity_scalar.val[i] != parity_ultra.val[i]) {
                all_match = 0;
                break;
            }
        }
        
        printf("Results: ");
        if (all_match) {
            printf("ALL MATCH ✓\n");
            passed_tests++;
        } else {
            printf("MISMATCH ✗\n");
            printf("  Scalar:    [%d, %d]\n", parity_scalar.val[0], parity_scalar.val[1]);
            printf("  Original:  [%d, %d]\n", parity_orig.val[0], parity_orig.val[1]);
            printf("  Optimized: [%d, %d]\n", parity_opt.val[0], parity_opt.val[1]);
            printf("  Ultra:     [%d, %d]\n", parity_ultra.val[0], parity_ultra.val[1]);
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

// Performance comparison test for all four implementations
void test_performance_comparison() {
    printf("\n=============== Performance Comparison Test (4 Versions) ===============\n");
    
    // Initialize encoder
    rs_model_avx2 *encoder = init_avx2_rs_encoder();
    if (!encoder) {
        printf("Failed to initialize AVX2 encoder\n");
        return;
    }
    
    int test_iterations = 1000000;  // 1 million iterations for good measurement
    rs_poly_vector test_data = { .len = 8, .val = {1, 2, 3, 4, 5, 6, 7, 8} };
    rs_poly_vector parity_scalar = { .len = 2 };
    rs_poly_vector parity_orig = { .len = 2 };
    rs_poly_vector parity_opt = { .len = 2 };
    rs_poly_vector parity_ultra = { .len = 2 };
    
    printf("Performance test with %d iterations:\n", test_iterations);
    printf("Test data: ");
    print_rs_poly_vector(&test_data);
    
    // Test 1: Scalar implementation (baseline)
    printf("\n--- Testing Scalar Implementation (Baseline) ---\n");
    clock_t start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        scalar_rs_encode(encoder, &test_data, &parity_scalar);
    }
    clock_t end_time = clock();
    double time_scalar = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Scalar implementation: %.6f seconds (%.1f ops/sec)\n", 
           time_scalar, test_iterations / time_scalar);
    
    // Test 2: Original AVX2 implementation
    printf("\n--- Testing Original AVX2 Implementation ---\n");
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        avx2_rs_encode(encoder, &test_data, &parity_orig);
    }
    end_time = clock();
    double time_original = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Original AVX2 implementation: %.6f seconds (%.1f ops/sec)\n", 
           time_original, test_iterations / time_original);
    
    // Test 3: Optimized AVX2 implementation
    printf("\n--- Testing Optimized AVX2 Implementation ---\n");
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        avx2_rs_encode_optimized(encoder, &test_data, &parity_opt);
    }
    end_time = clock();
    double time_optimized = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Optimized AVX2 implementation: %.6f seconds (%.1f ops/sec)\n", 
           time_optimized, test_iterations / time_optimized);
    
    // Test 4: Ultra-optimized AVX2 implementation
    printf("\n--- Testing Ultra-Optimized AVX2 Implementation ---\n");
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        avx2_rs_encode_ultra(encoder, &test_data, &parity_ultra);
    }
    end_time = clock();
    double time_ultra = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Ultra AVX2 implementation: %.6f seconds (%.1f ops/sec)\n", 
           time_ultra, test_iterations / time_ultra);
    
    // Verify all results match
    int all_match = 1;
    for (int i = 0; i < 2; i++) {
        if (parity_scalar.val[i] != parity_orig.val[i] ||
            parity_scalar.val[i] != parity_opt.val[i] ||
            parity_scalar.val[i] != parity_ultra.val[i]) {
            all_match = 0;
            break;
        }
    }
    printf("Final result verification: %s\n", all_match ? "ALL MATCH" : "MISMATCH DETECTED");
    
    // Performance analysis
    printf("\n=== Performance Analysis ===\n");
    
    // Speedup calculations relative to scalar baseline
    if (time_scalar > 0) {
        double speedup_orig = time_scalar / time_original;
        double speedup_opt = time_scalar / time_optimized;
        double speedup_ultra = time_scalar / time_ultra;
        
        printf("Speedup vs Scalar Baseline:\n");
        printf("  Original AVX2:   %.2fx ", speedup_orig);
        if (speedup_orig > 1.1) {
            printf("(%.1f%% faster than scalar)\n", (speedup_orig - 1.0) * 100.0);
        } else if (speedup_orig < 0.9) {
            printf("(%.1f%% slower than scalar)\n", (1.0 / speedup_orig - 1.0) * 100.0);
        } else {
            printf("(similar to scalar)\n");
        }
        
        printf("  Optimized AVX2:  %.2fx ", speedup_opt);
        if (speedup_opt > 1.1) {
            printf("(%.1f%% faster than scalar)\n", (speedup_opt - 1.0) * 100.0);
        } else if (speedup_opt < 0.9) {
            printf("(%.1f%% slower than scalar)\n", (1.0 / speedup_opt - 1.0) * 100.0);
        } else {
            printf("(similar to scalar)\n");
        }
        
        printf("  Ultra AVX2:      %.2fx ", speedup_ultra);
        if (speedup_ultra > 1.1) {
            printf("(%.1f%% faster than scalar)\n", (speedup_ultra - 1.0) * 100.0);
        } else if (speedup_ultra < 0.9) {
            printf("(%.1f%% slower than scalar)\n", (1.0 / speedup_ultra - 1.0) * 100.0);
        } else {
            printf("(similar to scalar)\n");
        }
    }
    
    // Direct comparison between AVX2 versions
    if (time_optimized > 0 && time_original > 0 && time_ultra > 0) {
        double speedup_opt_vs_orig = time_original / time_optimized;
        double speedup_ultra_vs_orig = time_original / time_ultra;
        double speedup_ultra_vs_opt = time_optimized / time_ultra;
        
        printf("\nDirect AVX2 Comparisons:\n");
        printf("  Optimized vs Original: %.2fx ", speedup_opt_vs_orig);
        if (speedup_opt_vs_orig > 1.1) {
            printf("(%.1f%% improvement)\n", (speedup_opt_vs_orig - 1.0) * 100.0);
        } else if (speedup_opt_vs_orig < 0.9) {
            printf("(%.1f%% regression)\n", (1.0 / speedup_opt_vs_orig - 1.0) * 100.0);
        } else {
            printf("(similar performance)\n");
        }
        
        printf("  Ultra vs Original:     %.2fx ", speedup_ultra_vs_orig);
        if (speedup_ultra_vs_orig > 1.1) {
            printf("(%.1f%% improvement)\n", (speedup_ultra_vs_orig - 1.0) * 100.0);
        } else if (speedup_ultra_vs_orig < 0.9) {
            printf("(%.1f%% regression)\n", (1.0 / speedup_ultra_vs_orig - 1.0) * 100.0);
        } else {
            printf("(similar performance)\n");
        }
        
        printf("  Ultra vs Optimized:    %.2fx ", speedup_ultra_vs_opt);
        if (speedup_ultra_vs_opt > 1.1) {
            printf("(%.1f%% improvement)\n", (speedup_ultra_vs_opt - 1.0) * 100.0);
        } else if (speedup_ultra_vs_opt < 0.9) {
            printf("(%.1f%% regression)\n", (1.0 / speedup_ultra_vs_opt - 1.0) * 100.0);
        } else {
            printf("(similar performance)\n");
        }
    }
    
    // Throughput calculations (assuming 8-byte packets)
    printf("\nThroughput Comparison (8-byte packets):\n");
    if (time_scalar > 0) {
        double throughput_scalar = (test_iterations * 8.0) / time_scalar / 1e6;  // MB/s
        double throughput_orig = (test_iterations * 8.0) / time_original / 1e6;
        double throughput_opt = (test_iterations * 8.0) / time_optimized / 1e6;
        double throughput_ultra = (test_iterations * 8.0) / time_ultra / 1e6;
        
        printf("  Scalar:      %.1f MB/s\n", throughput_scalar);
        printf("  Original:    %.1f MB/s (+%.1f MB/s vs scalar)\n", 
               throughput_orig, throughput_orig - throughput_scalar);
        printf("  Optimized:   %.1f MB/s (+%.1f MB/s vs scalar, +%.1f MB/s vs original)\n", 
               throughput_opt, throughput_opt - throughput_scalar, throughput_opt - throughput_orig);
        printf("  Ultra:       %.1f MB/s (+%.1f MB/s vs scalar, +%.1f MB/s vs optimized)\n", 
               throughput_ultra, throughput_ultra - throughput_scalar, throughput_ultra - throughput_opt);
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
        
        // Scalar version (baseline)
        clock_t start = clock();
        for (int i = 0; i < micro_iterations; i++) {
            scalar_rs_encode(encoder, &data, &parity);
        }
        clock_t end = clock();
        double time_scalar = (double)(end - start) / CLOCKS_PER_SEC;
        
        // Original AVX2 version
        start = clock();
        for (int i = 0; i < micro_iterations; i++) {
            avx2_rs_encode(encoder, &data, &parity);
        }
        end = clock();
        double time_orig = (double)(end - start) / CLOCKS_PER_SEC;
        
        // Optimized AVX2 version  
        start = clock();
        for (int i = 0; i < micro_iterations; i++) {
            avx2_rs_encode_optimized(encoder, &data, &parity);
        }
        end = clock();
        double time_opt = (double)(end - start) / CLOCKS_PER_SEC;
        
        // Ultra-optimized AVX2 version
        start = clock();
        for (int i = 0; i < micro_iterations; i++) {
            avx2_rs_encode_ultra(encoder, &data, &parity);
        }
        end = clock();
        double time_ultra = (double)(end - start) / CLOCKS_PER_SEC;
        
        printf("Scalar:    %.6f sec (%.1f M ops/sec)\n", 
               time_scalar, micro_iterations / time_scalar / 1e6);
        printf("Original:  %.6f sec (%.1f M ops/sec)\n", 
               time_orig, micro_iterations / time_orig / 1e6);
        printf("Optimized: %.6f sec (%.1f M ops/sec)\n", 
               time_opt, micro_iterations / time_opt / 1e6);
        printf("Ultra:     %.6f sec (%.1f M ops/sec)\n", 
               time_ultra, micro_iterations / time_ultra / 1e6);
        
        if (time_scalar > 0) {
            double speedup_orig = time_scalar / time_orig;
            double speedup_opt = time_scalar / time_opt;
            double speedup_ultra = time_scalar / time_ultra;
            printf("Speedup vs scalar: Original %.2fx, Optimized %.2fx, Ultra %.2fx\n", 
                   speedup_orig, speedup_opt, speedup_ultra);
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
    printf("Reed-Solomon Encoder Implementation Comparison\n");
    printf("==============================================\n");
    printf("Comparing 4 implementations:\n");
    printf("  1. Scalar (pure C, no SIMD)\n");
    printf("  2. Original AVX2 (hybrid vectorization)\n");
    printf("  3. Optimized AVX2 (enhanced vectorization)\n");
    printf("  4. Ultra AVX2 (aggressive optimizations)\n");
    printf("==============================================\n");
    
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