#!/usr/bin/env python3
"""Compare nj_ttf.c coverage maps against FreeType (the Pillow oracle).

tests/test_ttf.c rasterises a set of glyphs with src/gfx/nj_ttf.c, dumps each
mask as a binary PGM and writes a manifest, then runs this script and asserts
on the numbers it prints.  Doing the comparison in Python keeps the *oracle*
independent: FreeType is the only rasteriser anybody trusts for TrueType, and
Pillow is already a fixture-generator dependency of this project.

Usage:
    python3 tools/ttf_ft_compare.py --manifest /tmp/nj2me_ttf_cmp.txt

Manifest format (written by the test, one glyph per line):

    font <path-to-ttf>
    <codepoint> <px> <ascent> <advance> <w> <h> <path.pgm>

`ascent` is the value nj_font.ascent reports (row 0 sits that many pixels above
the baseline), `advance` the value glyph_advance returned, and the PGM is the
mask exactly as glyph_lookup returned it (P5, maxval 255, w bytes per row).

Output, one line per glyph:

    RESULT <cp> <px> adv=<n> ftadv=<n> mae=<f> ratio=<f> mid=<n> dv=<n> \
           cdx=<f> cdy=<f> bbx=<n> bby=<n>

    mae    mean absolute coverage difference over the union canvas (0..255)
    ratio  my ink total / FreeType's ink total
    mid    pixels with 0 < coverage < 255
    dv     distinct non-zero coverage values
    cdx    centroid x difference in pixels (mine - FreeType)
    cdy    centroid y difference in pixels (mine - FreeType)
    bbx    worst ink-bbox edge difference in pixels, x edges
    bby    worst ink-bbox edge difference in pixels, y edges

and a last line summarising the run.  Prints `SKIP <reason>` and exits 3 when
Pillow or the font is unavailable, which the test treats as "section skipped".
"""

import argparse
import os
import sys


def read_pgm(path):
    """Read the P5 masks we write: 8-bit, no comments, single-line header."""
    with open(path, "rb") as fh:
        data = fh.read()
    fields = []
    i = 0
    while len(fields) < 4:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while i < len(data) and data[i:i + 1] != b"\n":
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    if fields[0] != b"P5":
        raise ValueError("not a P5 PGM: %s" % path)
    w, h, maxval = int(fields[1]), int(fields[2]), int(fields[3])
    i += 1  # exactly one whitespace byte follows maxval
    px = data[i:i + w * h]
    if len(px) != w * h or maxval != 255:
        raise ValueError("truncated or non-8-bit PGM: %s" % path)
    return w, h, [px[y * w:(y + 1) * w] for y in range(h)]


