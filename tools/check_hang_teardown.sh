#!/bin/bash
# A MIDlet whose destroyApp() never returns must not hold the process open.
#
# 红色警戒3_7037 does exactly this, and before nj_vm_set_halt_after_ms() the
# emulator sat in teardown until the smoke test's timeout killed it - which is
# the one failure T3 does not allow.  tests/hangdemo/HangDemo.java is that
# MIDlet in miniature, so the regression is caught without the corpus.
#
# The assertion is the exit code and the elapsed time, not a log line: what
# matters is that the process ends, not what it says on the way out.
set -u
BIN="${NJ_BIN:-$(dirname "$0")/../build/nj2me}"
JAR="${1:-}"
[ -f "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }

start=$(date +%s)
out=$(timeout 30 "$BIN" --port headless --run-frames 3 "$JAR" 2>&1)
rc=$?
elapsed=$(( $(date +%s) - start ))

echo "$out" | grep -q "teardown: calling destroyApp()" || {
    echo "FAIL: the MIDlet never had destroyApp() called at all"; exit 1; }
[ "$rc" = "124" ] && { echo "FAIL: the process had to be killed after ${elapsed}s"; exit 1; }
[ "$elapsed" -gt 20 ] && { echo "FAIL: teardown took ${elapsed}s"; exit 1; }
echo "PASS: a destroyApp() that never returns is cut short (${elapsed}s, rc=$rc)"
