// profile_fused.cpp — micro-benchmark for interleave_rs_encode_avx2.
//
// Three measurement modes:
//
//  1. WALL  — overall wall-clock throughput (warm-up + N iterations).
//  2. RDTSC — per-phase cycle counts inserted by splitting the fused function
//             into three named phases timed with __rdtsc():
//               Phase A: gather (stride-N segment load into g0..g3)
//               Phase B: movemask + packed fill (interleave) + AVX2 transpose
//               Phase C: GF multiply-accumulate (interleaved exp/zm) + parity scatter
//  3. PERF  — intended for external `perf stat` invocation; runs a long
//             single-function loop so perf can attribute samples cleanly.
//
// Build (already handled by CMakeLists.txt; add profile_fused to targets):
//   cmake --build build_avx2 --target profile_fused
//
// Usage:
//   ./profile_fused            — modes 1 + 2
//   perf stat -e cycles,instructions,L1-dcache-load-misses,cache-misses
//     ./profile_fused perf     — mode 3 (long loop for perf sampling)

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <string>
#include <cassert>
#include <immintrin.h>

#include "fec/fec_block.h"
#include "fec/interleaver.h"
#include "fec/rs_encode.h"

// ---------------------------------------------------------------------------
// GF(16) tables (duplicated here so the phased version is self-contained)
// ---------------------------------------------------------------------------
static constexpr uint8_t p_gf_log[16]    = { 1,2,4,8,3,6,12,11,5,10,7,14,15,13,9,0 };
static constexpr uint8_t p_gf_exp[16]    = { 15,0,1,4,2,8,5,10,3,14,9,7,6,13,11,12 };
static constexpr uint8_t p_coeff0_exp[8] = { 11, 5, 11, 14, 10, 0, 12, 5 };
static constexpr uint8_t p_coeff1_exp[8] = {  8,14,  2, 13,  3, 0,  8, 3 };

static inline __m256i p_gf_mul(__m256i log_tbl, __m256i data_exp, __m256i zm,
                                uint8_t coeff_exp, __m256i fifteen)
{
    const __m256i coeff = _mm256_set1_epi8(static_cast<int8_t>(coeff_exp));
    const __m256i sum   = _mm256_add_epi8(data_exp, coeff);
    const __m256i ge15  = _mm256_cmpgt_epi8(sum, _mm256_set1_epi8(14));
    const __m256i mod   = _mm256_sub_epi8(sum, _mm256_and_si256(ge15, fifteen));
    return _mm256_andnot_si256(zm, _mm256_shuffle_epi8(log_tbl, mod));
}

// ---------------------------------------------------------------------------
// Phased version: same algorithm as interleave_rs_encode_avx2 but with
// __rdtsc() fences around each of the three phases.
//
// Returns {cycles_A, cycles_B, cycles_C} summed over all outer iterations.
// ---------------------------------------------------------------------------
struct PhaseCycles { uint64_t A, B, C; };

