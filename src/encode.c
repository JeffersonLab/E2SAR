
#include <stdio.h>
#include <stdlib.h>

#include "./ejfat_rs.h"

int main() {
    printf("Hello World\n");

    // -----   Allocate a packet buffer for the encoder to work on.
    ejfat_rs_buf *ej_buf = allocate_ejfat_rs_buf(5,10,2);

    printf("%d %d\n",ej_buf->n_packets,ej_buf->packet_len);

    // -----   Free the packet buffer

    free_ejfat_rs_buf(ej_buf);
    
    return 0;
}
