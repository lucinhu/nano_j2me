#!/usr/bin/env bash
#
# Build the parked-timer-task fixture.
#
# TimerDemo schedules a java.util.TimerTask whose run() waits, so the timer
# thread parks inside the native that called it.  Starting the MIDlet, and
# painting while it is parked, is what the runtime has to get right;
# tools/check_timer_wait.sh holds the expectations and this only builds it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/timerdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/timerdemo/TimerDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: TimerDemo\r\nMIDlet-1: TimerDemo, , TimerDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