static PhaseCycles run_phased(const uint8_t* segments, uint8_t* codewords,
                               const fec::FecBlockParams& p)
{
    const uint32_t ch = p.col_height;

    const __m256i rev4 = _mm256_setr_epi8(
         3,  2,  1,  0,   7,  6,  5,  4,  11, 10,  9,  8,  15, 14, 13, 12,
         3,  2,  1,  0,   7,  6,  5,  4,  11, 10,  9,  8,  15, 14, 13, 12);

    const __m256i exp_tbl = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(p_gf_exp)));
    const __m256i log_tbl = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(p_gf_log)));
    const __m256i zero            = _mm256_setzero_si256();
    const __m256i nib             = _mm256_set1_epi8(0x0F);
    const __m256i fifteen         = _mm256_set1_epi8(15);
    const __m256i perm_interleave = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
    const __m256i mask_lo         = _mm256_set1_epi32(0xFF);

    uint64_t cyc_A = 0, cyc_B = 0, cyc_C = 0;

    for (uint32_t byte_idx = 0; byte_idx + 3 < ch; byte_idx += 4) {

        // ---- Phase A: gather ----
        uint64_t t0 = __rdtsc();
        alignas(32) uint8_t g0[32], g1[32], g2[32], g3[32];
        for (uint32_t s = 0; s < 32u; ++s) {
            const uint32_t off = s * ch + byte_idx;
            _mm_prefetch(reinterpret_cast<const char*>(&segments[off + 4]), _MM_HINT_T0);
            g0[s] = segments[off];
            g1[s] = segments[off + 1];
            g2[s] = segments[off + 2];
            g3[s] = segments[off + 3];
        }
        const __m256i d0 = _mm256_shuffle_epi8(_mm256_load_si256(reinterpret_cast<const __m256i*>(g0)), rev4);
        const __m256i d1 = _mm256_shuffle_epi8(_mm256_load_si256(reinterpret_cast<const __m256i*>(g1)), rev4);
        const __m256i d2 = _mm256_shuffle_epi8(_mm256_load_si256(reinterpret_cast<const __m256i*>(g2)), rev4);
        const __m256i d3 = _mm256_shuffle_epi8(_mm256_load_si256(reinterpret_cast<const __m256i*>(g3)), rev4);
        uint64_t t1 = __rdtsc();
        cyc_A += t1 - t0;

        // ---- Phase B: movemask + packed write + pk_arr fill + AVX2 transpose ----
        alignas(32) uint32_t pk_arr[32];
        uint8_t* const cw_base = codewords + size_t{byte_idx} * 40u;

        t0 = __rdtsc();
        for (uint32_t sh = 0; sh < 8u; ++sh) {
            const int ishift = static_cast<int>(sh);
            auto nibswap = [](uint32_t pl) {
                return ((pl & 0x0F0F0F0Fu) << 4) | ((pl >> 4) & 0x0F0F0F0Fu);
            };
            const uint32_t pk0 = nibswap(static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_slli_epi16(d0, ishift))));
            const uint32_t pk1 = nibswap(static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_slli_epi16(d1, ishift))));
            const uint32_t pk2 = nibswap(static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_slli_epi16(d2, ishift))));
            const uint32_t pk3 = nibswap(static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_slli_epi16(d3, ishift))));

            const uint32_t cw_off = sh * 5u;
            std::memcpy(cw_base + 0u * 40u + cw_off, &pk0, 4);
            std::memcpy(cw_base + 1u * 40u + cw_off, &pk1, 4);
            std::memcpy(cw_base + 2u * 40u + cw_off, &pk2, 4);
            std::memcpy(cw_base + 3u * 40u + cw_off, &pk3, 4);

            pk_arr[0u * 8u + sh] = pk0;
            pk_arr[1u * 8u + sh] = pk1;
            pk_arr[2u * 8u + sh] = pk2;
            pk_arr[3u * 8u + sh] = pk3;
        }

        // AVX2 transpose: pk_arr[4×8 uint32_t] → rb0..rb3 YMM registers.
        const __m256i va = _mm256_load_si256(reinterpret_cast<const __m256i*>(pk_arr));
        const __m256i vb = _mm256_load_si256(reinterpret_cast<const __m256i*>(pk_arr + 8));
        const __m256i vc = _mm256_load_si256(reinterpret_cast<const __m256i*>(pk_arr + 16));
        const __m256i vd = _mm256_load_si256(reinterpret_cast<const __m256i*>(pk_arr + 24));

        auto xpose = [&](__m256i a, __m256i b, __m256i c, __m256i d, int pos) -> __m256i {
            const int sh32 = pos * 8;
            a = _mm256_and_si256(_mm256_srli_epi32(a, sh32), mask_lo);
            b = _mm256_and_si256(_mm256_srli_epi32(b, sh32), mask_lo);
            c = _mm256_and_si256(_mm256_srli_epi32(c, sh32), mask_lo);
            d = _mm256_and_si256(_mm256_srli_epi32(d, sh32), mask_lo);
            const __m256i w01 = _mm256_packus_epi32(a, b);
            const __m256i w23 = _mm256_packus_epi32(c, d);
            return _mm256_permutevar8x32_epi32(_mm256_packus_epi16(w01, w23), perm_interleave);
        };

        const __m256i rb0 = xpose(va, vb, vc, vd, 0);
        const __m256i rb1 = xpose(va, vb, vc, vd, 1);
        const __m256i rb2 = xpose(va, vb, vc, vd, 2);
        const __m256i rb3 = xpose(va, vb, vc, vd, 3);
        t1 = __rdtsc();
        cyc_B += t1 - t0;

        // ---- Phase C: RS GF math (interleaved exp/zm) + parity scatter ----
        t0 = __rdtsc();
        auto gmul = [&](__m256i de, __m256i zm, uint8_t ce) {
            return p_gf_mul(log_tbl, de, zm, ce, fifteen);
        };

        __m256i acc0, acc1;
        {
            const __m256i h = _mm256_and_si256(_mm256_srli_epi16(rb0, 4), nib);
            const __m256i l = _mm256_and_si256(rb0, nib);
            const __m256i eh = _mm256_shuffle_epi8(exp_tbl, h), zmh = _mm256_cmpeq_epi8(h, zero);
            const __m256i el = _mm256_shuffle_epi8(exp_tbl, l), zml = _mm256_cmpeq_epi8(l, zero);
            acc0 =                        gmul(eh, zmh, 11);
            acc0 = _mm256_xor_si256(acc0, gmul(el, zml,  5));
            acc1 =                        gmul(eh, zmh,  8);
            acc1 = _mm256_xor_si256(acc1, gmul(el, zml, 14));
        }
        {
            const __m256i h = _mm256_and_si256(_mm256_srli_epi16(rb1, 4), nib);
            const __m256i l = _mm256_and_si256(rb1, nib);
            const __m256i eh = _mm256_shuffle_epi8(exp_tbl, h), zmh = _mm256_cmpeq_epi8(h, zero);
            const __m256i el = _mm256_shuffle_epi8(exp_tbl, l), zml = _mm256_cmpeq_epi8(l, zero);
            acc0 = _mm256_xor_si256(acc0, gmul(eh, zmh, 11));
            acc0 = _mm256_xor_si256(acc0, gmul(el, zml, 14));
            acc1 = _mm256_xor_si256(acc1, gmul(eh, zmh,  2));
            acc1 = _mm256_xor_si256(acc1, gmul(el, zml, 13));
        }
        {
            const __m256i h = _mm256_and_si256(_mm256_srli_epi16(rb2, 4), nib);
            const __m256i l = _mm256_and_si256(rb2, nib);
            const __m256i eh = _mm256_shuffle_epi8(exp_tbl, h), zmh = _mm256_cmpeq_epi8(h, zero);
            const __m256i el = _mm256_shuffle_epi8(exp_tbl, l), zml = _mm256_cmpeq_epi8(l, zero);
            acc0 = _mm256_xor_si256(acc0, gmul(eh, zmh, 10));
            acc0 = _mm256_xor_si256(acc0, gmul(el, zml,  0));
            acc1 = _mm256_xor_si256(acc1, gmul(eh, zmh,  3));
            acc1 = _mm256_xor_si256(acc1, gmul(el, zml,  0));
        }
        {
            const __m256i h = _mm256_and_si256(_mm256_srli_epi16(rb3, 4), nib);
            const __m256i l = _mm256_and_si256(rb3, nib);
            const __m256i eh = _mm256_shuffle_epi8(exp_tbl, h), zmh = _mm256_cmpeq_epi8(h, zero);
            const __m256i el = _mm256_shuffle_epi8(exp_tbl, l), zml = _mm256_cmpeq_epi8(l, zero);
            acc0 = _mm256_xor_si256(acc0, gmul(eh, zmh, 12));
            acc0 = _mm256_xor_si256(acc0, gmul(el, zml,  5));
            acc1 = _mm256_xor_si256(acc1, gmul(eh, zmh,  8));
            acc1 = _mm256_xor_si256(acc1, gmul(el, zml,  3));
        }

        const __m256i par = _mm256_or_si256(
            _mm256_slli_epi16(_mm256_and_si256(acc0, nib), 4),
            _mm256_and_si256(acc1, nib));

        alignas(32) uint8_t parity[32];
        _mm256_store_si256(reinterpret_cast<__m256i*>(parity), par);
        for (int i = 0; i < 32; ++i) cw_base[i * 5 + 4] = parity[i];
        t1 = __rdtsc();
        cyc_C += t1 - t0;
    }

    return { cyc_A, cyc_B, cyc_C };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
