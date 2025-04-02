"""
The pytest for cpp class e2sar::LBManager.

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
lbm = e2sar_py.ControlPlane.LBManager
ej_uri = e2sar_py.EjfatURI

URI_STR = "ejfats://udplbd@192.168.0.3:18347/lb/1?data=127.0.0.1&sync=192.168.88.199:1234"


# Not included yet
@pytest.mark.cp
def test_add_sender_self():
    """Test the add_sender_self function."""

    uri = ej_uri(URI_STR, ej_uri.TokenType.instance)
    assert isinstance(uri, ej_uri), "EjfatURI creation failed!"

    lb_manager = lbm(uri, False)
    assert isinstance(lb_manager, lbm), "LBManager creation error! "

    res = lb_manager.add_sender_self(False)
    assert isinstance(res, e2sar_py.E2SARResultInt), "LBManager add_sender_self error! "

    # TODO: require real LB to test the functionality

