#!/bin/sh
# tools/check_libretro_frontend.sh -- compile and run the mock libretro
# frontend conformance harness against the nano_j2me libretro core.
#
# This script needs no RetroArch: it builds tests/test_libretro_frontend.c
# and drives the core exactly like a frontend would.
#
# Usage (from anywhere):
#   tools/check_libretro_frontend.sh
#   CORE=/path/to/nano_j2me_libretro.so tools/check_libretro_frontend.sh
#
# Environment:
#   CORE   libretro core path (default: build/libretro/nano_j2me_libretro.so)
#   CC     C compiler (default: cc)
#   HARNESS_CFLAGS
#          extra compile and link flags for the harness (the sanitizer build
#          passes -fsanitize=address,undefined here: the core .so is
#          instrumented, and the executable that dlopen()s it has to link the
#          sanitizer runtime too or ASan refuses to start)
#
# Matrix:
#   1. alphademo   load + 60 frames presented (content stats reported; the
#                  fixture's MIDlet paints black and exits during load, so the
#                  strict content assertion lives in step 2 instead)
#   2. loopdemo    60 frames, --visual-required: non-black and animated frames
#   3. audiodemo   --audio-required: at least one nonzero audio sample
#   4. bad input   --expect-load-fail with tests/fixtures/bad_btype.bin
#   5. two-cycle   load/run 30 frames/unload twice in one process
#   6. input       joypad + keyboard driven, core must poll input_state
#   7. rotation    rotation=90 through the core's own rotation option when the
#                  core registers one (otherwise SKIP).  A core that rotates
#                  internally -- spreading rotated 320x240 AV geometry but
#                  calling no SET_ROTATION -- is explicitly accepted, because
#                  both ways of honouring the rotation option are valid.
#   8. cadence     real-time 60 Hz retro_run pacing (--pace-us 16667), the
#                  cadence RetroArch uses; steps 1-6 run the tight loop
#
# Exit codes: 0 all checks passed, 1 at least one check failed, 2 setup error
# (missing core/fixture, compile failure, nothing to run).

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || {
   printf 'FAIL cannot resolve repository root\n' >&2
   exit 2
}
cd "$ROOT" || exit 2

CC=${CC:-cc}
HARNESS_CFLAGS=${HARNESS_CFLAGS:-}
CORE=${CORE:-$ROOT/build/libretro/nano_j2me_libretro.so}
HARNESS=$ROOT/build/test_libretro_frontend
FIXTURES=$ROOT/tests/fixtures

