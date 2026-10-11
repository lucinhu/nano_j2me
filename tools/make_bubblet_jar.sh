#!/usr/bin/env bash
#
# Build Bubblet - a real, GPL-licensed J2ME game - from source, with this
# project's MIDP stubs as the compile classpath.
#
#     tools/make_bubblet_jar.sh
#     build/nj2me --port headless tests/games/bubblet.jar
#
# Source: https://github.com/Summeli/Bubblet-Asha (GPL v2), a Nokia SDK 2.0
# project of about 900 lines.  It is fetched and built, never committed - see
# the note in tools/fetch_test_games.sh about other people's copyrighted work.
#
# WHY BUILD FROM SOURCE RATHER THAN FETCH A JAR.  A prebuilt JAR only proves the
# runtime agrees with whatever javac emitted years ago.  Compiling the game HERE,
# against tools/midp_stubs.py, proves something stronger: that the API surface
# this runtime implements is the one javac resolves against.  A missing or
# differently-described method is a compile error rather than a
# NoSuchMethodError found later by a player.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/games/bubblet.jar}"
REPO="${NJ_BUBBLET_SRC:-https://github.com/Summeli/Bubblet-Asha}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== source"
timeout 180 git clone -q --depth 1 "$REPO" "$WORK/src" 2>/dev/null || {
    echo "cannot fetch $REPO" >&2; exit 1; }

# The .jad names the MIDlet and its icon; the manifest is built from it so the
# entry point comes from the project rather than from a guess here.
MIDLET="$(sed -n 's/^MIDlet-1:[[:space:]]*//p' "$WORK/src/Application Descriptor" | head -1)"
NAME="$(printf '%s' "$MIDLET" | cut -d, -f1 | tr -d ' ')"
CLS="$(printf '%s' "$MIDLET" | cut -d, -f3 | tr -d ' ')"
ICON="$(printf '%s' "$MIDLET" | cut -d, -f2 | tr -d ' ')"
[ -n "$CLS" ] || { echo "no MIDlet-1 in the descriptor" >&2; exit 1; }
echo "   $NAME ($CLS)"

echo "== compile"
CP="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
mkdir -p "$WORK/classes"
find "$WORK/src" -name '*.java' > "$WORK/files"
javac -source 8 -target 8 -nowarn -cp "$CP" -d "$WORK/classes" @"$WORK/files"

echo "== package"
mkdir -p "$WORK/classes/META-INF"
{
    printf 'Manifest-Version: 1.0\r\n'
    printf 'MIDlet-Name: %s\r\n' "$NAME"
    printf 'MIDlet-Version: 1.0.0\r\n'
    printf 'MIDlet-Vendor: Summeli (GPL v2)\r\n'
    printf 'MIDlet-1: %s, %s, %s\r\n' "$NAME" "$ICON" "$CLS"
    printf '\r\n'
} > "$WORK/classes/META-INF/MANIFEST.MF"

# The resources the MIDlet asks for by name.
if [ -d "$WORK/src/res" ]; then
    cp -r "$WORK/src/res" "$WORK/classes/res"
fi

mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF . )
echo "wrote $OUT"
