# Packet-RS-FEC: Reed-Solomon Forward Error Correction library
# This is a header-only library with optional test programs

# Define include directory for FEC headers
fec_inc = include_directories('src')

# Detect platform for SIMD optimizations
host_machine_cpu = host_machine.cpu_family()
is_x86_64 = host_machine_cpu == 'x86_64'
is_arm = host_machine_cpu == 'aarch64' or host_machine_cpu == 'arm'

# Create a declare_dependency for other parts of E2SAR to use
fec_dep = declare_dependency(
    include_directories: fec_inc
)

# Optionally build test programs (disabled by default for library integration)
build_fec_tests = get_option('build_fec_tests')

if build_fec_tests
    # Basic test
    executable('fec_test',
        'src/test.c',
        include_directories: [fec_inc, include_directories('prototype/python')],
        c_args: ['-O3'],
        install: false
    )

    # Platform-specific tests
    if is_x86_64
        # AVX2 test
        executable('fec_test_avx2',
            'src/test_avx2.c',
            include_directories: fec_inc,
            c_args: ['-O3', '-mavx2'],
            install: false
        )

        # AVX-512 test
        executable('fec_test_avx512',
            'src/test_avx512.c',
            include_directories: fec_inc,
            c_args: ['-O3', '-mavx512f', '-mavx512bw'],
            install: false
        )
    endif

    if is_arm
        # NEON test
        executable('fec_test_neon',
            'src/test_neon.c',
            include_directories: fec_inc,
            c_args: ['-O3'],
            install: false
        )
    endif

    # Encoder/decoder test
    executable('fec_test_enc_dec',
        'src/test_enc_dec.c',
        include_directories: [fec_inc, include_directories('prototype/python')],
        c_args: ['-O3'],
        install: false
    )
endif
