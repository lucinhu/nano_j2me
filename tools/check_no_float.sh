#!/usr/bin/env bash
#
# Keep NJ_ENABLE_FLOAT honest.
#
# nj_config.h has always documented NJ_ENABLE_FLOAT=0 as dropping the libm
# transcendentals from the external-symbol set.  It was broken in TWO
# independent ways at once - nothing in src/ read the switch, and CMake never
# passed it to the compiler - so a "no floating point" build still linked every
# one of them, and the only visible symptom was a link error on a target that
# has no libm, with no clue which method wanted it.
#
# This compiles one object both ways and counts the transcendental symbols.  It
# is a build-time property, so it is checked at build time rather than by
# running anything.
#
#     tools/check_no_float.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

SYMS=' U (pow|log|log10|exp|sqrt|sin|cos|tan|asin|acos|atan|atan2|cbrt)$'
CFLAGS="-std=c99 -I$ROOT/include -I$ROOT/src/core"

gcc $CFLAGS -c "$ROOT/src/core/nj_lib_math.c" -o "$WORK/on.o"
gcc $CFLAGS -DNJ_ENABLE_FLOAT=0 -c "$ROOT/src/core/nj_lib_math.c" -o "$WORK/off.o"

on=$(nm -u "$WORK/on.o" | grep -cE "$SYMS" || true)
off=$(nm -u "$WORK/off.o" | grep -cE "$SYMS" || true)

printf 'NJ_ENABLE_FLOAT=1: %2d libm transcendentals\n' "$on"
printf 'NJ_ENABLE_FLOAT=0: %2d libm transcendentals\n' "$off"

fail=0
[ "$on" -ge 10 ] || { echo "FAIL: the switch being on no longer pulls in libm"; fail=1; }
[ "$off" -eq 0 ] || { echo "FAIL: NJ_ENABLE_FLOAT=0 leaves $off behind"; fail=1; }
[ "$fail" -eq 0 ] && echo "OK: the switch does what nj_config.h says"
exit "$fail"
