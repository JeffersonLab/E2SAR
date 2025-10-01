#ifndef __ejfat_rs_h
#define __ejfat_rs_h

#include <arm_neon.h>

// --------------------------------------------------------------------------------------

typedef struct {
  int n_packets;
  int packet_len;
  int n_parity;
  char **packets;
  char **parity_packets;
} ejfat_rs_buf;

int allocate_rs_buf(ejfat_rs_buf *buf) {

  buf->packets = malloc(buf->n_packets * sizeof(char *));
  if (buf->packets == NULL) {
    perror("malloc failed");
    return 1;
  };
  
  // Allocate memory for each row (array of chars)
  for (int i = 0; i < buf->n_packets; i++) {
    buf->packets[i] = malloc(buf->packet_len * sizeof(char));
    if (buf->packets[i] == NULL) {
      perror("malloc failed");
      return 1;
    }
  }
  return 0;
}

int free_rs_buf(ejfat_rs_buf *buf) {
  for (int i = 0; i < buf->n_packets; i++) {
    free(buf->packets[i]);
  }
  free(buf->packets);
  return 0;
}

void print_rs_buf(ejfat_rs_buf *buf) {

  for (int packet = 0 ; packet < buf->n_packets; packet++) {
    for (char symbol = 0 ; symbol < buf->packet_len; symbol++) {
      printf("%d ",buf->packets[packet][symbol]);
    }
    printf("\n");
  }
  return;
}

// --------------------------------------------------------------------------------------


typedef struct {
  int   len;
  char  val [256];
} rs_poly_vector;

void print_rs_poly_vector(rs_poly_vector *v) {
  for (int i=0; i < v->len; i++) {
    printf("%d ",v->val[i]);
  }
  printf("\n");
}


// --------------------------------------------------------------------------------------

typedef struct {
  int   rows;
  int   cols;
  rs_poly_vector *val[];  // this will be populated when instantiated.
} rs_poly_matrix;


void print_rs_poly_matrix(rs_poly_matrix *m) {
  printf("rows = %d\n",m->rows);
  printf("cols = %d\n",m->cols);
  for (int i = 0; i < m->rows ; i++) {
    print_rs_poly_vector(m->val[i]);
  }
}


// --------------------------------------------------------------------------

#include "../prototype/python/rs_model.h"

/*
def gf_mul(a,b):
  if (a == 0) or (b == 0):
     return 0

  exp_a = gf_exp_seq[a]
  exp_b = gf_exp_seq[b]
  sum = (exp_a+exp_b) % 15
  return(gf_log_seq[sum])
*/

char gf_mul(char a,char b) {
  if ( a == 0  |  b == 0) {
    return 0;
  }

  char exp_a = _ejfat_rs_gf_exp_seq[a];
  char exp_b = _ejfat_rs_gf_exp_seq[b];
  char sum = (exp_a+exp_b) % 15;
    
  return (_ejfat_rs_gf_log_seq[sum]);
}

/*
def gf_sum(a,b):
   return a^b
*/

char gf_sum(char a,char b) {
  return (a^b);
}

/*
def poly_elem_mul(poly_a,poly_b):
   return [ gf_mul(a,b) for a,b in zip(poly_a,poly_b) ]
*/

// Multiply elements of two polynomials (element-wise)
void poly_elem_mul(rs_poly_vector *a , rs_poly_vector *b, rs_poly_vector *y) {

  if ( a->len != b->len ) {
    printf("Error:  poly_elem_mul : poly vectors are not the same length %d %d",a->len,b->len);
    return;
  }
    
  for (int i = 0; i < a->len; ++i) {
    y->val[i] = gf_mul(a->val[i], b->val[i]);
  }
}
   
/*				
def poly_dot(X,Y):
   if (len(X) != len(Y)): return -1
   Z = poly_elem_mul(X,Y)
   p = 0
   for z in Z:
      p = gf_sum(p,z)
   return p
*/



