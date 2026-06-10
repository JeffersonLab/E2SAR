#!/usr/bin/env python3
"""
Generate cross-validation test vectors for the FEC pipeline.

Implements the complete GF(16)/RS(10,8) pipeline as a Python reference using
the galois library, then produces:
  - test_vectors.h  : C++ static arrays for fec_test
  - rs_cw_vectors.json, interleave_vectors.json, pipeline_vectors.json : human-readable

GF(16) field: irreducible polynomial x^4 + x + 1 = [1,0,0,1,1].
RS(10,8): 8 data symbols + 2 parity symbols per codeword.

Codeword byte layout (5 bytes, same as C++ FecBlockParams):
  byte 0: sym0 (hi nibble), sym1 (lo nibble)
  byte 1: sym2 (hi nibble), sym3 (lo nibble)
  byte 2: sym4 (hi nibble), sym5 (lo nibble)
  byte 3: sym6 (hi nibble), sym7 (lo nibble)
  byte 4: par0 (hi nibble), par1 (lo nibble)   <- written by RS encode

Interleave mapping (matches interleaver.cpp lines 18-48):
  For word W (0-indexed, MSB-first: word 0 = bit 7 of each segment byte):
    byte_idx = W // 8
    bit_shift = 7 - (W % 8)
  Symbol s (0-7) for word W:
    nibble_bit3 = (segments[(s*4+0) * col_height + byte_idx] >> bit_shift) & 1
    nibble_bit2 = (segments[(s*4+1) * col_height + byte_idx] >> bit_shift) & 1
    nibble_bit1 = (segments[(s*4+2) * col_height + byte_idx] >> bit_shift) & 1
    nibble_bit0 = (segments[(s*4+3) * col_height + byte_idx] >> bit_shift) & 1
  Packed into codeword: even sym → high nibble, odd sym → low nibble.

P matrix (element space, from rs_encode.cpp and rs_model.h):
  P col 0 = {14, 6, 14, 9, 7, 1, 15, 6}
  P col 1 = { 5, 9,  4,13, 8, 1,  5, 8}
"""

import galois
import json
import struct
import random
import os
import sys

# ---------------------------------------------------------------------------
# GF(16) setup — must match rs_encode.cpp
# ---------------------------------------------------------------------------
GF = galois.GF(16, irreducible_poly=[1, 0, 0, 1, 1])

# Primitive element alpha; verify log/exp tables match C++ gf_log / gf_exp.
alpha = GF.primitive_element

# Reproduce gf_log (index→element, i.e. alpha^i for i in 0..14, then 0)
gf_log = [int(alpha**i) for i in range(15)] + [0]
# gf_exp: element→exponent; gf_exp[0] = 15 (sentinel, same as C++)
gf_exp = [0] * 16
for i, v in enumerate(gf_log[:15]):
    gf_exp[v] = i
gf_exp[0] = 15  # sentinel

# Verify tables match C++ constants exactly.
_expected_gf_log = [1,2,4,8,3,6,12,11,5,10,7,14,15,13,9,0]
_expected_gf_exp = [15,0,1,4,2,8,5,10,3,14,9,7,6,13,11,12]
assert gf_log == _expected_gf_log, f"gf_log mismatch:\n  got {gf_log}\n  exp {_expected_gf_log}"
assert gf_exp == _expected_gf_exp, f"gf_exp mismatch:\n  got {gf_exp}\n  exp {_expected_gf_exp}"

# ---------------------------------------------------------------------------
# P matrix (element space) — matches rs_encode.cpp columns coeff0/coeff1
# ---------------------------------------------------------------------------
P_COL0 = [14, 6, 14, 9, 7, 1, 15, 6]  # parity symbol 0 coefficients
P_COL1 = [5, 9, 4, 13, 8, 1, 5, 8]   # parity symbol 1 coefficients

