#!/bin/sh
# The Nokia UI API fixture: the vendor calls the collected titles depend on
# most (FullCanvas 83 titles, DirectGraphics 74, DirectUtils 73, Sound 14,
# DeviceControl 10) must answer with something a game can use, not only exist.
#
# Each assertion below is a value, not a call that returned: the triangle's top
# edge is red where it was drawn, a byte pixel reads back as its own grey, an
# ARGB int survives the round trip, the PNG the fixture carries decodes, and the
# sound and device-control calls leave the object in the state the API says.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/nokiademo.jar}"
FAILS=0

say() { printf '%s\n' "$*"; }
fail() { FAILS=$((FAILS + 1)); say "FAIL: $*"; }

if [ ! -f "$JAR" ]; then
    say "SKIP: $JAR is not built (run ninja nj_fixtures)"
    exit 0
fi

OUT=$(timeout 90 "$BIN" --port headless --heap 16 --size 176x208 --fps 0 \
      --run-frames 5 "$JAR" 2>&1) || fail "the run exited non-zero"

printf '%s\n' "$OUT" | grep -q 'NOKIA img=16x16 edge=ffff0000 outside=ffffffff alpha=255' \
    || fail "drawTriangle did not draw its top edge, or getAlphaComponent did not answer 255"
printf '%s\n' "$OUT" | grep -q 'NOKIA bytes=ff808080 read=128' \
    || fail "the byte[] drawPixels/getPixels pair did not round trip the grey 0x80"
printf '%s\n' "$OUT" | grep -q 'NOKIA argb=ff112233' \
    || fail "the int[] drawPixels overload did not keep an ARGB colour"
printf '%s\n' "$OUT" | grep -q 'NOKIA png=2x2' \
    || fail "DirectUtils.createImage(byte[], int, int) did not decode the PNG in the jar"
printf '%s\n' "$OUT" | grep -q 'NOKIA snd=0,50,0,2,1' \
    || fail "Sound did not report SOUND_PLAYING (0), keep gain 50, resume, list two formats and one channel"
printf '%s\n' "$OUT" | grep -q 'NOKIA devctl=ok' \
    || fail "a DeviceControl call threw"

case "$OUT" in
    *"thrown:"*|*uncaught*|*NoSuchMethodError*|*NoSuchFieldError*)
        fail "an exception or a missing member appeared: $(printf '%s\n' "$OUT" | grep -m1 -E 'thrown:|uncaught|NoSuch')" ;;
esac

if [ "$FAILS" -eq 0 ]; then
    say "the Nokia UI API answers as the API documents"
    exit 0
fi
say "$FAILS check(s) failed"
exit 1
