#!/usr/bin/env bash
# Build the round-163 fixture MIDlet (Siemens mobile APIs + resource: streams).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/siemensdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"
printf 'siemens-demo-resource\n' > "$WORK/classes/resdemo.txt"
STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/siemensdemo/SiemensDemo.java" \
    "$ROOT/tests/siemensdemo/SiemensDemoMidlet.java"
printf 'Manifest-Version: 1.0\r\nMIDlet-Name: SiemensDemo\r\nMIDlet-1: SiemensDemo, , SiemensDemoMidlet\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class resdemo.txt )
echo "wrote $OUT"
