#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arm_neon.h>

// Debug logging control - set to 1 to enable detailed debug output
#define DEBUG_DUAL_NIBBLE 1

#if DEBUG_DUAL_NIBBLE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#define DEBUG_PRINT_ARRAY(label, arr, len) do { \
    printf("%s: ", label); \
    for (int _i = 0; _i < (len); _i++) printf("%X ", (arr)[_i]); \
    printf("\n"); \
} while(0)
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINT_ARRAY(label, arr, len)
#endif

// Include main RS header which has all the core types and tables
#include "../common/ejfat_rs.h"

// Forward declare the decoder table structures from ejfat_rs_neon_decoder.h
typedef struct {
  int erasure_pattern[2];
  int num_erasures;
  char inv_matrix[8][8];
  int valid;
} rs_decode_table_entry;

typedef struct {
  rs_decode_table_entry *entries;
  int size;
  int capacity;
} rs_decode_table;

// Inline implementation of the NEON GF multiplication vector function
static inline uint8x8_t neon_gf_mul_vec(uint8x8_t a, uint8x8_t b,
                                        uint8x8x2_t exp_table, uint8x8x2_t log_table) {
  uint8x8_t zero_vec = vdup_n_u8(0);
  uint8x8_t a_zero_mask = vceq_u8(a, zero_vec);
  uint8x8_t b_zero_mask = vceq_u8(b, zero_vec);
  uint8x8_t zero_mask = vorr_u8(a_zero_mask, b_zero_mask);

  uint8x8_t a_exp = vtbl2_u8(exp_table, a);
  uint8x8_t b_exp = vtbl2_u8(exp_table, b);

  uint8x8_t sum_exp = vadd_u8(a_exp, b_exp);
  uint8x8_t mod = vdup_n_u8(15);
  uint8x8_t mask = vcge_u8(sum_exp, mod);
  uint8x8_t mod15 = vand_u8(mod, mask);
  sum_exp = vsub_u8(sum_exp, mod15);

  uint8x8_t result = vtbl2_u8(log_table, sum_exp);
  result = vbic_u8(result, zero_mask);

  return result;
}

// Forward declare the dual-nibble encoder function
void neon_rs_encode_dual_nibble(rs_model *rs, char *data_bytes, char *parity_bytes);

// Forward declare the dual-nibble decoder function
int neon_rs_decode_dual_nibble(rs_decode_table *table, char *received_bytes,
                                int *erasure_locations, int num_erasures,
                                char *decoded_bytes);

// Utility function to print bytes in hex
void print_bytes_hex(const char *label, char *bytes, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) {
        printf("%02X ", (unsigned char)bytes[i]);
    }
    printf("\n");
}

// Utility function to print bytes as nibbles
void print_bytes_nibbles(const char *label, char *bytes, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) {
        printf("[%X %X] ", (bytes[i] >> 4) & 0x0F, bytes[i] & 0x0F);
    }
    printf("\n");
}

// GF(16) division (not in ejfat_rs.h)
char gf_div_local(char a, char b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    char exp_a = _ejfat_rs_gf_exp_seq[a];
    char exp_b = _ejfat_rs_gf_exp_seq[b];
    char diff = (exp_a - exp_b + 15) % 15;
    return _ejfat_rs_gf_log_seq[diff];
}

// Simple 8x8 matrix inversion over GF(16) using Gauss-Jordan elimination
int gf_matrix_invert(char matrix[8][8], char inv[8][8]) {
    char aug[8][16];

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            aug[i][j] = matrix[i][j];
            aug[i][j + 8] = (i == j) ? 1 : 0;
        }
    }

    for (int i = 0; i < 8; i++) {
        int pivot_row = i;
        for (int k = i + 1; k < 8; k++) {
            if (aug[k][i] != 0) {
                pivot_row = k;
                break;
            }
        }

        if (aug[pivot_row][i] == 0) {
            return -1;
        }

        if (pivot_row != i) {
            for (int j = 0; j < 16; j++) {
                char temp = aug[i][j];
                aug[i][j] = aug[pivot_row][j];
                aug[pivot_row][j] = temp;
            }
        }

        char pivot = aug[i][i];
        for (int j = 0; j < 16; j++) {
            aug[i][j] = gf_div_local(aug[i][j], pivot);
        }

        for (int k = 0; k < 8; k++) {
            if (k != i && aug[k][i] != 0) {
                char factor = aug[k][i];
                for (int j = 0; j < 16; j++) {
                    aug[k][j] = gf_sum(aug[k][j], gf_mul(factor, aug[i][j]));
                }
            }
        }
    }

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            inv[i][j] = aug[i][j + 8];
        }
    }

    return 0;
}

