#!/bin/bash
# The device profile: a handset's properties are answers ON TOP of the
# runtime's own, and a key nobody answers is still null.
#
# 1945空战英雄_6105 refuses to start unless microedition.platform begins with
# "Nokia", and the corpus probes IMEI/IMSI spellings far more often than it
# probes the platform at all (see nj2me/nj_device.h for the counts).  So the
# runtime can be told which handset to be, and this test pins down what that
# means, including the selected handset's raw Canvas soft keys:
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

# Exercise the real CLI profile path, including both Canvas key callbacks.
run() {
    log="$(mktemp)"
    if ! timeout 60 "$BIN" --port headless --run-frames 6 \
        --keys '1:soft1 3:soft2' "$@" "$JAR" >"$log" 2>&1; then
        echo "FAIL: emulator failed (args: $*)"; fail=1
    fi
    keys="$(grep '^KEY ' "$log" || true)"
    line="$(grep -m1 '^DEV ' "$log" || true)"
    mline="$(grep -m1 '^DEVM ' "$log" || true)"
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

expect_keys() {
    local left="$1" right="$2" phase expected
    for phase in press release; do
        for expected in "KEY $phase $left SOFTKEY1" "KEY $phase $right SOFTKEY2"; do
            if ! grep -Fxq -- "$expected" <<<"$keys"; then
                echo "FAIL: missing callback: $expected (observed: $keys)"; fail=1
            fi
        done
    done
    [ "$(printf '%s\n' "$keys" | wc -l)" -eq 4 ] || {
        echo "FAIL: expected exactly four key callbacks: $keys"; fail=1;
    }
}

echo "== generic (no --device): the runtime's own answers, no invented identity"
run
expect_keys -6 -7
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
# JSR 135 defines eight MMAPI system properties.  A title is allowed to read
# them without a null check (1942空战 calls .equals on supports.mixing, and
# the NPE landed in its own catch), so every one has to be answered - with
# "false" for the four capability flags and a defined (possibly empty) string
# for the four lists.
# The eight MMAPI properties are on the DEVM line: the DEV line is already
# long enough that the runner's log buffer cut its tail.
line="$mline"
case "$line" in
    "DEVM"*) ;;
    *) echo "FAIL: the fixture printed no DEVM line (args: $*)"; fail=1 ;;
esac
expect mixing false    "supports.mixing must be defined; this runtime mixes no audio"
expect audiocap false  "supports.audio.capture must be defined (no capture here)"
expect videocap false  "supports.video.capture must be defined (no capture here)"
expect recording false "supports.recording must be defined (no recording here)"
expect audioenc false  "audio.encodings must be a defined string"
expect videoenc false  "video.encodings must be a defined string"
expect snapenc false   "video.snapshot.encodings must be a defined string"
expect streamable false "streamable.contents must be a defined string"

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

echo "== Motorola Canvas soft keys through --device"
run --device motorola-e398
expect platform Motorola-E398 "the Motorola profile did not reach the runner"
expect_keys -21 -22

echo "== property overrides preserve the selected Motorola input profile"
run --device motorola-e398 --prop microedition.platform=custom-phone
expect platform custom-phone "the caller's property override was lost"
expect_keys -21 -22

echo "== a platform string alone does not select Motorola input"
run --prop microedition.platform=Motorola-E398
expect platform Motorola-E398 "the standalone property override was lost"
expect_keys -6 -7

[ "$fail" = 0 ] || exit 1
echo "PASS: handset properties, overrides and Canvas soft-key callbacks follow the selected device"
