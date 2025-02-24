#### Python bindings
# The module name to import in Python
pybind11_module_name = 'e2sar_py'

# message('Linker flags in Pybind11: ' + ' '.join(linker_flags) + '\n')

pybind11_src = [
    'py_e2sar.cpp',
    'py_e2sarCP.cpp',
    'py_e2sarDP.cpp',
    'py_e2sarHeaders.cpp',
    'py_e2sarUtil.cpp'
    ]

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
    dependencies: [py_install.dependency(),
        boost_dep, grpc_dep, protobuf_dep, thread_dep],
    link_language: 'cpp',
    override_options: [
        'cpp_rtti=true'
    ],
    link_with: libe2sar,
    link_args: linker_flags,  # Ensure linker flags are not empty
    install: true
)
