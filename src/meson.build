subdir('grpc')

e2sar_sources = ['e2sarUtil.cpp', 'e2sarCP.cpp', 
    'e2sarDPSegmenter.cpp', 'e2sarDPReassembler.cpp',
    'e2sarNetUtil.cpp']

libe2sar_t = static_library('e2sar_t',
                        e2sar_sources,
                        include_directories : inc,
                        dependencies : [grpc_dep, boost_dep, protobuf_dep],
                        install : true)

# build one library from the E2SAR and gRPC library pieces
libe2sar = static_library('e2sar',
                        objects : [libe2sar_t.extract_all_objects(recursive: false),
                                   liblbgrpc.extract_all_objects(recursive: false)])

#### Python bindings
pybind11_module_name = 'e2sar_py'

pybind11_src = ['pybind/py_e2sar.cpp']

# check the version of Python by looking for python executable
py_exec = find_program('python3', required: true, version: '>=3.9.0')

py_install = import('python').find_installation('python3')

## `pip install pybind11` to get Python pybind11 package
## NOTE: if using a virtual Python env, make sure it locates outside the root dir of this project,
# otherwise meson will struggle with absolute or relative path and cause troubles.
pybind11_include_dir = run_command(
    py_exec, '-c', 'import pybind11; print(pybind11.get_include());', check : true
    ).stdout().strip()

pybind11_inc = include_directories(pybind11_include_dir)

py_install.extension_module(pybind11_module_name,
    sources: pybind11_src,
    include_directories: [inc, pybind11_inc],
    dependencies: [py_install.dependency(), boost_dep, grpc_dep, protobuf_dep],
    link_language: 'cpp',
    override_options: [
        'cpp_rtti=true'
    ],
    link_with: libe2sar,
    install: true
)
