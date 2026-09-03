# Adapted from frobnitzem/pye2sar (https://github.com/frobnitzem/pye2sar)
"""Reassembler (PULL socket analog) for e2sar."""

from typing import Optional

import e2sar_py

from .get_ip import get_local_addr
from .errors import E2SARError


class Reassembler:
    """PULL-style socket that reassembles events received via EJFAT.

    Connects immediately upon construction. Use ``recv()`` to receive
    complete reassembled messages.

    Args:
        uri: EJFAT URI.
        data_ip: IP address to listen on. Auto-determined from uri if empty.
        data_port: Starting UDP port to listen on.
        num_recv_threads: Number of receive threads.
        use_cp: Enable control plane (register worker, send state).
        event_timeout_ms: Max ms to wait for all fragments of an event.
        rcv_socket_buf_size: SO_RCVBUF size in bytes.
        port_range: Override port range (2^portRange ports). -1 = auto.
        validate_cert: Validate TLS certificate on control plane.
        period_ms: SendState gRPC period in ms (only with CP).
        weight: Processing power weight for LB slot allocation.
        min_factor: Min slots multiplier.
        max_factor: Max slots multiplier.
        epoch_ms: Epoch length in ms for PID controller.
        Ki: PID integral gain.
        Kp: PID proportional gain.
        Kd: PID derivative gain.
        set_point: PID set point (queue occupancy fraction).
        report_stats: Report worker stats to control plane.
        use_host_address: Use raw IP address for gRPC (disables cert validation).

    Raises:
        E2SARError: If the URI is invalid or sockets cannot be opened.
    """

    def __init__(
        self,
        uri: str,
        data_ip: str = "",
        data_port: int = 10000,
        num_recv_threads: int = 1,
        *,
        use_cp: bool = False,
        event_timeout_ms: int = 500,
        rcv_socket_buf_size: int = 3145728,
        port_range: int = -1,
        validate_cert: bool = True,
        period_ms: int = 100,
        weight: float = 1.0,
        min_factor: float = 0.5,
        max_factor: float = 2.0,
        epoch_ms: int = 1000,
        Ki: float = 0.0,
        Kp: float = 0.0,
        Kd: float = 0.0,
        set_point: float = 0.0,
        report_stats: bool = True,
        use_host_address: bool = False,
    ):
        try:
            self._uri = e2sar_py.EjfatURI(
                uri=uri, tt=e2sar_py.EjfatURI.TokenType.instance
            )
        except Exception as e:
            raise E2SARError(f"Invalid URI: {e}") from e

        if not data_ip:
            data_ip = get_local_addr(uri)

        rflags = e2sar_py.DataPlane.Reassembler.ReassemblerFlags()
        rflags.useCP = use_cp
        rflags.eventTimeout_ms = event_timeout_ms
        rflags.rcvSocketBufSize = rcv_socket_buf_size
        rflags.portRange = port_range
        rflags.validateCert = validate_cert
        rflags.period_ms = period_ms
        rflags.weight = weight
        rflags.min_factor = min_factor
        rflags.max_factor = max_factor
        rflags.epoch_ms = epoch_ms
        rflags.Ki = Ki
        rflags.Kp = Kp
        rflags.Kd = Kd
        rflags.setPoint = set_point
        rflags.reportStats = report_stats
        rflags.useHostAddress = use_host_address

        try:
            ip_addr = e2sar_py.IPAddress.from_string(data_ip)
            self._reas = e2sar_py.DataPlane.Reassembler(
                self._uri, ip_addr, data_port, num_recv_threads, rflags
            )
        except Exception as e:
            raise E2SARError(f"Failed to create Reassembler: {e}") from e

        res = self._reas.OpenAndStart()
        if res.has_error():
            raise E2SARError(f"Failed to start Reassembler: {res.error().message}")

        self._closed = False

    def recv(self, timeout_ms: int = 100) -> Optional[bytes]:
        """Receive a reassembled event message.

        Returns None if no message was available within the timeout.

        Raises:
            E2SARError: If a receive error occurs.
            RuntimeError: If the reassembler is closed.
        """
        if self._closed:
            raise RuntimeError("Reassembler is closed")

        recv_len, recv_bytes, recv_event_num, recv_data_id = \
            self._reas.recvEventBytes(timeout_ms)

        if recv_len == -1:
            return None
        if recv_len == -2:
            raise E2SARError("Receive error (recvEventBytes returned -2)")
        return recv_bytes

    @property
    def recv_count(self) -> int:
        """Number of events successfully reassembled."""
        return self._reas.getStats().eventSuccess

    @property
    def recv_errors(self) -> int:
        """Number of data errors encountered."""
        return self._reas.getStats().dataErrCnt

    @property
    def enqueue_loss(self) -> int:
        """Number of events lost due to full queue."""
        return self._reas.getStats().enqueueLoss

    @property
    def reassembly_loss(self) -> int:
        """Number of events lost due to missing fragments."""
        return self._reas.getStats().reassemblyLoss

    def close(self) -> None:
        """Stop threads and release resources. Idempotent."""
        if not getattr(self, "_closed", True):
            self._reas.stopThreads()
            self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        self.close()
