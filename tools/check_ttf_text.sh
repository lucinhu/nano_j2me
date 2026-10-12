#!/bin/bash
#
# --font really changes what is drawn.
#
# The unit tests cover the two halves separately: tests/test_ttf.c the
# rasteriser, tests/test_font_coverage.c the renderer's coverage path.  Neither
# of them can tell you whether the option is *wired* - whether a font file the
# user names actually reaches the glyphs a game draws.  That failure is silent
# by construction: a font that is parsed and then never used renders exactly
# like a font that was never named, and the program exits 0 either way.
#
# So this compares the frames of a real title with and without --font, and
# requires them to differ.  Identical frames mean the option did nothing,
# which is the whole failure mode being guarded against.
#
# It also checks the two paths that must NOT change: no --font at all (the
# built-in bitmap faces, byte for byte as before) and a font file that cannot
# be used (a warning and the built-in faces, not a refusal to run).
#
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/games/jump.jar}"
TTF="${2:-/usr/share/fonts/liberation/LiberationMono-Regular.ttf}"
SIZE="${NJ_SIZE:-176x208}"
FRAMES="${NJ_FRAMES:-40}"

[ -x "$BIN" ] || { echo "no emulator at $BIN" >&2; exit 1; }
[ -f "$JAR" ] || { echo "no jar at $JAR" >&2; exit 1; }
[ -f "$TTF" ] || { echo "no font at $TTF" >&2; exit 1; }

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
fail=0
# `${1-}`/`${2-}` rather than `$1`/`$2`: this script runs under `set -u`, and
# the one call site that prints a continuation line (the note under a failed
# --font case) passes a single argument.  With `$2` that turned "the font never
# reached a glyph" - the line that explains the failure - into
# "check_ttf_text.sh: line 35: $2: unbound variable", replacing the diagnosis
# with a crash.  A test harness must not be the thing that breaks first.
note() { printf '  %-4s %s\n' "${1-}" "${2-}"; }

# Run one case; sets rc, sig (digest of all dumped frames) and log.
run() {
    local dir="$1"; shift
    mkdir -p "$dir"
    timeout 90 "$BIN" --port headless --size "$SIZE" --run-frames "$FRAMES" \
        --dump-frames "$dir/" "$@" "$JAR" >"$dir.log" 2>&1
    rc=$?
    n=$(ls "$dir" 2>/dev/null | wc -l)
    sig=$(cat "$dir"/* 2>/dev/null | sha256sum | cut -c1-32)
}

echo "== $JAR at $SIZE, $FRAMES frames =="

# ---- 1. the default: built-in bitmap faces --------------------------------
run "$out/a"
if [ "$rc" -ne 0 ] || [ "$n" -lt 1 ]; then
    note FAIL "the run without --font produced nothing (rc=$rc, frames=$n)"
    sed -n '1,6p' "$out/a.log" | sed 's/^/       /'
    exit 1
fi
note OK "without --font: rc=0, $n frames, $sig"

# ---- 2. with a real font --------------------------------------------------
#
# The frames must differ.  A digest comparison rather than a pixel diff, so
# this stays a dozen lines and still catches the case that matters.
run "$out/b" --font "$TTF"
if [ "$rc" -ne 0 ]; then
    note FAIL "--font made the run fail (rc=$rc)"
    sed -n '1,6p' "$out/b.log" | sed 's/^/       /'
    fail=1
elif [ "$n" -lt 1 ]; then
    note FAIL "--font produced no frames"
    fail=1
elif [ "$sig" = "$(cat "$out/a"/* | sha256sum | cut -c1-32)" ]; then
    note FAIL "--font changed nothing: the frames are identical to the built-in ones"
    note "     (the font was named but never reached a glyph)"
    fail=1
else
    note OK "with --font: rc=0, $n frames, $sig (differs from the built-in faces)"
fi

# ---- 3. a font file that cannot be used ----------------------------------
#
# Must degrade to the built-in faces and say so.  Refusing to start over a
# font would be worse than not honouring it: the option is an enhancement, and
# a title that cannot be played because of it is a bug.
run "$out/c" --font "$out/definitely-not-a-font.ttf"
if [ "$rc" -ne 0 ]; then
    note FAIL "an unusable font file made the run fail (rc=$rc)"
    fail=1
elif [ "$n" -lt 1 ]; then
    note FAIL "an unusable font file produced no frames"
    fail=1
elif [ "$sig" != "$(cat "$out/a"/* | sha256sum | cut -c1-32)" ]; then
    note FAIL "an unusable font file changed the rendering instead of being ignored"
    fail=1
elif ! grep -qi "font" "$out/c.log"; then
    note FAIL "an unusable font file was ignored silently (no warning in the log)"
    fail=1
else
    note OK "unusable font file: warned and fell back to the built-in faces"
fi

if [ "$fail" -eq 0 ]; then
    echo "check_ttf_text: PASS"
else
    echo "check_ttf_text: FAIL"
fi
exit "$fail"
