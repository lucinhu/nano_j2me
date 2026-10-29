#!/usr/bin/env bash
#
# Samsung's com.samsung.util.SM/SMS, end to end through the interpreter.
#
# 三国塔防－曹操传_1610, 宠物王国2紫_5550 and 魂斗罗3代-完全版_3784 all build an
# SM, set its data and destination and call SMS.send().  The vendor classes
# used to be absent, so that feature died on NoClassDefFoundError.  A device
# without an SMS stack answers with the SDK's own contract: isSupported() false
# and send() throwing the declared IOException.
#
# Usage: check_samsung_sms.sh [jar]
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
            -d "$WORK/classes" "$ROOT/tests/samsungdemo/SamsungDemo.java"; then
        echo "FAIL: the fixture does not compile against the stub API"
        exit 1
    fi
    printf 'Manifest-Version: 1.0\r\nMIDlet-Name: SamsungDemo\r\nMIDlet-1: SamsungDemo, , SamsungDemo\r\n\r\n' \
        > "$WORK/classes/META-INF/MANIFEST.MF"
    JAR="$WORK/samsungdemo.jar"
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

LINE="$(grep -m1 '^SMS ' "$LOG" || true)"
if [ -z "$LINE" ]; then
    echo "FAIL: the fixture printed no SMS line"
    sed -n '1,20p' "$LOG"
    exit 1
fi
case "$LINE" in
    "SMS ERROR"*)
        echo "FAIL: the vendor classes did not resolve: $LINE"
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
expect "supported=false"   # no SMS transport
expect "data=ok"           # the value object keeps what was set
expect "dest=ok"
expect "send=IOException"  # the SDK's catchable answer, not a fake success

if [ "$fail" -ne 0 ]; then
    echo "  line: $LINE"
    exit 1
fi
echo "ok: $LINE"
exit 0
