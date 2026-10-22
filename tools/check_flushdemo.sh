#!/usr/bin/env bash
# A GameCanvas that flushes whole frames must never be presented half drawn.
#
# tests/flushdemo/FlushDemo.java describes the fixture.  In one line:
#
#     worker { fill the whole screen blue, flush; next frame red, flush; ... }
#
# The buffer is not double buffered, so a runtime that ALSO paints the
# GameCanvas once per frame copies whatever is in the buffer at that moment - if
# the worker is halfway through the next frame, the presented frame holds part
# of one colour and part of the other.  That torn frame is the flicker a player
# reported for 鲁宾逊漂流记汉化版_702, whose game canvas flushes once per pass of
# its own loop.
#
#   FLUSH loop=<n>          the worker flushed n frames
#
# 120 frames, not 60: an ASan/Debug build runs the fixture ~2.6x slower, and
# with a 60-frame budget it only reached 8 worker flushes (the check wants 10).
# The worker's frame is deliberately slow, so the loop count follows wall time.
#   every presented frame is ONE colour (no frame holds both)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/flushdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
DUMP="$(mktemp -d)"
trap 'rm -rf "$DUMP"' EXIT

out="$(timeout 90 "$BIN" --port headless --heap 8 --size 176x208 --fps 30 \
        --run-frames 120 --dump-frames "$DUMP/" --dump-every 1 --stats \
        "$JAR" 2>&1)"
rc=$?

say() {
    if [ "$1" = 1 ]; then
        echo "FAIL $2"
        fail=1
    else
        echo "ok   $2"
    fi
}

say $([ "$rc" = 0 ] && echo 0 || echo 1) "the run exits cleanly (exit $rc)"

line="$(printf '%s\n' "$out" | grep '^FLUSH loop=' | tail -1 || true)"
say $([ -n "$line" ] && echo 0 || echo 1) "the worker flushed frames"
loops="$(printf '%s\n' "$line" | sed -n 's/.*loop=\([0-9]*\).*/\1/p')"
say $([ -n "$loops" ] && [ "$loops" -ge 10 ] && echo 0 || echo 1) \
    "the worker kept flushing (loop=${loops:-none})"

if printf '%s\n' "$out" | grep -qE 'uncaught exception|thrown:'; then
    say 1 "nothing was thrown"
else
    say 0 "nothing was thrown"
fi

n="$(ls "$DUMP"/*.ppm 2>/dev/null | wc -l)"
say $([ "$n" -ge 10 ] && echo 0 || echo 1) "the run presented frames ($n)"

if [ "$n" -ge 1 ]; then
    torn="$(python3 - "$DUMP" <<'PY'
import glob, sys
bad = 0
worst = 0
for path in sorted(glob.glob(sys.argv[1] + '/*.ppm')):
    data = open(path, 'rb').read()
    # P6 header: magic, width height, maxval - skip to the pixel block.
    i = data.index(b'255\n') + 4
    px = data[i:]
    red = blue = 0
    for k in range(0, len(px) - 2, 3):
        if px[k] > 180 and px[k + 1] < 80 and px[k + 2] < 80:
            red += 1
        elif px[k + 2] > 180 and px[k + 1] < 80 and px[k] < 80:
            blue += 1
    both = red if red < blue else blue     # the smaller population
    if both > worst:
        worst = both
    if both >= 200:                        # a torn frame holds plenty of both
        bad += 1
print(bad, worst)
PY
)"
    bad="$(printf '%s' "$torn" | awk '{print $1}')"
    worst="$(printf '%s' "$torn" | awk '{print $2}')"
    say $([ "${bad:-1}" = 0 ] && echo 0 || echo 1) \
        "no presented frame is half drawn (${bad:-?} torn, worst ${worst:-?} px of the other colour)"
fi

if [ "$fail" = 0 ]; then
    echo "PASS check_flushdemo"
else
    echo "FAILED check_flushdemo"
fi
exit "$fail"
