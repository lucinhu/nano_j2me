#!/usr/bin/env bash
# Scan the corpus for frames that flip back and forth between two pictures.
#
# The corpus sweep answers "did the title draw anything".  It cannot answer
# "does the picture stand still", because it samples every fortieth present and
# a two-frame alternation - the flicker 合金弹头2013BT版_678 and
# 鲁宾逊漂流记汉化版_702 showed - is invisible at that rate.
#
# Usage:
#   tools/scan_frame_health.sh [jar ...]
#   NJ_FRAMES=150 tools/scan_frame_health.sh          # the whole corpus
#
# Knobs: NJ_BIN, NJ_GAMES, NJ_FRAMES (150), NJ_SIZE (240x320), NJ_HEAP (16),
#        NJ_FPS (30), NJ_FLICKER_PCT (5, passed to tools/frame_health.py),
#        NJ_KEYS (a key script, for a title whose flicker is past its menu)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
GAMES="${NJ_GAMES:-$ROOT/tests/games}"
FRAMES="${NJ_FRAMES:-150}"
SIZE="${NJ_SIZE:-240x320}"
HEAP="${NJ_HEAP:-16}"
FPS="${NJ_FPS:-30}"
KEYS="${NJ_KEYS:-}"
KEYARGS=()
[ -n "$KEYS" ] && KEYARGS=(--keys "$KEYS")

if [ "$#" -gt 0 ]; then
  JARS=("$@")
else
  JARS=()
  for j in "$GAMES"/*.jar; do JARS+=("$j"); done
fi

if [ ! -x "$BIN" ]; then
  echo "no runtime at $BIN - build it or set NJ_BIN" >&2
  exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT INT TERM

flagged=0
scanned=0
for jar in "${JARS[@]}"; do
  name="$(basename "$jar" .jar)"
  out="$WORK/f"
  rm -rf "$out"
  mkdir -p "$out"
  timeout 180 "$BIN" --port headless --heap "$HEAP" --size "$SIZE" --fps "$FPS" \
      --run-frames "$FRAMES" --dump-frames "$out/" --dump-every 1 \
      "${KEYARGS[@]}" "$jar" \
      > "$WORK/log" 2>&1
  if grep -q 'not a MIDlet suite' "$WORK/log"; then
    printf '%-44s %s\n' "$name" "SKIP (not a MIDlet suite)"
    continue
  fi
  res="$(python3 "$ROOT/tools/frame_health.py" "$out" 2>/dev/null)"
  if [ -z "$res" ]; then
    printf '%-44s %s\n' "$name" "no frames"
    continue
  fi
  scanned=$((scanned + 1))
  case "$res" in
    *verdict=flicker*) flagged=$((flagged + 1)); mark=" <<<";;
    *) mark="";;
  esac
  printf '%-44s %s%s\n' "$name" "$res" "$mark"
done
echo "scanned: $scanned   flickering: $flagged"
[ "$flagged" -eq 0 ]
