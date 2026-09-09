# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)
"""Context factory for e2sar sockets."""

from .segmenter import Segmenter
from .reassembler import Reassembler


class Context:
    """Factory for creating Segmenter (PUSH) and Reassembler (PULL) sockets.

    Analogous to a ZMQ Context. Lightweight — holds no state itself.

    Example::

        ctx = e2sar.Context()
        push = ctx.push(uri, data_id=0x0505)
        pull = ctx.pull(uri, data_ip="127.0.0.1", data_port=10000)
    """

    def push(self, uri: str, **kwargs) -> Segmenter:
        """Create a Segmenter (PUSH socket). All kwargs forwarded to Segmenter."""
        return Segmenter(uri, **kwargs)

    def pull(self, uri: str, **kwargs) -> Reassembler:
        """Create a Reassembler (PULL socket). All kwargs forwarded to Reassembler."""
        return Reassembler(uri, **kwargs)
