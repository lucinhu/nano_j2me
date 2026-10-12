#!/bin/bash
#
# A MIDlet that runs its own frame loop inside startApp() must still present
# frames.
#
# 仙剑奇侠传完美版 does exactly this and presented none: Thread.sleep() parked
# the calling C bridge, which spun until the sleep was over, so the runner
# never got a chance to draw.  The VM's frame hook exists for this, and the
# fixture below is the same shape with an infinite loop - which the real title
# also has, so without the hook it never presents anything and never exits.
#
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/loopdemo.jar}"
FRAMES="${NJ_FRAMES:-20}"
[ -x "$BIN" ] || { echo "no emulator at $BIN" >&2; exit 1; }
[ -f "$JAR" ] || { echo "no fixture at $JAR" >&2; exit 1; }

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

timeout 60 "$BIN" --port headless --run-frames "$FRAMES" \
    --dump-frames "$out/" "$JAR" >/dev/null 2>&1
rc=$?
n=$(ls "$out" | wc -l)
fail=0
note() { printf '  %-4s %s\n' "$1" "$2"; }

if [ "$rc" -eq 124 ]; then
    note FAIL "the MIDlet never let the runner draw: the run timed out"
    fail=1
elif [ "$rc" -ne 0 ]; then
    note FAIL "the run exited $rc"
    fail=1
fi
if [ "$n" -lt 1 ]; then
    note FAIL "no frame was presented while startApp() was inside its loop"
    fail=1
else
    note ok "$n frame(s) presented while startApp() was still in its own loop"
fi

[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
