#!/bin/sh
# tools/check_libretro_retroarch.sh
#
# Headless end-to-end check for the nano_j2me libretro core using the portable
# RetroArch 1.22.2 extracted under .local-tools/retroarch/.
#
# It runs RetroArch with null video/audio/input drivers, loads the core plus a
# JAR for a bounded number of frames, and asserts that:
#   * RetroArch exits 0 after --max-frames (clean run), and
#   * the core's log lines forwarded through RETRO_ENVIRONMENT_GET_LOG_INTERFACE
#     appear in the output, including
#       "[nano_j2me] running <class> (<game> <version>)" and
#       "[nano_j2me] run ended: ..."                        (clean-run case).
#
# Bounded fallback (documented in .local-tools/retroarch/README.md):
#   * normal path: RetroArch exits 0 by itself after --max-frames and the check
#     asserts the "[nano_j2me] run ended: ..." marker;
#   * watchdog path: when the core requests RETRO_ENVIRONMENT_SHUTDOWN (our core
#     does that once the MIDlet ends) RetroArch 1.22.2 closes the content and,
#     having no usable menu driver under a null video driver, starts a "dummy
#     core" run loop that never exits.  The check notices "[nano_j2me] run
#     ended", gives the frontend a 1 s grace period and then SIGTERMs it ->
#     accepted as PASS (bounded), exit 143/137;
#   * hard bound: if the whole run exceeds $TIMEOUT the frontend is SIGTERM/SIGKILLed
#     -> accepted as PASS (bounded) only when the log shows the core booted;
#   * crashes (exit 134/139) are always FAIL.
# A plain `timeout` wrapper is deliberately NOT used for the kill: GNU timeout
# runs the monitored command in its own process group and does not forward the
# TERM we would send to the wrapper (verified: the RetroArch child was orphaned),
# so this script bounds RetroArch directly (start in background, kill its PID).
# RetroArch is also given stdin=/dev/null: with an inherited pipe it can block
# forever after a core-requested SHUTDOWN (verified 1.22.2: foreground run with
# a pipe stdin hung past --max-frames; the same run with </dev/null exits 0).
#
# Isolation (task-1 requirement): every RetroArch path is redirected into a
# mktemp directory: --config points at a throwaway config, HOME /
# XDG_CONFIG_HOME / XDG_CACHE_HOME point into the temp dir, and all
# save/state/screenshot/system/playlist/core-options paths are set there too.
# $HOME/.config/retroarch is never touched; the script also fails if that
# directory appears during the run (regression guard).
#
# Usage:
#   tools/check_libretro_retroarch.sh [CORE [JAR [FRAMES]]]
#
# Positional arguments take precedence over environment variables:
#   CORE    core shared object        default: <repo>/build/libretro/nano_j2me_libretro.so
#   JAR     content (MIDlet JAR)      default: <repo>/tests/fixtures/alphademo.jar
#   FRAMES  frames to run             default: 600
#   TIMEOUT hard deadline in seconds for the whole run (the frontend is
#           TERMed/KILLed after it)              default: 120
#   RETROARCH  RetroArch executable   default: <repo>/.local-tools/retroarch/retroarch
#                                     (extracted AppImage 1.22.2 wrapper), else
#                                     "retroarch" from PATH
#   CORE_OPTIONS_CFG  optional file with libretro core option lines, e.g.
#                     nano_j2me_device = "nokia6230"
#                     It is copied next to the temp config as
#                     retroarch-core-options.cfg, which RetroArch 1.22.2 reads
#                     when global_core_options="true" (set by this script).
#   CORE_OPTIONS      same as CORE_OPTIONS_CFG but given inline, one line per
#                     option (newlines allowed); CORE_OPTIONS_CFG wins if both.
#   VERBOSE=1         echo the exact command line and the tail of the log.
#   KEEP_LOG=1        always keep the full log (default: kept only on FAIL).
#
# Exit status: 0 = PASS, 1 = FAIL, 2 = prerequisite/setup problem.
#
# Examples:
#   tools/check_libretro_retroarch.sh
#   tools/check_libretro_retroarch.sh build/libretro/nano_j2me_libretro.so \
#       tests/fixtures/alphademo.jar 1200
#   CORE_OPTIONS='nano_j2me_device = "nokia6230"' \
#       tools/check_libretro_retroarch.sh
#   TIMEOUT=180 tools/check_libretro_retroarch.sh build/libretro/nano_j2me_libretro.so \
#       "tests/games/3D城市桌球_8299.jar" 3000
#
set -u