template<typename Fn>
static double bench_ms(Fn fn, int iters) {
    fn();
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) fn();
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count() / iters;
}

static double mibs(double ms) { return (8192.0 * 32) / (ms * 1e-3) / (1 << 20); }
static double gbits(double ms) { return mibs(ms) * 8.0 / 1024.0; }

int main(int argc, char** argv)
{
    const bool perf_mode = (argc > 1 && std::string(argv[1]) == "perf");

    fec::FecBlockParams p;
    p.col_height = 8192;

    std::vector<uint8_t> src(p.segment_buf_size());
    std::vector<uint8_t> cw(p.codeword_buf_size(), 0);

    for (size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<uint8_t>(i * 37u + 91u);

    // ---- Mode 3: long loop for perf record/stat ----
    if (perf_mode) {
        std::printf("perf mode: running 5000 iterations of interleave_rs_encode_avx2...\n");
        for (int i = 0; i < 5000; ++i) {
            fec::interleave_rs_encode_avx2(src, cw, p);
            // prevent dead-code elimination
            __asm__ volatile("" : : "m"(cw[0]) : "memory");
        }
        return 0;
    }

    static constexpr int ITERS = 200;

    // ---- Mode 1: wall-clock throughput ----
    std::printf("=== Wall-clock throughput (col_height=%u, %d iters) ===\n\n",
                p.col_height, ITERS);
    std::printf("  %-32s %8s %10s %9s\n", "Implementation", "ms/iter", "MiB/s", "Gbit/s");

    double ms_scalar = bench_ms([&]{
        fec::interleave(src, cw, p);
        fec::rs_encode(cw, p);
    }, ITERS);
    std::printf("  %-32s %8.2f %10.1f %9.3f\n", "scalar+scalar",
                ms_scalar, mibs(ms_scalar), gbits(ms_scalar));

    double ms_avx2 = bench_ms([&]{
        fec::interleave_avx2(src, cw, p);
        fec::rs_encode_avx2(cw, p);
    }, ITERS);
    std::printf("  %-32s %8.2f %10.1f %9.3f   (%.1fx scalar)\n", "avx2+avx2",
                ms_avx2, mibs(ms_avx2), gbits(ms_avx2), ms_scalar / ms_avx2);

    double ms_fused = bench_ms([&]{
        fec::interleave_rs_encode_avx2(src, cw, p);
    }, ITERS);
    std::printf("  %-32s %8.2f %10.1f %9.3f   (%.1fx scalar, %.1fx avx2+avx2)\n\n",
                "avx2 fused", ms_fused, mibs(ms_fused), gbits(ms_fused),
                ms_scalar / ms_fused, ms_avx2 / ms_fused);

    // ---- Mode 2: per-phase RDTSC breakdown ----
    std::printf("=== Per-phase cycle breakdown (single pass, col_height=%u) ===\n\n",
                p.col_height);

    // Warm up caches.
    run_phased(src.data(), cw.data(), p);
    run_phased(src.data(), cw.data(), p);

    // Accumulate over several passes to reduce noise.
    static constexpr int PHASE_ITERS = 20;
    uint64_t totA = 0, totB = 0, totC = 0;
    for (int i = 0; i < PHASE_ITERS; ++i) {
        auto [a, b, c] = run_phased(src.data(), cw.data(), p);
        totA += a; totB += b; totC += c;
    }
    const uint64_t A = totA / PHASE_ITERS;
    const uint64_t B = totB / PHASE_ITERS;
    const uint64_t C = totC / PHASE_ITERS;
    const uint64_t total = A + B + C;

    // Iterations of the outer loop = col_height / 4 = 2048
    const uint32_t n_outer = p.col_height / 4u;

    std::printf("  %-36s %10s %8s %10s\n", "Phase", "cycles", "%total", "cyc/outer");
    std::printf("  %-36s %10llu %7.1f%% %10.1f\n",
                "A: gather (stride-N segment load)",
                (unsigned long long)A, 100.0 * A / total, (double)A / n_outer);
    std::printf("  %-36s %10llu %7.1f%% %10.1f\n",
                "B: movemask + packed write + AVX2 transpose",
                (unsigned long long)B, 100.0 * B / total, (double)B / n_outer);
    std::printf("  %-36s %10llu %7.1f%% %10.1f\n",
                "C: GF multiply-acc + parity scatter",
                (unsigned long long)C, 100.0 * C / total, (double)C / n_outer);
    std::printf("  %-36s %10llu %8s\n",
                "Total (A+B+C)", (unsigned long long)total, "");

    std::printf("\n  Notes:\n");
    std::printf("    outer iterations : %u  (col_height/4)\n", n_outer);
    std::printf("    codewords/iter   : 32\n");
    std::printf("    cyc/codeword     : %.1f\n", (double)total / (n_outer * 32));

    return 0;
}
