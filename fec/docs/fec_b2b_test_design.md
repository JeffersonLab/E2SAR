# FEC Back-to-Back Test Design

## Purpose

`test/py_test/test_fec_b2b.py` is an end-to-end correctness test for the RS(10,8)
FEC encoder and decoder hooked into E2SAR's Segmenter and Reassembler.  It sends
real events through the full `Segmenter → UDP → Reassembler` path with controlled
packet loss, then verifies that received data is byte-exact after erasure recovery
(or correctly reported lost when recovery is impossible).

The existing FEC bench (`test/e2sar_fec_bench.cpp`) measures throughput only.
This suite adds loss injection and correctness checks.

---

## Test Infrastructure

### Pipeline Fixtures

| Fixture | MTU | Sockets | Use |
|---------|-----|---------|-----|
| `fec_pipeline` | 1500 | 4 | Single-event tests |
| `fec_pipeline_single_socket` | 1500 | 1 | Multi-event tests with proxy loss keyed by block number |
| `fec_pipeline_mtu500` | 500 | 1 | MTU variation tests |
| `fec_pipeline_mtu9000` | 9000 | 1 | MTU variation (jumbo frame) tests |

`mtu_params(mtu)` computes `(col_height, max_user_data, bytes_per_block)` from
any MTU using the canonical formula:

```python
col_height    = mtu - 80          # FEC header overhead (IPv4)
max_user_data = col_height - 20   # RE header embedded in each segment
bytes_per_block = max_user_data * 32
```

### Pipeline Architecture

```
Segmenter ──UDP──▶ FecUdpProxy ──drop/forward──▶ Reassembler
              (proxy_port)                        (reas_port)
```

A Python `FecUdpProxy` thread sits between the Segmenter and Reassembler on
loopback.  It parses the LB + EC headers of every UDP datagram to extract:

| Field | Byte offset | Description |
|---|---|---|
| LB magic | 0–1 | `'LB'` — confirms this is an E2SAR packet |
| EC magic | 16–17 | `'EC'` — confirms FEC encoding |
| `pFrameNum` | 22 | Bit 7 = parity flag; bits 6:0 = frame number within block |
| `fecBlockNum` | 28–31 | Big-endian uint32 — which FEC block this belongs to |

The proxy applies a `LossSpec` keyed by `fecBlockNum`:

```python
@dataclass
class LossSpec:
    drop_data_frames:   set   # frame numbers 0–31 to suppress
    drop_parity_frames: set   # parity frame numbers 0–7 to suppress
```

Packets not matching any block key are forwarded unchanged.  Because
`sendEvent()` is synchronous and `sendmsg()` completes before Python returns, all
loss patterns for a test must be set **before** the first `sendEvent()` call.

---

## FEC Block Topology

The RS(10,8) codec over GF(16) maps onto the wire as follows:

```
8 data columns × 4 segments/column = 32 data segments  (frames 0–31)
2 parity columns × 4 segments/column = 8 parity segments (parity frames 0–7)
```

Column-to-frame mapping:

| Column | Data frames |
|--------|-------------|
| 0 | 0, 1, 2, 3 |
| 1 | 4, 5, 6, 7 |
| 2 | 8, 9, 10, 11 |
| … | … |
| 7 | 28, 29, 30, 31 |

**Parity segment topology** — parity symbols are distributed across bit-plane
segments.  Each GF(16) parity symbol (4 bits) occupies exactly 4 consecutive
parity frames:

| Parity symbol | Parity frames |
|---------------|---------------|
| Symbol 0 | 0, 1, 2, 3 |
| Symbol 1 | 4, 5, 6, 7 |

This is critical: **a single parity segment does not constitute a recoverable
parity symbol**.  All 4 frames of a symbol must be present for that symbol to
contribute to erasure correction.

At MTU 1500 (IPv4):

```
getFecTotalHeaderLength = 20(IP) + 8(UDP) + 16(LB) + 16(EC) + 20(RE) = 80 B
col_height   = 1500 − 80 = 1420 B per segment
maxUserData  = 1420 − 20 (RE header) = 1400 B of user payload per segment
bytes/block  = 32 × 1400 = 44 800 B
packets/block = 32 data + 8 parity = 40 UDP datagrams
```

