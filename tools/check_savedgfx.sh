#!/bin/bash
# Drawing through the Graphics paint() handed out, after paint() returned.
#
# tests/savedgfx/SavedGfx.java describes the fixture.  In one line:
#
#     paint(g) { saved = g; clear(); blueSquare(); }
#     worker   { saved.fillRect(red) every 6 ms }
#
# must leave the red block on the presented frame.  A runtime that mirrors the
# canvas buffer onto the screen at the end of paint() presents the black buffer
# every time and the red block never appears.
#
#   SAVED loop=<n> paints=<m>     the worker drew through the saved Graphics
#   the last presented frame holds the blue square AND the red block
#
# 合金弹头2013BT版_678 needed exactly this: it paints its HUD inside paint()
# and then keeps drawing the scenery behind the character through the Graphics
# it kept, so a mid-paint copy showed the frame half drawn - the scenery layer
# appeared on every other frame, which is the flicker the user reported.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/savedgfx.jar}"
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

line="$(printf '%s\n' "$out" | grep '^SAVED loop=' | tail -1 || true)"
say $([ -n "$line" ] && echo 0 || echo 1) "the worker drew through the saved Graphics"
loops="$(printf '%s\n' "$line" | sed -n 's/.*loop=\([0-9]*\).*/\1/p')"
paints="$(printf '%s\n' "$line" | sed -n 's/.*paints=\([0-9]*\).*/\1/p')"
say $([ -n "$loops" ] && [ "$loops" -ge 10 ] && echo 0 || echo 1) \
    "the worker kept drawing (loop=${loops:-none})"
say $([ -n "$paints" ] && [ "$paints" -ge 2 ] && echo 0 || echo 1) \
    "paint() ran (paints=${paints:-none})"

if printf '%s\n' "$out" | grep -qE 'uncaught exception|thrown:'; then
    say 1 "nothing was thrown"
else
    say 0 "nothing was thrown"
fi

last="$(ls "$DUMP"/*.ppm 2>/dev/null | sort | tail -1)"
if [ -z "$last" ]; then
    say 1 "the run presented a frame at all"
else
    say 0 "the run presented frames ($(ls "$DUMP"/*.ppm | wc -l) of them)"
    counts="$(python3 - "$last" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
i = data.index(b'255\n') + 4
px = data[i:]
red = blue = 0
for k in range(0, len(px) - 2, 3):
    if px[k] > 200 and px[k + 1] < 60 and px[k + 2] < 60:
        red += 1
    elif px[k + 2] > 200 and px[k] < 60 and px[k + 1] < 60:
        blue += 1
print(red, blue)
PY
)"
    red="$(printf '%s' "$counts" | awk '{print $1}')"
    blue="$(printf '%s' "$counts" | awk '{print $2}')"
    say $([ "${red:-0}" -ge 500 ] && echo 0 || echo 1) \
        "the last presented frame holds the block drawn after paint() (${red:-0} red pixels)"
    say $([ "${blue:-0}" -ge 30 ] && echo 0 || echo 1) \
        "and the square paint() drew (${blue:-0} blue pixels)"
fi

if [ "$fail" = 0 ]; then
    echo "PASS check_savedgfx"
else
    echo "FAILED check_savedgfx"
fi
exit "$fail"
