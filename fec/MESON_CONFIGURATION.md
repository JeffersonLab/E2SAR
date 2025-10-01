# FEC Meson Build Configuration

This document describes the Meson build configuration for the EJFAT Reed-Solomon FEC tests.

## Overview

The FEC subdirectory now includes a complete Meson build configuration (`fec/meson.build`) that:

1. **Automatically detects CPU architecture** (ARM, x86_64)
2. **Tests for SIMD instruction set support** (NEON, AVX2, AVX-512)
3. **Builds only supported tests** for your platform
4. **Integrates with E2SAR's main build system**
5. **Provides organized test suites** for easy testing

## Configuration File

The configuration is in `/fec/meson.build` and is automatically included when building E2SAR from the root directory.

## Features

### Architecture Detection

```meson
host_cpu = host_machine.cpu_family()
```

Detects:
- `aarch64` / `arm` → Enables NEON tests
- `x86_64` / `x86` → Enables AVX2/AVX-512 tests

### SIMD Capability Testing

The configuration includes compile-time tests for:

**ARM NEON**:
```c
#include <arm_neon.h>
int main() {
    uint8x16_t a = vdupq_n_u8(0);
    return 0;
}
```

**x86 AVX2**:
```c
#include <immintrin.h>
int main() {
    __m256i a = _mm256_setzero_si256();
    return 0;
}
```

**x86 AVX-512**:
```c
#include <immintrin.h>
int main() {
    __m512i a = _mm512_setzero_si512();
    return 0;
}
```

### Test Suites

The configuration defines four test suites:

| Suite | Tests | Platform Requirement |
|-------|-------|---------------------|
| `fec-basic` | `fec_test`, `fec_test_enc_dec` | All platforms |
| `fec-neon` | `fec_test_neon_enc`, `fec_test_neon_dec`, `fec_test_neon_enc_dec` | ARM with NEON |
| `fec-avx2` | `fec_test_avx2_enc`, `fec_test_avx2_dec`, `fec_test_avx2_enc_dec`, `fec_test_avx2_optimization` | x86_64 with AVX2 |
| `fec-avx512` | `fec_test_avx512_enc` | x86_64 with AVX-512 |

### Compiler Flags

**Common flags** (all tests):
```meson
fec_cflags = ['-O3', '-Wall']
```

**AVX2-specific flags**:
```meson
avx2_cflags = fec_cflags + ['-mavx2']
```

**AVX-512-specific flags**:
```meson
avx512_cflags = fec_cflags + ['-mavx512f', '-mavx512bw']
```

## Build Example

### Configuration Output

When running `meson setup`, you'll see:

```
Message: Building FEC tests for architecture: aarch64
Checking if "NEON support check" compiles: YES
Message: ARM NEON support: enabled
Message: NEON tests configured
Message: Skipping AVX2 tests (not supported on this platform)
Message: Skipping AVX-512 tests (not supported on this platform)
```

### Build Summary

At the end of configuration:

```
FEC Test Configuration
  FEC Basic Tests  : enabled
  FEC NEON Tests   : enabled
  FEC AVX2 Tests   : disabled (platform)
  FEC AVX-512 Tests: disabled (platform)
```

## Usage

### Building FEC Tests

From E2SAR root directory:

```bash
# Configure (first time)
meson setup builddir

# Build all FEC tests
meson compile -C builddir

# Build specific test
meson compile -C builddir fec_test
meson compile -C builddir fec_test_neon_enc    # ARM only
```

### Running FEC Tests

```bash
# Run all FEC basic tests
meson test -C builddir --suite fec-basic

# Run NEON tests (ARM only)
meson test -C builddir --suite fec-neon

# Run AVX2 tests (x86_64 only)
meson test -C builddir --suite fec-avx2

# Run all FEC tests
meson test -C builddir --suite fec-basic --suite fec-neon --suite fec-avx2 --suite fec-avx512

# Verbose output
meson test -C builddir --suite fec-basic -v
```

### Direct Execution

Test executables are built in the `builddir` directory:

```bash
# Run directly
./builddir/fec_test
./builddir/fec_test_enc_dec
./builddir/fec_test_neon_enc      # ARM only
./builddir/fec_test_avx2_enc      # x86_64 only
```

## Integration with E2SAR

The FEC meson.build is included in the main E2SAR build via:

```meson
# In root meson.build
subdir('fec')
```

This means:
- FEC tests build automatically when building E2SAR
- FEC tests use the same compiler and flags as E2SAR
- FEC test results appear in E2SAR's test summary

## Platform-Specific Behavior

### On ARM (Apple Silicon, ARM servers)

```
✅ fec_test
✅ fec_test_enc_dec
✅ fec_test_neon_enc
✅ fec_test_neon_dec
✅ fec_test_neon_enc_dec
❌ AVX2 tests (skipped)
❌ AVX-512 tests (skipped)
```

### On x86_64 (Intel/AMD)

```
✅ fec_test
✅ fec_test_enc_dec
❌ NEON tests (skipped)
✅ fec_test_avx2_enc (if CPU supports AVX2)
✅ fec_test_avx2_dec (if CPU supports AVX2)
✅ fec_test_avx2_enc_dec (if CPU supports AVX2)
✅ fec_test_avx2_optimization (if CPU supports AVX2)
✅ fec_test_avx512_enc (if CPU supports AVX-512)
```

## Troubleshooting

### No FEC Tests Built

**Symptom**: Meson completes but no FEC tests are built

**Solution**: Check that the `subdir('fec')` line exists in the root `meson.build`:
```bash
grep "subdir('fec')" meson.build
```

### Wrong Architecture Tests Built

**Symptom**: NEON tests on x86_64 or AVX2 tests on ARM

**Solution**: Meson automatically skips unsupported tests. If tests fail, they simply won't be built. Check meson output for "Skipping..." messages.

### AVX2/AVX-512 Tests Not Built on x86_64

**Symptom**: On x86_64 but AVX2 tests are skipped

**Solution**: Your CPU may not support AVX2/AVX-512. Check CPU capabilities:
```bash
# Linux
grep avx2 /proc/cpuinfo
grep avx512 /proc/cpuinfo

# macOS
sysctl -a | grep avx
```

## File Structure

```
fec/
├── meson.build                # Meson configuration (this file describes)
├── meson.standalone.build     # Standalone build (FEC only, no E2SAR deps)
├── src/
│   ├── Makefile              # Alternative Make-based build
│   ├── common/               # Platform-independent implementations
│   ├── neon/                 # ARM NEON implementations
│   ├── avx2/                 # x86 AVX2 implementations
│   ├── avx512/               # x86 AVX-512 implementations
│   └── tests/                # Test source files
├── README.md                 # Main FEC documentation
├── BUILDING.md               # Build instructions
└── MESON_CONFIGURATION.md    # This file
```

## Technical Details

### Include Directories

```meson
fec_inc = include_directories('src')
```

All tests can include headers relative to `fec/src/`:
```c
#include "common/ejfat_rs.h"
#include "neon/ejfat_rs_neon_encoder.h"
#include "avx2/ejfat_rs_avx2_encoder.h"
```

### Test Definition Example

```meson
fec_test_basic = executable('fec_test',
    fec_src_dir / 'tests' / 'test.c',
    include_directories: fec_inc,
    c_args: fec_cflags)

test('FEC Basic Test', fec_test_basic, suite: 'fec-basic')
```

This:
1. Creates an executable named `fec_test`
2. Compiles `src/tests/test.c`
3. Uses `src/` as include directory
4. Applies `-O3 -Wall` flags
5. Registers as a test named "FEC Basic Test" in suite "fec-basic"

## Comparison with Makefile

| Feature | Makefile | Meson |
|---------|----------|-------|
| Location | `fec/src/Makefile` | `fec/meson.build` |
| Output dir | `fec/src/` | `builddir/` |
| Test runner | `make run-*` | `meson test` |
| Integration | Standalone | Part of E2SAR build |
| Dependencies | None | E2SAR dependencies |
| Best for | FEC development | E2SAR integration |

Both build systems are fully supported and produce identical test executables.