At MTU 500 (IPv4):

```
col_height   = 500 − 80 = 420 B
maxUserData  = 400 B
bytes/block  = 32 × 400 = 12 800 B
```

At MTU 9000 (jumbo, IPv4):

```
col_height   = 9000 − 80 = 8920 B
maxUserData  = 8900 B
bytes/block  = 32 × 8900 = 284 800 B
```

---

## Recovery Guard Conditions (GC thread)

The Reassembler's GC thread (`src/e2sarDPReassembler.cpp:284`) applies these
gates before attempting RS decode:

```
dataMissing  = dataSegsNeeded − popcount(dataReceived & dataMask)
damagedCols  = { s / 4 for s in missing_segments }

Attempt recovery if:
  dataMissing > 0
  AND dataMissing ≤ 8          ← segment count guard
  AND damagedCols.size() ≤ 2  ← column count guard
  AND popcount(parityReceived) ≥ damagedCols.size()  ← parity availability guard
```

The parity availability guard counts **parity segments**, not complete parity
symbols.  The guard passes with 4 segments received for a 1-column loss, but 4
segments from the same symbol is what makes those 4 segments collectively useful.

---

## Test Categories and Pass Criteria

### Category 1 — No Loss (baseline)

The recv-thread fast path assembles events without involving the GC.

| Test | Event size | Loss | Expected |
|------|-----------|------|----------|
| `test_no_loss_single_block` | 1 full block (32 segs) | None | `eventSuccess≥1`, `fecRecoveries=0`, `fecFailures=0` |
| `test_no_loss_multi_block` | 3 full blocks | None | same |

### Category 2 — Recoverable Single-Column Loss

Four segments of one column are dropped.  The GC uses one parity symbol to
restore the column.

| Test | Lost column | Expected |
|------|------------|---------|
| `test_one_column_loss_recoverable[0]` | col 0 (frames 0–3) | `fecRecoveries≥1`, data correct |
| `test_one_column_loss_recoverable[3]` | col 3 (frames 12–15) | same |
| `test_one_column_loss_recoverable[7]` | col 7 (frames 28–31) | same |

### Category 3 — Recoverable Two-Column Loss

Eight segments across two columns are dropped.  This is the maximum correctable
case for RS(10,8).

| Test | Lost columns | Expected |
|------|-------------|---------|
| `test_two_column_loss_recoverable[cols0]` | (0, 1) | `fecRecoveries≥1`, data correct |
| `test_two_column_loss_recoverable[cols1]` | (0, 7) | same |
| `test_two_column_loss_recoverable[cols2]` | (2, 5) | same |
| `test_two_column_loss_recoverable[cols3]` | (3, 6) | same |

### Category 4 — Partial Column Loss

Only some segments of a column are dropped.  The column is still marked damaged
(the GC zeroes the full column before decoding), but recovery succeeds.

| Test | Lost frames | Damaged cols | Expected |
|------|------------|-------------|---------|
| `test_partial_column_loss_recoverable` | frames 4, 5 (col 1) | 1 | `fecRecoveries≥1`, data correct |

### Category 5 — Unrecoverable: Column Count Exceeds RS Capacity

Three or more distinct columns are damaged.  The column-count guard fails.

| Test | Lost columns | Expected |
|------|-------------|---------|
| `test_three_column_loss_unrecoverable` | 0, 3, 7 (12 frames) | `fecFailures≥1`, `eventSuccess=0` |
| `test_four_column_loss_unrecoverable` | 0, 1, 2, 3 (16 frames) | same |

### Category 6 — Unrecoverable: Insufficient Parity

Damaged column count is within RS capacity but available parity is less than
the number of damaged columns.

| Test | Data loss | Parity kept | Expected |
|------|-----------|------------|---------|
| `test_two_col_loss_insufficient_parity` | cols 0, 7 | parity 0 only (1 segment) | `fecFailures≥1` |
| `test_one_col_loss_no_parity` | col 4 | none | same |

### Category 7 — Parity-Only Loss

All data arrives intact.  The recv-thread fast path completes immediately; the
GC never runs for this block.

