#!/usr/bin/env bash
#
# Build the draw-through-a-saved-Graphics fixture.
#
# SavedGfx keeps the Graphics paint() handed it and draws through it from a
# worker thread - see tests/savedgfx/SavedGfx.java.  tools/check_savedgfx.sh
# holds the expectations; this only builds the JAR.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/savedgfx.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/savedgfx/SavedGfx.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: SavedGfx\r\nMIDlet-1: SavedGfx, , SavedGfx\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
