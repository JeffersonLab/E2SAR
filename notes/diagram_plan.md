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

---

# FEC Receive Path Diagram Plan

A set of draw.io diagrams showing how wire packets received by `Reassembler`
are assembled (and, when needed, FEC-recovered) back into an N-byte event
buffer.  The receive path is implemented in `src/e2sarDPReassembler.cpp`.

## Concrete example parameters (same as send side)

| Symbol | Formula | Value |
|--------|---------|-------|
| colHeight | from ECHdr.ecSegmentSize | 1420 |
| maxUserData | colHeight − sizeof(REHdr) | 1400 |
| segmentBuf | 32 × colHeight | 45,440 B |
| parityBuf | 8 × colHeight | 11,360 B |
| codewordBuf | 11,360 × 5 B | 56,800 B |
| dataSegsNeeded | 32 − padFrames | ≤ 32 |
| dataMask | (1 << dataSegsNeeded) − 1 | 32-bit |
| maxRecoverable | parity symbols | 2 damaged RS columns |

---

## RX Diagram 1 — Wire Packet Format (Receiver View)

**What it shows**: The two packet types that arrive and the fields the receiver
extracts from each header.

Content:
- **Data packet** layout (same as send Diagram 6 data packet):
  ```
  [IP 20B][UDP 8B][LBHdrV2 16B][ECHdr 16B][segment payload: colHeight B]
  ```
  - ECHdr fields highlighted: magic='EC', P=0, ecFrameNum (0–31), padFrames,
    ecSegmentSize (=colHeight), fecBlockNum
  - REHdr fields highlighted (first 20 B of segment payload):
    eventNum, dataId, bufferOffset, bufferLength (=N)

- **Parity packet** layout:
  ```
  [IP 20B][UDP 8B][LBHdrV2 16B][ECHdr 16B][REHdr 20B][parity data: colHeight B]
  ```
  - ECHdr fields: P=1, ecFrameNum (0–7)
  - REHdr skipped over; parity data starts at payload + sizeof(ECHdr) + sizeof(REHdr)

- Label which fields drive routing decisions (P-bit, ecFrameNum, fecBlockNum,
  eventNum, dataId)

---

## RX Diagram 2 — Packet Classification and Block State

**What it shows**: How the recv thread strips headers, classifies the packet,
and places it into the right staging buffer using `FecBlockState`.

Content:
- Flowchart path from raw UDP payload:
  1. Strip LBHdrU (if withLBHeader)
  2. Check magic bytes `payload[0]=='E' && payload[1]=='C'` → FEC path
  3. Validate ECHdr, validate REHdr
  4. Extract key fields → compute `blockKey = (eventNum, dataId, fecBlockNum)`
  5. Lookup `fecBlocksInProgress[blockKey]`; create `FecBlockState` if new
     - Allocate `segmentBuf[32 × colHeight]`, `parityBuf[8 × colHeight]`
  6. Lookup `eventsInProgress[eventKey]`; create `EventQueueItem` if new
     - Allocate `event[totalEventBytes]`; compute `fecBlocksExpected`

- **FecBlockState** box:
  - `segmentBuf[32 × colHeight]` — rows for data segments 0–31
  - `parityBuf[8 × colHeight]` — rows for parity segments 0–7
  - `dataReceived` — 32-bit bitmask, bit i set when segment i arrives
  - `parityReceived` — 8-bit bitmask, bit j set when parity j arrives
  - `padFrames`, `colHeight`, `fecBlockNum`, `firstSegment` (arrival time)

- **Placement arrows**:
  - Data packet (P=0): `segData = payload + sizeof(ECHdr)`;
    `segmentBuf[ecFrameNum × colHeight .. +colHeight]` ← memcpy;
    set bit `ecFrameNum` in `dataReceived`
  - Parity packet (P=1): `segData = payload + sizeof(ECHdr) + sizeof(REHdr)`;
    `parityBuf[ecFrameNum × colHeight .. +colHeight]` ← memcpy;
    set bit `ecFrameNum` in `parityReceived`

