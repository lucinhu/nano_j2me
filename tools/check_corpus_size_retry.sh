#!/bin/bash
# The corpus must not write a title off after one screen size.
#
# tests/sizedemo/SizeDemo.java draws two colours at 240x320 and one at
# 176x208.  With 176x208 listed first, a corpus that stops at the first size
# that merely starts reports "flat frame: the game is not drawing" - which is
# what happened to 宠物王国2紫_5550 and several others.  The assertion is
# therefore about which SIZE the corpus settled on, not just the verdict.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAR="${1:-$ROOT/tests/fixtures/sizedemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }

dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT
cp "$JAR" "$dir/"

out="$(NJ_FRAMES="${NJ_FRAMES:-60}" NJ_SIZES="176x208 240x320" \
        "$ROOT/tools/run_game_corpus.sh" --dir "$dir" 2>/dev/null \
        | grep sizedemo || true)"

echo "$out"
case "$out" in
    *"240x320"*PASS*) ;;
    *) echo "FAIL: the corpus did not fall through to the size that draws"; exit 1 ;;
esac
case "$out" in
    *"176x208"*) echo "FAIL: it settled on the first size that merely started"; exit 1 ;;
    *) ;;
esac
echo "PASS: the corpus retried past the size where the game draws nothing"
