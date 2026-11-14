#!/usr/bin/env bash
# External-font selection and manual-state regression using a generated font.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="${1:-$ROOT/build/libretro/nano_j2me_libretro.so}"
FRONTEND="${FRONTEND:-$ROOT/build/test_libretro_frontend}"
FONT="$ROOT/tests/fixtures/ttf/nj_fixture.ttf"
JAR="$ROOT/tests/fixtures/loopdemo.jar"
TEXT_JAR="$ROOT/tests/fixtures/textdemo.jar"
for input in "$CORE" "$FRONTEND" "$FONT" "$JAR" "$TEXT_JAR"; do
    test -f "$input" || { echo "missing test input: $input" >&2; exit 2; }
done
LOG_DIR="${LOG_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/nj-libretro-fonts.XXXXXX")}"
mkdir -p "$LOG_DIR/system/nano_j2me" "$LOG_DIR/missing"
cp "$FONT" "$LOG_DIR/system/nano_j2me/font.ttf"
run_case() {
    local name="$1" mode="$2" dir="$3"
    "$FRONTEND" --core "$CORE" --jar "$JAR" --frames 60 --pace-us 1000 \
        --visual-required --save-dir "$dir" --set nano_j2me_heap=16 \
        --set "nano_j2me_font=$mode" > "$LOG_DIR/$name.log" 2>&1 || {
        echo "FAIL $name (see $LOG_DIR/$name.log)" >&2; return 1;
    }
}
run_case external system "$LOG_DIR/system"
grep -q 'font: loaded .*nano_j2me/font.ttf' "$LOG_DIR/external.log"
grep -q 'PASS.*unserialize_rejects_other_font' "$LOG_DIR/external.log"
grep -q 'PASS.*unserialize_accepts_resigned_state' "$LOG_DIR/external.log"
run_case builtin builtin "$LOG_DIR/system"
grep -q 'font: using built-in OFL bitmap fonts' "$LOG_DIR/builtin.log"
if grep -q 'font: loaded ' "$LOG_DIR/builtin.log"; then
    echo 'FAIL builtin unexpectedly loaded an external font' >&2; exit 1
fi
run_case missing system "$LOG_DIR/missing"
grep -q 'font: no supported external font; using built-in OFL bitmap fonts' "$LOG_DIR/missing.log"
printf OTTO > "$LOG_DIR/system/nano_j2me/font.ttf"
run_case invalid system "$LOG_DIR/system"
grep -q 'font: unsupported or invalid TrueType font:' "$LOG_DIR/invalid.log"
grep -q 'font: no supported external font; using built-in OFL bitmap fonts' "$LOG_DIR/invalid.log"
run_case auto auto "$LOG_DIR/missing"
grep -Eq 'font: (loaded |no supported external font;)' "$LOG_DIR/auto.log"
# TextDemo actually draws glyphs; LoopDemo above only draws animated rectangles.
# A generated font must change rendered pixels, not merely log a successful load.
cp "$FONT" "$LOG_DIR/system/nano_j2me/font.ttf"
for mode in system builtin; do
    "$FRONTEND" --core "$CORE" --jar "$TEXT_JAR" --frames 60 --pace-us 1000 \
        --visual-required --static-ok --save-dir "$LOG_DIR/system" \
        --set nano_j2me_heap=16 --set "nano_j2me_font=$mode" \
        > "$LOG_DIR/text-$mode.log" 2>&1 || {
        echo "FAIL text-$mode (see $LOG_DIR/text-$mode.log)" >&2; exit 1;
    }
done
external_hash="$(sed -n 's/.*last sampled frame hash=//p' "$LOG_DIR/text-system.log" | tail -n 1)"
builtin_hash="$(sed -n 's/.*last sampled frame hash=//p' "$LOG_DIR/text-builtin.log" | tail -n 1)"
if [ -z "$external_hash" ] || [ -z "$builtin_hash" ] || [ "$external_hash" = "$builtin_hash" ]; then
    echo 'FAIL external font did not change the text frame' >&2; exit 1
fi
echo "PASS external, builtin, missing, invalid and auto fonts, including manual states"
echo "PASS rendered text changes with external font ($external_hash vs $builtin_hash)"
echo "Logs: $LOG_DIR"