| Test | Parity dropped | Expected |
|------|---------------|---------|
| `test_all_parity_dropped_data_intact` | all 8 | `eventSuccess≥1`, `fecRecoveries=0`, `fecFailures=0` |
| `test_partial_parity_dropped_data_intact` | 4 of 8 | same |

### Category 8 — Multi-Block Events with Per-Block Loss

Events spanning multiple FEC blocks exercise independent per-block recovery and
the `fecBlocksCompleted == fecBlocksExpected` completion gate.

| Test | Block 0 | Block 1 | Block 2 | Expected |
|------|---------|---------|---------|---------|
| `test_multiblock_all_recoverable` | clean (fast path) | 1-col loss | 2-col loss | `fecRecoveries≥2`, data correct |
| `test_multiblock_one_unrecoverable` | clean | 1-col loss | 3-col loss | `fecFailures≥1`, `eventSuccess=0` |

For the unrecoverable case: whichever block fails first in the GC sweep deletes
the `EventQueueItem`.  Any subsequently recovered blocks find the event gone and
discard their data.

### Category 9 — Padded/Partial-Block Events

Events smaller than 32 × maxUserData bytes have `padFrames > 0`.  The GC uses
`dataMask = (1 << dataSegsNeeded) - 1` to ignore pad slots in the received-
segment bitmask.

| Test | Event size | Live segs | Expected |
|------|-----------|-----------|---------|
| `test_padded_block_no_loss` | 8 × 1400 B | 8 (segs 0–7) | fast path |
| `test_padded_block_one_column_loss` | 8 × 1400 B | 8 | 1-col recovery |
| `test_padded_block_loss_in_pad_range_no_effect` | 8 × 1400 B | 8 | proxy drops frames 8–31 which are never sent; no-op |
| `test_minimum_event_one_segment_no_loss` | 100 B | 1 (seg 0) | fast path |
| `test_minimum_event_one_segment_data_lost` | 100 B | 0 data | recovery (dataMask = 0x1) |

### Category 10 — Sequential Events, Independent Accounting

Multiple events sent through the same Segmenter/Reassembler.  Uses
`fec_pipeline_single_socket` (numSendSockets=1) so that each `sendEvent()` call
gets a monotonically incrementing `fecBlockNum`, allowing the proxy loss pattern
to be set for all events before any send.

| Test | Events | Expected |
|------|--------|---------|
| `test_sequential_events_alternating_loss` | A (recoverable), B (unrecoverable), C (clean) | `eventSuccess=2`, B lost, A and C data verified |
| `test_sequential_events_all_clean` | 4 events, no loss | all 4 arrive, stats accumulate |

### Category 11 — Minimum Sufficient Parity (Symbol-Level)

Tests that recovery works when the minimum number of **complete parity symbols**
is present.  Dropping 4 of 8 parity segments while keeping the other 4 (one
complete parity symbol) is sufficient for 1-column recovery.

| Test | Lost column | Parity kept | Expected |
|------|------------|------------|---------|
| `test_one_col_one_parity_symbol` | col 5 | symbol 0 (frames 0–3) | `fecRecoveries≥1` |
| `test_two_col_two_parity_symbols` | cols 2, 6 | all 8 | `fecRecoveries≥1` |
| `test_two_col_loss_one_parity_symbol_each` | cols 3, 6 | all 8 | `fecRecoveries≥1` |

### Category 12 — dataMissing > 8 Boundary

The `dataMissing ≤ 8` guard prevents the decoder from being called when the
segment-level missing count exceeds the maximum correctable.

| Test | Dropped frames | dataMissing | Expected |
|------|---------------|------------|---------|
| `test_all_data_lost_some_parity` | all 32 | 32 | guard fires, `fecFailures≥1` |
| `test_nine_segments_lost_three_cols_unrecoverable` | 3 segs × cols 0, 4, 7 | 9 | both guards fire |

### Category 13 — Non-Contiguous Frame Loss

Losing a single frame from each of several columns produces the same column-
damage set as dropping all 4 frames of those columns.

| Test | Lost frames | Damaged cols | Expected |
|------|------------|-------------|---------|
| `test_one_frame_per_col_two_cols_recoverable` | frames 0 (col 0), 7 (col 1) | {0, 1} | `fecRecoveries≥1` |
| `test_one_frame_per_col_three_cols_unrecoverable` | frames 0 (col 0), 4 (col 1), 8 (col 2) | {0, 1, 2} | `fecFailures≥1` |

