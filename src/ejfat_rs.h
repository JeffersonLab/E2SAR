#ifndef __ejfat_rs_h
#define __ejfat_rs_h

typedef struct {
  int n_packets;
  int packet_len;
  int packet_size;
  int n_parity;
  char **packets;
  char **parity_packets;
} ejfat_rs_buf;

ejfat_rs_buf* allocate_ejfat_rs_buf(int n_packets,int packet_len,int n_parity) {

  ejfat_rs_buf *aBuf = (ejfat_rs_buf *) malloc(sizeof(ejfat_rs_buf));

  aBuf->n_packets = n_packets;
  aBuf->packet_len = packet_len;
  aBuf->n_parity = n_parity;

  aBuf->packets = (char **) malloc(aBuf->n_packets * sizeof(char *));
    
  for (int i = 0 ; i < aBuf->n_packets ; i++) {
    aBuf->packets[i] = (char *) malloc(aBuf->packet_len * sizeof(char));
    for (char j = 0 ; j < aBuf->packet_len ; j++) {
      aBuf->packets[i][j] = j;
    }
  }

  return(aBuf);
}

void free_ejfat_rs_buf(ejfat_rs_buf* aBuf) {

  for	(int i = 0 ; i < aBuf->n_packets ; i++) {
    free(aBuf->packets[i]);
  }
  free(aBuf->packets);
  free(aBuf);
  
}

#endif

