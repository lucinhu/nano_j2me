#!/usr/bin/env bash
# Assert Class.forName() initialises the class (JVMS 5.5).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-${NJ_BIN:-$ROOT/build/nj2me}}"
JAR="${2:-$ROOT/tests/fixtures/classinitdemo.jar}"
if [ ! -x "$BIN" ]; then echo "SKIP classinit: no binary at $BIN"; exit 77; fi
if [ ! -f "$JAR" ]; then echo "SKIP classinit: no fixture at $JAR"; exit 77; fi
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT
timeout 60 "$BIN" --port headless --size 240x320 --heap 8 --fps 30 \
    --run-frames 200 --rms-dir "$(mktemp -d)" "$JAR" >"$LOG" 2>&1
rc=$?
fail=0
check() { if [ "$1" -ne 0 ]; then echo "FAIL classinit: $2"; fail=1; fi; }
grep -q "CLASSINIT before" "$LOG"; check $? "fixture did not start"
grep -q "CLASSINIT value=7" "$LOG"; check $? "static field VALUE did not read 7"
grep -q "CLASSINIT probe-init" "$LOG"; check $? "InitProbe initialiser never ran"
# The initialiser must run *while* Class.forName executes: probe-init has to
# appear after "CLASSINIT before" and before "CLASSINIT forName returned=".
init_line=$(grep -n "CLASSINIT probe-init" "$LOG" | head -1 | cut -d: -f1)
ret_line=$(grep -n "CLASSINIT forName returned=" "$LOG" | head -1 | cut -d: -f1)
before_line=$(grep -n "CLASSINIT before" "$LOG" | head -1 | cut -d: -f1)
if [ -n "$init_line" ] && [ -n "$ret_line" ] && [ -n "$before_line" ]; then
    [ "$init_line" -gt "$before_line" ]; check $? "initialiser ran before the fixture started"
    [ "$init_line" -lt "$ret_line" ]; check $? "Class.forName returned before the class was initialised"
fi
grep -qE "uncaught exception" "$LOG"; [ $? -ne 0 ]; check $? "uncaught exception"
grep -qE "NoClassDefFoundError|NoSuchMethodError" "$LOG"; [ $? -ne 0 ]; check $? "linkage error"
if [ $rc -ne 0 ]; then fail=1; echo "FAIL classinit: run exited rc=$rc"; fi
if [ $fail -eq 0 ]; then echo "PASS classinit: Class.forName initialises the class"; fi
exit $fail
