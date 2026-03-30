// stream_bench.cpp — 100-block streaming FEC benchmark
//
// CPU implementations: iterate sequentially over N_BLOCKS independent FEC blocks.
// Metal GPU: single batched dispatch covering all N_BLOCKS blocks at once.
//
// GPU batching strategy:
//   The interleave kernel processes one byte_idx per thread, entirely
//   independently of all other threads.  Batching N_BLOCKS together is
//   achieved by building a "super-buffer" with col_height_total = N_BLOCKS *
//   col_height and passing it to the existing kernel with that enlarged
//   col_height.  Thread gid = blk * col_height + byte_idx handles block blk.
//
//   Super-buffer segment layout:
//     super_seg[s * col_height_total + blk * col_height + b]
//       = block[blk].segments[s * col_height + b]
//
//   Super-buffer codeword output layout (from kernel):
//     super_cw[blk * col_height * 40 + byte_idx * 40 + word_off]
//       = block[blk].codewords[byte_idx * 40 + word_off]
//
// This amortizes the fixed ~0.4 ms Metal dispatch overhead over N_BLOCKS,
// bringing wall-clock throughput much closer to the GPU kernel limit.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>
#include <chrono>

#include "fec/fec_block.h"
#include "fec/interleaver.h"
#include "fec/deinterleaver.h"
#include "fec/rs_encode.h"

static constexpr int N_BLOCKS = 200;

template<typename Fn>
static double bench_ms(Fn fn, int iters) {
    fn();  // warm-up
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) fn();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / iters;
}

