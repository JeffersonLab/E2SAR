"""
The pytest for cpp class e2sar::Segmenter.

To make sure it's working, either append the path of "e2say_py.*.so" to sys.path.
# import sys

# sys.path.append(
#     '/home/xmei/ejfat_projects/E2SAR/build/src/pybind')

Or, add this path to PYTHONPATH, e.g,
# export PYTHONPATH=/home/xmei/ejfat_projects/E2SAR/build/src/pybind
"""

import pytest

# Make sure the compiled module is added to your path
import e2sar_py
reas = e2sar_py.DataPlane.Reassembler

rflags = reas.ReassemblerFlags
# Use absolute path and match the top dir reaseembler_config.ini path
RFLAGS_INIT_FILE = "/home/xmei/ejfat_projects/E2SAR/reassembler_config.ini"

DATA_ID = 0x0505   # decimal value: 1285
EVENTSRC_ID = 0x11223344   # decimal value: 287454020
DP_IPV4_ADDR = "10.250.100.123"  # magic address taken from the e2sar_seg_test.cpp
# DP_IPV4_ADDR = "127.0.0.1"  # trigger E2SARException
DP_IPV4_PORT = 19522
REAS_URI = f"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}"


def test_rflags_getFromINI():
    """
    Test cpp Reassembler::ReassemblerFlags::getFromINI() function.
    """
    res = rflags.getFromINI(RFLAGS_INIT_FILE)
    # print(res.has_error(), res.error() if res.has_error() else None)
    assert res.has_error() is False
    flags = res.value()
    assert flags.useCP is True  # match the ini file 


def test_reas_constructor():
    """Test reassembler constructor simple."""
    res = rflags.getFromINI(RFLAGS_INIT_FILE)
    assert res.has_error() is False
    flags = res.value()
    flags.useCP = False  # turn off CP. Default value is True
    flags.withLBHeader = True  # LB header will be attached since there is no LB

    assert isinstance(flags, rflags), "ReassemblerFlags object creation failed! "

    reas_uri = e2sar_py.EjfatURI(uri=REAS_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    assert isinstance(reas_uri, e2sar_py.EjfatURI)

    reassembler = reas(reas_uri,
        e2sar_py.IPAddress.from_string(DP_IPV4_ADDR), DP_IPV4_PORT, 1, flags)
    assert isinstance(reassembler, reas), "Reassembler object creation failed! "


def test_reas_constructor_core_list():
    """Test reassembler constructor simple."""
    res = rflags.getFromINI(RFLAGS_INIT_FILE)
    assert res.has_error() is False
    flags = res.value()
    flags.useCP = False  # turn off CP. Default value is True
    flags.withLBHeader = True  # LB header will be attached since there is no LB

    assert isinstance(flags, rflags), "ReassemblerFlags object creation failed! "

    reas_uri = e2sar_py.EjfatURI(uri=REAS_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    assert isinstance(reas_uri, e2sar_py.EjfatURI)

    core_list=[0]
    reassembler = reas(reas_uri,
        e2sar_py.IPAddress.from_string(DP_IPV4_ADDR), DP_IPV4_PORT, core_list, flags)
    assert isinstance(reassembler, reas), "Reassembler object creation failed! "


if __name__ == "__main__":
    # pytest.main()
    test_reas_constructor_core_list()