- Show `completedFecBlocks` set — late-arriving packets for already-assembled
  blocks are discarded immediately

---

## RX Diagram 3 — Happy Path: Block Assembly (No Recovery)

**What it shows**: The fast path when all `dataSegsNeeded` data segments arrive
before the GC timeout.

Content:
- Trigger condition:
  ```
  dataSegsNeeded = 32 − padFrames
  dataMask       = (1 << dataSegsNeeded) − 1
  popcount(dataReceived & dataMask) == dataSegsNeeded  → happy path
  ```
- Loop over segments 0 .. dataSegsNeeded−1:
  - Read `REHdr` at `segmentBuf[i × colHeight]`
  - `offset = segRe->bufferOffset`
  - `segDataLen = min(maxUserData, totalEventBytes − offset)`
  - `memcpy(event + offset, segmentBuf + i×colHeight + sizeof(REHdr), segDataLen)`
- Show how `bufferOffset` in REHdr scatters each segment to the correct position
  in the `EventQueueItem.event` buffer (offset = i × maxUserData for most segs,
  shorter for the last)
- Housekeeping:
  - `fecBlocksInProgress.erase(blockKey)` + `completedFecBlocks.insert(blockKey)`
  - `item->fecBlocksCompleted++`
  - If `fecBlocksCompleted == fecBlocksExpected` → `enqueue(item)` → event
    delivered to application

---

## RX Diagram 4 — Recovery Path: GC Thread and FEC Erasure Decode

**What it shows**: What happens when a block times out with missing data
segments but enough parity to recover.

Content:
- **GC sweep trigger**: `nowT − blk->firstSegment > eventTimeout_ms`
- **Decision tree**:
  ```
  dataMissing = dataSegsNeeded − popcount(dataPresent)
  ├── dataMissing == 0 → recovered = true (no-op; all data already present)
  ├── 0 < dataMissing ≤ 8 AND fecDecodeCtx.initialized
  │   ├── damagedCols = { s/4 for each missing segment s }
  │   ├── damagedCols.size() ≤ 2  AND
  │   │   popcount(parityReceived) ≥ damagedCols.size()
  │   │   → attempt recovery (Diagram 5)
  │   └── otherwise → fecFailures++, event lost
  └── dataMissing > 8 → fecFailures++, event lost
  ```
- **Zero-fill step**: for each col in damagedCols, zero all 4 rows
  (`segmentBuf[col×4 + b]` for b=0..3) before interleaving so the
  erased positions are known to be 0
- After recovery: same scatter loop as happy path (Diagram 3)
- Show `fecRecoveries` and `fecFailures` counters

---

## RX Diagram 5 — RS Erasure Correction Detail

**What it shows**: The four interleave/decode/deinterleave steps executed by
the GC thread to recover missing data segments.

Content (four-stage pipeline within one damaged block):

1. **interleave** (`segmentBuf` → `codewordBuf`, bytes 0–3):
   - Inverse of send Diagram 3; same bit-transposition, but now some rows
     may be all-zero (damaged) — their symbols will be the erased positions
   - Result: `codewordBuf[11,360 × 5 B]`, byte 4 still 0

2. **interleave_parity** (`parityBuf` → `codewordBuf` byte 4):
   - Mirror of send Diagram 5; fills byte 4 of each codeword from received
     parity segments
   - Parity symbols: [P0 hi | P1 lo] packed into byte 4

3. **rs_decode** with erasure list:
   - Each codeword: 10 GF(16) symbols — 8 data (bytes 0–3) + 2 parity (byte 4)
   - `erasedCols` = list of RS column indices (= `damagedCols`)
   - RS(10,8) erasure decoder over GF(2⁴) recovers up to 2 erased symbols
   - Show one codeword blown up: erased symbols marked ✗, parity symbols
     filled, recovered symbols marked ✓

4. **deinterleave** (`codewordBuf` → `segmentBuf`):
   - Inverse of interleave; writes repaired data back into the segment rows
     that were previously zeroed
   - `segmentBuf` now has all 32 rows populated (recovered rows filled in)

