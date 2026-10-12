#!/usr/bin/env python3
"""nano_j2me - CJK (GB2312) bitmap font table generator.

Renders the two cells the runtime's wide-glyph hook asks for - a 12x12 face
for SMALL/MEDIUM text and a 16x16 face for LARGE - from a system Source Han
Sans / Noto Sans CJK face and emits

    src/gfx/nj_font_cjk.h      the 1bpp glyph tables (checked in, const)

Glyph layout - the same frozen contract as nj_font_data.h:

    1 bit per pixel, MSB first, one byte per 8 horizontal pixels, rows packed
    with no padding at the end of a row:

        row_bytes    = (glyph_w + 7) / 8
        glyph stride = row_bytes * glyph_h
        glyph `cp`   = bitmap + index_of(cp) * stride

Coverage is the whole GB2312 charset: every byte pair 0xA1A1..0xF7FE that
Python's 'gb2312' codec decodes (7445 code points - both hanzi levels plus the
symbol rows: CJK punctuation, full-width forms, kana, Greek, Cyrillic, the
box-drawing rules and the pinyin row).  The few unassigned slots are skipped.

The table is sorted by code point and the index is therefore strictly
ascending, which is what lets the runtime binary-search it.

Rasterisation notes (they are what makes the 1bpp cells legible):

  * The glyphs come from FreeType's monochrome (FT_LOAD_TARGET_MONO)
    rasteriser at an em one pixel smaller than the cell (11 px for the 12x12
    face, 15 px for 16x16).  That is the size at which the measured ink
    envelope of the GB2312 hanzi - which is what sets the cell window - fits
    the cell exactly, so nothing is ever cut off.  A greyscale render plus a
    coverage threshold is a poor substitute here: at 11 px a Light stem
    covers ~40% of a pixel, so any sane threshold either deletes the stroke
    or smears the glyph.
  * The cell window is derived from the data, not hard-coded: the generator
    measures the ink envelope of the covered U+4E00..U+9FFF ideographs at the
    render size and centres that envelope in the cell.  Every glyph is placed
    through the same window, so the face's relative positions (a low comma, a
    centred ideograph, a full-width form) survive.
  * Unicode order is not GB2312 order: the GB2312 hanzi are sorted by pinyin
    while Unicode sorts them by radical/stroke, so the emitted index is a
    sorted permutation, not the decode order.
  * A handful of glyphs reach outside that window anyway (the full-width
    Latin/Greek/Cyrillic descenders and the box-drawing junctions, which are
    drawn to the edge of the em).  Those are re-rendered a pixel or two
    smaller and centred in the cell, so their ink stays complete instead of
    being clipped.
  * A few thin dashed box-drawing rules vanish entirely in monochrome mode;
    those (and only those) fall back to a greyscale render thresholded at
    GRAY_THRESHOLD.

The output is deterministic: same font file + same Pillow/FreeType build =
byte-identical header on every run.  Nothing here depends on the clock, the
locale, the filesystem order or dict iteration order.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - environment problem, not a code path
    sys.stderr.write(
        "error: Pillow is required to generate the fonts (pip install Pillow)\n")
    sys.exit(2)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER_PATH = os.path.join(REPO_ROOT, "src", "gfx", "nj_font_cjk.h")

# Source faces in preference order: Source Han Sans CN Light (SIL OFL 1.1)
# first, then the Noto CJK fallback.
FONT_CANDIDATES = (
    "/usr/share/fonts/adobe-source-han-sans/SourceHanSansCN-Light.otf",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
)

# GB2312 is a 94x94 grid: rows 0xA1..0xF7, cells 0xA1..0xFE.  Unassigned
# slots raise UnicodeDecodeError and are skipped.
GB_FIRST_ROW, GB_LAST_ROW = 0xA1, 0xF7
GB_FIRST_CELL, GB_LAST_CELL = 0xA1, 0xFE

# The ideographs whose ink envelope defines the cell window.
HANZI_FIRST, HANZI_LAST = 0x4E00, 0x9FFF

# --------------------------------------------------------------------------
# Faces.  `px` is the em size handed to FreeType; one pixel below the cell is
# the largest em whose hanzi envelope still fits (see the module docstring).
# --------------------------------------------------------------------------
FACES = (
    {"tag": "12", "w": 12, "h": 12, "px": 11},
    {"tag": "16", "w": 16, "h": 16, "px": 15},
)

# Smallest em a shrinking glyph may be re-rendered at before the generator
# gives up and clips it (which does not happen with the faces above).
MIN_PX = 6

# Greyscale fallback: pixel coverage (0..255) at or above which a pixel is
# inked.  Only used for glyphs whose monochrome rasterisation comes back
# empty although their outline has ink (the dashed box-drawing rules, whose
# 1 px dashes the monochrome rasteriser rounds away).
GRAY_THRESHOLD = 96

# Canvas padding around the pen origin: generous next to the ~1 em ink reach
# of a glyph, so nothing is ever clipped by the canvas itself.
PAD = 48


# --------------------------------------------------------------------------
# Source selection
# --------------------------------------------------------------------------
def pick_font(explicit=None):
    """Return the first candidate face Pillow can open, or exit with an error."""
    candidates = (explicit,) if explicit else FONT_CANDIDATES
    tried = []
    for path in candidates:
        if not os.path.isfile(path):
            tried.append("%s (not installed)" % path)
            continue
        try:
            # Opening is the only reliable probe: Pillow accepts a path, the
            # FreeType driver behind it is what rejects an unusable face.
            ImageFont.truetype(path, 16)
        except OSError as exc:
            tried.append("%s (%s)" % (path, exc))
            continue
        return path
    sys.stderr.write("error: no usable CJK font found.\n       looked for:\n")
    for path in candidates:
        sys.stderr.write("         %s\n" % path)
    for line in tried:
        sys.stderr.write("         rejected: %s\n" % line)
    sys.stderr.write(
        "       install one (e.g. apt install fonts-adobe-source-han-sans-cn)\n"
        "       or pass --font FILE.\n")
    sys.exit(2)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------------
# Coverage
# --------------------------------------------------------------------------
def gb2312_codepoints():
    """Every GB2312 code point, sorted ascending.

    Decoding the byte pairs (rather than listing code points) is what makes
    the coverage exactly the charset: a slot the standard leaves unassigned
    raises UnicodeDecodeError and drops out.
    """
    codepoints = set()
    for hi in range(GB_FIRST_ROW, GB_LAST_ROW + 1):
        for lo in range(GB_FIRST_CELL, GB_LAST_CELL + 1):
            try:
                ch = bytes((hi, lo)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            codepoints.add(ord(ch))
    return sorted(codepoints)


# --------------------------------------------------------------------------
# Rasterisation
# --------------------------------------------------------------------------
def canvas_size(px):
    return 2 * PAD + px


def mono_raster(font, ch, px):
    """Rasterise `ch` with FreeType's monochrome rasteriser.

    Returns (box, pixels), both in pen coordinates (the pen is the baseline
    origin, +y downwards): `box` is (left, top, right, bottom) with right and
    bottom exclusive, `pixels` the inked (x, y) points.  (None, None) means
    the rasteriser produced no ink at all.

    Drawing into a mode "1" image is what makes PIL ask FreeType for
    FT_LOAD_TARGET_MONO: hinted, grid-fitted, 1 bit per pixel - exactly the
    rasterisation the panels want.
    """
    side = canvas_size(px)
    img = Image.new("1", (side, side), 0)
    ImageDraw.Draw(img).text((PAD, PAD), ch, font=font, fill=1, anchor="ls")
    box = img.getbbox()
    if box is None:
        return None, None
    load = img.load()
    pixels = [(x - PAD, y - PAD)
              for y in range(box[1], box[3])
              for x in range(box[0], box[2])
              if load[x, y]]
    return (box[0] - PAD, box[1] - PAD, box[2] - PAD, box[3] - PAD), pixels


def gray_raster(font, ch, px, threshold):
    """Greyscale render of `ch`, thresholded at `threshold`/255 coverage.

    Same coordinate convention as mono_raster(); (None, None) when nothing
    reaches the threshold.
    """
    side = canvas_size(px)
    img = Image.new("L", (side, side), 0)
    ImageDraw.Draw(img).text((PAD, PAD), ch, font=font, fill=255, anchor="ls")
    load = img.load()
    pixels = []
    left = top = 1 << 30
    right = bottom = -(1 << 30)
    for y in range(side):
        for x in range(side):
            if load[x, y] >= threshold:
                pixels.append((x - PAD, y - PAD))
                left = min(left, x - PAD)
                top = min(top, y - PAD)
                right = max(right, x - PAD + 1)
                bottom = max(bottom, y - PAD + 1)
    if not pixels:
        return None, None
    return (left, top, right, bottom), pixels


def raster(font, ch, px):
    """Rasterise one glyph, preferring monochrome.

    Returns (box, pixels, mode) with mode in {"mono", "gray", "blank"}.
    """
    box, pixels = mono_raster(font, ch, px)
    if box is not None:
        return box, pixels, "mono"
    outline = font.getbbox(ch, anchor="ls")
    if outline is None or outline[2] <= outline[0] or outline[3] <= outline[1]:
        # No outline ink at all: a space-like glyph, blank at every size.
        return None, None, "blank"
    box, pixels = gray_raster(font, ch, px, GRAY_THRESHOLD)
    if box is None:
        return None, None, "blank"
    return box, pixels, "gray"


def blit(pixels, x_off, y_off, w, h):
    """Place `pixels` into a w x h cell at (x_off, y_off), clipping if needed.

    Returns (rows, dropped): rows is the h x w 0/1 bitmap, dropped the number
    of inked pixels that fell outside the cell (0 for every glyph the face
    sizes actually produce).
    """
    rows = [[0] * w for _ in range(h)]
    dropped = 0
    for x, y in pixels:
        cx = x + x_off
        cy = y + y_off
        if 0 <= cx < w and 0 <= cy < h:
            rows[cy][cx] = 1
        else:
            dropped += 1
    return rows, dropped


def blank_rows(w, h):
    return [[0] * w for _ in range(h)]


def pack_glyph(rows, w):
    """Pack one cell's rows MSB-first, row_bytes = (w + 7) / 8."""
    row_bytes = (w + 7) // 8
    out = bytearray()
    for row in rows:
        for byte_index in range(row_bytes):
            value = 0
            for bit in range(8):
                x = byte_index * 8 + bit
                if x < w and row[x]:
                    value |= 0x80 >> bit
            out.append(value)
    return bytes(out)


