
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arm_neon.h>

#include "../common/ejfat_rs.h"
#include "../common/ejfat_rs_decoder.h"

void neon_test() {
  printf(" testing ARM NEON mode \n");

  uint8_t a[16] = {
        10, 20, 30, 40, 50, 60, 70, 80,
        90, 100, 110, 120, 130, 140, 150, 160
    };
  uint8_t b[16] = {
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16
    };

    uint8_t result[16];

    // Load data into NEON registers
    uint8x16_t va = vld1q_u8(a);  // Load 16 x uint8_t
    uint8x16_t vb = vld1q_u8(b);

    // Add the vectors
    uint8x16_t vsum = veorq_u8(va, vb);  // Element-wise addition

    // Store the result
    vst1q_u8(result, vsum);

    // Print the result
    printf("Result of uint8_t NEON vector addition:\n");
    for (int i = 0; i < 16; i++) {
        printf("result[%2d] = %3u\n", i, result[i]);
    }

  return;
}

void test_rs() {

  neon_test();

  printf(" adding 2+7 = %d\n",gf_sum(2,7));
  printf(" mult   2*7 = %d\n",gf_mul(2,7));

  rs_poly_vector a = { .len = 4, .val = { 1,2,3,4 }};
  rs_poly_vector b = { .len = 4, .val = { 2,3,4,5 }};
  rs_poly_vector c = { .len = 4 };

  poly_elem_mul(&a,&b,&c);
  for (int i=0; i < c.len; i++) {
    printf("%d ",c.val[i]);
  };
  printf("\n");

  printf("%d\n",poly_dot(&a,&b));

  for (int i=0; i < 8; i++) {
    printf("%d ",_ejfat_rs_G[i][0]);
  }
  printf("\n");
}

