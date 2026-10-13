#!/usr/bin/env bash
#
# Build the clip/translate fixture.
#
# ClipDemo draws into an offscreen image and reports four things about the clip
# and the coordinate system: setClip() then translate(), translate() then
# setClip(), an empty clip, and the getClipX/Y/Width/Height round trip.  The
# expectations live in tools/check_clip_translate.sh; this only builds it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/clipdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/clipdemo/ClipDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: ClipDemo\r\nMIDlet-1: ClipDemo, , ClipDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