### Category 14 — Proxy Counter Integrity

Validates that the proxy infrastructure itself accounts for all packets
correctly, giving confidence that test outcomes reflect actual loss and not
proxy bugs.

| Test | Event | Loss | Expected proxy counters |
|------|-------|------|------------------------|
| `test_proxy_counters_no_loss` | 1 full block | none | rx=40, fwd=40, drop=0 |
| `test_proxy_counters_with_column_loss` | 1 full block | col 3 (4 frames) | rx=40, drop=4, fwd=36 |

### Category A — MTU / Segment Size Variation

Exercise the same recovery logic at MTU 500 (small segments) and MTU 9000
(jumbo frames).  `mtu_params()` derives correct event sizes so the block
geometry (8 cols × 4 rows = 32 data segs, 2 parity cols) is identical across
all MTU values.

| Test | MTU | Event size | Loss | Expected |
|------|-----|-----------|------|---------|
| `test_mtu500_no_loss_single_block` | 500 | 12 800 B (1 block) | none | fast path |
| `test_mtu500_one_col_recoverable` | 500 | 12 800 B | col 0 | `fecRecoveries≥1` |
| `test_mtu500_two_col_recoverable` | 500 | 12 800 B | cols 2, 5 | `fecRecoveries≥1` |
| `test_mtu500_three_col_unrecoverable` | 500 | 12 800 B | cols 0, 3, 7 | `fecFailures≥1` |
| `test_mtu9000_no_loss_single_block` | 9000 | 284 800 B (1 block) | none | fast path |
| `test_mtu9000_one_col_recoverable` | 9000 | 284 800 B | col 7 | `fecRecoveries≥1` |
| `test_mtu9000_two_col_recoverable` | 9000 | 284 800 B | cols 0, 4 | `fecRecoveries≥1` |
| `test_mtu9000_padded_block_recovery` | 9000 | 8 segs (71 200 B) | col 1 | `fecRecoveries≥1` |

### Category B — Block Count Variation

Events spanning 2, 4, and 5 FEC blocks (existing tests cover only 1 and 3).

| Test | Blocks | Event size | Loss | Expected |
|------|--------|-----------|------|---------|
| `test_two_block_no_loss` | 2 | 89 600 B | none | fast path |
| `test_two_block_one_col_loss_each` | 2 | 89 600 B | blk1: col 5 | `fecRecoveries≥1` |
| `test_four_block_all_clean` | 4 | 179 200 B | none | fast path |
| `test_four_block_mixed_loss` | 4 | 179 200 B | blk1: col 0; blk3: cols 1,6 | `fecRecoveries≥2` |
| `test_five_block_one_unrecoverable` | 5 | 224 000 B | blk4: cols 0,3,7 | `fecFailures≥1` |

For recovery tests, at least one block completes via the fast path to anchor the
event in the reassembler while the lossy block awaits GC.

### Category C — Cross-Block Loss Patterns

Distinct loss patterns applied to every block within a multi-block event.
Verifies that independent per-block recovery does not contaminate other blocks.

| Test | Blocks | Loss pattern | Expected |
|------|--------|-------------|---------|
| `test_all_blocks_one_col_loss` | 3 | blk0: col 0, blk1: col 3, blk2: col 7 | `fecRecoveries≥3` |
| `test_escalating_loss_across_blocks` | 3 | blk0: 1-col, blk1: 2-col, blk2: clean | `fecRecoveries≥2` |
| `test_all_blocks_two_col_loss` | 3 | every block: cols 0,7 (max capacity) | `fecRecoveries≥3` |
| `test_cross_block_mixed_data_parity_loss` | 3 | blk0: col 2 + par 4–7; blk1: col 5 (all parity); blk2: clean | `fecRecoveries≥2` |
| `test_cross_block_one_fails_rest_recover` | 4 | blk0–2: 1-col each; blk3: cols 0,3,7 | `fecFailures≥1`, `eventSuccess=0` |

### Category D — Interleaved Events of Different Sizes

