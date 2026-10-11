#!/usr/bin/env python3
"""nano_j2me - bitmap font table generator.

Renders the three javax.microedition.lcdui.Font sizes (SMALL / MEDIUM / LARGE,
each PLAIN and BOLD) from a system monospace TrueType face and emits

    src/gfx/nj_font_data.h      the 1bpp glyph tables (checked in, const)
    tests/out/font_specimen.png a human-readable specimen sheet

Glyph layout - exactly what nj_gfx.h documents and what nj_font.c/nj_gfx.c read:

    1 bit per pixel, MSB first, one byte per 8 horizontal pixels, rows packed
    with no padding at the end of a row:

        row_bytes    = (glyph_w + 7) / 8
        glyph stride = row_bytes * glyph_h
        glyph `ch`   = bitmap + (ch - FIRST) * row_bytes * glyph_h

Coverage is ASCII 0x20..0x7E plus Latin-1 0xA0..0xFF.  The control gap
0x7F..0x9F is present in the table but renders blank, so a lookup is always a
plain array index with no special cases.

The output is deterministic: same font files + same Pillow/FreeType build =
byte-identical header on every run.  Nothing here depends on the clock, the
filesystem order or the locale.
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
HEADER_PATH = os.path.join(REPO_ROOT, "src", "gfx", "nj_font_data.h")
SPECIMEN_PATH = os.path.join(REPO_ROOT, "tests", "out", "font_specimen.png")

# --------------------------------------------------------------------------
# Source faces.  First family whose regular *and* bold file exist wins.
# --------------------------------------------------------------------------
FONT_FAMILIES = (
    ("Liberation Mono", (
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    ), (
        "/usr/share/fonts/liberation/LiberationMono-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
    )),
    ("DejaVu Sans Mono", (
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    ), (
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono-Bold.ttf",
    )),
    ("Noto Sans Mono", (
        "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
    ), (
        "/usr/share/fonts/noto/NotoSansMono-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMono-Bold.ttf",
    )),
)

FIRST_CHAR = 0x20
LAST_CHAR = 0xFF
CHAR_COUNT = LAST_CHAR - FIRST_CHAR + 1

# Coverage of the rendered table: the two UTF-16 ranges a European game uses.
COVERAGE = tuple(range(0x20, 0x7F)) + tuple(range(0xA0, 0x100))

# Pixel size / cell width per MIDP size.  Chosen by measuring the ink envelope
# of every covered glyph (both styles) at each candidate size; see the module
# docstring of tools/gen_fonts.py in the repo history and the checks below.
#
#   MIDP size            px   cell      measured mono ink envelope
#   SMALL  (8)            9   6 x 12    ascent 10, descent 2, ink width 6
#   MEDIUM (0)           13   8 x 16    ascent 12, descent 4, ink width 8
#   LARGE  (16)          18  12 x 20    ascent 16, descent 4, ink width 11
#
# ascent/descent are not the face's hhea metrics: they are where the ink
# actually is, so the accented capitals of Latin-1 (which reach a couple of
# pixels above the cap line) and the deepest descenders both fit inside the
# cell and nothing is ever clipped.
SIZES = (
    {"tag": "small", "macro": "SMALL", "size": 8, "px": 9, "cell_w": 6},
    {"tag": "medium", "macro": "MEDIUM", "size": 0, "px": 13, "cell_w": 8},
    {"tag": "large", "macro": "LARGE", "size": 16, "px": 18, "cell_w": 12},
)

STYLES = (("p", "plain", 0), ("b", "bold", 1))

# Rendering canvas padding around the pen origin.
PAD = 48


# --------------------------------------------------------------------------
# Source selection
# --------------------------------------------------------------------------
def pick_font_files(explicit_regular=None, explicit_bold=None):
    """Return (family, regular_path, bold_path) or exit with a clear error."""
    if explicit_regular or explicit_bold:
        if not (explicit_regular and explicit_bold):
            sys.stderr.write(
                "error: --regular and --bold must be given together\n")
            sys.exit(2)
        for path in (explicit_regular, explicit_bold):
            if not os.path.isfile(path):
                sys.stderr.write("error: font file not found: %s\n" % path)
                sys.exit(2)
        return ("explicit", explicit_regular, explicit_bold)

    tried = []
    for family, regulars, bolds in FONT_FAMILIES:
        for reg in regulars:
            if not os.path.isfile(reg):
                continue
            for bold in bolds:
                if os.path.isfile(bold):
                    return (family, reg, bold)
            tried.append("%s (no bold companion for %s)" % (family, reg))
    sys.stderr.write(
        "error: no usable monospace TrueType font found.\n"
        "       looked for:\n")
    for family, regulars, bolds in FONT_FAMILIES:
        sys.stderr.write("         %-18s %s\n" % (family, regulars[0]))
    for line in tried:
        sys.stderr.write("         rejected: %s\n" % line)
    sys.stderr.write(
        "       install one (e.g. apt install fonts-liberation) or pass\n"
        "       --regular FILE --bold FILE.\n")
    sys.exit(2)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------------
# Rasterisation
# --------------------------------------------------------------------------
def render_glyph(font, ch):
    """Rasterise one character with the baseline at y = PAD.

    The canvas is bilevel, which makes ImageDraw ask FreeType for a
    FT_LOAD_TARGET_MONO bitmap: the hinted, 1 bit per pixel rasterisation the
    panel actually wants.  A greyscale render plus a coverage threshold does
    NOT work here - at 9..13 px a hinted-away stem covers two columns at ~50%
    each and a 128 threshold deletes it outright ('1' and 'l' lose their stem,
    the accented capitals lose the accent).
    """
    canvas = Image.new("1", (PAD * 2 + 96, PAD * 2 + 96), 0)
    ImageDraw.Draw(canvas).text((PAD, PAD), ch, font=font, fill=1,
                                anchor="ls")
    return canvas


def inked_bbox(canvas):
    """Bounding box of the inked pixels, in canvas coordinates (baseline PAD)."""
    return canvas.getbbox()


def measure_envelope(font):
    """Ink envelope of the whole coverage set, in pixels from the pen origin.

    Returns (ascent, descent, left, right): the tallest reach above the
    baseline, the deepest reach below it, and the leftmost/rightmost ink
    column relative to the pen position with the baseline at y = PAD.
    """
    top = 0
    bottom = 0
    left = 0
    right = 0
    for cp in COVERAGE:
        box = inked_bbox(render_glyph(font, chr(cp)))
        if box is None:
            continue
        top = max(top, PAD - box[1])
        bottom = max(bottom, box[3] - PAD)
        left = min(left, box[0] - PAD)
        right = max(right, box[2] - PAD)
    return top, bottom, left, right


def glyph_bitmap(font, ch, ascent, descent, cell_w):
    """One glyph as `ascent + descent` rows of `cell_w` 0/1 ints.

    The widest glyphs of the face (Y, _, Y-diaeresis, yen, macron and the
    vulgar fractions) are designed with a negative left side bearing so that
    they fill the em box; hinted at 9..18 px that puts their first column one
    pixel left of the pen origin.  Such a glyph is nudged right by the minimum
    amount that brings it inside the cell - which also centres it better on the
    advance box - and is never clipped.  Ordinary glyphs start one pixel in
    from the origin and are left exactly where the face puts them, so the
    fixed pitch of the face is preserved.

    Raises RuntimeError when the ink still leaves the cell: that means the
    metrics in SIZES are wrong and the table would silently clip glyphs.
    """
    height = ascent + descent
    canvas = render_glyph(font, ch)
    box = inked_bbox(canvas)
    rows = [[0] * cell_w for _ in range(height)]
    if box is None:
        return rows
    x_off = max(0, PAD - box[0])
    x0 = box[0] - PAD + x_off
    x1 = box[2] - PAD + x_off
    if x0 < 0 or x1 > cell_w \
            or (PAD - box[1]) > ascent or (box[3] - PAD) > descent:
        raise RuntimeError(
            "glyph U+%04X does not fit the %dx%d cell (ink x[%d,%d) "
            "y[%d,%d) vs ascent %d descent %d)" % (
                ord(ch), cell_w, height, x0, x1,
                PAD - box[1], box[3] - PAD, ascent, descent))
    pixels = canvas.load()
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            if pixels[x, y]:
                rows[y - PAD + ascent][x - PAD + x_off] = 1
    return rows


def pack_glyph(rows, cell_w):
    """Pack one glyph's rows MSB-first, row_bytes = (cell_w + 7) / 8."""
    row_bytes = (cell_w + 7) // 8
    out = bytearray()
    for row in rows:
        for byte_index in range(row_bytes):
            value = 0
            for bit in range(8):
                x = byte_index * 8 + bit
                if x < cell_w and row[x]:
                    value |= 0x80 >> bit
            out.append(value)
    return bytes(out)


