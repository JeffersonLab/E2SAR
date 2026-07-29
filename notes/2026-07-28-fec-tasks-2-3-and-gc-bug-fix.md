# 2026-07-28 — FEC Tasks 2 & 3 complete; GC race-condition bug fixed

## Task 2: `e2sar_perf` CLI flag and segmenter FIXME — completed

**`bin/e2sar_perf.cpp`**
- Added `--fec` / `-F` boolean option (Boost.Program_options).
- `enableFec` bool wired to `sflags.enableFec` and `rflags.enableFec` on both
  the sender and receiver paths, including the INI-file override path.
- Print line added for both sender and receiver startup blocks.

**`src/e2sarDPSegmenter.cpp` (~line 420)**
- Changed FIXME comment to NOTE (this was already done by the user earlier in the session).

---

## Task 3: FEC correctness unit tests — completed

**New file: `test/e2sar_fec_test.cpp`**

Three Boost.Test cases under suite `DPFecTests`:

| Test | Description |
|------|-------------|
| `DPFecTest1` | No-loss loopback, both sides FEC=true, 64 KB, MTU=1500, port 20200. Verifies full encode→decode round-trip. |
| `DPFecTest2` | Loopback through a UDP proxy (`FecColumnDropProxy`) that drops FEC data segment 0 from every block. Verifies `fecRecoveries > 0` and event reassembled correctly. |
| `DPFecTest3` | Sender FEC=false, receiver FEC=true. Verifies graceful fallback: non-FEC packets reassembled normally. |

The `FecColumnDropProxy` is an inline helper struct (port-forwarding UDP thread) that
reads `pFrameNum` at wire offset 22 (`LBHdrU` 16 bytes + ECHdr byte 6) to identify
and drop non-parity packets with `ecFrameNum == 0`.

**`test/meson.build`**
- `e2sar_fec_test` executable registered under `if enable_fec`, suite `unit`, timeout 120.

---

## Bug fixed: GC non-FEC sweep deletes multi-block FEC events prematurely

### Root cause

`e2sarDPReassembler.cpp` — `GCThreadState::_threadBody()`.

For a 64 KB event at MTU=1500, the segmenter produces **2 FEC blocks** (47 segments:
32 in block 0, 15 in block 1). Block 0's first packet arrives at the reassembler a
few milliseconds before block 1's first packet, because the proxy relays ~40 block-0
packets before block-1 packets start. This creates a non-zero delta between
`blk0->firstSegment` and `blk1->firstSegment`.

If the GC fires in the window `(T_blk0 + eventTimeout_ms, T_blk1 + eventTimeout_ms)`:

1. **FEC block sweep:** block 0 timed out → recovered (`fecRecoveries=1`,
   `fecBlocksCompleted=1`). Block 1 not yet timed out → skipped.
2. **Non-FEC sweep** (runs immediately after in the same GC iteration): event's
   `firstSegment = T_blk0`, `inWaiting > eventTimeout_ms` → **event deleted** from
   `eventsInProgress`.
3. **Next GC iteration:** block 1 times out → recovered (`fecRecoveries=2`), but
   `eit == eventsInProgress.end()` (event already gone). `fecBlocksCompleted` never
   reaches 2. `eventSuccess` never incremented. `recvEvent` times out.

Observable symptoms: `fecRecoveries=2`, `fecFailures=0`, `eventSuccess=0`,
`recvEvent` returns -1.

### Fix (`src/e2sarDPReassembler.cpp`, non-FEC GC sweep)

Before the non-FEC sweep deletes a timed-out FEC event (`fecBlocksExpected > 0`),
it checks whether any FEC blocks for that event are still pending in
`fecBlocksInProgress`. If yes, skip — the FEC block GC owns the event's lifecycle.
If no pending blocks remain (e.g. sender dropped mid-event), fall through to normal
deletion so the event doesn't leak.

```cpp
#ifdef E2SAR_ENABLE_FEC
if (reas.enableFec && it->second->fecBlocksExpected > 0) {
    auto evtNum = it->second->eventNum;
    auto dId    = it->second->dataId;
    bool hasPending = false;
    for (auto &fb : i->fecBlocksInProgress) {
        if (std::get<0>(fb.first) == evtNum &&
            std::get<1>(fb.first) == dId) {
            hasPending = true;
            break;
        }
    }
    if (hasPending) { ++it; continue; }
}
#endif
```

### Key FEC geometry (MTU=1500, IPv4, 64 KB)

```
getFecTotalHeaderLength = 20 (IP) + 8 (UDP) + 16 (LBHdrV2) + 16 (ECHdr) + 20 (REHdr) = 80
fecColHeight   = 1500 - 80 = 1420 bytes
fecMaxUserData = 1420 - 20 (REHdr) = 1400 bytes
numSegs        = ceil(65536 / 1400) = 47
numBlocks      = ceil(47 / 32) = 2   ← two-block event
```

---

## Build and test results

```
meson setup --wipe build -Denable_fec=true   # (with venv active)
meson compile -C build                        # 90 targets, zero errors
meson test -C build --suite unit --timeout 0
```

8/8 unit tests pass:
- LBCPTests, URITests, NetUtilTests, OptTests — fast
- **DPFecTests** — 7.87 s (previously FAILED with exit 201)
- DPSyncTests, DPReasTests, DPSegTests — pass unchanged

**Linker warning** (`ld: warning: ignoring duplicate libraries: '-lc++'`): pre-existing
on macOS, not introduced by FEC work. Meson adds `-lc++` explicitly while Clang's C++
driver also adds it implicitly. Harmless; ld ignores the duplicate.