char poly_dot(rs_poly_vector *X, rs_poly_vector *Y) {

  rs_poly_vector Z;
  char p;
  
  if ( X->len != Y->len ) {
    printf("Error:  poly_dot : poly vectors are not the same length %d %d",X->len,Y->len);
    return 0;
  }
  Z.len = Y->len;
  
  poly_elem_mul(X,Y,&Z);

  p = 0;
  for (int i=0; i < Z.len; ++i) {
    p = gf_sum(p,Z.val[i]);
  }

  return p;
}




/*
def poly_matrix_vector_mul(M,V):
   if (len(M[0]) != len(V)):
      # print(f"Error vector length does not match matrix row length")                         
      # print(f"  Mrow = {len(M[0])}  V={len(V)}")                                             
      return [-1]

   y = []
   for row in M:
      y.append(poly_dot(row,V))
   return y
*/

void poly_matrix_vector_mul(rs_poly_matrix *m,rs_poly_vector *v , rs_poly_vector *y) {

  if (y->len != m->rows) {
    printf("poly_matrix_vector_mul: Error result vector length (%d) does not match matrix cols (%d)\n",y->len,m->rows);
    return;
  }
  
  for (int row = 0 ; row < m->rows ; row++) {
    y->val[row] = poly_dot(m->val[row],v);
  }
}

typedef struct {
  int n;    // number of data symbols
  int p;    // number of parity symbols
  int k;    // number of message symbols (n+p)

  rs_poly_matrix *G;        // full G matrix  [ I | P ]
  rs_poly_matrix *Genc;     // Gtranspose without I  transpose([P])
  char **Genc_exp;         // P in log space for direct use by the encoder
} rs_model;


void print_rs_model(rs_model *rs) {

  printf(" n = %d \n",rs->n);
  printf(" p = %d \n",rs->p);
  printf(" k = %d \n",rs->k);

  printf("\n");
  printf("G = \n");
  print_rs_poly_matrix(rs->G);
  printf("\n");  
  printf("Genc = \n");
  print_rs_poly_matrix(rs->Genc);
  printf("\n");
  
}

rs_model *init_rs() {

  printf("Initilizing rs model\n");

  rs_model *rs = (rs_model *) malloc(sizeof(rs_model));

  rs->n = _ejfat_rs_n;
  rs->p = _ejfat_rs_p;
  rs->k = _ejfat_rs_k;

  rs->G = (rs_poly_matrix *) malloc(sizeof(rs_poly_matrix) + + rs->n * sizeof(rs_poly_vector *));
  rs->G->rows = rs->n;
  rs->G->cols = rs->k;
  
  for (int row = 0 ; row < rs->G->rows ; row++) {
  
    rs->G->val[row] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
    if (!rs->G->val[row] ) {
      printf("Error could not malloc a new row for the G matrix\n");
      return NULL;
    };

    rs->G->val[row]->len = rs->G->cols;
    for (int i= 0; i < rs->G->cols ; i++) {
      rs->G->val[row]->val[i] = _ejfat_rs_G[row][i];
    };    
  };

  rs->Genc = (rs_poly_matrix *) malloc(sizeof(rs_poly_matrix) + rs->p * sizeof(rs_poly_vector *));
  rs->Genc->rows = rs->p;
  rs->Genc->cols = rs->n;
  
  for (int col = 0 ; col < rs->p ; col++) {
    rs->Genc->val[col] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
    if (!rs->Genc->val[col]) {
      printf("Error could not malloc a new row for the G matrix\n");
      return NULL;
    };

    rs->Genc->val[col]->len = rs->n;
    for (int row = 0; row < rs->n ; row++) {
      rs->Genc->val[col]->val[row] = _ejfat_rs_G[row][col + rs->n];
    }
  }


  rs->Genc_exp = malloc(rs->Genc->rows * sizeof(char *));
  if (rs->Genc_exp == NULL) {
    perror("malloc failed");
    return NULL;
  };

  // Step 2: Allocate each row (a char array)
  for (int i = 0; i < rs->Genc->rows; i++) {
    rs->Genc_exp[i] = malloc(rs->Genc->cols * sizeof(char));
    if (rs->Genc_exp[i] == NULL) {
      perror("malloc failed");
      return NULL;
    }
  }

  for (int row = 0 ; row < rs->Genc->rows ; row++) {
    for (int col = 0 ; col < rs->Genc->cols ; col++) {
      rs->Genc_exp[row][col] = _ejfat_rs_gf_exp_seq[rs->Genc->val[row]->val[col]];
    }
  }

  printf(" ---- RS model after init \n");
  print_rs_model(rs);
  
  return rs;
}

