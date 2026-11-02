#!/usr/bin/env bash
#
# Build the .m3g mutation-loader harness (task-31).
#
# M3gMutDemo reads file:///root/mut/manifest.txt from the runtime's fs dir and
# feeds each listed file to Loader.load(byte[], 0).  Mutant files are produced
# by tools/m3g_mutation_fuzz.py; see docs/notes/2026-11-03-m3g-mutation-fuzz.md.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/m3gmutdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
M3G_STUBS="$(python3 "$ROOT/tools/m3g_stubs.py" --quiet)"
trap 'rm -rf "$WORK" "$M3G_STUBS"' EXIT
mkdir -p "$WORK/classes/META-INF"

javac --release 8 -nowarn -classpath "$STUBS:$M3G_STUBS" -d "$WORK/classes" \
    "$ROOT/tests/m3gmutdemo/M3gMutDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: M3gMutDemo\r\nMIDlet-1: M3gMutDemo, , M3gMutDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
