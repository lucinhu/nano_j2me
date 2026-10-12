#!/usr/bin/env bash
#
# Does the emulator wait for a Displayable that arrives from a thread?
#
# The runner used to check exactly once, straight after startApp() returned,
# and give up: "the MIDlet installed no Displayable; nothing to show".  A real
# title (Prehistoric Tribes) installs its Displayable from a thread it starts in
# startApp(), so that check declared a working game broken after eighteen
# bytecodes.
#
# The fixture sleeps 20 ms inside the thread before calling setCurrent(), so a
# runtime that only looks once cannot pass this by accident - which is the
# whole reason the delay is there.
#
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:?usage: check_late_display.sh <nj2me> <latedemo.jar>}"
JAR="${2:?usage: check_late_display.sh <nj2me> <latedemo.jar>}"
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

fails=0
fail() { echo "FAIL: $*" >&2; fails=$((fails+1)); }

timeout 60 "$BIN" --port headless --heap 8 --size 176x208 --run-frames 60 \
        --stats "$JAR" >/dev/null 2>"$LOG"

if grep -q "installed no Displayable" "$LOG"; then
    fail "the Displayable arrived, but the runner gave up before it did"
fi
frames="$(grep -o 'frames=[0-9]*' "$LOG" | head -1 | cut -d= -f2)"
if [ "${frames:-0}" != "60" ]; then
    fail "ran ${frames:-0} frames, expected 60"
fi
if ! grep -q "arrived .* frame(s) after startApp()" "$LOG"; then
    fail "no note about the late Displayable: the wait may not have happened"
fi
if grep -qiE "exception|error" "$LOG"; then
    fail "the run reported an exception"
fi

if [ "$fails" -eq 0 ]; then
    echo "PASS: waited for a Displayable installed from a thread ($frames frames)"
else
    echo "$fails check(s) failed" >&2
fi
exit $(( fails > 0 ))
