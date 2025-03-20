"""
The local back-to-back test to ensure the DP can send & recv events with Python interfaces.

To make sure it's working, either append the path of "e2say_py.*.so" to sys.path.
# import sys

# sys.path.append(
#     '<my_e2sar_build_path>/build/src/pybind')

Or, add this path to PYTHONPATH, e.g,
# export PYTHONPATH=<my_e2sar_build_path>/build/src/pybind
"""

import pytest
import e2sar_py

DP_IPV4_ADDR = "127.0.0.1"
DP_IPV4_PORT = 10000
DATA_ID = 0x0505   # decimal value: 1085
EVENTSRC_ID = 0x11223344   # decimal value: 287454020

SEG_URI = f"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}:{DP_IPV4_PORT}"
REAS_URI_ = f"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}"


def get_segmenter():
    seg_uri = e2sar_py.EjfatURI(uri=SEG_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    sflags = e2sar_py.DataPlane.SegmenterFlags()
    sflags.useCP = False  # turn off CP. Default value is True
    sflags.syncPeriodMs = 1000
    sflags.syncPeriods = 5
    return e2sar_py.DataPlane.Segmenter(seg_uri, DATA_ID, EVENTSRC_ID, sflags)

def get_reassembler():
    reas_uri = e2sar_py.EjfatURI(uri=REAS_URI_, tt=e2sar_py.EjfatURI.TokenType.instance)
    rflags = e2sar_py.DataPlane.ReassemblerFlags()
    rflags.useCP = False  # turn off CP. Default value is True
    rflags.withLBHeader = True  # LB header will be attached since there is no LB
    return e2sar_py.DataPlane.Reassembler(
        reas_uri, e2sar_py.IPAddress.from_string(DP_IPV4_ADDR), DP_IPV4_PORT, 1, rflags)


@pytest.mark.b2b
def test_get_segmenter_send_event():
    seg = get_segmenter()
    reas = get_reassembler()

