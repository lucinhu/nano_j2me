#!/bin/bash
# The clip is a rectangle of pixels: translate() must not move it.
#
# 帝国时代3-亚洲王朝_6288 draws its campaign map by translating the origin by the
# camera offset and then blitting tiles at map coordinates.  The runtime used to
# drag the clip along with the origin, so every tile was clipped away: the game
# ran, the HUD drew, and the whole map was black.  tests/test_clip.c pins the
# pixel layer; this pins the same rule through the MIDP calls a title actually
# makes, including the getClipX/... round trip the javadoc requires.
#
# The fixture prints one line and this file asserts every field of it:
#
#   a  setClip(0,0,8,8) then translate(8,8): nothing drawn at (8,8) or (15,15)
#   b  translate(8,8) then setClip(0,0,8,8): confined to (8..15, 8..15)
#   c  an empty clip discards the drawing
#   d  clip read back and re-applied with the same translate: same pixels
#   cx -8 (device 0 relative to the moved origin), cw 8, tx 8
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/clipdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

timeout 60 "$BIN" --port headless --run-frames 3 "$JAR" >"$log" 2>&1
rc=$?
line="$(grep -m1 '^CLIP ' "$log" || true)"

if [ "$rc" != 0 ]; then
    echo "FAIL: the emulator exited $rc"
    fail=1
fi
case "$line" in
    "CLIP FAILED"*) echo "FAIL: the fixture itself threw: $line"; fail=1 ;;
    "") echo "FAIL: the fixture printed no CLIP line"; sed -n '1,20p' "$log"; fail=1 ;;
esac

want="CLIP a=true b=true c=true d=true cx=-8 cw=8 tx=8"
if [ "$line" != "$want" ]; then
    echo "FAIL: the clip does not follow the specification"
    echo "      got:  $line"
    echo "      want: $want"
    fail=1
fi

if grep -q "uncaught exception" "$log"; then
    echo "FAIL: an exception escaped a thread"
    fail=1
fi

[ "$fail" = 0 ] || exit 1
echo "PASS: translate() moves the coordinate system and leaves the clip where it was"
