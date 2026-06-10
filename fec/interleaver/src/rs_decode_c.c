#include "rs_decode_c.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ejfat_rs.h"
#include "ejfat_rs_decoder.h"

struct fec_rs_ctx {
    rs_model *rs;
    rs_decode_table table;
};

fec_rs_ctx* fec_rs_ctx_create(void) {
    fec_rs_ctx* ctx = (fec_rs_ctx*)calloc(1, sizeof(fec_rs_ctx));
    if (!ctx) return NULL;

    ctx->rs = init_rs();
    if (!ctx->rs) {
        free(ctx);
        return NULL;
    }

    if (init_rs_decode_table(ctx->rs, &ctx->table) != 0) {
        free(ctx->rs);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void fec_rs_ctx_destroy(fec_rs_ctx* ctx) {
    if (!ctx) return;
    free_rs_decode_table(&ctx->table);
    free(ctx->rs);
    free(ctx);
}

int fec_rs_decode_codeword(fec_rs_ctx* ctx,
                           uint8_t* cw,
                           const int* erasure_locs,
                           int num_erasures)
{
    char d[8], p[2];
    d[0] = (cw[0] >> 4) & 0xF;
    d[1] =  cw[0]       & 0xF;
    d[2] = (cw[1] >> 4) & 0xF;
    d[3] =  cw[1]       & 0xF;
    d[4] = (cw[2] >> 4) & 0xF;
    d[5] =  cw[2]       & 0xF;
    d[6] = (cw[3] >> 4) & 0xF;
    d[7] =  cw[3]       & 0xF;
    p[0] = (cw[4] >> 4) & 0xF;
    p[1] =  cw[4]       & 0xF;

    rs_poly_vector received = { .len = 10 };
    for (int i = 0; i < 8; i++) received.val[i] = d[i];
    received.val[8] = p[0];
    received.val[9] = p[1];

    rs_poly_vector decoded = { .len = 8 };

    int ret = rs_decode_table_lookup(&ctx->table, &received,
                                     (int*)erasure_locs, num_erasures,
                                     &decoded);
    if (ret != 0) return -1;

    cw[0] = (uint8_t)((decoded.val[0] << 4) | decoded.val[1]);
    cw[1] = (uint8_t)((decoded.val[2] << 4) | decoded.val[3]);
    cw[2] = (uint8_t)((decoded.val[4] << 4) | decoded.val[5]);
    cw[3] = (uint8_t)((decoded.val[6] << 4) | decoded.val[7]);

    return 0;
}