int main() {
    std::printf("FEC %d-Block Streaming Benchmark\n", N_BLOCKS);
    std::printf("col_height = 8192 per block, %d blocks total\n\n", N_BLOCKS);

    fec::FecBlockParams p;
    p.col_height = 8192;

    const uint32_t total_ch = uint32_t{N_BLOCKS} * p.col_height;

    // ----------------------------------------------------------------
    // Allocate CPU per-block buffers (100 separate segments + codeword bufs).
    // ----------------------------------------------------------------
    std::vector<std::vector<uint8_t>> cpu_src(N_BLOCKS,
        std::vector<uint8_t>(p.segment_buf_size()));
    std::vector<std::vector<uint8_t>> cpu_ref(N_BLOCKS,
        std::vector<uint8_t>(p.codeword_buf_size(), 0));
    std::vector<std::vector<uint8_t>> cpu_dst(N_BLOCKS,
        std::vector<uint8_t>(p.codeword_buf_size(), 0));

    // Fill each block with a distinct-but-deterministic pattern.
    for (int blk = 0; blk < N_BLOCKS; ++blk) {
        for (size_t i = 0; i < cpu_src[blk].size(); ++i)
            cpu_src[blk][i] = static_cast<uint8_t>((i + size_t(blk) * 17u) * 37u + 91u);
    }

    // Build scalar reference output (used for correctness checks below).
    for (int blk = 0; blk < N_BLOCKS; ++blk)
        fec::interleave(cpu_src[blk], cpu_ref[blk], p);

    // RS-encoded scalar reference (interleave + rs_encode per block).
    std::vector<std::vector<uint8_t>> cpu_rs_ref(N_BLOCKS,
        std::vector<uint8_t>(p.codeword_buf_size(), 0));
    for (int blk = 0; blk < N_BLOCKS; ++blk) {
        cpu_rs_ref[blk] = cpu_ref[blk];  // copy interleaved data
        fec::rs_encode(cpu_rs_ref[blk], p);
    }

    // ----------------------------------------------------------------
    // Build GPU super-buffer from per-block CPU source data.
    //
    // For each segment s, copy block blk's bytes to:
    //   super_seg[s * total_ch + blk * col_height .. +col_height-1]
    // ----------------------------------------------------------------
    const size_t super_seg_sz = size_t{fec::FecBlockParams::NUM_SEGMENTS} * total_ch;
    const size_t super_cw_sz  = size_t{total_ch} * 8u * fec::FecBlockParams::CODEWORD_BYTES;

    std::vector<uint8_t> gpu_seg(super_seg_sz);
    std::vector<uint8_t> gpu_cw(super_cw_sz, 0);

    for (int blk = 0; blk < N_BLOCKS; ++blk) {
        for (uint32_t s = 0; s < fec::FecBlockParams::NUM_SEGMENTS; ++s) {
            const uint8_t* src = cpu_src[blk].data() + size_t{s} * p.col_height;
            uint8_t*       dst = gpu_seg.data()       + size_t{s} * total_ch
                                                      + static_cast<size_t>(blk) * p.col_height;
            std::memcpy(dst, src, p.col_height);
        }
    }

    // ----------------------------------------------------------------
    // Batch correctness checks: interleave + rs_encode combined pipeline.
    // Each test runs the full pipeline on the super-buffer, then extracts
    // per-block codeword slices and compares all 5 bytes (data + parity)
    // against the per-block scalar reference (cpu_rs_ref).
    // ----------------------------------------------------------------
    std::printf("Batch pipeline correctness (%d blocks, col_height=%u)\n\n",
                N_BLOCKS, p.col_height);
    std::printf("  %-48s %s\n", "Test", "Result");
    std::printf("  %s\n", std::string(58, '-').c_str());

    // Helper: check all codeword bytes (data + parity) for every block.
    auto check_all = [&](const uint8_t* super_cw, const char* tag) {
        bool ok = true;
        for (int blk = 0; blk < N_BLOCKS && ok; ++blk) {
            const uint8_t* blk_cw =
                super_cw + static_cast<size_t>(blk) * p.codeword_buf_size();
            if (std::memcmp(blk_cw, cpu_rs_ref[blk].data(),
                            p.codeword_buf_size()) != 0)
                ok = false;
        }
        std::printf("  %-48s %s\n", tag, ok ? "PASS" : "FAIL");
    };

#if defined(__ARM_NEON)
    // NEON per-block loop: interleave_neon + rs_encode_neon on each block.
    {
        std::vector<uint8_t> neon_cw(p.codeword_buf_size() * N_BLOCKS, 0u);
        for (int blk = 0; blk < N_BLOCKS; ++blk) {
            uint8_t* blk_cw = neon_cw.data()
                + static_cast<size_t>(blk) * p.codeword_buf_size();
            std::span<uint8_t> sp{blk_cw, p.codeword_buf_size()};
            fec::interleave_neon(cpu_src[blk], sp, p);
            fec::rs_encode_neon(sp, p);
        }
        check_all(neon_cw.data(),
                  "interleave_neon + rs_encode_neon (per-block loop)");
    }
#endif // __ARM_NEON

#if defined(__APPLE__)
    // Metal batch: single super-buffer dispatch for interleave then encode.
    {
        fec::FecBlockParams p_batch;
        p_batch.col_height = total_ch;
        std::fill(gpu_cw.begin(), gpu_cw.end(), 0u);
        fec::interleave_metal(gpu_seg, gpu_cw, p_batch);
        fec::rs_encode_metal(gpu_cw, p_batch);
        check_all(gpu_cw.data(),
                  "interleave_metal + rs_encode_metal (super-buffer batch)");
    }
    std::fill(gpu_cw.begin(), gpu_cw.end(), 0u);
#endif // __APPLE__

    std::printf("\n");

    // ----------------------------------------------------------------
    // Benchmark
    // ----------------------------------------------------------------
    const int ITERS = 20;
    const double total_seg_bytes = double(p.segment_buf_size()) * N_BLOCKS;
    auto gbits = [&](double ms) { return total_seg_bytes * 8.0 / (ms * 1e6); };
    auto mibs  = [&](double ms) {
        return (total_seg_bytes / (1024.0 * 1024.0)) / (ms / 1000.0);
    };

    // ---- CPU: scalar loop ----
    double ms_scalar = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk)
            fec::interleave(cpu_src[blk], cpu_dst[blk], p);
    }, ITERS);

    // ---- CPU: magic-multiply loop ----
    double ms_magic = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk)
            fec::interleave_magic(cpu_src[blk], cpu_dst[blk], p);
    }, ITERS);

    // ---- CPU: deinterleave scalar loop (uses cpu_ref as input) ----
    double ms_dil = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk)
            fec::deinterleave(cpu_ref[blk], cpu_dst[blk], p);
    }, ITERS);

    // ---- CPU: scalar RS encoder loop ----
    // cpu_ref already holds interleaved data from the scalar reference build above.
    double ms_rs_scalar = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk)
            fec::rs_encode(cpu_ref[blk], p);
    }, ITERS);

