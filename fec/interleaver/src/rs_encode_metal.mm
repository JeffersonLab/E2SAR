// rs_encode_metal.mm — Metal GPU implementation of rs_encode
//
// Each GPU thread handles one 5-byte codeword in the codeword buffer:
// reads bytes 0-3 (8 data nibbles), computes 2 GF(16) parity nibbles via the
// RS(10,8) generator matrix, and writes the result to byte 4.
//
// The kernel is in-place: the caller passes the codeword buffer produced by
// interleave() (or interleave_metal()).  Parity byte 4 is zeroed by the
// interleaver; this kernel fills it.
//
// Batching (super-buffer): pass FecBlockParams with col_height = N * per_block
// and a contiguous codeword buffer holding N blocks.  The grid becomes
// N * per_block * 8 threads — one per codeword across all blocks.  This is the
// same pattern used by interleave_metal() and amortizes the fixed ~0.4 ms
// Metal dispatch overhead over N blocks.
//
// The GF(16) tables and parity coefficients (48 bytes total) are declared as
// file-scope constant arrays in MSL — the compiler places them in the device's
// constant cache, shared uniformly across all threads in the dispatch.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "fec/rs_encode.h"

#include <cassert>
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Metal shader source (compiled at first call via newLibraryWithSource:)
// ---------------------------------------------------------------------------
static const char* kRsShaderSource = R"msl(
#include <metal_stdlib>
using namespace metal;

// GF(16) field tables — same as in rs_encode.cpp.
// Field: GF(2^4), irreducible polynomial x^4 + x + 1.
// gf_log[i]  = alpha^i  (exponent -> element)
// gf_exp[x]  = log_alpha(x)  (element -> exponent), gf_exp[0]=15 is sentinel
constant uchar gf_log[16] = { 1,2,4,8,3,6,12,11,5,10,7,14,15,13,9,0 };
constant uchar gf_exp[16] = { 15,0,1,4,2,8,5,10,3,14,9,7,6,13,11,12 };

// RS(10,8) parity sub-matrix P columns in exponent space.
// coeff0_exp[j] = gf_exp[ P[j][0] ],  P col 0 element-space: {14,6,14,9,7,1,15,6}
// coeff1_exp[j] = gf_exp[ P[j][1] ],  P col 1 element-space: { 5,9, 4,13,8,1, 5,8}
constant uchar coeff0_exp[8] = { 11, 5, 11, 14, 10, 0, 12, 5 };
constant uchar coeff1_exp[8] = {  8,14,  2, 13,  3, 0,  8, 3 };

kernel void rs_encode_gpu(
    device uchar*  codewords  [[buffer(0)]],  // in-place: bytes 0-3 read, byte 4 written
    constant uint& num_words  [[buffer(1)]],  // total codeword count
    uint gid [[thread_position_in_grid]])
{
    if (gid >= num_words) return;

    // Each codeword is 5 bytes: [sym0|sym1] [sym2|sym3] [sym4|sym5] [sym6|sym7] [p0|p1]
    device uchar* cw = codewords + (ulong)gid * 5u;

    // Extract 8 GF(16) data nibbles from bytes 0-3.
    uchar b0 = cw[0], b1 = cw[1], b2 = cw[2], b3 = cw[3];
    uchar s[8] = {
        (uchar)((b0 >> 4u) & 0xFu),   // symbol 0  (even → high nibble)
        (uchar)( b0        & 0xFu),   // symbol 1  (odd  → low  nibble)
        (uchar)((b1 >> 4u) & 0xFu),   // symbol 2
        (uchar)( b1        & 0xFu),   // symbol 3
        (uchar)((b2 >> 4u) & 0xFu),   // symbol 4
        (uchar)( b2        & 0xFu),   // symbol 5
        (uchar)((b3 >> 4u) & 0xFu),   // symbol 6
        (uchar)( b3        & 0xFu),   // symbol 7
    };

    // Compute parity0 and parity1 = sum_j  gf_mul(P[j][i], s[j])  for i=0,1
    // gf_mul(coeff, s) = (s==0) ? 0 : gf_log[(gf_exp[s] + coeff_exp) % 15]
    // All parity coefficients are non-zero, so only the data zero case matters.
    uchar p0 = 0u, p1 = 0u;
    for (uint j = 0u; j < 8u; ++j) {
        if (s[j] != 0u) {
            uchar e = gf_exp[s[j]];
            p0 ^= gf_log[(e + coeff0_exp[j]) % 15u];
            p1 ^= gf_log[(e + coeff1_exp[j]) % 15u];
        }
    }

    cw[4] = (p0 << 4u) | p1;
}
)msl";

// ---------------------------------------------------------------------------
// Singleton Metal context for the RS encoder pipeline.
// ---------------------------------------------------------------------------
namespace {

struct RsMetalCtx {
    id<MTLDevice>               device;
    id<MTLCommandQueue>         queue;
    id<MTLComputePipelineState> pipeline;

    // Pointer-matched MTLBuffer cache for the codeword buffer.
    // The RS kernel reads bytes 0-3 and writes byte 4 of every codeword in-place.
    // On Apple Silicon unified memory, zero-copy buffers give direct GPU access to
    // the caller's memory — no copy in or out is needed on the common path.
    void*         last_cw_ptr  = nullptr;
    size_t        last_cw_size = 0;
    id<MTLBuffer> cached_cw    = nil;

