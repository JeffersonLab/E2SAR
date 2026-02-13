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

# Use a different port from DPReassembler
SEG_URI = f"ejfat://useless@192.168.100.1:9875/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}:{DP_IPV4_PORT}"
REAS_URI_ = f"ejfat://useless@192.168.100.1:9875/lb/1?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}"

SEND_STR = "THIS IS A VERY LONG EVENT MESSAGE WE WANT TO SEND EVERY 1 SECOND."


def get_segmenter():
    """Create the segmenter instance with CP turned off."""
    seg_uri = e2sar_py.EjfatURI(uri=SEG_URI, tt=e2sar_py.EjfatURI.TokenType.instance)
    sflags = e2sar_py.DataPlane.Segmenter.SegmenterFlags()
    sflags.useCP = False  # turn off CP. Default value is True
    sflags.syncPeriodMs = 1000
    sflags.syncPeriods = 5
    sflags.rateGbps = 1.0
    return e2sar_py.DataPlane.Segmenter(seg_uri, DATA_ID, EVENTSRC_ID, sflags)

def get_reassembler():
    """Create the reassembler instance with CP turned off."""
    reas_uri = e2sar_py.EjfatURI(uri=REAS_URI_, tt=e2sar_py.EjfatURI.TokenType.instance)
    rflags = e2sar_py.DataPlane.Reassembler.ReassemblerFlags()
    rflags.useCP = False  # turn off CP. Default value is True
    rflags.withLBHeader = True  # LB header will be attached since there is no LB
    #rflags.eventTimeout_ms = 4500 # make sure event timeout is long enough to receive events
    return e2sar_py.DataPlane.Reassembler(
        reas_uri, e2sar_py.IPAddress.from_string(DP_IPV4_ADDR), DP_IPV4_PORT, 1, rflags)


def verify_result_obj(res_obj):
    """Helper function to check some of the return objects."""
    if res_obj.has_error():
        print(res_obj.error().message)
    assert res_obj.has_error() is False, f"{res_obj.error()}"
    assert res_obj.value() == 0


def supports_buffer_protocol(obj):
    """
    Helper function to test whether the data type supports Python buffer protocol.
    """
    try:
        memoryview(obj)
        return True
    except TypeError:
        return False


@pytest.mark.b2b
def test_b2b_send_bytes_recv_bytes():
    """
    Back-to-back test for Segmenter::sendEvent() and Reassembler::getEvent()
    using the py::bytes & List interface.
    """
    seg = get_segmenter()
    reas = get_reassembler()

    # Start the reassembler
    res = reas.OpenAndStart()
    verify_result_obj(res)

    # Start the segmenter
    res = seg.OpenAndStart()
    verify_result_obj(res)

    res = seg.getSendStats()
    assert res.msgCnt == 0,\
        f"Error encountered after Segmenter opening send socket: {res.lastE2SARError}"

    # Send the string
    send_context = SEND_STR.encode('utf-8')
    res = seg.sendEvent(send_context, len(send_context))
    verify_result_obj(res)

    res = seg.getSendStats()
    assert res.msgCnt == 1, "Segmenter::getSendStats msgCnt error"

    time.sleep(1)  # has to wait for a few cycles for the reassembler to recv the msg

    # Prepare the recv buffer. Python List is mutable. Use list[0] to get the context.
    recv_len, recv_bytes, recv_event_num, recv_data_id = reas.getEventBytes()
    assert recv_len >= 0, "Reassembler getEvent error! "   # the received nBytes
    assert recv_len == len(send_context), "recv_length did not match"
    assert recv_data_id == DATA_ID, "recv_data_id did not match"
    recv_str = recv_bytes.decode('utf-8')
    assert recv_str == SEND_STR, "recv_str did not match"

    reas.stopThreads()


# NOTE: Launch in the main suite will fail. Lauch with -m b2b will succeed.
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
    send_array = np.random.rand(100, 100, 50).astype(np.float32)  # up to 200 MB
    send_bytes = send_array.nbytes

    res = seg.sendNumpyArray(send_array, send_bytes)
    verify_result_obj(res)

    time.sleep(1)

    total_bytes = 0
    sleep_cnt = 0
    # Receive the numpy array
    while sleep_cnt < 5:
        print(f"Entering loop {sleep_cnt=}")
        recv_len, recv_array, recv_event_num, recv_data_id = reas.get1DNumpyArray(np.float32().dtype)
        if (recv_len == -2 or sleep_cnt > 4 or total_bytes >= send_array.nbytes):
            print(f"Receiving error")
            break

        if recv_len == -1:
            sleep_cnt += 1
            time.sleep(5)
            continue

        # success
        if recv_len > 0:
            break


    stats = reas.getStats()
    print(f"{stats.lastErrno=}")
    print(f"{stats.dataErrCnt=}")
    print(f"{stats.grpcErrCnt=}")
    print(f"{stats.reassemblyLoss=}")
    print(f"{stats.enqueueLoss=}")
    print(f"{stats.eventSuccess=}")

    lost = reas.get_LostEvent()
    if len(lost) > 0:
        print(f"Lost Event: {lost[0]}:{lost[1]} Fragments received: {lost[2]} ")
    else:
        print('No events lost')

    # print(recv_bytes, recv_array.nbytes, recv_event_num, recv_data_id)
    assert recv_len > 0
    assert recv_len == recv_array.nbytes
    assert recv_data_id == DATA_ID, "recv_data_id did not match"
    assert recv_array.nbytes == send_bytes, "recv_length did not match"
    assert np.array_equal(send_array.flatten(), recv_array), "recv_array did not match"

    reas.stopThreads()