#if defined(__ARM_NEON)
    // ---- CPU: NEON interleave loop ----
    double ms_neon_il = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk)
            fec::interleave_neon(cpu_src[blk], cpu_dst[blk], p);
    }, ITERS);

    // ---- CPU: NEON deinterleave loop ----
    double ms_neon_dil = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk)
            fec::deinterleave_neon(cpu_ref[blk], cpu_dst[blk], p);
    }, ITERS);

    // ---- CPU: NEON RS encoder loop ----
    // Pre-interleave all blocks so codeword buffers have valid data bytes 0-3.
    for (int blk = 0; blk < N_BLOCKS; ++blk)
        fec::interleave(cpu_src[blk], cpu_dst[blk], p);
    double ms_neon_rs = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk)
            fec::rs_encode_neon(cpu_dst[blk], p);
    }, ITERS);
#endif

    // ---- CPU: combined scalar pipeline loop ----
    double ms_combined_scalar = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk) {
            fec::interleave(cpu_src[blk], cpu_dst[blk], p);
            fec::rs_encode(cpu_dst[blk], p);
        }
    }, ITERS);

#if defined(__ARM_NEON)
    // ---- CPU: combined NEON pipeline loop ----
    double ms_combined_neon = bench_ms([&] {
        for (int blk = 0; blk < N_BLOCKS; ++blk) {
            fec::interleave_neon(cpu_src[blk], cpu_dst[blk], p);
            fec::rs_encode_neon(cpu_dst[blk], p);
        }
    }, ITERS);
#endif

#if defined(__APPLE__)
    fec::FecBlockParams p_batch;
    p_batch.col_height = total_ch;

    // ---- Metal GPU: batched wall-clock ----
    double ms_metal_wall = bench_ms([&] {
        fec::interleave_metal(gpu_seg, gpu_cw, p_batch);
    }, ITERS);

    // ---- Metal GPU: kernel-only time (single call after warm-up) ----
    double gpu_us = fec::interleave_metal_gpu_us(gpu_seg, gpu_cw, p_batch);

    // ---- Metal RS encoder: batched wall-clock ----
    // Ensure gpu_cw has valid interleaved data before encoding.
    fec::interleave_metal(gpu_seg, gpu_cw, p_batch);
    double ms_rs_metal_wall = bench_ms([&] {
        fec::rs_encode_metal(gpu_cw, p_batch);
    }, ITERS);

    // ---- Metal RS encoder: kernel-only time ----
    std::vector<uint8_t> gpu_cw_rs(super_cw_sz, 0);
    fec::interleave_metal(gpu_seg, gpu_cw_rs, p_batch);
    double rs_gpu_us = fec::rs_encode_metal_gpu_us(gpu_cw_rs, p_batch);

    // ---- Metal GPU: combined interleave + encode, wall-clock ----
    double ms_combined_metal = bench_ms([&] {
        fec::interleave_metal(gpu_seg, gpu_cw, p_batch);
        fec::rs_encode_metal(gpu_cw, p_batch);
    }, ITERS);
