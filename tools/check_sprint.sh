#!/usr/bin/env bash
#
# Sprint PCS carrier API gate (round 175).
#
# The fixture builds both Clip flavours, registers a PlayerListener, plays and
# stops a clip, vibrates, asks for a Muglet, sets an exit URI and checks that a
# bad repeat count raises IllegalArgumentException.  The check asserts the
# marker log plus a painted canvas, proving the com.sprintpcs.* classes link
# and dispatch from ordinary game code.
#
#   tools/check_sprint.sh [path/to/nj2me]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-${NJ_BIN:-$ROOT/build/nj2me}}"
JAR="${NJ_SPRINT_JAR:-$ROOT/tests/fixtures/sprintdemo.jar}"

if [ ! -x "$BIN" ]; then
    echo "check_sprint: $BIN is missing - build the host first" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    "$ROOT/tools/make_sprint_jar.sh" >/dev/null
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/frames"

if ! "$BIN" --port headless --size 240x320 --heap 8 --fps 30 \
        --run-frames 60 --dump-frames "$WORK/frames" --dump-every 5 \
        "$JAR" >"$WORK/run.log" 2>&1; then
    echo "FAIL sprint: the fixture run failed" >&2
    tail -20 "$WORK/run.log" >&2
    exit 1
fi

FRAME="$(ls "$WORK/frames"/*.ppm 2>/dev/null | sort | tail -1)"
if [ -z "$FRAME" ]; then
    echo "FAIL sprint: no frame was dumped" >&2
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

require_log("SPRINT clip_uri=audio/midi")
require_log("SPRINT clip_data=audio/x-tone-seq")
require_log("SPRINT player_listener=ok")
require_log("SPRINT play=ok stop=ok")
require_log("SPRINT vibrator=ok")
require_log("SPRINT muglet_uri=[] media=null referring=null")
require_log("SPRINT system_exit=ok")
require_log("SPRINT repeat_rejected=java.lang.IllegalArgumentException")
for bad in ("SPRINT failed=", "SPRINT repeat_bad=", "uncaught exception",
            "NoClassDefFoundError", "NoSuchMethodError", "Exception in thread"):
    if bad in log:
        failures.append("log contains " + bad)

raw = open(os.environ["PPM"], "rb").read()
parts = raw.split(b"\n", 3)
if len(parts) != 4 or parts[0] != b"P6":
    sys.exit("FAIL sprint: not a P6 PPM")
width, height = map(int, parts[1].split())
pixels = parts[3]

def rgb(x, y):
    o = (y * width + x) * 3
    return tuple(pixels[o:o + 3])

def is_blue(px):
    return px[2] > 150 and px[0] < 80 and px[1] < 80

for point in ((5, 5), (120, 160), (200, 300)):
    if not is_blue(rgb(*point)):
        failures.append("pixel %s is not the blue canvas: %r"
                        % (point, rgb(*point)))

if failures:
    for f in failures:
        print("FAIL sprint: " + f, file=sys.stderr)
    sys.exit(1)
print("check_sprint: ok (markers + pixels)")
PY
