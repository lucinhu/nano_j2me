#!/usr/bin/env bash
#
# JSR 75's PIM half, end to end through the interpreter.
#
# 3D孤胆枪手2010_4634 opens the contact list and walks it; with the classes
# absent it got a NoClassDefFoundError inside its own phone-book feature.  A
# fresh phone answers with an empty list, which is what this runtime does: it
# has no contact store to configure, so no field is supported, items() is empty
# and a list type it has no store for raises PIMException.
#
# Usage: check_pim.sh [jar]
#   NJ_BIN overrides the emulator under test (default build/nj2me).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if ! command -v javac >/dev/null 2>&1 || ! command -v jar >/dev/null 2>&1; then
    echo "SKIP: no JDK (javac/jar) to build the fixture"
    exit 77
fi
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 77; }

if [ -z "$JAR" ]; then
    STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)" || {
        echo "SKIP: the stub generator failed"; exit 77; }
    mkdir -p "$WORK/classes/META-INF"
    if ! javac --release 8 -nowarn -classpath "$STUBS" \
            -d "$WORK/classes" "$ROOT/tests/pimdemo/PimDemo.java"; then
        echo "FAIL: the fixture does not compile against the stub API"
        exit 1
    fi
    printf 'Manifest-Version: 1.0\r\nMIDlet-Name: PimDemo\r\nMIDlet-1: PimDemo, , PimDemo\r\n\r\n' \
        > "$WORK/classes/META-INF/MANIFEST.MF"
    JAR="$WORK/pimdemo.jar"
    ( cd "$WORK/classes" && jar cfm "$JAR" META-INF/MANIFEST.MF *.class ) || {
        echo "FAIL: could not package the fixture"; exit 1; }
fi

LOG="$WORK/run.log"
if ! timeout 120 "$BIN" --port headless --run-frames 120 --heap 8 "$JAR" \
        >"$LOG" 2>&1; then
    echo "FAIL: the emulator failed to run the fixture"
    sed -n '1,20p' "$LOG"
    exit 1
fi

LINE="$(grep -m1 '^PIM ' "$LOG" || true)"
if [ -z "$LINE" ]; then
    echo "FAIL: the fixture printed no PIM line"
    sed -n '1,20p' "$LOG"
    exit 1
fi
case "$LINE" in
    "PIM ERROR"*)
        echo "FAIL: the PIM classes did not resolve: $LINE"
        exit 1
        ;;
esac

fail=0
expect() {   # $1 = field=value
    case "$LINE" in
        *"$1"*) ;;
        *) echo "FAIL: expected $1 in: $LINE"; fail=1 ;;
    esac
}
expect "opened=true"       # the contact list opens
expect "supported=false"   # a store with no fields supports none
expect "items=0"           # and its enumeration is real but empty
expect "todo=PIMException" # a type with no store says so, per the spec
expect "mode=IAE"          # an unknown mode is IllegalArgumentException
expect "names=1"           # the contact list is listed under a name
expect "closed=true"       # close() returns

if [ "$fail" -ne 0 ]; then
    echo "  line: $LINE"
    exit 1
fi
echo "ok: $LINE"
exit 0
