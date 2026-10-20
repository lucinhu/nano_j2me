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
# defines NJ_PIXFMT.
#
# Usage: tools/check_threaded_dispatch.sh [keysdemo.jar]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAR="${1:-$ROOT/tests/fixtures/keysdemo.jar}"
CC="${CC:-cc}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
command -v "$CC" >/dev/null 2>&1 || { echo "SKIP: no C compiler"; exit 0; }
[ -d "$ROOT/src/core" ] || { echo "SKIP: no sources at $ROOT"; exit 0; }

SRC="$ROOT/src/core/*.c $ROOT/src/midp/*.c $ROOT/src/gfx/*.c $ROOT/src/zip/*.c
     $ROOT/src/runner/nj_run.c $ROOT/src/port/posix/nj_port_headless.c
     $ROOT/apps/nj2me_main.c"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

build() {
    # $1 = output, rest = extra compiler flags
    out="$1"; shift
    # shellcheck disable=SC2086
    $CC -std=c99 -O1 -I"$ROOT/include" -I"$ROOT/src/core" -I"$ROOT/src/midp" \
        -I"$ROOT/src/gfx" -I"$ROOT/src/zip" -o "$out" $SRC "$@" -lm -lz \
        >"$WORK/build.log" 2>&1
}

fail=0
say() {
    if [ "$1" = 1 ]; then echo "FAIL $2"; fail=1; else echo "ok   $2"; fi
}

# ---- the configuration default must not depend on NJ_PIXFMT ----
probe() {
    out="$1"; shift
    # shellcheck disable=SC2086
    $CC -std=c99 -I"$ROOT/include" -o "$out" -x c - "$@" >/dev/null 2>&1 <<'PROBE'
#include "nj2me/nj_config.h"
int main(void) { return NJ_THREADED_DISPATCH; }
PROBE
}
probe "$WORK/cfg_none"; rc=$?
if [ "$rc" = 0 ]; then say 0 "a build with no defines selects the portable switch"
else say 1 "NJ_THREADED_DISPATCH defaults to 1 when NJ_PIXFMT is undefined"; fi
probe "$WORK/cfg_pix" -DNJ_PIXFMT=NJ_PIXFMT_RGB565; rc=$?
if [ "$rc" = 0 ]; then say 0 "a build with NJ_PIXFMT selects the portable switch"
else say 1 "NJ_THREADED_DISPATCH depends on NJ_PIXFMT again"; fi

# ---- both interpreters, same sources ----
if ! build "$WORK/switch"; then
    echo "SKIP: cannot build the runtime: $(tail -1 "$WORK/build.log")"
    exit 0
fi
if ! build "$WORK/threaded" -DNJ_THREADED_DISPATCH=1; then
    if grep -q "label address\|computed goto\|'&&'\|\"&&\"" "$WORK/build.log"; then
        echo "SKIP: compiler has no label addresses (computed goto)"
        exit 0
    fi
    echo "FAIL the threaded build does not compile:"
    tail -5 "$WORK/build.log"
    exit 1
fi

run() {
    timeout 90 "$1" --port headless --heap 8 --fps 0 --run-frames 60 \
        --size 240x320 --stats "$JAR" 2>&1
}

out_sw="$(run "$WORK/switch")"; rc_sw=$?
out_td="$(run "$WORK/threaded")"; rc_td=$?
say $([ "$rc_sw" = 0 ] && echo 0 || echo 1) "the switch build exits cleanly (exit $rc_sw)"
say $([ "$rc_td" = 0 ] && echo 0 || echo 1) "the threaded build exits cleanly (exit $rc_td)"

case "$out_td" in
    *VerifyError*|*"receiver is not an object reference"*)
        say 1 "the threaded build misaligned an invocation again" ;;
    *)  say 0 "the threaded build runs the fixture without a VerifyError" ;;
esac
case "$out_td" in
    *"run ended: the frame budget"*)
        say 0 "the threaded build executed the frame budget" ;;
    *)  say 1 "the threaded build did not finish its frames" ;;
esac

bc() { sed -n 's/.*bytecodes=\([0-9][0-9]*\).*/\1/p' <<<"$1" | head -1; }
b_sw="$(bc "$out_sw")"
b_td="$(bc "$out_td")"
if [ -n "$b_sw" ] && [ "$b_sw" = "$b_td" ]; then
    say 0 "both interpreters execute the same number of bytecodes ($b_sw)"
else
    say 1 "bytecode counts differ: switch='$b_sw' threaded='$b_td'"
fi

[ "$fail" = 0 ] || exit 1
echo "PASS: threaded dispatch"
exit 0