fail()  { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
setup_fail() { printf 'FAIL: %s\n' "$*" >&2; exit 2; }

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

CORE=${1:-${CORE:-$REPO_DIR/build/libretro/nano_j2me_libretro.so}}
JAR=${2:-${JAR:-$REPO_DIR/tests/fixtures/alphademo.jar}}
FRAMES=${3:-${FRAMES:-600}}
TIMEOUT=${TIMEOUT:-120}
VERBOSE=${VERBOSE:-0}
KEEP_LOG=${KEEP_LOG:-0}
CORE_OPTIONS_CFG=${CORE_OPTIONS_CFG:-}
CORE_OPTIONS=${CORE_OPTIONS:-}

case $FRAMES in
   ''|*[!0-9]*) setup_fail "FRAMES must be a positive integer, got '$FRAMES'" ;;
   0)           setup_fail "FRAMES must be a positive integer, got 0" ;;
esac
case $TIMEOUT in
   ''|*[!0-9]*) setup_fail "TIMEOUT must be a positive integer, got '$TIMEOUT'" ;;
   0)           setup_fail "TIMEOUT must be a positive integer, got 0" ;;
esac

# --- locate RetroArch -------------------------------------------------------
if [ -n "${RETROARCH:-}" ]; then
   RA=$RETROARCH
   [ -x "$RA" ] || setup_fail "RETROARCH='$RA' is not executable"
elif [ -x "$REPO_DIR/.local-tools/retroarch/retroarch" ]; then
   RA=$REPO_DIR/.local-tools/retroarch/retroarch
elif command -v retroarch >/dev/null 2>&1; then
   RA=$(command -v retroarch)
else
   setup_fail "no RetroArch found: expected $REPO_DIR/.local-tools/retroarch/retroarch, or set RETROARCH=..., or put retroarch on PATH"
fi

# --- prerequisites ---------------------------------------------------------
[ -f "$CORE" ] || setup_fail "libretro core not found: $CORE
  build it first (e.g. cmake --build build --target nano_j2me_libretro) or pass CORE as the first argument"
[ -r "$CORE" ] || setup_fail "libretro core is not readable: $CORE"
[ -f "$JAR" ]  || setup_fail "content JAR not found: $JAR
  pass JAR as the second argument (tests/fixtures/alphademo.jar is the default)"
[ -r "$JAR" ]  || setup_fail "content JAR is not readable: $JAR"
if [ -n "$CORE_OPTIONS_CFG" ]; then
   [ -f "$CORE_OPTIONS_CFG" ] || setup_fail "CORE_OPTIONS_CFG not found: $CORE_OPTIONS_CFG"
   [ -r "$CORE_OPTIONS_CFG" ] || setup_fail "CORE_OPTIONS_CFG is not readable: $CORE_OPTIONS_CFG"
fi

