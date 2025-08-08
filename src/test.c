
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arm_neon.h>

#include "./ejfat_rs.h"

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

  int test_frames = 1000;
  int test_packet_length = 8000;

  clock_t start_time,end_time;
  double time_taken;
  
  start_time = clock();
  for (int i=0; i < test_frames ; i++) {
    for (int j=0; j < test_packet_length; j++ ) {
      rs_encode(rs,&msg,&parity);
    };
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;

  printf (" encode ran for %f seconds \n",time_taken);
  printf ("   frames / second = %f \n", test_frames / time_taken );
  printf ("   data rate = %f Mbps \n", 1/1E6 * 8 * 8 * test_packet_length * test_frames / time_taken );

  
  printf("parity woards are : ");
  print_rs_poly_vector(&parity);

  // ------------------------------------  fast encode ------------------------------------

  for (int i=0; i < parity.len; i++) {
    parity.val[i] = 0;     // reset the parity vector just to be sure we compute it again.
  }
  
  start_time = clock();
  for (int i=0; i < test_frames ; i++) {
    for (int j=0; j < test_packet_length; j++ ) {
      fast_rs_encode(rs,&msg,&parity);
    };
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;

  printf (" fast encode ran for %f seconds \n",time_taken);
  printf ("   frames / second = %f \n", test_frames / time_taken );
  printf ("   data rate = %f Mbps \n", 1/1E6 * 8 * 8 * test_packet_length * test_frames / time_taken );
  
  printf("parity woards are : ");
  print_rs_poly_vector(&parity);

    // ------------------------------------  neon encode ------------------------------------


  for (int i=0; i < parity.len; i++) {
    parity.val[i] = 0;     // reset the parity vector just to be sure we compute it again.
  }
  
  start_time = clock();
  for (int i=0; i < test_frames ; i++) {
    for (int j=0; j < test_packet_length; j++ ) {
      neon_rs_encode(rs,&msg,&parity);
    };
  }
  end_time = clock();
  time_taken = (double) (end_time - start_time) / CLOCKS_PER_SEC;

  printf (" neon encode ran for %f seconds \n",time_taken);
  printf ("   frames / second = %f \n", test_frames / time_taken );
  printf ("   data rate = %f Mbps \n", 1/1E6 * 8 * 8 * test_packet_length * test_frames / time_taken );
  
  printf("parity woards are : ");
  print_rs_poly_vector(&parity);
  
  
  printf ("Buf0 packet len = %d number of packets = %d\n",Buf0.packet_len,Buf0.n_packets);
  
  test_rs();

  free_rs_buf(&Buf0);
  free_rs(rs);
  
  return 0;
}
