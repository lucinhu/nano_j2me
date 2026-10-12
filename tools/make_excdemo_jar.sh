#!/usr/bin/env bash
#
# Build the stack-trace fixture.
#
# ExcDemo catches its own exception and calls printStackTrace().  What that
# prints is the whole point: the class and message alone name nothing, so the
# test asserts on the frames.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/excdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/excdemo/ExcDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: ExcDemo\r\nMIDlet-1: ExcDemo, , ExcDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
