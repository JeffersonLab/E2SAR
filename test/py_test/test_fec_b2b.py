"""
FEC back-to-back tests with controlled packet loss injection.

Architecture:
  Segmenter --> [FecUdpProxy:proxy_port] --drop/forward--> [Reassembler:reas_port]

The proxy parses LB+EC headers to identify each packet's FEC block number and
frame number, then applies a LossSpec to drop or forward. This exercises the
RS(10,8) erasure correction path in the Reassembler's GC thread.

FEC topology (RS(10,8) over GF(16)):
  - 8 data columns, 4 segments per column = 32 data segments per FEC block
  - 2 parity columns × 4 segments = 8 parity segments per block
  - Column C occupies data frames {C*4, C*4+1, C*4+2, C*4+3}
  - Max correctable: up to 2 damaged columns (any 2 columns fully missing)

Run with: pytest -m fec-b2b -v --timeout 300
"""

import gc
import struct
import threading
import socket
import time
from dataclasses import dataclass, field

import numpy as np
import pytest

import e2sar_py

# --- Constants -----------------------------------------------------------

DP_IPV4_ADDR = "127.0.0.1"
DATA_ID = 0x0505
EVENTSRC_ID = 0x11223344
MTU = 1500

# getFecTotalHeaderLength(IPv4) = 20(IP) + 8(UDP) + 16(LB) + 16(EC) + 20(RE) = 80
# colHeight = MTU - 80; maxUserData = colHeight - sizeof(REHdr)
COL_HEIGHT = MTU - 80           # 1420 bytes per segment
MAX_USER_DATA = COL_HEIGHT - 20  # 1400 bytes of user payload per segment
SEGS_PER_BLOCK = 32
BYTES_PER_BLOCK = MAX_USER_DATA * SEGS_PER_BLOCK  # 44800 bytes fills one full block

# GC thread sleeps eventTimeout_ms between sweeps.
# Wait 3× the timeout to guarantee the GC has run at least twice after the
# last packet arrives (once to expire the block, once to attempt recovery).
EVENT_TIMEOUT_MS = 1000
RECV_WAIT_S = (EVENT_TIMEOUT_MS * 4) / 1000.0        # 4 seconds (most tests)
RECV_WAIT_S_MULTIBLOCK = (EVENT_TIMEOUT_MS * 8) / 1000.0  # 8 seconds (multi-block)

# UDP port pool - incremented to avoid TIME_WAIT conflicts between tests
_port_base = 11000
_port_lock = threading.Lock()


def _alloc_ports():
    """Allocate a fresh pair of (proxy_port, reas_port)."""
    global _port_base
    with _port_lock:
        p, r = _port_base, _port_base + 1
        _port_base += 2
    return p, r


# --- Packet wire-format offsets -------------------------------------------
#
# UDP payload layout (from Segmenter, with withLBHeader=True on receiver):
#   Bytes  0-15: LBHdrU  (LBHdrV2: 'LB' + version + nextProto + rsvd + entropy + eventNum)
#   Bytes 16-31: ECHdr   ('EC' + version + nextProto + dataId + pFrameNum + rPadFrames
#                          + ecSegmentSize + padBytes + fecBlockNum)
#
# ECHdr field offsets within the datagram:
#   byte  22:    pFrameNum  — bit7=parity, bits6:0=ecFrameNum
#   bytes 28-31: fecBlockNum (big-endian uint32)

_LB_MAGIC_OFF = 0
_EC_MAGIC_OFF = 16
_PFRAME_NUM_OFF = 22
_FEC_BLOCK_NUM_OFF = 28


# --- Loss specification ---------------------------------------------------

@dataclass
class LossSpec:
    """Defines which packets to drop for one FEC block."""
    drop_data_frames: set = field(default_factory=set)    # frame numbers 0-31
    drop_parity_frames: set = field(default_factory=set)  # parity frame numbers 0-7


def column_frames(col: int) -> set:
    """Return the 4 data frame numbers belonging to column col (0-7)."""
    return {col * 4 + b for b in range(4)}


def columns_frames(*cols: int) -> set:
    """Return all frame numbers for multiple columns."""
    result = set()
    for c in cols:
        result |= column_frames(c)
    return result


# --- UDP Proxy -----------------------------------------------------------

