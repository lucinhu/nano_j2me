#!/bin/bash
# A MIDlet whose timer task parks must keep running - and keep painting.
#
# A java.util.Timer task runs on the timer thread, which reaches it through a
# native (nj2me/util/TimerThread.pump).  While the task is parked, that thread
# is blocked inside a C call whose top frame is the native, and a native frame
# has no bytecode: a scheduler that runs the thread there fetches an opcode out
# of an empty method.  Five collected titles (电子宠物蛋_11972, 极品飞车16：亡命
# 狂飙_1519, 纽约人生_1339, 实况足球2008中文版_9284, 倚天屠龙记_4827) failed
# exactly that way with
#
#   VerifyError: code runs past the end of the method
#   at nj2me/util/TimerThread.pump pc=0
#
# while the title was painting perfectly well before the park.  What this
# check regresses:
#
#   TIMER task done ticks=<n>    the parked task was resumed and returned
#   TIMER painted=5 ticks=<n>    the canvas is live while the task is parked
#
# and the absence of the VerifyError, of an escaped exception, and of the
# scheduler's "all threads are waiting" verdict - which used to fire because
# the parked thread's own sleep deadline was skipped instead of being counted.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/timerdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
out="$(timeout 90 "$BIN" --port headless --heap 8 --size 176x208 --fps 30 \
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

case "$out" in
    *"running TimerDemo"*) say 0 "the MIDlet started" ;;
    *) say 1 "the MIDlet never started" ;;
esac

[ "$rc" = 0 ]; say $? "the run exits cleanly (exit $rc)"

case "$out" in
    *"TIMER task done ticks="*) say 0 "the parked timer task resumed and returned" ;;
    *) say 1 "the parked timer task never finished" ;;
esac

case "$out" in
    *"TIMER painted=5"*) say 0 "the canvas painted five frames while the task was parked" ;;
    *) say 1 "the canvas did not reach its fifth frame" ;;
esac

case "$out" in
    *"VerifyError"*) say 1 "a VerifyError was thrown (the native frame was executed as bytecode)" ;;
    *) say 0 "no VerifyError was thrown" ;;
esac

case "$out" in
    *"all threads are waiting"*) say 1 "the scheduler gave up while a thread was parked in a C call" ;;
    *) say 0 "the scheduler did not give up on the parked thread" ;;
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

[ "$fail" = 0 ] && echo "PASS: a timer task that parks keeps the MIDlet running"
exit "$fail"
