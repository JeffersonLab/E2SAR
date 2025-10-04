// Single compilation unit that includes all test modules
#include "decoder_test_common.h"

// Include all test module implementations
#include "test_decoder_basic.c"
#include "test_decoder_patterns.c"
#include "test_decoder_data.c"
#include "test_decoder_errors.c"
#include "test_decoder_roundtrip.c"

// Main test runner
#include <string.h>

void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  --all              Run all test suites (default)\n");
    printf("  --basic            Run basic recovery tests\n");
    printf("  --patterns         Run loss pattern tests\n");
    printf("  --data             Run data pattern tests\n");
    printf("  --errors           Run error handling tests\n");
    printf("  --roundtrip        Run encoder/decoder round-trip tests\n");
    printf("  --help             Show this help message\n");
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    int run_all = 1;
    int run_basic = 0;
    int run_patterns = 0;
    int run_data = 0;
    int run_errors = 0;
    int run_roundtrip = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--all") == 0) {
            run_all = 1;
        } else if (strcmp(argv[i], "--basic") == 0) {
            run_basic = 1;
            run_all = 0;
        } else if (strcmp(argv[i], "--patterns") == 0) {
            run_patterns = 1;
            run_all = 0;
        } else if (strcmp(argv[i], "--data") == 0) {
            run_data = 1;
            run_all = 0;
        } else if (strcmp(argv[i], "--errors") == 0) {
            run_errors = 1;
            run_all = 0;
        } else if (strcmp(argv[i], "--roundtrip") == 0) {
            run_roundtrip = 1;
            run_all = 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // If no specific tests selected, run all
    if (run_all) {
        run_basic = run_patterns = run_data = run_errors = run_roundtrip = 1;
    }

    printf("=========================================\n");
    printf("  RS Decoder Comprehensive Test Suite\n");
    printf("=========================================\n");

    srand(time(NULL));

    // Initialize RS model
    rs_model *rs = init_rs();
    if (!rs) {
        printf("Failed to initialize RS model\n");
        return 1;
    }

    // Initialize decoder table
    rs_decode_table table;
    if (init_rs_decode_table(rs, &table) != 0) {
        printf("Failed to initialize decoder table\n");
        free_rs(rs);
        return 1;
    }

    // Run selected test suites
    int total_failures = 0;

    if (run_basic) {
        total_failures += run_basic_tests(rs, &table);
    }

    if (run_patterns) {
        total_failures += run_pattern_tests(rs, &table);
    }

    if (run_data) {
        total_failures += run_data_tests(rs, &table);
    }

    if (run_errors) {
        total_failures += run_error_tests(rs, &table);
    }

    if (run_roundtrip) {
        total_failures += run_roundtrip_tests(rs, &table);
    }

    // Clean up
    free_rs_decode_table(&table);
    free_rs(rs);

    // Print overall results
    printf("\n=========================================\n");
    printf("  Overall Test Results\n");
    printf("=========================================\n");
    if (total_failures == 0) {
        printf("ALL TESTS PASSED!\n");
    } else {
        printf("SOME TESTS FAILED (check output above)\n");
    }
    printf("=========================================\n\n");

    return total_failures > 0 ? 1 : 0;
}
