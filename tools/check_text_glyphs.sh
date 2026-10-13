#!/usr/bin/env bash
#
# The glyph-coverage gate.
#
# When the built-in CJK face is compiled out (NJ_ENABLE_FONT_CJK=0, which is
# what the Android build does to save ~480 KB per ABI) every Chinese character
# on screen has to come from a font file the port supplied.  If that font is
# missing, unreadable or unparsable, the runtime does not fail - it falls back
# to the Latin bitmap faces, and Chinese text silently becomes nothing at all.
# A screenshot of that looks like a game with a plain menu, which is exactly
# how it goes unnoticed.
#
# So: draw Latin and Chinese lines at known heights, and count the ink in each
# band.  Latin proves text is being drawn at all; the Chinese bands prove the
# font file reached the rasteriser.
#
#   tools/check_text_glyphs.sh                     # built-in faces
#   tools/check_text_glyphs.sh --font /path/font   # a supplied face
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

FONT=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --font) FONT="${2:-}"; shift 2 ;;
        *) echo "usage: check_text_glyphs.sh [--font FILE]" >&2; exit 2 ;;
    esac
done

JAR="$ROOT/tests/fixtures/textdemo.jar"
RUNNER="$ROOT/build/nj2me"
if [ ! -x "$RUNNER" ]; then
    echo "check_text_glyphs: build/nj2me is missing - build the host first" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    "$ROOT/tools/make_text_jar.sh" >/dev/null
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

FONT_ARGS=()
if [ -n "$FONT" ]; then
    if [ ! -f "$FONT" ]; then
        echo "check_text_glyphs: no such font file: $FONT" >&2
        exit 2
    fi
    FONT_ARGS=(--font "$FONT")
fi

"$RUNNER" --port headless --size 240x320 --heap 8 --fps 0 \
    --run-frames 3 --dump-frames "$WORK" --dump-every 1 \
    "${FONT_ARGS[@]}" "$JAR" >/dev/null

FRAME="$(ls "$WORK"/*.ppm | tail -1)"
python3 - "$FRAME" "$FONT" <<'PY'
import sys
from PIL import Image

path, font = sys.argv[1], sys.argv[2] or "(built-in bitmap faces)"
im = Image.open(path).convert("RGB")
w, h = im.size

# (label, y0, y1, must_have_ink): the bands come from tools/make_text_jar.sh's
# fixed line positions, with slack so a taller face cannot leak into the next.
bands = [
    ("latin small",  0,  30, False),
    ("cjk medium",  32,  70, True),
    ("cjk large",   80, 130, True),
    ("cjk small",  140, 180, True),
    ("latin medium",190, 240, False),
]

def ink(y0, y1):
    n = 0
    for y in range(y0, min(y1, h)):
        for x in range(w):
            r, g, b = im.getpixel((x, y))
            if r + g + b < 3 * 128:
                n += 1
    return n

bad = []
print("font: %s" % font)
for label, y0, y1, must in bands:
    n = ink(y0, y1)
    print("  %-13s y=%3d..%-3d ink=%5d%s" % (label, y0, y1, n,
                                             "" if n else "   <-- EMPTY"))
    if must and n < 20:
        bad.append(label)

if bad:
    print("FAIL: no ink in %s - the font file did not reach the rasteriser"
          % ", ".join(bad))
    sys.exit(1)
print("OK: every band has ink")
PY
