#!/bin/bash
# The device profile: a handset's properties are answers ON TOP of the
# runtime's own, and a key nobody answers is still null.
#
# 1945空战英雄_6105 refuses to start unless microedition.platform begins with
# "Nokia", and the corpus probes IMEI/IMSI spellings far more often than it
# probes the platform at all (see nj2me/nj_device.h for the counts).  So the
# runtime can be told which handset to be, and this test pins down what that
# means - in four runs of one fixture:
#
#   generic          the runtime's own facts, and no vendor keys invented
#   nokia6230        that handset's table, including its own kbd answer
#   k750 + --prop    an override landing on top of a profile
#   --prop java...   an override reaching a key the RUNTIME answers
#
# The last two are one run, and they are the reason the check is not just "does
# --device print the string": a table that silently dropped the caller's rows,
# or that could not be overridden, would pass the first two.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/devicedemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

fail=0
line=""

# Run the fixture once and keep its DEV line.
run() {
    log="$(mktemp)"
    timeout 60 "$BIN" --port headless --run-frames 3 "$@" "$JAR" >"$log" 2>&1
    line="$(grep -m1 '^DEV ' "$log" || true)"
    rm -f "$log"
    case "$line" in
        "DEV FAILED"*) echo "FAIL: the fixture itself threw: $line"; fail=1 ;;
        "") echo "FAIL: the fixture printed no DEV line (args: $*)"; fail=1 ;;
    esac
    echo "    [$*] $line"
}

expect() {   # field, expected, message
    case "$line" in
        *"$1=$2"*) ;;
        *) echo "FAIL: $3 ($1 is not $2)"; fail=1 ;;
    esac
}

# A property the runtime always answers has to have SOME value; the point here
# is that a profile cannot make it unreachable.
notnull() {
    case "$line" in
        *"$1=null"*) echo "FAIL: $1 is null, but the runtime answers it"; fail=1 ;;
    esac
}

echo "== generic (no --device): the runtime's own answers, no invented identity"
run
expect platform nano_j2me/headless "the port's own platform string did not survive"
expect nokiaimei null "a Nokia IMEI was answered with no device profile installed"
expect seimei null    "a Sony Ericsson IMEI was answered with no profile installed"
expect override null  "an override was answered without --prop"
notnull javaversion
notnull osname
expect profiles MIDP-2.0 "microedition.profiles is not the runtime's own answer"
expect unknown true  "a key nobody answers was answered anyway"
expect longkey true  "a 200-character key matched a shorter one"
expect emptykey true "the empty key was answered"

echo "== --device nokia6230"
run --device nokia6230
expect platform Nokia6230/05.50 "the Nokia platform string is wrong"
expect nokiaimei 357000000000009 "com.nokia.mid.imei is wrong"
expect nokiaoldimei 357000000000009 "the older com.nokia.IMEI spelling is missing"
expect imsi 310150000000000 "com.nokia.mid.imsi is wrong"
expect mcc 310 "com.nokia.mid.mcc is wrong"
# The desktop headless port answers com.nokia.keyboard.type=None, because a
# desktop has no keypad.  The profile has to be able to say otherwise or a
# title that hides its own soft keypad on a real Nokia still gets the desktop
# answer while pretending to be one.
expect kbd PhoneKeypad "the profile did not override the port's own keyboard answer"
expect seimei null "a Sony Ericsson IMEI was answered under the Nokia profile"
expect override null "an override was answered without --prop"
notnull javaversion
notnull osname
expect unknown true "a key nobody answers was answered anyway"

echo "== --device sonyericsson-k750 with an override"
run --device sonyericsson-k750 --prop com.example.test.override=hello
expect platform SonyEricssonK750i/R1J002 "the Sony Ericsson platform string is wrong"
expect seimei 357000000000009 "com.sonyericsson.imei is wrong"
expect nokiaimei null "a Nokia IMEI was answered under the Sony Ericsson profile"
expect override hello "--prop did not land (or the profile shadowed it)"
notnull javaversion

echo "== --prop over a key the runtime answers itself"
run --prop java.version=9.9-test
expect javaversion 9.9-test "--prop could not override one of the runtime's own keys"
expect platform nano_j2me/headless "the platform changed when only java.version was set"

[ "$fail" = 0 ] || exit 1
echo "PASS: the device profile answers a handset's keys, shadows nothing it should not, and a caller's own rows win"
