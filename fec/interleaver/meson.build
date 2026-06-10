# FEC interleaver library — RS(10,8)/GF(16) interleave + encode pipeline

fec_il_src = files(
    'src/interleaver.cpp',
    'src/deinterleaver.cpp',
    'src/pipeline.cpp',
    'src/rs_encode.cpp',
)

fec_il_inc = include_directories('include')
fec_il_link_args = []

# Metal GPU sources on macOS — only when the objcpp compiler is available
has_objcpp = add_languages('objcpp', required: false, native: false)
if host_machine.system() == 'darwin' and has_objcpp
    fec_il_src += files(
        'src/interleaver_metal.mm',
        'src/rs_encode_metal.mm',
    )
    fec_il_link_args += ['-framework', 'Metal', '-framework', 'Foundation']
endif

cpp_compiler = meson.get_compiler('cpp')

# The interleaver uses std::span which requires C++20
fec_il_cpp_args = ['-std=c++20', '-O3']
fec_il_objcpp_args = ['-std=c++20', '-O3']

libfec_interleaver = static_library('fec_interleaver',
    fec_il_src,
    include_directories: fec_il_inc,
    cpp_args: fec_il_cpp_args,
    objcpp_args: fec_il_objcpp_args,
    install: false,
)

fec_interleaver_dep = declare_dependency(
    include_directories: fec_il_inc,
    link_with: libfec_interleaver,
    link_args: fec_il_link_args,
)

# Test executable
fec_il_test = executable('fec_interleaver_test',
    'test/main.cpp',
    include_directories: fec_il_inc,
    link_with: libfec_interleaver,
    link_args: fec_il_link_args,
    cpp_args: fec_il_cpp_args,
    objcpp_args: fec_il_objcpp_args,
    dependencies: dependency('threads'),
)

test('FEC Interleaver Test', fec_il_test, suite: 'fec-interleaver', timeout: 120)
