#!/usr/bin/env python3
"""nano_j2me - PNG decoder fixtures.

Writes tests/fixtures/png/:

    <name>.png            the fixture
    <name>.argb           expected pixels, width*height little-endian uint32s
    positive.txt          name width height has_alpha
    negative.txt          name error_substring

The expected pixels are computed from the sample data that went into the file
(the PNG spec's own arithmetic), and every positive fixture is then decoded
again with Pillow - i.e. libpng - and compared against that expectation.  A
mismatch aborts the generator, so the fixtures cannot silently encode one
thing and expect another.

Pillow's PNG *writer* cannot produce interlaced files, 2/4-bit greyscale or
16-bit truecolour, so those fixtures are built by the small encoder below.  It
is validated the same way: libpng has to agree with it.

Usage:  python3 tests/gen_png_fixtures.py [--out DIR] [--quiet]
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.stderr.write("error: Pillow is required (pip install Pillow)\n")
    sys.exit(2)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(REPO_ROOT, "tests", "fixtures", "png")

PNG_SIG = b"\x89PNG\r\n\x1a\n"

# Adam7: x origin, y origin, x step, y step.
ADAM7 = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
         (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))


# --------------------------------------------------------------------------
# Hand-rolled PNG writer
# --------------------------------------------------------------------------
def chunk(tag: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + tag + payload +
            struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def pack_row(values, depth: int) -> bytes:
    """Pack one row of samples, MSB first inside a byte."""
    if depth == 8:
        return bytes(v & 0xFF for v in values)
    if depth == 16:
        return b"".join(struct.pack(">H", v & 0xFFFF) for v in values)
    out = bytearray()
    acc = 0
    nbits = 0
    for v in values:
        acc = (acc << depth) | (v & ((1 << depth) - 1))
        nbits += depth
        if nbits == 8:
            out.append(acc & 0xFF)
            acc = 0
            nbits = 0
    if nbits:
        out.append((acc << (8 - nbits)) & 0xFF)
    return bytes(out)


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filter_row(cur: bytes, prev: bytes, bpp: int, ftype: int) -> bytes:
    out = bytearray(len(cur))
    for i in range(len(cur)):
        a = cur[i - bpp] if i >= bpp else 0
        b = prev[i] if prev else 0
        c = prev[i - bpp] if (prev and i >= bpp) else 0
        x = cur[i]
        if ftype == 0:
            v = x
        elif ftype == 1:
            v = x - a
        elif ftype == 2:
            v = x - b
        elif ftype == 3:
            v = x - ((a + b) >> 1)
        else:
            v = x - paeth(a, b, c)
        out[i] = v & 0xFF
    return bytes(out)


def raw_stream(width, height, depth, channels, samples, interlace,
               filters=None):
    """Filtered scanline bytes, one pass at a time."""
    passes = ADAM7 if interlace else ((0, 0, 1, 1),)
    bpp = max(1, (depth * channels) // 8)
    raw = bytearray()
    index = 0
    for (x0, y0, dx, dy) in passes:
        pw = -(-(width - x0) // dx) if width > x0 else 0
        ph = -(-(height - y0) // dy) if height > y0 else 0
        if pw == 0 or ph == 0:
            continue
        prev = None
        for j in range(ph):
            values = []
            for i in range(pw):
                values.extend(samples[(x0 + i * dx, y0 + j * dy)])
            row = pack_row(values, depth)
            ftype = filters[index % len(filters)] if filters else 0
            index += 1
            raw.append(ftype)
            raw += filter_row(row, prev, bpp, ftype)
            prev = row
    return bytes(raw)


def build_png(width, height, depth, colour_type, raw, *, interlace=0,
              palette=None, trns=None, extra=(), idat_split=1):
    ihdr = struct.pack(">IIBBBBB", width, height, depth, colour_type, 0, 0,
                       1 if interlace else 0)
    out = bytearray(PNG_SIG)
    out += chunk(b"IHDR", ihdr)
    for (tag, payload) in extra:
        out += chunk(tag, payload)
    if palette is not None:
        out += chunk(b"PLTE", palette)
    if trns is not None:
        out += chunk(b"tRNS", trns)
    data = zlib.compress(raw, 9)
    if idat_split <= 1:
        out += chunk(b"IDAT", data)
    else:
        # Round-robin-ish split so the pieces are of different sizes.
        step = max(1, len(data) // idat_split)
        pos = 0
        pieces = []
        while pos < len(data):
            n = step if len(pieces) < idat_split - 1 else len(data) - pos
            pieces.append(data[pos:pos + n])
            pos += n
        for piece in pieces:
            out += chunk(b"IDAT", piece)
    out += chunk(b"IEND", b"")
    return bytes(out)


# --------------------------------------------------------------------------
# Expected pixels (the spec's arithmetic)
# --------------------------------------------------------------------------
def sample_to_8(v: int, depth: int) -> int:
    if depth == 1:
        return v * 255
    if depth == 2:
        return v * 85
    if depth == 4:
        return v * 17
    if depth == 16:
        return v >> 8
    return v


def expected_argb(width, height, depth, colour_type, samples, palette=None,
                  trns=None):
    """ARGB8888 for every pixel, straight from the source samples."""
    out = []
    grey_trns = struct.unpack(">H", trns)[0] if (trns and colour_type == 0) else None
    rgb_trns = (struct.unpack(">HHH", trns) if (trns and colour_type == 2)
                else None)
    for y in range(height):
        for x in range(width):
            s = samples[(x, y)]
            a = 255
            if colour_type == 0:
                r = g = b = sample_to_8(s[0], depth)
                if grey_trns is not None and s[0] == grey_trns:
                    a = 0
            elif colour_type == 2:
                r, g, b = (sample_to_8(v, depth) for v in s)
                if rgb_trns is not None and tuple(s) == rgb_trns:
                    a = 0
            elif colour_type == 3:
                r, g, b = palette[s[0] * 3:s[0] * 3 + 3]
                if trns is not None and s[0] < len(trns):
                    a = trns[s[0]]
            elif colour_type == 4:
                r = g = b = sample_to_8(s[0], depth)
                a = sample_to_8(s[1], depth)
            else:
                r, g, b = (sample_to_8(v, depth) for v in s[:3])
                a = sample_to_8(s[3], depth)
            out.append(((a & 0xFF) << 24) | (r << 16) | (g << 8) | b)
    return out


# --------------------------------------------------------------------------
# Cross-check with Pillow / libpng
# --------------------------------------------------------------------------
def flat_pixels(im):
    """Pixel access that works on Pillow 9..12 without deprecation noise."""
    getter = getattr(im, "get_flattened_data", None)
    if getter is not None:
        return list(getter())
    return list(im.getdata())


def pil_pixels(path):
    """Decode with Pillow and return (mode, [ (r,g,b,a), ... ])."""
    im = Image.open(path)
    im.load()
    mode = im.mode
    if mode == "I;16":
        # Pillow hands 16-bit greyscale back as raw samples; the decoder must
        # keep the high byte.
        return mode, [(v >> 8, v >> 8, v >> 8, 255) for v in flat_pixels(im)]
    rgba = im.convert("RGBA")
    return mode, list(flat_pixels(rgba))


def cross_check(path, argb, quiet):
    _mode, got = pil_pixels(path)
    want = [((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, (v >> 24) & 0xFF)
            for v in argb]
    if got != want:
        for i, (a, b) in enumerate(zip(got, want)):
            if a != b:
                raise SystemExit(
                    "FAIL %s: libpng disagrees at pixel %d: %r != %r" %
                    (os.path.basename(path), i, a, b))
        raise SystemExit("FAIL %s: libpng decoded %d pixels, expected %d" %
                         (os.path.basename(path), len(got), len(want)))
    if not quiet:
        print("    cross-checked with libpng (%d pixels)" % len(want))


# --------------------------------------------------------------------------
# Fixture definitions
# --------------------------------------------------------------------------
POSITIVE = []      # (name, width, height, has_alpha, argb)
NEGATIVE = []      # (name, error substring)


def emit(out_dir, name, png_bytes, width, height, argb, has_alpha,
         check=True, quiet=False):
    path = os.path.join(out_dir, name + ".png")
    with open(path, "wb") as fh:
        fh.write(png_bytes)
    if check:
        cross_check(path, argb, quiet)
    with open(os.path.join(out_dir, name + ".argb"), "wb") as fh:
        fh.write(b"".join(struct.pack("<I", v) for v in argb))
    POSITIVE.append((name, width, height, has_alpha))
    return path


def add_negative(out_dir, name, png_bytes, expect):
    with open(os.path.join(out_dir, name + ".png"), "wb") as fh:
        fh.write(png_bytes)
    NEGATIVE.append((name, expect))


def pil_save(img, **kwargs):
    import io
    buf = io.BytesIO()
    img.save(buf, "PNG", **kwargs)
    return buf.getvalue()


def build_all(out_dir, quiet):
    def log(msg):
        if not quiet:
            print(msg)

    # ---------------- 1. 8-bit truecolour ----------------
    w, h = 17, 13
    samples = {(x, y): ((x * 13 + y) % 256, (y * 29 + x * 3) % 256,
                        (x * x + y * y) % 256)
               for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 3, samples, 0)
    argb = expected_argb(w, h, 8, 2, samples)
    log("  rgb8")
    emit(out_dir, "rgb8", build_png(w, h, 8, 2, raw), w, h, argb, 0, quiet=quiet)

    # ---------------- 2. 8-bit RGBA ----------------
    w, h = 16, 16
    samples = {(x, y): (x * 16, y * 16, (x + y) * 8,
                        [0, 1, 64, 127, 128, 254, 255][(x + y) % 7])
               for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 4, samples, 0)
    argb = expected_argb(w, h, 8, 6, samples)
    log("  rgba8")
    emit(out_dir, "rgba8", build_png(w, h, 8, 6, raw), w, h, argb, 1, quiet=quiet)

    # ---------------- 3. palette + tRNS ----------------
    w, h = 12, 10
    palette = bytes([(i * 17) % 256 for i in range(3 * 12)])
    alpha = bytes([0, 32, 64, 128, 200, 255, 255, 255, 255, 255, 255, 7])
    samples = {(x, y): ((x + y * 3) % 12,) for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 1, samples, 0)
    argb = expected_argb(w, h, 8, 3, samples, palette=palette, trns=alpha)
    log("  palette8_trns")
    emit(out_dir, "palette8_trns",
         build_png(w, h, 8, 3, raw, palette=palette, trns=alpha),
         w, h, argb, 1, quiet=quiet)

    # ---------------- 4. 4-bit palette ----------------
    w, h = 9, 6
    palette = bytes([0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0,
                     255, 0, 255, 0, 255, 255, 255, 255, 255])
    samples = {(x, y): ((x + y) % 8,) for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 4, 1, samples, 0)
    argb = expected_argb(w, h, 4, 3, samples, palette=palette)
    log("  palette4")
    emit(out_dir, "palette4", build_png(w, h, 4, 3, raw, palette=palette),
         w, h, argb, 0, quiet=quiet)

    # ---------------- 5. 256-entry palette + full tRNS ----------------
    w, h = 16, 16
    palette = bytes((i * 7 + c * 3) % 256 for i in range(256) for c in range(3))
    alpha = bytes((i * 5) % 256 for i in range(256))
    samples = {(x, y): ((x + y * 16) % 256,) for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 1, samples, 0)
    argb = expected_argb(w, h, 8, 3, samples, palette=palette, trns=alpha)
    log("  palette256_trns")
    emit(out_dir, "palette256_trns",
         build_png(w, h, 8, 3, raw, palette=palette, trns=alpha),
         w, h, argb, 1, quiet=quiet)

    # ---------------- 6. 1/2/4-bit greyscale ----------------
    for depth, width, height in ((1, 23, 7), (2, 11, 5), (4, 19, 9)):
        top = (1 << depth) - 1
        samples = {(x, y): ((x * 3 + y * 5) % (top + 1),)
                   for y in range(height) for x in range(width)}
        raw = raw_stream(width, height, depth, 1, samples, 0)
        argb = expected_argb(width, height, depth, 0, samples)
        name = "grey%d" % depth
        log("  " + name)
        emit(out_dir, name, build_png(width, height, depth, 0, raw),
             width, height, argb, 0, quiet=quiet)

    # ---------------- 7. 16-bit greyscale (Pillow can write this one) ------
    w, h = 9, 5
    img = Image.new("I;16", (w, h))
    values = [((x * 7919 + y * 104729) % 65536) for y in range(h) for x in range(w)]
    img.putdata(values)
    samples = {(x, y): (values[y * w + x],) for y in range(h) for x in range(w)}
    argb = expected_argb(w, h, 16, 0, samples)
    log("  grey16")
    emit(out_dir, "grey16", pil_save(img), w, h, argb, 0, quiet=quiet)

    # ---------------- 8. 16-bit truecolour and RGBA ----------------
    for name, ctype, channels in (("rgb16", 2, 3), ("rgba16", 6, 4)):
        w, h = 7, 5
        samples = {}
        for y in range(h):
            for x in range(w):
                vals = tuple((x * 5000 + y * 1300 + c * 7717) % 65536
                             for c in range(channels))
                samples[(x, y)] = vals
        raw = raw_stream(w, h, 16, channels, samples, 0)
        argb = expected_argb(w, h, 16, ctype, samples)
        log("  " + name)
        emit(out_dir, name, build_png(w, h, 16, ctype, raw),
             w, h, argb, 1 if ctype == 6 else 0, quiet=quiet)

    # ---------------- 9. tRNS on greyscale and truecolour ----------------
    w, h = 10, 4
    samples = {(x, y): ((x * 20 + y * 5) % 256,) for y in range(h) for x in range(w)}
    trns = struct.pack(">H", 100)
    raw = raw_stream(w, h, 8, 1, samples, 0)
    argb = expected_argb(w, h, 8, 0, samples, trns=trns)
    log("  grey8_trns")
    emit(out_dir, "grey8_trns", build_png(w, h, 8, 0, raw, trns=trns),
         w, h, argb, 1, quiet=quiet)

    w, h = 10, 4
    samples = {(x, y): ((x * 25) % 256, (y * 60) % 256, 0x40)
               for y in range(h) for x in range(w)}
    trns = struct.pack(">HHH", 50, 60, 0x40)
    raw = raw_stream(w, h, 8, 3, samples, 0)
    argb = expected_argb(w, h, 8, 2, samples, trns=trns)
    log("  rgb8_trns")
    emit(out_dir, "rgb8_trns", build_png(w, h, 8, 2, raw, trns=trns),
         w, h, argb, 1, quiet=quiet)

    # ---------------- 10. grey+alpha (type 4), written by Pillow ----------
    w, h = 13, 6
    samples = {(x, y): ((x * 17 + y) % 256, ((x + y) * 12) % 256)
               for y in range(h) for x in range(w)}
    img = Image.new("LA", (w, h))
    img.putdata([samples[(x, y)] for y in range(h) for x in range(w)])
    argb = expected_argb(w, h, 8, 4, samples)
    log("  grey_alpha8")
    emit(out_dir, "grey_alpha8", pil_save(img), w, h, argb, 1, quiet=quiet)

    # ---------------- 11. every filter type ----------------
    w, h = 21, 10
    samples = {(x, y): ((x * 11 + y * 3) % 256, (x * x + y) % 256,
                        (x + y * y) % 256)
               for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 3, samples, 0, filters=[0, 1, 2, 3, 4])
    argb = expected_argb(w, h, 8, 2, samples)
    log("  filters_rgb8 (filter types 0..4)")
    emit(out_dir, "filters_rgb8", build_png(w, h, 8, 2, raw),
         w, h, argb, 0, quiet=quiet)

    # ---------------- 11b. wide rows, every filter ----------------
    # 900-byte scanlines: exercises the Sub/Average/Paeth predictors across
    # byte distances far larger than one pixel.
    w, h = 300, 200
    samples = {(x, y): ((x * 3 + y * 5) % 256, (x ^ y) % 256,
                        (x * y) % 256)
               for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 3, samples, 0, filters=[1, 2, 3, 4, 0])
    argb = expected_argb(w, h, 8, 2, samples)
    log("  filters_wide_rgb8 (300x200, 900-byte rows)")
    emit(out_dir, "filters_wide_rgb8", build_png(w, h, 8, 2, raw),
         w, h, argb, 0, quiet=quiet)

    # ---------------- 12. Adam7 interlaced ----------------
    w, h = 21, 17
    samples = {(x, y): ((x * 9 + y) % 256, (y * 5 + x * 2) % 256,
                        (x * y) % 256)
               for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 3, samples, 1)
    argb = expected_argb(w, h, 8, 2, samples)
    log("  interlace_rgb8")
    emit(out_dir, "interlace_rgb8", build_png(w, h, 8, 2, raw, interlace=1),
         w, h, argb, 0, quiet=quiet)

    w, h = 13, 11
    samples = {(x, y): ((x * 3 + y) % 2,) for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 1, 1, samples, 1)
    argb = expected_argb(w, h, 1, 0, samples)
    log("  interlace_grey1")
    emit(out_dir, "interlace_grey1", build_png(w, h, 1, 0, raw, interlace=1),
         w, h, argb, 0, quiet=quiet)

    w, h = 19, 13
    samples = {(x, y): ((x * 31) % 256, (y * 47) % 256, (x + y) % 256,
                        (x * y * 3) % 256)
               for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 4, samples, 1, filters=[4, 3, 2, 1, 0])
    argb = expected_argb(w, h, 8, 6, samples)
    log("  interlace_rgba8_filters (Adam7 + all filters)")
    emit(out_dir, "interlace_rgba8_filters",
         build_png(w, h, 8, 6, raw, interlace=1), w, h, argb, 1, quiet=quiet)

    # ---------------- 13. multiple IDAT chunks ----------------
    w, h = 40, 24
    samples = {(x, y): ((x * 6) % 256, (y * 10) % 256, (x ^ y) % 256)
               for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 3, samples, 0)
    argb = expected_argb(w, h, 8, 2, samples)
    log("  multi_idat (5 chunks)")
    emit(out_dir, "multi_idat",
         build_png(w, h, 8, 2, raw, idat_split=5), w, h, argb, 0, quiet=quiet)

    # ---------------- 14. ancillary chunks are skipped, not rejected ------
    w, h = 12, 8
    samples = {(x, y): ((x * 20) % 256, (y * 30) % 256, 0x80)
               for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 3, samples, 0)
    extra = (
        (b"gAMA", struct.pack(">I", 45455)),
        (b"sRGB", b"\x00"),
        (b"pHYs", struct.pack(">IIB", 2835, 2835, 1)),
        (b"tEXt", b"Comment\x00nano_j2me fixture"),
        (b"bKGD", struct.pack(">HHH", 0, 0, 0)),
        (b"zTXt", b"Note\x00\x00" + zlib.compress(b"ignored")),
    )
    argb = expected_argb(w, h, 8, 2, samples)
    log("  ancillary_chunks")
    emit(out_dir, "ancillary_chunks", build_png(w, h, 8, 2, raw, extra=extra),
         w, h, argb, 0, quiet=quiet)

    # ---------------- 15. extreme aspect ratios ----------------
    for name, w, h in (("w300_h1", 300, 1), ("w1_h300", 1, 300),
                       ("w1_h1", 1, 1)):
        samples = {(x, y): ((x * 3 + y * 7) % 256, (x + y * 11) % 256,
                            (x * y) % 256)
                   for y in range(h) for x in range(w)}
        raw = raw_stream(w, h, 8, 3, samples, 0)
        argb = expected_argb(w, h, 8, 2, samples)
        log("  " + name)
        emit(out_dir, name, build_png(w, h, 8, 2, raw), w, h, argb, 0,
             quiet=quiet)

    # ---------------- 16. interlaced extremes ----------------
    for name, w, h in (("interlace_w300_h1", 300, 1),
                       ("interlace_w1_h300", 1, 300)):
        samples = {(x, y): ((x + y) % 256, (x * 2) % 256, (y * 5) % 256)
                   for y in range(h) for x in range(w)}
        raw = raw_stream(w, h, 8, 3, samples, 1)
        argb = expected_argb(w, h, 8, 2, samples)
        log("  " + name)
        emit(out_dir, name, build_png(w, h, 8, 2, raw, interlace=1),
             w, h, argb, 0, quiet=quiet)


def build_negatives(out_dir, quiet):
    def log(msg):
        if not quiet:
            print(msg)

    w, h = 8, 8
    samples = {(x, y): (x * 8, y * 8, 0) for y in range(h) for x in range(w)}
    raw = raw_stream(w, h, 8, 3, samples, 0)
    good = build_png(w, h, 8, 2, raw)

    # CRC mismatch: damage one IDAT byte, leave the stored CRC alone.
    bad = bytearray(good)
    idx = good.index(b"IDAT") + 8
    bad[idx] ^= 0xFF
    log("  neg_bad_crc")
    # The default decoder ignores chunk CRCs (Java parity), so the damaged
    # IDAT surfaces as a DEFLATE error; a strict build reports the stale CRC.
    add_negative(out_dir, "neg_bad_crc", bytes(bad), "DEFLATE")

    # Bad signature.
    bad = bytearray(good)
    bad[0] = 0x88
    log("  neg_bad_sig")
    add_negative(out_dir, "neg_bad_sig", bytes(bad), "signature")

    # Truncated in the middle of the IDAT payload.
    cut = good.index(b"IDAT") + 12
    log("  neg_truncated")
    add_negative(out_dir, "neg_truncated", good[:cut], "past the end")

    # IHDR with width 0 and a correct CRC.
    ihdr = struct.pack(">IIBBBBB", 0, h, 8, 2, 0, 0, 0)
    body = chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + \
        chunk(b"IEND", b"")
    log("  neg_ihdr_w0")
    add_negative(out_dir, "neg_ihdr_w0", PNG_SIG + body, "non-zero")

    # IHDR with height 0.
    ihdr = struct.pack(">IIBBBBB", w, 0, 8, 2, 0, 0, 0)
    body = chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + \
        chunk(b"IEND", b"")
    log("  neg_ihdr_h0")
    add_negative(out_dir, "neg_ihdr_h0", PNG_SIG + body, "non-zero")

    # Oversized: 4097x4097 is 16.79 million pixels, past the 16.7 million cap
    # (a single dimension is no longer the limit - 浴火银河2完整版_7689 ships a
    # 5456x15 sprite sheet - but the pixel count is).
    ihdr = struct.pack(">IIBBBBB", 4097, 4097, 8, 2, 0, 0, 0)
    body = chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + \
        chunk(b"IEND", b"")
    log("  neg_too_large")
    add_negative(out_dir, "neg_too_large", PNG_SIG + body, "16.7 million")

    # Illegal colour type / bit depth combinations.
    for name, depth, ctype in (("neg_ctype5", 8, 5), ("neg_depth3_16", 16, 3),
                               ("neg_depth2_2", 2, 2)):
        ihdr = struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0)
        body = chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + \
            chunk(b"IEND", b"")
        log("  " + name)
        add_negative(out_dir, name, PNG_SIG + body,
                     "depth" if "depth" in name else "colour type")

    # Unsupported interlace method / compression method.
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 2)
    body = chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + \
        chunk(b"IEND", b"")
    log("  neg_interlace2")
    add_negative(out_dir, "neg_interlace2", PNG_SIG + body, "interlace")

    # Missing IDAT.
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IEND", b"")
    log("  neg_no_idat")
    add_negative(out_dir, "neg_no_idat", PNG_SIG + body, "no image data")

    # Missing IEND.
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw))
    log("  neg_no_iend")
    add_negative(out_dir, "neg_no_iend", PNG_SIG + body, "IEND")

    # Unknown *critical* chunk: must be refused.
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"ZZZZ", b"\x00\x01\x02\x03") + \
        chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    log("  neg_critical_chunk")
    add_negative(out_dir, "neg_critical_chunk", PNG_SIG + body, "critical")

    # Valid CRCs, garbage DEFLATE payload.
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", b"\x78\x9c" + b"\xff" * 32) + chunk(b"IEND", b"")
    log("  neg_bad_deflate")
    add_negative(out_dir, "neg_bad_deflate", PNG_SIG + body, "DEFLATE")

    # Valid DEFLATE, but the stream is 20 bytes short of a full image.
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw[:-20])) + chunk(b"IEND", b"")
    log("  neg_short_idat")
    add_negative(out_dir, "neg_short_idat", PNG_SIG + body, "truncated")

    # Palette image without PLTE.
    samples_p = {(x, y): ((x + y) % 4,) for y in range(h) for x in range(w)}
    raw_p = raw_stream(w, h, 8, 1, samples_p, 0)
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw_p)) + chunk(b"IEND", b"")
    log("  neg_palette_no_plte")
    add_negative(out_dir, "neg_palette_no_plte", PNG_SIG + body, "PLTE")

    # tRNS with the wrong length for a greyscale image.
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) + \
        chunk(b"tRNS", b"\x00") + \
        chunk(b"IDAT", zlib.compress(raw_stream(w, h, 8, 1,
                                                {(x, y): (x,) for y in range(h)
                                                 for x in range(w)}, 0))) + \
        chunk(b"IEND", b"")
    log("  neg_trns_len")
    add_negative(out_dir, "neg_trns_len", PNG_SIG + body, "tRNS")

    # Truncated in the middle of a chunk header.
    log("  neg_short_chunk")
    add_negative(out_dir, "neg_short_chunk", good[:20], "past the end")

    # Not a PNG at all - nj_image_load() must recognise the JPEG magic.
    with open(os.path.join(out_dir, "sniff_jpeg.bin"), "wb") as fh:
        fh.write(b"\xff\xd8\xff\xe0" + b"\x00" * 32)
    with open(os.path.join(out_dir, "sniff_unknown.bin"), "wb") as fh:
        fh.write(b"GIF89a" + b"\x00" * 32)
    log("  sniff_jpeg / sniff_unknown")


# --------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    if not args.quiet:
        print("PNG fixtures -> %s" % out_dir)
        print("  positives:")
    build_all(out_dir, args.quiet)
    if not args.quiet:
        print("  negatives:")
    build_negatives(out_dir, args.quiet)

    with open(os.path.join(out_dir, "positive.txt"), "w", newline="\n") as fh:
        fh.write("# name width height has_alpha\n")
        for (name, w, h, has_alpha) in POSITIVE:
            fh.write("%s %d %d %d\n" % (name, w, h, has_alpha))
    with open(os.path.join(out_dir, "negative.txt"), "w", newline="\n") as fh:
        fh.write("# name error_substring\n")
        for (name, expect) in NEGATIVE:
            fh.write("%s %s\n" % (name, expect))

    if not args.quiet:
        print("%d positive, %d negative fixtures" %
              (len(POSITIVE), len(NEGATIVE)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
