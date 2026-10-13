#!/usr/bin/env bash
#
# Build the paint-through-a-field fixture.
#
# RepaintDemo publishes its paint state in a field, calls repaint() and forces
# the paint with serviceRepaints() before clearing the field again - the idiom
# 仙魔飞行棋_417 uses with mobile/k.f.  tools/check_repaint.sh holds the
# expectations; this only builds the JAR.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/repaintdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/repaintdemo/RepaintDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: RepaintDemo\r\nMIDlet-1: RepaintDemo, , RepaintDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
