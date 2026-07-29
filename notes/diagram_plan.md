# FEC Send Path Diagram Plan

A set of draw.io diagrams showing how an N-byte event buffer is transformed
into wire packets by `_sendWithFec()`.

## Concrete example parameters (used throughout)

| Symbol | Formula | Value (MTU=1500, IPv4) |
|--------|---------|------------------------|
| MTU | given | 1500 |
| getFecTotalHeaderLength | IP + UDP + LBHdrV2 + ECHdr + REHdr | 20+8+16+16+20 = 80 |
| colHeight | MTU − getFecTotalHeaderLength | 1420 |
| maxUserData | colHeight − sizeof(REHdr) | 1420−20 = 1400 |
| numSegs | ⌈N / maxUserData⌉ | ⌈N / 1400⌉ |
| numBlocks | ⌈numSegs / 32⌉ | — |

---

## Diagram 1 — Event Partitioning into Segments

**What it shows**: The N-byte event buffer sliced into `numSegs` segments of
up to `maxUserData` (1400) bytes each, grouped into blocks of 32.

Content:
- A long rectangle labeled "Event buffer (N bytes)"
- Arrows/divisions showing it split into segments 0..numSegs−1, each 1400 B
  (last segment may be shorter)
- Segments grouped into Block 0 (segs 0–31), Block 1 (segs 32–63), …
- Last block has `segsInBlock ≤ 32` real segments + `padFrames` empty slots

---

## Diagram 2 — Segment Buffer Layout (one block)

**What it shows**: How `segmentBuf` (32 × colHeight = 45,440 bytes) is
populated for one block.

Content:
- 32 rows, each `colHeight` (1420) bytes wide
- Each row: `[REHdr 20B | user data ≤1400B | zero-padding]`
- Last real segment may have padding; remaining rows (padFrames) are all-zero
- Label sizes: REHdr=20, data portion, pad portion, total row = 1420

---

## Diagram 3 — Interleave: Segments → Codewords

**What it shows**: The bit-level transposition from 32 segment rows into
`num_words` (11,360) codewords of 5 bytes each.

Content:
- Left: 32×colHeight grid (segment buffer), with bit indexing highlighted
  - Columns = 8 groups of 4 segments (one group per RS symbol)
  - Segment c×4+b holds bit (3−b) of symbol c for all words
- Right: linear array of 11,360 codewords, each 5 bytes
  - Bytes 0–3: 8 data nibbles packed as [sym0|sym1] [sym2|sym3] [sym4|sym5] [sym6|sym7]
  - Byte 4: parity nibbles (zeroed at this stage)
- Arrows showing how bit position W in the segment buffer maps to codeword W:
  - byte_idx = W/8, bit_shift = 7−(W%8)
  - One bit extracted from each of 32 segments → 8 nibbles → 4 data bytes

---

## Diagram 4 — RS(10,8) Encoding

**What it shows**: Reed-Solomon encoding that fills byte 4 of each codeword.

Content:
- One codeword blown up: 10 GF(16) symbols
  - Symbols 0–7 (data): extracted from bytes 0–3
  - Symbols 8–9 (parity): written to byte 4
- Block diagram: 8 data symbols → RS(10,8) encoder over GF(2⁴) → 2 parity symbols
- Note: GF(16) irreducible polynomial x⁴+x+1, multiplication via log/exp tables
- Applied independently to each of the 11,360 codewords

---

## Diagram 5 — Parity Extraction (Deinterleave Parity)

**What it shows**: How byte 4 of all codewords is deinterleaved back into
8 parity segments of `colHeight` bytes each.

Content:
- Left: codeword array with byte 4 highlighted — [P0 hi | P1 lo] per codeword
- Right: parityBuf (8 × colHeight = 11,360 bytes total)
  - Parity segs 0–3: bit 3,2,1,0 of parity symbol 0
  - Parity segs 4–7: bit 3,2,1,0 of parity symbol 1
- Arrows: inverse of interleave — bit W of parity seg s comes from
  the corresponding nibble bit in codeword W's byte 4

---

## Diagram 6 — Wire Packet Format

**What it shows**: The final packets sent per block (40 total: 32 data + 8 parity).

Content:
- **Data packet** (×32 per block):
  ```
  [IP 20B][UDP 8B][LBHdrV2 16B][ECHdr 16B][segment payload: colHeight B]
  ```
  Total on wire: 60 + 1420 = 1480 B (< MTU)
  - Segment payload = REHdr(20) + user data(≤1400) + padding
  - ECHdr fields: P=0, frameNum=i (0–31), padFrames, segSize=colHeight, padBytes, blockNum

- **Parity packet** (×8 per block):
  ```
  [IP 20B][UDP 8B][LBHdrV2 16B][ECHdr 16B][REHdr 20B][parity data: colHeight B]
  ```
  Total on wire: 80 + 1420 = 1500 B (= MTU exactly)
  - ECHdr fields: P=1, frameNum=j (0–7), padFrames, segSize=colHeight, padBytes=0, blockNum
  - REHdr: bufferOffset=0, bufferLength=N, eventNum

- Show send ordering: all 32 data packets, then all 8 parity packets, per block

---

## Diagram 7 — End-to-End Pipeline Summary

**What it shows**: Single-page overview connecting all stages with sizes.

```
Event (N bytes)
  │
  ├─ partition: ⌈N/1400⌉ segments, ⌈segs/32⌉ blocks
  │
  ▼ (per block)
segmentBuf [32 × 1420 B]  ← REHdr + data + pad per row
  │
  ├─ interleave (bit transpose)
  │
  ▼
codewordBuf [11,360 × 5 B]  ← 8 data nibbles + 2 parity nibbles
  │
  ├─ RS(10,8) encode (fills parity nibbles)
  │
  ├─ deinterleave_parity
  │
  ▼
parityBuf [8 × 1420 B]
  │
  ├─ send 32 data packets: LBECHdr(32B) + segmentBuf row (1420B)
  ├─ send  8 parity packets: LBECREHdr(52B) + parityBuf row (1420B)
  ▼
Wire: 40 packets/block, total ≈ numBlocks × 59,360 bytes
```

---

## Implementation order

1. Diagram 7 (overview) — most useful standalone
2. Diagram 1 (event partitioning)
3. Diagram 2 (segment buffer layout)
4. Diagram 3 (interleave)
5. Diagram 4 (RS encoding)
6. Diagram 5 (parity extraction)
7. Diagram 6 (wire packet format)

All diagrams generated and validated (2026-07-23). Files in `diagrams/`:

- `fec_send_pipeline_overview.drawio` — Diagram 7 (end-to-end overview)
- `fec_01_event_partitioning.drawio` — Diagram 1
- `fec_02_segment_buffer_layout.drawio` — Diagram 2
- `fec_03_interleave.drawio` — Diagram 3
- `fec_04_rs_encoding.drawio` — Diagram 4
- `fec_05_parity_extraction.drawio` — Diagram 5
- `fec_06_wire_packet_format.drawio` — Diagram 6
