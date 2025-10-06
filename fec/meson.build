# ============================================================================
# EJFAT Reed-Solomon FEC - Meson Build Configuration
# ============================================================================

c_compiler = meson.get_compiler('c')

# Detect architecture for SIMD support
host_cpu = host_machine.cpu_family()
message('Building FEC tests for architecture: ' + host_cpu)

# FEC source directory
fec_src_dir = meson.current_source_dir() / 'src'

# Include directories
fec_inc = include_directories('src', 'src/common', 'src/neon', 'src/avx2', 'src/avx512')

# Common flags
fec_cflags = ['-O3', '-Wall']

# Architecture-specific SIMD flags
avx2_available = false
avx512_available = false
neon_available = false

if host_cpu == 'x86_64' or host_cpu == 'x86'
    # Check for AVX2 support
    avx2_test = '''
    #include <immintrin.h>
    int main() {
        __m256i a = _mm256_setzero_si256();
        return 0;
    }
    '''
    if c_compiler.compiles(avx2_test, args: ['-mavx2'], name: 'AVX2 support check')
        avx2_available = true
        message('AVX2 support: enabled')
    endif

    # Check for AVX-512 support
    avx512_test = '''
    #include <immintrin.h>
    int main() {
        __m512i a = _mm512_setzero_si512();
        return 0;
    }
    '''
    if c_compiler.compiles(avx512_test, args: ['-mavx512f', '-mavx512bw'], name: 'AVX-512 support check')
        avx512_available = true
        message('AVX-512 support: enabled')
    endif
elif host_cpu == 'aarch64' or host_cpu == 'arm'
    # Check for NEON support
    neon_test = '''
    #include <arm_neon.h>
    int main() {
        uint8x16_t a = vdupq_n_u8(0);
        return 0;
    }
    '''
    if c_compiler.compiles(neon_test, name: 'NEON support check')
        neon_available = true
        message('ARM NEON support: enabled')
    endif
endif

# ============================================================================
# Basic Tests (common/)
# ============================================================================

fec_test_basic = executable('fec_test_basic',
    fec_src_dir / 'tests' / 'test.c',
    include_directories: fec_inc,
    c_args: fec_cflags)

fec_test_enc_dec = executable('test_enc_dec',
    fec_src_dir / 'tests' / 'test_enc_dec.c',
    include_directories: fec_inc,
    c_args: fec_cflags)

fec_test_dual_nibble = executable('test_dual_nibble',
    fec_src_dir / 'tests' / 'test_dual_nibble.c',
    include_directories: fec_inc,
    c_args: fec_cflags)

fec_test_single_nibble_verify = executable('test_single_nibble_verify',
    fec_src_dir / 'tests' / 'test_single_nibble_verify.c',
    include_directories: fec_inc,
    c_args: fec_cflags)

fec_verify_gf_mul = executable('verify_gf_mul',
    fec_src_dir / 'tests' / 'verify_gf_mul.c',
    include_directories: fec_inc,
    c_args: fec_cflags)

fec_verify_matrix = executable('verify_matrix',
    fec_src_dir / 'tests' / 'verify_matrix.c',
    include_directories: fec_inc,
    c_args: fec_cflags)

test('FEC Basic Test', fec_test_basic, suite: 'fec-basic')
test('FEC Encode/Decode Test', fec_test_enc_dec, suite: 'fec-basic')
test('FEC Dual Nibble Test', fec_test_dual_nibble, suite: 'fec-basic')
test('FEC Single Nibble Verify Test', fec_test_single_nibble_verify, suite: 'fec-basic')
test('FEC Verify GF Multiplication', fec_verify_gf_mul, suite: 'fec-basic')
test('FEC Verify Matrix', fec_verify_matrix, suite: 'fec-basic')

# ============================================================================
# ARM NEON Tests (neon/)
# ============================================================================

if neon_available
    fec_test_neon = executable('test_neon',
        fec_src_dir / 'tests' / 'test_neon.c',
        include_directories: fec_inc,
        c_args: fec_cflags)

    test('FEC NEON Test', fec_test_neon, suite: 'fec-neon')

    message('NEON tests configured')
else
    message('Skipping NEON tests (not supported on this platform)')
endif

# ============================================================================
# x86 AVX2 Tests (avx2/)
# ============================================================================

if avx2_available
    avx2_cflags = fec_cflags + ['-mavx2']

    fec_test_avx2 = executable('test_avx2',
        fec_src_dir / 'tests' / 'test_avx2.c',
        include_directories: fec_inc,
        c_args: avx2_cflags)

    test('FEC AVX2 Test', fec_test_avx2, suite: 'fec-avx2')

    message('AVX2 tests configured')
else
    message('Skipping AVX2 tests (not supported on this platform)')
endif

# ============================================================================
# x86 AVX-512 Tests (avx512/)
# ============================================================================

if avx512_available
    avx512_cflags = fec_cflags + ['-mavx512f', '-mavx512bw']

    fec_test_avx512 = executable('test_avx512',
        fec_src_dir / 'tests' / 'test_avx512.c',
        include_directories: fec_inc,
        c_args: avx512_cflags)

    test('FEC AVX-512 Test', fec_test_avx512, suite: 'fec-avx512')

    message('AVX-512 tests configured')
else
    message('Skipping AVX-512 tests (not supported on this platform)')
endif

# ============================================================================
# Summary
# ============================================================================

summary({
    'FEC Basic Tests': 'enabled (6 tests)',
    'FEC NEON Tests': neon_available ? 'enabled (1 test)' : 'disabled (platform)',
    'FEC AVX2 Tests': avx2_available ? 'enabled (1 test)' : 'disabled (platform)',
    'FEC AVX-512 Tests': avx512_available ? 'enabled (1 test)' : 'disabled (platform)',
}, section: 'FEC Test Configuration')
