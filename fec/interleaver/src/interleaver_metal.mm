// interleaver_metal.mm — Metal GPU implementation of interleave_magic
//
// Uses a pointer-matched MTLBuffer cache in the MetalCtx singleton: when the
// same caller span pointer is passed on successive calls, the GPU page-table
// registration from the prior call is reused (saving ~200-400 us of IOKit
// overhead per buffer per dispatch).  Large std::vector allocations on macOS
// come from mmap and are page-aligned, so the zero-copy path is the common
// case.
//
// Each GPU thread handles one byte_idx (one 40-byte output block of 8
// codewords).  The algorithm is identical to interleave_magic on the CPU:
// XOR-3 gather, four uint64 loads, magic-multiply bit-plane extraction,
// nibble-swap, store.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "fec/interleaver.h"

#include <cassert>
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Metal shader source (compiled at first call via newLibraryWithSource:)
// ---------------------------------------------------------------------------
static const char* kShaderSource = R"msl(
#include <metal_stdlib>
using namespace metal;

kernel void interleave_gpu(
    device const uchar* segments  [[buffer(0)]],
    device       uchar* codewords [[buffer(1)]],
    constant     uint&  col_height [[buffer(2)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= col_height) return;

    // Gather one byte from each of the 32 segments with XOR-3 reorder.
    // gathered[s^3] = segments[s] places the LSB-carrying segment at the
    // lowest index within each column group so that after the magic multiply
    // nibbles emerge in natural bit order (no full reversal needed).
    uchar g[32];
    for (uint s = 0u; s < 32u; ++s) {
        g[s ^ 3u] = segments[s * col_height + gid];
    }

    // Pack groups of 8 gathered bytes into ulong (little-endian, matching
    // CPU memcpy semantics: g[i] -> byte 0, g[i+7] -> byte 7).
#define PACK8(i) \
    (  (ulong)g[(i)+0u]          | ((ulong)g[(i)+1u] <<  8u) \
     | ((ulong)g[(i)+2u] << 16u) | ((ulong)g[(i)+3u] << 24u) \
     | ((ulong)g[(i)+4u] << 32u) | ((ulong)g[(i)+5u] << 40u) \
     | ((ulong)g[(i)+6u] << 48u) | ((ulong)g[(i)+7u] << 56u) )

    ulong q0 = PACK8(0u);
    ulong q1 = PACK8(8u);
    ulong q2 = PACK8(16u);
    ulong q3 = PACK8(24u);
#undef PACK8

    // Magic multiply: bit i of ((q * M) >> 56) = bit-0 of byte i of q.
    // Proof: M = sum_{j=0}^7 2^(7j+7); product of b_i (bit 0 of byte i) with
    // term j=7-i lands at bit i+56; all other products fall below bit 56.
    constexpr ulong magic = 0x0102040810204080UL;
    constexpr ulong mask1 = 0x0101010101010101UL;

    device uchar* cw = codewords + (ulong)gid * 40u;

    // Unsigned wraparound sentinel: k = 7,6,...,0 then 0u-1 = UINT_MAX >= 8 -> exit.
    for (uint k = 7u; k < 8u; --k) {
        // Extract bit k from each byte of q0..q3, pack 8 values per uint.
        uint plane =
              (uint)((((q0 >> k) & mask1) * magic) >> 56u)
            | ((uint)((((q1 >> k) & mask1) * magic) >> 56u) <<  8u)
            | ((uint)((((q2 >> k) & mask1) * magic) >> 56u) << 16u)
            | ((uint)((((q3 >> k) & mask1) * magic) >> 56u) << 24u);

        // plane layout: [sym1|sym0] [sym3|sym2] [sym5|sym4] [sym7|sym6]
        // Nibble-swap to codeword byte format: [sym0|sym1] [sym2|sym3] ...
        uint packed = ((plane & 0x0F0F0F0Fu) << 4u)
                    | ((plane >>          4u) & 0x0F0F0F0Fu);

        // Write 4 data bytes + 1 zero parity slot.
        const uint off = (7u - k) * 5u;
        cw[off + 0u] = (uchar)(packed);
        cw[off + 1u] = (uchar)(packed >>  8u);
        cw[off + 2u] = (uchar)(packed >> 16u);
        cw[off + 3u] = (uchar)(packed >> 24u);
        cw[off + 4u] = 0u;
    }
}
)msl";

// ---------------------------------------------------------------------------
// Singleton Metal context — initialized once, reused for every call.
// ---------------------------------------------------------------------------
namespace {

struct MetalCtx {
    id<MTLDevice>              device;
    id<MTLCommandQueue>        queue;
    id<MTLComputePipelineState> pipeline;

