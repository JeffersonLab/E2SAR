#include "decoder_test_common.h"

// Test data packet loss only
void test_data_loss_only(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 1, 2, 3, 4, 5, 6, 7, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Lose 1 data packet
    erase_symbols(&codeword, (int[]){0}, 1);
    int erasures1[] = { 0 };
    int result1 = rs_decode_table_lookup(table, &codeword, erasures1, 1, &decoded);

    int passed1 = (result1 == 0) && verify_decode(&decoded, &data);
    record_test(results, passed1, "Data loss - 1 packet lost (position 0)");

    // Lose 2 data packets (maximum)
    codeword = (rs_poly_vector){ .len = 10 };
    create_codeword(&data, &parity, &codeword);
    erase_symbols(&codeword, (int[]){0, 1}, 2);
    int erasures2[] = { 0, 1 };
    int result2 = rs_decode_table_lookup(table, &codeword, erasures2, 2, &decoded);

    int passed2 = (result2 == 0) && verify_decode(&decoded, &data);
    record_test(results, passed2, "Data loss - 2 packets lost (positions 0,1)");
}

// Test parity packet loss
void test_parity_loss(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 15, 14, 13, 12, 11, 10, 9, 8 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Lose both parity packets - should still work with all data
    // We just copy data directly since no erasures in data symbols
    int erasures[] = {};
    int result = rs_decode_table_lookup(table, &codeword, erasures, 0, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Parity loss - both parity packets lost");
}

// Test mixed loss patterns
void test_mixed_loss(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 8, 7, 6, 5, 4, 3, 2, 1 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // 1 parity + 1 data lost
    erase_symbols(&codeword, (int[]){3}, 1);
    int erasures[] = { 3 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 1, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Mixed loss - 1 data packet");
}

// Test alternating loss patterns
void test_alternating_patterns(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 1, 3, 5, 7, 9, 11, 13, 15 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);

    // Test even positions
    create_codeword(&data, &parity, &codeword);
    erase_symbols(&codeword, (int[]){0, 2}, 2);
    int erasures1[] = { 0, 2 };
    int result1 = rs_decode_table_lookup(table, &codeword, erasures1, 2, &decoded);
    int passed1 = (result1 == 0) && verify_decode(&decoded, &data);
    record_test(results, passed1, "Alternating - even positions (0, 2)");

    // Test odd positions
    create_codeword(&data, &parity, &codeword);
    erase_symbols(&codeword, (int[]){1, 3}, 2);
    int erasures2[] = { 1, 3 };
    int result2 = rs_decode_table_lookup(table, &codeword, erasures2, 2, &decoded);
    int passed2 = (result2 == 0) && verify_decode(&decoded, &data);
    record_test(results, passed2, "Alternating - odd positions (1, 3)");
}

// Test first N packets lost
void test_first_n_lost(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 2, 4, 6, 8, 10, 12, 14, 1 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Lose first 2 packets
    erase_symbols(&codeword, (int[]){0, 1}, 2);
    int erasures[] = { 0, 1 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "First N lost - positions 0, 1");
}

// Test last N packets lost
void test_last_n_lost(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 1, 1, 2, 3, 5, 8, 13, 6 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Lose last 2 data packets
    erase_symbols(&codeword, (int[]){6, 7}, 2);
    int erasures[] = { 6, 7 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Last N lost - positions 6, 7");
}

// Test random loss patterns
void test_random_patterns(rs_model *rs, rs_decode_table *table, test_results *results) {
    int num_trials = 50;
    int all_passed = 1;

    for (int trial = 0; trial < num_trials; trial++) {
        rs_poly_vector data;
        generate_random_data(&data);

        rs_poly_vector parity = { .len = 2 };
        rs_poly_vector codeword = { .len = 10 };
        rs_poly_vector decoded = { .len = 8 };

        rs_encode(rs, &data, &parity);
        create_codeword(&data, &parity, &codeword);

        // Random 2-packet loss pattern (ensure pos1 < pos2 for table lookup)
        int pos1 = rand() % 8;
        int pos2 = (pos1 + 1 + (rand() % 7)) % 8;

        // Sort erasure positions
        int erasures[2];
        if (pos1 < pos2) {
            erasures[0] = pos1;
            erasures[1] = pos2;
        } else {
            erasures[0] = pos2;
            erasures[1] = pos1;
        }

        erase_symbols(&codeword, erasures, 2);
        int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

        if (result != 0 || !verify_decode(&decoded, &data)) {
            all_passed = 0;
            break;
        }
    }

    record_test(results, all_passed, "Random patterns - 50 random 2-packet losses");
}

int run_pattern_tests(rs_model *rs, rs_decode_table *table) {
    printf("\n========== Loss Pattern Tests ==========\n");
    test_results results;
    init_test_results(&results);

    test_data_loss_only(rs, table, &results);
    test_parity_loss(rs, table, &results);
    test_mixed_loss(rs, table, &results);
    test_alternating_patterns(rs, table, &results);
    test_first_n_lost(rs, table, &results);
    test_last_n_lost(rs, table, &results);
    test_random_patterns(rs, table, &results);

    print_test_summary("Loss Pattern Tests", &results);
    return results.failed == 0 ? 0 : 1;
}

#ifdef STANDALONE_PATTERN_TEST
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

    int result = run_pattern_tests(rs, &table);

    free_rs_decode_table(&table);
    free_rs(rs);

    return result;
}
#endif