- Show `RsDecodeContext` (pre-initialized GF log/exp tables) shared across
  the GC thread's sweep loop

---

## RX Diagram 6 — Event Reconstruction (Multi-Block)

**What it shows**: How segments from multiple blocks are scattered into a
single N-byte event buffer.

Content:
- Horizontal layout: Block 0 (segs 0–31), Block 1 (segs 32–63), …, last block
  (segs numSegs−1, possibly fewer than 32)
- Each block contributes `dataSegsNeeded` segments, each carrying
  `maxUserData` (1400) bytes except the last segment of the last block
- `REHdr.bufferOffset` = i × maxUserData drives the scatter:
  ```
  Block 0, seg 0  → event[0 .. 1399]
  Block 0, seg 1  → event[1400 .. 2799]
  ...
  Block 0, seg 31 → event[43400 .. 44799]
  Block 1, seg 0  → event[44800 .. 46199]
  ...
  ```
- Show `EventQueueItem`: `event[N]`, `fecBlocksExpected`, `fecBlocksCompleted`,
  `curBytes`; when `fecBlocksCompleted == fecBlocksExpected` the item is
  enqueued for the application

---

## RX Diagram 7 — End-to-End Receive Pipeline Summary

**What it shows**: Single-page overview of the full receive pipeline.

```
Wire: 40 packets/block (32 data + 8 parity)
  │
  ├─ recv thread (per packet)
  │   ├─ strip LBHdrU → check 'EC' magic
  │   ├─ parse ECHdr + REHdr → blockKey, eventKey
  │   ├─ lookup/create FecBlockState + EventQueueItem
  │   ├─ data pkt (P=0) → segmentBuf[ecFrameNum × colHeight]  dataReceived |= bit
  │   └─ parity pkt (P=1) → parityBuf[ecFrameNum × colHeight] parityReceived |= bit
  │
  ├─ happy path (all data segs present)
  │   └─ scatter REHdr.bufferOffset → event[], mark block done
  │
  ├─ GC thread (on timeout)
  │   ├─ 0 < dataMissing ≤ 8, damagedCols ≤ 2, enough parity?
  │   │   ├─ zero damaged cols in segmentBuf
  │   │   ├─ interleave segmentBuf → codewordBuf (bytes 0–3)
  │   │   ├─ interleave_parity parityBuf → codewordBuf (byte 4)
  │   │   ├─ rs_decode (erasure) → repaired codewords
  │   │   ├─ deinterleave → repaired segmentBuf
  │   │   └─ scatter → event[], mark block done
  │   └─ unrecoverable → fecFailures++, drop event
  │
  ▼ (all blocks complete)
EventQueueItem.event[N bytes] → enqueue() → recvEvent()/getEvent()
```

- Annotate sizes at each stage (same as send Diagram 7 for symmetry)
- Note: blocks for the same event may arrive out of order and are reassembled
  concurrently by the recv threads; the GC thread is a single shared sweeper

---

## Implementation order (receive-side)

1. RX Diagram 7 (overview) — most useful standalone
2. RX Diagram 1 (wire packet format, receiver view)
3. RX Diagram 2 (packet classification and block state)
4. RX Diagram 3 (happy path block assembly)
5. RX Diagram 4 (GC thread / recovery decision tree)
6. RX Diagram 5 (RS erasure correction detail)
7. RX Diagram 6 (multi-block event reconstruction)

Planned files in `diagrams/` (not yet generated):

- `fec_recv_pipeline_overview.drawio` — RX Diagram 7
- `fec_rx_01_wire_packet_format.drawio` — RX Diagram 1
- `fec_rx_02_block_state.drawio` — RX Diagram 2
- `fec_rx_03_happy_path.drawio` — RX Diagram 3
- `fec_rx_04_gc_recovery.drawio` — RX Diagram 4
- `fec_rx_05_rs_erasure.drawio` — RX Diagram 5
- `fec_rx_06_event_reconstruction.drawio` — RX Diagram 6