Multiple events with different block counts through the same pipeline.  Uses
`fec_pipeline_single_socket` so block numbers are monotonic and the proxy
correctly applies per-block loss patterns.

| Test | Events (sizes) | Loss | Expected |
|------|---------------|------|---------|
| `test_interleaved_different_sizes` | 1-blk, 3-blk, 1-blk | none | all 3 recovered |
| `test_interleaved_mixed_loss` | 1-blk, 2-blk, 1-blk | evt1 blk0: col 3; evt3 blk0: col 5 | all 3 recovered |
| `test_interleaved_one_lost` | 1-blk, 1-blk, 1-blk | evt2: cols 0,3,7 | evts 1+3 ok, evt 2 lost |
| `test_interleaved_padded_and_full` | 8-seg padded, 32-seg full | padded: col 0; full: col 7 | both recovered |

### Category E — Boundary / Edge-Case Event Sizes

Non-aligned event sizes exercising segment padding, block-boundary transitions,
and partial last segments.

| Test | Event size | Segs | Blocks | Loss | Expected |
|------|-----------|------|--------|------|---------|
| `test_block_boundary_33_segments` | 44 801 B | 33 | 2 | none | fast path (both blocks) |
| `test_block_boundary_33_segs_loss` | 44 801 B | 33 | 2 | blk0: col 2 | blk0 GC recovery, blk1 fast path |
| `test_non_aligned_event_size` | 10 000 B | 8 (partial last) | 1 | none | fast path |
| `test_non_aligned_with_recovery` | 10 000 B | 8 | 1 | col 0 | `fecRecoveries≥1` |
| `test_exactly_one_byte` | 1 B | 1 | 1 | none | fast path |

**Limitation discovered:** When every data segment of a 1-segment tail block is
dropped (sole data frame lost, only parity arrives), the GC cannot determine
`totalEventBytes` because that value is read from the RE header carried by data
packets.  Recovery from a fully-data-dropped padded block is therefore
impossible even when sufficient parity is present.  The test is designed to
lose a column from the full block 0 instead, leaving the 1-segment block 1 to
complete cleanly via fast path.

---

## Key Findings and Insights

### 1. Late-Arriving Parity Packets Triggered a Reassembler Bug

When the recv-thread fast path assembled a FEC block (all data present) and
erased it from `fecBlocksInProgress`, any subsequently arriving parity packets
re-created an orphaned `FecBlockState` with zero data received.  The GC then
saw 32 missing segments (unrecoverable), decremented `fecFailures`, and deleted
the in-progress `EventQueueItem` — corrupting multi-block events still being
assembled.

**Fix** (`src/e2sarDPReassembler.cpp`, `include/e2sarDPReassembler.hpp`): Added
`completedFecBlocks` (`std::unordered_set`) to `RecvThreadState`.  The fast path
inserts the block key on completion; the recv path silently discards any
subsequent packet whose key is already in the set.

### 2. Parity Segments Are Bit-Plane Slices, Not Symbols