class FecUdpProxy:
    """UDP proxy that sits between Segmenter and Reassembler.

    Forwards all packets except those matching the configured loss pattern.
    Thread-safe: loss_pattern can be updated between sends.
    """

    def __init__(self, listen_port: int, forward_host: str, forward_port: int):
        self.listen_port = listen_port
        self.forward_host = forward_host
        self.forward_port = forward_port
        self._lock = threading.Lock()
        self._loss_pattern: dict = {}  # fecBlockNum -> LossSpec
        self._stop_event = threading.Event()
        self._thread = None
        self._listen_sock = None
        self._fwd_sock = None
        self.packets_received = 0
        self.packets_forwarded = 0
        self.packets_dropped = 0

    def set_loss_pattern(self, pattern: dict):
        with self._lock:
            self._loss_pattern = dict(pattern)

    def start(self):
        self._listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listen_sock.bind((DP_IPV4_ADDR, self.listen_port))
        self._listen_sock.settimeout(0.1)

        self._fwd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop_event.set()
        if self._thread:
            self._thread.join(timeout=3.0)
        if self._listen_sock:
            self._listen_sock.close()
        if self._fwd_sock:
            self._fwd_sock.close()

    def _should_forward(self, data: bytes) -> bool:
        if len(data) < _FEC_BLOCK_NUM_OFF + 4:
            return True
        if data[_LB_MAGIC_OFF:_LB_MAGIC_OFF + 2] != b'LB':
            return True
        if data[_EC_MAGIC_OFF:_EC_MAGIC_OFF + 2] != b'EC':
            return True

        p_frame_num = data[_PFRAME_NUM_OFF]
        is_parity = bool(p_frame_num & 0x80)
        ec_frame_num = p_frame_num & 0x7F
        fec_block_num = struct.unpack('>I', data[_FEC_BLOCK_NUM_OFF:_FEC_BLOCK_NUM_OFF + 4])[0]

        with self._lock:
            spec = self._loss_pattern.get(fec_block_num)
        if spec is None:
            return True

        if is_parity:
            return ec_frame_num not in spec.drop_parity_frames
        else:
            return ec_frame_num not in spec.drop_data_frames

    def _loop(self):
        while not self._stop_event.is_set():
            try:
                data, _ = self._listen_sock.recvfrom(65536)
            except socket.timeout:
                continue
            self.packets_received += 1
            if self._should_forward(data):
                self._fwd_sock.sendto(data, (self.forward_host, self.forward_port))
                self.packets_forwarded += 1
            else:
                self.packets_dropped += 1


# --- Test helpers --------------------------------------------------------

def generate_event_data(size: int, seed: int = 0) -> bytes:
    """Deterministic pattern: event[i] = (seed + i) & 0xFF."""
    arr = (np.arange(size, dtype=np.uint64) + seed) & 0xFF
    return arr.astype(np.uint8).tobytes()


def get_fec_segmenter(send_port: int):
    uri_str = (
        f"ejfat://useless@192.168.100.1:9875/lb/1"
        f"?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}:{send_port}"
    )
    uri = e2sar_py.EjfatURI(uri=uri_str, tt=e2sar_py.EjfatURI.TokenType.instance)
    sflags = e2sar_py.DataPlane.Segmenter.SegmenterFlags()
    sflags.useCP = False
    sflags.syncPeriodMs = 1000
    sflags.syncPeriods = 5
    sflags.mtu = MTU
    sflags.enableFec = True
    return e2sar_py.DataPlane.Segmenter(uri, DATA_ID, EVENTSRC_ID, sflags)


def get_fec_reassembler(listen_port: int):
    uri_str = (
        f"ejfat://useless@192.168.100.1:9875/lb/1"
        f"?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}"
    )
    uri = e2sar_py.EjfatURI(uri=uri_str, tt=e2sar_py.EjfatURI.TokenType.instance)
    rflags = e2sar_py.DataPlane.Reassembler.ReassemblerFlags()
    rflags.useCP = False
    rflags.withLBHeader = True
    rflags.enableFec = True
    rflags.eventTimeout_ms = EVENT_TIMEOUT_MS
    return e2sar_py.DataPlane.Reassembler(
        uri, e2sar_py.IPAddress.from_string(DP_IPV4_ADDR), listen_port, 1, rflags
    )


