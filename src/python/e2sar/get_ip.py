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

    if ip_port.startswith("["):
        # IPv6 in brackets: [addr]:port or [addr]:min-max or [addr]
        bracket_end = ip_port.index("]")
        ip_addr = ip_port[1:bracket_end]
        port_part = ip_port[bracket_end + 1:]
        if port_part.startswith(":"):
            port = int(port_part[1:].split("-")[0])
        else:
            port = 80
    elif ":" in ip_port:
        ip_addr, port_str = ip_port.split(":", 1)
        port = int(port_str.split("-")[0])
    else:
        ip_addr = ip_port
        port = 80

    is_v6 = ":" in ip_addr
    family = socket.AF_INET6 if is_v6 else socket.AF_INET
    test_sock = socket.socket(family, socket.SOCK_DGRAM)
    test_sock.connect((ip_addr, port))
    with test_sock:
        local_ip, _ = test_sock.getsockname()
    return local_ip
