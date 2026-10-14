#!/usr/bin/env bash
#
# Build the font fixture: a Canvas that draws the characters the font round is
# about and prints the advances it draws them with.
#
# It exists because "the text uses the phone's font" and "the runtime measures
# the glyphs it draws" are two different claims, and only one of them is
# visible on the screen.  The rects printed here are what make the second one
# checkable: a bundled fixed-cell font gives "iii" and "WWW" the same width.
#
# Usage: bash tests/textdemo/build.sh [out.jar]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/textdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/textdemo/TextDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: TextDemo\r\nMIDlet-1: TextDemo, , TextDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