def _make_pipeline(num_send_sockets: int = 4):
    """Build and start a (seg, reas, proxy) triple; yield; then tear down."""
    proxy_port, reas_port = _alloc_ports()

    proxy = FecUdpProxy(proxy_port, DP_IPV4_ADDR, reas_port)
    reas = get_fec_reassembler(reas_port)

    uri_str = (
        f"ejfat://useless@192.168.100.1:9875/lb/1"
        f"?sync=192.168.0.1:12345&data={DP_IPV4_ADDR}:{proxy_port}"
    )
    uri = e2sar_py.EjfatURI(uri=uri_str, tt=e2sar_py.EjfatURI.TokenType.instance)
    sflags = e2sar_py.DataPlane.Segmenter.SegmenterFlags()
    sflags.useCP = False
    sflags.syncPeriodMs = 1000
    sflags.syncPeriods = 5
    sflags.mtu = MTU
    sflags.enableFec = True
    sflags.numSendSockets = num_send_sockets
    seg = e2sar_py.DataPlane.Segmenter(uri, DATA_ID, EVENTSRC_ID, sflags)

    proxy.start()
    res = reas.OpenAndStart()
    assert not res.has_error(), f"Reassembler start failed: {res.error().message}"
    res = seg.OpenAndStart()
    assert not res.has_error(), f"Segmenter start failed: {res.error().message}"

    yield seg, reas, proxy

    seg.stopThreads()
    time.sleep(0.5)
    reas.stopThreads()
    time.sleep(0.5)
    proxy.stop()
    # Force GC so C++ destructors run before the next fixture starts.
    # Without this, deferred Python GC can invoke Segmenter/Reassembler
    # destructors (which call stopThreads again) mid-way through the
    # next test's setup, causing spurious hangs.
    del seg, reas, proxy
    gc.collect()


@pytest.fixture
def fec_pipeline():
    """Fresh Segmenter (4 sockets), proxy, and Reassembler per test."""
    yield from _make_pipeline(num_send_sockets=4)


@pytest.fixture
def fec_pipeline_single_socket():
    """Same as fec_pipeline but numSendSockets=1.

    Use for multi-event sequential tests where fecBlockNums must be monotonic
    across consecutive sendEvent() calls so the proxy loss pattern (keyed by
    block number) applies to the right event.
    """
    yield from _make_pipeline(num_send_sockets=1)


def send_recv(seg, reas, proxy, event_data: bytes,
              loss_pattern: dict, seed: int = 0,
              wait_s: float = RECV_WAIT_S):
    """Send one event and poll for the result.

    Polls getEventBytes() every 0.1s until either an event arrives or
    wait_s seconds elapse. For GC-recovery paths, the GC thread must
    first time out the FEC block (after eventTimeout_ms) before recovery
    can run, so wait_s must be at least 2 * EVENT_TIMEOUT_MS / 1000.
    """
    proxy.set_loss_pattern(loss_pattern)

    res = seg.sendEvent(event_data, len(event_data))
    assert not res.has_error(), f"sendEvent failed: {res.error().message}"

    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline:
        recv_len, recv_bytes, _, recv_data_id = reas.getEventBytes()
        if recv_len >= 0:
            stats = reas.getStats()
            assert recv_data_id == DATA_ID
            return recv_bytes, stats
        time.sleep(0.1)

    stats = reas.getStats()
    return None, stats


# --- Tests: Category 1 — No Loss (baseline) --------------------------------

@pytest.mark.fec_b2b
def test_no_loss_single_block(fec_pipeline):
    """All 32 data + 8 parity arrive — event assembles in recv thread fast path."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=1)

    recv, stats = send_recv(seg, reas, proxy, event_data, {}, seed=1)

    assert recv == event_data, "Data mismatch on no-loss single block"
    assert stats.eventSuccess >= 1
    assert stats.fecRecoveries == 0
    assert stats.fecFailures == 0
    assert stats.reassemblyLoss == 0


@pytest.mark.fec_b2b
def test_no_loss_multi_block(fec_pipeline):
    """3-block event, no loss — all blocks complete in recv thread."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK * 3, seed=2)

    recv, stats = send_recv(seg, reas, proxy, event_data, {}, seed=2)

    assert recv == event_data, "Data mismatch on no-loss multi-block"
    assert stats.eventSuccess >= 1
    assert stats.fecRecoveries == 0
    assert stats.fecFailures == 0


# --- Tests: Category 2 — Recoverable single-column loss --------------------

