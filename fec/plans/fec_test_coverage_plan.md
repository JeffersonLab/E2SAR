# FEC Test Coverage Improvement Plan

## Context

The current FEC B2B test suite (34 tests in `test/py_test/test_fec_b2b.py`) covers RS(10,8) erasure coding thoroughly for a single configuration: MTU 1500, segment payload 1400 B, single-block or 3-block events, no cross-block loss, and purely sequential sends. The FEC geometry (RS(10,8)) is fixed by design and will not change.

This plan closes the remaining coverage gaps: MTU/segment-size variation, block-count variation, cross-block loss patterns, concurrent event interleaving, and boundary/edge-case event sizes.

## Infrastructure Changes

**File:** `test/py_test/test_fec_b2b.py`

### 1. Parameterize `_make_pipeline` for MTU

Add an `mtu` parameter (default 1500) to `_make_pipeline()`. Derive `COL_HEIGHT` and `MAX_USER_DATA` from the MTU at runtime instead of using module-level constants. Add helper fixtures:

- `fec_pipeline_mtu500` — MTU 500 (colHeight=420, maxUserData=400, bytes_per_block=12,800)
- `fec_pipeline_mtu9000` — MTU 9000 (colHeight=8920, maxUserData=8900, bytes_per_block=284,800)

The existing `fec_pipeline` fixture (MTU 1500) stays unchanged.

### 2. Add `mtu_params()` helper

A small function that computes `(col_height, max_user_data, bytes_per_block)` from an MTU value, using the same formula as the C++ code: `col_height = mtu - 80`, `max_user_data = col_height - 20`, `bytes_per_block = max_user_data * 32`.

## New Tests

### Category A: MTU / Segment Size Variation (8 tests)

Exercise the same FEC recovery logic at different segment sizes. Uses `mtu_params()` to compute correct event sizes.

| # | Test | MTU | Event Size | Loss | Outcome |
|---|------|-----|-----------|------|---------|
| A1 | `test_mtu500_no_loss_single_block` | 500 | 1 block (12,800 B) | none | fast path |
| A2 | `test_mtu500_one_col_recoverable` | 500 | 1 block | col 0 dropped | FEC recovery |
| A3 | `test_mtu500_two_col_recoverable` | 500 | 1 block | cols 2,5 dropped | FEC recovery |
| A4 | `test_mtu500_three_col_unrecoverable` | 500 | 1 block | cols 0,3,7 dropped | lost |
| A5 | `test_mtu9000_no_loss_single_block` | 9000 | 1 block (284,800 B) | none | fast path |
| A6 | `test_mtu9000_one_col_recoverable` | 9000 | 1 block | col 7 dropped | FEC recovery |
| A7 | `test_mtu9000_two_col_recoverable` | 9000 | 1 block | cols 0,4 dropped | FEC recovery |
| A8 | `test_mtu9000_padded_block_recovery` | 9000 | 8 segs (71,200 B) | col 1 dropped | FEC recovery |

### Category B: Block Count Variation (5 tests)

Events spanning 2, 4, and 5 FEC blocks (existing tests only cover 1 and 3).

| # | Test | Blocks | Event Size | Loss | Outcome |
|---|------|--------|-----------|------|---------|
| B1 | `test_two_block_no_loss` | 2 | 89,600 B | none | fast path |
| B2 | `test_two_block_one_col_loss_each` | 2 | 89,600 B | blk0: col 2, blk1: col 5 | FEC recovery |
| B3 | `test_four_block_all_clean` | 4 | 179,200 B | none | fast path |
| B4 | `test_four_block_mixed_loss` | 4 | 179,200 B | blk1: col 0, blk3: cols 1,6 | FEC recovery |
| B5 | `test_five_block_one_unrecoverable` | 5 | 224,000 B | blk4: cols 0,3,7 | lost |

### Category C: Cross-Block Loss Patterns (5 tests)

