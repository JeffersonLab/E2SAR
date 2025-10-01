# Packet RS-FEC

Reed-Solomon Forward Error Correction (RS-FEC) library for packet loss recovery in the EJFAT project. This is a header-only library with platform-specific SIMD optimizations (ARM NEON, x86 AVX2/AVX512).

## Getting Started

### Python Prototypes

Install the following dependencies for Python prototypes:

```bash
pip install galois
```

## Building and Testing

### C Implementation (Makefile-based)

The FEC library is header-only and located in `src/`. Test programs can be built using the provided Makefile.

#### Build Requirements
- GCC or Clang compiler
- Standard C library
- No external dependencies required

#### Building Tests

Navigate to the `src/` directory and build the tests:

```bash
cd src/

# Build basic encoder test
make test

# Build ARM NEON SIMD test (ARM platforms only)
make test_neon

# Build full encoder/decoder test
make test_enc_dec

# Build AVX2 test (x86_64 platforms only)
make test_avx2

# Build AVX-512 test (x86_64 platforms only)
make test_avx512
```

#### Running Tests

```bash
# Basic RS encoder test with performance benchmarks
./test

# NEON encoder test (single/dual nibble modes)
./test_neon

# Full encoder/decoder validation with erasure recovery
./test_enc_dec

# Platform-specific SIMD tests
./test_avx2      # x86_64 only
./test_avx512    # x86_64 only
```

#### Expected Test Results

- **Basic encoder**: Validates RS(10,8) encoding with 3 implementations (basic, fast, NEON)
- **NEON test**: Verifies single/dual nibble encoders, zero handling, performance ~500+ Gbps
- **Encoder/decoder test**:
  - GF(16) arithmetic operations
  - Erasure recovery (0, 1, or 2 erasures)
  - Table-based decoding (30-40x faster than general decoder)
  - NEON optimized decoding performance

#### Clean Build Artifacts

```bash
cd src/
rm -f test test_neon test_enc_dec test_avx2 test_avx512
```

### Meson Build (E2SAR Integration)

When integrated into E2SAR, the FEC library is built as part of the E2SAR meson build system. The FEC headers are available via the `fec_dep` dependency.

To build FEC test programs with meson:

```bash
# From E2SAR root directory
meson setup build -Dbuild_fec_tests=true
ninja -C build
```

Note: This requires the full E2SAR build environment (gRPC, Boost, protobuf, pybind11).

## Architecture

### Header-Only Library

The FEC implementation is header-only, located in `src/`:

- `ejfat_rs.h` - Main RS encoder
- `ejfat_rs_decoder.h` - RS decoder with erasure recovery
- `ejfat_rs_neon.h` - ARM NEON SIMD optimizations
- `ejfat_rs_avx2.h` - x86 AVX2 optimizations
- `ejfat_rs_avx512.h` - x86 AVX-512 optimizations
- `ejfat_rs_common.h` - Common definitions and GF(16) operations

### Configuration

Current RS configuration: **RS(10,8)**
- 8 data symbols + 2 parity symbols
- GF(16) Galois field arithmetic
- Can recover from up to 2 erasures

## Learning from Prototypes

There are various scripts in the `prototype/python/` directory that demonstrate how RS encoding works. They are mostly self-explanatory for learning the theory behind Reed-Solomon codes.