# --------------------------------------------------------------------------
# Consistency checks
# --------------------------------------------------------------------------
# Glyphs that must keep ink at every size/style: the thin-stroke canaries,
# plus enough of the alphabet to catch a badly placed baseline.
NEVER_BLANK = ("!", "|", "l", "1", "_", "M", "W", "i", "g", "@", "#", "%", "j", "t")
BLANK_OK = (0x20, 0xA0, 0xAD)   # space, no-break space, soft hyphen


def check_glyphs(tag, style, glyphs):
    for ch in NEVER_BLANK:
        if not any(any(row) for row in glyphs[ord(ch)]):
            raise RuntimeError(
                "%s/%s: glyph %r rasterised blank - the size is too small or "
                "the mono rasteriser dropped a stem" % (tag, style, ch))
    # '|' is a bare vertical stem: every inked row must be the same 1..2 px
    # wide run.  Anything else means the rasteriser is splitting a stroke
    # across two columns (which is what makes a stem disappear at 1bpp).
    stem = {sum(row) for row in glyphs[ord("|")] if any(row)}
    if len(stem) != 1 or not stem <= {1, 2, 3}:
        raise RuntimeError(
            "%s/%s: '|' stroke widths are %s px, expected one uniform 1..3 px "
            "run" % (tag, style, sorted(stem)))
    for cp in BLANK_OK:
        if any(any(row) for row in glyphs[cp]):
            raise RuntimeError(
                "%s/%s: U+%04X should be blank" % (tag, style, cp))
    blank = [cp for cp in COVERAGE
             if cp not in BLANK_OK and not any(any(r) for r in glyphs[cp])]
    if blank:
        raise RuntimeError("%s/%s: unexpected blank glyphs %s" %
                           (tag, style, [hex(c) for c in blank]))


