#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <arm_neon.h>

#include "../common/ejfat_rs.h"
#include "../common/ejfat_rs_decoder.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define TEST_ITERATIONS 1000000    // Number of iterations for performance tests
#define BATCH_SIZE 1000           // Batch size for batched tests
#define BLOCK_SIZE 256            // Block size for blocked tests

// ============================================================================
// TIMING UTILITIES
// ============================================================================

// Get current time in microseconds (for high-precision timing)
static inline double get_time_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e6 + (double)tv.tv_usec;
}

// ============================================================================
// ENCODER PERFORMANCE TESTS
// ============================================================================

void print_encoder_header() {
    printf("\n");
    printf("========================================================================\n");
    printf("                    ENCODER PERFORMANCE TESTS                          \n");
    printf("========================================================================\n");
    printf("Configuration:\n");
    printf("  Iterations: %d\n", TEST_ITERATIONS);
    printf("  Batch size: %d\n", BATCH_SIZE);
    printf("  Block size: %d\n", BLOCK_SIZE);
    printf("  RS Code: (10,8) - 8 data symbols, 2 parity symbols\n");
    printf("========================================================================\n\n");
}

void test_encoder_performance(rs_model *rs) {
    print_encoder_header();

    // Test data
    rs_poly_vector msg = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };

    char test_bytes[8];
    char test_parity_bytes[2];
    for (int i = 0; i < 8; i++) {
        test_bytes[i] = (msg.val[i] << 4) | msg.val[i];  // Dual nibble
    }

    clock_t start_time, end_time;
    double time_taken, data_rate, baseline_time = 0;
    int test_num = 0;

    // ========================================================================
    // 1. rs_encode (baseline)
    // ========================================================================
    test_num++;
    for (int i = 0; i < parity.len; i++) parity.val[i] = 0;

    start_time = clock();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        rs_encode(rs, &msg, &parity);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * TEST_ITERATIONS / time_taken;  // 4 bits per nibble
    baseline_time = time_taken;

    printf("%d. rs_encode (baseline matrix multiply)\n", test_num);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", TEST_ITERATIONS / time_taken);
    printf("   Speedup: 1.00x (baseline)\n\n");

    // ========================================================================
    // 2. fast_rs_encode
    // ========================================================================
    test_num++;
    for (int i = 0; i < parity.len; i++) parity.val[i] = 0;

    start_time = clock();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        fast_rs_encode(rs, &msg, &parity);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * TEST_ITERATIONS / time_taken;  // 4 bits per nibble

    printf("%d. fast_rs_encode (exp/log tables)\n", test_num);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", TEST_ITERATIONS / time_taken);
    printf("   Speedup: %.2fx\n\n", baseline_time / time_taken);

    // ========================================================================
    // 3. neon_rs_encode (single nibble SIMD)
    // ========================================================================
    test_num++;
    for (int i = 0; i < parity.len; i++) parity.val[i] = 0;

    start_time = clock();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        neon_rs_encode(rs, &msg, &parity);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * TEST_ITERATIONS / time_taken;  // 4 bits per nibble

    printf("%d. neon_rs_encode (SIMD single nibble)\n", test_num);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", TEST_ITERATIONS / time_taken);
    printf("   Speedup: %.2fx\n\n", baseline_time / time_taken);

    // ========================================================================
    // 4. neon_rs_encode_dual_nibble
    // ========================================================================
    test_num++;

    // Use 1000x iterations for more accurate timing of very fast function
    // Use microsecond precision timer for this ultra-fast function
    int dual_nibble_iterations = TEST_ITERATIONS * 1000;

    // Warmup to ensure caches are loaded
    for (int i = 0; i < 1000; i++) {
        neon_rs_encode_dual_nibble(rs, test_bytes, test_parity_bytes);
    }

    double start_usec = get_time_usec();
    for (int i = 0; i < dual_nibble_iterations; i++) {
        neon_rs_encode_dual_nibble(rs, test_bytes, test_parity_bytes);
        // Prevent compiler from optimizing away the loop
        __asm__ __volatile__("" ::: "memory");
    }
    double end_usec = get_time_usec();
    time_taken = (end_usec - start_usec) / 1e6;  // Convert microseconds to seconds

    // Ensure time_taken is not zero to avoid division by zero
    if (time_taken < 1e-9) {
        time_taken = 1e-9;  // Set minimum to 1 nanosecond
    }

    data_rate = 1/1E6 * 8 * 8 * dual_nibble_iterations / time_taken;  // 8 bits (2 nibbles) per byte

    // Calculate normalized time for fair comparison (time per TEST_ITERATIONS operations)
    double normalized_time = time_taken * TEST_ITERATIONS / dual_nibble_iterations;

    printf("%d. neon_rs_encode_dual_nibble (SIMD dual nibble)\n", test_num);
    printf("   Time: %.6f seconds (%d iterations)\n", time_taken, dual_nibble_iterations);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", dual_nibble_iterations / time_taken);
    printf("   Speedup: %.2fx\n\n", baseline_time / normalized_time);

    // ========================================================================
    // 5. neon_rs_encode_batch_blocked (single nibble batched)
    // ========================================================================
    test_num++;

    char *batch_data_vec = malloc(BATCH_SIZE * 8);
    char *batch_parity_vec = malloc(BATCH_SIZE * 2);
    char *batch_data_blocked = malloc(BATCH_SIZE * 8);
    char *batch_parity_blocked = malloc(BATCH_SIZE * 2);

    for (int i = 0; i < BATCH_SIZE; i++) {
        for (int j = 0; j < 8; j++) {
            batch_data_vec[i * 8 + j] = msg.val[j];
        }
    }
    convert_to_blocked_transposed_data(batch_data_vec, batch_data_blocked, BATCH_SIZE, BLOCK_SIZE);

    int num_batches = (TEST_ITERATIONS + BATCH_SIZE - 1) / BATCH_SIZE;

    start_time = clock();
    for (int i = 0; i < num_batches; i++) {
        neon_rs_encode_batch_blocked(rs, batch_data_blocked, batch_parity_blocked, BATCH_SIZE, BLOCK_SIZE);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * num_batches * BATCH_SIZE / time_taken;  // 4 bits per nibble

    printf("%d. neon_rs_encode_batch_blocked (batched single nibble, %d/batch)\n", test_num, BATCH_SIZE);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", num_batches * BATCH_SIZE / time_taken);
    printf("   Speedup: %.2fx\n\n", baseline_time / time_taken);

    // ========================================================================
    // 6. neon_rs_encode_dual_nibble_batch_blocked
    // ========================================================================
    test_num++;

    char *batch_bytes_vec = malloc(BATCH_SIZE * 8);
    char *batch_parity_bytes_vec = malloc(BATCH_SIZE * 2);
    char *batch_bytes_blocked = malloc(BATCH_SIZE * 8);
    char *batch_parity_bytes_blocked = malloc(BATCH_SIZE * 2);

    for (int i = 0; i < BATCH_SIZE; i++) {
        for (int j = 0; j < 8; j++) {
            batch_bytes_vec[i * 8 + j] = test_bytes[j];
        }
    }
    convert_to_blocked_transposed_data(batch_bytes_vec, batch_bytes_blocked, BATCH_SIZE, BLOCK_SIZE);

    start_time = clock();
    for (int i = 0; i < num_batches; i++) {
        neon_rs_encode_dual_nibble_batch_blocked(rs, batch_bytes_blocked, batch_parity_bytes_blocked,
                                                 BATCH_SIZE, BLOCK_SIZE);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 8 * 8 * num_batches * BATCH_SIZE / time_taken;  // 8 bits (2 nibbles) per byte

    printf("%d. neon_rs_encode_dual_nibble_batch_blocked (batched dual nibble, %d/batch)\n", test_num, BATCH_SIZE);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", num_batches * BATCH_SIZE / time_taken);
    printf("   Speedup: %.2fx\n\n", baseline_time / time_taken);

    // Cleanup
    free(batch_data_vec);
    free(batch_parity_vec);
    free(batch_data_blocked);
    free(batch_parity_blocked);
    free(batch_bytes_vec);
    free(batch_parity_bytes_vec);
    free(batch_bytes_blocked);
    free(batch_parity_bytes_blocked);

    printf("========================================================================\n\n");
}

// ============================================================================
// DECODER PERFORMANCE TESTS
// ============================================================================

void print_decoder_header(int num_erasures) {
    printf("\n");
    printf("========================================================================\n");
    printf("             DECODER PERFORMANCE TESTS (%d ERASURES)                   \n", num_erasures);
    printf("========================================================================\n");
    printf("Configuration:\n");
    printf("  Iterations: %d\n", TEST_ITERATIONS);
    printf("  Batch size: %d\n", BATCH_SIZE);
    printf("  Block size: %d\n", BLOCK_SIZE);
    printf("  RS Code: (10,8) - 8 data symbols, 2 parity symbols\n");
    printf("  Erasure pattern: %d symbols erased\n", num_erasures);
    printf("========================================================================\n\n");
}

void test_decoder_performance_with_erasures(rs_model *rs, rs_decode_table *decode_table,
                                            int num_erasures, int *erasure_positions) {
    print_decoder_header(num_erasures);

    // Create test message and encode
    rs_poly_vector msg = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &msg, &parity);

    // Create codeword with erasures
    rs_poly_vector codeword = { .len = 10 };
    for (int i = 0; i < 8; i++) {
        codeword.val[i] = msg.val[i];
    }
    for (int i = 0; i < 2; i++) {
        codeword.val[8 + i] = parity.val[i];
    }

    // Apply erasures
    for (int i = 0; i < num_erasures; i++) {
        codeword.val[erasure_positions[i]] = 0;
    }

    printf("Erased positions: ");
    for (int i = 0; i < num_erasures; i++) {
        printf("%d ", erasure_positions[i]);
    }
    printf("\n\n");

    clock_t start_time, end_time;
    double time_taken, data_rate, baseline_time = 0;
    int test_num = 0;
    int correct;

    // ========================================================================
    // 1. rs_decode_erasures (baseline)
    // ========================================================================
    test_num++;

    start_time = clock();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        rs_decode_erasures(rs, &codeword, erasure_positions, num_erasures, &decoded);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * TEST_ITERATIONS / time_taken;  // 4 bits per nibble
    baseline_time = time_taken;

    // Verify correctness
    correct = 1;
    for (int i = 0; i < 8; i++) {
        if (decoded.val[i] != msg.val[i]) {
            correct = 0;
            break;
        }
    }

    printf("%d. rs_decode_erasures (baseline Gauss-Jordan)\n", test_num);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", TEST_ITERATIONS / time_taken);
    printf("   Speedup: 1.00x (baseline)\n");
    printf("   Result: %s\n\n", correct ? "CORRECT" : "INCORRECT");

    // ========================================================================
    // 2. rs_decode_table_lookup
    // ========================================================================
    test_num++;

    start_time = clock();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        rs_decode_table_lookup(decode_table, &codeword, erasure_positions, num_erasures, &decoded);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * TEST_ITERATIONS / time_taken;  // 4 bits per nibble

    correct = 1;
    for (int i = 0; i < 8; i++) {
        if (decoded.val[i] != msg.val[i]) {
            correct = 0;
            break;
        }
    }

    printf("%d. rs_decode_table_lookup (precomputed matrices)\n", test_num);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", TEST_ITERATIONS / time_taken);
    printf("   Speedup: %.2fx\n", baseline_time / time_taken);
    printf("   Result: %s\n\n", correct ? "CORRECT" : "INCORRECT");

    // ========================================================================
    // 3. neon_rs_decode_table_lookup
    // ========================================================================
    test_num++;

    start_time = clock();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        neon_rs_decode_table_lookup(decode_table, &codeword, erasure_positions, num_erasures, &decoded);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * TEST_ITERATIONS / time_taken;  // 4 bits per nibble

    correct = 1;
    for (int i = 0; i < 8; i++) {
        if (decoded.val[i] != msg.val[i]) {
            correct = 0;
            break;
        }
    }

    printf("%d. neon_rs_decode_table_lookup (SIMD table lookup)\n", test_num);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", TEST_ITERATIONS / time_taken);
    printf("   Speedup: %.2fx\n", baseline_time / time_taken);
    printf("   Result: %s\n\n", correct ? "CORRECT" : "INCORRECT");

    // ========================================================================
    // 4. neon_rs_decode_table_lookup_v2
    // ========================================================================
    test_num++;

    start_time = clock();
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        neon_rs_decode_table_lookup_v2(decode_table, &codeword, erasure_positions, num_erasures, &decoded);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * TEST_ITERATIONS / time_taken;  // 4 bits per nibble

    correct = 1;
    for (int i = 0; i < 8; i++) {
        if (decoded.val[i] != msg.val[i]) {
            correct = 0;
            break;
        }
    }

    printf("%d. neon_rs_decode_table_lookup_v2 (optimized SIMD)\n", test_num);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", TEST_ITERATIONS / time_taken);
    printf("   Speedup: %.2fx\n", baseline_time / time_taken);
    printf("   Result: %s\n\n", correct ? "CORRECT" : "INCORRECT");

    // ========================================================================
    // 5. neon_rs_decode_batch_blocked (single nibble batched)
    // ========================================================================
    test_num++;

    char *batch_data_vec = malloc(BATCH_SIZE * 8);
    char *batch_parity_vec = malloc(BATCH_SIZE * 2);
    char *batch_data_blocked = malloc(BATCH_SIZE * 8);
    char *batch_parity_blocked = malloc(BATCH_SIZE * 2);
    char *batch_output_vec = malloc(BATCH_SIZE * 8);

    // Fill batch with codeword data
    for (int i = 0; i < BATCH_SIZE; i++) {
        for (int j = 0; j < 8; j++) {
            batch_data_vec[i * 8 + j] = codeword.val[j];
        }
        for (int j = 0; j < 2; j++) {
            batch_parity_vec[i * 2 + j] = codeword.val[8 + j];
        }
    }

    convert_to_blocked_transposed_data(batch_data_vec, batch_data_blocked, BATCH_SIZE, BLOCK_SIZE);
    convert_to_blocked_transposed_parity(batch_parity_vec, batch_parity_blocked, BATCH_SIZE, BLOCK_SIZE);

    int num_batches = (TEST_ITERATIONS + BATCH_SIZE - 1) / BATCH_SIZE;

    start_time = clock();
    for (int i = 0; i < num_batches; i++) {
        // Reset data for each iteration
        convert_to_blocked_transposed_data(batch_data_vec, batch_data_blocked, BATCH_SIZE, BLOCK_SIZE);
        neon_rs_decode_batch_blocked(decode_table, batch_data_blocked, batch_parity_blocked,
                                     erasure_positions, num_erasures, BATCH_SIZE, BLOCK_SIZE);
    }
    end_time = clock();
    time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    data_rate = 1/1E6 * 4 * 8 * num_batches * BATCH_SIZE / time_taken;  // 4 bits per nibble

    // Verify correctness
    convert_from_blocked_transposed_data(batch_data_blocked, batch_output_vec, BATCH_SIZE, BLOCK_SIZE);
    correct = 1;
    for (int i = 0; i < 8; i++) {
        if (batch_output_vec[i] != msg.val[i]) {
            correct = 0;
            break;
        }
    }

    printf("%d. neon_rs_decode_batch_blocked (batched single nibble, %d/batch)\n", test_num, BATCH_SIZE);
    printf("   Time: %.3f seconds\n", time_taken);
    printf("   Throughput: %.1f Mbps\n", data_rate);
    printf("   Operations/sec: %.0f\n", num_batches * BATCH_SIZE / time_taken);
    printf("   Speedup: %.2fx\n", baseline_time / time_taken);
    printf("   Result: %s\n\n", correct ? "CORRECT" : "INCORRECT");

    // Cleanup
    free(batch_data_vec);
    free(batch_parity_vec);
    free(batch_data_blocked);
    free(batch_parity_blocked);
    free(batch_output_vec);

    printf("========================================================================\n\n");
}

