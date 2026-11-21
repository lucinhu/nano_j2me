#!/usr/bin/env bash
#
# Siemens mobile API gate (round 163).
#
# The fixture paints a dark canvas, draws a red sprite through a
# com.siemens.mp.color_game.LayerManager, a white 8x8 block through
# ExtendedImage.blitToScreen, a green 4x4 block through a Layer subclass, runs
# the Light calls and reads "resource:resdemo.txt" from its own JAR.  The check
# asserts the marker log, the resource read and the resulting pixels (which
# prove the layer manager, the sprite draw, the extended image blit and the
# resource stream all reached the framebuffer).
#
#   tools/check_siemens.sh [path/to/nj2me]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-${NJ_BIN:-$ROOT/build/nj2me}}"
JAR="${NJ_SIEMENS_JAR:-$ROOT/tests/fixtures/siemensdemo.jar}"

if [ ! -x "$BIN" ]; then
    echo "check_siemens: $BIN is missing - build the host first" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    "$ROOT/tools/make_siemens_jar.sh" >/dev/null
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/frames"

if ! "$BIN" --port headless --size 240x320 --heap 8 --fps 30 \
        --run-frames 60 --dump-frames "$WORK/frames" --dump-every 5 \
        "$JAR" >"$WORK/run.log" 2>&1; then
    echo "FAIL siemens: the fixture run failed" >&2
    tail -20 "$WORK/run.log" >&2
    exit 1
fi

FRAME="$(ls "$WORK/frames"/*.ppm 2>/dev/null | sort | tail -1)"
if [ -z "$FRAME" ]; then
    echo "FAIL siemens: no frame was dumped" >&2
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

require_log("SIEMENS manager_size=1")
require_log("sprite_x=100 sprite_y=40 visible=true")
require_log("SIEMENS sprite=ok")
require_log("SIEMENS extended_image=ok")
require_log("SIEMENS light=ok")
require_log("SIEMENS layer_manager=ok")
require_log("SIEMENS resource=siemens-demo-resource len=22")
for bad in ("SIEMENS failed=", "SIEMENS resource_failed=",
            "uncaught exception", "NoClassDefFoundError",
            "NoSuchMethodError", "Exception in thread"):
    if bad in log:
        failures.append("log contains " + bad)

raw = open(os.environ["PPM"], "rb").read()
parts = raw.split(b"\n", 3)
if len(parts) != 4 or parts[0] != b"P6":
    sys.exit("FAIL siemens: not a P6 PPM")
width, height = map(int, parts[1].split())
pixels = parts[3]

def rgb(x, y):
    o = (y * width + x) * 3
    return tuple(pixels[o:o + 3])

def red(p):
    r, g, b = p
    return r > 200 and g < 60 and b < 60

def white(p):
    r, g, b = p
    return r > 200 and g > 200 and b > 200

def green(p):
    r, g, b = p
    return g > 200 and r < 60 and b < 60

def dark(p):
    r, g, b = p
    return r < 40 and g < 60 and b < 80 and (r + g + b) > 40

# Sprite drawn by LayerManager.append(Sprite) + Sprite.paint(Graphics).
for x, y in ((100, 40), (105, 45), (107, 47)):
    p = rgb(x, y)
    if not red(p):
        failures.append("pixel (%d,%d) is %r, expected the sprite's red" % (x, y, p))
# ExtendedImage.clear((byte)0) + blitToScreen(10,300): white 8x8 block.
for x, y in ((10, 300), (15, 305), (17, 307)):
    p = rgb(x, y)
    if not white(p):
        failures.append("pixel (%d,%d) is %r, expected the extended image's white" % (x, y, p))
# A Layer subclass painted by a second LayerManager: green 4x4 at (0,0).
for x, y in ((0, 0), (1, 1)):
    p = rgb(x, y)
    if not green(p):
        failures.append("pixel (%d,%d) is %r, expected the layer's green" % (x, y, p))
# Canvas background everywhere else.
for x, y in ((200, 200), (120, 100), (60, 250)):
    p = rgb(x, y)
    if not dark(p):
        failures.append("pixel (%d,%d) is %r, expected the canvas background" % (x, y, p))

if failures:
    print("FAIL siemens: %d check(s) failed" % len(failures))
    for f in failures:
        print("  " + f)
    sys.exit(1)
print("PASS siemens: Siemens layer/sprite/extended image and resource: stream verified")
PY
