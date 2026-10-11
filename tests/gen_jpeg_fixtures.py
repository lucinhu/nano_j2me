#!/usr/bin/env python3
"""
gen_jpeg_fixtures.py - build the JPEG fixtures used by tests/test_jpeg.c.

Two producers are used, on purpose:

  * Pillow, for everything libjpeg can encode (greyscale, 4:4:4/4:2:2/4:2:0,
    quality extremes, progressive).  Pillow is also the *reference decoder*:
    every positive fixture gets a `<name>.rgb` dump of PIL's own decode of the
    same file, which the C test compares against with a mean-absolute-error
    threshold (JPEG is lossy and PIL uses libjpeg's float IDCT, so byte-exact
    comparison is not meaningful - see tests/test_jpeg.c).

  * A small hand-written baseline encoder in this file, for the cases Pillow
    cannot produce: 4:1:1 (Pillow 12 silently rewrites "4:1:1" to 4:2:0),
    an explicit restart interval, 16-bit DQT (Pq=1), SOF1 (extended
    sequential), non-interleaved (one SOS per component) scans, Adobe
    APP14 with transform=0 (RGB) and 4-component CMYK/YCCK (Pillow can write
    CMYK only through its own colour model, and cannot write YCCK at all).
    The hand-built files are still decoded by PIL below, so they are checked
    against an independent decoder as well.

  * Byte surgery on real files for the negative fixtures (truncation, a marker
    length past EOF, an over-subscribed Huffman table, ...).

Layout written below tests/fixtures/jpeg/:

  <name>.jpg            the fixture
  <name>.rgb            PIL's decode of it, 3 bytes/pixel, row-major (8-bit
                        RGB; CMYK sources are converted by PIL, see the test)
  manifest.txt          "name width height kind reference" per line, so the C
                        test never hard-codes a size that could drift
  checksums.txt         SHA-256 of every .jpg, for reproducibility checks

Regeneration is deterministic: no timestamps, fixed RNG seed.
"""

import hashlib
import math
import os
import random
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures", "jpeg")

# ----------------------------------------------------------------------
# Constants from ITU T.81 (Annex K tables, zig-zag order)
# ----------------------------------------------------------------------
ZIGZAG = [
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
]

STD_LUM_QUANT = [
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99,
]
STD_CHR_QUANT = [
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
]

STD_DC_LUM_BITS = [0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0]
STD_DC_LUM_VALS = list(range(12))
STD_DC_CHR_BITS = [0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0]
STD_DC_CHR_VALS = list(range(12))
STD_AC_LUM_BITS = [0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d]
STD_AC_LUM_VALS = [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
]
STD_AC_CHR_BITS = [0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77]
STD_AC_CHR_VALS = [
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
    0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
]


