#!/usr/bin/env bash
#
# A title that throws from paint() on every frame must still stop at the frame
# budget.
#
# The budget used to be compared against frames *painted*.  A title that never
# paints a frame therefore never reached it: the run went on for ever at the
# frame rate, and the corpus reported a hang that was the runner's own bug.
set -u
BIN="${NJ_BIN:-build/nj2me}"
JAR="${1:?usage: check_paint_budget.sh <jar>}"
out="$(timeout 60 "$BIN" --port headless --size 240x320 --heap 16 \
        --run-frames 8 --stats "$JAR" 2>&1)"
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL: the run did not end at the budget (rc=$rc)"
    exit 1
fi
if ! printf '%s\n' "$out" | grep -q "the frame budget ran out"; then
    echo "FAIL: the run ended without saying the budget is why"
    exit 1
fi
if ! printf '%s\n' "$out" | grep -q "paints=8"; then
    echo "FAIL: the runner did not attempt 8 frames"
    printf '%s\n' "$out" | grep -o "stats:.*" | head -1
    exit 1
fi
echo "ok: the budget ended a run whose paint() always throws"
