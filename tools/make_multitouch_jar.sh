#!/usr/bin/env bash
#
# Build the multi-touch fixture MIDlet: registers a Nokia MultipointTouch
# listener the way Minecraft J2ME PE does and prints every changed pointer id
# with the state and position the runtime reports.
#
#   tools/make_multitouch_jar.sh                     # tests/fixtures/multitouchdemo.jar
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/multitouchdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"
STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/multitouchdemo/MultitouchDemo.java"
printf 'Manifest-Version: 1.0\r\nMIDlet-Name: MultitouchDemo\r\nMIDlet-1: MultitouchDemo, , MultitouchDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