@pytest.mark.fec_b2b
@pytest.mark.parametrize("lost_col", [0, 3, 7])
def test_one_column_loss_recoverable(fec_pipeline, lost_col):
    """Drop all 4 segments of one column — RS recovers with 1 parity column."""
    seg, reas, proxy = fec_pipeline
    seed = 10 + lost_col
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=seed)

    loss = {0: LossSpec(drop_data_frames=column_frames(lost_col))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=seed)

    assert recv == event_data, f"Data mismatch after column {lost_col} recovery"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0
    assert stats.reassemblyLoss == 0


# --- Tests: Category 3 — Recoverable two-column loss ----------------------

@pytest.mark.fec_b2b
@pytest.mark.parametrize("cols", [(0, 1), (0, 7), (2, 5), (3, 6)])
def test_two_column_loss_recoverable(fec_pipeline, cols):
    """Drop 2 full columns (8 frames) — maximum correctable for RS(10,8)."""
    seg, reas, proxy = fec_pipeline
    seed = 20 + cols[0] + cols[1]
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=seed)

    loss = {0: LossSpec(drop_data_frames=columns_frames(*cols))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=seed)

    assert recv == event_data, f"Data mismatch after columns {cols} recovery"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0
    assert stats.reassemblyLoss == 0


# --- Tests: Category 4 — Partial column loss ------------------------------

@pytest.mark.fec_b2b
def test_partial_column_loss_recoverable(fec_pipeline):
    """Drop 2 of 4 segments in column 1 — column is damaged, RS recovers it.

    The GC thread zeros the entire damaged column before calling rs_decode,
    then recovers all 4 segments from parity, including the 2 that arrived.
    """
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=30)

    # Frames 4 and 5 belong to column 1; frames 6 and 7 also belong to col 1
    loss = {0: LossSpec(drop_data_frames={4, 5})}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=30)

    assert recv == event_data, "Data mismatch after partial-column recovery"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0


# --- Tests: Category 5 — Unrecoverable: 3+ damaged columns ----------------

@pytest.mark.fec_b2b
def test_three_column_loss_unrecoverable(fec_pipeline):
    """Drop 3 columns — exceeds RS(10,8) capacity of 2 correctable columns."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=40)

    loss = {0: LossSpec(drop_data_frames=columns_frames(0, 3, 7))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=40)

    assert recv is None, "Event should not be received when 3 columns are lost"
    assert stats.fecFailures >= 1
    assert stats.eventSuccess == 0


@pytest.mark.fec_b2b
def test_four_column_loss_unrecoverable(fec_pipeline):
    """Drop 4 columns — well beyond RS recovery capacity."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=41)

    loss = {0: LossSpec(drop_data_frames=columns_frames(0, 1, 2, 3))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=41)

    assert recv is None, "Event should not be received when 4 columns are lost"
    assert stats.fecFailures >= 1
    assert stats.eventSuccess == 0


# --- Tests: Category 6 — Insufficient parity ------------------------------

@pytest.mark.fec_b2b
def test_two_col_loss_insufficient_parity(fec_pipeline):
    """2 damaged data columns but only 1 parity received — RS needs 2, has 1."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=50)

    # 2 damaged columns: need 2 parity symbols. Drop 7 of 8 parity packets.
    loss = {0: LossSpec(
        drop_data_frames=columns_frames(0, 7),
        drop_parity_frames={1, 2, 3, 4, 5, 6, 7},  # keep only parity 0
    )}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=50)

    assert recv is None, "Event should not be received with insufficient parity"
    assert stats.fecFailures >= 1
    assert stats.eventSuccess == 0


@pytest.mark.fec_b2b
def test_one_col_loss_no_parity(fec_pipeline):
    """1 damaged data column but all parity dropped — RS needs 1, has 0."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=51)

    loss = {0: LossSpec(
        drop_data_frames=column_frames(4),
        drop_parity_frames={0, 1, 2, 3, 4, 5, 6, 7},
    )}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=51)

    assert recv is None, "Event should not be received with no parity"
    assert stats.fecFailures >= 1
    assert stats.eventSuccess == 0


# --- Tests: Category 7 — Parity-only loss ----------------------------------

