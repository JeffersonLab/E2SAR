# E2SAR Tests

This directory contains the test suite for E2SAR, including C++ unit tests and Python integration tests.

## Overview

The E2SAR test suite is organized into two main categories:

1. **C++ Unit Tests** - Standalone tests for core E2SAR components
2. **Python Tests** - Integration tests for the Python bindings

Tests are further categorized by execution requirements:
- **Unit Tests** (`suite: 'unit'`) - Standalone tests with no external dependencies
- **Live Tests** (`suite: 'live'`) - Tests requiring a live control plane instance

## C++ Unit Tests

### Test Executables

| Test | Source File | Description |
|------|-------------|-------------|
| `e2sar_uri_test` | `e2sar_uri_test.cpp` | URI parsing and validation tests |
| `e2sar_lbcp_test` | `e2sar_lbcp_test.cpp` | Load balancer control plane tests |
| `e2sar_sync_test` | `e2sar_sync_test.cpp` | Data plane synchronization tests |
| `e2sar_seg_test` | `e2sar_seg_test.cpp` | Segmentation functionality tests |
| `e2sar_reas_test` | `e2sar_reas_test.cpp` | Reassembly functionality tests |
| `e2sar_netutil_test` | `e2sar_netutil_test.cpp` | Network utility tests (platform-specific) |
| `e2sar_opt_test` | `e2sar_opt_test.cpp` | Optimization tests |
| `boost_test` | `boost_test.cpp` | Boost library integration tests |
| `memtest` | `mem_tests.cpp` | Memory management tests |
| `e2sar_test` | `e2sar_test.cpp` | General E2SAR tests |

### Live Tests (require running control plane)

| Test | Source File | Description |
|------|-------------|-------------|
| `e2sar_lbcp_live_test` | `e2sar_lbcp_live_test.cpp` | Live load balancer control plane tests |
| `e2sar_sync_live_test` | `e2sar_sync_live_test.cpp` | Live data plane synchronization tests |
| `e2sar_seg_live_test` | `e2sar_seg_live_test.cpp` | Live segmentation tests |
| `e2sar_reas_live_test` | `e2sar_reas_live_test.cpp` | Live reassembly tests |

## Python Tests

Located in `py_test/`, these tests verify the Python bindings and integration:

- `test_ejfatURI.py` - URI handling tests
- `test_DPSegmenter.py` - Data plane segmenter tests
- `test_DPReassembler.py` - Data plane reassembler tests
- `test_affinity.py` - CPU affinity tests
- `test_b2b_DP.py` - Back-to-back data plane tests
- `test_Optimizations.py` - Optimization tests
- `test_live_LBManager.py` - Live load balancer manager tests

## Building Tests

Tests are built using the Meson build system. From the E2SAR root directory:

### Initial Setup

```bash
# Set up the build environment (adjust paths as needed)
source setup_compile_env.sh

# Configure the build
meson setup builddir
```

### Build All Tests

```bash
# Build the entire project including tests
meson compile -C builddir
```

### Build Specific Test

```bash
# Build a specific test executable
meson compile -C builddir e2sar_uri_test
```

## Running Tests

### Run All Unit Tests

```bash
# Run all tests in the 'unit' suite
meson test -C builddir --suite unit
```

### Run All Live Tests

```bash
# Run all tests in the 'live' suite (requires control plane)
meson test -C builddir --suite live
```

### Run Specific Test

```bash
# Run a specific test by name
meson test -C builddir URITests

# Or run the executable directly
./builddir/test/e2sar_uri_test
```

### Run All Tests

```bash
# Run all tests (unit + live)
meson test -C builddir
```

### Verbose Test Output

```bash
# Show detailed test output
meson test -C builddir --verbose

# Or run tests with specific verbosity
meson test -C builddir -v
```

## Running Python Tests

Python tests use pytest and require the Python virtual environment to be activated:

```bash
# Activate the virtual environment (if not already active)
source /path/to/e2sar/venv/bin/activate

# Run all Python tests
cd test/py_test
pytest

# Run specific test file
pytest test_ejfatURI.py

# Run with verbose output
pytest -v

# Run specific test function
pytest test_ejfatURI.py::test_function_name
```

## Test Output

Test results will show:
- **PASS** - Test succeeded
- **FAIL** - Test failed with error details
- **SKIP** - Test was skipped (e.g., platform-specific tests)

## Dependencies

C++ tests require:
- Boost (>= 1.83.0, <= 1.86.0)
- gRPC++ (>= 1.51.1)
- Protocol Buffers
- pthreads

Python tests require:
- Python >= 3.9
- pytest
- pybind11
- protobuf (Python package)

## Platform Notes

- `e2sar_netutil_test` contains conditional compilation and may be a no-op on non-Linux platforms
- Live tests require a running instance of the E2SAR control plane
- Some network tests may require elevated privileges

## Troubleshooting

### Tests Not Found
If Meson can't find tests, ensure you've configured the build directory:
```bash
meson setup builddir --reconfigure
```

### Live Tests Failing
Ensure the control plane is running and accessible before executing live tests.

### Python Import Errors
Make sure the virtual environment is activated and the E2SAR Python bindings are built:
```bash
source /path/to/e2sar/venv/bin/activate
meson compile -C builddir
```

## Additional Resources

- [E2SAR Wiki](https://github.com/JeffersonLab/E2SAR/wiki)
- [Doxygen Documentation](https://jeffersonlab.github.io/E2SAR-doc/annotated.html)
- [Meson Build System](https://mesonbuild.com/)
