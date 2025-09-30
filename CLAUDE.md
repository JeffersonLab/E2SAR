# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **Reed-Solomon Forward Error Correction (RS-FEC)** implementation for packet loss recovery in the EJFAT project. The codebase includes both Python prototypes and optimized C implementations using ARM NEON SIMD instructions.

## Build and Test Commands

### C Implementation (src/)
```bash
# Build the test program
cd src && make test

# Run the test program  
cd src && ./test
```

### Python Prototypes (prototype/python/)
```bash
# Install required dependency
pip install galois

# Run the simplified matrix generator
cd prototype/python && python genMatrix_simplified.py
```

## Architecture

### Core Components

1. **Python Prototypes** (`prototype/python/`):
   - `genMatrix_simplified.py`: Core RS matrix generation and algorithm prototyping
   - `rs_model.h`: Generated C header with pre-computed Galois field tables and matrices
   - Learning-focused implementations demonstrating RS theory

2. **C Implementation** (`src/`):
   - `ejfat_rs.h`: Main RS encoder library with multiple optimization levels
   - `test.c`: Performance testing and validation program
   - Auto-generated lookup tables for GF(16) arithmetic

### RS Encoder Implementations

The C library provides three encoder variants with increasing optimization:

1. **`rs_encode()`**: Reference implementation using matrix-vector multiplication
2. **`fast_rs_encode()`**: Optimized version using pre-computed exponent tables  
3. **`neon_rs_encode()`**: ARM NEON SIMD implementation for maximum performance

### Key Data Structures

- **`rs_model`**: Contains RS parameters (n=8 data, p=2 parity symbols) and pre-computed matrices
- **`ejfat_rs_buf`**: Packet buffer management for encoding multiple symbols per packet
- **`rs_poly_vector`/`rs_poly_matrix`**: Polynomial arithmetic over GF(16)

### Galois Field Operations

Uses GF(16) with irreducible polynomial [1,0,0,1,1]. All arithmetic operations (multiplication, division) use pre-computed lookup tables for performance.

## Development Notes

- The Python code generates the C header file `rs_model.h` with pre-computed tables
- ARM NEON optimizations target 8 data symbols with 2 parity symbols
- Performance testing measures encoding throughput in Mbps
- Current configuration: RS(10,8) - 8 data + 2 parity symbols
- always use rosetta to build and test avx2 and avx512 tests
- only run neon tests on an arm platform