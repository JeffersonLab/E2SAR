# FEC Integration Plan — fec2 Branch

## Context

The `fec2` branch implements RS(10,8)/GF(16) Forward Error Correction for E2SAR, enabling recovery from up to 2 lost packet columns per FEC block. The FEC library (`fec/`) is self-contained C/C++20 with no external dependencies. Large portions of the integration work are already on the branch: `fec/meson.build`, segmenter `_sendWithFec()`, reassembler GC decode path, Python bindings for `enableFec`, and a Python FEC b2b test. The branch needs to be rebased from main before any integration work is assessed or extended.

Three tasks: (1) meson build integration, (2) E2SAR codebase/app integration, (3) test framework integration.

---

## Prerequisite: Rebase fec2 from main

```bash
git fetch origin
git rebase origin/main
# resolve conflicts in shared files:
#   meson.build, src/meson.build, test/meson.build, bin/e2sar_perf.cpp
#   src/e2sarDPSegmenter.cpp, src/e2sarDPReassembler.cpp
#   include/e2sarDPSegmenter.hpp, include/e2sarDPReassembler.hpp
#   src/pybind/py_e2sarDP.cpp
```

Accept fec2 additions when in doubt; re-apply any main-branch changes that were overwritten.

---

## Task 1: Meson Build Integration

### 1.1 Validate the build after rebase

```bash
meson setup --reconfigure build   # or: rm -rf build && meson setup build
meson compile -C build
```

Fix any compile/link errors before proceeding.

### 1.2 Verify top-level meson.build wiring

**File:** `meson.build`

Check that `subdir('fec')` appears **before** `subdir('src')` (FEC must be built first so `fec_interleaver_dep` is defined when src/ consumes it). If missing or out of order, reorder the `subdir()` calls.

### 1.3 Verify src/meson.build consumes FEC correctly

**File:** `src/meson.build`

Ensure:
- `fec_interleaver_dep` is in the `dependencies:` list for `libe2sar_t`
- Both `libfec_interleaver.extract_all_objects(recursive: false)` and
  `libfec_rs_decode_c.extract_all_objects(recursive: false)` are in the monolithic `libe2sar` objects list

Pattern already present on branch — verify it survived the rebase.

### 1.4 Verify fec/meson.build and fec/interleaver/meson.build

**Files:** `fec/meson.build`, `fec/interleaver/meson.build`

Confirm:
- `libfec_interleaver` (C++20) and `libfec_rs_decode_c` (C) are built
- `fec_interleaver_dep = declare_dependency(include_directories: ..., link_with: ..., link_args: ...)` is declared
- Metal/Objective-C++ sources (`interleaver_metal.mm`, `rs_encode_metal.mm`) are conditionally included on macOS
- Architecture-conditional NEON/AVX2/AVX-512 source and test targets are correct

### 1.5 Add meson build summary entry

**File:** `meson.build` — in the `summary()` block at the bottom

Add a line reporting FEC status:
```meson
summary({'FEC enabled': true,
         'FEC NEON': neon_available,
         'FEC AVX2': avx2_available,
         'FEC Metal': (host_machine.system() == 'darwin' and has_objcpp)},
        section: 'FEC')
```

### 1.6 Run FEC low-level tests

```bash
meson test -C build --suite fec-basic --timeout 60
meson test -C build --suite fec-neon  --timeout 60   # ARM only
```

All tests must pass before moving to Task 2.

---

## Out of scope for this round

- **sendmmsg / liburing support in `_sendWithFec()`**: `_sendWithFec()` uses plain `sendmsg()` one call per packet (40 per FEC block). Wiring in the batch/async send optimizations requires pre-staging all headers and iovecs and changes to the per-packet malloc pattern. Deferred — get a correct working version first.

## Known bugs to fix

- **Double REHdr subtraction in colHeight calculation**: `getFecTotalHeaderLength()` includes `sizeof(REHdr)` as a wire header, but in the FEC data path REHdr is embedded inside the segment slot (part of the RS-protected payload), not a separate wire header. Then `fecMaxUserData = fecColHeight - sizeof(REHdr)` subtracts it again. Result: data packets are `sizeof(REHdr)` bytes short of MTU, wasting capacity on every data packet. Parity packets (which DO have REHdr as a wire header via `LBECREHdr`) fill the MTU correctly. Fix: compute `fecColHeight` without the REHdr subtraction, or introduce a separate `getFecDataHeaderLength()` that omits it.

---

## Task 2: E2SAR Codebase and Application Integration

### 2.1 Add --fec flag to e2sar_perf

**File:** `bin/e2sar_perf.cpp`

This is the only clearly missing integration item in the application layer. FEC can currently only be enabled via INI file; add a CLI flag.

Pattern to follow: look at how `--mtu`, `--rate`, or `--multiport` are declared in the `po::options_description` block.

Add to the options description block:
```cpp
("fec,F", po::bool_switch(&enableFec)->default_value(false),
    "enable FEC encoding/decoding (RS(10,8))")
```

Then pass it to both flags objects:
```cpp
sflags.enableFec = enableFec;
rflags.enableFec = enableFec;
```

The variable `enableFec` should be declared alongside other similar bool options near the top of main (e.g. near `withCP`, `multiPort`).

### 2.2 Review FIXME at Segmenter line ~418

**File:** `src/e2sarDPSegmenter.cpp`, line ~418

The comment says the error handling is already addressed via `lastErrno` in the stats block. Confirm the comment is accurate (i.e. the FEC path also updates stats on error) and either remove the FIXME or replace it with a brief rationale comment.

