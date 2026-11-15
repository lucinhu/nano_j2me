#!/bin/sh
# tools/check_libretro_rewind.sh
#
# Frontend rewind smoke check for the nano_j2me libretro core.
#
# RetroArch implements rewind by snapshotting the core with retro_serialize()
# into a ring buffer and restoring with retro_unserialize() when the user holds
# the rewind key.  For nano_j2me every snapshot is the whole Java heap, so the
# feature is only usable when the heap budget (and therefore the snapshot size)
# stays below the rewind buffer:
#
#   snapshot size ~= heap budget + ~6 MB (framebuffers + VM bookkeeping)
#     nano_j2me_heap=16  -> ~23 MB   (recommended for rewind)
#     nano_j2me_heap=32  -> ~40 MB
#     nano_j2me_heap=128 (auto on a big host) -> ~140 MB
#
# This check runs a bounded headless RetroArch session with rewind enabled and
# asserts that (a) RetroArch initializes its rewind buffer and exits cleanly
# with the core's run markers, and (b) the frontend does not report that the
# buffer is too small for our snapshots.  It cannot exercise an actual key
# press (no input driver headless); it proves the snapshot/unserialize path the
# frontend uses is wired up and sized correctly.
#
# Usage: tools/check_libretro_rewind.sh [CORE [JAR [FRAMES]]]
#   CORE   default build/libretro-dist/linux-x86_64/nano_j2me_libretro.so
#   JAR    default tests/fixtures/loopdemo.jar
#   FRAMES default 600
# Env: HEAP (core option, default 16), GRANULARITY (default 2),
#      BUFFER_SIZE (rewind_buffer_size MB, default 64), TIMEOUT (default 180).
#
# Exit: 0 pass, 1 fail, 2 setup problem.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CORE=${1:-$ROOT/build/libretro-dist/linux-x86_64/nano_j2me_libretro.so}
JAR=${2:-$ROOT/tests/fixtures/loopdemo.jar}
FRAMES=${3:-600}
HEAP=${HEAP:-16}
GRANULARITY=${GRANULARITY:-2}
BUFFER_SIZE=${BUFFER_SIZE:-64}
TIMEOUT=${TIMEOUT:-180}

fail() { echo "FAIL: $*" >&2; exit 1; }
[ -f "$CORE" ] || { echo "setup: core not found: $CORE" >&2; exit 2; }
[ -f "$JAR" ] || { echo "setup: content not found: $JAR" >&2; exit 2; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/nj-rewind.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT INT TERM

OPTS=$WORK/heap-options.cfg
printf 'nano_j2me_heap = "%s"\n' "$HEAP" > "$OPTS" || exit 2

echo "== nano_j2me libretro rewind check =="
echo "core        : $CORE"
echo "content     : $JAR"
echo "frames      : $FRAMES"
echo "heap        : ${HEAP} MB option (snapshot grows with it)"
echo "granularity : $GRANULARITY"
echo "buffer      : ${BUFFER_SIZE} MB"

RW_CFG="rewind_enable = \"true\"
rewind_granularity = $GRANULARITY
rewind_buffer_size = $BUFFER_SIZE"

START=$(date +%s%N)
KEEP_LOG=1 CORE_OPTIONS_CFG="$OPTS" EXTRA_CFG="$RW_CFG" \
   TIMEOUT="$TIMEOUT" \
   "$ROOT/tools/check_libretro_retroarch.sh" "$CORE" "$JAR" "$FRAMES" \
   > "$WORK/check.log" 2>&1
RC=$?
END=$(date +%s%N)
WALL_MS=$(( (END - START) / 1000000 ))

if [ "$RC" -ne 0 ]; then
   cat "$WORK/check.log" >&2
   fail "RetroArch run failed (rc=$RC); see log above"
fi

LOG=$(sed -n 's/^  full log kept at: //p' "$WORK/check.log" | tail -1)
[ -n "$LOG" ] && [ -f "$LOG" ] || fail "could not locate the kept RetroArch log"

BUFFER_LINE=$(grep -m1 "Rewind" "$LOG" 2>/dev/null)
[ -n "$BUFFER_LINE" ] || fail "RetroArch did not initialize a rewind buffer"
if grep -qi "容量不足\|capacity" "$LOG"; then
   fail "rewind buffer too small for the snapshots ($BUFFER_LINE); raise rewind_buffer_size or lower nano_j2me_heap"
fi

echo "rewind init : $BUFFER_LINE"
echo "wall time   : ${WALL_MS}ms for $FRAMES frames with granularity $GRANULARITY"
echo "frontend log: $LOG (kept)"
echo "PASS: rewind enabled, buffer initialized, snapshots fit, clean exit"
exit 0
