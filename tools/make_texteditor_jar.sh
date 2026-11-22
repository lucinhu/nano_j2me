#!/usr/bin/env bash
# Build the round-190 fixture MIDlet (Nokia TextEditor family).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/texteditordemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"
STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/texteditordemo/TextEditDemo.java"
printf 'Manifest-Version: 1.0\r\nMIDlet-Name: TextEditDemo\r\nMIDlet-1: TextEditDemo, , TextEditDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
