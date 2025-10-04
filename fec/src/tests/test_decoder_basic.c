#include "decoder_test_common.h"

// Test 1: No loss scenario
void test_no_loss(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    // Encode
    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Decode with no erasures
    int erasures[] = {};
    int result = rs_decode_table_lookup(table, &codeword, erasures, 0, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "No loss - all 10 packets received");
}

// Test 2: Recovery threshold - exactly 8 packets
void test_minimum_threshold(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 15, 14, 13, 12, 11, 10, 9, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    // Encode
    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Erase 2 packets (keep exactly 8)
    erase_symbols(&codeword, (int[]){0, 1}, 2);
    int erasures[] = { 0, 1 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Minimum threshold - exactly 8 packets");
}

// Test 3: Above minimum - 9 packets
void test_above_minimum(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 5, 5, 5, 5, 5, 5, 5, 5 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    // Encode
    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Erase 1 packet (keep 9)
    erase_symbols(&codeword, (int[]){7}, 1);
    int erasures[] = { 7 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 1, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Above minimum - 9 packets available");
}

// Test 4: Below minimum - 7 packets (should fail)
void test_below_minimum(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    // Encode
    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Try to erase 3 packets (should fail)
    erase_symbols(&codeword, (int[]){0, 3, 6}, 3);
    int erasures[] = { 0, 3, 6 };
    int result = rs_decode_erasures(rs, &codeword, erasures, 3, &decoded);

    // Should fail (result != 0)
    int passed = (result != 0);
    record_test(results, passed, "Below minimum - 7 packets (should fail)");
}

// Test 5: Maximum recoverable loss (2 packets)
void test_maximum_loss(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 9, 3, 7, 1, 4, 8, 2, 6 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    // Encode
    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Test multiple 2-packet loss combinations
    int test_patterns[5][2] = {
        {0, 1}, {2, 5}, {3, 7}, {0, 7}, {4, 6}
    };

    int all_passed = 1;
    for (int i = 0; i < 5; i++) {
        rs_poly_vector test_codeword = codeword;
        erase_symbols(&test_codeword, test_patterns[i], 2);
        int result = rs_decode_table_lookup(table, &test_codeword, test_patterns[i], 2, &decoded);

        if (result != 0 || !verify_decode(&decoded, &data)) {
            all_passed = 0;
            break;
        }
    }

    record_test(results, all_passed, "Maximum loss - 2 packets (multiple patterns)");
}

int run_basic_tests(rs_model *rs, rs_decode_table *table) {
    printf("\n========== Basic Recovery Tests ==========\n");
    test_results results;
    init_test_results(&results);

    test_no_loss(rs, table, &results);
    test_minimum_threshold(rs, table, &results);
    test_above_minimum(rs, table, &results);
    test_below_minimum(rs, table, &results);
    test_maximum_loss(rs, table, &results);

    print_test_summary("Basic Recovery Tests", &results);
    return results.failed == 0 ? 0 : 1;
}

#ifdef STANDALONE_BASIC_TEST
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

    int result = run_basic_tests(rs, &table);

    free_rs_decode_table(&table);
    free_rs(rs);

    return result;
}
#endif
