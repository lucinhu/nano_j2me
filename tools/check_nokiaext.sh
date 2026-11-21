#!/usr/bin/env bash
# Exercise the round-154 Nokia UI extras through real bytecode.
#
#   tools/check_nokiaext.sh [JAR] [BIN]
#
# Asserts every marker, and fails on thrown exceptions, missing classes or a
# crash.  Exits 77 (ctest SKIP) when the fixture JAR is absent.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
jar="${1:-$here/tests/fixtures/nokiaextdemo.jar}"
bin="${2:-${NJ_BIN:-$here/build/nj2me}}"
frames="${FRAMES:-90}"
[ -f "$jar" ] || { echo "SKIP: $jar is missing"; exit 77; }
[ -x "$bin" ] || { echo "FAIL: emulator binary not found: $bin" >&2; exit 1; }

out="$(mktemp)"
trap 'rm -f "$out"' EXIT
"$bin" --port headless --size 240x320 --heap 16 --fps 30 \
    --run-frames "$frames" --verbose 2 "$jar" >"$out" 2>&1 || true

fail=0
have() {
    if ! grep -qF -- "$1" "$out"; then
        echo "FAIL missing marker: $1" >&2
        fail=1
    fi
}
have 'NOKIAEXT marker=true'
have 'NOKIAEXT clipboard=hello nano'
have 'NOKIAEXT clipboard_cleared=true'
have 'NOKIAEXT gi_constants=274/260'
have 'NOKIAEXT gi old=0 now=2'
have 'NOKIAEXT gi invalid=caught'
have 'NOKIAEXT tactile supported=false style=2'
have 'NOKIAEXT tactile invalid=caught'
have 'NOKIAEXT sn id=42'
have 'NOKIAEXT sn done=true'
have 'NOKIAEXT gesture type=9 registered=false'
grep -qE 'NOKIAEXT .*failed=' "$out" && { echo "FAIL fixture reported failure:" >&2; grep 'failed=' "$out" >&2; fail=1; }
# The fixture deliberately provokes two exceptions and catches them: both must
# appear and nothing else may escape.
grep -qF 'thrown: com/nokia/mid/ui/GlobalIndicatorsException' "$out" || {
    echo "FAIL the invalid indicator did not raise GlobalIndicatorsException" >&2; fail=1; }
grep -qF 'thrown: java/lang/IllegalArgumentException' "$out" || {
    echo "FAIL the invalid tactile style did not raise IllegalArgumentException" >&2; fail=1; }
grep -qE 'uncaught exception|NoSuchMethodError|NoClassDefFoundError|Exception in thread' "$out" && {
    echo "FAIL unexpected exception:" >&2
    grep -E 'uncaught exception|NoSuchMethodError|NoClassDefFoundError|Exception in thread' "$out" | head -5 >&2
    fail=1
}
if [ "$fail" -ne 0 ]; then
    echo "--- fixture output ---" >&2
    tail -30 "$out" >&2
    exit 1
fi
echo "PASS: Nokia UI extras (Clipboard, indicators, tactile, soft notifications, gestures, marker)"
