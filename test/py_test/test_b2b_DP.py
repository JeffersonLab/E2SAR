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
import time
import numpy as np

import e2sar_py

DP_IPV4_ADDR = "127.0.0.1"
DP_IPV4_PORT = 10000
DATA_ID = 0x0505   # decimal value: 1085
EVENTSRC_ID = 0x11223344   # decimal value: 287454020

SEG_URI = f"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}:{DP_IPV4_PORT}"
REAS_URI_ = f"ejfat://useless@192.168.100.1:9876/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}"

SEND_STR = "THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 1 SECOND."


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


def verify_result_obj(res_obj):
    assert res_obj.has_error() is False
    assert res_obj.value() == 0


@pytest.mark.b2b
def test_b2b_send_bytes_recv_list():
    """Back-to-back test for Segmenter::sendEvent() and Reassembler::getEvent()"""
    seg = get_segmenter()
    reas = get_reassembler()

    # Start the reassembler
    res = reas.OpenAndStart()
    verify_result_obj(res)

    # Start the segmenter
    res = seg.OpenAndStart()
    verify_result_obj(res)

    res = seg.getSendStats()
    assert res[1] == 0, "Error encountered after Segmenter opening send socket: {res[2]}"

    # Send the string
    send_context = SEND_STR.encode('utf-8')
    res = seg.sendEvent(send_context, len(send_context))
    verify_result_obj(res)

    res = seg.getSendStats()
    assert res[0] == 1, "segmenter.getSendStats did not get the correct msgCnt"  # sent only 1 message

    time.sleep(1)  # has to wait for a few cycles for the reassembler to recv the msg

    # Prepare the recv buffer. Python List is mutable. Use list[0] to get the context.
    recv_bytes_list = [None]
    res, recv_event_len, recv_event_num, recv_data_id = reas.getEvent(recv_bytes_list)
    assert res == 0   # the error code
    assert recv_data_id == DATA_ID, "recv_data_id did not match"
    assert recv_event_len == len(send_context), "recv_length did not match"
    recv_str = recv_bytes_list[0].decode('utf-8')
    assert recv_str == SEND_STR, "recv_str did not match"


@pytest.mark.b2b
def test_b2b_send_numpy_get_numpy():
    """
    Back-to-back test for Segmenter::sendEvent() and Reassembler::getEvent() with numpy interfaces.
    """

    seg = get_segmenter()
    reas = get_reassembler()

    # Start the reassembler
    res = reas.OpenAndStart()
    verify_result_obj(res)

    # Start the segmenter
    res = seg.OpenAndStart()
    verify_result_obj(res)

    # Send a 3D numpy array,
    # send_array = np.array([1, 2, 3, 4, 5, 6, 7, 8], dtype=np.float32).reshape((2, 2, 2))
    send_array = np.random.rand(1000, 1000, 40).astype(np.float32)  # up to 200 MB
    send_bytes = send_array.nbytes

    res = seg.sendNumpyArray(send_array, send_bytes)
    verify_result_obj(res)

    time.sleep(1)

    # Receive the numpy array
    res, recv_array, recv_event_num, recv_data_id = reas.get1DNumpyArray(np.float32().dtype)
    # print(res, recv_array.nbytes, recv_event_num, recv_data_id)
    assert res == 0   # the error code
    assert recv_data_id == DATA_ID, "recv_data_id did not match"
    assert recv_array.nbytes == send_bytes, "recv_length did not match"
    assert np.array_equal(send_array.flatten(), recv_array), "recv_array did not match"


# @pytest.mark.b2b
# Lauch this one with python istead of pytest -m
def test_b2b_send_numpy_queue_get_numpy():
    """
    Back-to-back test for Segmenter::addToSendQueue() and Reassembler::getEvent() with numpy interfaces.
    """

    seg = get_segmenter()
    reas = get_reassembler()

    # Start the reassembler
    res = reas.OpenAndStart()
    verify_result_obj(res)

    # Start the segmenter
    res = seg.OpenAndStart()
    verify_result_obj(res)

    # Send 1D numpy array 5 times
    num_elements = 50000000  # Push to 50e6 may fail
    # Create a 2D numpy array and each time send 1 row
    send_array = np.array([np.full((num_elements,), i, dtype=np.uint8) for i in range(5)])
    print(f"Send numpy array of {send_array.nbytes / 5000000} MB for 5 times")
    for i in range(5):
        send_bytes = send_array[i].nbytes
        res = seg.addToSendQueue(send_array[i], send_bytes)
        verify_result_obj(res)

    time.sleep(1)
    # Verify we sent 5 messages
    res = seg.getSendStats()
    assert res[1] == 0, f"Send error occured with {res[0]} msg sent"
    print(f"Sent msg count: {res[0]}, send MB: {send_array.nbytes / 1000000}")

    # Recieve via a while loop
    recv_bytes = 0
    i = sleep_time = 0
    while (True):
        i += 1
        res, recv_array, recv_event_num, recv_data_id = reas.get1DNumpyArray(np.int32().dtype)
        if (res == -2 or sleep_time > 4 or recv_bytes > send_array.nbytes):
            print(f"Receiving error after recv {recv_bytes} bytes")
            break

        if res == -1:
            sleep_time += 1
            time.sleep(5)
            continue

        recv_bytes += recv_array.nbytes
        print(f"Rd {i}, received {recv_array.nbytes / 1000000} MB, total {recv_bytes / 1000000} MB")

        if recv_bytes >= send_array.nbytes:
            break


test_b2b_send_numpy_queue_get_numpy()