# --------------------------------------------------------------------------
# Header emission
# --------------------------------------------------------------------------
def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append(indent + " ".join("0x%02X," % b for b in chunk))
    return "\n".join(lines)


def build_size(family, paths, spec):
    """Render both styles of one size; returns a dict with tables + metrics."""
    px = spec["px"]
    cell_w = spec["cell_w"]
    faces = {}
    envelopes = {}
    for suffix, style, _bit in STYLES:
        font = ImageFont.truetype(paths[suffix], px)
        faces[suffix] = font
        envelopes[suffix] = measure_envelope(font)

    ascent = max(envelopes[s][0] for s, _, _ in STYLES)
    descent = max(envelopes[s][1] for s, _, _ in STYLES)
    ink_left = min(envelopes[s][2] for s, _, _ in STYLES)
    ink_right = max(envelopes[s][3] for s, _, _ in STYLES)
    height = ascent + descent
    if max(0, ink_right) > cell_w:
        raise RuntimeError(
            "%s: ink reaches column %d but the cell is only %d px wide - "
            "widen cell_w" % (spec["tag"], ink_right, cell_w))

    tables = {}
    for suffix, style, _bit in STYLES:
        glyphs = {}
        for cp in range(FIRST_CHAR, LAST_CHAR + 1):
            if cp in COVERAGE:
                glyphs[cp] = glyph_bitmap(faces[suffix], chr(cp),
                                          ascent, descent, cell_w)
            else:
                glyphs[cp] = [[0] * cell_w for _ in range(height)]
        check_glyphs(spec["tag"], style, glyphs)
        blob = bytearray()
        for cp in range(FIRST_CHAR, LAST_CHAR + 1):
            blob += pack_glyph(glyphs[cp], cell_w)
        tables[suffix] = (bytes(blob), glyphs)

    return {
        "tag": spec["tag"],
        "macro": spec["macro"],
        "size": spec["size"],
        "px": px,
        "cell_w": cell_w,
        "height": height,
        "ascent": ascent,
        "descent": descent,
        "row_bytes": (cell_w + 7) // 8,
        "tables": tables,
    }


