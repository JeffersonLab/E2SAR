"""
The pytest for cpp class e2sar::Affinity.

To make sure it's working, either append the path of "e2say_py.*.so" to sys.path.
# import sys

# sys.path.append(
#     '<my_e2sar_build_path>/build/src/pybind')

Or, add this path to PYTHONPATH, e.g,
# export PYTHONPATH=<my_e2sar_build_path>/build/src/pybind
"""

import pytest

# Make sure the compiled module is added to your path
import e2sar_py
affinity = e2sar_py.Affinity


@pytest.mark.unit
def test_set_process():
    """Test set_process() method."""
    cpu_cores = [0]
    res = affinity.set_process(cpu_cores)
    assert res.has_error() is False
    assert res.value() == 0, "Affinity set_process() did not succeed!"


@pytest.mark.unit
def test_set_thread():
    """Test set_thread() method."""
    cpu_core = 0
    res = affinity.set_thread(cpu_core)
    assert res.has_error() is False
    assert res.value() == 0, "Affinity set_thread() failed!"


@pytest.mark.unit
def test_set_thread_xor():
    """Test set_thread_xor() method."""
    cpu_cores = [1]
    res = affinity.set_thread_xor(cpu_cores)
    assert res.has_error() is False
    assert res.value() == 0, "Affinity set_thread_xor() failed!"


@pytest.mark.unit
def test_numa_bind():
    """Test set_numa_bind() method."""
    node = 0
    res = affinity.set_numa_bind(node)
    assert res.has_error() is False
    assert res.value() == 0, "Affinity set_numa_bind() failed!"
