#ifndef FEC_RS_DECODE_C_H
#define FEC_RS_DECODE_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fec_rs_ctx fec_rs_ctx;

fec_rs_ctx* fec_rs_ctx_create(void);
void fec_rs_ctx_destroy(fec_rs_ctx* ctx);

// Decode a single codeword (10 packed nibbles in 5 bytes) in-place.
// erasure_locs: positions 0-7 = data columns, 8-9 = parity columns
// Returns 0 on success, -1 on error.
int fec_rs_decode_codeword(fec_rs_ctx* ctx,
                           uint8_t* codeword_5bytes,
                           const int* erasure_locs,
                           int num_erasures);

#ifdef __cplusplus
}
#endif

#endif
