#!/bin/bash
# The JSR 184 surface is only complete if it behaves.
#
# tests/m3gdemo/M3GDemo.java runs one check per API area - object lifetimes,
# transforms, cameras, textures, vertex data, lights, the loader - and prints a
# single summary line.  This file asserts that line.  Writing it found fourteen
# defects in the surface that the API audit counted as present, among them
# Group children that getReferences() never reported, a VertexArray that
# answered BYTE for a two-byte component, a Sprite3D constructor that stored
# nothing, and Graphics3D.setLight() that counted a light without keeping it.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/m3gdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

timeout 120 "$BIN" --port headless --heap 8 --size 176x208 --fps 30 \
        --run-frames 10 "$JAR" >"$log" 2>&1
rc=$?
line="$(grep -E '^M3G checks=' "$log" | tail -1)"
echo "${line:-<no summary line>}"

[ "$rc" = 0 ] || { echo "FAIL: the run exited $rc"; fail=1; }
if [ -z "$line" ]; then
    echo "FAIL: the fixture printed no summary line"
    sed -n '1,20p' "$log"
    fail=1
elif ! printf '%s' "$line" | grep -q 'failures=0'; then
    echo "FAIL: the fixture reported failures"
    grep -E '^M3G FAIL' "$log" | head -10
    fail=1
fi

checks="$(printf '%s' "${line:-}" | sed -n 's/.*checks=\([0-9]*\).*/\1/p')"
if [ "${checks:-0}" -lt 100 ]; then
    echo "FAIL: only ${checks:-0} checks ran; the fixture did not finish"
    fail=1
fi

[ "$fail" = 0 ] && echo "PASS: ${line}"
exit $fail
