# E2SAR
The internal dev repo of E2SAR.

## Compile and build
We rely on `meson` and `ninja` to build and compile our C++ code and `pybind11` to create Python bindings.
Make sure they are correctly installed on your OS and the path is configured.

```bash
# Start a clean conda env based on python 3.10
conda create -n e2sar python=3.10
# Activate the conda env
conda activate e2sar
# Install pybind11
conda install meson ninja pybind11

# Make sure the path to `pybind11.pc` is added to `PKG_CONFIG_PATH`.
# Make sure the path to `Python.h` is added to `CPATH`.
# find / -name 'pybind11.pc'    # Locate `pybind11.pc` with `find`
export <parent_dir_of_pybind11.pc>:$PKG_CONFIG_PATH
export CPATH=<parent_dir_of_Python.h>:$CPATH
```
