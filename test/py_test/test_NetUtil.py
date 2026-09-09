"""
Tests for the e2sar::NetUtil Python bindings.

Run with:
  export PYTHONPATH=<build>/src/pybind
  pytest test/py_test/test_NetUtil.py -m unit
"""

import pytest
import e2sar_py

net_util = e2sar_py.NetUtil


@pytest.mark.unit
class TestIsNonRoutable:
    """Test NetUtil.is_non_routable() for single addresses."""

    def test_loopback_v4(self):
        assert net_util.is_non_routable("127.0.0.1") is True

    def test_loopback_v6(self):
        assert net_util.is_non_routable("::1") is True

    def test_private_10(self):
        assert net_util.is_non_routable("10.0.0.1") is True

    def test_private_172(self):
        assert net_util.is_non_routable("172.16.0.1") is True

    def test_private_192(self):
        assert net_util.is_non_routable("192.168.1.1") is True

    def test_link_local_v4(self):
        assert net_util.is_non_routable("169.254.1.1") is True

    def test_link_local_v6(self):
        assert net_util.is_non_routable("fe80::1") is True

    def test_ula_v6(self):
        assert net_util.is_non_routable("fd00::1") is True

    def test_unspecified_v4(self):
        assert net_util.is_non_routable("0.0.0.0") is True

    def test_public_v4(self):
        assert net_util.is_non_routable("8.8.8.8") is False

    def test_public_v6(self):
        assert net_util.is_non_routable("2001:4860:4860::8888") is False

    def test_invalid_returns_false(self):
        assert net_util.is_non_routable("not-an-ip") is False


@pytest.mark.unit
class TestIsAnyNonRoutable:
    """Test NetUtil.is_any_non_routable() for address lists."""

    def test_all_public(self):
        assert net_util.is_any_non_routable(["8.8.8.8", "1.1.1.1"]) is False

    def test_one_private(self):
        assert net_util.is_any_non_routable(["8.8.8.8", "10.0.0.1"]) is True

    def test_empty_list(self):
        assert net_util.is_any_non_routable([]) is False


@pytest.mark.unit
class TestGetHostname:
    """Test NetUtil.get_hostname()."""

    def test_returns_string(self):
        res = net_util.get_hostname()
        assert res.has_error() is False
        assert len(res.value()) > 0


@pytest.mark.unit
class TestGetMTU:
    """Test NetUtil.get_mtu()."""

    def test_loopback(self):
        mtu = net_util.get_mtu("lo0")
        assert mtu >= 1500
