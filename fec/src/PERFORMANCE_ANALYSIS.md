# RS-FEC Performance Analysis

## NEON SIMD vs Batched Performance Comparison

### Why `neon_rs_encode_dual_nibble` is Faster Than `neon_rs_encode_dual_nibble_batch_blocked`

The simple NEON dual nibble SIMD encoder significantly outperforms the batched version for several key reasons:

#### 1. Memory Layout Overhead
- **SIMD version**: Direct memory access - loads 8 bytes straight into a NEON register
- **Batched version**: Requires converting data to blocked transposed layout before/after encoding, adding significant overhead via `convert_to_blocked_transposed_data()` and `convert_from_blocked_transposed_parity()`

#### 2. Simplicity vs. Complexity

**Simple SIMD version** (`ejfat_rs_neon.h:83-172`):
```c
uint8x8_t data_vec = vld1_u8((uint8_t *)data_bytes);  // One load
uint8x8_t lower_nibbles = vand_u8(data_vec, nibble_mask);  // Simple extract
uint8x8_t upper_nibbles = vshr_n_u8(data_vec, 4);
```

**Batched version** (`ejfat_rs.h:800-904`) has:
- Complex block/chunk indexing calculations
- Conditional logic for partial chunks (lines 836-852)
- Temporary buffer handling
- Indirect memory access patterns

#### 3. Cache Behavior
For small operations (8 nibbles = 4 bytes per RS codeword):
- **SIMD**: Data fits in L1 cache, sequential access pattern is optimal
- **Batched**: The blocked transposed layout and chunking actually **hurts** cache performance because:
  - Data is scattered across memory in the transposed layout
  - Processing 16 vectors at once increases working set size
  - The conversion overhead negates any cache benefits

#### 4. Instruction Overhead
The batched version uses 128-bit instructions (`vld1q_u8`, `vqtbl2q_u8`) which:
- Have higher latency than 64-bit versions (`vld1_u8`, `vtbl2_u8`)
- Require more complex setup (replicating lookup tables to 16 elements)
- Add branching logic for chunk handling

#### 5. The "Small Data Paradox"
With only 8 bytes per encoding operation:
- Batching overhead (indexing, chunking, layout conversion) > actual computation time
- The simple SIMD version is already maximally efficient for this data size
- Batching benefits only appear with much larger data volumes or when amortized over many operations

**Conclusion**: The simple NEON dual nibble encoder is optimally tuned for single-vector operations. The batching infrastructure adds overhead that only pays off when processing extremely large datasets where layout conversion costs are amortized - which isn't the case in this 8-byte-per-codeword scenario.

---

## Blocked Transposed Layout

### Overview

The blocked transposed layout reorganizes data to optimize SIMD processing by:
1. **Transposing**: Grouping same-position symbols from different vectors
2. **Blocking**: Splitting into cache-friendly chunks

### Standard Vector-Major Layout

Traditional storage of multiple RS codewords:
```
Vector 0: [d0 d1 d2 d3 d4 d5 d6 d7]
Vector 1: [d0 d1 d2 d3 d4 d5 d6 d7]
Vector 2: [d0 d1 d2 d3 d4 d5 d6 d7]
...
Vector N: [d0 d1 d2 d3 d4 d5 d6 d7]

Memory: [V0d0 V0d1 ... V0d7 | V1d0 V1d1 ... V1d7 | V2d0 ...]
```
Each vector's 8 symbols are stored contiguously.

### Blocked Transposed Layout

The data is reorganized in two dimensions:

#### 1. Transpose: Symbols Before Vectors
```
Symbol 0 from all vectors: [V0d0 V1d0 V2d0 ... VNd0]
Symbol 1 from all vectors: [V0d1 V1d1 V2d1 ... VNd1]
...
Symbol 7 from all vectors: [V0d7 V1d7 V2d7 ... VNd7]
```

#### 2. Blocking: Split Into Cache-Friendly Chunks
With `block_size = 256`:
```
Block 0 (vectors 0-255):
  Symbol 0: [V0d0   V1d0   ... V255d0]
  Symbol 1: [V0d1   V1d1   ... V255d1]
  ...
  Symbol 7: [V0d7   V1d7   ... V255d7]

Block 1 (vectors 256-511):
  Symbol 0: [V256d0 V257d0 ... V511d0]
  Symbol 1: [V256d1 V257d1 ... V511d1]
  ...
  Symbol 7: [V256d7 V257d7 ... V511d7]
```

### Memory Layout Formula

From `ejfat_rs.h:605`:
```c
blocked[block * block_size * 8 + symbol * block_size + v] =
    vector_major[vec_idx * 8 + symbol];
```

Where `vec_idx = block * block_size + v`

### Visual Example

For 1000 vectors with `block_size = 256`:

**Vector-major** (input):
```
Offset 0:    V0[d0 d1 d2 d3 d4 d5 d6 d7]
Offset 8:    V1[d0 d1 d2 d3 d4 d5 d6 d7]
...
Offset 7992: V999[d0 d1 d2 d3 d4 d5 d6 d7]
```

**Blocked-transposed** (output):
```
Block 0 (vectors 0-255):
  Offset 0:    [V0d0   V1d0   V2d0   ... V255d0]   ← Symbol 0, 256 bytes
  Offset 256:  [V0d1   V1d1   V2d1   ... V255d1]   ← Symbol 1, 256 bytes
  Offset 512:  [V0d2   V2d2   V3d2   ... V255d2]   ← Symbol 2, 256 bytes
  ...
  Offset 1792: [V0d7   V1d7   V2d7   ... V255d7]   ← Symbol 7, 256 bytes

Block 1 (vectors 256-511):
  Offset 2048: [V256d0 V257d0 ... V511d0]
  ...
```

### Why This Layout?

#### SIMD Benefits
When computing parity for symbol `j`:
```c
// All vectors' symbol j are contiguous in memory
data_vec = vld1q_u8(&data_blocked[block_offset + j * block_size + v]);
```
- Loads 16 consecutive values of the **same symbol position** from different vectors
- Perfect for SIMD: same coefficient × 16 different data values
- Enables parallel processing of multiple codewords

#### Cache Optimization
- Block size (256) chosen to fit working set in L1/L2 cache
- All 8 symbols × 256 values = 2KB per block
- Minimizes cache misses during encoding by keeping related data close

#### The Tradeoff
- **Good for**: Large batches where conversion cost is amortized over many operations
- **Bad for**: Small operations where conversion overhead > computation savings

### When to Use Each Approach

| Data Size | Recommended Approach | Reason |
|-----------|---------------------|---------|
| Single codeword (8 bytes) | Simple SIMD | Zero conversion overhead |
| Small batch (<1000) | Simple SIMD loop | Conversion cost > savings |
| Large batch (>10,000) | Blocked transposed | Amortized conversion cost, better cache behavior |
| Streaming data | Simple SIMD | Real-time processing needs low latency |

In the current RS(10,8) implementation with 8-byte codewords, the simple SIMD version consistently outperforms the batched version for typical use cases.
