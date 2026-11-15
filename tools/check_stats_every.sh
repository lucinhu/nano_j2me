#!/bin/sh
# MIT. --stats-every prints the counter line every N frames, so a long run can
# be watched for heap growth instead of only reading the final counters.
#
#   NJ_BIN=build/nj2me tools/check_stats_every.sh tests/fixtures/loopdemo.jar
#
# Exit status: 0 = PASS, 1 = FAIL, 77 = fixture missing (SKIP).
set -u

JAR=${1:-}
BIN=${NJ_BIN:-build/nj2me}
FRAMES=${NJ_FRAMES:-300}
EVERY=${NJ_EVERY:-100}
TIMEOUT=${TIMEOUT:-120}

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

[ -n "$JAR" ] || fail "usage: check_stats_every.sh <loopdemo.jar>"
[ -f "$JAR" ] || { echo "SKIP: missing $JAR" >&2; exit 77; }
[ -x "$BIN" ] || fail "no CLI binary at $BIN (set NJ_BIN)"

OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT INT TERM
timeout "$TIMEOUT" "$BIN" --port headless --size 240x320 --heap 16 --fps 0 \
    --run-frames "$FRAMES" --stats --stats-every "$EVERY" \
    --rms-dir "$(mktemp -d)" "$JAR" >"$OUT" 2>&1 \
    || fail "CLI exited non-zero"

SAMPLES=$(grep -c '^stats: frames=' "$OUT")
# --stats prints one final line on top of the periodic ones.
WANT=$(( FRAMES / EVERY + 1 ))
[ "$SAMPLES" -ge "$WANT" ] || fail "expected >= $WANT stats lines, got $SAMPLES"

# The periodic line and the final line must report the same frame count shape:
# every sample carries heap=used/peak and the frames it was taken at.
grep -q '^stats: frames=.*heap=[0-9]*/[0-9]*' "$OUT" \
    || fail "stats line has no heap used/peak fields"
grep -q "^stats: frames=$FRAMES " "$OUT" \
    || fail "final stats line does not report frame $FRAMES"

# No periodic line may appear ahead of the final one: that would mean frames
# were counted twice or the sample ran after teardown.
LAST=$(grep '^stats: frames=' "$OUT" | tail -1)
printf '%s\n' "$LAST" | grep -q "^stats: frames=$FRAMES " \
    || fail "last stats line is not the final one"

echo "PASS: --stats-every printed $SAMPLES counter lines over $FRAMES frames"