case $CORE in
   /*) ;;
   *) CORE=$ROOT/$CORE ;;
esac

WORK=$(mktemp -d "${TMPDIR:-/tmp}/nj_libretro_frontend.XXXXXX") || {
   printf 'FAIL cannot create temporary directory\n' >&2
   exit 2
}
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

PASSED=0
FAILED=0
SKIPPED=0

say()  { printf '%s\n' "$*"; }
pass() { PASSED=$((PASSED + 1)); say "PASS $*"; }
fail() { FAILED=$((FAILED + 1)); say "FAIL $*"; }
skip() { SKIPPED=$((SKIPPED + 1)); say "SKIP $*"; }

TIMEOUT=
if command -v timeout >/dev/null 2>&1; then
   TIMEOUT="timeout 120"
fi

say "== nano_j2me libretro frontend conformance =="
say "   core:    $CORE"
say "   harness: $HARNESS"
say "   cc:      $CC"
say ""

if [ ! -f "$CORE" ]; then
   say "FAIL core not built: $CORE"
   say "     build it first or pass CORE=/path/to/nano_j2me_libretro.so"
   exit 2
fi

mkdir -p "$ROOT/build" || {
   say "FAIL cannot create $ROOT/build"
   exit 2
}

say "== build harness =="
say "   $CC ${HARNESS_CFLAGS:-} -std=c99 -O2 -Wall -Wextra -I src/port/libretro \\"
say "      -o build/test_libretro_frontend tests/test_libretro_frontend.c -ldl"
if ! $CC ${HARNESS_CFLAGS:-} -std=c99 -O2 -Wall -Wextra -I src/port/libretro \
      -o "$HARNESS" tests/test_libretro_frontend.c -ldl \
      >"$WORK/build.log" 2>&1; then
   say "FAIL harness build"
   tail -n 40 "$WORK/build.log" | sed 's/^/   /'
   exit 2
fi
   say "OK harness built"
   if [ -s "$WORK/build.log" ]; then
      say "   (build produced warnings:)"
      sed 's/^/   /' "$WORK/build.log"
   fi
   say ""

# run_one NAME -- harness args...
# Runs the harness once, prints PASS/FAIL plus a diagnostic tail on failure.
run_one() {
   name=$1
   shift
   log=$WORK/step.log
   if $TIMEOUT "$HARNESS" "$@" >"$log" 2>&1; then
      pass "$name"
      grep -E '^\[(summary|result)\]' "$log" | sed 's/^/     /'
   else
      rc=$?
      fail "$name (exit $rc)"
      say "     --- failed checks ---"
      grep -E '^FAIL' "$log" | head -n 20 | sed 's/^/     /'
      say "     --- output tail ---"
      tail -n 40 "$log" | sed 's/^/     /'
      say "     --- end output ---"
   fi
}

say "== core checks =="

if [ -f "$FIXTURES/alphademo.jar" ]; then
   run_one "alphademo load + present (60 frames)" \
      --core "$CORE" --jar "$FIXTURES/alphademo.jar" --frames 60 \
      --save-dir "$WORK/save-alpha"
else
   skip "alphademo load + present: missing $FIXTURES/alphademo.jar"
fi

if [ -f "$FIXTURES/loopdemo.jar" ]; then
   run_one "loopdemo animated frames (--visual-required, 60 frames)" \
      --core "$CORE" --jar "$FIXTURES/loopdemo.jar" --frames 60 \
      --visual-required --save-dir "$WORK/save-loop"
else
   skip "loopdemo animated frames: missing $FIXTURES/loopdemo.jar"
fi

if [ -f "$FIXTURES/audiodemo.jar" ]; then
   run_one "audiodemo audio (--audio-required, 60 frames)" \
      --core "$CORE" --jar "$FIXTURES/audiodemo.jar" --frames 60 \
      --audio-required --save-dir "$WORK/save-audio"
else
   skip "audiodemo audio: missing $FIXTURES/audiodemo.jar"
fi

if [ -f "$FIXTURES/bad_btype.bin" ]; then
   run_one "non-jar input rejected (--expect-load-fail)" \
      --core "$CORE" --jar "$FIXTURES/bad_btype.bin" --expect-load-fail \
      --save-dir "$WORK/save-bad"
else
   skip "non-jar input rejected: missing $FIXTURES/bad_btype.bin"
fi

CYCLE_JAR=
if [ -f "$FIXTURES/loopdemo.jar" ]; then
   CYCLE_JAR=$FIXTURES/loopdemo.jar
elif [ -f "$FIXTURES/alphademo.jar" ]; then
   CYCLE_JAR=$FIXTURES/alphademo.jar
fi
if [ -n "$CYCLE_JAR" ]; then
   run_one "two load/run/unload cycles (30 frames each)" \
      --core "$CORE" --jar "$CYCLE_JAR" --frames 30 --cycle \
      --save-dir "$WORK/save-cycle"
else
   skip "two load/run/unload cycles: no jar fixture available"
fi

if [ -n "$CYCLE_JAR" ]; then
   run_one "input delivery: joypad + keyboard (60 frames)" \
      --core "$CORE" --jar "$CYCLE_JAR" --frames 60 --input-check \
      --save-dir "$WORK/save-input"
else
   skip "input delivery: no jar fixture available"
fi

# Steps 1-6 use the harness's tight (unpaced) retro_run loop.  Also check the
# cadence a real frontend uses: once per 60 Hz display frame.
if [ -n "$CYCLE_JAR" ]; then
   run_one "real-time 60 Hz cadence (--pace-us 16667, 30 frames)" \
      --core "$CORE" --jar "$CYCLE_JAR" --frames 30 --pace-us 16667 \
      --visual-required --save-dir "$WORK/save-pace"
else
   skip "real-time 60 Hz cadence: no jar fixture available"
fi

# Rotation is a core *option*: the core reads it at load and can honour it
# either by calling RETRO_ENVIRONMENT_SET_ROTATION or by rotating internally
# and reporting swapped 320x240 AV geometry (the harness accepts both).  Probe
# the registered core options with a zero-frame load; only run the assertion if
# an option that looks like a rotation/orientation setting exposes 90 or 270.
if [ -f "$FIXTURES/loopdemo.jar" ]; then
   ROT_LOG=$WORK/rotation-probe.log
   if $TIMEOUT "$HARNESS" --core "$CORE" --jar "$FIXTURES/loopdemo.jar" \
         --frames 0 --save-dir "$WORK/save-rot-probe" >"$ROT_LOG" 2>&1; then
      ROT_LINE=$(grep -iE '^\[coreopt\] key=[^ ]*(rot|orient)' "$ROT_LOG" |
                 head -n 1)
      if [ -z "$ROT_LINE" ]; then
         skip "rotation=90: core registers no rotation/orientation core option"
      else
         ROT_KEY=$(printf '%s\n' "$ROT_LINE" |
                   sed -e 's/^\[coreopt\] key=\([^ ]*\).*/\1/')
         ROT_VALS=$(printf '%s\n' "$ROT_LINE" | sed -e 's/.* values=//')
         case ",$ROT_VALS," in
            *,90,*)
               run_one "rotation=90 via core option $ROT_KEY" \
                  --core "$CORE" --jar "$FIXTURES/loopdemo.jar" \
                  --frames 30 --rotation 90 --set "$ROT_KEY=90" \
                  --save-dir "$WORK/save-rot"
               ;;
            *,270,*)
               run_one "rotation=270 via core option $ROT_KEY" \
                  --core "$CORE" --jar "$FIXTURES/loopdemo.jar" \
                  --frames 30 --rotation 270 --set "$ROT_KEY=270" \
                  --save-dir "$WORK/save-rot"
               ;;
            *)
               skip "rotation=90: option $ROT_KEY values [$ROT_VALS] expose no 90/270 value"
               ;;
         esac
      fi
   else
      fail "rotation probe: zero-frame load failed"
      tail -n 20 "$ROT_LOG" | sed 's/^/     /'
   fi
