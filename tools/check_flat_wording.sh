#!/bin/bash
# The corpus may not describe a flat frame as something it did not measure.
#
# 合金弹头4(完美汉化版)_4053 asked Graphics for 60 calls over 20 frames and
# presented one colour: the game's own paint state machine draws nothing in the
# state it was sitting in.  The old verdict said "the game is not drawing",
# which sends the reader looking for a rendering bug that is not there.  The
# verdict must name the counts it saw instead.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAR="${1:-$ROOT/tests/fixtures/flatdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }

dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT
cp "$JAR" "$dir/"

out="$(NJ_FRAMES="${NJ_FRAMES:-20}" NJ_SIZES="240x320" NJ_MIN_COLOURS=2 \
        "$ROOT/tools/run_game_corpus.sh" --dir "$dir" 2>/dev/null \
        | grep flatdemo || true)"
echo "$out"

case "$out" in
    *"flat frame"*) ;;
    *) echo "FAIL: the fixture was not reported as a flat frame at all"; exit 1 ;;
esac
case "$out" in
    *"Graphics calls over"*) ;;
    *) echo "FAIL: the verdict did not report how many drawing calls it saw"; exit 1 ;;
esac
case "$out" in
    *"paint() calls"*) ;;
    *) echo "FAIL: the verdict did not report how many paint() calls it saw"; exit 1 ;;
esac
case "$out" in
    *"the game is not drawing"*)
        echo "FAIL: the verdict claims the game is not drawing, which it never measured"; exit 1 ;;
    *) ;;
esac
echo "PASS: the flat-frame verdict reports the counts it measured"
