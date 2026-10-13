#!/usr/bin/env python3
"""Read the pixels of a screenshot and say which colours are where.

A port can get colour wrong in exactly one place - the store that hands a
16-bit 5-6-5 pixel to the window - and the result looks like art, not like a
bug, so nobody notices until a game's red flag flies blue.  This turns the
question into numbers: find the game's area, walk the middle row, and print the
runs of saturated colour left to right.

    tools/sample_screenshot.py shot.png
    tools/sample_screenshot.py shot.png --row 300 --min-run 16
    tools/sample_screenshot.py shot.png --expect red,green,blue

--expect exits non-zero unless the saturated runs, left to right, name exactly
those colours; tools/check_color_order.sh uses it on the frame the fixture
tools/make_colordemo_jar.sh paints (three bars: pure red, green, blue).
"""
import argparse
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - the repo's python has Pillow
    print("sample_screenshot.py needs Pillow", file=sys.stderr)
    raise SystemExit(2)


def classify(c):
    """Name a saturated colour, or return None for anything muted."""
    r, g, b = c[0], c[1], c[2]
    hi, lo = max(r, g, b), min(r, g, b)
    if hi < 170 or hi - lo < 120:
        return None
    if r == hi and g < hi * 3 // 4 and b < hi * 3 // 4:
        return "red"
    if g == hi and r < hi * 3 // 4 and b < hi * 3 // 4:
        return "green"
    if b == hi and r < hi * 3 // 4 and g < hi * 3 // 4:
        return "blue"
    return None


def runs_of(row_pixels, min_run):
    """Saturated runs in one row, merging neighbours of the same name."""
    out = []
    for i, c in enumerate(row_pixels):
        name = classify(c)
        if not name:
            continue
        if out and out[-1][0] == name:
            out[-1][2] = i
            out[-1][3] += 1
        else:
            out.append([name, i, i, 1])
    return [r for r in out if r[3] >= min_run]


def content_box(im):
    """Bounding box of everything that is not the black letterbox."""
    px = im.load()
    w, h = im.size
    minx, miny, maxx, maxy = w, h, -1, -1
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            if sum(px[x, y]) > 30:
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                if y < miny:
                    miny = y
                if y > maxy:
                    maxy = y
    return (minx, miny, maxx, maxy) if maxx >= 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--row", type=int, default=None,
                    help="row to read (default: middle of the game area)")
    ap.add_argument("--min-run", type=int, default=8,
                    help="ignore saturated runs shorter than this")
    ap.add_argument("--expect", default=None,
                    help="comma-separated colour names, left to right")
    args = ap.parse_args()

    im = Image.open(args.image).convert("RGB")
    w, h = im.size
    box = content_box(im)
    if box is None:
        print("%s: %dx%d, no lit pixels" % (args.image, w, h))
        return 1
    row = args.row if args.row is not None else (box[1] + box[3]) // 2
    px = im.load()
    names = runs_of([px[x, row] for x in range(w)], args.min_run)

    print("%s: %dx%d, game area %s, row %d" % (args.image, w, h, box, row))
    for name, x0, x1, n in names:
        c = px[(x0 + x1) // 2, row]
        print("  %-5s x=%4d..%-4d (%4d px) rgb=%s" % (name, x0, x1, n, c))
    if not names:
        print("  (no saturated colour in that row)")

    if args.expect:
        want = [s.strip() for s in args.expect.split(",") if s.strip()]
        got = [n[0] for n in names]
        ok = got == want
        print("%s: expected %s, read %s" % ("ok" if ok else "MISMATCH",
                                            want, got))
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
