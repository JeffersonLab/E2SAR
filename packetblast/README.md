
A demo for creating python bindings to C++ codes using `pybind11`.

Binding code ([src/python_bindings/py_ejfat_packetize.cc](src/python_bindings/py_ejfat_packetize.cc)) defines a Python function `getMTU` based on the C++ method `ejfat::getMTU()` included in [ejfat_packetize.hpp](src/ejfat_packetize.hpp).

Check the [root README](../README.md) for path settings. Meson build the project.

```bash
cd packetblast
meson build # or `meson setup --wipe build`
ninja -C build

# Test whether the built python module is working
python3 python/test_binding.py build
# The output would look like:
## ioctl says MTU = 1280
## 1280
```
