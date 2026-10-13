#!/usr/bin/env bash
#
# Build the constructor-wait fixture.
#
# WaitDemo's constructor starts a worker thread and waits for it, so starting
# the MIDlet depends on the runtime letting that worker run while the C caller
# is parked.  tools/check_wait_pump.sh holds the expectations; this only builds
# it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/waitdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/waitdemo/WaitDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: WaitDemo\r\nMIDlet-1: WaitDemo, , WaitDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
