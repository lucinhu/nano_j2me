#!/usr/bin/env bash
# Build the Class.forName initialisation fixture MIDlet.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/classinitdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"
STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/classinitdemo/ClassInitDemo.java" \
    "$ROOT/tests/classinitdemo/InitProbe.java"
printf 'Manifest-Version: 1.0\r\nMIDlet-Name: ClassInitDemo\r\nMIDlet-1: ClassInitDemo, , ClassInitDemo\r\nMIDlet-Version: 1.0\r\nMIDlet-Vendor: nano_j2me\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
