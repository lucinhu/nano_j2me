#!/bin/bash
# The computed-goto interpreter is a second implementation of all 256 opcodes
# and it has to agree with the portable switch.
#
# It was broken for as long as it existed: a converted handler's DISPATCH()
# fetches the next opcode before branching, but the dispatch table sent
# unconverted opcodes to L_slow, which fetched a SECOND time.  Every
# converted-to-unconverted transition therefore lost one byte, and
# tests/fixtures/keysdemo.jar turned the resulting misaligned operand stack into
#
#     java.lang.VerifyError at KeysDemo$Pad.<init>()V pc=14:
#     receiver is not an object reference
#
# Nothing caught it because the dispatch default was nested inside the
# `#ifndef NJ_PIXFMT` guard: CMake defines a pixel format and got the switch,
# while a hand build got the threaded interpreter - so the broken path was
# never the one the tests ran.
#
# This check builds BOTH interpreters from the same sources and requires them
# to run the fixture with the same bytecode count and no VerifyError.  It also
# pins the configuration default: a build that defines nothing must select the
# portable switch (exit status 0 from the probe below), and so must one that
# defines NJ_PIXFMT. A missing compiler/fixture or unsupported computed goto
# may skip; a project build or runtime failure must fail the check.
#
# Usage: tools/check_threaded_dispatch.sh [keysdemo.jar]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAR="${1:-$ROOT/tests/fixtures/keysdemo.jar}"
CC="${CC:-cc}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
command -v "$CC" >/dev/null 2>&1 || { echo "SKIP: no C compiler"; exit 0; }
[ -d "$ROOT/src/core" ] || { echo "FAIL no sources at $ROOT"; exit 1; }
command -v timeout >/dev/null 2>&1 || { echo "FAIL timeout is required"; exit 1; }

