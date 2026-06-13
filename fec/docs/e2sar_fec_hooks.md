# E2SAR FEC Integration Hooks

Two hook points: one in the Segmenter send path, one in the Reassembler GC thread.

---

## Send Hook (`src/e2sarDPSegmenter.cpp`)

**Entry point:** `Segmenter::sendEvent()` at line 1089

```
if (enableFec)
    return sendThreadState._sendWithFec(event, bytes, eventNum, dataId, ...);
else
    return sendThreadState._send(event, bytes, ...);
```

### Data structure passed: `FecSendBuffers` (pre-allocated per send socket)

```cpp
struct FecSendBuffers {
    std::vector<uint8_t> segmentBuf;   // 32 × col_height  — packed segments
    std::vector<uint8_t> codewordBuf;  // col_height × 40  — interleaved codewords
    std::vector<uint8_t> parityBuf;    // 8 × col_height   — extracted parity rows
};
```

`col_height = MTU − FEC header overhead` (set at `SendThreadState` construction).

The `FecBlockParams` descriptor (passed by value to every FEC call) holds only one field set at
call time:

```cpp
fec::FecBlockParams fecParams;
fecParams.col_height = static_cast<uint32_t>(colHeight);  // all else is derived
```

### How FEC is disabled

`SegmenterFlags::enableFec` (default `false`). Set it in code or via INI key
`data-plane.enableFec`. When false, `sendEvent` calls the non-FEC `_send` path;
`fecBufs` is never allocated.

### FEC call sequence and returns

Per 32-segment block:

| Step | Call | Returns |
|---|---|---|
| Interleave segments → codewords | `interleave_neon_tiled(segmentBuf, codewordBuf, params)` or `interleave(...)` | void |
| RS(10,8) encode | `rs_encode_neon(codewordBuf, params)` or `rs_encode(...)` | void |
| Extract parity rows | `deinterleave_parity_neon(codewordBuf, parityBuf, params)` or `deinterleave_parity(...)` | void |

Output: 32 data packets with `LBECHdr` + one `col_height`-byte segment each, then 8 parity
packets with `LBECREHdr` + one `col_height`-byte parity row each, all sent via `sendmsg`.

---

## Receive Hook (`src/e2sarDPReassembler.cpp`)

**Entry point:** `Reassembler::GCThreadState::_threadBody()` at line 264 — runs every
`eventTimeout_ms`, sweeps `RecvThreadState::fecBlocksInProgress`.

### Data structure passed: `FecBlockState` (tracked per `(eventNum, dataId, fecBlockNum)`)

```cpp
struct FecBlockState {
    std::vector<uint8_t> segmentBuf;  // 32 × col_height — received data segments
    std::vector<uint8_t> parityBuf;   // 8  × col_height — received parity rows
    uint32_t dataReceived;            // bitmask of arrived data segments
    uint32_t parityReceived;          // bitmask of arrived parity rows
    uint8_t  padFrames;               // number of padding (unused) segment slots
    uint32_t colHeight;               // col_height for this block
    ...
};
```

`RsDecodeContext` (one instance per GC thread, initialized once at thread start) holds the
pre-computed RS decode table.

### How FEC is disabled

`Reassembler::enableFec` (default `false`). When false the GC sweep skips the entire
`fecBlocksInProgress` loop. The receive thread still parses `LBECHdr`/`LBECREHdr` packet types
but does not populate `fecBlocksInProgress` either, so no FEC state is created.

### FEC call sequence and returns

Triggered when a block times out with 1–8 missing data segments and ≥1 parity row received:

| Step | Call | Returns |
|---|---|---|
| Interleave surviving data | `interleave_neon_tiled(segmentBuf, cw, params)` or `interleave(...)` | void |
| Merge parity into codeword buf | `interleave_parity(parityBuf, cw, params)` | void |
| RS decode (erasure correction) | `rs_decode(cw, params, erasedCols, fecDecodeCtx)` | `int` — 0 on success, non-zero on failure |
| Recover segments from codewords | `deinterleave_neon(cw, segmentBuf, params)` or `deinterleave(...)` | void |

On `rs_decode` return 0: segments are copied from `segmentBuf` into the event buffer,
`recvStats.fecRecoveries` is incremented, and the event is enqueued if all FEC blocks are done.
On non-zero return or too many erasures: the event is discarded and `recvStats.fecFailures` is
incremented.
