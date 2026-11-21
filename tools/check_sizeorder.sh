#!/usr/bin/env bash
#
# sizeChanged() ordering gate (round 170).
#
# The fixture's game thread stores a boot renderer into a field, and its
# sizeChanged(240, 320) restores that field from a backup that is still null
# when the callback arrives early.  A runtime that delivers the very first
# sizeChanged() lazily on the first paint (after startApp() started the game
# thread) leaves the field null and every paint throws; the fixed runtime
# delivers it inside setCurrent(), so the game thread's store wins.
#
#   tools/check_sizeorder.sh [path/to/nj2me]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-${NJ_BIN:-$ROOT/build/nj2me}}"
JAR="${NJ_SIZEORDER_JAR:-$ROOT/tests/fixtures/sizeorderdemo.jar}"

if [ ! -x "$BIN" ]; then
    echo "check_sizeorder: $BIN is missing - build the host first" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    "$ROOT/tools/make_sizeorder_jar.sh" >/dev/null
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/frames"

if ! "$BIN" --port headless --size 240x320 --heap 8 --fps 30 \
        --run-frames 60 --dump-frames "$WORK/frames" --dump-every 10 \
        "$JAR" >"$WORK/run.log" 2>&1; then
    echo "FAIL sizeorder: the fixture run failed" >&2
    tail -20 "$WORK/run.log" >&2
    exit 1
fi

FRAME="$(ls "$WORK/frames"/*.ppm 2>/dev/null | sort | tail -1)"
if [ -z "$FRAME" ]; then
    echo "FAIL sizeorder: no frame was dumped" >&2
    exit 1
fi

PPM="$FRAME" LOG="$WORK/run.log" python3 - "$WORK" <<'PY'
import os
import sys

log = open(os.environ["LOG"], "r", errors="replace").read()
failures = []

def require(line):
    if line not in log:
        failures.append("missing log line: " + line)

# The first sizeChanged() must arrive while the game thread has not stored its
# renderer yet; the thread then has to be able to keep it.
require("SIZEORDER sizeChanged 240x320 boot=false")
require("SIZEORDER thread boot=true")
require("SIZEORDER paint renderer=1")
for bad in ("uncaught exception", "NullPointerException",
            "Exception in thread", "NoClassDefFoundError",
            "NoSuchMethodError"):
    if bad in log:
        failures.append("log contains " + bad)

raw = open(os.environ["PPM"], "rb").read()
parts = raw.split(b"\n", 3)
if len(parts) != 4 or parts[0] != b"P6":
    sys.exit("FAIL sizeorder: not a P6 PPM")
width, height = map(int, parts[1].split())
pixels = parts[3]

def rgb(x, y):
    o = (y * width + x) * 3
    return tuple(pixels[o:o + 3])

def green(p):
    r, g, b = p
    return g > 200 and r < 40 and b < 40

for x, y in ((0, 0), (120, 160), (239, 319)):
    p = rgb(x, y)
    if not green(p):
        failures.append("pixel (%d,%d) is %r, expected the fixture's green" % (x, y, p))

if failures:
    print("FAIL sizeorder: %d check(s) failed" % len(failures))
    for f in failures:
        print("  " + f)
    sys.exit(1)
print("PASS sizeorder: first sizeChanged() delivered before the game thread's store")
PY
