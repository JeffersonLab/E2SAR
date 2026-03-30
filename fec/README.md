# EJFAT Reed-Solomon FEC Tests

This directory contains a **Reed-Solomon Forward Error Correction (RS-FEC)** implementation for packet loss recovery in the EJFAT project. The codebase provides optimized C implementations using SIMD instructions (ARM NEON, x86 AVX2, and AVX-512).

## Architecture

### Directory Structure

```
fec/
├── src/
│   ├── common/          # Reference implementations and common utilities
│   ├── neon/            # ARM NEON SIMD optimizations
│   ├── avx2/            # x86 AVX2 SIMD optimizations
│   ├── avx512/          # x86 AVX-512 SIMD optimizations
│   ├── tests/           # Test programs
│   └── Makefile         # Build system
├── prototype/python/    # Python prototypes for algorithm development
└── docs/                # Documentation
```

### Core Components

1. **Common Implementation** (`common/`):
   - `ejfat_rs.h`: Main RS encoder library
   - `ejfat_rs_decoder.h`: Reed-Solomon decoder
   - `ejfat_rs_tables.h`: Pre-computed Galois field lookup tables
   - `ejfat_rs_common.h`: Shared constants and definitions

2. **SIMD Optimizations**:
   - **NEON** (`neon/`): ARM NEON SIMD implementations for ARM processors
   - **AVX2** (`avx2/`): x86 AVX2 SIMD implementations for modern Intel/AMD processors
   - **AVX-512** (`avx512/`): x86 AVX-512 SIMD implementations for latest Intel processors

3. **Python Prototypes** (`prototype/python/`):
   - Algorithm prototyping and validation
   - Generates C header files with pre-computed tables

### RS-FEC Configuration

- **Current configuration**: RS(10,8) - 8 data symbols + 2 parity symbols
- **Galois Field**: GF(16) with irreducible polynomial [1,0,0,1,1]
- All arithmetic operations use pre-computed lookup tables for performance

## Test Suite

### Basic Tests (common/)

| Test | Description |
|------|-------------|
| `test` | Basic Reed-Solomon encoder validation and NEON verification |
| `test_enc_dec` | Encoder/decoder integration test with common implementation |

### ARM NEON Tests (neon/)

| Test | Description | Platform |
|------|-------------|----------|
| `test_neon_enc` | NEON encoder test | ARM only |
| `test_neon_dec` | NEON decoder test | ARM only |
| `test_neon_enc_dec` | NEON encoder/decoder integration test | ARM only |

### x86 AVX2 Tests (avx2/)

| Test | Description | Platform |
|------|-------------|----------|
| `test_avx2_enc` | AVX2 encoder test | x86_64 |
| `test_avx2_dec` | AVX2 decoder test | x86_64 |
| `test_avx2_enc_dec` | AVX2 encoder/decoder integration test | x86_64 |
| `test_avx2_optimization` | AVX2 performance comparison test | x86_64 |

### x86 AVX-512 Tests (avx512/)

| Test | Description | Platform |
|------|-------------|----------|
| `test_avx512_enc` | AVX-512 encoder test | x86_64 with AVX-512 |

## Building Tests

FEC tests can be built using either **Meson** (recommended, integrated with E2SAR) or **Make** (standalone).

### Building with Meson (Recommended)

Meson automatically detects your platform architecture and builds only the tests supported by your CPU.

#### Option 1: Integrated Build (Part of E2SAR)

From the E2SAR root directory:

```bash
# Configure the build (first time only)
meson setup builddir

# Build all FEC tests (part of the full E2SAR build)
meson compile -C builddir
```

Build specific FEC tests:
```bash
meson compile -C builddir fec_test
meson compile -C builddir fec_test_enc_dec
meson compile -C builddir fec_test_neon_enc      # ARM only
meson compile -C builddir fec_test_avx2_enc      # x86_64 only
meson compile -C builddir fec_test_avx512_enc    # x86_64 with AVX-512
```

**Note**: The integrated build requires full E2SAR dependencies (gRPC, Boost, etc.)

#### Option 2: Standalone Build (FEC Tests Only)

To build only FEC tests without E2SAR dependencies:

```bash
cd fec

# Use the standalone meson build configuration
cp standalone_meson.build meson.build

# Configure and build
meson setup builddir
meson compile -C builddir

# Restore the integrated build file when done
git restore meson.build
```

The standalone build has no external dependencies and only requires a C compiler.

Meson will skip tests not supported by your platform automatically.

### Building with Make (Standalone)

All tests can also be built using the Makefile in `fec/src/`.

#### Build All Tests

```bash
cd fec/src
make all
```

This builds all test executables. The Makefile automatically detects the architecture and applies appropriate SIMD flags.

#### Build Specific Tests

```bash
cd fec/src

# Build basic tests
make test
make test_enc_dec

# Build NEON tests (ARM only)
make test_neon_enc
make test_neon_dec
make test_neon_enc_dec

# Build AVX2 tests (x86_64)
make test_avx2_enc
make test_avx2_dec
make test_avx2_enc_dec
make test_avx2_optimization

# Build AVX-512 tests (x86_64)
make test_avx512_enc
```

