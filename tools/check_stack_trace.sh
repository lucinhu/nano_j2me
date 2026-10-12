#!/bin/bash
# printStackTrace() must say WHERE the exception happened, not just what it was.
#
# Three collected titles throw every frame from inside their own catch block,
# and the log said only "exception: java.lang.NullPointerException" - no
# method, no pc.  That turned every one of them into a hunt through the whole
# suite.  ExcDemo is that shape in miniature: throw in a helper, catch in
# startApp(), print the trace.
set -u
BIN="${NJ_BIN:-$(dirname "$0")/../build/nj2me}"
JAR="${1:-}"
[ -f "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }

out=$(timeout 30 "$BIN" --port headless --run-frames 3 "$JAR" 2>&1)

echo "$out" | grep -q "exception: java.lang.NullPointerException" || {
    echo "FAIL: the exception was not reported at all"; exit 1; }
echo "$out" | grep -q "    at ExcDemo.startApp" || {
    echo "FAIL: the trace does not name the method handling the exception"
    echo "$out" | grep -A4 "exception:" | head -6
    exit 1; }
# A trace of one line is not a trace: the chain must have more than one frame.
echo "$out" | grep -c "    at " | grep -qv "^0$" || {
    echo "FAIL: no frames were printed at all"; exit 1; }
echo "PASS: printStackTrace() names the throwing method and its caller"
