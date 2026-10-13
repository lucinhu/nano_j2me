#!/usr/bin/env bash
#
# Build the field-shadowing fixture.
#
# FieldDemo's FieldChild declares a STATIC Vector b while its superclass
# declares an INSTANCE Vector b with the same name and descriptor, which is the
# shape 疯狂农场2_2375 uses.  tools/check_field_shadow.sh holds the
# expectations; this only builds it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/fielddemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/fielddemo/FieldDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: FieldDemo\r\nMIDlet-1: FieldDemo, , FieldDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF ./*.class )
echo "wrote $OUT"
