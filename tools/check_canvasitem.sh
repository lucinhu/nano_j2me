#!/usr/bin/env bash
#
# Nokia UI CanvasItem overlay gate.
#
# The fixture paints a blue canvas and attaches a green 40x30
# CanvasGraphicsItem at (10,20).  This check runs the fixture, dumps the last
# frame and asserts that the item rectangle has been composited on top of the
# canvas (green inside, blue outside).  It also asserts the API behaviour the
# item depends on: attach/getParent/position/size reported to Java, and the
# IllegalArgumentException for a negative z position or a non-Canvas parent.
#
#   tools/check_canvasitem.sh [path/to/nj2me]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-${NJ_BIN:-$ROOT/build/nj2me}}"
JAR="${NJ_CANVASITEM_JAR:-$ROOT/tests/fixtures/canvasitemdemo.jar}"

if [ ! -x "$BIN" ]; then
    echo "check_canvasitem: $BIN is missing - build the host first" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    "$ROOT/tools/make_canvasitem_jar.sh" >/dev/null
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/frames"

if ! "$BIN" --port headless --size 240x320 --heap 8 --fps 0 \
        --run-frames 30 --dump-frames "$WORK/frames" --dump-every 5 \
        "$JAR" >"$WORK/run.log" 2>&1; then
    echo "FAIL canvasitem: the fixture run failed" >&2
    tail -20 "$WORK/run.log" >&2
    exit 1
fi

FRAME="$(ls "$WORK/frames"/*.ppm 2>/dev/null | sort | tail -1)"
if [ -z "$FRAME" ]; then
    echo "FAIL canvasitem: no frame was dumped" >&2
    exit 1
fi

PPM="$FRAME" LOG="$WORK/run.log" python3 - "$WORK" <<'PY'
import os
import sys

log_path = os.environ["LOG"]
log = open(log_path, "r", errors="replace").read()
failures = []

def require_log(line):
    if line not in log:
        failures.append("missing log line: " + line)

require_log("CANVASITEM attached=")
require_log("x=10 y=20 w=40 h=30")
require_log("CANVASITEM item painted")
require_log("CANVASITEM z rejected")
require_log("CANVASITEM parent rejected")
if "CANVASITEM z accepted" in log:
    failures.append("negative z position was accepted")
if "CANVASITEM parent accepted" in log:
    failures.append("a non-Canvas parent was accepted")
for bad in ("uncaught exception", "NoClassDefFoundError", "NoSuchMethodError",
            "Exception in thread"):
    if bad in log:
        failures.append("log contains " + bad)

raw = open(os.environ["PPM"], "rb").read()
parts = raw.split(b"\n", 3)
if len(parts) != 4 or parts[0] != b"P6":
    sys.exit("FAIL canvasitem: not a P6 PPM")
width, height = map(int, parts[1].split())
pixels = parts[3]

def rgb(x, y):
    o = (y * width + x) * 3
    return tuple(pixels[o:o + 3])

def green(p):
    r, g, b = p
    return g > 200 and r < 40 and b < 40

def blue(p):
    r, g, b = p
    return b > 200 and r < 40 and g < 40

# Inside the 40x30 item at (10,20): green.
for x, y in ((10, 20), (25, 35), (49, 49)):
    p = rgb(x, y)
    if not green(p):
        failures.append("pixel (%d,%d) is %r, expected the item's green" % (x, y, p))
# Around it, including one pixel before the item rect: the canvas blue.
for x, y in ((0, 0), (9, 20), (50, 50), (60, 60), (200, 300)):
    p = rgb(x, y)
    if not blue(p):
        failures.append("pixel (%d,%d) is %r, expected the canvas's blue" % (x, y, p))

if failures:
    print("FAIL canvasitem: %d check(s) failed" % len(failures))
    for f in failures:
        print("  " + f)
    sys.exit(1)
print("PASS canvasitem: overlay composited and API behaviour checked")
PY
