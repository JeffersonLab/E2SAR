"""Back-to-back tests for the high-level e2sar Python package.

These tests run loopback send/receive on localhost without a real LB.
Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar).
"""

import pytest
import e2sar

URI_SEG = "ejfat://unused-host@192.168.100.1:9875/lb/1?sync=192.168.0.1:12345&data=127.0.0.1:19001"
URI_REAS = "ejfat://unused-host@192.168.100.1:9875/lb/1?sync=192.168.0.1:12345&data=127.0.0.1"
DATA_PORT = 19001


@pytest.mark.b2b
def test_send_recv_basic():
    """Send a short message and receive it back on localhost."""
    ctx = e2sar.Context()
    # Start reassembler first so the socket is bound before the sender fires.
    pull = ctx.pull(URI_REAS, data_ip="127.0.0.1", data_port=DATA_PORT)
    push = ctx.push(URI_SEG, data_id=0x0505, event_src_id=0x11223344)

    msg = b"hello EJFAT world"
    push.send(msg)

    received = None
    for _ in range(50):
        received = pull.recv(timeout_ms=100)
        if received is not None:
            break

    push.close()
    pull.close()

    assert received == msg


@pytest.mark.b2b
def test_send_recv_auto_ip():
    """Reassembler auto-detects local IP from URI when data_ip is empty."""
    ctx = e2sar.Context()
    pull = ctx.pull(URI_REAS, data_ip="", data_port=DATA_PORT)
    push = ctx.push(URI_SEG, data_id=0x0505, event_src_id=0x11223344)

    msg = b"auto ip test"
    push.send(msg)

    received = None
    for _ in range(50):
        received = pull.recv(timeout_ms=100)
        if received is not None:
            break

    push.close()
    pull.close()

    assert received == msg


@pytest.mark.b2b
def test_send_after_close():
    """Sending on a closed Segmenter raises RuntimeError."""
    ctx = e2sar.Context()
    push = ctx.push(URI_SEG, data_id=0x0505, event_src_id=0x11223344)
    push.close()

    with pytest.raises(RuntimeError):
        push.send(b"should fail")


@pytest.mark.b2b
def test_recv_after_close():
    """Receiving on a closed Reassembler raises RuntimeError."""
    ctx = e2sar.Context()
    pull = ctx.pull(URI_REAS, data_ip="127.0.0.1", data_port=DATA_PORT)
    pull.close()

    with pytest.raises(RuntimeError):
        pull.recv()


@pytest.mark.b2b
def test_context_manager_segmenter():
    """Context manager closes Segmenter on exit."""
    ctx = e2sar.Context()
    with ctx.push(URI_SEG) as push:
        push.send(b"context manager test")
    assert push._closed is True


@pytest.mark.b2b
def test_context_manager_reassembler():
    """Context manager closes Reassembler on exit."""
    ctx = e2sar.Context()
    with ctx.pull(URI_REAS, data_ip="127.0.0.1", data_port=DATA_PORT):
        pass
    # If no exception raised, the context manager worked


@pytest.mark.b2b
def test_stats_accessible():
    """send_count / recv_count / loss properties are readable."""
    ctx = e2sar.Context()
    pull = ctx.pull(URI_REAS, data_ip="127.0.0.1", data_port=DATA_PORT)
    push = ctx.push(URI_SEG, data_id=0x0505, event_src_id=0x11223344)

    push.send(b"stats test")

    assert isinstance(push.send_count, int)
    assert isinstance(push.send_errors, int)
    assert isinstance(pull.recv_count, int)
    assert isinstance(pull.recv_errors, int)
    assert isinstance(pull.enqueue_loss, int)
    assert isinstance(pull.reassembly_loss, int)

    push.close()
    pull.close()