Loss applied to multiple blocks within a single multi-block event — existing tests only apply loss to one block at a time.

| # | Test | Blocks | Loss Pattern | Outcome |
|---|------|--------|-------------|---------|
| C1 | `test_all_blocks_one_col_loss` | 3 | every block loses a different column (0, 3, 7) | FEC recovery (all 3 blocks) |
| C2 | `test_escalating_loss_across_blocks` | 3 | blk0: 1-col, blk1: 2-col, blk2: no loss | FEC recovery |
| C3 | `test_all_blocks_two_col_loss` | 3 | every block loses cols (0,7) | FEC recovery (all 3 blocks) |
| C4 | `test_cross_block_mixed_data_parity_loss` | 3 | blk0: col 2 + par 4-7, blk1: col 5 + par 0-3, blk2: clean | FEC recovery |
| C5 | `test_cross_block_one_fails_rest_recover` | 4 | blk0-2: 1-col loss each (recoverable), blk3: 3-col loss (unrecoverable) | lost |

### Category D: Concurrent / Interleaved Events (4 tests)

Test multiple events sent sequentially through the same pipeline with different sizes. Uses `fec_pipeline_single_socket` to guarantee block number ordering for the proxy.

| # | Test | Events | Sizes | Loss | Outcome |
|---|------|--------|-------|------|---------|
| D1 | `test_interleaved_different_sizes` | 3 | 1-block, 3-block, 1-block | none | all 3 recovered |
| D2 | `test_interleaved_mixed_loss` | 3 | 1-block, 2-block, 1-block | evt1-blk0: col 3, evt2 clean, evt3-blk0: col 5 | all recovered |
| D3 | `test_interleaved_one_lost` | 3 | 1-block, 1-block, 1-block | evt1 clean, evt2: 3-col (lost), evt3 clean | evts 1+3 ok, evt 2 lost |
| D4 | `test_interleaved_padded_and_full` | 2 | 8-seg padded, full 32-seg | padded: col 0 lost, full: col 7 lost | both recovered |

### Category E: Boundary / Edge-Case Event Sizes (5 tests)

Non-aligned event sizes that exercise segment padding, block-boundary transitions, and partial last segments.

| # | Test | Event Size | Segments | Blocks | Description | Loss | Outcome |
|---|------|-----------|----------|--------|-------------|------|---------|
| E1 | `test_block_boundary_33_segments` | 32×1400+1 = 44,801 B | 33 | 2 | Smallest 2-block event: block 1 has 1 data seg, 31 pad | none | fast path |
| E2 | `test_block_boundary_33_segs_loss` | 44,801 B | 33 | 2 | Same, lose block 1's sole data segment | blk1: frame 0 | FEC recovery |
| E3 | `test_non_aligned_event_size` | 10,000 B | 8 (last partial) | 1 | Not divisible by maxUserData — last segment has padBytes | none | fast path |
| E4 | `test_non_aligned_with_recovery` | 10,000 B | 8 | 1 | Same with 1-col loss | col 0 | FEC recovery |
| E5 | `test_exactly_one_byte` | 1 B | 1 | 1 | Minimum possible event | none | fast path |

## Implementation Approach

1. Add `mtu_params(mtu)` helper function near the existing constants
2. Add `mtu` parameter to `_make_pipeline()`, passing it through to `SegmenterFlags.mtu`
3. Add the new fixtures (`fec_pipeline_mtu500`, `fec_pipeline_mtu9000`)
4. Add all 27 new tests, grouped by category with docstrings matching the table descriptions
5. All new tests use the `@pytest.mark.fec_b2b` marker

## Verification

```bash
cd test/py_test
PYTHONPATH=../../build/src/pybind E2SARCONFIGDIR=../../ pytest test_fec_b2b.py -m fec_b2b -v
```

Expect 61 tests (34 existing + 27 new), all passing. MTU 9000 tests will be slower due to larger payloads.