# --------------------------------------------------------------------------
# Cell window + one face
# --------------------------------------------------------------------------
def hanzi_window(font, codepoints, px, w, h):
    """Cell offset that centres the hanzi ink envelope in the cell.

    The window is measured, never guessed: the envelope of the covered
    U+4E00..U+9FFF glyphs at the render size is what the cell must contain,
    and every glyph goes through the same offset so relative positions are
    preserved.
    """
    left = top = 1 << 30
    right = bottom = -(1 << 30)
    for cp in codepoints:
        if not (HANZI_FIRST <= cp <= HANZI_LAST):
            continue
        box, _pixels = mono_raster(font, chr(cp), px)
        if box is None:
            continue
        left = min(left, box[0])
        top = min(top, box[1])
        right = max(right, box[2])
        bottom = max(bottom, box[3])
    if left > right or top > bottom:
        raise RuntimeError("no hanzi ink found - wrong font?")
    span_w = right - left
    span_h = bottom - top
    if span_w > w or span_h > h:
        raise RuntimeError(
            "hanzi ink envelope %dx%d does not fit the %dx%d cell - render "
            "at a smaller em" % (span_w, span_h, w, h))
    return (w - span_w) // 2 - left, (h - span_h) // 2 - top


def fits(box, x_off, y_off, w, h):
    return (box[0] + x_off >= 0 and box[1] + y_off >= 0 and
            box[2] + x_off <= w and box[3] + y_off <= h)


