# Building and Testing FEC

This guide explains how to build and test the EJFAT Reed-Solomon FEC implementation using either Meson or Make.

## Quick Start

### Using Make (Simplest)

```bash
cd fec/src
make test          # Build basic test
./tests/test       # Run the test
```

### Using Meson (Integrated with E2SAR)

From the E2SAR root directory:

```bash
meson setup builddir
meson compile -C builddir    # Build everything including FEC tests
meson test -C builddir --suite fec-basic
```

## Build Systems Comparison

| Feature | Make | Meson (Integrated) |
|---------|------|-------------------|
| Dependencies | C compiler only | Full E2SAR deps (gRPC, Boost, etc.) |
| Configuration | Automatic arch detection in compiler | Automatic arch detection + feature detection |
| Location | `fec/src/` | E2SAR root |
| Output Location | `fec/src/tests/` | `builddir/` |
| Use Case | FEC development/testing | Full E2SAR build |
| Test Runner | Direct execution | `meson test` |

## Building with Make

### Prerequisites

- C compiler (gcc, clang, or compatible)
- No external dependencies required

### Build Commands

```bash
cd fec/src

# Build specific tests
make test                    # Basic test
make test_enc_dec           # Encoder/decoder test
make test_neon              # NEON test (ARM only)
make test_avx2              # AVX2 test (with fallback on non-x86_64)
make test_avx512            # AVX-512 test (with fallback on non-x86_64)
```

### Run Commands

```bash
cd fec/src

# Run individual tests directly
./tests/test
./tests/test_enc_dec
./tests/test_neon
./tests/test_avx2
./tests/test_avx512
```

### Notes

- Test executables are built in the `tests/` directory
- AVX2 and AVX-512 tests automatically use scalar fallback on non-x86_64 platforms
- Each test target builds only that specific test (no `all` or `clean` targets)

## Building with Meson

### Prerequisites

- Meson build system (`pip install meson ninja`)
- C compiler
- Full E2SAR dependencies (for integrated build)

### Integrated Build (with E2SAR)

From the E2SAR root directory:

```bash
# Configure build (first time only)
meson setup builddir

# Build all FEC tests
meson compile -C builddir

# Build specific FEC test
meson compile -C builddir test
meson compile -C builddir test_neon          # ARM only
meson compile -C builddir test_avx2          # x86_64 only
```

### Run Tests

```bash
# Run all FEC tests
meson test -C builddir --suite fec-basic --suite fec-neon --suite fec-avx2

# Run specific test suite
meson test -C builddir --suite fec-basic
meson test -C builddir --suite fec-neon      # ARM only
meson test -C builddir --suite fec-avx2      # x86_64 only

# Run with verbose output
meson test -C builddir --suite fec-basic -v

# Run specific test by name
meson test -C builddir 'FEC Basic Test'
meson test -C builddir 'FEC NEON Test'

# Run executable directly
./builddir/test
./builddir/test_neon
```

## Architecture Support

The build system automatically detects your CPU architecture and builds appropriate tests:

### ARM (aarch64/arm64)
- ✅ Basic tests (common/)
- ✅ NEON tests (neon/)
- ❌ AVX2 tests (skipped)
- ❌ AVX-512 tests (skipped)

### x86_64
- ✅ Basic tests (common/)
- ❌ NEON tests (skipped)
- ✅ AVX2 tests (avx2/) - if CPU supports AVX2
- ✅ AVX-512 tests (avx512/) - if CPU supports AVX-512

### Cross-Platform Testing

On macOS with Apple Silicon, you can test x86 builds using Rosetta:

```bash
cd fec/src

# Build and run with Rosetta
arch -x86_64 make test_avx2
arch -x86_64 ./tests/test_avx2
```

## Test Suites

### Make Build Targets

| Target | Description | Output |
|--------|-------------|--------|
| `make test` | Build basic test | `tests/test` |
| `make test_enc_dec` | Build encoder/decoder test | `tests/test_enc_dec` |
| `make test_neon` | Build ARM NEON test | `tests/test_neon` |
| `make test_avx2` | Build AVX2 test | `tests/test_avx2` |
| `make test_avx512` | Build AVX-512 test | `tests/test_avx512` |

### Meson Test Suites

| Suite | Description | Platform |
|-------|-------------|----------|
| `fec-basic` | Basic encoder/decoder tests | All |
| `fec-neon` | ARM NEON SIMD tests | ARM only |
| `fec-avx2` | x86 AVX2 SIMD tests | x86_64 only |
| `fec-avx512` | x86 AVX-512 SIMD tests | x86_64 only |

## Troubleshooting

### Make Build Issues

**Problem**: `make: gcc: Command not found`
```bash
# Install gcc/clang
# macOS:
brew install gcc

# Linux (Ubuntu/Debian):
sudo apt install build-essential

# Linux (RHEL/CentOS):
sudo dnf install gcc
```

**Problem**: NEON tests fail to build on x86_64
- This is expected - NEON tests require ARM processors
- Use AVX2/AVX-512 tests instead on x86_64

### Meson Build Issues

**Problem**: Meson build fails with missing dependencies
- The integrated meson build requires full E2SAR dependencies
- Use Make for standalone FEC testing
- Or install missing dependencies (gRPC, Boost, protobuf)

**Problem**: Tests not found
```bash
# Reconfigure the build
meson setup builddir --reconfigure
```

## Development Workflow

### For FEC Development

Use Make for fastest iteration:

```bash
cd fec/src

# Edit code...

# Quick rebuild and test
make test && ./tests/test
```

### For E2SAR Integration

Use Meson for full integration testing:

```bash
# From E2SAR root
meson compile -C builddir
meson test -C builddir --suite fec-basic
```

## Performance Testing

Several tests include performance measurements:

```bash
# Run encoder/decoder test with performance metrics
cd fec/src
make test_enc_dec
./tests/test_enc_dec

# Run SIMD-optimized tests
make test_avx2       # x86_64
./tests/test_avx2

make test_neon       # ARM
./tests/test_neon
```

Performance metrics include:
- Encoding/decoding throughput (Mbps)
- Operations per second
- SIMD speedup vs scalar implementation

## Continuous Integration

### Recommended CI Commands

```bash
# Make-based CI
cd fec/src
make test && ./tests/test
make test_enc_dec && ./tests/test_enc_dec

# Meson-based CI (with E2SAR)
meson setup builddir
meson compile -C builddir
meson test -C builddir --suite fec-basic --suite fec-neon --suite fec-avx2 --print-errorlogs
```

## Additional Resources

- [FEC README](README.md) - Detailed architecture and test documentation
- [Main E2SAR README](../README.md) - E2SAR project documentation
- [Makefile](src/Makefile) - Simple Make build configuration
- [meson.build](meson.build) - Comprehensive Meson build configuration
