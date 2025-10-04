#include "decoder_test_common.h"

// Test insufficient data (0-7 packets)
void test_insufficient_data(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Test with 3, 4, 5, 6, 7 erasures (all should fail)
    int test_cases[] = { 3, 4, 5, 6, 7 };
    int all_failed_correctly = 1;

    for (int i = 0; i < 5; i++) {
        int num_erasures = test_cases[i];
        int erasures[7] = { 0, 1, 2, 3, 4, 5, 6 };

        int result = rs_decode_erasures(rs, &codeword, erasures, num_erasures, &decoded);

        // Should fail (result != 0)
        if (result == 0) {
            all_failed_correctly = 0;
            break;
        }
    }

    record_test(results, all_failed_correctly, "Insufficient data - 3 to 7 erasures (all fail)");
}

// Test exactly threshold (should succeed)
void test_exactly_threshold(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 8, 7, 6, 5, 4, 3, 2, 1 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Exactly 2 erasures (at threshold)
    erase_symbols(&codeword, (int[]){1, 5}, 2);
    int erasures[] = { 1, 5 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Exactly threshold - 2 erasures (should succeed)");
}

// Test zero erasures (trivial case)
void test_zero_erasures(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 15, 14, 13, 12, 11, 10, 9, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // No erasures
    int erasures[] = {};
    int result = rs_decode_table_lookup(table, &codeword, erasures, 0, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Zero erasures - trivial decode");
}

// Test all combinations of 2 erasures
void test_all_combinations(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 5, 10, 15, 3, 7, 12, 1, 9 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);

    // Test all (8 choose 2) = 28 combinations
    int total_combinations = 0;
    int successful = 0;

    for (int i = 0; i < 8; i++) {
        for (int j = i + 1; j < 8; j++) {
            rs_poly_vector codeword = { .len = 10 };
            create_codeword(&data, &parity, &codeword);

            erase_symbols(&codeword, (int[]){i, j}, 2);
            int erasures[] = { i, j };
            int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

            total_combinations++;
            if (result == 0 && verify_decode(&decoded, &data)) {
                successful++;
            }
        }
    }

    int passed = (successful == total_combinations);
    record_test(results, passed, "All combinations - 28 combinations of 2 erasures");
}

// Test consistency across decoder implementations
void test_decoder_consistency(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 9, 3, 7, 1, 4, 8, 2, 6 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    erase_symbols(&codeword, (int[]){2, 5}, 2);
    int erasures[] = { 2, 5 };

    // Test all three decoder implementations
    rs_poly_vector decoded1 = { .len = 8 };
    rs_poly_vector decoded2 = { .len = 8 };
    rs_poly_vector decoded3 = { .len = 8 };

    int r1 = rs_decode_erasures(rs, &codeword, erasures, 2, &decoded1);
    int r2 = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded2);
    int r3 = neon_rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded3);

    int all_succeeded = (r1 == 0) && (r2 == 0) && (r3 == 0);
    int all_match = verify_decode(&decoded1, &data) &&
                    verify_decode(&decoded2, &data) &&
                    verify_decode(&decoded3, &data);

    // Also verify they all match each other
    int decoders_match = 1;
    for (int i = 0; i < 8; i++) {
        if (decoded1.val[i] != decoded2.val[i] || decoded2.val[i] != decoded3.val[i]) {
            decoders_match = 0;
            break;
        }
    }

    int passed = all_succeeded && all_match && decoders_match;
    record_test(results, passed, "Decoder consistency - all implementations agree");
}

// Test single erasure at each position
void test_single_erasures(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);

    int all_passed = 1;
    for (int pos = 0; pos < 8; pos++) {
        rs_poly_vector codeword = { .len = 10 };
        create_codeword(&data, &parity, &codeword);

        erase_symbols(&codeword, (int[]){pos}, 1);
        int erasures[] = { pos };
        int result = rs_decode_table_lookup(table, &codeword, erasures, 1, &decoded);

        if (result != 0 || !verify_decode(&decoded, &data)) {
            all_passed = 0;
            break;
        }
    }

    record_test(results, all_passed, "Single erasures - test each position 0-7");
}

int run_error_tests(rs_model *rs, rs_decode_table *table) {
    printf("\n========== Error Handling Tests ==========\n");
    test_results results;
    init_test_results(&results);

    test_insufficient_data(rs, table, &results);
    test_exactly_threshold(rs, table, &results);
    test_zero_erasures(rs, table, &results);
    test_all_combinations(rs, table, &results);
    test_decoder_consistency(rs, table, &results);
    test_single_erasures(rs, table, &results);

    print_test_summary("Error Handling Tests", &results);
    return results.failed == 0 ? 0 : 1;
}

#ifdef STANDALONE_ERROR_TEST
int main() {
    srand(time(NULL));

    rs_model *rs = init_rs();
    if (!rs) {
        printf("Failed to initialize RS model\n");
        return 1;
    }

    rs_decode_table table;
    if (init_rs_decode_table(rs, &table) != 0) {
        printf("Failed to initialize decoder table\n");
        free_rs(rs);
        return 1;
    }

    int result = run_error_tests(rs, &table);

    free_rs_decode_table(&table);
    free_rs(rs);

    return result;
}
#endif
