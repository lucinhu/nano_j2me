#!/bin/bash
# DataInputStream.readUTF(DataInput) must decode what writeUTF() wrote.
#
# The static form was missing entirely - 世界冠军拉力赛3D_8360 threw
# NoSuchMethodError at h.a+497 and never drew a frame.  It shares one decoder
# with the instance form, so the assertion is that the two agree AND that the
# bytes really are multi-byte modified UTF-8: a fixture whose text is ASCII
# would pass a decoder that only ever handles single bytes.
#
# The code point is checked as a number.  Whether this machine's console can
# print U+00E9 is a property of the console, not of the decoder.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/utfdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
timeout 60 "$BIN" --port headless --run-frames 3 "$JAR" >"$log" 2>&1
line="$(grep -m1 '^UTF ' "$log" || true)"
echo "$line"
fail=0
case "$line" in
    *"bytes=26"*) ;;
    *) echo "FAIL: writeUTF did not produce 26 bytes of modified UTF-8"; fail=1 ;;
esac
case "$line" in
    *"same=true"*) ;;
    *) echo "FAIL: the static and instance forms disagree"; fail=1 ;;
esac
case "$line" in
    *"matches=true"*) ;;
    *) echo "FAIL: the decoded text is not the text that was written"; fail=1 ;;
esac
case "$line" in
    *"len=22"*) ;;
    *) echo "FAIL: 22 characters went in and a different number came out"; fail=1 ;;
esac
case "$line" in
    *"code=233"*) ;;
    *) echo "FAIL: the multi-byte character did not decode to U+00E9"; fail=1 ;;
esac
[ "$fail" = 0 ] || exit 1
echo "PASS: both readUTF() forms decode multi-byte modified UTF-8 identically"
