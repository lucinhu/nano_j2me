#!/bin/bash
# repaint(); serviceRepaints(); with the paint state in a field.
#
# The idiom this regresses, and why both halves matter, are described in
# tests/repaintdemo/RepaintDemo.java.  In one line:
#
#     state = 1; canvas.repaint(); canvas.serviceRepaints(); state = 0;
#
# has to leave the canvas showing what paint() drew inside the window.
#
#   REPAINT loop=<n> paints=<m>        the worker forced <m> synchronous paints
#   the last presented frame is red    no later paint wiped it
#
# The frame check is the strict half: a runtime that paints every frame on its
# own presents a black frame right after the application's (the state field is
# back to 0 by then), so the last dump goes black even though the synchronous
# paint worked.  仙魔飞行棋_417 needed exactly that extra paint removed - its
# map stayed black for a whole run while the runtime counted a paint a frame.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/repaintdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
DUMP="$(mktemp -d)"
trap 'rm -rf "$DUMP"' EXIT

out="$(timeout 90 "$BIN" --port headless --heap 8 --size 176x208 --fps 30 \
        --run-frames 40 --dump-frames "$DUMP/" --dump-every 1 --stats \
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

line="$(printf '%s\n' "$out" | grep -m1 '^REPAINT loop=' | tail -1 || true)"
line="$(printf '%s\n' "$out" | grep '^REPAINT loop=' | tail -1 || true)"
say $([ -n "$line" ] && echo 0 || echo 1) "the worker ran its paint loop"
loops="$(printf '%s\n' "$line" | sed -n 's/.*loop=\([0-9]*\).*/\1/p')"
paints="$(printf '%s\n' "$line" | sed -n 's/.*paints=\([0-9]*\).*/\1/p')"
say $([ -n "$loops" ] && [ "$loops" -ge 10 ] && echo 0 || echo 1) \
    "the loop kept running through the run (loop=${loops:-none})"
say $([ -n "$paints" ] && [ "$paints" -ge 10 ] && echo 0 || echo 1) \
    "serviceRepaints() delivered a paint per iteration (paints=${paints:-none})"

if printf '%s\n' "$out" | grep -qE 'uncaught exception|thrown:'; then
    say 1 "nothing was thrown"
else
    say 0 "nothing was thrown"
fi

# The last presented frame must still be the red block.
last="$(ls "$DUMP"/*.ppm 2>/dev/null | sort | tail -1)"
if [ -z "$last" ]; then
    say 1 "the run presented a frame at all"
else
    say 0 "the run presented frames ($(ls "$DUMP"/*.ppm | wc -l) of them)"
    red="$(python3 - "$last" <<'PY'
import sys
path = sys.argv[1]
data = open(path, 'rb').read()
i = data.index(b'255\n') + 4
px = data[i:]
n = 0
for k in range(0, len(px) - 2, 3):
    if px[k] > 200 and px[k + 1] < 60 and px[k + 2] < 60:
        n += 1
print(n)
PY
)"
    say $([ "${red:-0}" -ge 100 ] && echo 0 || echo 1) \
        "the last presented frame still holds the drawn block (${red:-0} red pixels)"
fi

if [ "$fail" = 0 ]; then
    echo "PASS check_repaint"
else
    echo "FAILED check_repaint"
fi
exit "$fail"