#endif

    // ----------------------------------------------------------------
    // Print results
    // ----------------------------------------------------------------
    char lbl[64];
    std::printf("  %-34s %8s %10s %9s\n",
                "Implementation", "ms/iter", "MiB/s", "Gbit/s");
    std::printf("  %s\n", std::string(72, '-').c_str());

    std::snprintf(lbl, sizeof(lbl), "interleave scalar (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f\n",
                lbl, ms_scalar, mibs(ms_scalar), gbits(ms_scalar));
    std::snprintf(lbl, sizeof(lbl), "interleave magic  (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                lbl, ms_magic, mibs(ms_magic), gbits(ms_magic), ms_scalar / ms_magic);
    std::snprintf(lbl, sizeof(lbl), "deinterleave scalar (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f\n",
                lbl, ms_dil, mibs(ms_dil), gbits(ms_dil));

#if defined(__ARM_NEON)
    std::snprintf(lbl, sizeof(lbl), "interleave NEON   (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                lbl, ms_neon_il, mibs(ms_neon_il), gbits(ms_neon_il),
                ms_scalar / ms_neon_il);
    std::snprintf(lbl, sizeof(lbl), "deinterleave NEON (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                lbl, ms_neon_dil, mibs(ms_neon_dil), gbits(ms_neon_dil),
                ms_dil / ms_neon_dil);
#endif

    std::snprintf(lbl, sizeof(lbl), "rs_encode scalar  (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f\n",
                lbl, ms_rs_scalar, mibs(ms_rs_scalar), gbits(ms_rs_scalar));

#if defined(__ARM_NEON)
    std::snprintf(lbl, sizeof(lbl), "rs_encode NEON    (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                lbl, ms_neon_rs, mibs(ms_neon_rs), gbits(ms_neon_rs),
                ms_rs_scalar / ms_neon_rs);
#endif

#if defined(__APPLE__)
    std::printf("  %-34s %8.2f %10.1f %9.3f   (%.1fx scalar, %.1fx NEON, wall)\n",
                "interleave Metal  (batch)", ms_metal_wall, mibs(ms_metal_wall),
                gbits(ms_metal_wall), ms_scalar / ms_metal_wall,
#if defined(__ARM_NEON)
                ms_neon_il / ms_metal_wall
#else
                ms_magic / ms_metal_wall
#endif
    );
    std::printf("  %-34s %8.2f %10.1f %9.3f   (GPU kernel only)\n",
                "interleave Metal  (GPU-t)", gpu_us / 1000.0,
                mibs(gpu_us / 1000.0), gbits(gpu_us / 1000.0));
    std::printf("\n");
    std::printf("  Metal overhead per batch: %.3f ms  (%.1f%% of wall time)\n",
                ms_metal_wall - gpu_us / 1000.0,
                100.0 * (ms_metal_wall - gpu_us / 1000.0) / ms_metal_wall);
    std::printf("\n");
    std::printf("  %-34s %8.2f %10.1f %9.3f   (%.1fx scalar, wall)\n",
                "rs_encode Metal  (batch)", ms_rs_metal_wall, mibs(ms_rs_metal_wall),
                gbits(ms_rs_metal_wall), ms_scalar / ms_rs_metal_wall);
    std::printf("  %-34s %8.2f %10.1f %9.3f   (GPU kernel only)\n",
                "rs_encode Metal  (GPU-t)", rs_gpu_us / 1000.0,
                mibs(rs_gpu_us / 1000.0), gbits(rs_gpu_us / 1000.0));
    std::printf("  RS-encode Metal overhead: %.3f ms  (%.1f%% of wall time)\n",
                ms_rs_metal_wall - rs_gpu_us / 1000.0,
                100.0 * (ms_rs_metal_wall - rs_gpu_us / 1000.0) / ms_rs_metal_wall);
#endif

    std::printf("\n");

    // ----------------------------------------------------------------
    // Combined pipeline (interleave + rs_encode together)
    // ----------------------------------------------------------------
    std::printf("  Combined pipeline (interleave + rs_encode)\n");
    std::printf("  %s\n", std::string(72, '-').c_str());

    std::snprintf(lbl, sizeof(lbl), "scalar+scalar     (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f\n",
                lbl, ms_combined_scalar, mibs(ms_combined_scalar),
                gbits(ms_combined_scalar));

#if defined(__ARM_NEON)
    std::snprintf(lbl, sizeof(lbl), "neon+neon         (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f   (%.1fx scalar)\n",
                lbl, ms_combined_neon, mibs(ms_combined_neon),
                gbits(ms_combined_neon), ms_combined_scalar / ms_combined_neon);
#endif

#if defined(__APPLE__)
    std::snprintf(lbl, sizeof(lbl), "metal+metal batch (\xc3\x97%d)", N_BLOCKS);
    std::printf("  %-34s %8.2f %10.1f %9.3f   (%.1fx scalar, wall)\n",
                lbl, ms_combined_metal, mibs(ms_combined_metal),
                gbits(ms_combined_metal), ms_combined_scalar / ms_combined_metal);
#endif

    std::printf("\n");
    return 0;
}