def glyph_cell(fonts, ch, w, h, x_off, y_off, base_px):
    """One glyph as (rows, mode).

    mode is "mono"/"gray" for the common case, "shrunk" when the glyph had
    to be re-rendered smaller and centred (its ink would not fit the shared
    window), "blank" for a space-like code point, and "clipped" if even the
    smallest render did not fit (a hard failure the caller reports).
    """
    box, pixels, mode = raster(fonts[base_px], ch, base_px)
    if box is None:
        return blank_rows(w, h), "blank"
    if fits(box, x_off, y_off, w, h):
        rows, _dropped = blit(pixels, x_off, y_off, w, h)
        return rows, mode

    # Does not fit the shared window: shrink until it does and centre the
    # ink in the cell, which keeps the whole glyph instead of clipping its
    # descender.  This is a small minority (the full-width Latin/Greek/
    # Cyrillic descenders and a few accented letters).
    for px in range(base_px - 1, MIN_PX - 1, -1):
        box, pixels, mode = raster(fonts[px], ch, px)
        if box is None:
            break
        if box[2] - box[0] <= w and box[3] - box[1] <= h:
            cx = (w - (box[2] - box[0])) // 2 - box[0]
            cy = (h - (box[3] - box[1])) // 2 - box[1]
            rows, _dropped = blit(pixels, cx, cy, w, h)
            return rows, "shrunk"
    rows, _dropped = blit(pixels, x_off, y_off, w, h)
    return rows, "clipped"


