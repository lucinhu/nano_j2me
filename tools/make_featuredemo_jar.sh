#!/usr/bin/env bash
#
# Build the frontend-feature fixture (tests/featuredemo/FeatureDemo.java).
#
# The mock frontend test (tests/test_libretro_frontend.c) consumes the
# "FEATURE locale=..." / "FEATURE canvas=..." log lines and observes the
# Display.vibrate() calls.  The jar is normally built into a temporary
# directory by tools/check_libretro_frontend.sh; this script only builds it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/featuredemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/featuredemo/FeatureDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: FeatureDemo\r\nMIDlet-1: FeatureDemo, , FeatureDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