# Include the CLI's portable client helpers, just as NJ_SOURCES does in CMake.
# Omitting them made the CLI fail to link, which used to be reported as SKIP.
SRC=("$ROOT"/src/core/*.c "$ROOT"/src/midp/*.c "$ROOT"/src/gfx/*.c
     "$ROOT"/src/zip/*.c "$ROOT/src/runner/nj_run.c"
     "$ROOT/src/client/nj_library.c" "$ROOT/src/client/nj_client_config.c"
     "$ROOT/src/client/nj_client_ui.c"
     "$ROOT/src/port/common/nj_system_amr.c"
     "$ROOT/src/port/posix/nj_port_headless.c" "$ROOT/apps/nj2me_main.c")
WORK="$(mktemp -d)" || exit 1
[ -d "$WORK" ] && [ ! -L "$WORK" ] || exit 1
trap 'rm -rf -- "${WORK:?}"' EXIT

build() {
    # $1 = output, rest = extra compiler flags
    local out="$1"; shift
    "$CC" -std=c99 -O1 -I"$ROOT/include" -I"$ROOT/src/core" -I"$ROOT/src/midp" \
        -I"$ROOT/src/gfx" -I"$ROOT/src/zip" -I"$ROOT/src/port/posix" \
        -o "$out" "${SRC[@]}" "$@" -lm -lz >"$WORK/build.log" 2>&1
}

fail=0
say() {
    if [ "$1" = 1 ]; then echo "FAIL $2"; fail=1; else echo "ok   $2"; fi
}

# ---- the configuration default must not depend on NJ_PIXFMT ----
probe() {
    local out="$1"; shift
    if ! "$CC" -std=c99 -I"$ROOT/include" -o "$out" -x c - "$@" \
        >"$WORK/probe.log" 2>&1 <<'PROBE'
#include "nj2me/nj_config.h"
int main(void) { return NJ_THREADED_DISPATCH; }
PROBE
    then
        echo "FAIL cannot compile the dispatch configuration probe:"
        cat "$WORK/probe.log"
        exit 1
    fi
    # Compiling alone cannot check the macro's value: execute the probe.
    "$out"
}
probe "$WORK/cfg_none"; rc=$?
if [ "$rc" = 0 ]; then say 0 "a build with no defines selects the portable switch"
else say 1 "NJ_THREADED_DISPATCH defaults to 1 when NJ_PIXFMT is undefined"; fi
probe "$WORK/cfg_pix" -DNJ_PIXFMT=NJ_PIXFMT_RGB565; rc=$?
if [ "$rc" = 0 ]; then say 0 "a build with NJ_PIXFMT selects the portable switch"
else say 1 "NJ_THREADED_DISPATCH depends on NJ_PIXFMT again"; fi
[ "$fail" = 0 ] || exit 1

# Probe the language extension separately. A project error mentioning && must
# never be mistaken for an unsupported compiler and silently skipped.
if ! "$CC" -std=c99 -o "$WORK/computed_goto" -x c - \
    >"$WORK/probe.log" 2>&1 <<'PROBE'
int main(void) {
    void *next = &&done;
    goto *next;
done:
    return 0;
}
PROBE
then
    echo "SKIP: compiler has no label addresses (computed goto)"
    cat "$WORK/probe.log"
    exit 0
fi
if ! "$WORK/computed_goto"; then
    echo "FAIL the computed-goto probe does not run"
    exit 1
fi

# ---- both interpreters, same sources ----
if ! build "$WORK/switch" -DNJ_THREADED_DISPATCH=0; then
    echo "FAIL the switch runtime does not build:"
    cat "$WORK/build.log"
    exit 1
fi
if ! build "$WORK/threaded" -DNJ_THREADED_DISPATCH=1; then
    echo "FAIL the threaded runtime does not build:"
    cat "$WORK/build.log"
    exit 1
fi

run() {
    timeout 90 "$1" --port headless --heap 8 --fps 0 --run-frames 60 \
        --size 240x320 --stats --rms-dir "$1.rms" "$JAR" 2>&1
}

out_sw="$(run "$WORK/switch")"; rc_sw=$?
out_td="$(run "$WORK/threaded")"; rc_td=$?
say $([ "$rc_sw" = 0 ] && echo 0 || echo 1) "the switch build exits cleanly (exit $rc_sw)"
say $([ "$rc_td" = 0 ] && echo 0 || echo 1) "the threaded build exits cleanly (exit $rc_td)"

for mode in switch threaded; do
    if [ "$mode" = switch ]; then out="$out_sw"; else out="$out_td"; fi
    case "$out" in
        *VerifyError*|*"receiver is not an object reference"*|*"uncaught exception"*)
            say 1 "the $mode build reported an uncaught exception or VerifyError" ;;
        *)  say 0 "the $mode build runs the fixture without a VerifyError" ;;
    esac
    case "$out" in
        *"run ended: the frame budget"*)
            say 0 "the $mode build executed the frame budget" ;;
        *)  say 1 "the $mode build did not finish its frames" ;;
    esac
done

stat() { sed -n "s/.*stats: .*${1}=\([0-9][0-9]*\).*/\1/p" <<<"$2" | head -1; }
f_sw="$(stat frames "$out_sw")"
f_td="$(stat frames "$out_td")"
if [ "$f_sw" = 60 ] && [ "$f_td" = 60 ]; then
    say 0 "both interpreters present 60 frames"
else
    say 1 "frame counts differ: switch='$f_sw' threaded='$f_td' (expected 60)"
fi
b_sw="$(stat bytecodes "$out_sw")"
b_td="$(stat bytecodes "$out_td")"
if [[ "$b_sw" =~ ^[1-9][0-9]*$ ]] && [ "$b_sw" = "$b_td" ]; then
    say 0 "both interpreters execute the same number of bytecodes ($b_sw)"
else
    say 1 "bytecode counts differ: switch='$b_sw' threaded='$b_td'"
fi

if [ "$fail" != 0 ]; then
    printf '\n--- switch runtime ---\n%s\n--- threaded runtime ---\n%s\n' "$out_sw" "$out_td"
    exit 1
fi
echo "PASS: threaded dispatch"
exit 0
