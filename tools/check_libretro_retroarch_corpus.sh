#!/bin/sh
# tools/check_libretro_retroarch_corpus.sh
#
# Batch integration check: run a set of real corpus titles through the real
# RetroArch build under .local-tools/retroarch/, one after another, using the
# single-title checker (tools/check_libretro_retroarch.sh) for each.
#
# The single-title checker already asserts the end-to-end contract: RetroArch
# exits cleanly after --max-frames, and the core's forwarded log lines
# "[nano_j2me] running <class> (<game> <version>)" and
# "[nano_j2me] run ended: ..." appear. This batch wrapper just runs that check
# over several titles (2D, 3D, audio and save-state users), keeps one log per
# title and prints a PASS/FAIL summary, so the integration evidence for a
# release is one command instead of eight.
#
# It is deliberately serial: RetroArch instances are heavy and the point is the
# core/frontend contract, not throughput. Pixel-level rendering is covered by
# the libretro mock-frontend probes (tools/check_libretro_corpus_visual.py).
#
# Usage:
#   tools/check_libretro_retroarch_corpus.sh [CORE [FRAMES [TITLE...]]]
#
#   CORE   core shared object
#          default: <repo>/build/libretro-dist/linux-x86_64/nano_j2me_libretro.so
#   FRAMES frames per title (default 600 = ten virtual seconds at 60 Hz)
#   TITLE  one or more JAR names under tests/games or paths; default: the
#          eight titles listed below.
#
# Environment:
#   TIMEOUT  hard deadline per title, seconds (default 180)
#   NJ_RA_OUT directory for the per-title logs
#             (default <repo>/.local-tools/debug-runs/retroarch-corpus)
#   RETROARCH/CORE_OPTIONS/... are forwarded to the single-title checker.
#
# Exit status: 0 = every title passed, 1 = at least one failed,
#              2 = setup problem (core/script/title missing).
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
CHECK=$SCRIPT_DIR/check_libretro_retroarch.sh

CORE=${1:-$REPO_DIR/build/libretro-dist/linux-x86_64/nano_j2me_libretro.so}
FRAMES=${2:-600}
shift 2 2>/dev/null || true

TIMEOUT=${TIMEOUT:-180}
NJ_RA_OUT=${NJ_RA_OUT:-$REPO_DIR/.local-tools/debug-runs/retroarch-corpus}

[ -x "$CHECK" ] || { echo "FAIL: missing checker $CHECK" >&2; exit 2; }
[ -f "$CORE" ]  || { echo "FAIL: core not found: $CORE" >&2; exit 2; }
case $FRAMES in ''|*[!0-9]*) echo "FAIL: FRAMES must be a positive integer" >&2; exit 2 ;; esac

if [ "$#" -gt 0 ]; then
  TITLES=$(printf '%s\n' "$@")
else
  TITLES=$(cat <<'EOF'
浴火银河2完整版_7689.jar
仙剑奇侠传完美版_11857.jar
拳皇98终极之战_7534.jar
雷霆战机2012BT版_3101.jar
灵异村庄_11169.jar
超级马里奥_10283.jar
三国杀单机战功修改版_4143.jar
美女滑雪胜地_2.jar
EOF
)
fi

mkdir -p "$NJ_RA_OUT" || exit 2

total=0
failed=0
for t in $TITLES; do
  case $t in
    /*) jar=$t ;;
    *)  jar=$REPO_DIR/tests/games/$t ;;
  esac
  total=$((total + 1))
  safe=$(printf '%s' "$t" | tr '/ ' '__' | tr -cd 'A-Za-z0-9._\200-\377' )
  safe=${safe%.jar}
  log=$NJ_RA_OUT/$(printf '%02d' "$total")-$safe.log
  if [ ! -f "$jar" ]; then
    printf 'FAIL %s: no such JAR\n' "$t"
    failed=$((failed + 1))
    continue
  fi
  printf '== %s (%s frames)\n' "$t" "$FRAMES"
  if KEEP_LOG=1 TIMEOUT=$TIMEOUT "$CHECK" "$CORE" "$jar" "$FRAMES" >"$log" 2>&1; then
    printf 'PASS %s\n' "$t"
  else
    rc=$?
    printf 'FAIL %s (rc=%s, log %s)\n' "$t" "$rc" "$log"
    tail -n 12 "$log" | sed 's/^/    /'
    failed=$((failed + 1))
  fi
done

printf '\n== %d titles: %d passed, %d failed ==\n' \
  "$total" "$((total - failed))" "$failed"
[ "$failed" -eq 0 ] || exit 1
