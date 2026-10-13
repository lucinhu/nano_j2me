#!/usr/bin/env bash
#
# Build the speed-clock fixture: tests/speeddemo/SpeedDemo.java.
#
# The fixture itself lives in tests/ and so does its build, rather than in
# tools/make_*_jar.sh, so that everything a reviewer needs to re-run the
# speed evidence is in one directory.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/speeddemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/speeddemo/SpeedDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: SpeedDemo\r\nMIDlet-1: SpeedDemo, , SpeedDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
