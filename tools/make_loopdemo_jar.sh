#!/usr/bin/env bash
#
# Build the blocked-constructor fixture.
#
# LoopDemo's <init> calls Thread.sleep().  A blocking native cannot suspend
# the C caller that invoked it, so the bridge gives up - and the point of this
# fixture is that giving up must still produce a NAMED exception.  It used to
# produce none at all, and the runner turned that into
# "cannot instantiate X: ?", which says nothing to whoever is reading it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/loopdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/loopdemo/LoopDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: LoopDemo\r\nMIDlet-1: LoopDemo, , LoopDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
