# FEC Interleaver/Deinterleaver Design

## Overview

RS(10,8)/GF(16) block: 32 segments × `col_height` bytes → `col_height×8` codewords of
5 bytes each.  Four implementations are provided:

| Function | Strategy | Gbit/s (Apple M1+, col_height=8192) |
|---|---|---|
| `interleave` | Scalar reference | 1.53 |
| `interleave_magic` | 64-bit magic-multiply, no SIMD | 10.29 |
| `interleave_neon` | ARM NEON, bit-transpose | 11.89 |
| `deinterleave` | Scalar reference | 0.16 |
| `deinterleave_neon` | ARM NEON, segment-major + TBL | 15.24 |

---

## Data Layout

**Segments buffer** — segment-major, bit-packed:
```
segments[seg * col_height + byte_idx]    (32 segs × col_height bytes)
```
Segment `seg` → column `c = seg/4`, symbol bit `bp = seg%4` (bit 3=MSB, bit 0=LSB).
Bit ordering within each segment byte is MSB-first: bit 7 → word 0, bit 0 → word 7.

**Codeword buffer** — one 5-byte record per word:
```
codewords[word * 5 + 0..4]              (num_words = col_height × 8)

Byte 0: [Sym0 hi] [Sym1 lo]
Byte 1: [Sym2 hi] [Sym3 lo]
Byte 2: [Sym4 hi] [Sym5 lo]
Byte 3: [Sym6 hi] [Sym7 lo]
Byte 4: [Par0 hi] [Par1 lo]  ← zeroed by interleave; RS encoder writes here
```
8 consecutive words share the same `byte_idx` and form a contiguous 40-byte block:
`codewords[byte_idx × 40 .. +39]`.

---

## `interleave_magic` — 64-bit scalar, no SIMD

**10.29 Gbit/s — 6.7× scalar, 86% of NEON, works on any 64-bit CPU.**

### Core primitive

Given a `uint64_t q` where each byte holds a single-bit value in bit 0:

```cpp
(q * 0x0102040810204080ULL) >> 56
```

packs those 8 values into one byte: bit `i` of the result = bit-0 of byte `i` of `q`.

**Proof:** magic `M = ∑_{j=0..7} 2^(7j+7)`. Cross-product of byte `i` with M's
term `j = 7−i` lands at bit `8i + 7(7−i) + 7 = i + 56` — one distinct bit per
input byte, no carry conflicts.

### Algorithm

Outer loop over `byte_idx`. Per iteration:

**1. Gather with XOR-3 trick:**
```cpp
for (uint32_t s = 0; s < 32u; ++s)
    gathered[s ^ 3u] = segments[s * ch + byte_idx];
```
Writing to `gathered[s ^ 3]` reverses the bit-position order within each column
group of 4 segments (XOR-3 maps 0→3, 1→2, 2→1, 3→0). After the magic multiply,
nibbles emerge in natural order (bit 3 = MSB of symbol) without a post-multiply
bit-reversal step.

**2. Load as four `uint64_t`** (8 bytes = 8 segments each):
```cpp
memcpy(&q0, gathered,     8);  // segs 0–7
memcpy(&q1, gathered + 8, 8);  // segs 8–15
memcpy(&q2, gathered +16, 8);  // segs 16–23
memcpy(&q3, gathered +24, 8);  // segs 24–31
```

**3. Per bit position `k` (7..0, unsigned wraparound sentinel):**
```cpp
constexpr uint64_t magic = 0x0102040810204080ULL;
constexpr uint64_t mask1 = 0x0101010101010101ULL;

uint32_t plane =
      (uint32_t)((((q0 >> k) & mask1) * magic) >> 56)
    | ((uint32_t)((((q1 >> k) & mask1) * magic) >> 56) <<  8)
    | ((uint32_t)((((q2 >> k) & mask1) * magic) >> 56) << 16)
    | ((uint32_t)((((q3 >> k) & mask1) * magic) >> 56) << 24);
```
`plane[3:0]` = nibble for sym0, `plane[7:4]` = sym1, …, `plane[31:28]` = sym7.

**4. Nibble-swap** (pack pairs into codeword byte order):
```cpp
uint32_t packed = ((plane & 0x0F0F0F0Fu) << 4) | ((plane >> 4) & 0x0F0F0F0Fu);
// cw byte 0 = (sym0 << 4) | sym1, byte 1 = (sym2 << 4) | sym3, …
```

**5. Store:** `memcpy` of 4-byte `packed` + one zero parity byte at `word_off + 4`.

### Worked Example

**Setup:** `col_height = 1`, `byte_idx = 0`.  Only segments 0–7 are non-zero:

```
Seg  Role                 Byte    Bits
 0   col 0, bit 3 (MSB)   0xF0   1111 0000
 1   col 0, bit 2         0xF0   1111 0000
 2   col 0, bit 1         0xF0   1111 0000
 3   col 0, bit 0 (LSB)   0xF0   1111 0000
 4   col 1, bit 3 (MSB)   0x0F   0000 1111
 5   col 1, bit 2         0x0F   0000 1111
 6   col 1, bit 1         0x0F   0000 1111
 7   col 1, bit 0 (LSB)   0x0F   0000 1111
```

