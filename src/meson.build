subdir('grpc')

e2sar_sources = ['e2sar.cpp', 'e2sarUtil.cpp', 'e2sarCP.cpp', 'e2sarDP.cpp', 'e2sarNetUtil.cpp']

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

py_install = import('python').find_installation('python3')

# check the version of Python by looking for python executable
py_exec = find_program('python3', required: true, version: '>=3.9.0')

pybind11_includes = run_command(
    py_install, '-c', 'import pybind11; print(pybind11.get_include());', check : true).stdout().strip()

py_install.extension_module(pybind11_module_name,
    sources: pybind11_src,
    include_directories: [inc, pybind11_includes],
    dependencies: [py_install.dependency(), boost_dep, grpc_dep, protobuf_dep],
    link_language : 'cpp',
    override_options: [
        'cpp_rtti=true'
    ],
    link_with: libe2sar,
    install : true
)
