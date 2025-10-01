#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "./ejfat_rs.h"
#include "./ejfat_rs_decoder.h"

void test_decoder() {
    printf("\n=============== Testing RS Decoder ===============\n");
    
    // Initialize RS model
    rs_model *rs = init_rs();
    if (!rs) {
        printf("Failed to initialize RS model\n");
        return;
    }
    
    // Initialize decoder table
    rs_decode_table decode_table;
    if (init_rs_decode_table(rs, &decode_table) != 0) {
        printf("Failed to initialize decoder table\n");
        free_rs(rs);
        return;
    }
    
    // Create test message
    rs_poly_vector msg = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };
    
    printf("Original message: ");
    print_rs_poly_vector(&msg);
    
    // Encode the message
    rs_encode(rs, &msg, &parity);
    printf("Parity symbols: ");
    print_rs_poly_vector(&parity);
    
    // Create full codeword (data + parity)
    rs_poly_vector codeword = { .len = 10 };
    for (int i = 0; i < 8; i++) {
        codeword.val[i] = msg.val[i];
    }
    for (int i = 0; i < 2; i++) {
        codeword.val[8 + i] = parity.val[i];
    }
    
    printf("Full codeword: ");
    print_rs_poly_vector(&codeword);
    
    // Test 1: No erasures
    printf("\n--- Test 1: No erasures ---\n");
    rs_poly_vector decoded1 = { .len = 8 };
    int erasures1[] = {};
    
    if (rs_decode_erasures(rs, &codeword, erasures1, 0, &decoded1) == 0) {
        printf("Decoded (no erasures): ");
        print_rs_poly_vector(&decoded1);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded1.val[i] != msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("Decoding %s\n", correct ? "PASSED" : "FAILED");
    }
    
    // Test 2: Single erasure
    printf("\n--- Test 2: Single erasure (position 3) ---\n");
    rs_poly_vector corrupted2 = codeword;
    corrupted2.val[3] = 0;  // Erase symbol at position 3
    
    printf("Corrupted codeword: ");
    print_rs_poly_vector(&corrupted2);
    
    rs_poly_vector decoded2 = { .len = 8 };
    int erasures2[] = { 3 };
    
    if (rs_decode_erasures(rs, &corrupted2, erasures2, 1, &decoded2) == 0) {
        printf("Decoded (1 erasure): ");
        print_rs_poly_vector(&decoded2);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded2.val[i] != msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("Decoding %s\n", correct ? "PASSED" : "FAILED");
    }
    
    // Test 3: Two erasures (maximum for RS(10,8))
    printf("\n--- Test 3: Two erasures (positions 1, 5) ---\n");
    rs_poly_vector corrupted3 = codeword;
    corrupted3.val[1] = 0;  // Erase symbol at position 1
    corrupted3.val[5] = 0;  // Erase symbol at position 5
    
    printf("Corrupted codeword: ");
    print_rs_poly_vector(&corrupted3);
    
    rs_poly_vector decoded3 = { .len = 8 };
    int erasures3[] = { 1, 5 };
    
    if (rs_decode_erasures(rs, &corrupted3, erasures3, 2, &decoded3) == 0) {
        printf("Decoded (2 erasures): ");
        print_rs_poly_vector(&decoded3);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded3.val[i] != msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("Decoding %s\n", correct ? "PASSED" : "FAILED");
    }
    
    // Test 4: Test substitute method
    printf("\n--- Test 4: Substitute method (1 erasure at position 2) ---\n");
    rs_poly_vector corrupted4 = codeword;
    corrupted4.val[2] = 0;  // Erase symbol at position 2
    
    printf("Corrupted codeword: ");
    print_rs_poly_vector(&corrupted4);
    
    rs_poly_vector decoded4 = { .len = 8 };
    int erasures4[] = { 2 };
    
    if (rs_decode_substitute(rs, &corrupted4, erasures4, 1, &decoded4) == 0) {
        printf("Decoded (substitute): ");
        print_rs_poly_vector(&decoded4);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded4.val[i] != msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("Decoding %s\n", correct ? "PASSED" : "FAILED");
    }
    
    // Test 5: Too many erasures (should fail)
    printf("\n--- Test 5: Too many erasures (3 erasures - should fail) ---\n");
    rs_poly_vector corrupted5 = codeword;
    corrupted5.val[0] = 0;
    corrupted5.val[3] = 0;
    corrupted5.val[6] = 0;
    
    rs_poly_vector decoded5 = { .len = 8 };
    int erasures5[] = { 0, 3, 6 };
    
    int result = rs_decode_erasures(rs, &corrupted5, erasures5, 3, &decoded5);
    printf("Decoding with 3 erasures: %s (expected to fail)\n", 
           result == 0 ? "UNEXPECTEDLY PASSED" : "FAILED as expected");
    
    // Test 6: Table-based decoder
    printf("\n--- Test 6: Table-based decoder (1 erasure at position 4) ---\n");
    rs_poly_vector corrupted6 = codeword;
    corrupted6.val[4] = 0;  // Erase symbol at position 4
    
    printf("Corrupted codeword: ");
    print_rs_poly_vector(&corrupted6);
    
    rs_poly_vector decoded6 = { .len = 8 };
    int erasures6[] = { 4 };
    
    if (rs_decode_table_lookup(&decode_table, &corrupted6, erasures6, 1, &decoded6) == 0) {
        printf("Decoded (table lookup): ");
        print_rs_poly_vector(&decoded6);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded6.val[i] != msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("Table-based decoding %s\n", correct ? "PASSED" : "FAILED");
    }
    
    // Test 7: Table-based decoder with 2 erasures
    printf("\n--- Test 7: Table-based decoder (2 erasures at positions 0, 7) ---\n");
    rs_poly_vector corrupted7 = codeword;
    corrupted7.val[0] = 0;  // Erase symbols at positions 0 and 7
    corrupted7.val[7] = 0;
    
    printf("Corrupted codeword: ");
    print_rs_poly_vector(&corrupted7);
    
    rs_poly_vector decoded7 = { .len = 8 };
    int erasures7[] = { 0, 7 };
    
    if (rs_decode_table_lookup(&decode_table, &corrupted7, erasures7, 2, &decoded7) == 0) {
        printf("Decoded (table lookup 2 erasures): ");
        print_rs_poly_vector(&decoded7);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded7.val[i] != msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("Table-based decoding (2 erasures) %s\n", correct ? "PASSED" : "FAILED");
    }

    // Test 8: NEON table-based decoder (1 erasure)
    printf("\n--- Test 8: NEON table-based decoder (1 erasure at position 3) ---\n");
    rs_poly_vector corrupted8 = codeword;
    corrupted8.val[3] = 0;  // Erase symbol at position 3
    
    printf("Corrupted codeword: ");
    print_rs_poly_vector(&corrupted8);
    
    rs_poly_vector decoded8 = { .len = 8 };
    int erasures8[] = { 3 };
    
    if (neon_rs_decode_table_lookup(&decode_table, &corrupted8, erasures8, 1, &decoded8) == 0) {
        printf("Decoded (NEON table lookup): ");
        print_rs_poly_vector(&decoded8);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded8.val[i] != msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("NEON table-based decoding %s\n", correct ? "PASSED" : "FAILED");
    }

    // Test 9: NEON table-based decoder (2 erasures)
    printf("\n--- Test 9: NEON table-based decoder (2 erasures at positions 2, 6) ---\n");
    rs_poly_vector corrupted9 = codeword;
    corrupted9.val[2] = 0;  // Erase symbols at positions 2 and 6
    corrupted9.val[6] = 0;
    
    printf("Corrupted codeword: ");
    print_rs_poly_vector(&corrupted9);
    
    rs_poly_vector decoded9 = { .len = 8 };
    int erasures9[] = { 2, 6 };
    
    if (neon_rs_decode_table_lookup(&decode_table, &corrupted9, erasures9, 2, &decoded9) == 0) {
        printf("Decoded (NEON table lookup 2 erasures): ");
        print_rs_poly_vector(&decoded9);
        
        // Verify correctness
        int correct = 1;
        for (int i = 0; i < 8; i++) {
            if (decoded9.val[i] != msg.val[i]) {
                correct = 0;
                break;
            }
        }
        printf("NEON table-based decoding (2 erasures) %s\n", correct ? "PASSED" : "FAILED");
    }

    // Clean up
    free_rs_decode_table(&decode_table);
    free_rs(rs);
    printf("\n=============== Decoder Tests Complete ===============\n");
}

