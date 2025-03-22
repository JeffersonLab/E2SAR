"""
The pytest for cpp class e2sar::Reassembler.

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
reas = e2sar_py.DataPlane.Reassembler
rflags = reas.ReassemblerFlags

# Use absolute path and match the top dir reaseembler_config.ini path
RFLAGS_INIT_FILE = "/home/xmei/ejfat_projects/E2SAR/reassembler_config.ini"

DATA_ID = 0x0505   # decimal value: 1285
EVENTSRC_ID = 0x11223344   # decimal value: 287454020
DP_IPV4_ADDR = "127.0.0.1"
DP_IPV4_PORT = 19522
REAS_URI = f"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}"


def init_reassembler():
    """Helper function to return a reas object with the top level INI file."""
    res = rflags.getFromINI(RFLAGS_INIT_FILE)
    assert res.has_error() is False
    flags = res.value()
    flags.useCP = False  # turn off CP. Default value is True
    flags.withLBHeader = True  # LB header will be attached since there is no LB

    assert isinstance(flags, rflags), "ReassemblerFlags object creation failed! "

    reas_uri = e2sar_py.EjfatURI(uri=REAS_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    assert isinstance(reas_uri, e2sar_py.EjfatURI)

    return reas(reas_uri,
        e2sar_py.IPAddress.from_string(DP_IPV4_ADDR), DP_IPV4_PORT, 1, flags)


@pytest.mark.unit
def test_rflags_getFromINI():
    """
    Test cpp Reassembler::ReassemblerFlags::getFromINI() function.
    """
    res = rflags.getFromINI(RFLAGS_INIT_FILE)
    # print(res.has_error(), res.error() if res.has_error() else None)
    assert res.has_error() is False
    flags = res.value()
    assert flags.useCP is True  # match the ini file


@pytest.mark.unit
def test_reas_constructor():
    """Test reassembler constructor simple."""

    reassembler = init_reassembler()
    assert isinstance(reassembler, reas), "Reassembler object creation failed! "


@pytest.mark.unit
def test_reas_constructor_core_list():
    """Test reassembler constructor with CPU core list."""
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


@pytest.mark.unit
def test_reas_constructor_core_list_auto_data_ip():
    """Test reassembler constructor with CPU core list and auto dataIP detection."""
    res = rflags.getFromINI(RFLAGS_INIT_FILE)
    assert res.has_error() is False
    flags = res.value()
    flags.useCP = False  # turn off CP. Default value is True
    flags.withLBHeader = True  # LB header will be attached since there is no LB

    assert isinstance(flags, rflags), "ReassemblerFlags object creation failed! "

    reas_uri = e2sar_py.EjfatURI(uri=REAS_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    assert isinstance(reas_uri, e2sar_py.EjfatURI)

    core_list=[0]
    reassembler = reas(reas_uri, DP_IPV4_PORT, core_list, flags, False)
    assert isinstance(reassembler, reas), "Reassembler object creation failed! "



@pytest.mark.unit
def test_get_stats():
    """Test Reassembler::getStats"""
    reassembler = init_reassembler()
    assert isinstance(reassembler, reas), "Reassembler object creation failed! "

    res = reassembler.getStats()
    assert isinstance(res, reas.ReportedStats)
    assert res.enqueueLoss  == 0, "Reassembler getStats wrong enqueueLoss! "
    assert res.lastE2SARError ==  e2sar_py.E2SARErrorc.NoError,\
        "Reassembler getStats wrong error code! "


@pytest.mark.unit
def test_get_fd_stats():
    """Test Reassembler::get_FDStats"""
    reassembler = init_reassembler()
    res = reassembler.get_FDStats()
    assert isinstance(res, e2sar_py.E2SARResultListOfFDPairs), "Reassembler get_FDStats failed! "
    assert res.has_error() is True
    assert res.error().code == e2sar_py.E2SARErrorc.LogicError


@pytest.mark.unit
def test_get_data_ip():
    """Test Reassembler::get_dataIP"""
    res = rflags.getFromINI(RFLAGS_INIT_FILE)
    assert res.has_error() is False
    flags = res.value()
    flags.useCP = False  # turn off CP. Default value is True
    flags.withLBHeader = True  # LB header will be attached since there is no LB

    assert isinstance(flags, rflags), "ReassemblerFlags object creation failed! "

    reas_uri = e2sar_py.EjfatURI(uri=REAS_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    assert isinstance(reas_uri, e2sar_py.EjfatURI)

    core_list=[0]
    reassembler = reas(reas_uri, DP_IPV4_PORT, core_list, flags, False)
    assert isinstance(reassembler, reas), "Reassembler object creation failed! "

    res = reas_uri.get_data_addr_v4()
    assert res.has_error() is False
    ip = res.value()[0]
    assert isinstance(ip, e2sar_py.IPAddress)
    assert str(ip) == DP_IPV4_ADDR, "EjfatURI get_data_addr_v4() failed!"

    ret_ip = reassembler.get_dataIP()
    assert ret_ip == str(ip), "Reassembler::get_dataIP() method failed!"


'''
def test_get_lost_event():
    """Test  Reassembler::get_LostEvent"""
    res = rflags.getFromINI(RFLAGS_INIT_FILE)
    assert res.has_error() is False
    flags = res.value()
    flags.useCP = False  # turn off CP. Default value is True
    flags.withLBHeader = True  # LB header will be attached since there is no LB

    assert isinstance(flags, rflags), "ReassemblerFlags object creation failed! "

    reas_uri = e2sar_py.EjfatURI(uri=REAS_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    assert isinstance(reas_uri, e2sar_py.EjfatURI)

    core_list=[0]
    reassembler = reas(reas_uri, DP_IPV4_PORT, core_list, flags, False)
    assert isinstance(reassembler, reas), "Reassembler object creation failed! "

    print(reassembler.get_LostEvent())  # Catch an unknown error instead
'''