@pytest.mark.fec_b2b
def test_all_parity_dropped_data_intact(fec_pipeline):
    """Drop all 8 parity packets. All data arrives — fast path, no FEC recovery."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK // 2, seed=60)

    loss = {0: LossSpec(drop_parity_frames={0, 1, 2, 3, 4, 5, 6, 7})}
    # No data loss => fast path in recv thread; short wait is enough
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=60, wait_s=2.0)

    assert recv == event_data, "Data mismatch when only parity dropped"
    assert stats.eventSuccess >= 1
    assert stats.fecRecoveries == 0
    assert stats.fecFailures == 0


@pytest.mark.fec_b2b
def test_partial_parity_dropped_data_intact(fec_pipeline):
    """Drop half the parity, all data arrives — no impact on reassembly."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=61)

    loss = {0: LossSpec(drop_parity_frames={0, 3, 5, 7})}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=61, wait_s=2.0)

    assert recv == event_data, "Data mismatch when partial parity dropped"
    assert stats.eventSuccess >= 1
    assert stats.fecRecoveries == 0
    assert stats.fecFailures == 0


# --- Tests: Category 8 — Multi-block events with per-block loss ------------

@pytest.mark.fec_b2b
def test_multiblock_all_recoverable(fec_pipeline):
    """3-block event: block 0 clean, block 1 one-col loss, block 2 two-col loss.
    All blocks recover — event arrives with correct data."""
    seg, reas, proxy = fec_pipeline
    # 3 full blocks
    event_data = generate_event_data(BYTES_PER_BLOCK * 3, seed=70)

    loss = {
        # block 0: no loss
        1: LossSpec(drop_data_frames=column_frames(2)),           # 1-col loss
        2: LossSpec(drop_data_frames=columns_frames(1, 6)),       # 2-col loss
    }
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=70,
                            wait_s=RECV_WAIT_S_MULTIBLOCK)

    assert recv == event_data, "Data mismatch in recoverable multi-block event"
    assert stats.eventSuccess >= 1
    assert stats.fecRecoveries >= 2  # blocks 1 and 2 both needed recovery
    assert stats.fecFailures == 0