// Initialize decoder table for single erasure patterns
int init_decode_table_for_test(rs_decode_table *table) {
    printf("Initializing decoder table...\n");

    static const char Genc[2][8] = {
        {14, 6, 14, 9, 7, 1, 15, 6},
        {5, 9, 4, 13, 8, 1, 5, 8}
    };

    table->capacity = 20;
    table->entries = malloc(table->capacity * sizeof(rs_decode_table_entry));
    if (!table->entries) {
        return -1;
    }

    table->size = 0;

    // Pattern 0: No erasures
    rs_decode_table_entry *entry = &table->entries[table->size];
    entry->num_erasures = 0;
    entry->erasure_pattern[0] = -1;
    entry->erasure_pattern[1] = -1;
    entry->valid = 1;

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            entry->inv_matrix[i][j] = (i == j) ? 1 : 0;
        }
    }
    table->size++;

    // Generate all single erasure patterns
    for (int pos = 0; pos < 8; pos++) {
        entry = &table->entries[table->size];
        entry->num_erasures = 1;
        entry->erasure_pattern[0] = pos;
        entry->erasure_pattern[1] = -1;
        entry->valid = 1;

        char g_mod[8][8];
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                if (i == pos) {
                    g_mod[i][j] = Genc[0][j];
                } else {
                    g_mod[i][j] = (i == j) ? 1 : 0;
                }
            }
        }

        if (gf_matrix_invert(g_mod, entry->inv_matrix) != 0) {
            entry->valid = 0;
        }
        table->size++;
    }

    printf("Decoder table initialized with %d patterns\n", table->size);
    return 0;
}

void free_decode_table(rs_decode_table *table) {
    if (table->entries) {
        free(table->entries);
        table->entries = NULL;
    }
    table->size = 0;
    table->capacity = 0;
}

