#!/usr/bin/env bash
#
# Build the GameCanvas flush fixture.
#
# FlushDemo draws whole frames into its GameCanvas buffer from its own thread
# and flushes them - see tests/flushdemo/FlushDemo.java.
# tools/check_flushdemo.sh holds the expectations; this only builds the JAR.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/flushdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/flushdemo/FlushDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: FlushDemo\r\nMIDlet-1: FlushDemo, , FlushDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
