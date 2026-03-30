#include "decoder_test_common.h"

// Test all zeros
void test_all_zeros(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 0, 0, 0, 0, 0, 0, 0, 0 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    erase_symbols(&codeword, (int[]){2, 5}, 2);
    int erasures[] = { 2, 5 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Boundary - all zeros");
}

// Test all ones (0x0F in GF(16))
void test_all_ones(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 15, 15, 15, 15, 15, 15, 15, 15 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    erase_symbols(&codeword, (int[]){0, 7}, 2);
    int erasures[] = { 0, 7 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Boundary - all maximum values (15)");
}

// Test sequential pattern
void test_sequential_pattern(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 0, 1, 2, 3, 4, 5, 6, 7 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    erase_symbols(&codeword, (int[]){1, 6}, 2);
    int erasures[] = { 1, 6 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Structured - sequential [0,1,2,3,4,5,6,7]");
}

// Test alternating pattern
void test_alternating_pattern(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 15, 0, 15, 0, 15, 0, 15, 0 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    erase_symbols(&codeword, (int[]){2, 4}, 2);
    int erasures[] = { 2, 4 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Structured - alternating [15,0,15,0,...]");
}

// Test incremental per packet
void test_incremental_pattern(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 0, 1, 2, 3, 4, 5, 6, 7 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    erase_symbols(&codeword, (int[]){0, 3}, 2);
    int erasures[] = { 0, 3 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Structured - incremental per packet");
}

// Test random data patterns
void test_random_data(rs_model *rs, rs_decode_table *table, test_results *results) {
    int num_trials = 100;
    int all_passed = 1;

    for (int trial = 0; trial < num_trials; trial++) {
        rs_poly_vector data;
        generate_random_data(&data);

        rs_poly_vector parity = { .len = 2 };
        rs_poly_vector codeword = { .len = 10 };
        rs_poly_vector decoded = { .len = 8 };

        rs_encode(rs, &data, &parity);
        create_codeword(&data, &parity, &codeword);

        // Fixed erasure pattern for consistency
        erase_symbols(&codeword, (int[]){2, 6}, 2);
        int erasures[] = { 2, 6 };
        int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

        if (result != 0 || !verify_decode(&decoded, &data)) {
            all_passed = 0;
            break;
        }
    }

    record_test(results, all_passed, "Random data - 100 random data patterns");
}

// Test single non-zero symbol
void test_single_nonzero(rs_model *rs, rs_decode_table *table, test_results *results) {
    rs_poly_vector data = { .len = 8, .val = { 0, 0, 0, 0, 0, 0, 0, 15 }};
    rs_poly_vector parity = { .len = 2 };
    rs_poly_vector codeword = { .len = 10 };
    rs_poly_vector decoded = { .len = 8 };

    rs_encode(rs, &data, &parity);
    create_codeword(&data, &parity, &codeword);

    // Erase the non-zero symbol
    erase_symbols(&codeword, (int[]){3, 7}, 2);
    int erasures[] = { 3, 7 };
    int result = rs_decode_table_lookup(table, &codeword, erasures, 2, &decoded);

    int passed = (result == 0) && verify_decode(&decoded, &data);
    record_test(results, passed, "Edge case - single non-zero symbol at position 7");
}

int run_data_tests(rs_model *rs, rs_decode_table *table) {
    printf("\n========== Data Pattern Tests ==========\n");
    test_results results;
    init_test_results(&results);

    test_all_zeros(rs, table, &results);
    test_all_ones(rs, table, &results);
    test_sequential_pattern(rs, table, &results);
    test_alternating_pattern(rs, table, &results);
    test_incremental_pattern(rs, table, &results);
    test_random_data(rs, table, &results);
    test_single_nonzero(rs, table, &results);

    print_test_summary("Data Pattern Tests", &results);
    return results.failed == 0 ? 0 : 1;
}

#ifdef STANDALONE_DATA_TEST
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

    int result = run_data_tests(rs, &table);

    free_rs_decode_table(&table);
    free_rs(rs);

    return result;
}
#endif