def compare(face, ft_asc, cp, px, asc, adv, w, h, rows):
    ch = chr(cp)
    mask, off = face.getmask2(ch, mode="L")
    fw, fh = mask.size
    frows = [[mask.getpixel((x, y)) for x in range(fw)] for y in range(fh)]
    ftadv = int(round(face.getlength(ch)))

    # FreeType's mask has its own origin: column 0 is pen_x + off[0], and row 0
    # sits off[1] pixels below the ascender line.  Map both onto the canvas of
    # our mask, whose column 0 is pen_x and whose row 0 is `asc` above the
    # baseline.
    dx = off[0]
    dy = off[1] + (asc - ft_asc)

    W = max(w, dx + fw) + 2
    H = max(h, dy + fh) + 2

    tot = 0
    ink_a = 0
    ink_b = 0
    mid = 0
    seen = {}
    sa = sb = 0.0
    cax = cay = cbx = cby = 0.0
    bba = [None] * 4
    bbb = [None] * 4
    for y in range(H):
        for x in range(W):
            va = rows[y][x] if y < h and x < w else 0
            fy = y - dy
            fx = x - dx
            vb = frows[fy][fx] if 0 <= fy < fh and 0 <= fx < fw else 0
            tot += abs(va - vb)
            ink_a += va
            ink_b += vb
            if va:
                seen[va] = seen.get(va, 0) + 1
                if 0 < va < 255:
                    mid += 1
                sa += va
                cax += x * va
                cay += y * va
                bba[0] = x if bba[0] is None else min(bba[0], x)
                bba[1] = y if bba[1] is None else min(bba[1], y)
                bba[2] = x if bba[2] is None else max(bba[2], x)
                bba[3] = y if bba[3] is None else max(bba[3], y)
            if vb:
                sb += vb
                cbx += x * vb
                cby += y * vb
                bbb[0] = x if bbb[0] is None else min(bbb[0], x)
                bbb[1] = y if bbb[1] is None else min(bbb[1], y)
                bbb[2] = x if bbb[2] is None else max(bbb[2], x)
                bbb[3] = y if bbb[3] is None else max(bbb[3], y)

    mae = tot / float(W * H)
    ratio = (ink_a / float(ink_b)) if ink_b else 0.0
    if sa and sb:
        cdx = cax / sa - cbx / sb
        cdy = cay / sa - cby / sb
    else:
        cdx = cdy = 0.0
    if bba[0] is None or bbb[0] is None:
        bbx = bby = 0
    else:
        bbx = max(abs(bba[0] - bbb[0]), abs(bba[2] - bbb[2]))
        bby = max(abs(bba[1] - bbb[1]), abs(bba[3] - bbb[3]))

    print("RESULT %d %d adv=%d ftadv=%d mae=%.3f ratio=%.4f mid=%d dv=%d "
          "cdx=%.3f cdy=%.3f bbx=%d bby=%d fw=%d fh=%d"
          % (cp, px, adv, ftadv, mae, ratio, mid, len(seen),
             cdx, cdy, bbx, bby, fw, fh))
    return dict(mae=mae, ratio=ratio, mid=mid, dv=len(seen), cdx=cdx, cdy=cdy,
                bbx=bbx, bby=bby, adv=adv, ftadv=ftadv)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    args = ap.parse_args()

    try:
        from PIL import ImageFont
    except ImportError:
        print("SKIP Pillow is not available to python3")
        return 3

    with open(args.manifest) as fh:
        lines = [ln.split() for ln in fh if ln.strip()]

    fonts = [ln[1] for ln in lines if ln[0] == "font"]
    if not fonts:
        print("SKIP manifest has no font line")
        return 3
    font = fonts[0]
    if not os.path.exists(font):
        print("SKIP font not found: %s" % font)
        return 3

    faces = {}
    results = []
    for ln in lines:
        if ln[0] == "font":
            continue
        cp, px, asc, adv, w, h = (int(v) for v in ln[0:6])
        path = ln[6]
        if px not in faces:
            try:
                faces[px] = ImageFont.truetype(font, px)
            except Exception as exc:                      # pragma: no cover
                print("SKIP FreeType cannot open %s at %dpx: %s"
                      % (font, px, exc))
                return 3
        face = faces[px]
        try:
            cw, chh, rows = read_pgm(path)
        except Exception as exc:
            print("ERROR %s: %s" % (path, exc))
            return 2
        if (cw, chh) != (w, h):
            print("ERROR %s: manifest says %dx%d, PGM is %dx%d"
                  % (path, w, h, cw, chh))
            return 2
        results.append(compare(face, face.getmetrics()[0], cp, px, asc, adv,
                               w, h, rows))

    if not results:
        print("SKIP manifest has no glyphs")
        return 3

    maes = [r["mae"] for r in results]
    ratios = [r["ratio"] for r in results if r["ratio"]]
    print("SUMMARY glyphs=%d mean_mae=%.3f max_mae=%.3f min_ratio=%.4f "
          "max_ratio=%.4f mid_total=%d dv_min=%d"
          % (len(results), sum(maes) / len(maes), max(maes),
             min(ratios) if ratios else 0.0, max(ratios) if ratios else 0.0,
             sum(r["mid"] for r in results),
             min(r["dv"] for r in results)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