void test_encode_decode_performance() {
    printf("\n=============== Encode/Decode Performance Test ===============\n");
    
    rs_model *rs = init_rs();
    if (!rs) {
        printf("Failed to initialize RS model\n");
        return;
    }
    
    // Initialize decoder table
    rs_decode_table decode_table;
    if (init_rs_decode_table(rs, &decode_table) != 0) {
        printf("Failed to initialize decoder table\n");
        free_rs(rs);
        return;
    }
    
    int test_iterations = 100000;  // Increased for better measurement
    int num_erasures = 2;
    
    // Create test message
    rs_poly_vector msg = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector decoded = { .len = 8 };
    
    // Encode once to get parity
    rs_encode(rs, &msg, &parity);
    
    // Create corrupted message with 2 erasures
    rs_poly_vector corrupted = { .len = 10 };
    for (int i = 0; i < 8; i++) {
        corrupted.val[i] = msg.val[i];
    }
    for (int i = 0; i < 2; i++) {
        corrupted.val[8 + i] = parity.val[i];
    }
    corrupted.val[2] = 0;  // Erase positions 2 and 6
    corrupted.val[6] = 0;
    int erasures[] = { 2, 6 };
    
    printf("Performance comparison with %d iterations:\n", test_iterations);
    
    // Test 1: General erasure decoder
    clock_t start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        rs_decode_erasures(rs, &corrupted, erasures, num_erasures, &decoded);
    }
    clock_t end_time = clock();
    double time_general = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("General erasure decoder: %f seconds (%.1f ops/sec)\n", 
           time_general, test_iterations / time_general);
    
    // Test 2: Table-based decoder
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        rs_decode_table_lookup(&decode_table, &corrupted, erasures, num_erasures, &decoded);
    }
    end_time = clock();
    double time_table = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("Table-based decoder:     %f seconds (%.1f ops/sec)\n", 
           time_table, test_iterations / time_table);
    
    // Test 3: NEON table-based decoder
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        neon_rs_decode_table_lookup(&decode_table, &corrupted, erasures, num_erasures, &decoded);
    }
    end_time = clock();
    double time_neon = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("NEON table-based decoder: %f seconds (%.1f ops/sec)\n", 
           time_neon, test_iterations / time_neon);
    
    // Test 4: NEON v2 decoder
    start_time = clock();
    for (int i = 0; i < test_iterations; i++) {
        neon_rs_decode_table_lookup_v2(&decode_table, &corrupted, erasures, num_erasures, &decoded);
    }
    end_time = clock();
    double time_neon_v2 = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("NEON v2 decoder:         %f seconds (%.1f ops/sec)\n", 
           time_neon_v2, test_iterations / time_neon_v2);
    
    // Performance comparisons
    printf("\nPerformance improvements:\n");
    printf("Table vs General:  %.2fx faster\n", time_general / time_table);
    printf("NEON vs Table:     %.2fx faster\n", time_table / time_neon);
    printf("NEON vs General:   %.2fx faster\n", time_general / time_neon);
    printf("NEON v2 vs NEON:   %.2fx faster\n", time_neon / time_neon_v2);
    printf("NEON v2 vs General: %.2fx faster\n", time_general / time_neon_v2);
    
    // Verify final result
    int correct = 1;
    for (int i = 0; i < 8; i++) {
        if (decoded.val[i] != msg.val[i]) {
            correct = 0;
            break;
        }
    }
    printf("\nFinal decode result: %s\n", correct ? "CORRECT" : "INCORRECT");
    
    // Throughput calculations (assuming 8-byte packets)
    double throughput_general = (test_iterations * 8.0) / time_general / 1e6;  // MB/s
    double throughput_neon = (test_iterations * 8.0) / time_neon / 1e6;
    double throughput_neon_v2 = (test_iterations * 8.0) / time_neon_v2 / 1e6;
    
    printf("\nData throughput (8-byte packets):\n");
    printf("General decoder: %.1f MB/s\n", throughput_general);
    printf("NEON decoder:    %.1f MB/s\n", throughput_neon);
    printf("NEON v2 decoder: %.1f MB/s\n", throughput_neon_v2);
    
    free_rs_decode_table(&decode_table);
    free_rs(rs);
    printf("=============== Performance Test Complete ===============\n");
}