    // Pointer-matched MTLBuffer cache.
    // When the same segment/codeword span pointer is passed on successive calls
    // the Metal GPU page-table registration (the dominant IOKit overhead, ~200-400 us
    // per buffer) is reused rather than re-done each call.  Only zero-copy
    // (page-aligned) buffers are cached; copy-backed fallback buffers are
    // recreated per call (they can't be safely reused without knowing whether
    // the caller mutated the source data).
    const void*   last_seg_ptr  = nullptr;
    size_t        last_seg_size = 0;
    id<MTLBuffer> cached_seg    = nil;
    void*         last_cw_ptr   = nullptr;
    size_t        last_cw_size  = 0;
    id<MTLBuffer> cached_cw     = nil;

    // Returns an MTLBuffer for the given input span.
    // Cache hit: same pointer & size → return the cached zero-copy buffer directly.
    //   On Apple Silicon unified memory, any CPU writes to those pages since the
    //   last dispatch are already visible to the GPU — no explicit sync needed.
    // Cache miss: attempt zero-copy; if the pointer is not page-aligned, fall
    //   back to a fresh allocation + memcpy (not cached).
    id<MTLBuffer> get_seg(const void* ptr, size_t sz) {
        if (ptr == last_seg_ptr && sz == last_seg_size && cached_seg != nil)
            return cached_seg;
        id<MTLBuffer> buf = make_zero_copy(ptr, sz);
        if (buf) {
            cached_seg = buf;
            last_seg_ptr  = ptr;
            last_seg_size = sz;
        } else {
            cached_seg    = nil;
            last_seg_ptr  = nullptr;
            last_seg_size = 0;
            buf = [device newBufferWithLength:sz options:MTLResourceStorageModeShared];
            std::memcpy([buf contents], ptr, sz);
        }
        return buf;
    }

    // Returns an MTLBuffer for the codeword output span.
    // Zero-copy buffers are cached; the GPU writes results directly into the
    // caller's memory — no copy-back needed.  Copy-backed buffers are not
    // cached; check [buf contents] != ptr after dispatch to detect them.
    id<MTLBuffer> get_cw(void* ptr, size_t sz) {
        if (ptr == last_cw_ptr && sz == last_cw_size && cached_cw != nil)
            return cached_cw;
        id<MTLBuffer> buf = make_zero_copy(ptr, sz);
        if (buf) {
            cached_cw    = buf;
            last_cw_ptr  = ptr;
            last_cw_size = sz;
        } else {
            cached_cw    = nil;
            last_cw_ptr  = nullptr;
            last_cw_size = 0;
            buf = [device newBufferWithLength:sz options:MTLResourceStorageModeShared];
            // No input memcpy: GPU writes output; caller memcpy's out after.
        }
        return buf;
    }

    static MetalCtx& get() {
        static MetalCtx ctx = init();
        return ctx;
    }

private:
    // Attempts newBufferWithBytesNoCopy (zero copy, just page-table registration).
    // Returns nil if ptr is not page-aligned or sz is not a page multiple.
    // On Apple Silicon vm_page_size=16384; on Intel vm_page_size=4096.  We use
    // 4096 as a conservative lower-bound mask — the API returns nil anyway if
    // the system's actual page size requirement is not met.
    id<MTLBuffer> make_zero_copy(const void* ptr, size_t sz) const {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        if ((addr & 0xFFFu) != 0 || (sz & 0xFFFu) != 0) return nil;
        return [device newBufferWithBytesNoCopy:(void*)ptr
                                         length:sz
                                        options:MTLResourceStorageModeShared
                                     deallocator:nil];
    }

