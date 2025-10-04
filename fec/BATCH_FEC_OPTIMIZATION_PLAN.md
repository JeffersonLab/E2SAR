# Batch FEC Encoder/Decoder Optimization Plan

## Executive Summary

Optimize the Reed-Solomon FEC encoder/decoder to process multiple frames in batch mode where all frames share the same erasure pattern. This enables amortizing expensive table lookups and matrix operations across N frames.

## Key Insight

When processing multiple frames with the **same erasure pattern**, we can:
- **Encoder**: Amortize GF table loads and matrix coefficient loads across N frames
- **Decoder**: Perform erasure pattern lookup and matrix inversion **ONCE**, then apply to all N frames ← **PRIMARY OPTIMIZATION**

## Current Architecture Analysis

### Current Bottlenecks

**Encoder (per frame):**
- Load GF exp/log tables into NEON (already fast)
- Load encoding matrix coefficients from `rs->Genc_exp`
- Perform matrix-vector multiplication (8 data symbols × 2 parity rows)

**Decoder (per frame):**
- Linear search through decode table (~37 entries) for erasure pattern match (O(n) lookup)
- Apply pre-computed 8×8 inverse matrix (8 GF matrix-vector multiplications)
- Each matrix application involves 8×8=64 GF multiplications + reductions

### Data Flow

**Current Single-Frame Encoder:**
```
Input: 8 nibbles (data) → RS Encoder → Output: 2 nibbles (parity)
1. Load GF tables (exp_seq, log_seq)
2. Load encoding matrix coefficients (Genc_exp)
3. For each parity symbol: matrix-vector mul
```

**Current Single-Frame Decoder:**
```
Input: 10 nibbles (8 data + 2 parity), erasure_pattern → Decoder → Output: 8 nibbles
1. Linear search decode_table for matching erasure pattern
2. Load pre-computed 8×8 inverse matrix
3. Substitute parity for erased data symbols
4. Apply inverse matrix (8 GF dot products)
```

## Proposed Batch Architecture

### 1. Batch Encoder APIs

Process N frames with shared lookup tables and encoding matrices:

```c
// Reference implementation - works on rs_poly_vector arrays
void fast_rs_encode_batch(rs_model *rs, int n_frames,
                          rs_poly_vector **data, rs_poly_vector **parity);

// SIMD-optimized single-nibble encoder
void neon_rs_encode_batch(rs_model *rs, int n_frames,
                          const char **data, char **parity);

// SIMD-optimized dual-nibble encoder (processes full bytes)
void neon_rs_encode_dual_nibble_batch(int n_frames,
                                      const char **data_bytes, char **parity_bytes);
```

**Implementation Strategy:**
- Load GF tables ONCE before processing loop
- Load encoding matrix coefficients ONCE
- Loop through N frames applying the pre-loaded coefficients
- NEON registers stay hot with table data

### 2. Batch Decoder APIs (Primary Optimization)

Decode N frames sharing the same erasure pattern:

```c
// Reference implementation
int rs_decode_table_lookup_batch(rs_decode_table *table, int n_frames,
                                 rs_poly_vector **received,
                                 int *erasure_locations, int num_erasures,
                                 rs_poly_vector **decoded);

// SIMD-optimized decoder
int neon_rs_decode_table_lookup_batch(rs_decode_table *table, int n_frames,
                                      rs_poly_vector **received,
                                      int *erasure_locations, int num_erasures,
                                      rs_poly_vector **decoded);
```

**Implementation Strategy:**
```
1. Perform erasure pattern lookup ONCE (find matching table entry)
2. Load inverse matrix from table entry
3. Load GF tables into NEON registers ONCE
4. FOR each frame in batch:
     a. Substitute parity for erased symbols
     b. Apply pre-loaded inverse matrix
5. Return decoded frames
```

### 3. Erasure Pattern Lookup Optimization

**Current**: O(n) linear search through 37 table entries
**Optimized**: O(1) direct indexing using hash/array lookup

```c
// Enhanced decode table structure
typedef struct {
  rs_decode_table_entry *entries;        // Full table (37 entries)
  int size;
  int capacity;

  // NEW: Direct O(1) lookup structures
  rs_decode_table_entry *pattern_index[8][8];  // Double erasures: pattern_index[e1][e2]
  rs_decode_table_entry *single_erasure[8];     // Single erasures: single_erasure[e]
  rs_decode_table_entry *no_erasure;            // No erasures case
} rs_decode_table;
```

**Lookup Logic:**
```c
rs_decode_table_entry* lookup_pattern(rs_decode_table *table,
                                     int *erasure_locations, int num_erasures) {
  if (num_erasures == 0) {
    return table->no_erasure;
  } else if (num_erasures == 1) {
    return table->single_erasure[erasure_locations[0]];
  } else if (num_erasures == 2) {
    int e1 = erasure_locations[0];
    int e2 = erasure_locations[1];
    if (e1 > e2) { int tmp = e1; e1 = e2; e2 = tmp; }  // normalize order
    return table->pattern_index[e1][e2];
  }
  return NULL;  // Too many erasures
}
```

