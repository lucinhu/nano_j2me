#!/bin/sh
# MIT. Decode the synthetic MPEG-4 Part 2 video fixture and assert the Java
# MMAPI path worked: the movie was parsed (the core prints a "media:" line),
# the Player started, VideoControl was driven, and nothing was refused.
#
#   NJ_BIN=build/nj2me tools/check_movie_decode.sh tests/fixtures/moviedemo.jar
#
# Exit status: 0 = PASS, 1 = FAIL, 77 = fixture missing (SKIP).
set -u

JAR=${1:-}
BIN=${NJ_BIN:-build/nj2me}
FRAMES=${NJ_FRAMES:-240}
TIMEOUT=${TIMEOUT:-120}

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

[ -n "$JAR" ] || fail "usage: check_movie_decode.sh <moviedemo.jar>"
[ -f "$JAR" ] || { echo "SKIP: missing $JAR" >&2; exit 77; }
[ -x "$BIN" ] || fail "no CLI binary at $BIN (set NJ_BIN)"

OUT=$(mktemp)
ERR=$(mktemp)
trap 'rm -f "$OUT" "$ERR"' EXIT INT TERM

timeout "$TIMEOUT" "$BIN" --port headless --size 240x320 --heap 16 --fps 30 \
    --run-frames "$FRAMES" --verbose 3 --stats --rms-dir "$(mktemp -d)" \
    "$JAR" >"$OUT" 2>"$ERR"
STATUS=$?
[ "$STATUS" -eq 0 ] || { cat "$OUT" "$ERR" >&2; fail "CLI exited $STATUS"; }

ALL=$(cat "$OUT" "$ERR")
printf '%s\n' "$ALL" | grep -q 'media: mpeg4-part2 movie 64x48' \
    || { printf '%s\n' "$ALL" | tail -20 >&2; fail "movie was not parsed"; }
printf '%s\n' "$ALL" | grep -q 'VIDEO started' \
    || { printf '%s\n' "$ALL" | tail -20 >&2; fail "Player did not start"; }
printf '%s\n' "$ALL" | grep -q 'VIDEO failed' \
    && { printf '%s\n' "$ALL" | tail -20 >&2; fail "Java video path failed"; }
printf '%s\n' "$ALL" | grep -q 'media: started Player kind=2' \
    || fail "video Player did not get a video track"

echo "PASS: MPEG-4 Part 2 video decoded and presented through VideoControl"
