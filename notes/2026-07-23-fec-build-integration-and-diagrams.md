# 2026-07-23 — FEC build integration (Task 1) and send-path diagrams

## Task 1: Meson build integration — completed

### What was done

Wired FEC into the meson build as a conditional option (`-Denable_fec=true`), off by default.

**New file: `meson.options`**
- Declares `enable_fec` boolean option, default `false`.

**`meson.build`**
- Bumped project-wide `cpp_std` from `c++17` to `c++20`.
- Added conditional: when `enable_fec` is true, defines `-DE2SAR_ENABLE_FEC` and
  includes `subdir('fec')`.
- Added `FEC enabled` to the build summary section.

**`src/meson.build`**
- Conditionally adds `fec_interleaver_dep` to `libe2sar_t` dependencies.
- Conditionally extracts `libfec_interleaver` and `libfec_rs_decode_c` objects
  into the monolithic `libe2sar`.

**`test/meson.build`**
- Re-enabled `e2sar_fec_bench` under an `if enable_fec` guard (was commented out).

**Source files unchanged** — `#ifdef E2SAR_ENABLE_FEC` guards in
`e2sarDPSegmenter.cpp` and `e2sarDPReassembler.cpp` remain. The macro is defined
by the build system when FEC is enabled.

### Build validation

```
meson setup --wipe build -Denable_fec=true
meson compile -C build          # 88 targets, zero errors
meson test -C build --suite unit --suite fec-basic --suite fec-interleaver --suite fec-neon
```

All 15 tests pass:
- 7 unit tests (LBCPTests, URITests, NetUtilTests, OptTests, DPSyncTests, DPReasTests, DPSegTests)
- 6 fec-basic tests
- 1 fec-interleaver test
- 1 fec-neon test

---

## FEC send-path diagrams — completed

Created a set of 7 draw.io diagrams documenting the `_sendWithFec()` data flow,
following the plan in `notes/diagram_plan.md`.

**Files in `diagrams/`:**

| File | Content |
|------|---------|
| `fec_send_pipeline_overview.drawio` | End-to-end pipeline summary (Diagram 7) |
| `fec_01_event_partitioning.drawio` | Event buffer → segments → blocks (Diagram 1) |
| `fec_02_segment_buffer_layout.drawio` | segmentBuf layout: 32 rows × colHeight (Diagram 2) |
| `fec_03_interleave.drawio` | Bit-level transposition into codewords (Diagram 3) |
| `fec_04_rs_encoding.drawio` | RS(10,8)/GF(16) encoding of parity nibbles (Diagram 4) |
| `fec_05_parity_extraction.drawio` | Deinterleave parity → 8 parity segments (Diagram 5) |
| `fec_06_wire_packet_format.drawio` | Wire packet headers and send ordering (Diagram 6) |

Concrete parameters used throughout (MTU=1500, IPv4):
- colHeight = 1420 bytes
- maxUserData = 1400 bytes (colHeight − REHdr)
- 32 data packets + 8 parity packets = 40 packets per FEC block
- 11,360 codewords per block (colHeight × 8 bits)

---

## Next steps (from fec_plan.md)

- **Task 2:** Add `--fec` CLI flag to `bin/e2sar_perf.cpp`; review FIXME at Segmenter ~418
- **Task 3:** Add C++ Boost.Test FEC correctness unit tests (`test/e2sar_fec_test.cpp`)
