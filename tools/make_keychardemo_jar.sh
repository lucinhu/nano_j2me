#!/usr/bin/env bash
#
# Build the getKeyChar() fixture (tests/keychardemo/KeyCharDemo.java).
#
# The expectations live in tools/check_keychar.sh; this only builds it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/keychardemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/keychardemo/KeyCharDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: KeyCharDemo\r\nMIDlet-1: KeyCharDemo, , KeyCharDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