All col-0 bytes have bits 7–4 set (0xF0); all col-1 bytes have bits 3–0 set (0x0F).
Expected: sym0 = 0xF and sym1 = 0x0 for words 0–3 (bits 7–4), then sym0 = 0x0 and
sym1 = 0xF for words 4–7 (bits 3–0).

---

**Step 1 — XOR-3 gather:** `gathered[s ^ 3] = segments[s]`

XOR-3 swaps bit-positions within each column group of four (0↔3, 1↔2), placing the
LSB-carrying segment at the lowest array index:

```
s   s^3   gathered[s^3] ← seg s
─   ───   ─────────────────────────────────────────────
0    3    gathered[3] = 0xF0   (col 0 MSB lands at index 3)
1    2    gathered[2] = 0xF0   (col 0 bit 2 lands at index 2)
2    1    gathered[1] = 0xF0   (col 0 bit 1 lands at index 1)
3    0    gathered[0] = 0xF0   (col 0 LSB lands at index 0)
4    7    gathered[7] = 0x0F   (col 1 MSB lands at index 7)
5    6    gathered[6] = 0x0F
6    5    gathered[5] = 0x0F
7    4    gathered[4] = 0x0F   (col 1 LSB lands at index 4)
```

Result: `gathered = [0xF0, 0xF0, 0xF0, 0xF0, 0x0F, 0x0F, 0x0F, 0x0F, 0, …]`

---

**Step 2 — Load `q0`:** `memcpy` of gathered[0..7] into a little-endian `uint64_t`:

```
Byte index:  0     1     2     3     4     5     6     7
             ↑     ↑     ↑     ↑     ↑     ↑     ↑     ↑
             seg3  seg2  seg1  seg0  seg7  seg6  seg5  seg4
            (col0                   (col1
             LSB)  …    …    MSB)   LSB)  …    …    MSB)

q0 = 0x0F0F_0F0F_F0F0_F0F0   (byte 0 is least-significant)
q1 = q2 = q3 = 0
```

The byte order arranges col-0 LSB at byte 0 and col-0 MSB at byte 3 — critical for
the multiply to output the nibble in natural bit order.

---

**Step 3 — Bit-plane extraction for k = 7 (word 0):**

`(q0 >> 7) & mask1` shifts bit 7 of each byte to bit 0, then isolates it:

```
Byte:  [0]   [1]   [2]   [3]   [4]   [5]   [6]   [7]
 q0:  0xF0  0xF0  0xF0  0xF0  0x0F  0x0F  0x0F  0x0F
 >>7:   1     1     1     1     0     0     0     0     (bit 7 of each)
```

Multiply the resulting `uint64_t` (bytes `[1,1,1,1,0,0,0,0]`) by the magic constant.
The MSByte of the product has bit `i` = bit-0 of byte `i`:

```
MSByte bit:   7    6    5    4    3    2    1    0
From byte:   [7]  [6]  [5]  [4]  [3]  [2]  [1]  [0]
Value:        0    0    0    0    1    1    1    1
```

`(q0_masked * magic) >> 56 = 0b0000_1111 = 0x0F`

With q1=q2=q3=0 contributing nothing:

```
plane = 0x0000_000F

bits [3:0] = 0xF   →  sym0 nibble = 1111  (bit 3 from byte [3] = seg0 = 1, …, bit 0 from byte [0] = seg3 = 1)
bits [7:4] = 0x0   →  sym1 nibble = 0000  (all col-1 bytes had bit 7 = 0)
```

**Contrast — k = 3 (word 4):** bit 3 of 0xF0 is 0; bit 3 of 0x0F is 1.  The bytes
after `(q0 >> 3) & mask1` are `[0,0,0,0,1,1,1,1]`.  The MSByte of the product is
`0b1111_0000 = 0xF0`.  So `plane = 0x0000_00F0`, giving sym0=0x0 and sym1=0xF —
the exact inverse, as expected.

---

**Step 4 — Nibble swap** for word 0 (`plane = 0x0000_000F`):

In `plane`, sym0 sits in bits [3:0] and sym1 in bits [7:4] — but the codeword format
needs sym0 in the *high* nibble.  Swap them:

```
plane & 0x0F = 0x0F  →  << 4  =  0xF0   (sym0 = 0xF into high nibble)
plane >> 4   = 0x00  →  & 0x0F = 0x00   (sym1 = 0x0 into low nibble)
packed = 0xF0
```

For word 4 (`plane = 0xF0`):
```
plane & 0x0F = 0x00  →  << 4  =  0x00
plane >> 4   = 0x0F  →  & 0x0F = 0x0F
packed = 0x0F
```

---

**Step 5 — Store** at `word_off = (7 − k) × 5`:

```
Word  k  word_off  packed  cw byte 0  (sym0 hi | sym1 lo)
────  ─  ────────  ──────  ─────────────────────────────────
  0   7       0   0xF0    (0xF << 4) | 0x0 = 0xF0  ✓
  1   6       5   0xF0
  2   5      10   0xF0
  3   4      15   0xF0
  4   3      20   0x0F    (0x0 << 4) | 0xF = 0x0F  ✓
  5   2      25   0x0F
  6   1      30   0x0F
  7   0      35   0x0F
```