// Test dual-nibble encoding/decoding with various patterns
void test_dual_nibble_basic() {
    printf("\n=============== Basic Dual-Nibble Encoding/Decoding Test ===============\n");

    rs_model *encoder = init_rs();
    if (!encoder) {
        printf("Failed to initialize RS encoder\n");
        return;
    }

    rs_decode_table decode_table;
    if (init_decode_table_for_test(&decode_table) != 0) {
        printf("Failed to initialize decode table\n");
        free_rs(encoder);
        return;
    }

    // Test patterns with full byte values (0x00 to 0xFF)
    struct {
        const char *name;
        char data[8];
    } test_patterns[] = {
        {"All zeros", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
        {"All 0xFF", {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}},
        {"Sequential", {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF}},
        {"Alternating", {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}},
        {"Random 1", {0x7D, 0x3B, 0xC5, 0xD8, 0xE2, 0xF1, 0x9C, 0x4E}},
        {"Random 2", {0xCA, 0x69, 0xA4, 0x1F, 0x85, 0xDB, 0x3C, 0xE7}},
        {"Lower only", {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}},
        {"Upper only", {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}}
    };

    int num_patterns = sizeof(test_patterns) / sizeof(test_patterns[0]);
    int total_tests = 0;
    int passed_tests = 0;

    for (int p = 0; p < num_patterns; p++) {
        printf("\n--- Pattern %d: %s ---\n", p + 1, test_patterns[p].name);

        char original_data[8];
        char parity_bytes[2];
        char codeword[10];

        memcpy(original_data, test_patterns[p].data, 8);

        print_bytes_hex("Original data", original_data, 8);
        print_bytes_nibbles("  (nibbles)  ", original_data, 8);

        // Encode using dual-nibble encoder
        neon_rs_encode_dual_nibble(encoder, original_data, parity_bytes);
        print_bytes_hex("Parity bytes ", parity_bytes, 2);
        print_bytes_nibbles("  (nibbles)  ", parity_bytes, 2);

        // Create codeword
        memcpy(codeword, original_data, 8);
        memcpy(codeword + 8, parity_bytes, 2);

        // Test decoding with no errors
        printf("Testing no errors: ");
        char decoded[8];
        int erasures[] = {};

        total_tests++;
        if (neon_rs_decode_dual_nibble(&decode_table, codeword, erasures, 0, decoded) == 0) {
            if (memcmp(decoded, original_data, 8) == 0) {
                printf("PASSED\n");
                passed_tests++;
            } else {
                printf("FAILED (incorrect decode)\n");
                print_bytes_hex("  Expected", original_data, 8);
                print_bytes_hex("  Got     ", decoded, 8);
            }
        } else {
            printf("FAILED (decode error)\n");
        }

        // Test decoding with single error at position 3
        printf("Testing error at position 3: ");
        char corrupted[10];
        memcpy(corrupted, codeword, 10);
        corrupted[3] = 0x00;  // Erase byte at position 3

        int erasures_err[] = { 3 };
        total_tests++;
        if (neon_rs_decode_dual_nibble(&decode_table, corrupted, erasures_err, 1, decoded) == 0) {
            if (memcmp(decoded, original_data, 8) == 0) {
                printf("PASSED\n");
                passed_tests++;
            } else {
                printf("FAILED (incorrect decode)\n");
                print_bytes_hex("  Expected", original_data, 8);
                print_bytes_hex("  Got     ", decoded, 8);
            }
        } else {
            printf("FAILED (decode error)\n");
        }
    }

    printf("\n=== Basic Dual-Nibble Test Summary ===\n");
    printf("Total tests: %d\n", total_tests);
    printf("Passed: %d\n", passed_tests);
    printf("Failed: %d\n", total_tests - passed_tests);
    printf("Success rate: %.1f%%\n", 100.0 * passed_tests / total_tests);

    free_decode_table(&decode_table);
    free_rs(encoder);
    printf("\n=============== Basic Dual-Nibble Tests Complete ===============\n");
}

// Test that upper and lower nibbles are encoded independently
void test_nibble_independence() {
    printf("\n=============== Nibble Independence Test ===============\n");

    rs_model *encoder = init_rs();
    if (!encoder) {
        printf("Failed to initialize RS encoder\n");
        return;
    }

    // Test 1: Same lower nibbles, different upper nibbles
    char data1[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    char data2[8] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x17, 0x28};

    char parity1[2], parity2[2];

    neon_rs_encode_dual_nibble(encoder, data1, parity1);
    neon_rs_encode_dual_nibble(encoder, data2, parity2);

    printf("Test 1: Same lower nibbles, different upper nibbles\n");
    print_bytes_hex("Data 1  ", data1, 8);
    print_bytes_hex("Parity 1", parity1, 2);
    print_bytes_hex("Data 2  ", data2, 8);
    print_bytes_hex("Parity 2", parity2, 2);

    // Lower nibble parity should be the same
    if ((parity1[0] & 0x0F) == (parity2[0] & 0x0F) &&
        (parity1[1] & 0x0F) == (parity2[1] & 0x0F)) {
        printf("✓ Lower nibble parity matches (as expected)\n");
    } else {
        printf("✗ Lower nibble parity differs (unexpected!)\n");
    }

    // Upper nibble parity should be different
    if ((parity1[0] >> 4) != (parity2[0] >> 4) ||
        (parity1[1] >> 4) != (parity2[1] >> 4)) {
        printf("✓ Upper nibble parity differs (as expected)\n");
    } else {
        printf("✗ Upper nibble parity matches (unexpected!)\n");
    }

    free_rs(encoder);
    printf("\n=============== Nibble Independence Test Complete ===============\n");
}

// Implementation of the dual-nibble decoder
int neon_rs_decode_dual_nibble(rs_decode_table *table, char *received_bytes,
                                int *erasure_locations, int num_erasures,
                                char *decoded_bytes) {

  DEBUG_PRINT("\n=== DUAL-NIBBLE DECODER DEBUG ===\n");
  DEBUG_PRINT("Number of erasures: %d\n", num_erasures);
  if (num_erasures > 0) {
    DEBUG_PRINT("Erasure locations: ");
    for (int i = 0; i < num_erasures; i++) DEBUG_PRINT("%d ", erasure_locations[i]);
    DEBUG_PRINT("\n");
  }

  if (num_erasures > 2) {
    return -1;
  }

  uint8x8_t data_vec = vld1_u8((uint8_t *)received_bytes);

  uint8x8_t nibble_mask = vdup_n_u8(0x0F);
  uint8x8_t lower_data = vand_u8(data_vec, nibble_mask);
  uint8x8_t upper_data = vshr_n_u8(data_vec, 4);

  uint8_t lower_parity[2] = {received_bytes[8] & 0x0F, received_bytes[9] & 0x0F};
  uint8_t upper_parity[2] = {(received_bytes[8] >> 4) & 0x0F, (received_bytes[9] >> 4) & 0x0F};

  DEBUG_PRINT("Parity bytes: 0x%02X 0x%02X\n", (uint8_t)received_bytes[8], (uint8_t)received_bytes[9]);
  DEBUG_PRINT_ARRAY("Lower parity", lower_parity, 2);
  DEBUG_PRINT_ARRAY("Upper parity", upper_parity, 2);

  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;
  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);
  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);

  rs_decode_table_entry *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry *candidate = &table->entries[t];

    if (!candidate->valid || candidate->num_erasures != num_erasures) {
      continue;
    }

    int match = 0;
    if (num_erasures == 0) {
      match = 1;
    } else if (num_erasures == 1) {
      match = (candidate->erasure_pattern[0] == erasure_locations[0]);
    } else if (num_erasures == 2) {
      match = ((candidate->erasure_pattern[0] == erasure_locations[0] &&
                candidate->erasure_pattern[1] == erasure_locations[1]) ||
               (candidate->erasure_pattern[0] == erasure_locations[1] &&
                candidate->erasure_pattern[1] == erasure_locations[0]));
    }

    if (match) {
      entry = candidate;
      break;
    }
  }

  if (!entry) {
    DEBUG_PRINT("ERROR: No matching decode table entry found!\n");
    return -1;
  }

  DEBUG_PRINT("Using decode table entry for %d erasure(s)\n", entry->num_erasures);

  // Decode lower nibble stream
  uint8_t lower_rx_modified[8] __attribute__((aligned(16)));
  vst1_u8(lower_rx_modified, lower_data);

  DEBUG_PRINT("\n--- LOWER NIBBLE STREAM ---\n");
  DEBUG_PRINT_ARRAY("Original lower data", lower_rx_modified, 8);

  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      DEBUG_PRINT("Substituting position %d with lower_parity[%d] = %X\n",
                  erasure_locations[i], i, lower_parity[i]);
      lower_rx_modified[erasure_locations[i]] = lower_parity[i];
    }
  }

  DEBUG_PRINT_ARRAY("After substitution", lower_rx_modified, 8);

  uint8x8_t lower_rx_vec = vld1_u8(lower_rx_modified);

  uint8_t lower_decoded[8];
  for (int i = 0; i < 8; i++) {
    uint8x8_t matrix_row = vld1_u8((uint8_t *)entry->inv_matrix[i]);
    uint8x8_t prod_vec = neon_gf_mul_vec(matrix_row, lower_rx_vec, exp_table, log_table);

    uint8_t temp[8];
    vst1_u8(temp, prod_vec);

    #if DEBUG_DUAL_NIBBLE
    if (i == 3 && num_erasures > 0) {  // Debug row 3 for erasure cases
      uint8_t matrix_row_vals[8];
      vst1_u8(matrix_row_vals, matrix_row);
      DEBUG_PRINT("LOWER Row 3 matrix: ");
      for (int k = 0; k < 8; k++) DEBUG_PRINT("%X ", matrix_row_vals[k]);
      DEBUG_PRINT("\n");
      DEBUG_PRINT("LOWER Row 3 products: ");
      for (int k = 0; k < 8; k++) DEBUG_PRINT("%X ", temp[k]);
      DEBUG_PRINT("\n");
    }
    #endif

    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= temp[j];
    }

    lower_decoded[i] = result & 0x0F;

    #if DEBUG_DUAL_NIBBLE
    if (i == 3 && num_erasures > 0) {
      DEBUG_PRINT("LOWER Row 3 XOR result: %X (expected: 7)\n", lower_decoded[i]);
    }
    #endif
  }

  DEBUG_PRINT_ARRAY("Lower decoded", lower_decoded, 8);

  // Decode upper nibble stream
  uint8_t upper_rx_modified[8] __attribute__((aligned(16)));
  vst1_u8(upper_rx_modified, upper_data);

  DEBUG_PRINT("\n--- UPPER NIBBLE STREAM ---\n");
  DEBUG_PRINT_ARRAY("Original upper data", upper_rx_modified, 8);

  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      DEBUG_PRINT("Substituting position %d with upper_parity[%d] = %X\n",
                  erasure_locations[i], i, upper_parity[i]);
      upper_rx_modified[erasure_locations[i]] = upper_parity[i];
    }
  }

  DEBUG_PRINT_ARRAY("After substitution", upper_rx_modified, 8);

  uint8x8_t upper_rx_vec = vld1_u8(upper_rx_modified);

  uint8_t upper_decoded[8];
  for (int i = 0; i < 8; i++) {
    uint8x8_t matrix_row = vld1_u8((uint8_t *)entry->inv_matrix[i]);
    uint8x8_t prod_vec = neon_gf_mul_vec(matrix_row, upper_rx_vec, exp_table, log_table);

    uint8_t temp[8];
    vst1_u8(temp, prod_vec);

    #if DEBUG_DUAL_NIBBLE
    if (i == 3 && num_erasures > 0) {  // Debug row 3 for erasure cases
      uint8_t matrix_row_vals[8];
      vst1_u8(matrix_row_vals, matrix_row);
      DEBUG_PRINT("Row 3 matrix: ");
      for (int k = 0; k < 8; k++) DEBUG_PRINT("%X ", matrix_row_vals[k]);
      DEBUG_PRINT("\n");
      DEBUG_PRINT("Row 3 products: ");
      for (int k = 0; k < 8; k++) DEBUG_PRINT("%X ", temp[k]);
      DEBUG_PRINT("\n");
    }
    #endif

    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= temp[j];
    }

    upper_decoded[i] = result & 0x0F;

    #if DEBUG_DUAL_NIBBLE
    if (i == 3 && num_erasures > 0) {
      DEBUG_PRINT("Row 3 XOR result: %X (expected: 6)\n", upper_decoded[i]);
    }
    #endif
  }

  DEBUG_PRINT_ARRAY("Upper decoded", upper_decoded, 8);

  // SIMD combine decoded nibbles
  uint8x8_t lower_vec = vld1_u8(lower_decoded);
  uint8x8_t upper_vec = vld1_u8(upper_decoded);
  uint8x8_t upper_shifted = vshl_n_u8(upper_vec, 4);
  uint8x8_t combined = vorr_u8(upper_shifted, lower_vec);

  vst1_u8((uint8_t *)decoded_bytes, combined);

  DEBUG_PRINT("\n--- FINAL RESULT ---\n");
  DEBUG_PRINT_ARRAY("Decoded bytes", (uint8_t*)decoded_bytes, 8);
  DEBUG_PRINT("=================================\n\n");

  return 0;
}

int main() {
    printf("Dual-Nibble Reed-Solomon Encoder/Decoder Test\n");
    printf("==============================================\n");

    test_dual_nibble_basic();
    test_nibble_independence();

    return 0;
}
