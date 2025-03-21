"""
The pytest for cpp class e2sar::EjfatURI.

To make sure it's working, either append the path of "e2say_py.*.so" to sys.path. E.g,
# import sys

# sys.path.append(
#     '<my_e2sar_build_path>/build/src/pybind')

Or, add this path to PYTHONPATH, e.g,
# export PYTHONPATH=<my_e2sar_build_path>/build/src/pybind
"""

import pytest

# Make sure the compiled module is added to your path
import e2sar_py
ej_uri = e2sar_py.EjfatURI

URI_STR = "ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20"


@pytest.mark.unit
def test_get_dp_local_addrs():
    """Test the get_dp_local_addr function."""

    uri = ej_uri(URI_STR, ej_uri.TokenType.instance)
    assert isinstance(uri, ej_uri), "EjfatURI creation failed!"

    ips = uri.get_dp_local_addrs(False)
    assert isinstance(ips, list), "EjfatURI get_dp_local_addrs failed!"
    # More strict validation: ips[0] equals to local IP address regardless of URI_STR


# if __name__ == "__main__":
#     pytest.main()