Bytes 1–3 of each codeword are 0x00 (segs 8–31 are zero).  Byte 4 is the zeroed parity slot.

### Why it's fast

The inner loop exposes 32 independent multiply chains (`q0`–`q3` × 8 planes).
Apple M1+'s 6 integer ALUs and wide OoO window keep most in flight simultaneously,
sustaining near-NEON throughput without touching a SIMD unit.

---

## `interleave_neon` — ARM NEON

**11.89 Gbit/s — 7.8× scalar, 1.2× magic-multiply.**

Outer loop over `byte_idx`, processing all 8 codewords per iteration.

**1. Gather** — 32 scalar byte loads into a temporary array, then load into two
128-bit registers:
```
g_lo = vld1q_u8(gathered +  0)   // segs 0–15
g_hi = vld1q_u8(gathered + 16)   // segs 16–31
```

**2. Bit-transpose** — unrolled 8× (one per bit position `k = 7..0`).  Per pass:
```
bits_lo = (g_lo >> k) & 1          // bit k of segs 0–15  (0 or 1 per lane)
bits_hi = (g_hi >> k) & 1          // bit k of segs 16–31

// Weight {8,4,2,1}×4: each bit lands at its nibble position.
// Two vpadd_u8 passes reduce 16 weighted values → 4 nibbles per half.
p1 = vpadd(low(bits_lo × wt), high(bits_lo × wt))   // 4 nibbles, cols 0–3
p2 = vpadd(low(bits_hi × wt), high(bits_hi × wt))   // 4 nibbles, cols 4–7
nibbles = vpadd(p1, p2)                              // all 8 nibbles
```

**3. Nibble packing:**
```
// Weight {16,1}×4 then vpadd combines (even<<4)|odd pairs.
packed = vpadd(nibbles × {16,1,…}, zero)   // 4 codeword data bytes
```

**4. Store** — `vst1_lane_u32` (4 bytes) + 1 scalar zero byte per codeword.

`vshrq_n_u8` requires a compile-time constant shift; the `k=0` case uses
`vand_u8(g, one)` directly. All 8 bit positions are unrolled via macro.

---

## `deinterleave_neon` — ARM NEON

**15.24 Gbit/s — 93.7× scalar.**

Two combined optimizations:

### 1. Loop inversion (cache)

| Scalar | NEON |
|---|---|
| Outer: words | Outer: **segments** (0..31) |
| Inner: 32 scattered OR-writes | Inner: **sequential** byte writes |
| Every write = different cache line | Stride-1 output, linear codeword reads |

### 2. Contiguous load + TBL gather

The inner loop (per `byte_idx`) loads the 40-byte codeword block as three
independent 16-byte registers, then uses `vqtbl3_u8` with a compile-time index
vector to extract the 8 needed bytes in one instruction:

```cpp
const uint8_t* blk = codewords.data() + size_t{byte_idx} * 40u;
const uint8x16x3_t block = {{
    vld1q_u8(blk),       // block[0..15]  → table[0..15]
    vld1q_u8(blk + 16),  // block[16..31] → table[16..31]
    vld1q_u8(blk + 24),  // block[24..39] → table[32..47]
}};
uint8x8_t cb = vqtbl3_u8(block, idx);
```

Third load at offset `+24` (not `+32`) keeps all reads within the 40-byte block.
The three loads are independent and execute in parallel; TBL resolves in 2 cycles.
Total gather latency: ~5 cycles vs ~16 cycles for the previous 8× `vld1_lane_u8`
dependency chain.

**TBL index vectors** — four compile-time constants, one per `cw_off` (= `c/2`).
For actual byte offset `o`: index = `o` if `o < 32`; index = `o + 8` if `o ≥ 32`
(because `reg2[k] = block[24+k]`, so `block[o] = reg2[o−24]` → table index `32+(o−24) = o+8`):

```
cw_off=0: { 0,  5, 10, 15, 20, 25, 30, 43}
cw_off=1: { 1,  6, 11, 16, 21, 26, 31, 44}
cw_off=2: { 2,  7, 12, 17, 22, 27, 40, 45}
cw_off=3: { 3,  8, 13, 18, 23, 28, 41, 46}
```

**Nibble extraction and bit packing** (same for all gather strategies):
```cpp
uint8x8_t nibble = high_nib ? vshr_n_u8(cb, 4) : vand_u8(cb, 0xF);
uint8x8_t bit_val = vand_u8(vshl_s8(nibble, nib_shift), 1);
seg_out[byte_idx] = vaddv_u8(vmul_u8(bit_val, pack_wt));
// pack_wt = {128,64,32,16,8,4,2,1} — MSB-first bit position weights
```

`nib_shift = -(3 − bp)` (hoisted; `vshl_s8` with negative = right shift).
`vaddv_u8` (AArch64 horizontal add) collapses 8 weighted bits to one output byte.