void free_rs(rs_model *rs) {

  printf("freeing rs model\n");

  // ---- Free the G matrix
  
  for (int row = 0 ; row < rs->G->rows ; row++) {
    free(rs->G->val[row]);
  };
  free(rs->G);


  // ---- Free the Genc_exp   exponent space matrix.  Uses Genc->rows
  //  so free this first.  Then Genc

  for (int row = 0 ; row < rs->Genc->rows ; row++) {
    free(rs->Genc_exp[row]);
  };
  free(rs->Genc_exp);
  
  // ---- Free the Genc matrix  

  for (int row = 0 ; row < rs->Genc->rows ; row++) {
    free(rs->Genc->val[row]);
  };
  free(rs->Genc);
  
  free(rs);

  printf("done freeing\n");
}

// --------------------------------------------------------------------------
// Using model (rs), encode data vector (d), to produce parity words (p) which
// can be appended to (d) for transmission.  This assumes we are using a 
// systematic rs model, which transmits the data words as is.
// --------------------------------------------------------------------------

void rs_encode(rs_model *rs , rs_poly_vector *d , rs_poly_vector *p) {
  poly_matrix_vector_mul(rs->Genc , d , p);
}

void fast_rs_encode(rs_model *rs , rs_poly_vector *d , rs_poly_vector *p) {

  char exp_d;
  char exp_sum;
  
  for (int row=0 ; row < rs->Genc->rows ; row++ ) {
    p->val[row] = 0;
    for (int j=0 ; j < d->len ; j++ ) {

      char exp_d = _ejfat_rs_gf_exp_seq[d->val[j]];
      char exp_sum = (exp_d + rs->Genc_exp[row][j]) % 15;
    
      p->val[row] ^= (_ejfat_rs_gf_log_seq[exp_sum]);
    };
  };
}

void neon_rs_encode(rs_model *rs , rs_poly_vector *d , rs_poly_vector *p) {

  // --- for speed reasons, we do not check assmptions.  But the following must be true:
  //     d must be exactly 8 data words
  //     p must be exactly 2 pariy words
  // ------------------------------------------------------------------------------------

  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;  
  
  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);   // t[0] to t[7]
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);   // t[8] to t[15]

  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);   // t[0] to t[7]
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);   // t[8] to t[15]
  
// Load indices
  uint8x8_t indices = vld1_u8((unsigned const char *) d->val);

  // Do the lookup: y[i] = t[a[i]]
  uint8x8_t d_vec = vtbl2_u8(exp_table, indices);

  uint8x8_t mod = vdup_n_u8(15);  // used by mod instruction
  
  for (int i=0; i < rs->p; i++) {
    uint8x8_t enc_vec = vld1_u8((uint8_t *) rs->Genc_exp[i]);
    uint8x8_t sum = vadd_u8(d_vec, enc_vec);

    // Step 3: Compare: sum >= 15
    uint8x8_t mask = vcge_u8(sum, mod);  // returns 0xFF where sum >= 15
    // Step 4: Subtract 15 where needed
    uint8x8_t mod15 = vand_u8(mod, mask); // get 15 where needed, 0 elsewhere
    uint8x8_t exp_sum = vsub_u8(sum, mod15);

    uint8x8_t sum_vec = vtbl2_u8(log_table, exp_sum);

    uint8_t sum_vec_array[8];
    vst1_u8(sum_vec_array,sum_vec);
    p->val[i] = 0;
    for (int j=0 ; j < 8 ; j++ ){
      p->val[i] ^= sum_vec_array[j];
    }
  }
}

// --------------------------------------------------------------------------------------
// Dual-nibble NEON RS encoder - operates on full bytes (both upper and lower nibbles)
// --------------------------------------------------------------------------------------