void test_gf_operations() {
    printf("\n=============== Testing GF Operations ===============\n");
    
    // Test GF division
    printf("Testing GF division:\n");
    printf("gf_div(14, 7) = %d (expected: 2)\n", gf_div(14, 7));
    printf("gf_div(10, 5) = %d (expected: 2)\n", gf_div(10, 5));
    printf("gf_div(15, 3) = %d (expected: 5)\n", gf_div(15, 3));
    
    // Test multiplication and division are inverses
    printf("\nTesting mul/div inverse property:\n");
    for (int a = 1; a < 16; a++) {
        for (int b = 1; b < 16; b++) {
            char product = gf_mul(a, b);
            char quotient = gf_div(product, b);
            if (quotient != a) {
                printf("ERROR: gf_mul(%d,%d)=%d, gf_div(%d,%d)=%d != %d\n", 
                       a, b, product, product, b, quotient, a);
            }
        }
    }
    printf("Multiplication/division inverse test completed\n");
    
    printf("=============== GF Operations Test Complete ===============\n");
}

int main() {
    printf("Reed-Solomon Encoder/Decoder Test Program\n");
    printf("==========================================\n");
    
    // Test GF operations first
    test_gf_operations();
    
    // Test decoder functionality
    test_decoder();
    
    // Test performance
    test_encode_decode_performance();
    
    return 0;
}