# Pre-convert to exponent space (same as coeff0_exp / coeff1_exp in C++)
coeff0_exp = [_expected_gf_exp[c] for c in P_COL0]
coeff1_exp = [_expected_gf_exp[c] for c in P_COL1]
_expected_c0_exp = [11, 5, 11, 14, 10, 0, 12, 5]
_expected_c1_exp = [8, 14, 2, 13, 3, 0, 8, 3]
assert coeff0_exp == _expected_c0_exp, f"coeff0_exp mismatch: {coeff0_exp}"
assert coeff1_exp == _expected_c1_exp, f"coeff1_exp mismatch: {coeff1_exp}"

# ---------------------------------------------------------------------------
# GF(16) arithmetic
# ---------------------------------------------------------------------------
def gf_mul(a: int, b: int) -> int:
    if a == 0 or b == 0:
        return 0
    return gf_log[(gf_exp[a] + gf_exp[b]) % 15]


def gf_add(a: int, b: int) -> int:
    return a ^ b


# ---------------------------------------------------------------------------
# Generator polynomial for RS(10,8), t=1 → g(x) = (x-alpha)(x-alpha^2)
# Coefficients: [1, alpha + alpha^2, alpha^3] = [1, 6, 8] in element space.
# Matches rs_gen_poly(t=1) from genMatrix_simplified.py.
# ---------------------------------------------------------------------------
def _make_gen_poly():
    r1 = gf_log[1]  # alpha^1 = 2
    r2 = gf_log[2]  # alpha^2 = 4
    # g(x) = (x + r1)(x + r2) = x^2 + (r1^r2)*x + gf_mul(r1,r2)
    coeff1 = gf_add(r1, r2)
    coeff0 = gf_mul(r1, r2)
    return [1, coeff1, coeff0]   # highest degree first

GEN_POLY = _make_gen_poly()  # [1, 6, 8]


def poly_div_mod(dividend: list, divisor: list):
    """Polynomial division over GF(16); returns (quotient, remainder)."""
    rem = list(dividend)
    while len(rem) >= len(divisor):
        if rem[0] == 0:
            rem = rem[1:]
            continue
        scale = gf_div(rem[0], divisor[0])
        for i, d in enumerate(divisor):
            rem[i] ^= gf_mul(scale, d)
        rem = rem[1:]
    return rem


def gf_div(a: int, b: int) -> int:
    if a == 0:
        return 0
    return gf_log[(gf_exp[a] - gf_exp[b]) % 15]


# ---------------------------------------------------------------------------
# Level 1: Single-codeword RS encoding
# ---------------------------------------------------------------------------
def rs_encode_syms_matrix(data_syms: list) -> tuple:
    """Encode 8 GF(16) data symbols → (p0, p1) using P matrix multiply."""
    assert len(data_syms) == 8
    p0, p1 = 0, 0
    for j, s in enumerate(data_syms):
        if s != 0:
            es = gf_exp[s]
            p0 ^= gf_log[(es + coeff0_exp[j]) % 15]
            p1 ^= gf_log[(es + coeff1_exp[j]) % 15]
    return p0, p1


def rs_encode_syms_poly(data_syms: list) -> tuple:
    """Encode 8 GF(16) data symbols → (p0, p1) using polynomial division."""
    # dividend = data * x^2 (append two zero parity slots)
    dividend = list(data_syms) + [0, 0]
    remainder = poly_div_mod(dividend, GEN_POLY)
    # remainder has degree < 2, i.e. exactly 2 coefficients
    while len(remainder) < 2:
        remainder = [0] + remainder
    return remainder[0], remainder[1]


def rs_encode_codeword(cw_bytes: bytearray) -> None:
    """In-place RS encode: read data nibbles from bytes 0-3, write parity to byte 4."""
    syms = [
        (cw_bytes[0] >> 4) & 0xF,
        (cw_bytes[0])      & 0xF,
        (cw_bytes[1] >> 4) & 0xF,
        (cw_bytes[1])      & 0xF,
        (cw_bytes[2] >> 4) & 0xF,
        (cw_bytes[2])      & 0xF,
        (cw_bytes[3] >> 4) & 0xF,
        (cw_bytes[3])      & 0xF,
    ]
    p0m, p1m = rs_encode_syms_matrix(syms)
    p0p, p1p = rs_encode_syms_poly(syms)
    assert p0m == p0p and p1m == p1p, (
        f"Matrix vs poly mismatch: syms={syms} matrix=({p0m},{p1m}) poly=({p0p},{p1p})"
    )
    cw_bytes[4] = (p0m << 4) | p1m


