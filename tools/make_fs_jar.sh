#!/usr/bin/env bash
#
# The JSR 75 fixture: javax.microedition.io.file, including the escape attempts.
#
#     tools/make_fs_jar.sh [out.jar]
#
# The test points it at a directory it builds itself, so the entries it expects
# are the entries it created.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/fsdemo.jar}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/src/fsfix" "$WORK/classes/META-INF"
cp "$ROOT/tests/fsdemo/FsDemo.java" "$WORK/src/fsfix/FsDemo.java"

CP="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac -source 8 -target 8 -nowarn -cp "$CP" -d "$WORK/classes" \
      "$WORK/src/fsfix/FsDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: FsDemo\r\nMIDlet-Version: 1.0.0\r\nMIDlet-1: FsDemo, , fsfix.FsDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"

mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF . )
python3 "$ROOT/tools/check_cldc_api.py" "$OUT"
echo "wrote $OUT"
