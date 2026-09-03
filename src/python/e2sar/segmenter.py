# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)
"""Segmenter (PUSH socket analog) for e2sar."""

import e2sar_py

from .errors import E2SARError


class Segmenter:
    """PUSH-style socket that segments and sends events via EJFAT.

    Connects immediately upon construction. Use ``send(msg)`` to transmit
    messages. Each message is treated as one event.

    Args:
        uri: EJFAT URI with ``data=<ip>:<port>`` specifying the destination.
        data_id: ID of the originating segmentation point (RE header).
        event_src_id: ID of the sending host (Sync header).
        mtu: MTU for segmentation. Defaults to 1500.
        rate_gbps: Send rate cap in Gbps. Negative means unlimited.
        num_send_sockets: Number of source ports (helps LAG distribution).
        snd_socket_buf_size: SO_SNDBUF size in bytes.
        use_cp: Enable control plane sync messages.
        sync_period_ms: Period (ms) between sync messages.
        sync_periods: Number of sync periods for rate averaging.
        dpv6: Use IPv6 dataplane.
        connected_socket: Use connected UDP sockets.
        warm_up_ms: Sync-only warm-up period before data sends (ms).
        event_queue_size: Size of the internal send queue.
        smooth: Shape rate per sendmsg() call rather than per event.
        multi_port: Use numSendSockets consecutive destination ports.
        ticks_as_re_event_num: Override RE event number with LB event number.
        lb_hdr_version: LB header version to use (2 or 3).

    Raises:
        E2SARError: If the URI is invalid or sockets cannot be opened.
    """

    def __init__(
        self,
        uri: str,
        data_id: int = 0x0505,
        event_src_id: int = 0x11223344,
        *,
        mtu: int = 1500,
        rate_gbps: float = -1.0,
        num_send_sockets: int = 4,
        snd_socket_buf_size: int = 3145728,
        use_cp: bool = False,
        sync_period_ms: int = 1000,
        sync_periods: int = 2,
        dpv6: bool = False,
        connected_socket: bool = True,
        warm_up_ms: int = 1000,
        event_queue_size: int = 2047,
        smooth: bool = False,
        multi_port: bool = False,
        ticks_as_re_event_num: bool = False,
        lb_hdr_version: int = 2,
    ):
        try:
            self._uri = e2sar_py.EjfatURI(
                uri=uri, tt=e2sar_py.EjfatURI.TokenType.instance
            )
        except Exception as e:
            raise E2SARError(f"Invalid URI: {e}") from e

        sflags = e2sar_py.DataPlane.Segmenter.SegmenterFlags()
        sflags.useCP = use_cp
        sflags.mtu = mtu
        sflags.rateGbps = rate_gbps
        sflags.numSendSockets = num_send_sockets
        sflags.sndSocketBufSize = snd_socket_buf_size
        sflags.syncPeriodMs = sync_period_ms
        sflags.syncPeriods = sync_periods
        sflags.dpV6 = dpv6
        sflags.connectedSocket = connected_socket
        sflags.warmUpMs = warm_up_ms
        sflags.eventQueueSize = event_queue_size
        sflags.smooth = smooth
        sflags.multiPort = multi_port
        sflags.ticksAsREEventNum = ticks_as_re_event_num
        sflags.lbHdrVersion = lb_hdr_version

        try:
            self._seg = e2sar_py.DataPlane.Segmenter(
                self._uri, data_id, event_src_id, sflags
            )
        except Exception as e:
            raise E2SARError(f"Failed to create Segmenter: {e}") from e

        res = self._seg.OpenAndStart()
        if res.has_error():
            raise E2SARError(f"Failed to start Segmenter: {res.error().message}")

        self._closed = False

    def send(self, msg: bytes) -> None:
        """Send a message as a single event.

        Raises:
            E2SARError: If the send fails.
            RuntimeError: If the segmenter is closed.
        """
        if self._closed:
            raise RuntimeError("Segmenter is closed")
        res = self._seg.sendEvent(msg, len(msg))
        if res.has_error():
            raise E2SARError(f"Send failed: {res.error().message}")

    @property
    def send_count(self) -> int:
        """Number of data frames sent."""
        return self._seg.getSendStats().msgCnt

    @property
    def send_errors(self) -> int:
        """Number of send errors encountered."""
        return self._seg.getSendStats().errCnt

    def close(self) -> None:
        """Stop threads and release resources. Idempotent."""
        if not getattr(self, "_closed", True):
            self._seg.stopThreads()
            self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        self.close()
