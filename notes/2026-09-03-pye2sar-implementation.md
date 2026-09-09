# pye2sar Integration — Implementation Notes (2026-09-03)

## What was done

Phase 1 of `notes/pye2sar_plan.md` is complete and merged on branch `feat/pye2sar`.

### C++ fix: `ReassemblerFlags.withLBHeader` removed

The field had a latent bug: the default constructor always initialized it to `false` regardless of `useCP`, because `withLBHeader{not useCP}` evaluated with `useCP=true` at construction time. When users later set `rflags.useCP = false`, the field stayed `false`, causing `badHeaderDiscards` on loopback tests (Reassembler tried to parse the LB header as an RE header).

**Resolution:**
- Removed `withLBHeader` from `ReassemblerFlags` entirely (`include/e2sarDPReassembler.hpp`)
- All 4 Reassembler constructors now initialize the internal `withLBHeader` member directly as `not rflags.useCP` (`src/e2sarDPReassembler.cpp`)
- Removed 3 stale `rflags.withLBHeader = true` lines from `test/e2sar_reas_test.cpp`
- All 7 C++ unit test suites pass after the change

### New Python package: `e2sar`

Added `src/python/e2sar/` — a ZMQ-inspired ergonomics wrapper over `e2sar_py`, adapted from [frobnitzem/pye2sar](https://github.com/frobnitzem/pye2sar) with author's permission.

**Files created:**
- `src/python/e2sar/__init__.py` — exports `Context`, `Segmenter`, `Reassembler`, `E2SARError`, `__version__`
- `src/python/e2sar/errors.py` — `E2SARError(Exception)`
- `src/python/e2sar/context.py` — `Context` factory (ZMQ-like `push()`/`pull()`)
- `src/python/e2sar/segmenter.py` — `Segmenter` wrapping `e2sar_py.DataPlane.Segmenter`; exposes all current `SegmenterFlags` fields as keyword args with defaults
- `src/python/e2sar/reassembler.py` — `Reassembler` wrapping `e2sar_py.DataPlane.Reassembler`; no `withLBHeader` arg (derived automatically from `useCP`)
- `src/python/e2sar/get_ip.py` — `get_local_addr(url)` auto-detects local source IP via UDP connect
- `src/python/e2sar/cli/send.py`, `recv.py` — argparse CLI tools (`python -m e2sar.cli.send/recv`)
- `src/python/meson.build` — `py.install_sources()` installs the package to `site-packages/e2sar/`

**Modified:**
- `src/meson.build` — `subdir('python')` added after `subdir('pybind')`

**Tests:**
- `test/py_test/test_highlevel_import.py` — 5 `unit`-marked smoke tests
- `test/py_test/test_highlevel_b2b.py` — 7 `b2b`-marked loopback tests (all pass)

### How to use without installing

```bash
export PYTHONPATH=/path/to/E2SAR/src/python:/path/to/E2SAR/build/src/pybind
export E2SARCONFIGDIR=/path/to/E2SAR
pytest test/py_test/ -m unit
pytest test/py_test/ -m b2b
```

Both paths are required: `src/python/` for the pure-Python `e2sar` package, `build/src/pybind/` for the compiled `e2sar_py` extension.

---

## Decision: Phase 2 (pyproject.toml / PyPI) not pursued

Phase 2 of `pye2sar_plan.md` proposed adding a `pyproject.toml` (backed by `meson-python`) to enable `pip install .` and eventual PyPI publication.

**Decision: skip indefinitely.**

**Reason:** The `e2sar_py` compiled extension is only available via the `ibaldin` conda channel, not PyPI. A PyPI-published `e2sar` wrapper would install successfully but fail at `import e2sar` with `ModuleNotFoundError: No module named 'e2sar_py'` — a confusing broken state. The dependency cannot be expressed in `pyproject.toml`, so pip cannot resolve it automatically.

The only scenario where PyPI publication adds real value is if `e2sar_py` is *also* published to PyPI as a self-contained wheel (with `auditwheel`/`delocate` bundling gRPC, Boost, and other native dependencies). That is a substantially larger build-infrastructure effort and is tracked separately if it becomes a priority.

**For now:** conda is the sole distribution channel. The Meson install step (`meson install -C build`) deposits both `e2sar_py.so` and the `e2sar/` package into the conda environment's `site-packages` automatically — no conda recipe changes required.