# ----------------------------------------------------------------------
# Test patterns (deterministic)
# ----------------------------------------------------------------------
def pattern_rgb(w, h):
    """A structured image: large flat regions, hard vertical/horizontal edges,
    a smooth gradient, and a few isolated pixels.  Structured so that PIL's
    decode and a correct decoder agree closely, while a channel swap, a
    broken upsample or an off-by-one DC prediction shows up immediately."""
    rnd = random.Random(20240517)
    px = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            if x < w // 3:                       # flat red block
                r, g, b = 0xCC, 0x33, 0x33
            elif x < 2 * w // 3:                 # horizontal ramp
                r = g = b = (x - w // 3) * 255 // max(1, w // 3)
            else:                                # checkerboard
                r, g, b = (200, 200, 200) if ((x // 4 + y // 4) & 1) else (40, 70, 120)
            if y > 3 * h // 4:                   # noise band at the bottom
                r = rnd.randrange(256)
                g = rnd.randrange(256)
                b = rnd.randrange(256)
            i = (y * w + x) * 3
            px[i], px[i + 1], px[i + 2] = r, g, b
    return bytes(px)


def gray_pattern(w, h):
    rnd = random.Random(99)
    px = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            v = (x * 255) // max(1, w - 1)
            if y > h // 2:
                v = rnd.randrange(256)
            px[y * w + x] = v
    return bytes(px)


def solid_rgb(w, h, rgb):
    return bytes(rgb) * (w * h)


# ----------------------------------------------------------------------
# Minimal baseline JPEG encoder (bit-exact, no external deps)
# ----------------------------------------------------------------------
class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def bits(self, value, nbits):
        for i in range(nbits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc & 0xFF)
                if (self.acc & 0xFF) == 0xFF:
                    self.out.append(0x00)      # byte stuffing
                self.acc = 0
                self.n = 0

    def flush(self):
        """Pad the final partial byte with 1 bits (T.81 B.1.1.5)."""
        while self.n:
            self.bits(1, 1)


def build_huff(bits, vals):
    """bits[1..16] = count of codes of that length -> {symbol: (code, len)}"""
    table = {}
    code = 0
    k = 0
    for length in range(1, 17):
        for _ in range(bits[length]):
            table[vals[k]] = (code, length)
            k += 1
            code += 1
        code <<= 1
    return table


def category(value):
    a = abs(value)
    n = 0
    while a:
        a >>= 1
        n += 1
    return n


def amplitude_bits(value, size):
    if size == 0:
        return 0
    return value if value > 0 else value + (1 << size) - 1


_COS = [[math.cos((2 * x + 1) * u * math.pi / 16.0) for u in range(8)]
        for x in range(8)]


def fdct(block):
    """Float 8x8 DCT-II of `block` (natural order, 64 floats).  Only used to
    build fixtures, so clarity beats speed."""
    tmp = [0.0] * 64
    for y in range(8):
        for u in range(8):
            s = 0.0
            for x in range(8):
                s += block[y * 8 + x] * _COS[x][u]
            cu = (1.0 / math.sqrt(2.0)) if u == 0 else 1.0
            tmp[y * 8 + u] = 0.5 * cu * s
    out = [0.0] * 64
    for u in range(8):
        for v in range(8):
            s = 0.0
            for y in range(8):
                s += tmp[y * 8 + u] * _COS[y][v]
            cv = (1.0 / math.sqrt(2.0)) if v == 0 else 1.0
            out[v * 8 + u] = 0.5 * cv * s
    return out


class Component:
    def __init__(self, cid, h, v, tq, plane, dc_tbl, ac_tbl):
        self.cid, self.h, self.v, self.tq = cid, h, v, tq
        self.plane = plane          # callable (x, y) -> sample
        self.dc_tbl, self.ac_tbl = dc_tbl, ac_tbl


class JpegEncoder:
    """Baseline sequential encoder.  `components` share a max sampling grid
    given by the largest h/v."""

    def __init__(self, width, height, components, quants,
                 precision=8, sof=0xC0, restart_interval=0,
                 adobe_transform=None, jfif=True, app_payloads=(),
                 comment=None, non_interleaved=False):
        self.w, self.h = width, height
        self.comps = components
        self.quants = quants                     # {tq: [64 natural]}
        self.precision = precision
        self.sof = sof
        self.restart_interval = restart_interval
        self.adobe_transform = adobe_transform
        self.jfif = jfif
        self.app_payloads = app_payloads         # [(marker, bytes), ...]
        self.comment = comment
        self.non_interleaved = non_interleaved

    # ---- block extraction with edge replication -------------------
    @staticmethod
    def _block(plane, bx, by, xmax, ymax):
        out = [0.0] * 64
        for y in range(8):
            sy = min(by * 8 + y, ymax - 1)
            for x in range(8):
                sx = min(bx * 8 + x, xmax - 1)
                out[y * 8 + x] = float(plane(sx, sy)) - 128.0
        return out

    def _encode_block(self, bw, comp, plane, bx, by, xmax, ymax, pred):
        c = fdct(self._block(plane, bx, by, xmax, ymax))
        q = self.quants[comp.tq]
        coef = [0] * 64
        for i in range(64):
            v = int(round(c[i] / q[i]))
            coef[i] = max(-1023, min(1023, v))
        # DC
        diff = coef[0] - pred[0]
        pred[0] = coef[0]
        s = category(diff)
        code, nbits = self.dc_tbl[comp.dc_tbl][s]
        bw.bits(code, nbits)
        if s:
            bw.bits(amplitude_bits(diff, s), s)
        # AC, zig-zag with run-length coding
        run = 0
        for k in range(1, 64):
            v = coef[ZIGZAG[k]]
            if v == 0:
                run += 1
                continue
            while run >= 16:
                code, nbits = self.ac_tbl[comp.ac_tbl][0xF0]
                bw.bits(code, nbits)
                run -= 16
            s = category(v)
            sym = (run << 4) | s
            code, nbits = self.ac_tbl[comp.ac_tbl][sym]
            bw.bits(code, nbits)
            bw.bits(amplitude_bits(v, s), s)
            run = 0
        if run:
            code, nbits = self.ac_tbl[comp.ac_tbl][0x00]
            bw.bits(code, nbits)

    def _mcus_per_row(self, hmax, vmax):
        return (self.w + 8 * hmax - 1) // (8 * hmax)

    def _mcu_rows(self, hmax, vmax):
        return (self.h + 8 * vmax - 1) // (8 * vmax)

    def encode(self):
        hmax = max(c.h for c in self.comps)
        vmax = max(c.v for c in self.comps)
        self.dc_tbl = {}
        self.ac_tbl = {}
        for t in range(4):
            self.dc_tbl[t] = build_huff(STD_DC_LUM_BITS if t == 0 else STD_DC_CHR_BITS,
                                        STD_DC_LUM_VALS if t == 0 else STD_DC_CHR_VALS)
            self.ac_tbl[t] = build_huff(STD_AC_LUM_BITS if t == 0 else STD_AC_CHR_BITS,
                                        STD_AC_LUM_VALS if t == 0 else STD_AC_CHR_VALS)

        body = bytearray()
        body += self._markers_prefix()
        body += self._sof(hmax, vmax)
        body += self._dht()
        body += self._sos_and_scan(hmax, vmax)
        return b"\xff\xd8" + bytes(body) + b"\xff\xd9"

    # ---- segments --------------------------------------------------
    def _markers_prefix(self):
        out = bytearray()
        if self.jfif:
            app0 = (b"JFIF\x00\x01\x02\x00\x00\x01\x00\x01\x00\x00")
            out += b"\xff\xe0" + struct.pack(">H", len(app0) + 2) + app0
        for marker, payload in self.app_payloads:
            out += bytes([0xFF, marker]) + struct.pack(">H", len(payload) + 2) + payload
        if self.adobe_transform is not None:
            adobe = b"Adobe\x00\x64\x00\x00\x00\x00" + bytes([self.adobe_transform])
            out += b"\xff\xee" + struct.pack(">H", len(adobe) + 2) + adobe
        if self.comment is not None:
            cb = self.comment.encode("ascii")
            out += b"\xff\xfe" + struct.pack(">H", len(cb) + 2) + cb
        for tq in sorted(self.quants):
            out += self._dqt(tq)
        return out

    def _dqt(self, tq):
        q = self.quants[tq]
        wide = any(v > 255 for v in q)
        payload = bytearray([(1 if wide else 0) << 4 | tq])
        for k in range(64):
            v = q[ZIGZAG[k]]
            if wide:
                payload += struct.pack(">H", v)
            else:
                payload.append(v)
        return b"\xff\xdb" + struct.pack(">H", len(payload) + 2) + bytes(payload)

    def _sof(self, hmax, vmax):
        payload = bytearray([self.precision,
                             (self.h >> 8) & 0xFF, self.h & 0xFF,
                             (self.w >> 8) & 0xFF, self.w & 0xFF,
                             len(self.comps)])
        for c in self.comps:
            payload += bytes([c.cid, (c.h << 4) | c.v, c.tq])
        return bytes([0xFF, self.sof]) + struct.pack(">H", len(payload) + 2) + bytes(payload)

    def _dht(self):
        out = bytearray()
        for t in sorted(self.dc_tbl):
            bits = STD_DC_LUM_BITS if t == 0 else STD_DC_CHR_BITS
            vals = STD_DC_LUM_VALS if t == 0 else STD_DC_CHR_VALS
            payload = bytes([t]) + bytes(bits[1:17]) + bytes(vals)
            out += b"\xff\xc4" + struct.pack(">H", len(payload) + 2) + payload
        for t in sorted(self.ac_tbl):
            bits = STD_AC_LUM_BITS if t == 0 else STD_AC_CHR_BITS
            vals = STD_AC_LUM_VALS if t == 0 else STD_AC_CHR_VALS
            payload = bytes([0x10 | t]) + bytes(bits[1:17]) + bytes(vals)
            out += b"\xff\xc4" + struct.pack(">H", len(payload) + 2) + payload
        return bytes(out)

    def _sos_segment(self, comps, hmax, vmax):
        payload = bytearray([len(comps)])
        for c in comps:
            payload += bytes([c.cid, (c.dc_tbl << 4) | c.ac_tbl])
        payload += bytes([0x00, 0x3F, 0x00])
        return b"\xff\xda" + struct.pack(">H", len(payload) + 2) + bytes(payload)

    def _scan(self, comps, hmax, vmax):
        bw = BitWriter()
        if self.non_interleaved:
            assert len(comps) == 1
            c = comps[0]
            cw = (self.w * c.h + hmax - 1) // hmax
            ch = (self.h * c.v + vmax - 1) // vmax
            bxs = (cw + 7) // 8
            bys = (ch + 7) // 8
            pred = [0]
            total = bxs * bys
            for i in range(total):
                if self.restart_interval and i and i % self.restart_interval == 0:
                    bw.flush()
                    bw.out += bytes([0xFF, 0xD0 + ((i // self.restart_interval - 1) & 7)])
                    pred[0] = 0
                self._encode_block(bw, c, c.plane, i % bxs, i // bxs, cw, ch, pred)
        else:
            mcus = self._mcus_per_row(hmax, vmax) * self._mcu_rows(hmax, vmax)
            preds = {c.cid: [0] for c in comps}
            i = 0
            for my in range(self._mcu_rows(hmax, vmax)):
                for mx in range(self._mcus_per_row(hmax, vmax)):
                    if self.restart_interval and i and i % self.restart_interval == 0:
                        bw.flush()
                        bw.out += bytes([0xFF, 0xD0 + ((i // self.restart_interval - 1) & 7)])
                        for c in comps:
                            preds[c.cid] = [0]
                    for c in comps:
                        cw = (self.w * c.h + hmax - 1) // hmax
                        ch = (self.h * c.v + vmax - 1) // vmax
                        for by in range(c.v):
                            for bx in range(c.h):
                                self._encode_block(bw, c, c.plane,
                                                   mx * c.h + bx, my * c.v + by,
                                                   cw, ch, preds[c.cid])
                    i += 1
            assert i == mcus
        bw.flush()
        return bytes(bw.out)

    def _sos_and_scan(self, hmax, vmax):
        out = bytearray()
        if self.restart_interval:
            out += b"\xff\xdd" + struct.pack(">H", 4) + \
                   struct.pack(">H", self.restart_interval)
        if self.non_interleaved:
            for c in self.comps:
                out += self._sos_segment([c], hmax, vmax)
                out += self._scan([c], hmax, vmax)
        else:
            out += self._sos_segment(self.comps, hmax, vmax)
            out += self._scan(self.comps, hmax, vmax)
        return bytes(out)


# ---- colour helpers ---------------------------------------------------
def clamp8(v):
    return 0 if v < 0 else (255 if v > 255 else int(v))


def ycbcr_to_rgb(y, cb, cr):
    r = clamp8(round(y + 1.402 * (cr - 128)))
    g = clamp8(round(y - 0.344136 * (cb - 128) - 0.714136 * (cr - 128)))
    b = clamp8(round(y + 1.772 * (cb - 128)))
    return r, g, b


def rgb_to_ycbcr(r, g, b):
    y = clamp8(round(0.299 * r + 0.587 * g + 0.114 * b))
    cb = clamp8(round(-0.168736 * r - 0.331264 * g + 0.5 * b + 128))
    cr = clamp8(round(0.5 * r - 0.418688 * g - 0.081312 * b + 128))
    return y, cb, cr


def make_ycbcr_components(width, height, rgb, sampling, qtable=0,
                          cqtable=1, ids=(1, 2, 3), labels="YCbCr"):
    """Build 3 components sampling raw RGB into Y/Cb/Cr planes."""
    hmax, vmax = sampling[0], sampling[1]
    planes = []
    for ci in range(3):
        cw = (width * sampling[2 + ci][0] + hmax - 1) // hmax
        ch = (height * sampling[2 + ci][1] + vmax - 1) // vmax

        def make(cw=cw, ch=ch, ci=ci):
            def sample(x, y):
                sx = min(x * hmax // sampling[2 + ci][0], width - 1)
                sy = min(y * vmax // sampling[2 + ci][1], height - 1)
                i = (sy * width + sx) * 3
                yv, cb, cr = rgb_to_ycbcr(rgb[i], rgb[i + 1], rgb[i + 2])
                return (yv, cb, cr)[ci]
            return sample
        planes.append(make())
    comps = []
    for ci in range(3):
        comps.append(Component(ids[ci], sampling[2 + ci][0], sampling[2 + ci][1],
                               qtable if ci == 0 else cqtable, planes[ci],
                               0 if ci == 0 else 1, 0 if ci == 0 else 1))
    return comps, {qtable: STD_LUM_QUANT, cqtable: STD_CHR_QUANT}


def make_rgb_components(width, height, rgb, ids=(ord('R'), ord('G'), ord('B'))):
    planes = []
    for ci in range(3):
        def make(ci=ci):
            def sample(x, y):
                return rgb[(y * width + x) * 3 + ci]
            return sample
        planes.append(make())
    comps = [Component(ids[ci], 1, 1, 0 if ci == 0 else 1, planes[ci],
                       0 if ci == 0 else 1, 0 if ci == 0 else 1) for ci in range(3)]
    return comps, {0: STD_LUM_QUANT, 1: STD_CHR_QUANT}


# ----------------------------------------------------------------------
# Fixture installation
# ----------------------------------------------------------------------
FILES = {}
NOTES = {}
# Fixtures whose `.rgb` reference is NOT Pillow's decode:
#  * the 4-component ones, whose source is the RGB the encoder started from.
#    Pillow converts CMYK with a multiplicative ink model
#    (R = (255-C)*(255-K)/255) while the decoder under test implements the
#    additive one (R = 255 - min(255, C+K)), which is the exact inverse of the
#    generator's CMYK encoder; comparing against Pillow there would test the
#    ink model, not the decoder.  Pillow's own CMYK decode is still checked
#    for plausibility by hand in the generator's output.
SOURCE_REF = {}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def add(name, data, note=""):
    FILES[name] = data
    NOTES[name] = note
    return name


# ----------------------------------------------------------------------
# Positive fixtures
# ----------------------------------------------------------------------
def positive_fixtures():
    # --- Pillow-produced ------------------------------------------------
    im = Image.frombytes("L", (17, 13), gray_pattern(17, 13))
    add("gray_17x13", _pil(im, quality=85), "gray 8-bit 17x13 (not a multiple of 8)")

    for sub, tag in ((0, "444"), (1, "422"), (2, "420")):
        rgb = pattern_rgb(35, 23)
        im = Image.frombytes("RGB", (35, 23), rgb)
        add("ycbcr%s_35x23" % tag, _pil(im, quality=85, subsampling=sub),
            "YCbCr 4:%s 35x23" % {"444": "4:4:4", "422": "4:2:2", "420": "4:2:0"}[tag])

    # 4:1:1: Pillow 12 maps "4:1:1" to 4:2:0 (see JpegImagePlugin._save), so
    # this one is hand-built with h=4, v=1 and decoded back by PIL below.
    rgb = pattern_rgb(35, 23)
    comps, quants = make_ycbcr_components(35, 23, rgb, (4, 1, (4, 1), (1, 1), (1, 1)))
    add("ycbcr411_35x23", JpegEncoder(35, 23, comps, quants).encode(),
        "YCbCr 4:1:1 35x23 (hand-built: PIL cannot encode 4:1:1)")

    im = Image.frombytes("RGB", (16, 16), solid_rgb(16, 16, (0x33, 0x66, 0xCC)))
    add("solid_16x16_q95", _pil(im, quality=95), "flat #3366CC, quality 95")

    im = Image.frombytes("RGB", (1, 1), bytes((0xE0, 0x40, 0x20)))
    add("one_1x1", _pil(im, quality=90), "1x1")
    im = Image.frombytes("RGB", (300, 1), pattern_rgb(300, 1))
    add("wide_300x1", _pil(im, quality=90), "300x1")
    im = Image.frombytes("RGB", (1, 300), pattern_rgb(1, 300))
    add("tall_1x300", _pil(im, quality=90), "1x300")

    im = Image.frombytes("RGB", (64, 64), pattern_rgb(64, 64))
    add("mcu_16x16", _pil(Image.frombytes("RGB", (16, 16), pattern_rgb(16, 16)),
                          quality=88), "exactly one 4:2:0 MCU")

    # Restart interval: hand-built so the DRI really is there.
    rgb = pattern_rgb(64, 64)
    comps, quants = make_ycbcr_components(64, 64, rgb, (2, 2, (2, 2), (1, 1), (1, 1)))
    add("restart_64x64", JpegEncoder(64, 64, comps, quants, restart_interval=3).encode(),
        "restart interval 3 MCUs (hand-built)")

    for q in (1, 100):
        im = Image.frombytes("RGB", (35, 23), pattern_rgb(35, 23))
        add("q%d_35x23" % q, _pil(im, quality=q), "quality %d" % q)

    for tag, colour in (("r", (255, 0, 0)), ("g", (0, 255, 0)), ("b", (0, 0, 255))):
        im = Image.frombytes("RGB", (32, 32), solid_rgb(32, 32, colour))
        add("pure_%s_32x32" % tag, _pil(im, quality=95), "pure %s 32x32" % tag)

    im = Image.frombytes("RGB", (64, 48), pattern_rgb(64, 48))
    add("pattern_64x48", _pil(im, quality=85),
        "structured 64x48: MAE + 1/2 and 1/4 DCT-domain downscale")
    # References for the scaled decode: Pillow's draft() mode drives libjpeg's
    # own reduced-size IDCT, so it is a like-for-like reference (a box
    # downsample of the full decode is not: the reduced IDCT is a low-pass of
    # the block, not a 2x2 average).

    # --- hand-built -----------------------------------------------------
    rgb = pattern_rgb(35, 23)
    comps, quants = make_ycbcr_components(35, 23, rgb, (2, 2, (2, 2), (1, 1), (1, 1)))
    wide = {0: [v * 4 + 3 for v in STD_LUM_QUANT], 1: [v * 4 + 3 for v in STD_CHR_QUANT]}
    wide[0] = [max(1, min(65535, v)) for v in wide[0]]
    add("dqt16_35x23",
        JpegEncoder(35, 23, comps, wide).encode(),
        "16-bit DQT (Pq=1, values > 255)")
    add("sof1_35x23",
        JpegEncoder(35, 23, comps, quants, sof=0xC1).encode(),
        "SOF1 extended sequential")

    # COM + APPn skipping, and Adobe APP14 transform=1 (YCbCr).
    add("com_app_35x23",
        JpegEncoder(35, 23, comps, quants,
                    adobe_transform=1, comment="nano_j2me fixture",
                    app_payloads=[(0xE1, b"Exif\x00\x00fixture")]).encode(),
        "COM + APP1 + APP14 transform=1")

    # Adobe transform=0 with component IDs 'R','G','B': no colour transform.
    rgb = pattern_rgb(32, 16)
    comps, quants = make_rgb_components(32, 16, rgb)
    add("adobe_rgb_32x16", JpegEncoder(32, 16, comps, quants,
                                       adobe_transform=0, jfif=False).encode(),
        "3 components, APP14 transform=0, ids RGB (no colour transform)")

    # 4-component Adobe CMYK (transform=0): stored inverted, K included.
    rgb = pattern_rgb(32, 32)
    planes = []
    for ci in range(4):
        def make(ci=ci):
            def sample(x, y):
                r, g, b = rgb[(y * 32 + x) * 3:(y * 32 + x) * 3 + 3]
                c, m, yy = 255 - r, 255 - g, 255 - b
                k = min(c, m, yy)
                c, m, yy = c - k, m - k, yy - k
                return (255 - c, 255 - m, 255 - yy, 255 - k)[ci]
            return sample
        planes.append(make())
    comps = [Component(1 + ci, 1, 1, 0 if ci == 0 else 1, planes[ci],
                       0 if ci == 0 else 1, 0 if ci == 0 else 1) for ci in range(4)]
    add("cmyk_32x32",
        JpegEncoder(32, 32, comps, {0: STD_LUM_QUANT, 1: STD_CHR_QUANT},
                    adobe_transform=0, jfif=False).encode(),
        "4 components, APP14 transform=0 (inverted CMYK)")
    SOURCE_REF["cmyk_32x32"] = rgb

    # 4-component Adobe YCCK (transform=2).
    planes = []
    ycc = []
    for i in range(32 * 32):
        r, g, b = rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]
        c, m, yy = 255 - r, 255 - g, 255 - b
        k = min(c, m, yy)
        ycc.append((rgb_to_ycbcr(255 - (c - k), 255 - (m - k), 255 - (yy - k)), 255 - k))

    def make_plane(ci):
        def sample(x, y):
            (yv, cb, cr), kk = ycc[y * 32 + x]
            return (yv, cb, cr, kk)[ci]
        return sample
    planes = [make_plane(ci) for ci in range(4)]
    comps = [Component(1 + ci, 1, 1, 0 if ci == 0 else 1, planes[ci],
                       0 if ci == 0 else 1, 0 if ci == 0 else 1) for ci in range(4)]
    add("ycck_32x32",
        JpegEncoder(32, 32, comps, {0: STD_LUM_QUANT, 1: STD_CHR_QUANT},
                    adobe_transform=2, jfif=False).encode(),
        "4 components, APP14 transform=2 (YCCK)")
    SOURCE_REF["ycck_32x32"] = rgb

    # Non-interleaved sequential: one scan per component.
    rgb = pattern_rgb(24, 16)
    comps, quants = make_ycbcr_components(24, 16, rgb, (2, 2, (2, 2), (1, 1), (1, 1)))
    add("noninterleaved_24x16",
        JpegEncoder(24, 16, comps, quants, non_interleaved=True).encode(),
        "three sequential scans, one component each")

    im = Image.frombytes("RGB", (35, 23), pattern_rgb(35, 23))
    add("progressive_35x23", _pil(im, quality=85, progressive=True),
        "SOF2 progressive: accepts()==1, decode must fail cleanly")


def _pil(im, **kw):
    import io
    buf = io.BytesIO()
    im.save(buf, format="JPEG", **kw)
    return buf.getvalue()


# ----------------------------------------------------------------------
# Negative fixtures (byte surgery on a real file)
# ----------------------------------------------------------------------
def segments(data):
    """(marker, offset_of_length_field) for every segment before the scan."""
    out = []
    i = 2
    while i + 3 < len(data):
        if data[i] != 0xFF:
            break
        m = data[i + 1]
        if m == 0xDA:
            out.append((m, i))
            break
        if m == 0xD9:
            break
        if m in (0x01,) or 0xD0 <= m <= 0xD7:
            i += 2
            continue
        ln = (data[i + 2] << 8) | data[i + 3]
        out.append((m, i))
        i += 2 + ln
    return out


def find_segment(data, marker):
    for m, off in segments(data):
        if m == marker:
            return off
    return -1


def scan_offset(data):
    return find_segment(data, 0xDA)


def craft_dht(table_class, table_id, bits, vals):
    """A DHT segment built by hand (bits[1..16], one value per code)."""
    payload = bytes([(table_class << 4) | table_id]) + bytes(bits[1:17]) + bytes(vals)
    return b"\xff\xc4" + struct.pack(">H", len(payload) + 2) + payload


def replace_segment(data, marker, occurrence, new_segment):
    for n, (m, off) in enumerate(segments(data)):
        if m == marker:
            if occurrence:
                occurrence -= 1
                continue
            ln = (data[off + 2] << 8) | data[off + 3]
            return bytes(data[:off]) + new_segment + bytes(data[off + 2 + ln:])
    raise AssertionError("segment FF%02X not found" % marker)


def negative_fixtures():
    base = FILES["ycbcr420_35x23"]

    # Truncated in the middle of the entropy-coded scan (well before the last
    # MCU), with the EOI removed as well.
    sos = scan_offset(base)
    cut = sos + (len(base) - sos) * 55 // 100
    add("trunc_mid", base[:cut], "entropy data cut ~45% short, no EOI")

    add("soi_only", b"\xff\xd8\xff\xd9", "SOI + EOI, nothing else")

    # Over-subscribed Huffman table: three codes of length 1 (Kraft sum > 1).
    d = bytearray(base)
    off = find_segment(d, 0xC4)
    d[off + 4] = 0x00                     # DC table 0
    for k in range(16):                   # bits[1..16]
        d[off + 5 + k] = 0x00
    d[off + 5] = 0x03                     # three 1-bit codes: over-subscribed
    add("bad_huff_oversub", bytes(d), "Huffman table: three 1-bit codes")

    # "All 1s code lengths": sixteen codes of length 1..16, in a well-formed
    # segment.  Legal per Annex C but it leaves the all-ones code as the only
    # 16-bit code word, so the entropy stream decodes to nonsense and the
    # decoder must fail cleanly rather than run away.
    d = bytes(base)
    d = replace_segment(d, 0xC4, 0,
                        craft_dht(0, 0, [0] + [1] * 16, list(range(16))))
    add("bad_huff_allones", d, "Huffman table with all-1s code lengths")

    # Marker length that runs past EOF.
    d = bytearray(base)
    off = find_segment(d, 0xDB)
    d[off + 2], d[off + 3] = 0xFF, 0xF0
    add("len_past_eof", bytes(d), "DQT segment length runs past EOF")

    # Cut inside the final MCU only: the last MCU of a 16x16 4:2:0 image is
    # the whole image, so this is the "truncated final MCU" case the decoder
    # has to complete with neutral grey rather than reject or leave unwritten.
    one_mcu = FILES["mcu_16x16"]
    add("trunc_last_mcu", one_mcu[:-6], "cut inside the final (only) MCU")
    POSITIVE_EXTRA.append("trunc_last_mcu")

    # Zero dimension in the SOF.
    d = bytearray(base)
    off = find_segment(d, 0xC0)
    d[off + 5] = 0x00                     # height high byte
    d[off + 6] = 0x00
    add("zero_dim", bytes(d), "SOF height == 0")

    # Five components: not a colour model this decoder can render.
    d = bytearray(base)
    off = find_segment(d, 0xC0)
    ln = (d[off + 2] << 8) | d[off + 3]
    body = bytes(d[off + 2:off + 2 + ln])
    newbody = bytearray(body)
    newbody[7] = 5                        # Nf
    newbody += bytes([4, 0x11, 0x00, 5, 0x11, 0x00])
    newbody[0:2] = struct.pack(">H", len(newbody))
    add("five_comp", bytes(d[:off + 2]) + bytes(newbody) + bytes(d[off + 2 + ln:]),
        "SOF claims 5 components")

    # Two components: legal JPEG, but not a colour model we render.
    d = bytearray(base)
    off = find_segment(d, 0xC0)
    ln = (d[off + 2] << 8) | d[off + 3]
    body = bytearray(d[off + 2:off + 2 + ln])
    body = body[:8] + bytes([3, 0x11, 0x00])
    body[7] = 2
    body[0:2] = struct.pack(">H", len(body))
    add("two_comp", bytes(d[:off + 2]) + bytes(body) + bytes(d[off + 2 + ln:]),
        "SOF claims 2 components")

    # Arithmetic-coded (SOF9).
    d = bytearray(base)
    off = find_segment(d, 0xC0)
    d[off + 1] = 0xC9
    add("arith_sof9", bytes(d), "SOF9 arithmetic-coded")

    # A restart interval with the wrong restart marker.
    r = bytearray(FILES["restart_64x64"])
    i = r.find(b"\xff\xd0")
    assert i > 0
    r[i + 1] = 0xD3                       # expected RST0
    add("bad_restart", bytes(r), "wrong RSTn after the restart interval")

    # A scan with no frame header.
    d = bytearray(base)
    off = find_segment(d, 0xC0)
    ln = (d[off + 2] << 8) | d[off + 3]
    add("no_sof", bytes(d[:off]) + bytes(d[off + 2 + ln:]),
        "SOS before any SOF")


# ----------------------------------------------------------------------
# main
# ----------------------------------------------------------------------
# Fixtures produced by negative_fixtures() that are nevertheless expected to
# decode (the decoder completes a short final MCU with neutral grey).
POSITIVE_EXTRA = []

NEGATIVE = ("soi_only", "bad_huff_oversub", "bad_huff_allones", "len_past_eof",
            "zero_dim", "five_comp", "two_comp", "arith_sof9", "bad_restart",
            "no_sof", "trunc_mid")


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))

    positive_fixtures()
    negative_fixtures()

    failures = []
    manifest = []
    names = sorted(FILES)
    for name in names:
        path = os.path.join(OUT, name + ".jpg")
        with open(path, "wb") as f:
            f.write(FILES[name])
        if name in NEGATIVE and name not in POSITIVE_EXTRA:
            manifest.append((name, 0, 0, "negative", NOTES.get(name, "")))
            continue
        if name == "trunc_last_mcu":
            # PIL refuses a truncated file, so the reference is PIL's decode
            # of the complete one; the two images are identical up to the cut
            # and the decoder under test must complete the final MCU as grey.
            with Image.open(os.path.join(OUT, "mcu_16x16.jpg")) as im2:
                NOTES[name] = ("cut inside the final MCU; reference is PIL's "
                               "decode of the complete mcu_16x16")
                with open(os.path.join(OUT, name + ".rgb"), "wb") as f:
                    f.write(im2.convert("RGB").tobytes())
            manifest.append((name, 16, 16, "positive", NOTES[name]))
            continue
        # Every positive fixture must be decodable by PIL as well: that is
        # what makes the .rgb dump a meaningful reference.  PIL's decode of
        # the very same file is the `.rgb` the C test compares against.
        try:
            with Image.open(path) as im:
                w, h = im.size
                rgb = im.convert("RGB").tobytes()
        except Exception as exc:                       # pragma: no cover
            failures.append("%s: PIL cannot open it: %s" % (name, exc))
            continue
        if name in SOURCE_REF:
            rgb = SOURCE_REF[name]
            NOTES[name] += " [reference is the source RGB: see the header]"
        with open(os.path.join(OUT, name + ".rgb"), "wb") as f:
            f.write(rgb)
        if name == "pattern_64x48":
            for factor in (2, 4):
                with Image.open(path) as im:
                    im.draft("RGB", (w // factor, h // factor))
                    scaled = im.convert("RGB")
                if scaled.size != (w // factor, h // factor):
                    failures.append("%s: draft %d gave %r" %
                                    (name, factor, scaled.size))
                with open(os.path.join(OUT, "%s.draft%d.rgb" % (name, factor)),
                          "wb") as f:
                    f.write(scaled.tobytes())
        manifest.append((name, w, h, "positive", NOTES.get(name, "")))

    with open(os.path.join(OUT, "manifest.txt"), "w") as f:
        f.write("# name width height kind\n")
        for name, w, h, kind, note in manifest:
            f.write("%s %d %d %s\n" % (name, w, h, kind))
        f.write("# notes:\n")
        for name, w, h, kind, note in manifest:
            f.write("#   %s: %s\n" % (name, note))

    with open(os.path.join(OUT, "checksums.txt"), "w") as f:
        for name in names:
            f.write("%s  %s.jpg\n" % (sha256(FILES[name]), name))

    print("wrote %d fixtures to %s" % (len(names), OUT))
    for name, w, h, kind, note in manifest:
        print("  %-24s %5dx%-4d %-9s %6d bytes  %s"
              % (name + ".jpg", w, h, kind, len(FILES[name]), note))
    if failures:
        for x in failures:
            print("FAIL", x, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
