#!/usr/bin/env bash
#
# Build the stack-trace fixture.
#
# This MIDlet throws from paint() on every frame, so the runner never
# sees a painted frame.  The run must still end at the frame budget.
#
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/paintthrow.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/paintthrow/PaintThrowDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: PaintThrowDemo\r\nMIDlet-1: PaintThrowDemo, , PaintThrowDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