The interleaver distributes each GF(16) parity symbol across 4 parity segments
(one bit per segment).  A single parity segment provides only 1 bit of the
4-bit symbol.  Attempting recovery with an incomplete parity symbol (e.g., only
parity segments {4, 5, 6, 7} when segment 0's symbol is needed) produces
silently wrong data — `rs_decode` returns 0 (success) but writes garbage for
the erased column.

The GC's parity-availability guard `popcount(parityReceived) ≥ damagedCols.size()`
counts **segments**, not complete symbols.  For a 1-column loss it requires only
1 segment, but 4 are needed to reconstitute one whole parity symbol.  Tests must
use complete symbol groups {0–3} or {4–7}.

### 3. Loss Patterns Must Be Set Before Sends

The UDP proxy processes packets asynchronously in a background thread.  If the
loss pattern is updated between consecutive `sendEvent()` calls, the proxy may
see packets from event N after the pattern has already been updated to the
specification for event N+1.  All per-block `LossSpec` entries for a test must
be loaded into the proxy with a single `set_loss_pattern({block0: spec0, block1:
spec1, ...})` call before the first send.

### 4. fecBlockNum Is Per Send-Socket, Not Per Event

The Segmenter has `numSendSockets` (default 4) send sockets, each with its own
`fecBlockNum` atomic counter.  `sendEvent()` round-robins across sockets; all
blocks of a single event go through the same socket (one `roundRobinIndex`
per call), so blocks within an event have consecutive numbers.  But across
multiple `sendEvent()` calls, successive calls use different sockets, each
starting their counter at 0.

For tests that key the proxy loss pattern by `fecBlockNum` across multiple
events, set `numSendSockets = 1` via `fec_pipeline_single_socket` so the
counter is monotonic across calls.

### 5. Python GC Must Be Forced Between Tests

The C++ `Segmenter` and `Reassembler` destructors call `stopThreads()`.  When
pytest runs fixtures with `yield from`, Python may defer garbage collection of
the previous test's objects until the middle of the next test's execution,
causing their C++ destructors (and thus `stopThreads()`) to run concurrently
with the live test.  Adding `del seg, reas, proxy; gc.collect()` at the end of
fixture teardown forces synchronous C++ destruction before the next fixture
starts.

### 6. Multi-Block Tests Need Extended Wait Windows

A 3-block event where blocks 1 and 2 need GC recovery requires the GC to
process them in the same sweep (possible if both timed out before the sweep
runs) or across two sweeps (requiring 2 × `eventTimeout_ms`).  Using a polling
loop with a 16× `eventTimeout_ms` deadline avoids race-sensitive fixed sleeps
while still bounding the test duration.

### 7. GC Recovery Requires at Least One Data Packet per Block

The GC reads `totalEventBytes` from the RE header embedded in each **data**
segment.  Parity segments do not carry this field.  If every data segment of a
block is dropped, the GC cannot determine how large the original event was and
therefore cannot verify recovery correctness.  `rs_decode` will run and may
return 0 (no error), but the recovered payload is meaningless because the event
size is unknown.

This affects any scenario where a block's data-to-pad ratio is 1:31 (a
1-segment tail block) and that sole data frame is lost.  The test
`test_block_boundary_33_segs_loss` is designed to avoid this by losing data
from the full block 0 instead.

### 8. Parity Symbol 1 Cannot Substitute for Symbol 0 in 1-Column Recovery

The GC's parity-availability guard counts segments (`popcount(parityReceived) ≥
damagedCols.size()`), which passes with as few as 1 segment for a 1-column
loss.  However, the underlying `rs_decode` call requires a **complete** parity
symbol (4 consecutive parity segments forming one GF(16) coefficient).

When parity symbol 0 (frames 0–3) is dropped and only symbol 1 (frames 4–7)
is kept, `rs_decode` returns 0 (success) but writes silently incorrect data for
the erased column.  The guard does not catch this.  Tests must use complete
symbol groups {0–3} or {4–7}; mixing across groups produces wrong output without
any error indication.  (`test_cross_block_mixed_data_parity_loss` uses only
symbol 0 on each lossy block for exactly this reason.)

---

## Running the Tests

```bash
# From the E2SAR repository root
export PYTHONPATH=build/src/pybind
export E2SARCONFIGDIR=$(pwd)

# Run all FEC back-to-back tests
pytest test/py_test/test_fec_b2b.py -m fec_b2b -v

# Run a specific category
pytest test/py_test/test_fec_b2b.py -m fec_b2b -v -k "multiblock"

# Run a single test
pytest test/py_test/test_fec_b2b.py::test_two_column_loss_recoverable -v
```

Total suite: **61 tests**, ~360 s on macOS with `eventTimeout_ms=1000`.

| Fixture / MTU | Approx. time |
|---------------|-------------|
| MTU 1500 (most tests) | ~5 s per GC-recovery test |
| MTU 500 (category A) | ~5 s per GC-recovery test |
| MTU 9000 (category A) | ~5–10 s per test (larger payloads) |

Fast-path tests (no loss, parity-only loss) return within ~0.1 s of the event
arriving.  GC-recovery tests return as soon as the GC processes the block,
typically within 1–2 s of sending.  Unrecoverable tests wait the full deadline
(`RECV_WAIT_S` = 4 s for single-block, `RECV_WAIT_S_MULTIBLOCK` = 16 s for
multi-block) to confirm the event does not arrive.
