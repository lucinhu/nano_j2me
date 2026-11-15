#!/bin/sh
# End-to-end save-state check through a real RetroArch frontend.
#
# The mock frontend (tests/test_libretro_frontend) checks serialize/unserialize
# directly.  This script checks the *frontend* path instead: RetroArch saves a
# state when the content unloads (savestate_auto_save) and loads it on the next
# launch (savestate_auto_load), which is what a user actually does.
#
# Usage:
#   tools/check_libretro_retroarch_state.sh [CORE [JAR [FRAMES]]]
#
#   CORE    defaults to build/libretro-dist/linux-x86_64/nano_j2me_libretro.so
#   JAR     defaults to tests/fixtures/loopdemo.jar
#   FRAMES  frames for the save run (default 150); the load run uses 60
#
# Environment:
#   NJ_RA_BIN   RetroArch binary; otherwise .local-tools/retroarch/retroarch
#               (extracted AppImage wrapper), else "retroarch" from PATH
#   TIMEOUT     per-run bound in seconds (default 180)
#   KEEP_LOG    keep the temporary work directory and print its path
#
# Exit status: 0 pass, 1 fail, 2 setup problem.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CORE=${1:-$ROOT/build/libretro-dist/linux-x86_64/nano_j2me_libretro.so}
JAR=${2:-$ROOT/tests/fixtures/loopdemo.jar}
FRAMES=${3:-150}
TIMEOUT=${TIMEOUT:-180}
[ -f "$CORE" ] || { echo "no core at $CORE" >&2; exit 2; }
[ -f "$JAR" ]  || { echo "no content at $JAR" >&2; exit 2; }

if [ -n "${NJ_RA_BIN:-}" ]; then
   RA=$NJ_RA_BIN
elif [ -x "$ROOT/.local-tools/retroarch/retroarch" ]; then
   RA=$ROOT/.local-tools/retroarch/retroarch
else
   RA=$(command -v retroarch || true)
fi
[ -n "$RA" ] || { echo "no RetroArch binary found" >&2; exit 2; }

TMP=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMP/nano_j2me_ra_state.XXXXXX") || exit 2
cleanup() {
   if [ "${KEEP_LOG:-0}" = 1 ]; then
      echo "work directory kept at: $WORK"
   else
      [ -n "${WORK:-}" ] && rm -rf "$WORK"
   fi
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$WORK/home" "$WORK/xdg" "$WORK/cache" "$WORK/saves" "$WORK/system"
CFG=$WORK/retroarch.cfg
SAVES=$WORK/saves

write_cfg() {
   auto_save=$1
   auto_load=$2
   cat > "$CFG" <<EOF
video_driver = "null"
audio_driver = "null"
input_driver = "null"
input_joypad_driver = "null"
menu_driver = "null"
log_to_file = "false"
log_verbosity = "true"
savestate_directory = "$SAVES"
savefile_directory = "$SAVES"
system_directory = "$WORK/system"
savestate_auto_save = "$auto_save"
savestate_auto_load = "$auto_load"
EOF
}

run_once() {
   log=$1
   frames=$2
   env -u DISPLAY -u WAYLAND_DISPLAY \
       QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
       HOME="$WORK/home" XDG_CONFIG_HOME="$WORK/xdg" \
       XDG_CACHE_HOME="$WORK/cache" \
       timeout "${TIMEOUT}" "$RA" \
          --config "$CFG" --verbose --max-frames "$frames" \
          -L "$CORE" "$JAR" < /dev/null > "$log" 2>&1
}

FAILED=0
fail() {
   echo "FAIL: $*"
   FAILED=1
}

echo '== nano_j2me libretro / RetroArch save-state round trip =='
echo "core    : $CORE"
echo "content : $JAR"
echo "retroarch: $RA ($("$RA" --version 2>/dev/null | sed -n 's/^Version: //p' | head -n 1))"

# --- run 1: play, then let the frontend save a state on unload -------------
write_cfg true false
if ! run_once "$WORK/save.log" "$FRAMES"; then
   fail "save run exited nonzero (see $WORK/save.log)"
fi
grep -Fq '[nano_j2me] running ' "$WORK/save.log" || fail 'save run never reached the emulation loop'

STATE=$(find "$SAVES" -type f -name '*.state.auto' 2>/dev/null | head -n 1 || true)
if [ -z "$STATE" ]; then
   fail 'RetroArch did not write an auto-save state'
else
   size=$(wc -c < "$STATE" | tr -d ' ')
   [ "$size" -gt 1024 ] || fail "auto-save state is suspiciously small ($size bytes)"
   echo "state   : $STATE ($size bytes)"
fi

# --- run 2: launch again; the frontend must restore it ---------------------
write_cfg false true
if ! run_once "$WORK/load.log" 60; then
   fail "load run exited nonzero (see $WORK/load.log)"
fi
grep -Fq '[nano_j2me] resumed from a save state' "$WORK/load.log" \
   || fail 'second launch did not report "resumed from a save state"'

if [ "$FAILED" -ne 0 ]; then
   echo '-- save run tail --'
   tail -n 20 "$WORK/save.log" 2>/dev/null || true
   echo '-- load run tail --'
   tail -n 20 "$WORK/load.log" 2>/dev/null || true
   echo 'FAILED'
   exit 1
fi

echo "PASS: frontend saved a state and restored it on the next launch"
exit 0