def emit_header(family, font_info, sizes, path):
    row_bytes_max = max(s["row_bytes"] for s in sizes)
    out = []
    w = out.append
    w("/*")
    w(" * nano_j2me - nj_font_data.h  --  GENERATED FILE, DO NOT EDIT.")
    w(" *")
    w(" * Produced by tools/gen_fonts.py from %s:" % family)
    for suffix, label, _bit in STYLES:
        info = font_info[suffix]
        w(" *   %-5s %s" % (label, os.path.basename(info["path"])))
        w(" *         sha256 %s" % info["sha256"])
    w(" *")
    w(" * Glyph layout (frozen contract, see nj_gfx.h):")
    w(" *   1 bit per pixel, MSB first, one byte per 8 horizontal pixels,")
    w(" *   rows packed with NO padding:")
    w(" *       row_bytes    = (glyph_w + 7) / 8")
    w(" *       glyph stride = row_bytes * glyph_h")
    w(" *       glyph ch     = bitmap + (ch - NJ_FONT_DATA_FIRST) * stride")
    w(" *")
    w(" * Coverage: ASCII 0x20..0x7E and Latin-1 0xA0..0xFF.  The control gap")
    w(" * 0x7F..0x9F is present as blank cells so the lookup stays a plain")
    w(" * array index.")
    w(" *")
    w(" * Regenerate with:   python3 tools/gen_fonts.py")
    w(" * Glyphs come from FreeType's monochrome (FT_LOAD_TARGET_MONO)")
    w(" * rasteriser, so stems are hinted onto the pixel grid; the cell")
    w(" * metrics are the measured ink envelope of every covered glyph in")
    w(" * both styles, which is what makes the faces clip-free.")
    w(" */")
    w("#ifndef NJ_FONT_DATA_H")
    w("#define NJ_FONT_DATA_H")
    w("")
    w('#include "nj2me/nj_types.h"')
    w("")
    w("/* This header defines file-scope objects: include it from nj_font.c only. */")
    w("")
    w("#define NJ_FONT_DATA_FIRST 0x%02Xu" % FIRST_CHAR)
    w("#define NJ_FONT_DATA_LAST  0x%02Xu" % LAST_CHAR)
    w("#define NJ_FONT_DATA_CHARS %du" % CHAR_COUNT)
    w("")
    for spec in sizes:
        macro = spec["macro"]
        w("/* ---------------- %s: %d x %d (ascent %d, descent %d) ---------------- */"
          % (macro, spec["cell_w"], spec["height"], spec["ascent"], spec["descent"]))
        w("#define NJ_FONT_%s_W        %d" % (macro, spec["cell_w"]))
        w("#define NJ_FONT_%s_H        %d" % (macro, spec["height"]))
        w("#define NJ_FONT_%s_ASCENT   %d" % (macro, spec["ascent"]))
        w("#define NJ_FONT_%s_DESCENT  %d" % (macro, spec["descent"]))
        w("#define NJ_FONT_%s_LEADING  0" % macro)
        w("#define NJ_FONT_%s_ROWBYTES %d" % (macro, spec["row_bytes"]))
        w("")
        for suffix, label, _bit in STYLES:
            blob, glyphs = spec["tables"][suffix]
            name = "nj_font_%s_%s" % (spec["tag"], suffix)
            w("/* %s %s, %d glyphs x %d rows x %d byte(s)/row = %d bytes */"
              % (macro, label, CHAR_COUNT, spec["height"], spec["row_bytes"], len(blob)))
            w("static const nj_u1 %s[NJ_FONT_DATA_CHARS * NJ_FONT_%s_H * NJ_FONT_%s_ROWBYTES] = {"
              % (name, macro, macro))
            stride = spec["row_bytes"] * spec["height"]
            for index in range(CHAR_COUNT):
                cp = FIRST_CHAR + index
                if 0x21 <= cp <= 0x7E:
                    w("    /* U+%04X '%s' */" % (cp, chr(cp)))
                else:
                    w("    /* U+%04X */" % cp)
                w(c_bytes(blob[index * stride:(index + 1) * stride]))
            w("};")
            w("")
    w("#endif /* NJ_FONT_DATA_H */")
    w("")

    text = "\n".join(out)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    return len(text.encode("utf-8")), row_bytes_max


# --------------------------------------------------------------------------
# Specimen sheet
# --------------------------------------------------------------------------
SAMPLE_LINES = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "abcdefghijklmnopqrstuvwxyz",
    "0123456789 !\"#$%&'()*+,-./",
    ":;<=>?@[\\]^_`{|}~",
    "\u00c0\u00c1\u00c2\u00c3\u00c4\u00c5\u00c6\u00c7\u00c8\u00c9\u00ca\u00cb"
    "\u00cc\u00cd\u00ce\u00cf\u00d0\u00d1\u00d2\u00d3\u00d4\u00d5\u00d6\u00d7"
    "\u00d8\u00d9\u00da\u00db\u00dc\u00dd\u00de\u00df",
    "\u00e0\u00e1\u00e2\u00e3\u00e4\u00e5\u00e6\u00e7\u00e8\u00e9\u00ea\u00eb"
    "\u00ec\u00ed\u00ee\u00ef\u00f0\u00f1\u00f2\u00f3\u00f4\u00f5\u00f6\u00f7"
    "\u00f8\u00f9\u00fa\u00fb\u00fc\u00fd\u00fe\u00ff",
    "The quick brown fox jumps over the lazy dog.",
    "Gr\u00fc\u00dfe aus K\u00f6ln! \u00bfQu\u00e9 tal? \u00c7a va tr\u00e8s bien.",
    "Score: 1234567  Lives: 3  \u00a9 2024",
)


