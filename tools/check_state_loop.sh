#!/bin/bash
#
# A manual snapshot must work on a title that runs its frame loop inside
# startApp().
#
# 仙剑奇侠传完美版 does exactly that and 2026-11-14 could not be snapshotted
# from the CLI at all: the synchronous startApp() call never returns while the
# title sleeps between frames, the frame hook spends the whole budget, and the
# runner's frame loop - the only place --save-state was honoured - is never
# entered.  The runner now starts the MIDlet as a VM thread when a snapshot was
# asked for, so threads are parked at frame boundaries and the state can be
# written and loaded back.  loopdemo.jar is the fixture with the same shape.
#
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/loopdemo.jar}"
FRAMES="${NJ_FRAMES:-60}"
SAVE_AT="${NJ_SAVE_AT:-40}"
RESUME="${NJ_RESUME:-30}"
[ -x "$BIN" ] || { echo "no emulator at $BIN" >&2; exit 1; }
[ -f "$JAR" ] || { echo "no fixture at $JAR" >&2; exit 1; }

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
state="$out/loopdemo.state"
fail=0

run() { timeout 120 "$BIN" --port headless "$@" 2>&1; }

save_out="$(run --run-frames "$FRAMES" --save-state "$state" \
    --save-state-frame "$SAVE_AT" --stats "$JAR")"
save_rc=$?

restore_out="$(run --run-frames "$RESUME" --load-state "$state" \
    --stats "$JAR")"
restore_rc=$?

note() { printf '  %-4s %s\n' "$1" "$2"; }

if [ "$save_rc" -ne 0 ]; then
    note FAIL "the saving run exited $save_rc"
    fail=1
fi
if [ ! -s "$state" ]; then
    note FAIL "no state file was written"
    fail=1
else
    note pass "state written ($(wc -c < "$state") bytes)"
fi
if ! printf '%s\n' "$save_out" | grep -q "^saved state to "; then
    note FAIL "the run did not log that a state was saved"
    fail=1
fi
if [ "$restore_rc" -ne 0 ]; then
    note FAIL "the restoring run exited $restore_rc"
    fail=1
fi
if ! printf '%s\n' "$restore_out" | grep -q "resumed LoopDemo from "; then
    note FAIL "the state was not resumed (expected 'resumed ... from')"
    fail=1
else
    note pass "state resumed"
fi
if ! printf '%s\n' "$restore_out" | grep -qE "frames=$RESUME\b"; then
    note FAIL "the resumed run did not advance $RESUME frame(s)"
    fail=1
else
    note pass "resumed run advanced $RESUME frame(s)"
fi
if printf '%s\n' "$restore_out" | grep -q "cannot resume from "; then
    note FAIL "the loader refused the state it had just written"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILED"
    printf '%s\n' "$save_out" | tail -5
    printf '%s\n' "$restore_out" | tail -5
    exit 1
fi
echo "PASS"
exit 0
