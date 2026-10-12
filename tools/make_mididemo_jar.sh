#!/usr/bin/env bash
#
# Build the MIDI playback fixture.
#
# MidiDemo opens "/song.mid" through Manager.createPlayer() and prints the
# Player's duration and state.  Any Standard MIDI File will do; with no
# argument the script takes one out of a corpus title, so the fixture is
# driven by the same kind of file the corpus ships.
#
#   tools/make_mididemo_jar.sh [out.jar] [song.mid]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/mididemo.jar}"
MID="${2:-}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

if [ -z "$MID" ]; then
    JAR="$ROOT/tests/games/龙火2加强版_12437.jar"
    if [ ! -f "$JAR" ]; then
        echo "no song.mid given and $JAR is not fetched" >&2
        exit 1
    fi
    MID="$WORK/song.mid"
    unzip -p "$JAR" "*.mid" > "$MID"
fi

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/mididemo/MidiDemo.java"
cp "$MID" "$WORK/classes/song.mid"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: MidiDemo\r\nMIDlet-1: MidiDemo, , MidiDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class song.mid )
echo "wrote $OUT"
