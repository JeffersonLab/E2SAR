# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)
"""Utility for detecting the local source IP address for a given EJFAT URI."""

import socket
from urllib.parse import urlsplit, parse_qsl


def get_local_addr(url: str) -> str:
    """Parse the data= IP address from the url and return the corresponding
    local source IP address via a test UDP connection.

    Uses the first data= parameter if there are multiple.

    Returns: local IP address (string)
    Raises: ValueError on invalid input
    """
    scheme, netloc, path, query, fragment = urlsplit(url)
    if scheme not in ("ejfat", "ejfats"):
        raise ValueError(f"Invalid scheme ({scheme}) - must be ejfat or ejfats.")

    ip_port = ""
    for k, v in parse_qsl(query, keep_blank_values=False, strict_parsing=False, encoding="utf-8"):
        if k == "data":
            ip_port = v
            break
    if not ip_port:
        raise ValueError("URL query string must define data=")

    if ":" in ip_port:
        ip_addr, port_str = ip_port.split(":", 1)
        port = int(port_str)
    else:
        ip_addr = ip_port
        port = 80

    test_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    test_sock.connect((ip_addr, port))
    with test_sock:
        local_ip, _ = test_sock.getsockname()
    return local_ip
