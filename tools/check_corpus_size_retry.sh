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

# Second case: a title that ends inside one dump interval.  At 128x160 the same
# fixture draws two colours and calls notifyDestroyed() after three paints, so
# a 1-in-40 dump samples none of them and the verdict becomes "nothing was ever
# presented" - the failure that took seven Apress demos off the pass list when
# the smoke key set started ending them early.  Nothing was dumped is not the
# same as nothing drawn, and the corpus has to ask again before saying so.
out128="$(NJ_FRAMES="${NJ_FRAMES:-60}" NJ_SIZES="128x160" \
        "$ROOT/tools/run_game_corpus.sh" --dir "$dir" 2>/dev/null \
        | grep sizedemo || true)"
echo "$out128"
case "$out128" in
    *"nothing was ever presented"*) echo "FAIL: it believed a 3-frame title never drew"; exit 1 ;;
    *PASS*) ;;
    *) echo "FAIL: the dense-dump retry did not rescue the short run"; exit 1 ;;
esac
echo "PASS: a title that ends inside the dump interval is still measured"
