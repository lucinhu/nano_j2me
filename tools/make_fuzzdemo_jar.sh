#!/usr/bin/env bash
#
# Build the JSR 184 / JSR 75 fuzz driver.
#
# FuzzDemo walks the M3G and FileConnection entry points with a seeded mix of
# legal and illegal arguments; a sanitizer abort under a given seed is the
# finding.  This only builds it; see docs/notes/2026-11-02-m3g-fuzz.md for how
# the runs are driven.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/fuzzdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
M3G_STUBS="$(python3 "$ROOT/tools/m3g_stubs.py" --quiet)"
trap 'rm -rf "$WORK" "$M3G_STUBS"' EXIT
javac --release 8 -nowarn -classpath "$STUBS:$M3G_STUBS" -d "$WORK/classes" \
    "$ROOT/tests/fuzzdemo/FuzzDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: FuzzDemo\r\nMIDlet-1: FuzzDemo, , FuzzDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
