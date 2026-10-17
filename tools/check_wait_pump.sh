#!/bin/bash
# A MIDlet constructor that waits on another thread must still start.
#
# The runtime enters a MIDlet through C - the AMS side calls the constructor -
# and a constructor is allowed to park: 毁灭战士3_10598 starts a worker and
# waits for it while it builds the level.  A parked thread whose frames are
# intact is not a failure, and the thread that will wake it is a Java thread,
# so the C caller has to keep the VM running rather than report a timeout.  It
# used to report "construction blocked (it called Thread.sleep or wait, and a C
# caller cannot be suspended)", which is what this check regresses:
#
#   WAIT ready=true paints=0     the constructor's wait ended
#   WAIT painted=5 ready=true    the canvas is live afterwards
#
# The second line matters as much as the first: a runtime that abandons the
# parked frame instead of resuming it can still print the first one.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/waitdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
out="$(timeout 60 "$BIN" --port headless --heap 8 --size 176x208 --fps 30 \
        --run-frames 30 --stats "$JAR" 2>&1)"
rc=$?

say() {
    if [ "$1" = 1 ]; then
        echo "FAIL $2"
        fail=1
    else
        echo "ok   $2"
    fi
}

[ "$rc" = 0 ]; say $? "the run exits cleanly (exit $rc)"

case "$out" in
    *"WAIT ready=true"*) say 0 "the constructor's wait ended" ;;
    *) say 1 "the constructor never got past its wait" ;;
esac

case "$out" in
    *"WAIT painted=5 ready=true"*) say 0 "the canvas painted five frames" ;;
    *) say 1 "the canvas did not reach its fifth frame" ;;
esac

case "$out" in
    *"construction blocked"*) say 1 "the run reported a blocked construction" ;;
    *) say 0 "no blocked construction was reported" ;;
esac

case "$out" in
    *"uncaught exception"*) say 1 "an exception escaped a thread" ;;
    *) say 0 "no exception escaped a thread" ;;
esac

frames="$(printf '%s\n' "$out" | sed -n 's/.*paints=\([0-9]*\).*/\1/p' | tail -n 1)"
if [ -n "$frames" ] && [ "$frames" -ge 30 ]; then
    say 0 "the frame budget was spent ($frames paints)"
else
    say 1 "the run painted ${frames:-0} frames, expected at least 30"
fi

# A sleeping worker can legitimately take longer than the bridge's old 5s cap.
long_out="$(timeout 30 "$BIN" --port headless --heap 8 --fps 30 \
    --prop fixture.wait.ms=6000 --run-frames 220 "$JAR" 2>&1)"
long_rc=$?
[ "$long_rc" = 0 ]; say $? "the six-second constructor wait exits cleanly"
case "$long_out" in
    *"WAIT painted=5 ready=true"*) say 0 "the long wait resumes and paints" ;;
    *) say 1 "the long wait failed to resume" ;;
esac

cancel_out="$(timeout 15 "$BIN" --port headless --heap 8 --fps 30 \
    --prop fixture.wait.ms=6000 --run-frames 30 "$JAR" 2>&1)"
cancel_rc=$?
[ "$cancel_rc" = 0 ]; say $? "frame-budget cancellation ends the constructor pump"
case "$cancel_out" in
    *"WAIT ready=true"*) say 1 "construction continued after cancellation" ;;
    *) say 0 "the cancelled constructor did not start the MIDlet" ;;
esac

[ "$fail" = 0 ] && echo "PASS: a MIDlet whose constructor waits for another thread starts"
exit "$fail"