@pytest.mark.fec_b2b
def test_multiblock_one_unrecoverable(fec_pipeline):
    """3-block event: block 1 recoverable (1-col), block 2 unrecoverable (3-col).
    Event must be lost because block 2 cannot be recovered."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK * 3, seed=71)

    loss = {
        # block 0: no loss
        1: LossSpec(drop_data_frames=column_frames(5)),            # 1-col: recoverable
        2: LossSpec(drop_data_frames=columns_frames(0, 3, 7)),     # 3-col: unrecoverable
    }
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, seed=71,
                            wait_s=RECV_WAIT_S_MULTIBLOCK)

    assert recv is None, "Event should be lost when one block is unrecoverable"
    assert stats.fecFailures >= 1
    assert stats.eventSuccess == 0


# --- Tests: Category 9 — Padded/partial-block events -----------------------
#
# Events smaller than one full FEC block leave padding slots (padFrames > 0).
# The GC uses dataMask = (1 << dataSegsNeeded) - 1 to ignore pad frame slots.
# These tests exercise that masking and recovery on under-full blocks.

@pytest.mark.fec_b2b
def test_padded_block_no_loss(fec_pipeline):
    """Small event (8 data segs, padFrames=24) arrives intact — fast path."""
    seg, reas, proxy = fec_pipeline
    event_size = MAX_USER_DATA * 8      # 8 segments → padFrames = 24
    event_data = generate_event_data(event_size, seed=80)

    recv, stats = send_recv(seg, reas, proxy, event_data, {}, wait_s=2.0)

    assert recv == event_data, "Data mismatch on padded-block no-loss"
    assert stats.eventSuccess >= 1
    assert stats.fecRecoveries == 0
    assert stats.fecFailures == 0


@pytest.mark.fec_b2b
def test_padded_block_one_column_loss(fec_pipeline):
    """Small event (8 segs, padFrames=24): lose column 1 (frames 4-7 are live).
    Pad-aware dataMask means only frames 0-7 are considered; losing col 1
    damages 1 column — RS recovers."""
    seg, reas, proxy = fec_pipeline
    event_size = MAX_USER_DATA * 8      # 8 live segments in frames 0-7
    event_data = generate_event_data(event_size, seed=81)

    # column 1 = frames 4,5,6,7 — all are live data frames for this event
    loss = {0: LossSpec(drop_data_frames=column_frames(1))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv == event_data, "Data mismatch after padded-block column recovery"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0


@pytest.mark.fec_b2b
def test_padded_block_loss_in_pad_range_no_effect(fec_pipeline):
    """Drop frames in pad range (frames 8-31) — those segments are never sent,
    so the proxy drops nothing and the event should arrive intact via fast path."""
    seg, reas, proxy = fec_pipeline
    event_size = MAX_USER_DATA * 8      # frames 0-7 are live; 8-31 are padding
    event_data = generate_event_data(event_size, seed=82)

    # Dropping frames 8-31 has no effect: Segmenter never sends them
    loss = {0: LossSpec(drop_data_frames=set(range(8, 32)))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss, wait_s=2.0)

    assert recv == event_data, "Data mismatch — drop of pad-range frames should be a no-op"
    assert stats.eventSuccess >= 1
    assert stats.fecRecoveries == 0


@pytest.mark.fec_b2b
def test_minimum_event_one_segment_no_loss(fec_pipeline):
    """Smallest possible FEC event: 1 data segment (padFrames=31).
    All data + 8 parity arrive — fast path."""
    seg, reas, proxy = fec_pipeline
    event_size = 100    # well under MAX_USER_DATA → numSegs=1, padFrames=31
    event_data = generate_event_data(event_size, seed=83)

    recv, stats = send_recv(seg, reas, proxy, event_data, {}, wait_s=2.0)

    assert recv == event_data, "Data mismatch on single-segment event"
    assert stats.eventSuccess >= 1
    assert stats.fecRecoveries == 0
    assert stats.fecFailures == 0


@pytest.mark.fec_b2b
def test_minimum_event_one_segment_data_lost(fec_pipeline):
    """1-segment event: lose the single data frame (frame 0) but keep all parity.
    dataMask = 0x1, damagedCols = {0}, 1 parity needed — RS recovers."""
    seg, reas, proxy = fec_pipeline
    event_size = 100
    event_data = generate_event_data(event_size, seed=84)

    loss = {0: LossSpec(drop_data_frames={0})}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv == event_data, "Data mismatch after single-segment FEC recovery"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0


# --- Tests: Category 10 — Sequential events, independent accounting ---------
#
# Multiple events through the same pipeline with different loss patterns.
# Validates that fecBlockNum uniqueness (keyed by eventNum) isolates accounting.

@pytest.mark.fec_b2b
def test_sequential_events_alternating_loss(fec_pipeline_single_socket):
    """Send 3 events: event 1 recoverable, event 2 unrecoverable, event 3 clean.
    Confirms independent per-event accounting and that a lost event doesn't
    contaminate the pipeline for subsequent events.

    Loss patterns for all events are set before any send, so the proxy
    thread sees the correct spec regardless of packet delivery timing.
    With numSendSockets=1, events use consecutive block numbers: 0, 1, 2.
    """
    seg, reas, proxy = fec_pipeline_single_socket

    size = BYTES_PER_BLOCK
    data_a = generate_event_data(size, seed=90)
    data_b = generate_event_data(size, seed=91)
    data_c = generate_event_data(size, seed=92)

    # Pre-configure loss for all three blocks before any send.
    # Event A (block 0): 1-col loss — recoverable
    # Event B (block 1): 3-col loss — unrecoverable
    # Event C (block 2): no entry — no loss
    proxy.set_loss_pattern({
        0: LossSpec(drop_data_frames=column_frames(3)),
        1: LossSpec(drop_data_frames=columns_frames(0, 3, 7)),
    })

    res = seg.sendEvent(data_a, len(data_a))
    assert not res.has_error()
    res = seg.sendEvent(data_b, len(data_b))
    assert not res.has_error()
    res = seg.sendEvent(data_c, len(data_c))
    assert not res.has_error()

    time.sleep(RECV_WAIT_S)

    # Collect all received events (up to 3 attempts)
    received = []
    for _ in range(3):
        recv_len, recv_bytes, _, _ = reas.getEventBytes()
        if recv_len >= 0:
            received.append(bytes(recv_bytes))

    stats = reas.getStats()

    # Events A and C should arrive; B should be lost
    assert stats.eventSuccess == 2, f"Expected 2 successes, got {stats.eventSuccess}"
    assert stats.fecFailures >= 1,  f"Expected at least 1 FEC failure for event B"
    assert stats.fecRecoveries >= 1, f"Expected at least 1 FEC recovery for event A"
    assert stats.reassemblyLoss >= 1, f"Expected event B in reassemblyLoss"

    assert data_a in received, "Event A (recoverable) not found in received set"
    assert data_c in received, "Event C (clean) not found in received set"
    assert data_b not in received, "Event B (unrecoverable) should not be in received set"


@pytest.mark.fec_b2b
def test_sequential_events_all_clean(fec_pipeline):
    """Send 4 events with no loss — verify all 4 arrive and stats accumulate."""
    seg, reas, proxy = fec_pipeline

    events = [generate_event_data(MAX_USER_DATA * 5, seed=100 + i) for i in range(4)]
    proxy.set_loss_pattern({})   # no drops for any block number

    for e in events:
        res = seg.sendEvent(e, len(e))
        assert not res.has_error()


    time.sleep(RECV_WAIT_S)

    received = []
    for _ in range(4):
        recv_len, recv_bytes, _, _ = reas.getEventBytes()
        if recv_len >= 0:
            received.append(bytes(recv_bytes))

    stats = reas.getStats()
    assert stats.eventSuccess == 4
    assert stats.fecFailures == 0
    assert stats.fecRecoveries == 0
    for e in events:
        assert e in received, "An event was not received in the clean 4-event sequence"


# --- Tests: Category 11 — Minimum parity exactly sufficient -----------------
#
# Each parity SYMBOL (4 bits in GF(16)) is distributed across 4 parity SEGMENTS:
#   Parity symbol 0: segments {0, 1, 2, 3}  (bit 3, bit 2, bit 1, bit 0)
#   Parity symbol 1: segments {4, 5, 6, 7}
#
# "Minimum sufficient" means keeping exactly one complete parity symbol's
# worth of segments (4 segments) while dropping the other 4.
# The GC condition `popcount(parityReceived) >= damagedCols` counts segment
# count — one complete symbol = 4 segments, which satisfies ">= 1".

@pytest.mark.fec_b2b
def test_one_col_one_parity_symbol(fec_pipeline):
    """1 damaged column, keep only parity symbol 0 (segments 0-3), drop 4-7.
    RS needs 1 parity symbol; exactly 1 complete symbol is available."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=110)

    loss = {0: LossSpec(
        drop_data_frames=column_frames(5),
        drop_parity_frames={4, 5, 6, 7},    # keep parity symbol 0 (segs 0-3)
    )}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv == event_data, "Data mismatch with exactly 1 parity symbol for 1-col loss"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0