    // Returns an MTLBuffer wrapping the codeword span.
    // Zero-copy (page-aligned) buffers are cached; copy-backed buffers are not.
    // For in-place operation the caller's data must be in the buffer before
    // dispatch, so copy-backed paths memcpy in; after dispatch the result must
    // be copied back (checked by comparing [buf contents] to the original ptr).
    id<MTLBuffer> get_cw(void* ptr, size_t sz) {
        if (ptr == last_cw_ptr && sz == last_cw_size && cached_cw != nil)
            return cached_cw;
        id<MTLBuffer> buf = make_zero_copy(ptr, sz);
        if (buf) {
            cached_cw    = buf;
            last_cw_ptr  = ptr;
            last_cw_size = sz;
        } else {
            // Not page-aligned: allocate a fresh shared buffer and copy data in
            // so the GPU can read bytes 0-3.  After dispatch, caller copies back.
            cached_cw    = nil;
            last_cw_ptr  = nullptr;
            last_cw_size = 0;
            buf = [device newBufferWithLength:sz options:MTLResourceStorageModeShared];
            std::memcpy([buf contents], ptr, sz);
        }
        return buf;
    }

    static RsMetalCtx& get() {
        static RsMetalCtx ctx = init();
        return ctx;
    }

private:
    id<MTLBuffer> make_zero_copy(const void* ptr, size_t sz) const {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        if ((addr & 0xFFFu) != 0 || (sz & 0xFFFu) != 0) return nil;
        return [device newBufferWithBytesNoCopy:(void*)ptr
                                         length:sz
                                        options:MTLResourceStorageModeShared
                                     deallocator:nil];
    }

    static RsMetalCtx init() {
        RsMetalCtx ctx;
        @autoreleasepool {
            ctx.device = MTLCreateSystemDefaultDevice();
            if (!ctx.device)
                throw std::runtime_error("rs_encode_metal: no Metal device");

            NSError* err = nil;
            NSString* src = [NSString stringWithUTF8String:kRsShaderSource];
            id<MTLLibrary> lib = [ctx.device
                newLibraryWithSource:src options:nil error:&err];
            if (!lib)
                throw std::runtime_error(
                    std::string("rs_encode_metal: shader compile failed: ")
                    + [[err localizedDescription] UTF8String]);

            id<MTLFunction> fn = [lib newFunctionWithName:@"rs_encode_gpu"];
            if (!fn)
                throw std::runtime_error(
                    "rs_encode_metal: kernel 'rs_encode_gpu' not found");

            ctx.pipeline = [ctx.device
                newComputePipelineStateWithFunction:fn error:&err];
            if (!ctx.pipeline)
                throw std::runtime_error(
                    std::string("rs_encode_metal: pipeline creation failed: ")
                    + [[err localizedDescription] UTF8String]);

            ctx.queue = [ctx.device newCommandQueue];
            if (!ctx.queue)
                throw std::runtime_error(
                    "rs_encode_metal: command queue creation failed");
        }
        return ctx;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// rs_encode_metal — public entry point
// ---------------------------------------------------------------------------
namespace fec {

void rs_encode_metal(std::span<uint8_t> codewords, const FecBlockParams& params)
{
    assert(codewords.size() == params.codeword_buf_size());

    @autoreleasepool {
        RsMetalCtx& ctx = RsMetalCtx::get();
        const uint32_t num_words = params.num_words();

        id<MTLBuffer> cw_buf = ctx.get_cw(codewords.data(), codewords.size());

        id<MTLCommandBuffer> cmd =
            [ctx.queue commandBufferWithUnretainedReferences];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];

        [enc setComputePipelineState:ctx.pipeline];
        [enc setBuffer:cw_buf  offset:0 atIndex:0];
        [enc setBytes:&num_words length:sizeof(num_words) atIndex:1];

        // One thread per codeword.
        NSUInteger tpg = ctx.pipeline.maxTotalThreadsPerThreadgroup;
        if (tpg > 256) tpg = 256;
        [enc dispatchThreads:MTLSizeMake(num_words, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(tpg, 1, 1)];
        [enc endEncoding];

        [cmd commit];
        [cmd waitUntilCompleted];

        // Zero-copy: GPU wrote directly into caller's memory — nothing to do.
        // Copy-backed: copy the result (including byte 4) back to the caller.
        if ([cw_buf contents] != (void*)codewords.data())
            std::memcpy(codewords.data(), [cw_buf contents], codewords.size());
    }
}

// ---------------------------------------------------------------------------
// rs_encode_metal_gpu_us — returns GPU kernel execution time in microseconds.
// Fills parity bytes exactly once; intended for benchmarking.
// The codeword buffer must already contain valid interleaved data (bytes 0-3).
// ---------------------------------------------------------------------------
double rs_encode_metal_gpu_us(std::span<uint8_t> codewords, const FecBlockParams& params)
{
    assert(codewords.size() == params.codeword_buf_size());

    double gpu_us = 0.0;
    @autoreleasepool {
        RsMetalCtx& ctx = RsMetalCtx::get();
        const uint32_t num_words = params.num_words();

        // Always allocate a fresh shared buffer to isolate GPU kernel time.
        id<MTLBuffer> cw_buf = [ctx.device
            newBufferWithBytes:codewords.data()
            length:codewords.size()
            options:MTLResourceStorageModeShared];

        id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];

        [enc setComputePipelineState:ctx.pipeline];
        [enc setBuffer:cw_buf  offset:0 atIndex:0];
        [enc setBytes:&num_words length:sizeof(num_words) atIndex:1];

        NSUInteger tpg = ctx.pipeline.maxTotalThreadsPerThreadgroup;
        if (tpg > 256) tpg = 256;
        [enc dispatchThreads:MTLSizeMake(num_words, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(tpg, 1, 1)];
        [enc endEncoding];

        [cmd commit];
        [cmd waitUntilCompleted];

        gpu_us = ([cmd GPUEndTime] - [cmd GPUStartTime]) * 1e6;

        std::memcpy(codewords.data(), [cw_buf contents], codewords.size());
    }
    return gpu_us;
}

} // namespace fec