# ---------------------------------------------------------------------------
# Level 2: Interleave (bit-transpose)
# Matches interleaver.cpp: interleave(segments, codewords, p)
# ---------------------------------------------------------------------------
def interleave_ref(segments: bytes, col_height: int) -> bytearray:
    """
    Input:  segments — 32 * col_height bytes, segments[seg * col_height + byte]
    Output: codewords — num_words * 5 bytes, codewords[w * 5 + byte_pos]
    """
    NUM_SEGS = 32
    NUM_COLS = 8
    SEGS_PER_COL = 4
    num_words = col_height * 8

    assert len(segments) == NUM_SEGS * col_height

    cw = bytearray(num_words * 5)

    for w in range(num_words):
        byte_idx = w // 8
        bit_shift = 7 - (w % 8)
        cw_off = w * 5

        for s in range(NUM_COLS):
            sb = s * SEGS_PER_COL
            nibble = (
                (((segments[(sb + 0) * col_height + byte_idx] >> bit_shift) & 1) << 3) |
                (((segments[(sb + 1) * col_height + byte_idx] >> bit_shift) & 1) << 2) |
                (((segments[(sb + 2) * col_height + byte_idx] >> bit_shift) & 1) << 1) |
                (((segments[(sb + 3) * col_height + byte_idx] >> bit_shift) & 1) << 0)
            )
            if s % 2 == 0:
                cw[cw_off + s // 2] = nibble << 4
            else:
                cw[cw_off + s // 2] |= nibble

        cw[cw_off + 4] = 0  # parity slot

    return cw


# ---------------------------------------------------------------------------
# Level 3: Full pipeline — interleave + RS encode + parity extraction
# ---------------------------------------------------------------------------
def rs_encode_buffer(cw: bytearray, col_height: int) -> None:
    """In-place RS encode all codewords in buffer."""
    num_words = col_height * 8
    assert len(cw) == num_words * 5
    for w in range(num_words):
        rs_encode_codeword(memoryview(cw)[w * 5: w * 5 + 5])


def deinterleave_parity_ref(cw: bytes, col_height: int) -> bytearray:
    """
    Extract 8 parity segments from the encoded codeword buffer.

    Parity byte layout: byte 4 of each codeword = (p0 hi nibble | p1 lo nibble).
    Parity columns:
      byte 4 hi nibble = parity symbol 0 → 4 segments (bit3→seg0, bit2→seg1, bit1→seg2, bit0→seg3)
      byte 4 lo nibble = parity symbol 1 → 4 segments (bit3→seg4, bit2→seg5, bit1→seg6, bit0→seg7)

    This is the inverse of interleave for the parity symbols: word W, parity nibble bit b
    maps to segment (sym_pair*4 + bit_pos) at byte byte_idx, bit bit_shift.

    Output: 8 * col_height bytes — parity_segs[seg * col_height + byte_idx]
    """
    num_words = col_height * 8
    assert len(cw) == num_words * 5

    parity = bytearray(8 * col_height)

    for w in range(num_words):
        byte_idx = w // 8
        bit_shift = 7 - (w % 8)
        par_byte = cw[w * 5 + 4]
        p0 = (par_byte >> 4) & 0xF  # parity symbol 0
        p1 =  par_byte       & 0xF  # parity symbol 1

        # parity symbol 0 → segments 0-3 (bit 3 is MSB → seg 0)
        for bit_pos in range(4):
            bit_val = (p0 >> (3 - bit_pos)) & 1
            seg = bit_pos  # seg 0..3
            if bit_val:
                parity[seg * col_height + byte_idx] |= (1 << bit_shift)

        # parity symbol 1 → segments 4-7
        for bit_pos in range(4):
            bit_val = (p1 >> (3 - bit_pos)) & 1
            seg = 4 + bit_pos  # seg 4..7
            if bit_val:
                parity[seg * col_height + byte_idx] |= (1 << bit_shift)

    return parity


def run_full_pipeline(segments: bytes, col_height: int):
    """Run interleave → rs_encode → extract parity. Returns (cw_buf, parity_segs)."""
    cw = interleave_ref(segments, col_height)
    rs_encode_buffer(cw, col_height)
    parity = deinterleave_parity_ref(bytes(cw), col_height)
    return bytes(cw), bytes(parity)


# ---------------------------------------------------------------------------
# Vector generation helpers
# ---------------------------------------------------------------------------
def _rng(seed: int, n: int) -> bytes:
    """Deterministic pseudo-random bytes from seed."""
    r = random.Random(seed)
    return bytes(r.randint(0, 255) for _ in range(n))


def _syms_to_cw_bytes(data_syms: list) -> bytearray:
    """Pack 8 nibble symbols into codeword bytes 0-3 (byte 4 = 0)."""
    assert len(data_syms) == 8
    return bytearray([
        (data_syms[0] << 4) | data_syms[1],
        (data_syms[2] << 4) | data_syms[3],
        (data_syms[4] << 4) | data_syms[5],
        (data_syms[6] << 4) | data_syms[7],
        0,
    ])


# ---------------------------------------------------------------------------
# Level 1 vectors
# ---------------------------------------------------------------------------
def gen_level1():
    vectors = []

    named = [
        ("all_zero",  [0, 0, 0, 0, 0, 0, 0, 0]),
        ("all_one",   [1, 1, 1, 1, 1, 1, 1, 1]),
        ("all_0xF",   [15,15,15,15,15,15,15,15]),
        ("seq_1_8",   [1, 2, 3, 4, 5, 6, 7, 8]),
    ]
    for name, syms in named:
        p0, p1 = rs_encode_syms_matrix(syms)
        p0p, p1p = rs_encode_syms_poly(syms)
        assert p0 == p0p and p1 == p1p, f"Method disagreement on {name}"
        vectors.append({"name": name, "data": syms, "parity": [p0, p1]})

    # Single-symbol isolation: symbol i = v, rest = 0
    for i in range(8):
        for v in [1, 7, 15]:
            syms = [0] * 8
            syms[i] = v
            p0, p1 = rs_encode_syms_matrix(syms)
            p0p, p1p = rs_encode_syms_poly(syms)
            assert p0 == p0p and p1 == p1p
            vectors.append({
                "name": f"iso_sym{i}_val{v}",
                "data": syms,
                "parity": [p0, p1],
            })

    # 20 seeded random vectors
    rng = random.Random(42)
    for i in range(20):
        syms = [rng.randint(0, 15) for _ in range(8)]
        p0, p1 = rs_encode_syms_matrix(syms)
        p0p, p1p = rs_encode_syms_poly(syms)
        assert p0 == p0p and p1 == p1p
        vectors.append({"name": f"rand_{i}", "data": syms, "parity": [p0, p1]})

    return vectors


# ---------------------------------------------------------------------------
# Level 2 vectors
# ---------------------------------------------------------------------------
def gen_level2():
    vectors = []

    configs = [
        (1,  "all_zero",     bytes(32)),
        (1,  "all_0xFF",     bytes([0xFF] * 32)),
        (1,  "single_bit_7", bytes([0x80] + [0] * 31)),
        (1,  "single_bit_0", bytes([0, 0, 0, 0x01] + [0] * 28)),  # seg3, bit0 → word7
        (1,  "walking_one",  bytes([(1 << (i % 8)) for i in range(32)])),
        (8,  "pseudo_rand_seed42", _rng(42, 32 * 8)),
        (512,"pseudo_rand_seed99", _rng(99, 32 * 512)),
    ]

    for col_height, name, segs in configs:
        assert len(segs) == 32 * col_height
        cw = interleave_ref(segs, col_height)
        # Extract only data bytes (0-3) for comparison; byte 4 is always 0 after interleave.
        vectors.append({
            "name": name,
            "col_height": col_height,
            "segments": list(segs),
            "codewords": list(cw),  # includes zero parity bytes
        })

    return vectors


# ---------------------------------------------------------------------------
# Level 3 vectors
# ---------------------------------------------------------------------------
def gen_level3():
    vectors = []

    def make_vec(col_height: int, name: str, segs: bytes):
        cw, parity = run_full_pipeline(segs, col_height)
        return {
            "name": name,
            "col_height": col_height,
            "segments": list(segs),
            "codewords": list(cw),
            "parity_segs": list(parity),
        }

    for col_height in [1, 8, 64, 512]:
        vectors.append(make_vec(col_height, f"all_zero_ch{col_height}", bytes(32 * col_height)))
        vectors.append(make_vec(col_height, f"all_0xFF_ch{col_height}", bytes([0xFF] * (32 * col_height))))
        vectors.append(make_vec(col_height, f"rand_ch{col_height}", _rng(col_height * 7 + 3, 32 * col_height)))

    # Partial-column case: only 24 of 32 segments have data (rest zero-padded).
    for col_height in [8, 64]:
        segs = bytearray(32 * col_height)
        segs[:24 * col_height] = _rng(col_height * 13 + 17, 24 * col_height)
        vectors.append(make_vec(col_height, f"partial24_ch{col_height}", bytes(segs)))

    return vectors


# ---------------------------------------------------------------------------
# C++ header emission
# ---------------------------------------------------------------------------
def emit_header(level1, level2, level3, out_path: str):
    lines = [
        "// Auto-generated by generate_test_vectors.py — do not edit.",
        "#pragma once",
        "#include <cstdint>",
        "#include <cstddef>",
        "",
    ]

    # ---- Level 1 ----
    lines += [
        "// Level 1: single-codeword RS encode vectors.",
        "// data[8]: GF(16) symbols.  parity[2]: expected parity symbols (element space).",
        "struct RsCwVector {",
        "    uint8_t data[8];",
        "    uint8_t parity[2];",
        "};",
        f"static constexpr std::size_t kRsCwVectorCount = {len(level1)};",
        "static constexpr RsCwVector kRsCwVectors[] = {",
    ]
    for v in level1:
        d = ", ".join(str(x) for x in v["data"])
        p = ", ".join(str(x) for x in v["parity"])
        lines.append("    { {" + d + "}, {" + p + "} },  // " + v["name"])
    lines += ["};", ""]

    # ---- Level 2 ----
    lines += [
        "// Level 2: interleave (bit-transpose) vectors.",
        "// segments: 32 * col_height bytes (flat).  codewords: num_words * 5 bytes.",
        "struct InterleaveVector {",
        "    uint32_t    col_height;",
        "    uint32_t    seg_len;     // 32 * col_height",
        "    uint32_t    cw_len;      // num_words * 5 = col_height * 8 * 5",
        "    const uint8_t* segments;",
        "    const uint8_t* codewords;",
        "};",
    ]
    # Emit segment and codeword data arrays first, then the table.
    seg_arrays = []
    cw_arrays = []
    for i, v in enumerate(level2):
        sname = f"kIlSegData_{i}"
        cname = f"kIlCwData_{i}"
        seg_hex = ", ".join(f"0x{b:02X}" for b in v["segments"])
        cw_hex  = ", ".join(f"0x{b:02X}" for b in v["codewords"])
        lines += [
            f"static constexpr uint8_t {sname}[] = {{{seg_hex}}};",
            f"static constexpr uint8_t {cname}[] = {{{cw_hex}}};",
        ]
        seg_arrays.append(sname)
        cw_arrays.append(cname)

    lines += [
        f"static constexpr std::size_t kInterleaveVectorCount = {len(level2)};",
        "static constexpr InterleaveVector kInterleaveVectors[] = {",
    ]
    for i, v in enumerate(level2):
        ch = v["col_height"]
        sl = len(v["segments"])
        cl = len(v["codewords"])
        lines.append(
            f"    {{ {ch}, {sl}, {cl}, {seg_arrays[i]}, {cw_arrays[i]} }},  // {v['name']}"
        )
    lines += ["};", ""]

    # ---- Level 3 ----
    lines += [
        "// Level 3: full pipeline vectors (interleave + rs_encode + parity extraction).",
        "struct PipelineVector {",
        "    uint32_t    col_height;",
        "    uint32_t    seg_len;",
        "    uint32_t    cw_len;",
        "    uint32_t    parity_len;  // 8 * col_height",
        "    const uint8_t* segments;",
        "    const uint8_t* codewords;",
        "    const uint8_t* parity_segs;",
        "};",
    ]
    seg_arrays3 = []
    cw_arrays3  = []
    par_arrays3 = []
    for i, v in enumerate(level3):
        sname = f"kPlSegData_{i}"
        cname = f"kPlCwData_{i}"
        pname = f"kPlParData_{i}"
        seg_hex = ", ".join(f"0x{b:02X}" for b in v["segments"])
        cw_hex  = ", ".join(f"0x{b:02X}" for b in v["codewords"])
        par_hex = ", ".join(f"0x{b:02X}" for b in v["parity_segs"])
        lines += [
            f"static constexpr uint8_t {sname}[] = {{{seg_hex}}};",
            f"static constexpr uint8_t {cname}[] = {{{cw_hex}}};",
            f"static constexpr uint8_t {pname}[] = {{{par_hex}}};",
        ]
        seg_arrays3.append(sname)
        cw_arrays3.append(cname)
        par_arrays3.append(pname)

    lines += [
        f"static constexpr std::size_t kPipelineVectorCount = {len(level3)};",
        "static constexpr PipelineVector kPipelineVectors[] = {",
    ]
    for i, v in enumerate(level3):
        ch = v["col_height"]
        sl = len(v["segments"])
        cl = len(v["codewords"])
        pl = len(v["parity_segs"])
        lines.append(
            f"    {{ {ch}, {sl}, {cl}, {pl}, {seg_arrays3[i]}, {cw_arrays3[i]}, {par_arrays3[i]} }},  // {v['name']}"
        )
    lines += ["};", ""]

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
# JSON emission
# ---------------------------------------------------------------------------
def emit_json(data, path: str):
    with open(path, "w") as f:
        json.dump(data, f, indent=2)


# ---------------------------------------------------------------------------
# FPGA hex emission — $readmemh, 512-bit lines
#
# Convention (matches SystemVerilog packed-array LSB ordering):
#   Each line is 128 hex chars = 512 bits, MSB on the left.
#   Data occupies the LOW bits; unused high bits are zero.
#
#   Nibble arrays (GF(16) symbols):
#     [sym0, sym1, ..., symN] → sym[0] in bits[3:0], sym[N-1] in bits[4N-1:4N-4]
#     Hex string: 0*(128-N) + symN-1_hex ... sym1_hex sym0_hex
#
#   Byte arrays:
#     [byte0, byte1, ..., byteN] → byte[0] in bits[7:0], byte[N-1] in bits[8N-1:8N-8]
#     Hex string: 0*(128-2N) + byteN-1_hex ... byte1_hex byte0_hex
#
#   If the data payload exceeds 64 bytes (512 bits), it is split across multiple
#   consecutive lines, each covering 64 bytes of the payload (little-endian order:
#   lowest-address bytes on the first line in the low bits).
# ---------------------------------------------------------------------------
_HEX_LINE_NIBBLES = 128  # 512 bits
_HEX_LINE_BYTES   = 64


def _nibbles_to_512(nibbles: list) -> str:
    """Pack a list of GF(16) nibbles into a 512-bit hex line (little-endian nibble order)."""
    assert len(nibbles) <= 128, f"too many nibbles: {len(nibbles)}"
    # Build integer: nibble[0] in bits[3:0], nibble[-1] in the highest slot.
    val = 0
    for i, n in enumerate(nibbles):
        val |= (n & 0xF) << (4 * i)
    return f"{val:0{_HEX_LINE_NIBBLES}x}"


def _bytes_to_512_lines(data: bytes) -> list:
    """
    Convert a byte array to one or more 512-bit hex lines.
    byte[0] sits in bits[7:0] of the first line (little-endian byte order).
    """
    lines = []
    for chunk_start in range(0, max(1, len(data)), _HEX_LINE_BYTES):
        chunk = data[chunk_start: chunk_start + _HEX_LINE_BYTES]
        val = 0
        for i, b in enumerate(chunk):
            val |= b << (8 * i)
        lines.append(f"{val:0{_HEX_LINE_NIBBLES}x}")
    return lines


def emit_fpga_hex(level1, level3, out_dir: str):
    """Write FPGA $readmemh hex files into out_dir/fpga/."""
    fpga_dir = os.path.join(out_dir, "fpga")
    os.makedirs(fpga_dir, exist_ok=True)

    # ---- Level 1: RS codeword vectors ----
    # rs_cw_input.hex   : 48 lines, each 512 bits; 8 data symbols in low 32 bits.
    # rs_cw_parity.hex  : 48 lines, each 512 bits; 2 parity symbols in low 8 bits.
    with open(os.path.join(fpga_dir, "rs_cw_input.hex"), "w") as fi, \
         open(os.path.join(fpga_dir, "rs_cw_parity.hex"), "w") as fp:
        for v in level1:
            fi.write(_nibbles_to_512(v["data"]) + "\n")
            fp.write(_nibbles_to_512(v["parity"]) + "\n")

    # ---- Level 3 col_height=1 pipeline vectors ----
    # Segments (32 bytes), codewords (40 bytes), parity_segs (8 bytes) each fit
    # in a single 512-bit line. One line per vector.
    ch1_vecs = [v for v in level3 if v["col_height"] == 1]
    for tag, field, fname in [
        ("segments",    "segments",    "pipeline_ch1_segments.hex"),
        ("codewords",   "codewords",   "pipeline_ch1_codewords.hex"),
        ("parity_segs", "parity_segs", "pipeline_ch1_parity_segs.hex"),
    ]:
        with open(os.path.join(fpga_dir, fname), "w") as f:
            for v in ch1_vecs:
                lines = _bytes_to_512_lines(bytes(v[field]))
                for line in lines:
                    f.write(line + "\n")

    # ---- README documenting format ----
    readme = """\
# FPGA Hex Test Vectors

Generated by `../generate_test_vectors.py`.  All files use Verilog `$readmemh`
format: one 512-bit value per line, 128 hex characters, MSB on the left.

## Bit-packing convention

**Nibble arrays** (GF(16) symbols, 4 bits each):
  nibble[0] occupies bits [3:0], nibble[1] occupies bits [7:4], etc.
  In hex: rightmost digit = nibble[0].

  Matches SystemVerilog packed-array `[N-1:0][3:0]` where index 0 is the LSB.

**Byte arrays** (segment/codeword buffers):
  byte[0] occupies bits [7:0], byte[1] occupies bits [15:8], etc.
  In hex: rightmost two digits = byte[0].

  Matches `[N-1:0][7:0]` where index 0 is the LSB.

## Files

### Level 1 — RS codeword encode

| File | Contents |
|------|----------|
| `rs_cw_input.hex`  | 48 vectors × 1 line; 8 GF(16) data symbols in bits [31:0] |
| `rs_cw_parity.hex` | 48 vectors × 1 line; 2 GF(16) parity symbols in bits [7:0] |

Symbol ordering: sym[0] in bits[3:0], sym[7] in bits[31:28].
Parity ordering: par[0] in bits[3:0], par[1] in bits[7:4].

Example Verilog usage:
```systemverilog
logic [7:0][3:0] ref_data   [0:47];  // [sym_idx][bit]
logic [1:0][3:0] ref_parity [0:47];
// ... or equivalently as wide memory:
logic [511:0] mem_data [0:47];
initial begin
    $readmemh("rs_cw_input.hex",  mem_data);
    $readmemh("rs_cw_parity.hex", mem_parity);
end
// Access: mem_data[i][7:4] = sym[1], mem_data[i][3:0] = sym[0], etc.
```

### Level 3 — Full pipeline (col_height = 1)

| File | Contents | Bytes/vector | Lines/vector |
|------|----------|-------------|-------------|
| `pipeline_ch1_segments.hex`    | 3 vectors; 32-byte segment buffer per vector   | 32 | 1 |
| `pipeline_ch1_codewords.hex`   | 3 vectors; 40-byte codeword buffer per vector  | 40 | 1 |
| `pipeline_ch1_parity_segs.hex` | 3 vectors; 8-byte parity segment buffer        |  8 | 1 |

Vector order: all_zero_ch1, all_0xFF_ch1, rand_ch1 (same as pipeline_vectors.json).

Segment buffer layout: `seg[s * col_height + byte_idx]`, col_height=1 so
`seg[s]` for s=0..31. In the hex line: seg[0] in bits[7:0], seg[31] in bits[255:248].

Codeword buffer layout: `cw[w * 5 + b]` for word w=0..7, byte b=0..4.
cw[0] in bits[7:0], cw[39] in bits[319:312].

Parity segment layout: `par[s * col_height + byte_idx]`, col_height=1.
par[0] in bits[7:0], par[7] in bits[63:56].
"""
    with open(os.path.join(fpga_dir, "README.md"), "w") as f:
        f.write(readme)


# ---------------------------------------------------------------------------
# Self-test: round-trip deinterleave(interleave(x)) == x for each Level 2 vector
# ---------------------------------------------------------------------------
def deinterleave_ref(cw: bytes, col_height: int) -> bytearray:
    """Scalar deinterleave — inverse of interleave_ref (data bytes only)."""
    NUM_SEGS = 32
    num_words = col_height * 8
    assert len(cw) == num_words * 5

    segs = bytearray(NUM_SEGS * col_height)

    for w in range(num_words):
        byte_idx = w // 8
        bit_shift = 7 - (w % 8)
        cw_off = w * 5

        for s in range(8):
            nibble = (cw[cw_off + s // 2] >> (4 if s % 2 == 0 else 0)) & 0xF
            for bit_pos in range(4):
                bit_val = (nibble >> (3 - bit_pos)) & 1
                seg = s * 4 + bit_pos
                if bit_val:
                    segs[seg * col_height + byte_idx] |= (1 << bit_shift)

    return segs


def self_test(level2):
    print("Self-test: deinterleave(interleave(x)) == x ...")
    for v in level2:
        ch = v["col_height"]
        segs_in = bytes(v["segments"])
        cw = bytes(v["codewords"])
        segs_out = deinterleave_ref(cw, ch)
        assert segs_out == bytearray(segs_in), f"Round-trip FAIL for {v['name']}"
    print(f"  All {len(level2)} interleave round-trips OK.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    print("Generating Level 1 (RS codeword) vectors ...")
    level1 = gen_level1()
    print(f"  {len(level1)} vectors")

    print("Generating Level 2 (interleave) vectors ...")
    level2 = gen_level2()
    print(f"  {len(level2)} vectors")

    print("Generating Level 3 (full pipeline) vectors ...")
    level3 = gen_level3()
    print(f"  {len(level3)} vectors")

    self_test(level2)

    out_h    = os.path.join(script_dir, "test_vectors.h")
    out_l1   = os.path.join(script_dir, "rs_cw_vectors.json")
    out_l2   = os.path.join(script_dir, "interleave_vectors.json")
    out_l3   = os.path.join(script_dir, "pipeline_vectors.json")

    print(f"Writing {out_h} ...")
    emit_header(level1, level2, level3, out_h)

    print(f"Writing JSON files ...")
    emit_json(level1, out_l1)
    emit_json(level2, out_l2)
    emit_json(level3, out_l3)

    print(f"Writing FPGA hex files ...")
    emit_fpga_hex(level1, level3, script_dir)

    print("Done.")


if __name__ == "__main__":
    main()
