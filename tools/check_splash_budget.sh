#!/bin/sh
# A title may sleep through a splash screen in its constructor, which means the
# frame hook spends the embedder's whole budget before the constructor returns.
# The run then ends on purpose - and it has to say so.  It used to say "cannot
# instantiate <class>", which reads as a missing class and is not one; two
# Need for Speed titles were chased for a round on the strength of it.
#
# Usage: check_splash_budget.sh <emulator> <splash-midlet.jar>
set -u
EMU="$1"
JAR="$2"
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

# One frame: the budget is certainly spent while <init> is still asleep.
"$EMU" --port headless --run-frames 1 --size 176x208 "$JAR" >"$LOG" 2>&1
rc=$?
fail=0

if grep -q "cannot instantiate" "$LOG"; then
    echo "FAIL: the run ended on purpose but was reported as a failed construction"
    grep "cannot instantiate" "$LOG"
    fail=1
fi
if ! grep -q "run ended: .*frame budget ran out" "$LOG"; then
    echo "FAIL: the run did not say the frame budget ended it"
    grep -E "run ended|cannot instantiate|^running" "$LOG" || true
    fail=1
fi
if [ "$rc" -ne 0 ]; then
    echo "FAIL: a budget that ran out is not a failure, but the exit code was $rc"
    fail=1
fi
[ "$fail" -eq 0 ] && echo "PASS: a splash that outlasts the budget is reported as the budget ending, not as a broken title"
exit "$fail"
