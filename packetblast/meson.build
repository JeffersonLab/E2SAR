##
## Created on Mar/19/2024 by xmei@jlab.org

project('packetblast', 'cpp', default_options : ['cpp_std=c++17',])

# Set the version of your project
version = '0.0.0'

# Dependencies
# Pthread. https://mesonbuild.com/Threads.html
cpp_dep = dependency('threads')

# Python bindings
'''
References:
- https://github.com/mesonbuild/meson/issues/4677
- https://github.com/the-risk-taker/meson-cmake-pybind11-cpp-python
'''
py_version = 'python3.10'
py_install = import('python').find_installation(py_version)

pybind11_config = find_program('pybind11-config')
pybind11_config_ret = run_command(pybind11_config, ['--includes'], check: true)
pybind11_dep = declare_dependency(
    include_directories: [pybind11_config_ret.stdout().split('-I')[-1].strip()],
)

py_module_name = 'packetblast'
py_install.extension_module(py_module_name, 
    sources : ['src/python_bindings/py_ejfat_packetize.cc'],
    include_directories: ['src', 'src/python_bindings'],
    dependencies: [cpp_dep, pybind11_dep],
    link_language : 'cpp',
    override_options: [
        'cpp_rtti=true',
    ]
)