int main() {

  // ---   initialize the rs model for encoding and decoding
  
  rs_model *rs;
  rs = init_rs();

  // ---   Create a buffer of packets 

  printf(" --------------  Creating a packet buffer for testing ------------------ \n");
  
  ejfat_rs_buf Buf0 = { .n_packets = rs->n , .n_parity = rs->p , .packet_len = 32 };
  allocate_rs_buf(&Buf0);
  for (int packet = 0 ; packet < Buf0.n_packets; packet++) {
    for (char symbol = 0 ; symbol < Buf0.packet_len; symbol++) {
      Buf0.packets[packet][symbol] = (symbol+packet) % 16;
    }
  }

  print_rs_buf(&Buf0);
  
  // ---   Create a reed solomon message vector from the buffer of packets.
  
  rs_poly_vector msg;
  msg.len = Buf0.n_packets;
  if (msg.len > rs->n) { printf("Error trying to send a RS message > rs design %d > %d \n",Buf0.n_packets,rs->n); };

  for (int i=0 ; (i < Buf0.n_packets) ; i++ ) {
    msg.val[i] = Buf0.packets[i][1];
  };

  printf("message vector m = ");
  print_rs_poly_vector(&msg);

  rs_poly_vector parity;
  parity.len = rs->p;
  for (int i=0; i < parity.len; i++) {
    parity.val[i] = 0;
  };

  // ========================================================================
  // PERFORMANCE COMPARISON TESTS
  // ========================================================================

  int test_frames = 1000;
  int test_packet_length = 8000;
  int batch_size = 1000;
  int block_size = 256;

  clock_t start_time, end_time;
  double time_taken;
  double data_rate;

  printf("\n");
  printf("======================================================================\n");
  printf("                   PERFORMANCE COMPARISON                            \n");
  printf("======================================================================\n");
  printf("Test configuration:\n");
  printf("  Frames: %d\n", test_frames);
  printf("  Symbols per frame: %d\n", test_packet_length);
  printf("  Total operations: %d\n", test_frames * test_packet_length);
  printf("  Batch size (for batched tests): %d\n", batch_size);
  printf("  Block size: %d\n\n", block_size);

  // ------------------------------------  rs_encode (baseline) ------------------------------------

  for (int i=0; i < parity.len; i++) {
    parity.val[i] = 0;
  }

  start_time = clock();
  for (int i=0; i < test_frames ; i++) {
    for (int j=0; j < test_packet_length; j++ ) {
      rs_encode(rs,&msg,&parity);
    }
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;
  data_rate = 1/1E6 * 4 * 8 * test_packet_length * test_frames / time_taken;  // 4 bits per nibble

  printf("1. rs_encode (baseline matrix multiply):\n");
  printf("   Time: %.3f seconds\n", time_taken);
  printf("   Throughput: %.1f Mbps\n", data_rate);
  printf("   Speedup: 1.00x (baseline)\n");
  double baseline_time = time_taken;
  printf("   Parity: ");
  print_rs_poly_vector(&parity);

  // ------------------------------------  fast_rs_encode ------------------------------------

  for (int i=0; i < parity.len; i++) {
    parity.val[i] = 0;
  }

  start_time = clock();
  for (int i=0; i < test_frames ; i++) {
    for (int j=0; j < test_packet_length; j++ ) {
      fast_rs_encode(rs,&msg,&parity);
    }
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;
  data_rate = 1/1E6 * 4 * 8 * test_packet_length * test_frames / time_taken;  // 4 bits per nibble

  printf("\n2. fast_rs_encode (exp/log tables):\n");
  printf("   Time: %.3f seconds\n", time_taken);
  printf("   Throughput: %.1f Mbps\n", data_rate);
  printf("   Speedup: %.2fx\n", baseline_time / time_taken);
  printf("   Parity: ");
  print_rs_poly_vector(&parity);

  // ------------------------------------  neon_rs_encode ------------------------------------

  for (int i=0; i < parity.len; i++) {
    parity.val[i] = 0;
  }

  start_time = clock();
  for (int i=0; i < test_frames ; i++) {
    for (int j=0; j < test_packet_length; j++ ) {
      neon_rs_encode(rs,&msg,&parity);
    }
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;
  data_rate = 1/1E6 * 4 * 8 * test_packet_length * test_frames / time_taken;  // 4 bits per nibble

  printf("\n3. neon_rs_encode (SIMD nibble):\n");
  printf("   Time: %.3f seconds\n", time_taken);
  printf("   Throughput: %.1f Mbps\n", data_rate);
  printf("   Speedup: %.2fx\n", baseline_time / time_taken);
  printf("   Parity: ");
  print_rs_poly_vector(&parity);

  // ------------------------------------  neon_rs_encode_dual_nibble ------------------------------------

  char test_bytes[8];
  char test_parity_bytes[2];
  for (int i = 0; i < 8; i++) {
    test_bytes[i] = (msg.val[i] << 4) | msg.val[i];  // Create full bytes
  }

  start_time = clock();
  for (int i=0; i < test_frames ; i++) {
    for (int j=0; j < test_packet_length; j++ ) {
      neon_rs_encode_dual_nibble(rs, test_bytes, test_parity_bytes);
    }
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;
  data_rate = 1/1E6 * 8 * 8 * test_packet_length * test_frames / time_taken;  // 8 bits (2 nibbles) per byte

  printf("\n4. neon_rs_encode_dual_nibble (SIMD dual-nibble):\n");
  printf("   Time: %.3f seconds\n", time_taken);
  printf("   Throughput: %.1f Mbps\n", data_rate);
  printf("   Speedup: %.2fx\n", baseline_time / time_taken);
  printf("   Parity bytes: %02X %02X\n", (unsigned char)test_parity_bytes[0], (unsigned char)test_parity_bytes[1]);

  // ------------------------------------  batched nibble encode ------------------------------------

  // Prepare batched data
  char *batch_data_vec = malloc(batch_size * 8);
  char *batch_parity_vec = malloc(batch_size * 2);
  char *batch_data_blocked = malloc(batch_size * 8);
  char *batch_parity_blocked = malloc(batch_size * 2);

  for (int i = 0; i < batch_size; i++) {
    for (int j = 0; j < 8; j++) {
      batch_data_vec[i * 8 + j] = msg.val[j];
    }
  }

  convert_to_blocked_transposed_data(batch_data_vec, batch_data_blocked, batch_size, block_size);

  int num_batches = (test_frames * test_packet_length + batch_size - 1) / batch_size;

  start_time = clock();
  for (int i = 0; i < num_batches; i++) {
    neon_rs_encode_batch_blocked(rs, batch_data_blocked, batch_parity_blocked, batch_size, block_size);
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;
  data_rate = 1/1E6 * 4 * 8 * num_batches * batch_size / time_taken;  // 4 bits per nibble

  printf("\n5. neon_rs_encode_batch_blocked (batched SIMD, %d vectors):\n", batch_size);
  printf("   Time: %.3f seconds\n", time_taken);
  printf("   Throughput: %.1f Mbps\n", data_rate);
  printf("   Speedup: %.2fx\n", baseline_time / time_taken);
  printf("   Vectors/batch: %d\n", batch_size);

  // ------------------------------------  batched dual-nibble encode ------------------------------------

  char *batch_bytes_vec = malloc(batch_size * 8);
  char *batch_parity_bytes_vec = malloc(batch_size * 2);
  char *batch_bytes_blocked = malloc(batch_size * 8);
  char *batch_parity_bytes_blocked = malloc(batch_size * 2);

  for (int i = 0; i < batch_size; i++) {
    for (int j = 0; j < 8; j++) {
      batch_bytes_vec[i * 8 + j] = test_bytes[j];
    }
  }

  convert_to_blocked_transposed_data(batch_bytes_vec, batch_bytes_blocked, batch_size, block_size);

  start_time = clock();
  for (int i = 0; i < num_batches; i++) {
    neon_rs_encode_dual_nibble_batch_blocked(rs, batch_bytes_blocked, batch_parity_bytes_blocked,
                                             batch_size, block_size);
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;
  data_rate = 1/1E6 * 8 * 8 * num_batches * batch_size / time_taken;  // 8 bits (2 nibbles) per byte

  printf("\n6. neon_rs_encode_dual_nibble_batch_blocked (batched dual-nibble, %d vectors):\n", batch_size);
  printf("   Time: %.3f seconds\n", time_taken);
  printf("   Throughput: %.1f Mbps\n", data_rate);
  printf("   Speedup: %.2fx\n", baseline_time / time_taken);
  printf("   Vectors/batch: %d\n", batch_size);

  printf("\n======================================================================\n\n");

  // Cleanup batched test buffers
  free(batch_data_vec);
  free(batch_parity_vec);
  free(batch_data_blocked);
  free(batch_parity_blocked);
  free(batch_bytes_vec);
  free(batch_parity_bytes_vec);
  free(batch_bytes_blocked);
  free(batch_parity_bytes_blocked);
  
  
  printf ("Buf0 packet len = %d number of packets = %d\n",Buf0.packet_len,Buf0.n_packets);
  
  test_rs();

  free_rs_buf(&Buf0);
  free_rs(rs);
  
  return 0;
}
