#!/usr/bin/env python3
"""nano_j2me - CJK (GB2312) bitmap font table generator.

Renders GB2312 glyphs from Source Han Sans CN Regular / Noto Sans CJK.
The 12- and 16-pixel advances retain their existing em sizes (11 and 15).
Storage cells have extra vertical space (16 and 20 rows) so accented letters,
descenders and box rules keep the same baseline as ideographs. No glyph is
individually resized or vertically centred. The runtime uses the emitted
ascent for these cells independently of the Latin line metrics.

The checked-in tables are const 1bpp rows, MSB first, with stride
((width + 7) // 8) * height. Coverage is all 7445 GB2312 code points,
sorted for runtime binary search. A shared supersampling grid avoids integer
pixel jumps from target-size monochrome hinting, while retaining 1bpp storage.

The output is deterministic: same font file + same Pillow/FreeType build =
byte-identical header on every run.  Nothing here depends on the clock, the
locale, the filesystem order or dict iteration order.
"""

from __future__ import annotations

import argparse
import hashlib
import functools
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

# Source faces in preference order: Source Han Sans CN Regular (SIL OFL 1.1)
# first, then the Noto CJK fallback.
FONT_CANDIDATES = (
    "/usr/share/fonts/adobe-source-han-sans/SourceHanSansCN-Regular.otf",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
)

# GB2312 is a 94x94 grid: rows 0xA1..0xF7, cells 0xA1..0xFE.  Unassigned
# slots raise UnicodeDecodeError and are skipped.
GB_FIRST_ROW, GB_LAST_ROW = 0xA1, 0xF7
GB_FIRST_CELL, GB_LAST_CELL = 0xA1, 0xFE

# --------------------------------------------------------------------------
# Faces. `px` is the em size handed to FreeType. Storage height includes
# accents and descenders; advance width and em size stay independent of it.
# --------------------------------------------------------------------------
FACES = (
    {"tag": "12", "w": 12, "h": 16, "px": 11, "ascent": 12},
    {"tag": "16", "w": 16, "h": 20, "px": 15, "ascent": 16},
)

# All glyphs use the same finer grid and coverage threshold. A threshold below
# 50% retains thin strokes and dashed rules without target-size MONO hinting.
RASTER_SCALE = 4
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


@functools.lru_cache(maxsize=4)
def supersampled_font(font):
    return font.font_variant(size=font.size * RASTER_SCALE)


def raster(font, ch, px):
    """Rasterise on a shared finer grid, then threshold to runtime 1bpp.

    Target-size MONO hinting moves whole Han outlines by an integer pixel
    (e.g. 始/绍/关/出 at 11 px). Supersampling limits hinting to a fraction
    of a target pixel. Scale the entire canvas, including the pen origin;
    never translate or fit individual glyph bounding boxes.
    """
    side = canvas_size(px)
    img = Image.new("L", (side * RASTER_SCALE, side * RASTER_SCALE), 0)
    origin = PAD * RASTER_SCALE
    ImageDraw.Draw(img).text((origin, origin), ch,
                            font=supersampled_font(font), fill=255, anchor="ls")
    img = img.resize((side, side), Image.Resampling.LANCZOS)
    img = img.point(lambda value: 255 if value >= GRAY_THRESHOLD else 0)
    box = img.getbbox()
    mode = "gray"
    if box is None:
        # Hairline symbols such as U+2016 and U+FF3F can fall below the
        # threshold. Retain their hinted 1bpp strokes, without moving them.
        img = Image.new("1", (side, side), 0)
        ImageDraw.Draw(img).text((PAD, PAD), ch, font=font, fill=1, anchor="ls")
        box = img.getbbox()
        mode = "mono-fallback"
        if box is None:
            return None, None, "blank"
    load = img.load()
    pixels = [(x - PAD, y - PAD)
              for y in range(box[1], box[3])
              for x in range(box[0], box[2])
              if load[x, y]]
    return tuple(value - PAD for value in box), pixels, mode


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
def glyph_cell(font, ch, w, h, x_off, y_off, px):
    """Place every glyph at the face baseline; never resize an outlier."""
    box, pixels, mode = raster(font, ch, px)
    if box is None:
        return blank_rows(w, h), "blank"
    rows, dropped = blit(pixels, x_off, y_off, w, h)
    return rows, "clipped" if dropped else mode


def build_face(family_info, codepoints, spec, stats):
    """Rasterise one face; returns the packed table and its metrics."""
    w = spec["w"]
    h = spec["h"]
    base_px = spec["px"]
    font = ImageFont.truetype(family_info["path"], base_px)
    # Only horizontal placement is derived from the full coverage envelope.
    # Vertical placement is a font-wide baseline shared with Latin drawing.
    boxes = [raster(font, chr(cp), base_px)[0] for cp in codepoints]
    boxes = [box for box in boxes if box is not None]
    x_off = -min(box[0] for box in boxes)
    y_off = spec["ascent"]

    blob = bytearray()
    for cp in codepoints:
        rows, mode = glyph_cell(font, chr(cp), w, h, x_off, y_off, base_px)
        modes = stats["modes"]
        modes[mode] = modes.get(mode, 0) + 1
        if mode == "blank":
            stats["blank"].append(cp)
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
        "ascent": y_off,
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
    w(" * Source Han Sans CN glyphs: Copyright Adobe, SIL Open Font License 1.1.")
    w(" * See docs/licenses/source-han-sans-OFL.txt for attribution and licence.")
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
    w(" * Glyphs use a shared %dx greyscale grid, reduced with Lanczos and" % RASTER_SCALE)
    w(" * thresholded at %d/255 into the same 1bpp cells." % GRAY_THRESHOLD)
    w(" * Hairline symbols that fall below this threshold retain MONO strokes.")
    w(" * Every glyph uses the shared baseline above; no individual resizing")
    w(" * or vertical centring is applied (%d clipped)." % len(stats["clipped"]))
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
        w("/* ---------------- %d x %d ---------------- */" % (face["w"], face["h"]))
        w("#define NJ_CJK_%s_W        %d" % (tag, face["w"]))
        w("#define NJ_CJK_%s_H        %d" % (tag, face["h"]))
        w("#define NJ_CJK_%s_ASCENT   %d" % (tag, face["ascent"]))
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
        stat = {"modes": {}, "blank": [], "gray": [],
                "clipped": [], "empty": []}
        face = build_face(font_info, codepoints, spec, stat)
        faces.append(face)
        stats.append(stat)

    # One combined report: the header documents blank/gray glyphs of
    # both faces, and both faces share the same coverage.
    merged = {"modes": {}, "blank": [], "gray": [],
              "clipped": [], "empty": []}
    for stat in stats:
        for key, value in stat["modes"].items():
            merged["modes"][key] = merged["modes"].get(key, 0) + value
        merged["gray"] += stat["gray"]
        merged["clipped"] += stat["clipped"]
    merged["empty"] = sorted(set(stats[0]["empty"]) | set(stats[1]["empty"]))
    # The counts in the banner are sums over both faces, so record the
    # per-face split as well (the header quotes both).
    merged["gray_by_face"] = [len(stat["gray"]) for stat in stats]

    if merged["clipped"]:
        raise RuntimeError("shared font window clips glyphs: %s" % merged["clipped"])

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
            print("  clipped   : %d" % len(stat["clipped"]))
        print("header      : %s (%d bytes)" % (args.header, nbytes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
