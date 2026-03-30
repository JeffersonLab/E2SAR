# E2SAR Event Buffer Layout vs. Interleaver Segment Layout

## Assumption: Each Packet = One Segment

With 32 packets per FEC block, each packet payload becomes one of the 32 segments.

---

## E2SAR Reassembler: How Packets Map Into the Event Buffer

The reassembler allocates a single contiguous `uint8_t[bufferLength]` buffer for each event,
where `bufferLength` = total event size = `32 × maxPldLen` (all 32 fragment payloads).

Each received packet is placed at its `bufferOffset` via:
```cpp
memcpy(event + rehdr->get_bufferOffset(), payload_ptr, payload_len);
```

The segmenter sets `bufferOffset = packet_index × maxPldLen`, so the reassembled buffer is:

```
event[0 * maxPldLen .. 1*maxPldLen - 1]    = packet 0 payload  (= segment 0)
event[1 * maxPldLen .. 2*maxPldLen - 1]    = packet 1 payload  (= segment 1)
...
event[31 * maxPldLen .. 32*maxPldLen - 1]  = packet 31 payload (= segment 31)
```

A flat, contiguous array of 32 equally-sized byte blocks.

---

## Interleaver: Expected Segment Layout

```
segments[seg * col_height + byte_idx]
```

Also a flat, contiguous array of 32 equally-sized byte blocks, each `col_height` bytes.

---

## The Layouts Match Structurally

If `col_height == maxPldLen`, the two layouts are **identical in memory**:

| E2SAR buffer offset       | Interleaver buffer offset     | Content              |
|---------------------------|-------------------------------|----------------------|
| `0 * maxPldLen + b`       | `0 * col_height + b`          | byte `b` of segment 0 |
| `1 * maxPldLen + b`       | `1 * col_height + b`          | byte `b` of segment 1 |
| ...                       | ...                           | ...                  |
| `31 * maxPldLen + b`      | `31 * col_height + b`         | byte `b` of segment 31 |

The reassembled event buffer can be handed directly to the interleaver (or deinterleaver) as a
`std::span<const uint8_t>` of size `32 × col_height` — **no rearrangement needed**.

### The "bit-planar" interpretation is internal to the algorithm

The interleaver's description of each segment as a "bit-plane of a GF(16) symbol column" is
how the algorithm interprets the bits — not a constraint on the input format. The segments can
contain arbitrary raw bytes. The bit-transpose is mechanical and the round-trip
(interleave → RS encode → deinterleave) recovers the original bytes identically.

---

## Gaps That Need Bridging

### 1. Last fragment may be short

E2SAR's last fragment in an event can be smaller than `maxPldLen`:
```cpp
curLen = eventEnd - curOffset;   // may be < maxPldLen
```

The interleaver requires all 32 segments to be exactly `col_height` bytes.
`new uint8_t[bufferLength]` in the reassembler is **not zero-initialized**, so uninitialized
tail bytes would corrupt FEC.

**Fix**: Constrain event size to exactly `32 × maxPldLen`, or zero-initialize the event buffer
on allocation.

### 2. Events must have exactly 32 fragments

E2SAR uses `numBuffers = ceil(event_bytes / maxPldLen)` fragments per event. The interleaver
requires exactly `NUM_SEGMENTS = 32` segments per block.

**Fix**: Either constrain event size to exactly `32 × maxPldLen`, or add a chunking layer that
breaks each event into 32-packet FEC blocks below the event level.

### 3. Codeword-major output isn't packet-major

After interleave + RS encode, the output is **codeword-major** (5-byte records, word by word):
```
codewords[word * 5 + 0..4]
```

This cannot be directly split into 32+2 packet payloads by simple stride. To transmit
the RS-encoded data as packets, a deinterleave pass is required first. The natural
sender pipeline is:

```
32 raw segments → interleave → RS encode → deinterleave → 40 segment-sized outputs
```

The 40 output segments (32 data + 8 additional parity segments worth of data) can then each
be sent as a separate packet.

### 4. Parity granularity is nibble-level, not packet-level

The RS(10,8)/GF(16) code operates on **4-bit symbols**. Each codeword has 8 data nibbles and
2 parity nibbles. Losing one whole packet (segment) means losing one bit-plane across two
columns — 2 of 10 symbols damaged per codeword.

This is **within-block bit/nibble error correction**, not classical packet-level erasure FEC
("lose 1 of N packets, recover from the other N-1"). True packet-level erasure FEC would
require RS over GF(2⁸) or GF(2¹⁶) where each symbol = one packet payload.

---

## Summary

| Aspect | E2SAR | Interleaver | Compatible? |
|--------|-------|-------------|-------------|
| Memory layout (32 blocks, contiguous) | `event[seg * maxPldLen + b]` | `segments[seg * col_height + b]` | Yes, if `col_height == maxPldLen` |
| Segment count | Arbitrary (`ceil` division) | Fixed at 32 | Needs constraint or chunking |
| Last segment size | May be short | Must be `col_height` | Needs padding / zero-init |
| Input data format | Raw bytes | Raw bytes (bit-planar is internal) | Yes |
| Output format | Byte stream to application | Codeword-major (5-byte records) | Needs deinterleave pass before packetizing |
| FEC protection unit | N/A | 4-bit nibble across 8 columns | Not packet-level recovery |

### Key header sizes (for computing `maxPldLen`)

```
REHdr:    20 bytes   (preamble 2 + dataId 2 + bufferOffset 4 + bufferLength 4 + eventNum 8)
LBHdr:    16 bytes   (same size for v2 and v3)
LBREHdr:  36 bytes   (LBHdr + REHdr, prepended by segmenter)

maxPldLen (IPv4, MTU 1500) = 1500 - 20(IP) - 8(UDP) - 36(LBREHdr) = 1436 bytes
maxPldLen (IPv4, MTU 9000) = 9000 - 20(IP) - 8(UDP) - 36(LBREHdr) = 8936 bytes
```

The LB strips the LBHdr before forwarding to workers, so the reassembler normally only sees the
REHdr (20 bytes) prepended to the payload. The `withLBHeader` flag is used in testing when the
LB is bypassed.
