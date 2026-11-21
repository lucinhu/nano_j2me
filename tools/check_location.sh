#!/bin/sh
# MIT. Drive the JSR-179 fixture and assert the javax.microedition.location
# surface: provider namespace, value classes, landmine-free error paths.
#
#   NJ_BIN=build/nj2me tools/check_location.sh tests/fixtures/locationdemo.jar
#
# Exit status: 0 = PASS, 1 = FAIL, 77 = fixture missing (SKIP).
set -u

JAR=${1:-}
BIN=${2:-${NJ_BIN:-build/nj2me}}
FRAMES=${NJ_FRAMES:-120}
TIMEOUT=${TIMEOUT:-120}

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

[ -n "$JAR" ] || fail "usage: check_location.sh <locationdemo.jar> [bin]"
[ -f "$JAR" ] || { echo "SKIP: missing $JAR" >&2; exit 77; }
[ -x "$BIN" ] || fail "no CLI binary at $BIN"

OUT=$(mktemp)
ERR=$(mktemp)
trap 'rm -f "$OUT" "$ERR"' EXIT INT TERM

timeout "$TIMEOUT" "$BIN" --port headless --size 240x320 --heap 16 --fps 30 \
    --run-frames "$FRAMES" --verbose 3 --stats --rms-dir "$(mktemp -d)" \
    "$JAR" >"$OUT" 2>"$ERR"
STATUS=$?
[ "$STATUS" -eq 0 ] || { cat "$OUT" "$ERR" >&2; fail "CLI exited $STATUS"; }

ALL=$(cat "$OUT" "$ERR")
have() { printf '%s\n' "$ALL" | grep -q "$1"; }

have 'LOCATION criteria=50/1/true'  || fail "Criteria getters mismatch"
have 'LOCATION provider=true state=1' || fail "provider not available"
have 'LOCATION last valid=true lat=60170 lon=24938' || fail "last known fix wrong"
have 'LOCATION fix valid=true lat=60170 lon=24938' || fail "getLocation fix wrong"
have 'LOCATION distance=11119 azimuth=0' || fail "distance/azimuth wrong"
have 'LOCATION accuracy=30/10' || fail "QualifiedCoordinates accuracies wrong"
have 'LOCATION convert=60170' || fail "Coordinates.convert(String) failed"
have 'formatted=24:56.304' || fail "Coordinates.convert(double,int) failed"
have 'LOCATION orientation=0/true/' || fail "Orientation.getOrientation failed"
have 'LOCATION address=Main Street 1/Helsinki/+358 0 000 000/demo@example.org' \
    || fail "AddressInfo fields wrong"
have 'LOCATION invalid=caught' || fail "invalid coordinate was accepted"
have 'LOCATION proximity=caught' || fail "proximity monitoring did not throw"
have 'LOCATION updated=1 valid=true' || fail "location listener not called"
printf '%s\n' "$ALL" | grep -q 'LOCATION failed=' && fail "a location call failed"
printf '%s\n' "$ALL" | grep -q 'NoSuchMethodError' && fail "NoSuchMethodError"
printf '%s\n' "$ALL" | grep -q 'NoClassDefFoundError' && fail "NoClassDefFoundError"
printf '%s\n' "$ALL" | grep -q 'uncaught exception' && fail "uncaught exception"

echo "PASS: JSR-179 location API exercised through the fixture"
exit 0
