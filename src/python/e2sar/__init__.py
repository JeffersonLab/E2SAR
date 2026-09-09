# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)
"""E2SAR — EJ-FAT sender/receiver with a pyzmq-like interface.

This package provides a high-level Python API on top of the e2sar_py
pybind11 C++ extension. For the raw binding see e2sar_py directly.

Example::

    import e2sar

    ctx = e2sar.Context()

    # Send side
    push = ctx.push("ejfat://...?data=127.0.0.1:10000", data_id=0x0505)
    push.send(b"hello")
    push.close()

    # Receive side
    pull = ctx.pull("ejfat://...", data_ip="127.0.0.1", data_port=10000)
    msg = pull.recv(timeout_ms=100)   # bytes or None
    pull.close()
"""

try:
    from importlib.metadata import version, PackageNotFoundError
    try:
        __version__ = version("e2sar")
    except PackageNotFoundError:
        __version__ = "unknown"
except ImportError:
    __version__ = "unknown"

from .errors import E2SARError
from .context import Context
from .segmenter import Segmenter
from .reassembler import Reassembler

__all__ = ["Context", "Segmenter", "Reassembler", "E2SARError", "__version__"]