@pytest.mark.fec_b2b
def test_two_col_two_parity_symbols(fec_pipeline):
    """2 damaged columns, keep only parity symbols 0 and 1 (all 8 segments needed
    for 2 complete symbols), effectively keeping all parity — tests the 2-symbol
    minimum boundary from the symbol-level perspective."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=111)

    # 2 damaged columns need 2 complete parity symbols = all 8 parity segments.
    # Test: drop columns 2 and 6, keep all parity. This is the same as the
    # 2-column recoverable test but uses different columns to cover distinct
    # generator matrix entries.
    loss = {0: LossSpec(drop_data_frames=columns_frames(2, 6))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv == event_data, "Data mismatch with 2 parity symbols for 2-col loss"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0


@pytest.mark.fec_b2b
def test_two_col_loss_one_parity_symbol_each(fec_pipeline):
    """2 damaged columns (cols 3 and 6), with all 8 parity segments.
    Uses a different column pair than the main 2-col tests to cover
    additional generator matrix entries for the GF(16) RS decoder."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=112)

    loss = {0: LossSpec(drop_data_frames=columns_frames(3, 6))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv == event_data, "Data mismatch after cols (3,6) recovery"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0


# --- Tests: Category 12 — Boundary: dataMissing > 8 (all data lost) --------

@pytest.mark.fec_b2b
def test_all_data_lost_some_parity(fec_pipeline):
    """Drop all 32 data frames but keep all parity.
    dataMissing = 32 > 8 → condition dataMissing <= 8 fails → unrecoverable."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=120)

    loss = {0: LossSpec(drop_data_frames=set(range(32)))}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv is None, "Event should not be received when all data is lost"
    assert stats.fecFailures >= 1
    assert stats.eventSuccess == 0


@pytest.mark.fec_b2b
def test_nine_segments_lost_three_cols_unrecoverable(fec_pipeline):
    """Lose 9 data segments (3 columns × 3 segs each): dataMissing=9 > 8 AND
    damagedCols=3 > 2 — doubly unrecoverable, hits both guards."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=121)

    # Drop 3 segs from each of columns 0, 4, 7 (9 segments total, 3 columns)
    loss = {0: LossSpec(drop_data_frames={c * 4 + b for c in (0, 4, 7) for b in range(3)})}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv is None
    assert stats.fecFailures >= 1
    assert stats.eventSuccess == 0


# --- Tests: Category 13 — Non-contiguous single-frame loss per column ------

@pytest.mark.fec_b2b
def test_one_frame_per_col_two_cols_recoverable(fec_pipeline):
    """Lose exactly 1 frame from each of 2 distinct columns (frames 0 and 7).
    damagedCols = {0, 1} still → 2 damaged columns → recoverable (same as full column drop)."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=130)

    # frame 0 → col 0, frame 7 → col 1 (7//4 = 1)
    loss = {0: LossSpec(drop_data_frames={0, 7})}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv == event_data, "Data mismatch: 2-column damage (1 frame each) should recover"
    assert stats.fecRecoveries >= 1
    assert stats.fecFailures == 0


@pytest.mark.fec_b2b
def test_one_frame_per_col_three_cols_unrecoverable(fec_pipeline):
    """Lose 1 frame from each of 3 columns (frames 0, 4, 8).
    damagedCols = {0, 1, 2} → 3 columns → exceeds RS capacity."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=131)

    # frame 0 → col 0, frame 4 → col 1, frame 8 → col 2
    loss = {0: LossSpec(drop_data_frames={0, 4, 8})}
    recv, stats = send_recv(seg, reas, proxy, event_data, loss)

    assert recv is None, "3-column damage should be unrecoverable even with 1 frame each"
    assert stats.fecFailures >= 1
    assert stats.eventSuccess == 0


# --- Tests: Category 14 — Proxy counter integrity --------------------------

@pytest.mark.fec_b2b
def test_proxy_counters_no_loss(fec_pipeline):
    """Verify the proxy forwards exactly the expected packet count with no loss.
    A full-block event produces 32 data + 8 parity = 40 UDP packets."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=140)

    proxy.set_loss_pattern({})
    # Reset counters by re-reading initial state (they start at 0)
    before_rx = proxy.packets_received
    before_fwd = proxy.packets_forwarded
    before_drop = proxy.packets_dropped

    res = seg.sendEvent(event_data, len(event_data))
    assert not res.has_error()
    time.sleep(2.0)

    packets_rx = proxy.packets_received - before_rx
    packets_fwd = proxy.packets_forwarded - before_fwd
    packets_drop = proxy.packets_dropped - before_drop

    # Full FEC block: 32 data + 8 parity = 40 packets total
    assert packets_rx == 40, f"Expected 40 packets from Segmenter, proxy saw {packets_rx}"
    assert packets_fwd == 40, f"Expected all 40 forwarded with no loss, got {packets_fwd}"
    assert packets_drop == 0, f"Expected 0 dropped, got {packets_drop}"


@pytest.mark.fec_b2b
def test_proxy_counters_with_column_loss(fec_pipeline):
    """Verify proxy drops exactly the expected count for a 1-column loss."""
    seg, reas, proxy = fec_pipeline
    event_data = generate_event_data(BYTES_PER_BLOCK, seed=141)

    before_rx = proxy.packets_received
    before_fwd = proxy.packets_forwarded
    before_drop = proxy.packets_dropped

    loss = {0: LossSpec(drop_data_frames=column_frames(3))}    # 4 frames dropped
    proxy.set_loss_pattern(loss)
    res = seg.sendEvent(event_data, len(event_data))
    assert not res.has_error()
    time.sleep(RECV_WAIT_S)

    packets_rx = proxy.packets_received - before_rx
    packets_fwd = proxy.packets_forwarded - before_fwd
    packets_drop = proxy.packets_dropped - before_drop

    assert packets_rx == 40,   f"Expected 40 packets total, proxy saw {packets_rx}"
    assert packets_drop == 4,  f"Expected 4 dropped (col 3), got {packets_drop}"
    assert packets_fwd == 36,  f"Expected 36 forwarded, got {packets_fwd}"

    # And the event should still be recovered
    recv_len, recv_bytes, _, _ = reas.getEventBytes()
    assert recv_len >= 0 and bytes(recv_bytes) == event_data, "Event should recover despite column loss"