### 2.3 Verify Python bindings (already done — confirm survives rebase)

**File:** `src/pybind/py_e2sarDP.cpp`

Confirm after rebase that these lines survive:
- Line ~107: `.def_readwrite("enableFec", &Segmenter::SegmenterFlags::enableFec)`
- Line ~271: `.def_readwrite("enableFec", &Reassembler::ReassemblerFlags::enableFec)`
- Lines ~480-481: `.def_readonly("fecRecoveries", ...)` and `.def_readonly("fecFailures", ...)`

No new binding work needed unless the rebase dropped these lines.

---

## Task 3: Test Framework Integration

### 3.1 Add C++ Boost.Test FEC unit tests

**New file:** `test/e2sar_fec_test.cpp`  
**Suite name:** `DPFecTests`

These tests exercise the Segmenter + Reassembler with `enableFec=true` over loopback UDP, following the same pattern as `test/e2sar_seg_test.cpp` (DPSegTests) and `test/e2sar_reas_test.cpp` (DPReasTests):

| Test case | What it verifies |
|-----------|-----------------|
| `DPFecTest1` | Loopback send/receive with `enableFec=true`, no packet loss — event reassembles correctly |
| `DPFecTest2` | Loopback with 1 simulated column loss — `fecRecoveries > 0` after receive |
| `DPFecTest3` | `enableFec=false` on sender, `enableFec=true` on receiver — receiver falls back gracefully |

Pattern: use `EjfatURI` with `useCP=false`, `sflags.enableFec=true`, `rflags.enableFec=true`. Bind to 127.0.0.1 on a free port. Use a small MTU (e.g. 1500) with a modest payload (e.g. 64 KB) to avoid timeout issues.

Reference existing test structure: `test/e2sar_seg_test.cpp:DPSegTest1` for the send-side setup, `test/e2sar_reas_test.cpp:DPReasTest1` for the receive-side setup.

### 3.2 Register new C++ test in test/meson.build

**File:** `test/meson.build`

Add the executable and test registration following the existing pattern:

```meson
e2sar_fec_test = executable('e2sar_fec_test', 'e2sar_fec_test.cpp',
    include_directories: inc,
    link_with: libe2sar,
    link_args: linker_flags,
    dependencies: [boost_dep, grpc_dep, protobuf_dep])

test('DPFecTests', e2sar_fec_test,
    suite: 'unit',
    timeout: 120)
```

Suite `unit` keeps it consistent with the other loopback tests that run without external dependencies.

### 3.3 Verify Python pytest FEC marker and conftest.py

**File:** `test/py_test/conftest.py`

Confirm `fec-b2b` is declared as a known pytest marker:
```python
def pytest_configure(config):
    config.addinivalue_line("markers", "fec-b2b: FEC back-to-back tests with loss injection")
```

If missing, add it alongside the existing `b2b`, `unit`, `cp` marker declarations.

### 3.4 Verify test_fec_b2b.py and FecUdpProxy

**File:** `test/py_test/test_fec_b2b.py`

After rebase, confirm the file exists and the `@pytest.mark.fec-b2b` decorator is on each test function. The `FecUdpProxy` loss-injection harness should be in the same file or imported from a helper. No changes expected here unless the rebase dropped content.

### 3.5 Register Python FEC tests in meson

**File:** `test/meson.build`

Check if a `pytest` runner target for `test_fec_b2b.py` exists. If not, add it following the same pattern as other Python tests (if any), or document that FEC b2b tests are run manually with:

```bash
cd test/py_test
pytest -m fec-b2b -v
```

---

## What is Already Done on fec2 (survives rebase if no conflicts)

- `fec/` library: all source, tests, test vectors, docs
- `fec/meson.build` and `fec/interleaver/meson.build`
- `Segmenter::SegmenterFlags::enableFec`, `_sendWithFec()` method
- `Reassembler::ReassemblerFlags::enableFec`, GC thread FEC decode path
- `LBECHdr` / `LBECREHdr` packet headers in `include/e2sarHeaders.hpp`
- Python bindings: `enableFec` on both flags, `fecRecoveries`/`fecFailures` in stats
- `test/py_test/test_fec_b2b.py` with `FecUdpProxy` loss-injection harness
- `test/e2sar_fec_bench.cpp` FEC throughput benchmark

## Known Gaps

| Gap | File | Task |
|-----|------|------|
| `--fec` CLI flag | `bin/e2sar_perf.cpp` | 2.1 |
| FIXME review | `src/e2sarDPSegmenter.cpp` ~418 | 2.2 |
| C++ correctness unit tests | `test/e2sar_fec_test.cpp` (new) | 3.1–3.2 |
| pytest marker registration | `test/py_test/conftest.py` | 3.3 |
| Build validation post-rebase | varies | 1.1–1.6 |

---

## Verification

### Build verification
```bash
meson setup --reconfigure build
meson compile -C build
meson test -C build --suite fec-basic --timeout 60
```

### C++ unit test verification
```bash
meson test -C build --suite unit --timeout 0
# DPFecTests must appear and pass
```

### Python FEC test verification
```bash
export PYTHONPATH=/path/to/build/src/pybind
export E2SARCONFIGDIR=/path/to/E2SAR
cd test/py_test
pytest -m fec-b2b -v
```

### Application smoke test
```bash
./build/bin/e2sar_perf --fec -h   # verify --fec flag exists
```
