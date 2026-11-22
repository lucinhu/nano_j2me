#!/usr/bin/env bash
#
# Nokia UI TextEditor gate.
#
# The fixture paints a yellow canvas and attaches a 60x20 TextEditor at
# (10,10).  This check dumps a frame and asserts the editor rectangle was
# composited over the canvas in its own background colour, and asserts the
# content/caret/selection/listener/error behaviour the API promises.
#
#   tools/check_texteditor.sh [path/to/nj2me]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-${NJ_BIN:-$ROOT/build/nj2me}}"
JAR="${NJ_TEXTEDITOR_JAR:-$ROOT/tests/fixtures/texteditordemo.jar}"

if [ ! -x "$BIN" ]; then
    echo "check_texteditor: $BIN is missing - build the host first" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    "$ROOT/tools/make_texteditor_jar.sh" >/dev/null
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/frames"

if ! "$BIN" --port headless --size 240x320 --heap 8 --fps 0 \
        --run-frames 30 --dump-frames "$WORK/frames" --dump-every 5 \
        "$JAR" >"$WORK/run.log" 2>&1; then
    echo "FAIL texteditor: the fixture run failed" >&2
    tail -20 "$WORK/run.log" >&2
    exit 1
fi

FRAME="$(ls "$WORK/frames"/*.ppm 2>/dev/null | sort | tail -1)"
if [ -z "$FRAME" ]; then
    echo "FAIL texteditor: no frame was dumped" >&2
    exit 1
fi

PPM="$FRAME" LOG="$WORK/run.log" python3 - <<'PY'
import os
import sys

log = open(os.environ["LOG"], "r", errors="replace").read()
failures = []

def require_log(line):
    if line not in log:
        failures.append("missing log line: " + line)

for line in (
        "TE attached=true",
        "TE content=abc",
        "TE inserted=Xabc",
        "TE deleted=Xc",
        "TE truncated=hell",
        "TE caret=2",
        "TE selection=bc",
        "TE listener action=1 content=changed",
        "TE after_callback=changed",
        "TE listener action=4",
        "TE iae=2",
        "TE unfocused_caret=0",
        "TE max=20 size=4",
        "TE painted"):
    require_log(line)
for bad in ("TE failed", "uncaught exception", "NoClassDefFoundError",
            "NoSuchMethodError", "Exception in thread"):
    if bad in log:
        failures.append("log contains " + bad)

raw = open(os.environ["PPM"], "rb").read()
parts = raw.split(b"\n", 3)
if len(parts) != 4 or parts[0] != b"P6":
    sys.exit("FAIL texteditor: not a P6 PPM")
width, height = map(int, parts[1].split())
pixels = parts[3]

def rgb(x, y):
    o = (y * width + x) * 3
    return tuple(pixels[o:o + 3])

def editor_blue(p):
    r, g, b = p
    return b > 200 and r < 40 and g < 40

def canvas_yellow(p):
    r, g, b = p
    return r > 200 and g > 200 and b < 60

# Inside the 60x20 editor at (10,10): its blue background (white text may
# cover a few pixels, so pick corners and an empty row).
for x, y in ((10, 10), (30, 28), (69, 29), (12, 27)):
    p = rgb(x, y)
    if not editor_blue(p):
        failures.append("pixel (%d,%d) is %r, expected the editor blue" % (x, y, p))
# One pixel outside the editor rectangle: the canvas yellow.
for x, y in ((0, 0), (9, 10), (70, 30), (5, 5), (120, 200)):
    p = rgb(x, y)
    if not canvas_yellow(p):
        failures.append("pixel (%d,%d) is %r, expected the canvas yellow" % (x, y, p))

if failures:
    print("FAIL texteditor: %d check(s) failed" % len(failures))
    for f in failures:
        print("  " + f)
    sys.exit(1)
print("PASS texteditor: overlay composited and TextEditor API behaviour checked")
PY