void neon_rs_encode_dual_nibble(rs_model *rs, char *data_bytes, char *parity_bytes) {

  // --- Assumptions:
  //     data_bytes must be exactly 8 bytes
  //     parity_bytes will receive 2 bytes (4 parity nibbles combined)
  //     rs->p must be 2 (two parity symbols per nibble stream)
  // ------------------------------------------------------------------------------------

  // Load 8 data bytes into NEON register
  uint8x8_t data_vec = vld1_u8((uint8_t *)data_bytes);

  // SIMD nibble extraction
  uint8x8_t nibble_mask = vdup_n_u8(0x0F);
  uint8x8_t lower_nibbles = vand_u8(data_vec, nibble_mask);     // bits 0-3
  uint8x8_t upper_nibbles = vshr_n_u8(data_vec, 4);              // bits 4-7 shifted down

  // Load GF lookup tables (shared by both nibble streams)
  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;

  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);
  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);

  uint8x8_t mod = vdup_n_u8(15);  // used by mod instruction

  // ---- Encode lower nibbles ----

  // Create zero mask for lower nibbles
  uint8x8_t zero_vec = vdup_n_u8(0);
  uint8x8_t lower_zero_mask = vceq_u8(lower_nibbles, zero_vec);

  // Convert lower nibbles to exponent space
  uint8x8_t lower_exp = vtbl2_u8(exp_table, lower_nibbles);

  char lower_parity[2];
  for (int i = 0; i < rs->p; i++) {
    uint8x8_t enc_vec = vld1_u8((uint8_t *) rs->Genc_exp[i]);
    uint8x8_t sum = vadd_u8(lower_exp, enc_vec);

    // Modulo 15 operation
    uint8x8_t mask = vcge_u8(sum, mod);
    uint8x8_t mod15 = vand_u8(mod, mask);
    uint8x8_t exp_sum = vsub_u8(sum, mod15);

    // Convert back to normal space
    uint8x8_t sum_vec = vtbl2_u8(log_table, exp_sum);

    // Apply zero mask: if lower nibble was zero, result should be zero
    sum_vec = vbic_u8(sum_vec, lower_zero_mask);

    // Horizontal XOR reduction
    uint8_t sum_vec_array[8];
    vst1_u8(sum_vec_array, sum_vec);
    lower_parity[i] = 0;
    for (int j = 0; j < 8; j++) {
      lower_parity[i] ^= sum_vec_array[j];
    }
  }

  // ---- Encode upper nibbles ----

  // Create zero mask for upper nibbles
  uint8x8_t upper_zero_mask = vceq_u8(upper_nibbles, zero_vec);

  // Convert upper nibbles to exponent space
  uint8x8_t upper_exp = vtbl2_u8(exp_table, upper_nibbles);

  char upper_parity[2];
  for (int i = 0; i < rs->p; i++) {
    uint8x8_t enc_vec = vld1_u8((uint8_t *) rs->Genc_exp[i]);
    uint8x8_t sum = vadd_u8(upper_exp, enc_vec);

    // Modulo 15 operation
    uint8x8_t mask = vcge_u8(sum, mod);
    uint8x8_t mod15 = vand_u8(mod, mask);
    uint8x8_t exp_sum = vsub_u8(sum, mod15);

    // Convert back to normal space
    uint8x8_t sum_vec = vtbl2_u8(log_table, exp_sum);

    // Apply zero mask: if upper nibble was zero, result should be zero
    sum_vec = vbic_u8(sum_vec, upper_zero_mask);

    // Horizontal XOR reduction
    uint8_t sum_vec_array[8];
    vst1_u8(sum_vec_array, sum_vec);
    upper_parity[i] = 0;
    for (int j = 0; j < 8; j++) {
      upper_parity[i] ^= sum_vec_array[j];
    }
  }

  // ---- Combine parity nibbles into bytes ----

  parity_bytes[0] = ((upper_parity[0] & 0x0F) << 4) | (lower_parity[0] & 0x0F);
  parity_bytes[1] = ((upper_parity[1] & 0x0F) << 4) | (lower_parity[1] & 0x0F);
}


#endif