def build_face(family_info, codepoints, spec, stats):
    """Rasterise one face; returns the packed table and its metrics."""
    w = spec["w"]
    h = spec["h"]
    base_px = spec["px"]
    fonts = {}
    for px in range(base_px, MIN_PX - 1, -1):
        fonts[px] = ImageFont.truetype(family_info["path"], px)

    x_off, y_off = hanzi_window(fonts[base_px], codepoints, base_px, w, h)

    blob = bytearray()
    for cp in codepoints:
        rows, mode = glyph_cell(fonts, chr(cp), w, h, x_off, y_off, base_px)
        modes = stats["modes"]
        modes[mode] = modes.get(mode, 0) + 1
        if mode == "blank":
            stats["blank"].append(cp)
        elif mode == "shrunk":
            stats["shrunk"].append(cp)
        elif mode == "gray":
            stats["gray"].append(cp)
        elif mode == "clipped":
            stats["clipped"].append(cp)
        if not any(any(row) for row in rows):
            stats["empty"].append(cp)
        blob += pack_glyph(rows, w)

    return {
        "tag": spec["tag"],
        "w": w,
        "h": h,
        "px": base_px,
        "x_off": x_off,
        "y_off": y_off,
        "row_bytes": (w + 7) // 8,
        "stride": ((w + 7) // 8) * h,
        "table": bytes(blob),
    }


# --------------------------------------------------------------------------
# Header emission
# --------------------------------------------------------------------------
def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append(indent + " ".join("0x%02X," % b for b in chunk))
    return "\n".join(lines)


def c_index(codepoints):
    lines = []
    for i in range(0, len(codepoints), 8):
        chunk = codepoints[i:i + 8]
        lines.append("    " + " ".join("0x%04Xu," % cp for cp in chunk))
    return "\n".join(lines)


def emit_header(font_info, codepoints, faces, stats, path):
    count = len(codepoints)
    face12 = faces[0]
    face16 = faces[1]
    out = []
    w = out.append
    w("/*")
    w(" * nano_j2me - nj_font_cjk.h  --  GENERATED FILE, DO NOT EDIT.")
    w(" *")
    w(" * Produced by tools/gen_cjk_font.py from:")
    w(" *   %s" % font_info["path"])
    w(" *   sha256 %s" % font_info["sha256"])
    w(" *")
    w(" * Coverage: the GB2312 charset - every byte pair 0xA1A1..0xF7FE that")
    w(" * Python's 'gb2312' codec decodes, both levels plus the symbol rows")
    w(" * (CJK punctuation and full-width forms, kana, Greek, Cyrillic, the")
    w(" * pinyin row, the box-drawing rules).  %d code points; the few" % count)
    w(" * unassigned slots are not in the table.")
    w(" *")
    w(" * The index is sorted by code point and therefore strictly ascending:")
    w(" * the runtime binary-searches it and the bitmap offset of a code point")
    w(" * is its index times the stride.")
    w(" *")
    w(" * Glyph layout (frozen contract, same as nj_font_data.h):")
    w(" *   1 bit per pixel, MSB first, one byte per 8 horizontal pixels,")
    w(" *   rows packed with NO padding:")
    w(" *       row_bytes = (glyph_w + 7) / 8")
    w(" *       stride    = row_bytes * glyph_h")
    w(" *       glyph cell = bitmap + index_of(code_point) * stride")
    w(" *")
    for face in faces:
        w(" * %sx%s face: %d x %d cells, %d byte(s)/row, stride %d, "
          "%d glyphs, %d bytes;"
          % (face["tag"], face["tag"], face["w"], face["h"],
             face["row_bytes"], face["stride"], count, len(face["table"])))
        w(" *   rendered at a %d px em with the shared cell window offset "
          "(%d, %d)." % (face["px"], face["x_off"], face["y_off"]))
    w(" * Glyphs come from FreeType's monochrome (FT_LOAD_TARGET_MONO)")
    w(" * rasteriser, so strokes are hinted onto the pixel grid; the cell")
    w(" * window is the measured ink envelope of the covered ideographs.")
    w(" * %d glyph cell(s) whose ink reaches past that window are re-rendered"
      % len(stats["shrunk"]))
    w(" * smaller and centred (%s), never clipped (%d clipped);"
      % (", ".join("%dx%s: %d" % (face["w"], face["h"], n)
                   for face, n in zip(faces, stats["shrunk_by_face"])),
         len(stats["clipped"])))
    w(" * %d thin dashed rule cell(s) that monochrome mode drops (%s) are"
      % (len(stats["gray"]),
         ", ".join("%dx%s: %d" % (face["w"], face["h"], n)
                   for face, n in zip(faces, stats["gray_by_face"]))))
    w(" * rendered greyscale and thresholded at %d/255." % GRAY_THRESHOLD)
    w(" * %d glyph(s) are blank: %s."
      % (len(stats["empty"]),
         ", ".join("U+%04X" % cp for cp in stats["empty"]) if stats["empty"]
         else "none"))
    w(" *")
    w(" * Regenerate with:   python3 tools/gen_cjk_font.py")
    w(" * Deterministic: the same face and the same Pillow/FreeType build")
    w(" * produce a byte-identical header - nothing depends on the clock,")
    w(" * the locale, the filesystem order or dict iteration order.")
    w(" */")
    w("#ifndef NJ_FONT_CJK_H")
    w("#define NJ_FONT_CJK_H")
    w("")
    w('#include "nj2me/nj_types.h"')
    w("")
    w("/* This header defines file-scope objects: include it from nj_font.c only. */")
    w("")
    for face in faces:
        tag = face["tag"]
        w("/* ---------------- %s x %s ---------------- */" % (tag, tag))
        w("#define NJ_CJK_%s_W        %d" % (tag, face["w"]))
        w("#define NJ_CJK_%s_H        %d" % (tag, face["h"]))
        w("#define NJ_CJK_%s_ROWBYTES %d" % (tag, face["row_bytes"]))
        w("#define NJ_CJK_%s_STRIDE   %d" % (tag, face["stride"]))
        w("#define NJ_CJK_%s_COUNT    %du" % (tag, count))
        w("")
        w("/* Code points, strictly ascending (binary-search key). */")
        w("static const nj_u2 nj_cjk_%s_index[NJ_CJK_%s_COUNT] = {"
          % (tag, tag))
        w(c_index(codepoints))
        w("};")
        w("")
        w("/* %d glyphs x %d bytes = %d bytes, %d byte(s) per row, stride %d. */"
          % (count, face["stride"], len(face["table"]), face["row_bytes"],
             face["stride"]))
        w("static const nj_u1 nj_cjk_%s_bitmap[NJ_CJK_%s_COUNT * NJ_CJK_%s_STRIDE] = {"
          % (tag, tag, tag))
        stride = face["stride"]
        table = face["table"]
        for index, cp in enumerate(codepoints):
            w("    /* U+%04X */" % cp)
            w(c_bytes(table[index * stride:(index + 1) * stride]))
        w("};")
        w("")
    w("#endif /* NJ_FONT_CJK_H */")
    w("")

    text = "\n".join(out)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    return len(text.encode("utf-8"))


# --------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--font", help="CJK font file to use")
    ap.add_argument("--header", default=HEADER_PATH,
                    help="output header path (default: %(default)s)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    path = pick_font(args.font)
    font_info = {"path": path, "sha256": sha256_file(path)}

    codepoints = gb2312_codepoints()
    if not args.quiet:
        print("font source : %s" % path)
        print("  sha256    : %s" % font_info["sha256"])
        print("coverage    : %d GB2312 code points, U+%04X..U+%04X"
              % (len(codepoints), codepoints[0], codepoints[-1]))

    faces = []
    stats = []
    for spec in FACES:
        stat = {"modes": {}, "blank": [], "shrunk": [], "gray": [],
                "clipped": [], "empty": []}
        face = build_face(font_info, codepoints, spec, stat)
        faces.append(face)
        stats.append(stat)

    # One combined report: the header documents blank/shrunk/gray glyphs of
    # both faces, and both faces share the same coverage.
    merged = {"modes": {}, "blank": [], "shrunk": [], "gray": [],
              "clipped": [], "empty": []}
    for stat in stats:
        for key, value in stat["modes"].items():
            merged["modes"][key] = merged["modes"].get(key, 0) + value
        merged["shrunk"] += stat["shrunk"]
        merged["gray"] += stat["gray"]
        merged["clipped"] += stat["clipped"]
    merged["empty"] = sorted(set(stats[0]["empty"]) | set(stats[1]["empty"]))
    # The counts in the banner are sums over both faces, so record the
    # per-face split as well (the header quotes both).
    merged["shrunk_by_face"] = [len(stat["shrunk"]) for stat in stats]
    merged["gray_by_face"] = [len(stat["gray"]) for stat in stats]

    nbytes = emit_header(font_info, codepoints, faces, merged, args.header)
    if not args.quiet:
        for face, stat in zip(faces, stats):
            print("%s face      : cell %dx%d, em %d px, window (%d, %d), "
                  "%d row byte(s), stride %d"
                  % (face["tag"], face["w"], face["h"], face["px"],
                     face["x_off"], face["y_off"], face["row_bytes"],
                     face["stride"]))
            print("  rasterised: %d glyphs, %d bytes"
                  % (len(codepoints), len(face["table"])))
            print("  modes     : %s"
                  % ", ".join("%s=%d" % kv for kv in sorted(stat["modes"].items())))
            print("  blanks    : %d %s"
                  % (len(stat["empty"]),
                     ["U+%04X" % cp for cp in stat["empty"]] or ""))
            print("  shrunk    : %d" % len(stat["shrunk"]))
            print("  clipped   : %d" % len(stat["clipped"]))
        print("header      : %s (%d bytes)" % (args.header, nbytes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