## Performance Analysis

### Expected Speedups

**For N=100 frames with same erasure pattern:**

**Current decoder:**
- 100 × O(n) table lookups (~37 comparisons each) = 3700 operations
- 100 × matrix applications (64 GF muls each) = 6400 GF muls
- **Total**: ~10,100 operations

**Optimized batch decoder:**
- 1 × O(1) table lookup = 1 operation
- 100 × matrix applications (64 GF muls each) = 6400 GF muls
- **Total**: ~6,401 operations
- **Speedup**: ~1.6× (plus cache benefits from hot data)

**For encoder (N=100):**
- Amortized table loading and coefficient loading
- **Speedup**: ~1.3-1.5× (less dramatic but worthwhile)

### Real-World Impact

Typical E2SAR packet stream scenarios:
- Network burst of 100-1000 packets with consistent erasure pattern
- Current: Process each packet individually
- Optimized: Batch process with shared erasure pattern
- **Expected wall-clock speedup**: 2-3× for decode-heavy workloads

## Implementation Roadmap

### Phase 1: Core Batch Operations
1. ✅ Analyze current data flow and identify bottlenecks
2. ⬜ Design and document batch encoder API
3. ⬜ Implement `fast_rs_encode_batch()` (reference implementation)
4. ⬜ Implement `neon_rs_encode_batch()` (SIMD single-nibble)
5. ⬜ Implement `neon_rs_encode_dual_nibble_batch()` (SIMD dual-nibble)
6. ⬜ Design and document batch decoder API
7. ⬜ Implement `rs_decode_table_lookup_batch()` (reference implementation)
8. ⬜ Implement `neon_rs_decode_table_lookup_batch()` (SIMD optimized)

### Phase 2: Lookup Optimization
9. ⬜ Add O(1) indexing fields to `rs_decode_table` structure
10. ⬜ Update `init_rs_decode_table()` to populate index arrays
11. ⬜ Implement `lookup_pattern()` helper with O(1) access
12. ⬜ Update batch decoder to use optimized lookup

### Phase 3: Testing & Validation
13. ⬜ Create test suite for batch encoder (verify correctness)
14. ⬜ Create test suite for batch decoder (verify correctness)
15. ⬜ Test edge cases (n_frames=1, n_frames=1000, mixed erasure patterns)
16. ⬜ Validate against existing single-frame implementations

### Phase 4: Performance Benchmarking
17. ⬜ Benchmark batch encoder vs N individual encoder calls
18. ⬜ Benchmark batch decoder vs N individual decoder calls
19. ⬜ Measure lookup optimization impact (linear vs O(1))
20. ⬜ Profile cache behavior and memory access patterns
21. ⬜ Generate performance report with throughput numbers (MB/s, Gbps)

## Advanced Future Optimizations (Phase 5)

### Super-Vectorization via Transposition

Process 8 frames simultaneously by transposing the data layout:

**Concept:**
- Current: Each NEON operation processes 8 symbols from 1 frame
- Super-vectorized: Each NEON operation processes 1 symbol from 8 frames
- Requires transposing input data: frames → symbols layout

**Potential Benefit:**
- 5-8× speedup for large batches (N ≥ 8)
- Requires significant code restructuring
- Best for high-throughput packet processing scenarios

**Implementation Complexity:** High (defer to Phase 5)

## File Organization

New/Modified files:
- `src/common/ejfat_rs_batch.h` - Batch encoder/decoder APIs
- `src/neon/ejfat_rs_neon_batch.h` - NEON-optimized batch implementations
- `src/tests/test_batch_enc_dec.c` - Batch operation test suite
- `src/common/ejfat_rs_decoder.h` - Update with O(1) lookup structures

## API Design Principles

1. **Backwards Compatibility**: Existing single-frame APIs remain unchanged
2. **Zero-Copy**: Batch APIs work with arrays of pointers (no data copying)
3. **Flexible Batching**: Support n_frames from 1 to 10,000+
4. **Error Handling**: Return frame-level error codes for partial failures
5. **SIMD-Friendly**: Data layouts optimized for NEON register operations

## Success Metrics

- ✅ Correctness: 100% pass rate on test suite
- ✅ Performance: 2-3× speedup for batch decoder (N=100)
- ✅ Performance: 1.3-1.5× speedup for batch encoder (N=100)
- ✅ Scalability: Linear performance scaling from N=1 to N=1000
- ✅ Code Quality: Clean API, well-documented, maintainable

## Notes

- Focus on decoder optimization first (bigger impact)
- Encoder batch optimization is secondary but complementary
- O(1) lookup optimization applies to both single-frame and batch decoders
- Consider alignment requirements for NEON operations (16-byte boundaries)
- Benchmark on real ARM hardware (not Rosetta emulation)

---

**Status**: Planning phase complete, ready for implementation
**Next Step**: Begin Phase 1, Task 2 (Design batch encoder API)
