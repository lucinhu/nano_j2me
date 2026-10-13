#!/bin/bash
# A static field must hide a superclass's instance field of the same name.
#
# JVMS 5.4.3.2 resolves a Fieldref by NAME and DESCRIPTOR through the
# referenced class and then its superclasses; static-ness is a property of the
# field that WINS, not a filter on the search.  Resolving the instance variant
# first instead made 疯狂农场2_2375 lose its screen: class h declares a static
# `Vector b` while its superclass j declares an instance `Vector b`, so
# `getstatic h.b` and `putstatic h.b` were linked to j's instance offset.  A
# getstatic then read the field off a NULL receiver and a putstatic dropped the
# value - silently, both of them - so the Vector the constructor had stored
# read back as null and the next addElement() threw NullPointerException in a
# method that had nothing to do with the field.
#
#   FIELD before=null after=1:first grown=2:first
#
# `before=null` is correct (nothing has seeded it yet); `after` is the round
# trip through putstatic+getstatic; `grown` catches a read that only works
# because the value happened to land in the slot the first write went to.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/fielddemo.jar}"
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
    *"FIELD before=null after=1:first grown=2:first"*)
        say 0 "the static field holds what the constructor put there" ;;
    *) say 1 "the static field did not hold its value" ;;
esac

case "$out" in
    *"NullPointerException"*) say 1 "a null receiver was dereferenced" ;;
    *) say 0 "no null receiver was dereferenced" ;;
esac

case "$out" in
    *"IncompatibleClassChangeError"*)
        say 1 "a static/instance field mismatch was reported" ;;
    *) say 0 "no static/instance field mismatch was reported" ;;
esac

case "$out" in
    *"uncaught exception"*) say 1 "an exception escaped a thread" ;;
    *) say 0 "no exception escaped a thread" ;;
esac

[ "$fail" = 0 ] && echo "PASS: a static field hides a superclass instance field of the same name"
exit "$fail"
