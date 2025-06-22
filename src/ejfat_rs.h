#ifndef __ejfat_rs_h
#define __ejfat_rs_h

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
  rs_poly_matrix *Genc;     // Gtranspose without I  transpose([P})
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

  printf(" ---- RS model after init \n");
  print_rs_model(rs);
  
  return rs;
}

void free_rs(rs_model *rs) {

  printf("freeing rs model\n");

  for (int row = 0 ; row < rs->G->rows ; row++) {
    free(rs->G->val[row]);
  };
  free(rs->G);

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



#endif

