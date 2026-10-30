#!/usr/bin/env bash
#
# Build the monitor-exclusivity fixture.
#
# LockDemo runs two threads through the same synchronized block and yields
# inside it; tools/check_lockdemo.sh holds the expectations.  This only builds
# it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/lockdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/lockdemo/LockDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: LockDemo\r\nMIDlet-1: LockDemo, , LockDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
