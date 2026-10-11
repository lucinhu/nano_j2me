#!/usr/bin/env bash
#
# The constant-consistency fixture: a MIDlet that prints every constant the MIDP
# stubs declare, next to the values it should print.
#
#     tools/make_const_jar.sh [out.jar]
#
# WHY.  A `public static final int` in the stubs is INLINED by javac at every use
# site, so a compiled MIDlet carries the STUB's value while the runtime compares
# against its OWN.  List.IMPLICIT was 1 in the stubs and 3 in the runtime, so
# every MIDlet asking for an implicit list got an exclusive one and nothing said
# so.  Every constant in tools/midp_stubs.py is a promise about the runtime, and
# this is what checks the promise.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/constdemo.jar}"
EXPECTED="${2:-$ROOT/tests/fixtures/constdemo.expected}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 "$ROOT/tools/gen_const_fixture.py" "$WORK" >/dev/null
mkdir -p "$WORK/classes/META-INF"
CP="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac -source 8 -target 8 -nowarn -cp "$CP" -d "$WORK/classes" \
      "$WORK/src/consts/ConstDemo.java"
printf 'Manifest-Version: 1.0\r\nMIDlet-Name: ConstDemo\r\nMIDlet-Version: 1.0.0\r\nMIDlet-1: ConstDemo, , consts.ConstDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"

mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF . )
cp "$WORK/expected.txt" "$EXPECTED"
echo "wrote $OUT and $EXPECTED ($(wc -l < "$EXPECTED") constants)"