# Callback function that will be executed when the event is sent
def send_completion_callback(cb_arg):
    """Called when the event transmission completes"""
    print(f"Event sent successfully! Callback argument: {cb_arg}")


@pytest.mark.b2b
def test_b2b_send_numpy_queue_recv_bytes():
    """
    Back-to-back test for Segmenter::addToSendQueue() and Reassembler::recvEvent().
    Send with numpy and receive with Python bytes.
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
    num_elements = 100
    # Create a 2D numpy array and each time send 1 row
    send_array = np.array([np.full((num_elements,), i, dtype=np.uint8) for i in range(5)])
    for i in range(5):
        # print(f'Sending {send_array[i]=}')
        send_bytes = send_array[i].nbytes
        callback_arg = {
            'event_id': i,
            'user_data': 'Important event marker'
        }
        res = seg.addNumpyArrayToSendQueue(numpy_array=send_array[i], nbytes=send_bytes, 
                                 callback=send_completion_callback, cbArg=callback_arg)
        verify_result_obj(res)

    time.sleep(1)

    # Print the msgCnt of SendStats
    res = seg.getSendStats()
    assert res.errCnt == 0, f"Send error occured with {res.msgCnt} msg sent"
    # print(f"Sent msg count: {res.msgCnt}, sent bytes: {send_array.nbytes}")

    # Recieve via a while loop
    total_bytes = 0
    i = sleep_cnt = 0
    while True:
        i += 1
        recv_len, recv_data, recv_event_num, recv_data_id = reas.recvEventBytes(wait_ms=100)
        if (recv_len == -2 or sleep_cnt > 4 or total_bytes >= send_array.nbytes):
            # print(f"Receiving error after recv {total_bytes} bytes")
            break

        if recv_len == -1:
            sleep_cnt += 1
            time.sleep(5)
            continue

        total_bytes += recv_len
        # Validate the recv buffer
        # Decode the buffer as numpy 1D array
        recv_array = np.frombuffer(recv_data, dtype=np.uint8)
        # search for input data - may not arrive in-order
        found = False
        for j in range(5):
            if np.array_equal(recv_array, send_array[j]):
                found = True
        assert found == True
        #assert np.array_equal(send_array[i - 1], recv_array)
        assert recv_data_id == DATA_ID

        if total_bytes >= send_array.nbytes:
            break

    reas.stopThreads()


# @pytest.mark.b2b
# Launch this one with python istead of pytest -m
#
# NOTE: It's not included in the b2b test because the numpy array size is big and may fail.
#
# A fail run:
# (e2sar) (e2sar) python test/py_test/test_b2b_DP.py
# Send numpy array of 200.0 MB for 5 times
# Sent msg count: 282863, send MB: 1000.0
# Rd 1, received 200.0 MB, total 200.0 MB
# Rd 2, received 200.0 MB, total 400.0 MB
# No message received, continuing
# Rd 4, received 200.0 MB, total 600.0 MB
# No message received, continuing
# No message received, continuing
# No message received, continuing
# No message received, continuing
# No message received, continuing
# Receiving error after recv 600000000 bytes
#
#@pytest.mark.skip(reason="Excluded from main suite")
@pytest.mark.skip(reason="Excluded from main suite")
def test_b2b_send_numpy_queue_get_numpy():
    """
    A "bonus" Back-to-back test for Segmenter::addToSendQueue() and Reassembler::getEvent()
    with numpy interfaces.
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
    num_elements = 100000000  # push to 50e6 may fail, depends on your platform
    # Create a 2D numpy array and each time send 1 row
    send_array = np.array([np.full((num_elements,), i, dtype=np.uint8) for i in range(5)])
    print(f"Send numpy array of {send_array.nbytes / 5000000} MB for 5 times")
    for i in range(5):
        send_bytes = send_array[i].nbytes
        res = seg.addToSendQueue(send_array[i], send_bytes)
        verify_result_obj(res)

    time.sleep(1)

    # Print the msgCnt of SendStats
    res = seg.getSendStats()
    assert res.errCnt == 0, f"Send error occured with {res.msgCnt} msg sent"
    print(f"Sent msg count: {res.msgCnt}, sent MB: {send_array.nbytes / 1000000}")

    # Recieve via a while loop
    total_bytes = 0
    i = sleep_cnt = 0
    while True:
        i += 1
        recv_len, recv_array, recv_event_num, recv_data_id = reas.get1DNumpyArray(np.uint8().dtype)
        if (recv_len == -2 or sleep_cnt > 4 or total_bytes >= send_array.nbytes):
            print(f"Receiving error after recv {total_bytes} bytes")
            break

        if recv_len == -1:
            sleep_cnt += 1
            time.sleep(5)
            continue

        total_bytes += recv_array.nbytes
        assert recv_data_id == DATA_ID
        print(
            f"Rd {i}, received {recv_array.nbytes / 1000000} MB, total {total_bytes / 1000000} MB")

        if total_bytes >= send_array.nbytes:
            break


test_b2b_send_numpy_queue_get_numpy()
