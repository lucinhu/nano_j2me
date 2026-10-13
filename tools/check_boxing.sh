#!/bin/bash
# A boxed value in a string is its value, not its identity.
#
# CLDC declares toString(), equals(Object) and hashCode() on every wrapper.
# When one of them is missing here the call resolves up to java.lang.Object
# and the program keeps running with the wrong answer: a path built by
# concatenation becomes "/global_data/java.lang.Integer@c1803e7" (which is how
# 模拟人生2_10762 lost its content and went black), and a Hashtable keyed on
# Longs or Booleans buckets every key by identity.
#
# The fixture prints three lines; this file asserts each field.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/boxdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

timeout 60 "$BIN" --port headless --run-frames 3 "$JAR" >"$log" 2>&1
rc=$?

if [ "$rc" != 0 ]; then
    echo "FAIL: the emulator exited $rc"
    fail=1
fi
if grep -q "BOX FAILED" "$log"; then
    echo "FAIL: the fixture itself threw: $(grep -m1 'BOX FAILED' "$log")"
    fail=1
fi

check() {
    what="$1"; got="$2"; want="$3"
    if [ "$got" != "$want" ]; then
        echo "FAIL: $what"
        echo "      got:  $got"
        echo "      want: $want"
        fail=1
    fi
}

check "an Integer concatenated into a string is its value" \
    "$(grep -m1 '^BOX path=' "$log")" \
    "BOX path=/global_data/34795 long=4294967296 short=-12 byte=7 char=A bool=true,false"
check "the wrappers compare by value, and only within their own class" \
    "$(grep -m1 '^BOX eq=' "$log")" \
    "BOX eq=true,false,true,true,true,false"
check "hashCode() is the value (and Boolean's is 1231/1237)" \
    "$(grep -m1 '^BOX hash=' "$log")" \
    "BOX hash=1,-12,1231,65"
# Float and Double follow the same rules, with Java's own details: a whole
# value prints with a ".0", equals() compares the BIT patterns (so NaN equals
# NaN and +0.0f does not equal -0.0f), and hashCode() is floatToIntBits()
# resp. the xor of the two halves of doubleToLongBits().
check "Float/Double print and compare the Java way" \
    "$(grep -m1 '^BOX f=' "$log")" \
    "BOX f=1.5,2.0,true,false,1069547520,2.5,1074003968"

if grep -q "uncaught exception" "$log"; then
    echo "FAIL: an exception escaped a thread"
    fail=1
fi

[ "$fail" = 0 ] || exit 1
echo "PASS: a boxed value prints as its value and compares by value"