# --- isolated scratch directory --------------------------------------------
TMP=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMP/nano_j2me_retroarch_check.XXXXXX") || setup_fail "mktemp failed"
cleanup() {
   if [ -n "${RA_PID:-}" ] && kill -0 "$RA_PID" 2>/dev/null; then
      kill -TERM "$RA_PID" 2>/dev/null
   fi
   [ -n "${WORK:-}" ] && rm -rf "$WORK"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$WORK/xdg" "$WORK/cache" "$WORK/home" \
         "$WORK/saves" "$WORK/states" "$WORK/screenshots" \
         "$WORK/system" "$WORK/playlists" || setup_fail "cannot create scratch dirs under $WORK"

CFG=$WORK/retroarch.cfg
LOG=$WORK/retroarch.log
RCOPT=$WORK/retroarch-core-options.cfg

# Fail if the real user config gets created during this run (must never happen).
REAL_CFG=${HOME:-/nonexistent}/.config/retroarch
REAL_CFG_EXISTED=0
[ -d "$REAL_CFG" ] && REAL_CFG_EXISTED=1

cat > "$CFG" <<EOF
video_driver = "null"
audio_driver = "null"
audio_enable = "false"
input_driver = "null"
menu_driver = "null"
config_save_on_exit = "false"
log_verbosity = "true"
video_vsync = "false"
audio_sync = "false"
video_threaded = "false"
global_core_options = "true"
savefile_directory = "$WORK/saves"
savestate_directory = "$WORK/states"
screenshot_directory = "$WORK/screenshots"
system_directory = "$WORK/system"
playlist_directory = "$WORK/playlists"
EOF

# Core option overrides.  RetroArch 1.22.2 in global_core_options mode reads
# <dir of --config>/retroarch-core-options.cfg, so a sibling of the temp config
# is the authoritative place (its keys are the plain libretro option keys, e.g.
# nano_j2me_device = "nokia6230").  Note: putting those keys in --config or
# --appendconfig does NOT work in 1.22.2; they must be in this options file.
if [ -n "$CORE_OPTIONS_CFG" ]; then
   cp "$CORE_OPTIONS_CFG" "$RCOPT" || setup_fail "cannot copy CORE_OPTIONS_CFG to $RCOPT"
elif [ -n "$CORE_OPTIONS" ]; then
   printf '%s\n' "$CORE_OPTIONS" > "$RCOPT"
fi

# --- run RetroArch with a hard bound and an early-exit watchdog ------------
# RetroArch is started directly in the background (not under `timeout`, see the
# header comment) so that its PID can be bounded with TERM then KILL.
START_TS=$(date +%s)
DEADLINE=$((START_TS + TIMEOUT))
KILLED_EARLY=0
KILLED_HARD=0
set +e
env -u DISPLAY -u WAYLAND_DISPLAY \
    HOME="$WORK/home" \
    XDG_CONFIG_HOME="$WORK/xdg" \
    XDG_CACHE_HOME="$WORK/cache" \
    NANO_J2ME_RA_HOME="$WORK/home" \
    "$RA" \
       --config "$CFG" \
       --verbose \
       --max-frames "$FRAMES" \
       -L "$CORE" \
       "$JAR" < /dev/null > "$LOG" 2>&1 &
RA_PID=$!

# Watchdog loop: poll the log for the core's end-of-run marker and the clock.
while kill -0 "$RA_PID" 2>/dev/null; do
   now=$(date +%s)
   if [ "$KILLED_EARLY" -eq 0 ] && [ "$now" -le "$DEADLINE" ] \
      && grep -Fq '[nano_j2me] run ended' "$LOG" 2>/dev/null \
      && grep -Fq '[Environ] SHUTDOWN' "$LOG" 2>/dev/null; then
      # The core finished but RetroArch is stuck in its fallback dummy-core
      # run loop (no menu driver under null video).  Grace period first, so a
      # normal clean exit is still reported as such.
      sleep 1
      if kill -0 "$RA_PID" 2>/dev/null; then
         kill -TERM "$RA_PID" 2>/dev/null
         KILLED_EARLY=1
      fi
      continue
   fi
   if [ "$now" -gt "$DEADLINE" ]; then
      kill -TERM "$RA_PID" 2>/dev/null
      n=0
      while [ "$n" -lt 20 ] && kill -0 "$RA_PID" 2>/dev/null; do
         sleep 0.1
         n=$((n + 1))
      done
      kill -KILL "$RA_PID" 2>/dev/null
      KILLED_HARD=1
      break
   fi
   sleep 0.2
done
wait "$RA_PID"
RC=$?
set -e
END_TS=$(date +%s)
WALL=$((END_TS - START_TS))

# --- evaluate --------------------------------------------------------------
has_marker=0; has_running=0; has_ended=0
grep -Fq  '[nano_j2me]' "$LOG" 2>/dev/null && has_marker=1
awk '/\[nano_j2me\]/ && /running /  { found=1 } END { exit !found }' "$LOG" 2>/dev/null && has_running=1
awk '/\[nano_j2me\]/ && /run ended/ { found=1 } END { exit !found }' "$LOG" 2>/dev/null && has_ended=1
marker_lines=$(grep -Fc '[nano_j2me]' "$LOG" 2>/dev/null || true)
[ -n "$marker_lines" ] || marker_lines=0

RA_VERSION=$(env HOME="$WORK/home" XDG_CONFIG_HOME="$WORK/xdg" XDG_CACHE_HOME="$WORK/cache" \
      NANO_J2ME_RA_HOME="$WORK/home" \
      "$RA" --version 2>/dev/null | sed -n 's/^Version: //p' | head -n 1)
[ -n "$RA_VERSION" ] || RA_VERSION='(unknown version)'

printf '%s\n' '== nano_j2me libretro / RetroArch headless check =='
printf 'retroarch : %s (%s)\n' "$RA" "$RA_VERSION"
printf 'core      : %s\n' "$CORE"
printf 'content   : %s\n' "$JAR"
printf 'frames    : %s (timeout %ss)\n' "$FRAMES" "$TIMEOUT"
printf 'log       : %s (temp; kept under .local-tools/retroarch/logs/ on FAIL)\n' "$LOG"

if [ "$VERBOSE" = 1 ]; then
   printf 'command   : env -u DISPLAY -u WAYLAND_DISPLAY HOME=%s XDG_CONFIG_HOME=%s \\\n' "$WORK/home" "$WORK/xdg"
   printf '            timeout -k 5 %s %s --config %s --verbose \\\n' "$TIMEOUT" "$RA" "$CFG"
   printf '            --max-frames %s -L %s %s\n' "$FRAMES" "$CORE" "$JAR"
fi

# Guard: the real user config dir must not appear/change because of this run.
if [ "$REAL_CFG_EXISTED" -eq 0 ] && [ -d "$REAL_CFG" ]; then
   printf '%s\n' "FAIL: sandbox breach: $REAL_CFG was created during the run" >&2
   fail "RetroArch wrote to the real user config directory"
fi

print_tail() {
   printf '%s\n' '--- last 60 log lines ---'
   tail -n 60 "$LOG" 2>/dev/null || true
   printf '%s\n' '-------------------------'
}

# Always keep the log on FAIL so it can be inspected after the temp dir is gone.
keep_log_copy() {
   logs_dir=$REPO_DIR/.local-tools/retroarch/logs
   if mkdir -p "$logs_dir" 2>/dev/null; then
      dst=$logs_dir/check-$(date +%Y%m%d-%H%M%S)-$$.log
      if cp "$LOG" "$dst" 2>/dev/null; then
         printf '%s\n' "$dst"
         return 0
      fi
   fi
   return 1
}

print_hints() {
   if grep -Eq 'Failed to load symbol' "$LOG" 2>/dev/null; then
      printf '%s\n' "  hint: the core is missing a mandatory libretro entry point (see the"
      printf '%s\n' "        'Failed to load symbol: ...' line above). The core must export every"
      printf '%s\n' "        RETRO_API procedure from libretro.h, including retro_get_region()."
   fi
   if grep -Eq 'cannot dynamically load|opening libretro core|打开libretro核心失败' "$LOG" 2>/dev/null; then
      printf '%s\n' "  hint: the CORE looks invalid/unloadable - is it a shared object built for this host?"
      printf '%s\n' "        file $CORE"
   fi
   if grep -Eq 'Failed to load content|无法加载内容|does not support content|Not an iNES' "$LOG" 2>/dev/null; then
      printf '%s\n' "  hint: RetroArch failed to load the CONTENT (the core rejected the JAR"
      printf '%s\n' "        extension or the JAR itself); confirm the core advertises jar/JAR as a"
      printf '%s\n' "        valid extension and that retro_load_game() accepts this file."
   fi
   if [ "$has_marker" -ne 1 ] && ! grep -Eq 'cannot dynamically load|打开libretro核心失败|Failed to load content|无法加载内容|Failed to load symbol' "$LOG" 2>/dev/null; then
      printf '%s\n' "  hint: no '[nano_j2me]' lines: the core must forward logs through"
      printf '%s\n' "        RETRO_ENVIRONMENT_GET_LOG_INTERFACE (retro_log_callback) and log"
      printf '%s\n' "        '[nano_j2me] running <class> (<game> <version>)' after load_game()."
   fi
}

fail_run() {
   printf '%s\n' "  [nano_j2me] lines : $marker_lines"
   printf '%s\n' "  running marker   : $([ "$has_running" -eq 1 ] && echo yes || echo no)"
   printf '%s\n' "  run ended marker : $([ "$has_ended" -eq 1 ] && echo yes || echo no)"
   printf '%s\n' "  retroarch exit   : $RC (124/137 = killed by timeout)"
   printf '%s\n' "  wall time        : ${WALL}s"
   if [ "$KILLED_EARLY" -eq 1 ]; then
      printf '%s\n' '  frontend        : killed after the core run ended (bounded fallback)'
   fi
   if [ "$KILLED_HARD" -eq 1 ]; then
      printf '%s\n' "  frontend        : killed at the ${TIMEOUT}s hard deadline"
   fi
   print_hints
   print_tail
   if kept=$(keep_log_copy 2>/dev/null); then printf '  full log kept at: %s\n' "$kept"; fi
   fail "$1"
}

case $RC in
   0)
      if [ "$has_marker" -ne 1 ]; then
         fail_run "RetroArch exited 0 but no '[nano_j2me]' log lines were found
  -> the core did not log through RETRO_ENVIRONMENT_GET_LOG_INTERFACE, or the
     core/content failed to load before logging"
      fi
      if [ "$has_running" -ne 1 ]; then
         fail_run "RetroArch exited 0 but the expected '[nano_j2me] running <class> (<game> <version>)' line is missing"
      fi
      if [ "$has_ended" -ne 1 ]; then
         fail_run "RetroArch exited 0 but the expected '[nano_j2me] run ended: ...' line is missing
  -> the core must log that line when the run finishes (retro_unload_game
     or equivalent); set KEEP_LOG=1 and inspect the log"
      fi
      printf '%s\n' '  [nano_j2me] lines : '"$marker_lines"
      printf '%s\n' '  running marker   : yes'
      printf '%s\n' '  run ended marker : yes'
      printf '%s\n' "  retroarch exit   : 0"
      printf '%s\n' "  wall time        : ${WALL}s"
      if [ "$KEEP_LOG" = 1 ]; then
         if kept=$(keep_log_copy 2>/dev/null); then printf '  full log kept at: %s\n' "$kept"; fi
      fi
      printf 'PASS: RetroArch exited 0 after %s frames; core log markers verified\n' "$FRAMES"
      exit 0
      ;;
   124|137|143)
      # Bounded fallback: the watchdog or the hard deadline terminated the
      # frontend.  Only accept when the log proves the core booted.
      if [ "$has_marker" -eq 0 ] || [ "$has_running" -ne 1 ]; then
         fail_run "RetroArch was terminated (exit $RC) but the log does not show the core booting"
      fi
      printf '%s\n' '  [nano_j2me] lines : '"$marker_lines"
      printf '%s\n' '  running marker   : yes'
      printf '%s\n' "  run ended marker : $([ "$has_ended" -eq 1 ] && echo yes || echo no)"
      printf '%s\n' "  retroarch exit   : $RC (killed by timeout after ${TIMEOUT}s)"
      printf '%s\n' "  wall time        : ${WALL}s"
      if [ "$KEEP_LOG" = 1 ]; then
         if kept=$(keep_log_copy 2>/dev/null); then printf '  full log kept at: %s\n' "$kept"; fi
      fi
      if [ "$KILLED_EARLY" -eq 1 ]; then
         printf '%s\n' 'WARN: RetroArch did not exit after the core finished (null video +'
         printf '%s\n' '      unusable menu driver => it falls back to a dummy-core loop).'
         printf '%s\n' '      The check terminated it after the "[nano_j2me] run ended" marker.'
      else
         printf '%s\n' "WARN: RetroArch was killed at the ${TIMEOUT}s hard deadline;"
         printf '%s\n' '      accepting the run as BOUNDED because the core log shows it booted.'
         printf '%s\n' '      (raise TIMEOUT if the emulation loop is simply slow)'
      fi
      printf 'PASS (bounded): core booted; frontend terminated after %ss (exit %s)\n' "$WALL" "$RC"
      exit 0
      ;;
   134|135|136|138|139)
      if [ "$KILLED_HARD" -eq 1 ] || [ "$KILLED_EARLY" -eq 1 ]; then
         fail_run "RetroArch crashed with signal $((RC - 128)) (exit $RC) while being terminated"
      fi
      fail_run "RetroArch crashed with signal $((RC - 128)) (exit $RC) - this is a frontend/core crash, not a timeout"
      ;;
   *)
      fail_run "RetroArch exited with status $RC (expected 0, or 124/137/143 when bounded)"
      ;;
esac