else
   skip "rotation=90: missing $FIXTURES/loopdemo.jar"
fi

# Heap is a core *option* too.  Automatic is the default and has to resolve to
# a concrete number at load: probe what the core registered, then run once and
# check that the resolved size reached the log.  Both halves matter - an option
# that says "auto" but never resolves is a VM created with a 0-byte arena, and
# a resolved log with no option is not something RetroArch can select.
if [ -f "$FIXTURES/loopdemo.jar" ]; then
   HEAP_LOG=$WORK/heap-probe.log
   if $TIMEOUT "$HARNESS" --core "$CORE" --jar "$FIXTURES/loopdemo.jar" \
         --frames 0 --save-dir "$WORK/save-heap-probe" >"$HEAP_LOG" 2>&1; then
      HEAP_LINE=$(grep -iE '^\[coreopt\] key=[^ ]*heap' "$HEAP_LOG" |
                  head -n 1)
      if [ -z "$HEAP_LINE" ]; then
         fail "heap automatic: core registers no heap core option"
      else
         HEAP_DEF=$(printf '%s\n' "$HEAP_LINE" |
                    sed -e 's/.* default=\([^ ]*\).*/\1/')
         HEAP_VALS=$(printf '%s\n' "$HEAP_LINE" | sed -e 's/.* values=//')
         case "$HEAP_DEF" in
            auto) pass "heap option defaults to auto" ;;
            *)    fail "heap option defaults to [$HEAP_DEF], want auto" ;;
         esac
         case ",$HEAP_VALS," in
            *,auto,*) pass "heap option offers auto (values $HEAP_VALS)" ;;
            *)        fail "heap option values [$HEAP_VALS] do not include auto" ;;
         esac
      fi
      RESOLVE_LOG=$WORK/heap-resolve.log
      if $TIMEOUT "$HARNESS" --core "$CORE" --jar "$FIXTURES/loopdemo.jar" \
            --frames 2 --save-dir "$WORK/save-heap-auto" \
            >"$RESOLVE_LOG" 2>&1; then
         if grep -qE '\[core\]\[info\].*heap: automatic, [0-9]+ MB for this host' \
               "$RESOLVE_LOG"; then
            pass "automatic heap resolves at load and is logged"
         else
            fail "automatic heap: no resolved-size log line"
            grep -i heap "$RESOLVE_LOG" | sed 's/^/     /' | head -n 5
         fi
      else
         fail "automatic heap: 2-frame load failed"
         tail -n 10 "$RESOLVE_LOG" | sed 's/^/     /'
      fi
   else
      fail "heap probe: zero-frame load failed"
      tail -n 20 "$HEAP_LOG" | sed 's/^/     /'
   fi
else
   skip "heap automatic: missing $FIXTURES/loopdemo.jar"
fi

say ""
say "== result: $PASSED passed, $FAILED failed, $SKIPPED skipped =="

if [ "$FAILED" -gt 0 ]; then
   exit 1
fi
if [ "$PASSED" -eq 0 ]; then
   say "FAIL no core checks ran (are the jar fixtures present?)"
   exit 2
fi
exit 0
