#!/bin/bash
# synchronized has to mean mutual exclusion.
#
# A cooperatively scheduled thread is not preempted by the scheduler, but the
# bytecode quantum still runs out - including inside a critical section.  When
# monitorenter was only recorded on the frame and never blocked, two threads
# ran the same synchronized block at once and LockDemo printed overlap=true.
# This check regresses that:
#
#   LOCK counter=400 overlap=false
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/lockdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
out="$(timeout 120 "$BIN" --port headless --heap 8 --size 176x208 --fps 30 \
        --run-frames 600 --stats "$JAR" 2>&1)"
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
    *"LOCK start"*) say 0 "the MIDlet started" ;;
    *) say 1 "the MIDlet never started" ;;
esac

case "$out" in
    *"LOCK counter=400 overlap=false"*) say 0 "threads never shared the monitor" ;;
    *"overlap=true"*) say 1 "two threads ran the same critical section" ;;
    *) say 1 "the run never reached its verdict" ;;
esac

case "$out" in
    *"all threads are waiting"*) say 1 "the run deadlocked" ;;
    *) say 0 "no deadlock was reported" ;;
esac

case "$out" in
    *"uncaught exception"*) say 1 "an exception escaped a thread" ;;
    *) say 0 "no exception escaped a thread" ;;
esac

# A synchronized Canvas.paint() is invoked through the C bridge, not through
# the interpreter's invoke opcodes.  notifyAll() inside it is the ownership
# probe: it throws IllegalMonitorStateException unless the callback already
# owns the monitor.  The runtime used to call Java callbacks from C without
# taking it, so this threw once per frame.
case "$out" in
    *"IllegalMonitorStateException"*)
        say 1 "a synchronized callback ran without its monitor" ;;
    *)
        say 0 "synchronized callbacks own their monitor" ;;
esac

paints="$(printf '%s\n' "$out" | sed -n 's/.*paints=\([0-9][0-9]*\).*/\1/p' | tail -1)"
[ -n "$paints" ] && [ "$paints" -gt 0 ]
say $? "the synchronized paint() ran (${paints:-0} frames)"

[ "$fail" = 0 ] && echo "PASS: synchronized excludes other threads"
exit "$fail"
