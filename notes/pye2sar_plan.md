# pye2sar Integration Plan

Incorporate the ergonomic Python wrapper from [frobnitzem/pye2sar](https://github.com/frobnitzem/pye2sar) into the E2SAR repo as a first-class part of the Python distribution. The wrapper adds a ZMQ-inspired `Context`/`Segmenter`/`Reassembler` API on top of the raw `e2sar_py` pybind11 bindings, hiding C++ result-type error handling, verbose flags construction, and manual lifecycle management.

---

## Phase 1: Add as a pure-Python Meson-installed package

### Goal
Ship the wrapper via `meson install` so that:
- **Conda users**: rebuilding the existing conda package automatically delivers the new `e2sar` Python package to `site-packages` — no recipe changes needed.
- **Source-build users**: after `meson install`, `import e2sar` works from the install prefix alongside `e2sar_py`.
- **Dev/test**: adding `src/python/` to `PYTHONPATH` is sufficient without installing.

### Files to create

```
src/python/
  e2sar/
    __init__.py     # Context, Segmenter, Reassembler, E2SARError
    get_ip.py       # get_local_addr() — detects local UDP source IP from ejfat:// URI
    cli/
      __init__.py
      send.py       # python -m e2sar.cli.send
      recv.py       # python -m e2sar.cli.recv
  meson.build
```

Adapted from pye2sar with:
- Package name changed from `pye2sar` → `e2sar`
- `requires-python` relaxed from `>=3.10,<=3.11` → `>=3.9` (matches E2SAR)
- Attribution header added: `# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)`

### Files to modify

- `src/meson.build` — add `subdir('python')` alongside `subdir('pybind')`

### `src/python/meson.build` content

```meson
py = import('python').find_installation('python3', required: true)

py.install_sources(
  'e2sar/__init__.py',
  'e2sar/get_ip.py',
  subdir: 'e2sar',
)

py.install_sources(
  'e2sar/cli/__init__.py',
  'e2sar/cli/send.py',
  'e2sar/cli/recv.py',
  subdir: 'e2sar/cli',
)
```

`py.install_sources()` resolves `python.install_dir` at build time. In a conda build this is `$PREFIX/lib/pythonX.Y/site-packages/`, so no recipe changes are needed.

### Phase 1 Test Plan

**Smoke tests (unit — no hardware):**
- `test/py_test/test_highlevel_import.py` — `import e2sar; assert e2sar.Context()`
- Extend `test/py_test/test_highlevel_import.py` to verify `E2SARError` is an `Exception` subclass and that `Context`, `Segmenter`, `Reassembler` are importable

**Back-to-back loopback tests (b2b — no hardware):**
- `test/py_test/test_highlevel_b2b.py` — adapted from pye2sar `tests/test_b2b.py`:
  - `test_send_recv_basic`: spin up `Segmenter` + `Reassembler` on localhost, send `b"hello EJFAT world"`, assert receipt within 5 s
  - `test_error_bad_uri`: assert `E2SARError` on malformed URI
  - `test_send_after_close` / `test_recv_after_close`: assert `RuntimeError` after `close()`
  - `test_context_manager`: validate `with` block sets closed state

**Run commands:**
```bash
# Dev (no install needed)
export PYTHONPATH=/path/to/build/src/pybind:/path/to/E2SAR/src/python
cd test/py_test && pytest -m unit

# After meson install
meson install -C build
export PYTHONPATH=$MESON_INSTALL_PREFIX/lib/pythonX.Y/site-packages
pytest -m b2b
```

**Conda verification:**
```bash
conda build conda-recipe/   # or however the package is currently built
conda install --use-local e2sar
python -c "import e2sar; print(e2sar.__version__)"
```

---

## Phase 2: Add pyproject.toml for PyPI wheel creation

### Goal
Allow `pip install .` and production of a distributable wheel on PyPI. The wheel contains only the pure-Python `e2sar` package; `e2sar_py` (the compiled extension) remains a conda/system dependency and is declared as a runtime requirement.

### Approach

Use `meson-python` as the build backend, which reuses the existing `meson.build` to drive the build.

### Files to create / modify

**`pyproject.toml`** (repo root):
```toml
[build-system]
requires = ["meson-python"]
build-backend = "mesonpy"

[project]
name = "e2sar"
version = "0.4.0"          # keep in sync with VERSION.txt
requires-python = ">=3.9"
description = "High-level Python interface for E2SAR event segmentation and reassembly"
# e2sar_py is the compiled extension; it cannot be expressed as a PyPI dep
# because it is delivered via conda. Document this in README.
dependencies = []

[project.scripts]
e2sar-send = "e2sar.cli.send:main"
e2sar-recv = "e2sar.cli.recv:main"
```

**`meson.build`** (repo root) — expose the Python package to `meson-python` by ensuring `src/python/e2sar/` is reachable. `meson-python` discovers installed Python files via the Meson install manifest, so no extra wiring beyond Phase 1 is needed.

**`src/python/e2sar/__init__.py`** — add `__version__` attribute (read from `importlib.metadata` or hardcoded to match `VERSION.txt`).

### Constraints and notes

- The compiled `e2sar_py` extension **cannot** be listed as a PyPI `dependency` because it ships via conda, not pip. The `pyproject.toml` therefore lists no `dependencies`, and the README must document that `e2sar_py` must be installed separately (via conda or by building E2SAR from source).
- A wheel built on Linux will include only the pure-Python files; it is platform-independent (`py3-none-any`) if `e2sar_py` is excluded. Consider publishing as a universal wheel on PyPI as a thin ergonomics layer, with the expectation that users already have `e2sar_py` installed.
- Alternatively, a future phase could produce a self-contained wheel that bundles `e2sar_py.so` (using `auditwheel repair` on Linux), but that is significantly more complex.

### Phase 2 Test Plan

**Build and install via pip:**
```bash
# Editable install for development
pip install -e .

# Production wheel
pip wheel . --no-deps -w dist/
pip install dist/e2sar-*.whl
python -c "import e2sar; print(e2sar.__version__)"
```

**Entry points:**
```bash
e2sar-send --help
e2sar-recv --help
```

**Packaging metadata:**
```bash
pip show e2sar        # verify name, version, summary
python -m pytest test/py_test/ -m "unit or b2b"
```

**Wheel contents check:**
```bash
unzip -l dist/e2sar-*.whl   # should contain only e2sar/*.py, not e2sar_py.so
```

**CI addition**: add a `pip wheel . --no-deps` step to the existing CI workflow to catch packaging regressions.
