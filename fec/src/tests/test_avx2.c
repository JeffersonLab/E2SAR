#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <immintrin.h>

#include "../avx2/ejfat_rs_avx2.h"

void test_single_nibble_encoder() {
  printf("\n=== Testing Single Nibble Encoder ===\n");

  // Test data: 8 nibbles (4-bit values)
  char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  char parity[2];

  avx2_rs_encode(data, parity);

  printf("Data symbols:   ");
  for (int i = 0; i < 8; i++) {
    printf("%d ", data[i]);
  }
  printf("\n");

  printf("Parity symbols: %d %d\n", parity[0], parity[1]);
}

void test_dual_nibble_encoder() {
  printf("\n=== Testing Dual Nibble Encoder ===\n");

  // Test data: 8 bytes, each byte contains two nibbles
  char data_bytes[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
  char parity_bytes[2];

  avx2_rs_encode_dual_nibble(data_bytes, parity_bytes);

  printf("Data bytes:   ");
  for (int i = 0; i < 8; i++) {
    printf("%02X ", (unsigned char)data_bytes[i]);
  }
  printf("\n");

  printf("Parity bytes: %02X %02X\n",
         (unsigned char)parity_bytes[0],
         (unsigned char)parity_bytes[1]);
}

void benchmark_single_nibble() {
  printf("\n=== Benchmark Single Nibble Encoder ===\n");

  char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  char parity[2];

  int test_frames = 1000;
  int test_packet_length = 8000;

  clock_t start_time = clock();
  for (int i = 0; i < test_frames; i++) {
    for (int j = 0; j < test_packet_length; j++) {
      avx2_rs_encode(data, parity);
    }
  }
  clock_t end_time = clock();

  double time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;

  printf("Time:           %f seconds\n", time_taken);
  printf("Frames/second:  %f\n", test_frames / time_taken);
  printf("Data rate:      %f Mbps\n",
         1/1E6 * 4 * 8 * test_packet_length * test_frames / time_taken);  // 4 bits per nibble
  printf("Parity result:  %d %d\n", parity[0], parity[1]);
}

void benchmark_dual_nibble() {
  printf("\n=== Benchmark Dual Nibble Encoder ===\n");

  char data_bytes[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
  char parity_bytes[2];

  int test_frames = 1000;
  int test_packet_length = 8000;

  clock_t start_time = clock();
  for (int i = 0; i < test_frames; i++) {
    for (int j = 0; j < test_packet_length; j++) {
      avx2_rs_encode_dual_nibble(data_bytes, parity_bytes);
    }
  }
  clock_t end_time = clock();

  double time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;

  printf("Time:           %f seconds\n", time_taken);
  printf("Frames/second:  %f\n", test_frames / time_taken);
  printf("Data rate:      %f Mbps\n",
         1/1E6 * 8 * 8 * test_packet_length * test_frames / time_taken);
  printf("Parity result:  %02X %02X\n",
         (unsigned char)parity_bytes[0],
         (unsigned char)parity_bytes[1]);
}

void test_zero_handling() {
  printf("\n=== Testing Zero Value Handling ===\n");

  // Test with zeros in various positions
  char data1[8] = {0, 2, 3, 4, 5, 6, 7, 8};
  char data2[8] = {1, 0, 3, 4, 5, 6, 7, 8};
  char data3[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  char parity[2];

  avx2_rs_encode(data1, parity);
  printf("Data [0,2,3,4,5,6,7,8]: Parity = %d %d\n", parity[0], parity[1]);

  avx2_rs_encode(data2, parity);
  printf("Data [1,0,3,4,5,6,7,8]: Parity = %d %d\n", parity[0], parity[1]);

  avx2_rs_encode(data3, parity);
  printf("Data [0,0,0,0,0,0,0,0]: Parity = %d %d\n", parity[0], parity[1]);

  // Test dual nibble with zeros
  char data_bytes[8] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
  char parity_bytes[2];

  avx2_rs_encode_dual_nibble(data_bytes, parity_bytes);
  printf("Dual nibble with zeros: Parity = %02X %02X\n",
         (unsigned char)parity_bytes[0],
         (unsigned char)parity_bytes[1]);
}

int main() {

  printf("========================================\n");
  printf("EJFAT Reed-Solomon AVX2 Encoder Tests\n");
  printf("========================================\n");
  printf("Configuration: RS(10,8) over GF(16)\n");
  printf("- 8 data symbols + 2 parity symbols\n");
  printf("- Single nibble: operates on 4-bit symbols\n");
  printf("- Dual nibble: processes full bytes\n");

  // Initialize the encoder
  init_ejfat_rs_avx2();

  test_single_nibble_encoder();
  test_dual_nibble_encoder();
  test_zero_handling();
  benchmark_single_nibble();
  benchmark_dual_nibble();

  printf("\n========================================\n");
  printf("All tests completed successfully!\n");
  printf("========================================\n");

  return 0;
}