def render_text(glyphs, text, cell_w, height, scale):
    """Blit a string with the packed 1bpp cells at `scale`x, black on white."""
    img = Image.new("L", (max(1, len(text) * cell_w) * scale, height * scale), 255)
    for index, ch in enumerate(text):
        cp = ord(ch)
        if cp < FIRST_CHAR or cp > LAST_CHAR:
            cp = 0x20
        rows = glyphs[cp]
        for y in range(height):
            row = rows[y]
            for x in range(cell_w):
                if row[x]:
                    for sy in range(scale):
                        for sx in range(scale):
                            img.putpixel((index * cell_w * scale + x * scale + sx,
                                          y * scale + sy), 0)
    return img


def build_specimen(sizes, path, label_ttf, scale=4):
    label_font = ImageFont.truetype(label_ttf, 15)
    pad = 10
    title_h = 22
    blocks = []
    for spec in sizes:
        for suffix, label, _bit in STYLES:
            _blob, glyphs = spec["tables"][suffix]
            width = max(len(line) for line in SAMPLE_LINES) * spec["cell_w"] * scale
            blocks.append({
                "spec": spec, "label": label, "glyphs": glyphs,
                "w": width, "h": len(SAMPLE_LINES) * spec["height"] * scale,
            })
    total_w = pad * 2 + max(b["w"] for b in blocks)
    total_h = pad + sum(b["h"] + title_h + pad for b in blocks)
    sheet = Image.new("L", (total_w, total_h), 255)
    draw = ImageDraw.Draw(sheet)
    y = pad
    for block in blocks:
        spec = block["spec"]
        caption = ("%s %s   cell %dx%d   ascent %d  descent %d   "
                   "(rendered from the packed 1bpp table at %dx)"
                   % (spec["macro"], block["label"], spec["cell_w"],
                      spec["height"], spec["ascent"], spec["descent"], scale))
        draw.text((pad, y + 4), caption, font=label_font, fill=0)
        y += title_h
        baseline = y + spec["ascent"] * scale
        draw.line([(0, baseline + scale - 1), (total_w, baseline + scale - 1)],
                  fill=210)
        for line_index, line in enumerate(SAMPLE_LINES):
            strip = render_text(block["glyphs"], line, spec["cell_w"],
                                spec["height"], scale)
            sheet.paste(strip, (pad, y + line_index * spec["height"] * scale))
        y += block["h"] + pad
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sheet.save(path, "PNG", optimize=False)
    return sheet.size


# --------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--regular", help="regular TTF to use")
    ap.add_argument("--bold", help="bold TTF to use")
    ap.add_argument("--header", default=HEADER_PATH,
                    help="output header path (default: %(default)s)")
    ap.add_argument("--specimen", default=SPECIMEN_PATH,
                    help="output specimen PNG (default: %(default)s)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    family, regular, bold = pick_font_files(args.regular, args.bold)
    paths = {"p": regular, "b": bold}
    font_info = {s: {"path": p, "sha256": sha256_file(p)} for s, p in paths.items()}

    if not args.quiet:
        print("font source : %s" % family)
        print("  plain     : %s" % regular)
        print("  bold      : %s" % bold)

    sizes = [build_size(family, paths, spec) for spec in SIZES]
    nbytes, _ = emit_header(family, font_info, sizes, args.header)
    if not args.quiet:
        print("header      : %s (%d bytes)" % (args.header, nbytes))
        for spec in sizes:
            total = sum(len(spec["tables"][s][0]) for s, _, _ in STYLES)
            print("  %-6s cell %2dx%-2d ascent %-2d descent %-2d %5d bytes"
                  % (spec["tag"], spec["cell_w"], spec["height"],
                     spec["ascent"], spec["descent"], total))

    size = build_specimen(sizes, args.specimen, regular)
    if not args.quiet:
        print("specimen    : %s (%dx%d)" % (args.specimen, size[0], size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