void test_decoder_performance(rs_model *rs, rs_decode_table *decode_table) {
    // Test with different 2 data symbol erasure patterns
    // Note: Only testing 2 data erasures for meaningful performance comparison

    // Test 1: Two erasures at positions 2 and 6 (middle positions)
    printf("\n>>> SCENARIO 1: Two data erasures at positions 2 and 6\n");
    int erasures_2a[] = { 2, 6 };
    test_decoder_performance_with_erasures(rs, decode_table, 2, erasures_2a);

    // Test 2: Two consecutive erasures at positions 0 and 1 (beginning)
    printf("\n>>> SCENARIO 2: Two consecutive data erasures at positions 0 and 1\n");
    int erasures_2b[] = { 0, 1 };
    test_decoder_performance_with_erasures(rs, decode_table, 2, erasures_2b);

    // Test 3: Two erasures at end positions 6 and 7 (last two data symbols)
    printf("\n>>> SCENARIO 3: Two data erasures at positions 6 and 7 (last two data symbols)\n");
    int erasures_2c[] = { 6, 7 };
    test_decoder_performance_with_erasures(rs, decode_table, 2, erasures_2c);
}

// ============================================================================
// MAIN TEST DRIVER
// ============================================================================

int main(int argc, char *argv[]) {
    printf("\n");
    printf("************************************************************************\n");
    printf("*                                                                      *\n");
    printf("*         COMPREHENSIVE RS-FEC PERFORMANCE TEST SUITE                 *\n");
    printf("*                                                                      *\n");
    printf("************************************************************************\n");

    // Initialize RS model
    rs_model *rs = init_rs();
    if (!rs) {
        printf("ERROR: Failed to initialize RS model\n");
        return 1;
    }

    // Initialize decoder table
    rs_decode_table decode_table;
    if (init_rs_decode_table(rs, &decode_table) != 0) {
        printf("ERROR: Failed to initialize decoder table\n");
        free_rs(rs);
        return 1;
    }

    // Determine which tests to run
    int run_encoder = 1;
    int run_decoder = 1;

    if (argc > 1) {
        if (strcmp(argv[1], "encoder") == 0) {
            run_decoder = 0;
        } else if (strcmp(argv[1], "decoder") == 0) {
            run_encoder = 0;
        }
    }

    // Run encoder tests
    if (run_encoder) {
        test_encoder_performance(rs);
    }

    // Run decoder tests
    if (run_decoder) {
        test_decoder_performance(rs, &decode_table);
    }

    // Cleanup
    free_rs_decode_table(&decode_table);
    free_rs(rs);

    printf("\n");
    printf("************************************************************************\n");
    printf("*                     ALL TESTS COMPLETED                              *\n");
    printf("************************************************************************\n\n");

    return 0;
}
