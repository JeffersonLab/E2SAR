"""
The pytest for cpp class e2sar::EjfatURI.

To make sure it's working, either append the path of "e2say_py.*.so" to sys.path. E.g,
# import sys

# sys.path.append(
#     '<my_e2sar_build_path>/build/src/pybind')

Or, add this path to PYTHONPATH, e.g,
# export PYTHONPATH=<my_e2sar_build_path>/build/src/pybind
"""

import sys
import pytest

# Make sure the compiled module is added to your path
import e2sar_py
ej_uri = e2sar_py.EjfatURI

# New format: dual sync (v4 + v6) and data port range
URI_STR = (
    "ejfat://token@192.188.29.6:18020/lb/36"
    "?sync=192.188.29.6:19020"
    "&sync=[::1]:19020"
    "&data=192.188.29.20:16384-32767"
)

# Single-port data address format
URI_SINGLE_PORT = (
    "ejfat://token@192.188.29.6:18020/lb/36"
    "?sync=192.188.29.6:19020"
    "&data=192.188.29.20:19522"
)

# No port (defaults to 16384-32767)
URI_NO_PORT = (
    "ejfat://token@192.188.29.6:18020/lb/36"
    "?sync=192.188.29.6:19020"
    "&data=192.188.29.20"
)


@pytest.mark.unit
@pytest.mark.skipif(sys.platform == 'darwin', reason='getDataplaneLocalAddresses requires Linux netlink')
def test_get_dp_local_addrs():
    """Test the get_dp_local_addr function using a locally routable data address."""
    # Use 127.0.0.1 so the routing lookup works on any machine (unlike the
    # facility IP in URI_STR which is unreachable from developer workstations).
    local_uri = ej_uri(
        "ejfat://token@127.0.0.1:18020/lb/1?data=127.0.0.1:10000",
        ej_uri.TokenType.instance,
    )
    assert isinstance(local_uri, ej_uri), "EjfatURI creation failed!"

    ips = local_uri.get_dp_local_addrs(False)
    assert isinstance(ips, list), "EjfatURI get_dp_local_addrs failed!"
    assert len(ips) > 0, "Expected at least one local address"


@pytest.mark.unit
def test_dual_sync_addresses():
    """URI with two sync= entries exposes both v4 and v6 sync addresses."""
    uri = ej_uri(URI_STR, ej_uri.TokenType.instance)
    assert uri.has_sync_addr_v4(), "Expected v4 sync address"
    assert uri.has_sync_addr_v6(), "Expected v6 sync address"

    v4 = uri.get_sync_addr_v4()
    assert not v4.has_error(), f"get_sync_addr_v4 failed: {v4}"
    addr_v4, port_v4 = v4.value()
    assert str(addr_v4) == "192.188.29.6"
    assert port_v4 == 19020

    v6 = uri.get_sync_addr_v6()
    assert not v6.has_error(), f"get_sync_addr_v6 failed: {v6}"
    addr_v6, port_v6 = v6.value()
    assert "1" in str(addr_v6)  # ::1 loopback
    assert port_v6 == 19020


@pytest.mark.unit
def test_data_port_range():
    """data= with explicit min-max range is parsed correctly."""
    uri = ej_uri(URI_STR, ej_uri.TokenType.instance)
    addr, min_p, max_p = uri.get_data_addr_v4()
    assert str(addr) == "192.188.29.20"
    assert min_p == 16384
    assert max_p == 32767

    pr = uri.get_data_port_range()
    assert pr == (16384, 32767)


@pytest.mark.unit
def test_data_single_port():
    """data= with a single port yields (port, port) as the range."""
    uri = ej_uri(URI_SINGLE_PORT, ej_uri.TokenType.instance)
    addr, min_p, max_p = uri.get_data_addr_v4()
    assert str(addr) == "192.188.29.20"
    assert min_p == 19522
    assert max_p == 19522


@pytest.mark.unit
def test_data_default_port_range():
    """data= with no port defaults to 16384-32767."""
    uri = ej_uri(URI_NO_PORT, ej_uri.TokenType.instance)
    _, min_p, max_p = uri.get_data_addr_v4()
    assert min_p == 16384
    assert max_p == 32767


@pytest.mark.unit
def test_uri_roundtrip_new_format():
    """EjfatURI parsed from a string and converted back produces a valid URI."""
    uri = ej_uri(URI_STR, ej_uri.TokenType.instance)
    out = str(uri)
    assert "sync=" in out
    assert "data=" in out
    assert "192.188.29.20" in out


@pytest.mark.unit
def test_set_data_addr():
    """set_data_addr with (addr, min_port, max_port) is reflected in getters."""
    uri = ej_uri(URI_STR, ej_uri.TokenType.instance)
    uri.set_data_addr("10.0.0.1", 20000, 21000)
    addr, min_p, max_p = uri.get_data_addr_v4()
    assert str(addr) == "10.0.0.1"
    assert min_p == 20000
    assert max_p == 21000


@pytest.mark.unit
def test_set_data_port_range():
    """set_data_port_range updates the port range without touching the address."""
    uri = ej_uri(URI_STR, ej_uri.TokenType.instance)
    uri.set_data_port_range(22000, 23000)
    pr = uri.get_data_port_range()
    assert pr == (22000, 23000)