### Platform-Specific Notes

- **ARM Platforms**: Build and run NEON tests natively
- **x86_64 Platforms**: Build and run AVX2/AVX-512 tests natively
- **macOS with Apple Silicon**: Use Rosetta to build and test AVX2/AVX-512 tests:
  ```bash
  arch -x86_64 make test_avx2_enc
  arch -x86_64 ./test_avx2_enc
  ```

## Running Tests

### Running with Meson (Recommended)

#### Run All FEC Tests

From the E2SAR root directory:

```bash
# Run all FEC tests
meson test -C builddir --suite fec-basic --suite fec-neon --suite fec-avx2 --suite fec-avx512

# Or with verbose output
meson test -C builddir --suite fec-basic --suite fec-neon --suite fec-avx2 --suite fec-avx512 -v
```

#### Run Test Suites by Architecture

```bash
# Run basic tests (all platforms)
meson test -C builddir --suite fec-basic

# Run NEON tests (ARM only)
meson test -C builddir --suite fec-neon

# Run AVX2 tests (x86_64)
meson test -C builddir --suite fec-avx2

# Run AVX-512 tests (x86_64 with AVX-512)
meson test -C builddir --suite fec-avx512
```

#### Run Individual Tests

```bash
# Run specific test by name
meson test -C builddir 'FEC Basic Test'
meson test -C builddir 'FEC NEON Encoder Test'
meson test -C builddir 'FEC AVX2 Optimization Test'

# Or run executables directly
./builddir/fec_test
./builddir/fec_test_enc_dec
./builddir/fec_test_neon_enc       # ARM only
./builddir/fec_test_avx2_enc       # x86_64 only
```

### Running with Make (Standalone)

#### Run All Tests

```bash
cd fec/src
make run-all
```

This builds and runs all tests sequentially, showing results for each test.

#### Run Test Suites by Architecture

```bash
cd fec/src

# Run basic tests (platform-independent)
make run-basic

# Run NEON tests (ARM only)
make run-neon

# Run AVX2 tests (x86_64)
make run-avx2

# Run AVX-512 tests (x86_64)
make run-avx512
```

#### Run Individual Tests

```bash
cd fec/src

# Run a specific test directly
./test
./test_enc_dec
./test_neon_enc
./test_avx2_optimization
```

### Output

Test programs output:
- Validation results (PASS/FAIL)
- Performance metrics (encoding throughput in Mbps)
- Detailed test information

Example output:
```
--- Basic Test ---
testing ARM NEON mode
adding 2+7 = 5
mult   2*7 = 14
Encoder test PASSED

--- AVX2 Optimization Test ---
Testing AVX2 encoder performance...
Throughput: 1234.56 Mbps
PASSED
```

## Clean Build

```bash
cd fec/src
make clean
```

This removes all compiled test executables and object files.

## Compiler Flags

The Makefile uses the following compiler settings:

- **Base flags**: `-O3 -Wall` (optimization level 3, all warnings)
- **AVX2 flags**: `-mavx2` (x86_64 only)
- **AVX-512 flags**: `-mavx512f -mavx512bw` (x86_64 only)
- **Include path**: `-I.` (current directory for header resolution)

## Dependencies

- **C Compiler**: gcc (or compatible compiler)
- **Architecture Support**:
  - ARM: NEON support (ARMv7-A or later)
  - x86_64: AVX2 support (Intel Haswell/AMD Excavator or later)
  - x86_64: AVX-512 support (Intel Skylake-X or later)

## Troubleshooting

### Tests Fail on Wrong Architecture

NEON tests require ARM processors. AVX2/AVX-512 tests require x86_64 processors with corresponding instruction set support.

**Solution**: Only run tests for your platform architecture, or use emulation (Rosetta on macOS).

### AVX-512 Tests Fail

AVX-512 requires newer processors. Not all x86_64 CPUs support AVX-512.

**Solution**: Verify CPU capabilities:
```bash
# Linux
grep avx512 /proc/cpuinfo

# macOS (x86_64)
sysctl -a | grep avx512
```

### Build Errors

Ensure you have a C compiler installed:
```bash
gcc --version
```

If using macOS without gcc, install via Homebrew:
```bash
brew install gcc
```

## Performance Testing

The optimization tests (e.g., `test_avx2_optimization`) compare performance between different implementations:
- Reference scalar implementation
- Optimized table-based implementation
- SIMD-optimized implementation

Use these to verify that SIMD optimizations provide expected performance improvements.

## Additional Resources

- [EJFAT Project Documentation](https://github.com/JeffersonLab/E2SAR)
- [Reed-Solomon Error Correction](https://en.wikipedia.org/wiki/Reed%E2%80%93Solomon_error_correction)
- [Galois Field Arithmetic](https://en.wikipedia.org/wiki/Finite_field_arithmetic)
