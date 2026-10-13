#!/bin/bash
# A static field with a ConstantValue attribute must take that value.
#
# JVMS 5.5 step 4: during class initialisation a static field carrying a
# ConstantValue attribute is assigned before <clinit> runs.  javac emits the
# attribute for a `static final` field initialised with a constant expression
# and writes NO assignment for it into <clinit>, so a runtime that ignores the
# attribute leaves such a field at 0 or null forever.  That is not theoretical:
# of the 395 titles in tests/games, 20 static fields across 11 titles are read
# by a getstatic and never assigned by any <clinit>.
#
#   CV a=42 s=cv-string b=7 l=1311768467463790320 f=2.5
#
# `b` is the control: a plain static the runtime assigns in a real <clinit>,
# which pins down that this is about the attribute and not about statics in
# general.  `l` and `f` also cover the wide and float cases, where the field
# must receive the constant's BYTES - a float field read or written through a
# converting accessor answers a different number entirely.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/cvdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
out="$(timeout 60 "$BIN" --port headless --heap 8 --size 176x208 --fps 30 \
        --run-frames 5 --stats "$JAR" 2>&1)"
rc=$?

say() {
    if [ "$1" = 1 ]; then
        echo "FAIL $2"
        fail=1
    else
        echo "ok   $2"
    fi
}

[ "$rc" = 0 ]; say $? "the run exits cleanly (exit $rc)"

case "$out" in
    *"CV a=42 s=cv-string b=7 l=1311768467463790320 f=2.5"*)
        say 0 "every constant came from its ConstantValue attribute" ;;
    *) say 1 "a static final field did not hold its constant" ;;
esac

case "$out" in
    *"CV a=0 "*|*" s=null"*|*" l=0 "*|*" f=0.0"*)
        say 1 "a constant read as its default value" ;;
    *) say 0 "no constant read as its default value" ;;
esac

case "$out" in
    *"uncaught exception"*) say 1 "an exception escaped a thread" ;;
    *) say 0 "no exception escaped a thread" ;;
esac

[ "$fail" = 0 ] && echo "PASS: a ConstantValue field takes its constant before <clinit>"
exit "$fail"
