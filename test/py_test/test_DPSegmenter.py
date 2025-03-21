"""
The pytest for cpp class e2sar::Segmenter.

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
seg = e2sar_py.DataPlane.Segmenter

sflags = seg.SegmenterFlags
# Use absolute path and match the top dir segmenter_config.ini path
SFLAGS_INIT_FILE = "/home/xmei/ejfat_projects/E2SAR/segmenter_config.ini"

DATA_ID = 0x0505   # decimal value: 1285
EVENTSRC_ID = 0x11223344   # decimal value: 287454020
DP_IPV4_ADDR = "127.0.0.1"
DP_IPV4_PORT = 19522
SEG_URI = f"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}:{DP_IPV4_PORT}"


def init_segmenter():
    """Helper function to create a segmenter object based on the top level INI file"""
    res = sflags.getFromINI(SFLAGS_INIT_FILE)
    flags = res.value()
    flags.useCP = False  # turn off CP. Default value is True

    seg_uri = e2sar_py.EjfatURI(uri=SEG_URI, tt=e2sar_py.EjfatURI.TokenType.instance)

    return e2sar_py.DataPlane.Segmenter(seg_uri, DATA_ID, EVENTSRC_ID, flags)


@pytest.mark.unit
def test_sflags_getFromINI():
    """
    Test cpp segmenter::segmenterFlags::getFromINI() function.
    """
    res = sflags.getFromINI(SFLAGS_INIT_FILE)
    # print(res.has_error(), res.error() if res.has_error() else None)
    assert res.has_error() is False
    flags = res.value()
    assert flags.mtu == 1500


@pytest.mark.unit
def test_seg_constructor_simple():
    """Test segmenter constructor simple."""
    res = sflags.getFromINI(SFLAGS_INIT_FILE)
    assert res.has_error() is False
    flags = res.value()
    flags.useCP = False  # turn off CP. Default value is True

    # Make sure flags is fully initialzed
    # print(flags.dpV6)
    # print(flags.connectedSocket)
    # print(flags.useCP)
    # print(flags.zeroRate)
    # print(flags.usecAsEventNum)
    # print(flags.syncPeriodMs)
    # print(flags.syncPeriods)
    # print(flags.mtu)
    # print(flags.numSendSockets)
    # print(flags.sndSocketBufSize)

    assert isinstance(flags, sflags), "SegmentationFlags object failure"

    seg_uri = e2sar_py.EjfatURI(uri=SEG_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    assert isinstance(seg_uri, e2sar_py.EjfatURI)

    segmenter = e2sar_py.DataPlane.Segmenter(seg_uri, DATA_ID, EVENTSRC_ID, flags)
    assert isinstance(
        segmenter, e2sar_py.DataPlane.Segmenter), "Segmenter object creation failed"


@pytest.mark.unit
def test_seg_constructor_from_cpucorelist():
    """Test segmenter constructor with CPU core list."""
    flags = sflags.getFromINI(SFLAGS_INIT_FILE).value()
    assert flags.mtu == 1500
    seg_uri = e2sar_py.EjfatURI(uri=SEG_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    assert isinstance(seg_uri, e2sar_py.EjfatURI)
    cpu_core_list = [0]     # Only one core allowed now?
    segmenter = e2sar_py.DataPlane.Segmenter(seg_uri, DATA_ID, EVENTSRC_ID, cpu_core_list, flags)
    assert isinstance(segmenter, e2sar_py.DataPlane.Segmenter), "Segmenter object creation failed"


@pytest.mark.unit
def test_get_send_stats():
    """Test Segmenter::getSendStats"""
    segmenter = init_segmenter()
    res = segmenter.getSendStats()
    assert isinstance(res, seg.ReportedStats), "Segmenter getSendStats failed! "
    assert res.msgCnt == 0, "Segmenter getSendStats wrong msgCnt! "
    assert res.lastE2SARError ==  e2sar_py.E2SARErrorc.NoError,\
        "Segmenter getSendStats wrong error code! "


@pytest.mark.unit
def test_get_sync_stats():
    """Test Segmenter::getSyncStats"""
    segmenter = init_segmenter()
    res = segmenter.getSyncStats()
    assert isinstance(res, seg.ReportedStats), "Segmenter getSyncStats failed! "
    assert res.msgCnt == 0, "Segmenter getSendStats wrong msgCnt! "
    assert res.lastE2SARError ==  e2sar_py.E2SARErrorc.NoError,\
        "Segmenter getSyncStats wrong error code! "
