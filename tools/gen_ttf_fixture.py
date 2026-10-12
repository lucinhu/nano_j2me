#!/usr/bin/env python3
"""nano_j2me - hand-built TrueType fixture generator.

Writes the fonts tests/test_ttf.c parses, or rejects, into
tests/fixtures/ttf/.  They are deliberately nasty: every glyph exists to make
one specific bug in src/gfx/nj_ttf.c visible.

    nj_fixture.ttf          the main face (format 4 cmap, short loca)
    nj_fixture_long.ttf     the same outlines with a long loca
    nj_fixture_cmap12.ttf   format 4 + a format 12 subtable that wins for 'B'
    nj_fixture_mac.ttf      nothing but a (1,0) format 0 subtable
    nj_fixture_symbol.ttf   nothing but a (3,0) format 4 subtable (0xF0xx)

    bad_glyf_offset.ttf     glyf table offset past the end of the file
    bad_numglyphs.ttf       maxp numGlyphs far past the end of loca
    bad_hmtx.ttf            hmtx truncated below numberOfHMetrics
    bad_cmap_segcount.ttf   cmap format 4 segCountX2 past the end
    bad_loca.ttf            loca[0] points past the end of glyf
    bad_magic.ttf           head magic number zeroed
    bad_sfnt_version.ttf    'OTTO' (CFF outlines: not a glyf font)
    bad_cmap_offset.ttf     cmap directory offset 0xFFFFFF00
    truncated.ttf           the main face cut to 60%

    manifest.txt            per-glyph expected metrics (see below)

Glyph inventory (gid, mapped character, what it is there to catch)

     0  -        .notdef: a plain box, so drawing it is impossible to miss
     1  space    zero-length glyf slice (the empty-glyph encoding)
     2  'o'      nested contours with OPPOSITE winding (a counter)
     3  'm'      two consecutive off-curve points (implied midpoint)
     4  'n'      a contour that starts AND ends off-curve (wrap-around)
     5  'c'      composite: words/bytes args, scale, x/y scale, 2x2
     6  'h'      horizontal edges exactly on the baseline and on a scanline
     7  's'      a spike 8 font units wide (0.125 px at 16 px)
     8  'p'      plain rectangle
     9  'x'      two overlapping SAME-winding contours (non-zero != even-odd)
    10  'z'      self-intersecting (retraced) contour
    11  'q'      a single wide quadratic (smooth antialiasing)
    12  'l'      a stem exactly half a pixel wide at 16 px
    13  U+00A0   zero contours but a glyph header (the other empty encoding)
    14  'O'      four-arc circle (the FreeType comparison's smooth case)

unitsPerEm is 1024, not 2048, and there are 15 glyphs but only 8 horizontal
metrics, so the trailing-LSB hmtx path is exercised too.  The format 4 cmap
uses idDelta for the single-character segments and a non-zero idRangeOffset
for U+2190..U+2193.

The output is deterministic - no timestamps, no locale, no filesystem order -
so re-running the generator produces byte-identical files.  `--check` verifies
that against what is already on disk.

Usage:
    python3 tools/gen_ttf_fixture.py [--outdir DIR] [--check]
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUTDIR = os.path.join(REPO_ROOT, "tests", "fixtures", "ttf")

UPEM = 1024
ASCENDER = 800
DESCENDER = -224
LINE_GAP = 0
TYPO_LINE_GAP = 200
NUM_HMETRICS = 8          # < number of glyphs, on purpose
FAMILY = "NJ2ME Fixture"

# Composite component flags (the subset the generator emits).
ARG_1_AND_2_ARE_WORDS = 0x0001
ARGS_ARE_XY_VALUES = 0x0002
ROUND_XY_TO_GRID = 0x0004
WE_HAVE_A_SCALE = 0x0008
MORE_COMPONENTS = 0x0020
WE_HAVE_AN_X_AND_Y_SCALE = 0x0040
WE_HAVE_A_TWO_BY_TWO = 0x0080


# --------------------------------------------------------------------------
# Byte helpers
# --------------------------------------------------------------------------
def u8(v):
    return struct.pack(">B", v & 0xFF)


def u16(v):
    return struct.pack(">H", v & 0xFFFF)


def s16(v):
    return struct.pack(">h", v)


def u32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def f2dot14(v):
    return s16(int(round(v * 16384.0)))


def tag_bytes(tag):
    return tag.encode("ascii")


# --------------------------------------------------------------------------
# Glyph definitions
#
# A simple glyph is a list of contours; a contour is a list of
# (x, y, on_curve) in font units, y up, baseline at y = 0.
#
# A composite glyph is a list of component dicts: gid, dx, dy, words, scale,
# xy_scale, matrix.  Offsets are unscaled (the MS default), which is also what
# FreeType does when neither SCALED_COMPONENT_OFFSET nor UNSCALED_COMPONENT_OFFSET
# is set.
# --------------------------------------------------------------------------
def rect(x0, y0, x1, y1, ccw=True):
    if ccw:
        return [(x0, y0, True), (x1, y0, True), (x1, y1, True), (x0, y1, True)]
    return [(x0, y0, True), (x0, y1, True), (x1, y1, True), (x1, y0, True)]


SIMPLE_GLYPHS = {
    0: [rect(64, 0, 448, 640)],                                  # .notdef
    1: [],                                                       # space (zero-length)
    2: [rect(64, 0, 576, 512), rect(160, 160, 416, 416, ccw=False)],
    3: [[(64, 64, True), (256, 640, False), (512, 640, False), (768, 64, True)]],
    4: [[(64, 512, False), (64, 64, True), (448, 64, True),
         (448, 512, True), (256, 700, False)]],
    6: [rect(64, 0, 576, 320)],
    7: [[(64, 64, True), (72, 64, True), (68, 600, True)]],
    8: [rect(64, 0, 576, 448)],
    9: [rect(64, 64, 320, 320), rect(192, 192, 448, 448)],
    10: [[(64, 64, True), (256, 64, True), (256, 256, True), (64, 256, True),
          (96, 96, True), (224, 96, True), (224, 224, True), (96, 224, True)]],
    11: [[(64, 64, True), (64, 704, False), (576, 64, True)]],
    12: [rect(64, 0, 96, 640)],
    13: [],                                                      # zero contours
    14: [[(448, 256, True), (448, 362, False), (362, 448, False), (256, 448, True),
          (150, 448, False), (64, 362, False), (64, 256, True),
          (64, 150, False), (150, 64, False), (256, 64, True),
          (362, 64, False), (448, 150, False)]],
}

COMPOSITE_GLYPHS = {
    5: [
        # A half-size 'h' rectangle 6 px up (word args, uniform scale).
        {"gid": 6, "dx": 0, "dy": 384, "words": True, "scale": 0.5},
        # The spike, byte args, x/y scale (identity but a separate code path).
        {"gid": 7, "dx": 96, "dy": 0, "words": False, "xy_scale": (1.0, 1.0)},
        # The ring 10 px to the right, through the full 2x2 path.
        {"gid": 2, "dx": 640, "dy": 0, "words": True, "matrix": (1.0, 0.0, 0.0, 1.0)},
    ],
}

# advance widths, in font units, for gids 0..NUM_HMETRICS-1.
ADVANCES = [640, 512, 640, 640, 640, 640, 640, 640]

# character -> gid for the format 4 cmap.  'B' is deliberately mapped
# differently in the cmap12 variant so the test can prove which subtable won.
CMAP4 = {
    0x20: 1,
    0x41: 0,     # 'A' -> .notdef: a cmap entry that must render as missing
    0x42: 12,    # 'B' -> stem, overridden by the format 12 subtable in the variant
    0x4F: 14,    # 'O'
    0x63: 5,     # 'c'
    0x68: 6,     # 'h'
    0x6C: 12,    # 'l'
    0x6D: 3,     # 'm'
    0x6E: 4,     # 'n'
    0x6F: 2,     # 'o'
    0x70: 8,     # 'p'
    0x71: 11,    # 'q'
    0x73: 7,     # 's'
    0x78: 9,     # 'x'
    0x7A: 10,    # 'z'
    0xA0: 13,    # nbsp
}

# Ranges served through a non-zero idRangeOffset.  The third entry is glyph 0,
# so U+2193 must render as missing even though the cmap claims the range.
CMAP4_RANGES = [
    (0x2190, [2, 3, 0, 2]),
]

# Character -> gid, used for the manifest and by the tests.
CHAR_OF_GID = {0: -1, 1: 0x20, 5: 0x63, 6: 0x68, 7: 0x73, 8: 0x70, 9: 0x78,
               10: 0x7A, 11: 0x71, 12: 0x6C, 13: 0xA0, 14: 0x4F}

# The format 12 groups in the cmap12 variant: 'B' -> gid 9 (where format 4
# says gid 12) and a non-BMP character.
CMAP12_GROUPS = [
    (0x42, 0x42, 9),
    (0x1F600, 0x1F600, 11),
]


# --------------------------------------------------------------------------
# Outline maths (only for the manifest's expected bounding boxes)
# --------------------------------------------------------------------------
def quad_point(p0, c, p1, t):
    mt = 1.0 - t
    return (mt * mt * p0[0] + 2.0 * mt * t * c[0] + t * t * p1[0],
            mt * mt * p0[1] + 2.0 * mt * t * c[1] + t * t * p1[1])


def contour_path(contour):
    """Turn a contour into a list of ('line'|'quad', points...) commands.

    Implements the TrueType rules the rasteriser has to implement: implied
    on-curve midpoints between consecutive off-curve points, and the
    wrap-around start point.
    """
    pts = list(contour)
    if not pts:
        return []
    if not pts[0][2]:
        if pts[-1][2]:
            # Start at the last on-curve point instead.
            pts = [pts[-1]] + pts[:-1]
        else:
            mid = ((pts[-1][0] + pts[0][0]) / 2.0, (pts[-1][1] + pts[0][1]) / 2.0)
            pts = [(mid[0], mid[1], True)] + pts

    cmds = []
    cur = (pts[0][0], pts[0][1])
    i = 1
    n = len(pts)
    while i <= n:
        p = pts[i % n]
        if p[2]:
            cmds.append(("line", cur, (p[0], p[1])))
            cur = (p[0], p[1])
            i += 1
        else:
            nxt = pts[(i + 1) % n]
            if nxt[2]:
                end = (nxt[0], nxt[1])
                cmds.append(("quad", cur, (p[0], p[1]), end))
                cur = end
                i += 2
            else:
                end = ((p[0] + nxt[0]) / 2.0, (p[1] + nxt[1]) / 2.0)
                cmds.append(("quad", cur, (p[0], p[1]), end))
                cur = end
                i += 1
    return cmds


def outline_bbox(contours, xform=(1.0, 0.0, 0.0, 1.0, 0.0, 0.0)):
    a, b, c, d, dx, dy = xform
    xs = []
    ys = []

    def emit(x, y):
        X = a * x + c * y + dx
        Y = b * x + d * y + dy
        xs.append(X)
        ys.append(Y)

    for contour in contours:
        for cmd in contour_path(contour):
            if cmd[0] == "line":
                emit(*cmd[1])
                emit(*cmd[2])
            else:
                _, p0, ctl, p1 = cmd
                for k in range(65):
                    emit(*quad_point(p0, ctl, p1, k / 64.0))
    if not xs:
        return None
    return (min(xs), min(ys), max(xs), max(ys))


def glyph_bbox(gid, xform=(1.0, 0.0, 0.0, 1.0, 0.0, 0.0), depth=0):
    """Exact-ish outline bbox of a glyph, in font units."""
    if gid in COMPOSITE_GLYPHS:
        boxes = []
        for comp in COMPOSITE_GLYPHS[gid]:
            a, b, c, d = comp_matrix(comp)
            cdx, cdy = comp_offset(comp, a, b, c, d)
            sub = glyph_bbox(comp["gid"], (a, b, c, d, cdx, cdy), depth + 1)
            if sub is None:
                continue
            # Compose with the outer transform.
            A, B, C, D, DX, DY = xform
            for (x0, y0, x1, y1) in [sub]:
                corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
                pts = [(A * x + C * y + DX, B * x + D * y + DY) for x, y in corners]
                boxes.append((min(p[0] for p in pts), min(p[1] for p in pts),
                              max(p[0] for p in pts), max(p[1] for p in pts)))
        if not boxes:
            return None
        a, b, c, d, dx, dy = xform
        out = []
        for (x0, y0, x1, y1) in boxes:
            corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
            pts = [(a * x + c * y + dx, b * x + d * y + dy) for x, y in corners]
            out.append((min(p[0] for p in pts), min(p[1] for p in pts),
                        max(p[0] for p in pts), max(p[1] for p in pts)))
        return (min(o[0] for o in out), min(o[1] for o in out),
                max(o[2] for o in out), max(o[3] for o in out))
    return outline_bbox(SIMPLE_GLYPHS.get(gid, []), xform)


def comp_matrix(comp):
    if "matrix" in comp:
        a, b, c, d = comp["matrix"]
    elif "xy_scale" in comp:
        a, d = comp["xy_scale"]
        b = c = 0.0
    elif "scale" in comp:
        a = d = comp["scale"]
        b = c = 0.0
    else:
        a, d = 1.0, 1.0
        b = c = 0.0
    return a, b, c, d


def comp_offset(comp, a, b, c, d):
    """Component offset, applying the component matrix iff the spec says so."""
    dx, dy = comp["dx"], comp["dy"]
    if comp.get("scaled_offset"):
        dx, dy = a * dx + c * dy, b * dx + d * dy
    return dx, dy


# --------------------------------------------------------------------------
# glyf packing
# --------------------------------------------------------------------------
def pack_simple(contours):
    pts = []
    ends = []
    for contour in contours:
        for (x, y, on) in contour:
            pts.append((x, y, on))
        ends.append(len(pts) - 1)
    box = outline_bbox(contours)
    if box is None:
        box = (0.0, 0.0, 0.0, 0.0)
    data = s16(len(contours))
    data += s16(int(box[0])) + s16(int(box[1])) + s16(int(box[2])) + s16(int(box[3]))
    for e in ends:
        data += u16(e)
    data += u16(0)                       # instructionLength

    flags = []
    xs = []
    ys = []
    prev = (0, 0)
    for (x, y, on) in pts:
        dx = x - prev[0]
        dy = y - prev[1]
        prev = (x, y)
        f = 0x01 if on else 0x00
        if dx == 0:
            f |= 0x10
            xs.append(b"")
        elif -255 <= dx <= 255:
            f |= 0x02
            if dx > 0:
                f |= 0x10
            xs.append(u8(abs(dx)))
        else:
            xs.append(s16(dx))
        if dy == 0:
            f |= 0x20
            ys.append(b"")
        elif -255 <= dy <= 255:
            f |= 0x04
            if dy > 0:
                f |= 0x20
            ys.append(u8(abs(dy)))
        else:
            ys.append(s16(dy))
        flags.append(f)

    # Run-length encode: a run of n identical flag bytes becomes the flag with
    # the repeat bit plus a count of n-1.
    encoded = bytearray()
    i = 0
    while i < len(flags):
        f = flags[i]
        run = 1
        while i + run < len(flags) and flags[i + run] == f and run < 256:
            run += 1
        if run > 1:
            encoded.append(f | 0x08)
            encoded.append(run - 1)
        else:
            encoded.append(f)
        i += run
    data += bytes(encoded)
    for chunk in xs:
        data += chunk
    for chunk in ys:
        data += chunk
    return data


def pack_composite(components):
    data = s16(-1)
    boxes = []
    for comp in components:
        sub = glyph_bbox(comp["gid"])
        if sub is not None:
            boxes.append(sub)
    if boxes:
        data += s16(int(min(b[0] for b in boxes))) + s16(int(min(b[1] for b in boxes)))
        data += s16(int(max(b[2] for b in boxes))) + s16(int(max(b[3] for b in boxes)))
    else:
        data += s16(0) * 4

    for i, comp in enumerate(components):
        flags = ARGS_ARE_XY_VALUES
        if i + 1 < len(components):
            flags |= MORE_COMPONENTS
        if comp.get("words", True):
            flags |= ARG_1_AND_2_ARE_WORDS
        if "matrix" in comp:
            flags |= WE_HAVE_A_TWO_BY_TWO
        elif "xy_scale" in comp:
            flags |= WE_HAVE_AN_X_AND_Y_SCALE
        elif "scale" in comp:
            flags |= WE_HAVE_A_SCALE
        data += u16(flags) + u16(comp["gid"])
        if flags & ARG_1_AND_2_ARE_WORDS:
            data += s16(comp["dx"]) + s16(comp["dy"])
        else:
            data += struct.pack(">bb", comp["dx"], comp["dy"])
        if "matrix" in comp:
            a, b, c, d = comp["matrix"]
            data += f2dot14(a) + f2dot14(b) + f2dot14(c) + f2dot14(d)
        elif "xy_scale" in comp:
            data += f2dot14(comp["xy_scale"][0]) + f2dot14(comp["xy_scale"][1])
        elif "scale" in comp:
            data += f2dot14(comp["scale"])
    return data


def build_glyf():
    """Return (glyf_bytes, list_of_(offset, length)) for gids 0..14."""
    out = bytearray()
    spans = []
    for gid in range(15):
        if gid in COMPOSITE_GLYPHS:
            data = pack_composite(COMPOSITE_GLYPHS[gid])
        elif gid == 1:
            data = b""                    # zero-length slice
        else:
            data = pack_simple(SIMPLE_GLYPHS.get(gid, []))
        if len(data) & 1:
            data += b"\0"                 # short loca addressing needs even offsets
        spans.append((len(out), len(data)))
        out += data
    return bytes(out), spans


# --------------------------------------------------------------------------
# Tables
# --------------------------------------------------------------------------
def build_loca(spans, long_format):
    offs = [s[0] for s in spans] + [spans[-1][0] + spans[-1][1]]
    if long_format:
        return b"".join(u32(o) for o in offs)
    return b"".join(u16(o // 2) for o in offs)


def build_head(long_format):
    # created and modified are LONGDATETIME (8 bytes each), not LONG.
    return (u32(0x00010000) + u32(0x00010000) + u32(0) + u32(0x5F0F3CF5) +
            u16(0x0003) + u16(UPEM) + u32(0) * 4 +
            s16(0) + s16(DESCENDER) + s16(1024) + s16(ASCENDER) +
            u16(0) + u16(8) + s16(2) + s16(1 if long_format else 0) + s16(0))


def build_maxp(num_glyphs):
    return (u32(0x00010000) + u16(num_glyphs) + u16(64) + u16(8) + u16(2) +
            u16(0) + u16(0) + u16(0) + u16(0) + u16(0) + u16(0) +
            u16(0) + u16(0) + u16(0) + u16(0))


def build_hhea(num_glyphs):
    widths = ADVANCES + [ADVANCES[-1]] * (num_glyphs - len(ADVANCES))
    return (u32(0x00010000) + s16(ASCENDER) + s16(DESCENDER) + s16(LINE_GAP) +
            u16(max(widths)) + s16(0) + s16(0) + s16(1024) +
            s16(1) + s16(0) + s16(0) + s16(0) * 4 +
            s16(0) + u16(NUM_HMETRICS))


def build_hmtx(num_glyphs):
    out = bytearray()
    for gid in range(NUM_HMETRICS):
        box = glyph_bbox(gid)
        lsb = int(box[0]) if box else 0
        out += u16(ADVANCES[gid]) + s16(lsb)
    for gid in range(NUM_HMETRICS, num_glyphs):
        box = glyph_bbox(gid)
        lsb = int(box[0]) if box else 0
        out += s16(lsb)
    return bytes(out)


def build_cmap4(mapping, ranges):
    """A format 4 subtable: idDelta segments plus one idRangeOffset range."""
    segs = []                              # (start, end, delta, gids or None)
    for ch in sorted(mapping):
        gid = mapping[ch]
        segs.append((ch, ch, (gid - ch) & 0xFFFF, None))
    for (start, gids) in ranges:
        segs.append((start, start + len(gids) - 1, 0, gids))
    segs.append((0xFFFF, 0xFFFF, 1, None))
    segs.sort(key=lambda s: s[1])

    seg_count = len(segs)
    # glyphIdArray entries, assigned in segment order.
    glyph_array = []
    range_start_index = {}
    for i, seg in enumerate(segs):
        if seg[3] is not None:
            range_start_index[i] = len(glyph_array)
            glyph_array.extend(seg[3])

    id_range_base = 16 + 6 * seg_count        # offset of idRangeOffset[0]
    glyph_array_base = 16 + 8 * seg_count
    id_range_off = []
    for i, seg in enumerate(segs):
        if seg[3] is None:
            id_range_off.append(0)
        else:
            # For character c, the address read is
            #   idRangeOffset[i] + 2*(c - start) + &idRangeOffset[i]
            # so the value has to point at this segment's first array entry.
            here = id_range_base + 2 * i
            there = glyph_array_base + 2 * range_start_index[i]
            id_range_off.append(there - here)

    seg2 = seg_count * 2
    search_range = 2 * (1 << (seg_count.bit_length() - 1))
    entry_selector = seg_count.bit_length() - 1
    range_shift = seg2 - search_range

    body = bytearray()
    body += u16(4) + u16(0)                # format, length (patched below)
    body += u16(0)                         # language
    body += u16(seg2) + u16(search_range) + u16(entry_selector) + u16(range_shift)
    for seg in segs:
        body += u16(seg[1])
    body += u16(0)                         # reservedPad
    for seg in segs:
        body += u16(seg[0])
    for seg in segs:
        body += u16(seg[2])
    for v in id_range_off:
        body += u16(v)
    for g in glyph_array:
        body += u16(g)
    body[2:4] = u16(len(body))
    return bytes(body)


def build_cmap12(groups):
    body = u16(12) + u16(0) + u32(0) + u32(0) + u32(len(groups))
    for (s, e, g) in groups:
        body += u32(s) + u32(e) + u32(g)
    body = body[:4] + u32(len(body)) + body[8:]
    return body


def build_cmap0(table):
    return u16(0) + u16(262) + u16(0) + bytes(table)


def build_cmap(subtables):
    """subtables: list of (platform, encoding, bytes)."""
    n = len(subtables)
    header = u16(0) + u16(n)
    offset = 4 + 8 * n
    records = b""
    body = b""
    for (plat, enc, data) in subtables:
        records += u16(plat) + u16(enc) + u32(offset + len(body))
        body += data
    return header + records + body


def build_name():
    # Macintosh records are single-byte (Latin-1), Windows records are
    # UTF-16BE: the reader has to handle both.
    family = FAMILY.encode("utf-16-be")
    sub = "Regular".encode("utf-16-be")
    full = (FAMILY + " Regular").encode("utf-16-be")
    m_family = FAMILY.encode("latin-1")
    m_sub = "Regular".encode("latin-1")
    m_full = (FAMILY + " Regular").encode("latin-1")
    records = [
        (1, 0, 0, 1, m_family),
        (1, 0, 0, 2, m_sub),
        (1, 0, 0, 4, m_full),
        (3, 1, 0x409, 1, family),
        (3, 1, 0x409, 2, sub),
        (3, 1, 0x409, 4, full),
    ]
    records.sort(key=lambda r: (r[0], r[1], r[2], r[3]))
    strings = b""
    recs = b""
    for (plat, enc, lang, nid, data) in records:
        recs += u16(plat) + u16(enc) + u16(lang) + u16(nid) + u16(len(data)) + u16(len(strings))
        strings += data
    return u16(0) + u16(len(records)) + u16(6 + 12 * len(records)) + recs + strings


def build_os2():
    panose = bytes(10)
    return (u16(0) + s16(640) + u16(400) + u16(5) + u16(0) +
            s16(650) + s16(600) + s16(0) + s16(650) + s16(600) +
            s16(0) + s16(75) + s16(350) + s16(50) + s16(0) +
            s16(0) + panose +
            u32(1) + u32(0) + u32(0) + u32(0) +
            b"NONE" + u16(0x0040) +
            u16(0x20) + u16(0x1F600 & 0xFFFF) +
            s16(ASCENDER) + s16(DESCENDER) + s16(TYPO_LINE_GAP) +
            u16(ASCENDER) + u16(-DESCENDER))


# --------------------------------------------------------------------------
# Assembly
# --------------------------------------------------------------------------
def checksum(data):
    if len(data) & 3:
        data = data + b"\0" * (4 - (len(data) & 3))
    total = 0
    for i in range(0, len(data), 4):
        total = (total + struct.unpack(">I", data[i:i + 4])[0]) & 0xFFFFFFFF
    return total


def assemble(tables):
    tags = sorted(tables)
    n = len(tags)
    # The directory offset for head has to be known before the checksum pass.
    offset = 12 + 16 * n
    entries = []
    body = bytearray()
    for tag in tags:
        data = tables[tag]
        entries.append((tag, offset + len(body), len(data)))
        body += data
        if len(body) & 3:
            body += b"\0" * (4 - (len(body) & 3))

    search_range = 16 * (1 << (n.bit_length() - 1))
    entry_selector = n.bit_length() - 1
    header = u32(0x00010000) + u16(n) + u16(search_range) + u16(entry_selector)
    header += u16(16 * n - search_range)

    for (tag, off, length) in entries:
        data = tables[tag]
        if tag == "head":
            # The head checksum is computed with checkSumAdjustment zeroed,
            # which it already is here.
            pass
        header += tag_bytes(tag) + u32(checksum(data)) + u32(off) + u32(length)

    out = bytearray(header + body)
    # checkSumAdjustment: 0xB1B0AFBA - checksum(whole file with the field zero).
    head_off = [o for (t, o, _) in entries if t == "head"][0]
    struct.pack_into(">I", out, head_off + 8, 0)
    adjust = (0xB1B0AFBA - checksum(bytes(out))) & 0xFFFFFFFF
    struct.pack_into(">I", out, head_off + 8, adjust)
    return bytes(out)


def make_font(long_format=False, cmap=None, sfnt_version=0x00010000,
              glyf_offset_delta=0, glyf_length=None, num_glyphs=None,
              hmtx_length=None, loca0=None, cmap_offset=None, magic=None,
              head_check=None):
    num = 15 if num_glyphs is None else num_glyphs
    glyf, spans = build_glyf()
    if hmtx_length is not None:
        hmtx = build_hmtx(15)[:hmtx_length]
    else:
        hmtx = build_hmtx(15)
    if cmap is None:
        cmap = build_cmap([(3, 1, build_cmap4(CMAP4, CMAP4_RANGES))])
    loca = build_loca(spans, long_format)
    if loca0 is not None:
        loca = (u32(loca0) if long_format else u16(loca0)) + loca[4 if long_format else 2:]
    tables = {
        "OS/2": build_os2(),
        "cmap": cmap,
        "glyf": glyf,
        "head": build_head(long_format),
        "hhea": build_hhea(15),
        "hmtx": hmtx,
        "loca": loca,
        "maxp": build_maxp(num),
        "name": build_name(),
    }
    data = bytearray(assemble(tables))
    if sfnt_version != 0x00010000:
        struct.pack_into(">I", data, 0, sfnt_version)
    if magic is not None:
        # head is the first table in the directory (tags sorted).
        n = struct.unpack_from(">H", data, 4)[0]
        for i in range(n):
            off = 12 + 16 * i
            if data[off:off + 4] == b"head":
                toff = struct.unpack_from(">I", data, off + 8)[0]
                struct.pack_into(">I", data, toff + 12, magic)
    if head_check is not None:
        # Patch an arbitrary offset inside head, for the checksum-adjustment
        # field and friends.
        n = struct.unpack_from(">H", data, 4)[0]
        for i in range(n):
            off = 12 + 16 * i
            if data[off:off + 4] == b"head":
                toff = struct.unpack_from(">I", data, off + 8)[0]
                struct.pack_into(">I", data, toff + head_check[0], head_check[1])
    if glyf_offset_delta or glyf_length is not None:
        n = struct.unpack_from(">H", data, 4)[0]
        for i in range(n):
            off = 12 + 16 * i
            if data[off:off + 4] == b"glyf":
                toff = struct.unpack_from(">I", data, off + 8)[0]
                if glyf_offset_delta:
                    struct.pack_into(">I", data, off + 8, toff + glyf_offset_delta)
                if glyf_length is not None:
                    struct.pack_into(">I", data, off + 12, glyf_length)
    if cmap_offset is not None:
        n = struct.unpack_from(">H", data, 4)[0]
        for i in range(n):
            off = 12 + 16 * i
            if data[off:off + 4] == b"cmap":
                struct.pack_into(">I", data, off + 8, cmap_offset)
    return bytes(data)


def cmap12_font():
    return make_font(cmap=build_cmap([
        (3, 1, build_cmap4(CMAP4, CMAP4_RANGES)),
        (3, 10, build_cmap12(CMAP12_GROUPS)),
    ]))


def mac_font():
    table = [0] * 256
    table[ord("A")] = 2
    table[ord("O")] = 14
    return make_font(cmap=build_cmap([(1, 0, build_cmap0(table))]))


def symbol_font():
    mapping = {0xF041: 0, 0xF04F: 14, 0xF06F: 2}
    return make_font(cmap=build_cmap([(3, 0, build_cmap4(mapping, []))]))


def corrupt_cmap_segcount(data):
    """segCountX2 of the first format 4 subtable -> 0xFFFE."""
    out = bytearray(data)
    n = struct.unpack_from(">H", out, 4)[0]
    for i in range(n):
        off = 12 + 16 * i
        if out[off:off + 4] != b"cmap":
            continue
        coff = struct.unpack_from(">I", out, off + 8)[0]
        nsub = struct.unpack_from(">H", out, coff + 2)[0]
        for j in range(nsub):
            rec = coff + 4 + 8 * j
            sub = coff + struct.unpack_from(">I", out, rec + 4)[0]
            if struct.unpack_from(">H", out, sub)[0] == 4:
                struct.pack_into(">H", out, sub + 6, 0xFFFE)
                return bytes(out)
    raise SystemExit("no format 4 subtable to corrupt")


def corrupt_loca(data):
    out = bytearray(data)
    n = struct.unpack_from(">H", out, 4)[0]
    for i in range(n):
        off = 12 + 16 * i
        if out[off:off + 4] == b"loca":
            loff = struct.unpack_from(">I", out, off + 8)[0]
            struct.pack_into(">H", out, loff, 0xFFFE)
            return bytes(out)
    raise SystemExit("no loca table")


def build_all():
    """Return {filename: bytes}."""
    files = {}
    base = make_font()
    files["nj_fixture.ttf"] = base
    files["nj_fixture_long.ttf"] = make_font(long_format=True)
    files["nj_fixture_cmap12.ttf"] = cmap12_font()
    files["nj_fixture_mac.ttf"] = mac_font()
    files["nj_fixture_symbol.ttf"] = symbol_font()

    files["bad_glyf_offset.ttf"] = make_font(glyf_offset_delta=0x40000000)
    files["bad_numglyphs.ttf"] = make_font(num_glyphs=0xFFF0)
    files["bad_hmtx.ttf"] = make_font(hmtx_length=4)
    files["bad_cmap_segcount.ttf"] = corrupt_cmap_segcount(base)
    files["bad_loca.ttf"] = corrupt_loca(base)
    files["bad_magic.ttf"] = make_font(magic=0)
    files["bad_sfnt_version.ttf"] = make_font(sfnt_version=0x4F54544F)
    files["bad_cmap_offset.ttf"] = make_font(cmap_offset=0xFFFFFF00)
    files["truncated.ttf"] = base[:int(len(base) * 0.6)]

    files["manifest.txt"] = build_manifest().encode("ascii")
    return files


def build_manifest():
    lines = ["# gid char advance xmin ymin xmax ymax contours components"]
    for gid in range(15):
        box = glyph_bbox(gid)
        if box is None:
            box = (0.0, 0.0, 0.0, 0.0)
        adv = ADVANCES[gid] if gid < len(ADVANCES) else ADVANCES[-1]
        ncont = len(SIMPLE_GLYPHS.get(gid, [])) if gid not in COMPOSITE_GLYPHS else 0
        ncomp = len(COMPOSITE_GLYPHS.get(gid, []))
        lines.append("%d %d %d %d %d %d %d %d %d" % (
            gid, CHAR_OF_GID.get(gid, -1), adv,
            int(box[0]), int(box[1]), int(box[2]), int(box[3]), ncont, ncomp))
    return "\n".join(lines) + "\n"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--outdir", default=DEFAULT_OUTDIR)
    ap.add_argument("--check", action="store_true",
                    help="compare against the files already on disk; do not write")
    args = ap.parse_args(argv)
    files = build_all()

    if args.check:
        bad = 0
        for name in sorted(files):
            path = os.path.join(args.outdir, name)
            if not os.path.isfile(path):
                print("MISSING %s" % path)
                bad += 1
                continue
            with open(path, "rb") as fh:
                if fh.read() != files[name]:
                    print("DIFFERS %s" % path)
                    bad += 1
        if bad:
            print("FAIL: %d file(s) not byte-identical to a fresh build" % bad)
            return 1
        print("OK: %d files byte-identical to a fresh build" % len(files))
        return 0

    os.makedirs(args.outdir, exist_ok=True)
    for name in sorted(files):
        path = os.path.join(args.outdir, name)
        with open(path, "wb") as fh:
            fh.write(files[name])
        print("%-26s %6d bytes  sha256=%s" % (
            name, len(files[name]),
            hashlib.sha256(files[name]).hexdigest()[:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
