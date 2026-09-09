"""Smoke tests for the high-level e2sar Python package."""

import pytest
import e2sar


@pytest.mark.unit
def test_import():
    assert e2sar.Context is not None
    assert e2sar.Segmenter is not None
    assert e2sar.Reassembler is not None
    assert e2sar.E2SARError is not None


@pytest.mark.unit
def test_e2sar_error_is_exception():
    assert issubclass(e2sar.E2SARError, Exception)
    err = e2sar.E2SARError("test error")
    assert str(err) == "test error"


@pytest.mark.unit
def test_context_has_push_pull():
    ctx = e2sar.Context()
    assert callable(ctx.push)
    assert callable(ctx.pull)


@pytest.mark.unit
def test_bad_uri_raises_e2sar_error():
    ctx = e2sar.Context()
    with pytest.raises(e2sar.E2SARError):
        ctx.push("not-a-valid-uri")


@pytest.mark.unit
def test_bad_uri_reassembler_raises_e2sar_error():
    ctx = e2sar.Context()
    with pytest.raises(e2sar.E2SARError):
        ctx.pull("garbage://invalid", data_ip="127.0.0.1", data_port=10000)
