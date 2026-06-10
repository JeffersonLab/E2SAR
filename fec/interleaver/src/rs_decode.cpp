#include "fec/rs_decode.h"
#include "rs_decode_c.h"
#include <cassert>

namespace fec {

RsDecodeContext::RsDecodeContext() {
    auto* ctx = fec_rs_ctx_create();
    if (!ctx) return;
    rs = ctx;      // we store the opaque pointer directly
    table = nullptr;
    initialized = true;
}

RsDecodeContext::~RsDecodeContext() {
    if (initialized) {
        fec_rs_ctx_destroy(static_cast<fec_rs_ctx*>(rs));
    }
}

int rs_decode(
    std::span<uint8_t>      codewords,
    const FecBlockParams&   params,
    const std::vector<int>& erasedDataCols,
    RsDecodeContext&        ctx)
{
    if (erasedDataCols.size() > 2) return -1;
    if (erasedDataCols.empty()) return 0;
    if (!ctx.initialized) return -1;

    assert(codewords.size() == params.codeword_buf_size());

    auto* c_ctx = static_cast<fec_rs_ctx*>(ctx.rs);
    const uint32_t nw = params.num_words();

    int erasure_locs[2];
    int num_erasures = static_cast<int>(erasedDataCols.size());
    for (int i = 0; i < num_erasures; ++i)
        erasure_locs[i] = erasedDataCols[i];

    for (uint32_t w = 0; w < nw; ++w) {
        uint8_t* cw = codewords.data() + size_t{w} * FecBlockParams::CODEWORD_BYTES;
        int ret = fec_rs_decode_codeword(c_ctx, cw, erasure_locs, num_erasures);
        if (ret != 0) return -1;
    }

    return 0;
}

} // namespace fec
