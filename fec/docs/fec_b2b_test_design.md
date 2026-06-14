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

## Test Architecture

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
col_height = 1500 − 80 = 1420 B per segment
maxUserData = 1420 − 20 (RE header) = 1400 B of user payload per segment
bytes per full block = 32 × 1400 = 44 800 B
packets per full block = 32 data + 8 parity = 40 UDP datagrams
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
loop with an 8× `eventTimeout_ms` deadline avoids race-sensitive fixed sleeps
while still bounding the test duration.

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

Total suite: 34 tests, ~165 s on macOS with MTU 1500 and `eventTimeout_ms=1000`.

Fast-path tests (no loss, parity-only loss) return within ~0.1 s of the event
arriving.  GC-recovery tests return as soon as the GC processes the block,
typically within 1–2 s of sending.  Unrecoverable tests wait the full deadline
to confirm the event does not arrive.
