#!/bin/sh
# tools/check_libretro_state_cost.sh
#
# Measures what a frontend pays per stored frame for rewind/runahead.
#
# RetroArch's rewind ring and its runahead both call retro_serialize() (and,
# when rolling back, retro_unserialize()) once per displayed frame.  For
# nano_j2me a snapshot is the whole Java arena plus the framebuffers, so the
# cost scales with nano_j2me_heap.  This check runs the mock libretro frontend
# with --serialize-timing and asserts the timing report is produced; the
# numbers are printed for the docs (they depend on the machine, so no pass/fail
# threshold is applied by default).
#
# Usage: tools/check_libretro_state_cost.sh [HARNESS [CORE [JAR]]]
#   HARNESS default: required (built by tools/check_libretro_frontend.sh)
#   CORE    default build/libretro-dist/linux-x86_64/nano_j2me_libretro.so
#   JAR     default tests/fixtures/loopdemo.jar
# Env: HARNESS, CORE, JAR, FRAMES (default 180), RUNS (default 5),
#      HEAP (core option, default auto), TIMEOUT (default 180)
#
# Exit: 0 pass, 1 fail, 2 setup problem.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
HARNESS=${1:-${HARNESS:-}}
CORE=${2:-${CORE:-$ROOT/build/libretro-dist/linux-x86_64/nano_j2me_libretro.so}}
JAR=${3:-${JAR:-$ROOT/tests/fixtures/loopdemo.jar}}
FRAMES=${FRAMES:-180}
RUNS=${RUNS:-5}
HEAP=${HEAP:-auto}
TIMEOUT=${TIMEOUT:-180}

fail() { echo "FAIL: $*" >&2; exit 1; }
[ -n "$HARNESS" ] || { echo "setup: pass the harness path or set HARNESS" >&2; exit 2; }
[ -x "$HARNESS" ] || { echo "setup: harness not executable: $HARNESS" >&2; exit 2; }
[ -f "$CORE" ] || { echo "setup: core not found: $CORE" >&2; exit 2; }
[ -f "$JAR" ] || { echo "setup: content not found: $JAR" >&2; exit 2; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/nj-statecost.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT INT TERM

echo "== nano_j2me libretro state cost check =="
echo "harness : $HARNESS"
echo "core    : $CORE"
echo "content : $JAR"
echo "frames  : $FRAMES"
echo "heap    : $HEAP"

set -- --core "$CORE" --jar "$JAR" --frames "$FRAMES" \
       --serialize-timing "$RUNS" --save-dir "$WORK/save"
if [ "$HEAP" != auto ]; then
   set -- "$@" --set nano_j2me_heap="$HEAP"
fi

LOG=$WORK/harness.log
timeout "$TIMEOUT" "$HARNESS" "$@" >"$LOG" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
   tail -20 "$LOG" >&2
   fail "harness exited $rc"
fi

if ! grep -q '^\[timing\] state size=' "$LOG"; then
   tail -20 "$LOG" >&2
   fail "no [timing] report in the harness output"
fi

grep -E '^\[timing\] state size=' "$LOG"
echo "PASS: save/restore cost measured for one stored frame"