    static MetalCtx init() {
        MetalCtx ctx;
        @autoreleasepool {
            ctx.device = MTLCreateSystemDefaultDevice();
            if (!ctx.device)
                throw std::runtime_error("interleave_metal: no Metal device");

            NSError* err = nil;
            NSString* src = [NSString stringWithUTF8String:kShaderSource];
            id<MTLLibrary> lib = [ctx.device
                newLibraryWithSource:src options:nil error:&err];
            if (!lib)
                throw std::runtime_error(
                    std::string("interleave_metal: shader compile failed: ")
                    + [[err localizedDescription] UTF8String]);

            id<MTLFunction> fn = [lib newFunctionWithName:@"interleave_gpu"];
            if (!fn)
                throw std::runtime_error(
                    "interleave_metal: kernel 'interleave_gpu' not found");

            ctx.pipeline = [ctx.device
                newComputePipelineStateWithFunction:fn error:&err];
            if (!ctx.pipeline)
                throw std::runtime_error(
                    std::string("interleave_metal: pipeline creation failed: ")
                    + [[err localizedDescription] UTF8String]);

            ctx.queue = [ctx.device newCommandQueue];
            if (!ctx.queue)
                throw std::runtime_error(
                    "interleave_metal: command queue creation failed");
        }
        return ctx;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// interleave_metal — public entry point
// ---------------------------------------------------------------------------
namespace fec {

void interleave_metal(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(segments.size()  == p.segment_buf_size());
    assert(codewords.size() == p.codeword_buf_size());

    @autoreleasepool {
        MetalCtx& ctx = MetalCtx::get();
        const uint32_t col_height = p.col_height;

        // Resolve buffers via the pointer-matched cache.
        // For large mmap-backed std::vector allocations (>= ~128 KB on macOS)
        // the caller's memory is page-aligned, so make_zero_copy succeeds and
        // the GPU page-table registration is reused across calls.
        id<MTLBuffer> seg_buf = ctx.get_seg(segments.data(), segments.size());
        id<MTLBuffer> cw_buf  = ctx.get_cw(codewords.data(), codewords.size());

        // commandBufferWithUnretainedReferences: buffer lifetimes are managed by
        // the cache in MetalCtx, so the command buffer need not retain them.
        id<MTLCommandBuffer> cmd =
            [ctx.queue commandBufferWithUnretainedReferences];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];

        [enc setComputePipelineState:ctx.pipeline];
        [enc setBuffer:seg_buf offset:0 atIndex:0];
        [enc setBuffer:cw_buf  offset:0 atIndex:1];
        [enc setBytes:&col_height length:sizeof(col_height) atIndex:2];

        // One thread per byte_idx.
        NSUInteger tpg = ctx.pipeline.maxTotalThreadsPerThreadgroup;
        if (tpg > 256) tpg = 256;
        MTLSize grid    = MTLSizeMake(col_height, 1, 1);
        MTLSize tg_size = MTLSizeMake(tpg, 1, 1);
        [enc dispatchThreads:grid threadsPerThreadgroup:tg_size];
        [enc endEncoding];

        [cmd commit];
        [cmd waitUntilCompleted];

        // If the codeword buffer is zero-copy, the GPU wrote directly into the
        // caller's memory — no copy needed.  Otherwise copy results back.
        if ([cw_buf contents] != (void*)codewords.data())
            std::memcpy(codewords.data(), [cw_buf contents], codewords.size());
    }
}

// ---------------------------------------------------------------------------
// interleave_metal_gpu_us — returns GPU kernel execution time in microseconds.
// Fills the output buffer exactly once; intended for benchmarking.
// ---------------------------------------------------------------------------
double interleave_metal_gpu_us(
    std::span<const uint8_t> segments,
    std::span<uint8_t>       codewords,
    const FecBlockParams&    p)
{
    assert(segments.size()  == p.segment_buf_size());
    assert(codewords.size() == p.codeword_buf_size());

    double gpu_us = 0.0;
    @autoreleasepool {
        MetalCtx& ctx = MetalCtx::get();

        const uint32_t col_height = p.col_height;

        // Always use shared buffers (may copy if non-aligned, but timing is GPU-only).
        MTLResourceOptions opt = MTLResourceStorageModeShared;
        id<MTLBuffer> seg_buf = [ctx.device
            newBufferWithBytes:segments.data()
            length:segments.size()
            options:opt];
        id<MTLBuffer> cw_buf = [ctx.device
            newBufferWithLength:codewords.size()
            options:opt];

        id<MTLCommandBuffer> cmd = [ctx.queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];

        [enc setComputePipelineState:ctx.pipeline];
        [enc setBuffer:seg_buf  offset:0 atIndex:0];
        [enc setBuffer:cw_buf   offset:0 atIndex:1];
        [enc setBytes:&col_height length:sizeof(col_height) atIndex:2];

        NSUInteger tpg = ctx.pipeline.maxTotalThreadsPerThreadgroup;
        if (tpg > 256) tpg = 256;
        [enc dispatchThreads:MTLSizeMake(col_height, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(tpg, 1, 1)];
        [enc endEncoding];

        [cmd commit];
        [cmd waitUntilCompleted];

        // GPUStartTime and GPUEndTime are in seconds (CFAbsoluteTime).
        gpu_us = ([cmd GPUEndTime] - [cmd GPUStartTime]) * 1e6;

        std::memcpy(codewords.data(), [cw_buf contents], codewords.size());
    }
    return gpu_us;
}

} // namespace fec
