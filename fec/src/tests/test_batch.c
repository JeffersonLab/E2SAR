#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../common/ejfat_rs.h"
#include "../common/ejfat_rs_decoder.h"

// Test batched encoding and decoding with consistent error patterns
int main() {
  printf("=== Batched RS Encoding/Decoding Test ===\n\n");

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

  // Test parameters
  int num_vectors = 1000;
  int block_size = 256;

  printf("Test configuration:\n");
  printf("  Number of vectors: %d\n", num_vectors);
  printf("  Block size: %d\n", block_size);
  printf("  RS code: RS(10,8) - 8 data + 2 parity symbols\n\n");

  // ===== Test 1: Nibble version (single symbols) =====
  printf("--- Test 1: Nibble Version (Single Symbols) ---\n");

  // Allocate buffers for vector-major layout
  char *data_vector_major = malloc(num_vectors * 8);
  char *parity_vector_major = malloc(num_vectors * 2);

  // Generate random test data
  srand(time(NULL));
  for (int i = 0; i < num_vectors * 8; i++) {
    data_vector_major[i] = rand() % 16;  // GF(16) symbols 0-15
  }

  // Convert to blocked transposed layout
  char *data_blocked = malloc(num_vectors * 8);
  char *parity_blocked = malloc(num_vectors * 2);
  convert_to_blocked_transposed_data(data_vector_major, data_blocked, num_vectors, block_size);

  // Encode using batched encoder
  printf("Encoding %d vectors...\n", num_vectors);
  neon_rs_encode_batch_blocked(rs, data_blocked, parity_blocked, num_vectors, block_size);

  // Convert parity back to vector-major for verification
  convert_from_blocked_transposed_parity(parity_blocked, parity_vector_major, num_vectors, block_size);

  // Print first vector as example
  printf("First vector (example):\n");
  printf("  Data: ");
  for (int i = 0; i < 8; i++) printf("%d ", data_vector_major[i]);
  printf("\n  Parity: ");
  for (int i = 0; i < 2; i++) printf("%d ", parity_vector_major[i]);
  printf("\n");

  // Simulate erasures (same pattern for all vectors)
  int erasure_locations[2] = {2, 5};  // Erase symbols at positions 2 and 5
  int num_erasures = 2;

  printf("\nSimulating erasures at positions: ");
  for (int i = 0; i < num_erasures; i++) printf("%d ", erasure_locations[i]);
  printf("\n");

  // Save original data for comparison
  char *original_data = malloc(num_vectors * 8);
  memcpy(original_data, data_blocked, num_vectors * 8);

  // Zero out erased symbols in all vectors
  for (int block = 0; block < (num_vectors + block_size - 1) / block_size; block++) {
    int vecs_in_block = (block * block_size + block_size <= num_vectors) ?
                        block_size : (num_vectors - block * block_size);
    int block_offset = block * block_size * 8;

    for (int e = 0; e < num_erasures; e++) {
      for (int v = 0; v < vecs_in_block; v++) {
        data_blocked[block_offset + erasure_locations[e] * block_size + v] = 0;
      }
    }
  }

  // Decode using batched decoder
  printf("Decoding %d vectors with shared erasure pattern...\n", num_vectors);
  int result = neon_rs_decode_batch_blocked(&table, data_blocked, parity_blocked,
                                            erasure_locations, num_erasures,
                                            num_vectors, block_size);

  if (result != 0) {
    printf("ERROR: Batch decoding failed!\n");
  } else {
    // Verify all vectors were decoded correctly
    int errors = 0;
    for (int i = 0; i < num_vectors * 8; i++) {
      if (data_blocked[i] != original_data[i]) {
        errors++;
      }
    }

    if (errors == 0) {
      printf("SUCCESS: All %d vectors decoded correctly!\n", num_vectors);
    } else {
      printf("FAILURE: %d symbol errors detected\n", errors);
    }
  }

  free(data_vector_major);
  free(parity_vector_major);
  free(data_blocked);
  free(parity_blocked);
  free(original_data);

  // ===== Test 2: Dual-nibble version (full bytes) =====
  printf("\n--- Test 2: Dual-Nibble Version (Full Bytes) ---\n");

  // Allocate buffers for dual-nibble test
  char *data_bytes_vector_major = malloc(num_vectors * 8);
  char *parity_bytes_vector_major = malloc(num_vectors * 2);

  // Generate random test data (full bytes)
  for (int i = 0; i < num_vectors * 8; i++) {
    data_bytes_vector_major[i] = rand() % 256;  // Full byte values 0-255
  }

  // Convert to blocked transposed layout
  char *data_bytes_blocked = malloc(num_vectors * 8);
  char *parity_bytes_blocked = malloc(num_vectors * 2);
  convert_to_blocked_transposed_data(data_bytes_vector_major, data_bytes_blocked, num_vectors, block_size);

  // Encode using batched dual-nibble encoder
  printf("Encoding %d vectors (dual-nibble)...\n", num_vectors);
  neon_rs_encode_dual_nibble_batch_blocked(rs, data_bytes_blocked, parity_bytes_blocked,
                                           num_vectors, block_size);

  // Convert parity back for display
  convert_from_blocked_transposed_parity(parity_bytes_blocked, parity_bytes_vector_major, num_vectors, block_size);

  // Print first vector as example
  printf("First vector (example, hex):\n");
  printf("  Data: ");
  for (int i = 0; i < 8; i++) printf("%02X ", (unsigned char)data_bytes_vector_major[i]);
  printf("\n  Parity: ");
  for (int i = 0; i < 2; i++) printf("%02X ", (unsigned char)parity_bytes_vector_major[i]);
  printf("\n");

  // Save original data for comparison
  char *original_bytes = malloc(num_vectors * 8);
  memcpy(original_bytes, data_bytes_blocked, num_vectors * 8);

  // Zero out erased symbols (same pattern)
  for (int block = 0; block < (num_vectors + block_size - 1) / block_size; block++) {
    int vecs_in_block = (block * block_size + block_size <= num_vectors) ?
                        block_size : (num_vectors - block * block_size);
    int block_offset = block * block_size * 8;

    for (int e = 0; e < num_erasures; e++) {
      for (int v = 0; v < vecs_in_block; v++) {
        data_bytes_blocked[block_offset + erasure_locations[e] * block_size + v] = 0;
      }
    }
  }

  // Decode using batched dual-nibble decoder
  printf("\nDecoding %d vectors with shared erasure pattern...\n", num_vectors);
  result = neon_rs_decode_dual_nibble_batch_blocked(&table, data_bytes_blocked, parity_bytes_blocked,
                                                    erasure_locations, num_erasures,
                                                    num_vectors, block_size);

  if (result != 0) {
    printf("ERROR: Batch dual-nibble decoding failed!\n");
  } else {
    // Verify all vectors were decoded correctly
    int errors = 0;
    for (int i = 0; i < num_vectors * 8; i++) {
      if (data_bytes_blocked[i] != original_bytes[i]) {
        errors++;
      }
    }

    if (errors == 0) {
      printf("SUCCESS: All %d vectors decoded correctly (dual-nibble)!\n", num_vectors);
    } else {
      printf("FAILURE: %d byte errors detected\n", errors);
    }
  }

  free(data_bytes_vector_major);
  free(parity_bytes_vector_major);
  free(data_bytes_blocked);
  free(parity_bytes_blocked);
  free(original_bytes);

  // Cleanup
  free_rs_decode_table(&table);
  free_rs(rs);

  printf("\n=== Test Complete ===\n");
  return 0;
}
