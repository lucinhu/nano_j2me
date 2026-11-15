#!/usr/bin/env bash
#
# Build the JPEG/GIF decoder fixture.
#
# ImagesDemo decodes res/sample.jpg and res/sample.gif at startApp and prints
# their sizes ("IMAGE jpeg 17x13", "IMAGE gif ...").  The media fuzzer mutates
# those two resources; the fixture makes a decoded image or a caught exception
# both observable, and a crash/hang visible.
#
# The resources are the checked-in synthetic assets from tests/fixtures/jpeg
# and tests/fixtures/gif (not game data), so the fixture stays redistributable.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/imagesdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF" "$WORK/classes/res"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/imagesdemo/ImagesDemo.java"

cp "$ROOT/tests/fixtures/jpeg/gray_17x13.jpg" "$WORK/classes/res/sample.jpg"
cp "$ROOT/tests/fixtures/gif/anim.gif" "$WORK/classes/res/sample.gif"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: ImagesDemo\r\nMIDlet-1: ImagesDemo, , ImagesDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class res/sample.jpg res/sample.gif )
echo "wrote $OUT"
