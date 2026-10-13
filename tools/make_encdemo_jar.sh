#!/usr/bin/env bash
#
# Build the encoding-name fixture.
#
# The encoding table in src/core/nj_lib_string.c is matched case-insensitively
# because Java says encoding names are case-insensitive and 48 of the collected
# titles spell UTF-8 some way other than "UTF-8".  EncDemo walks the spellings
# and checks the bytes, not just the acceptance.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/encdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/encdemo/EncDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: EncDemo\r\nMIDlet-1: EncDemo, , EncDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
