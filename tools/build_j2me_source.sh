#!/usr/bin/env bash
#
# Build a J2ME MIDlet from source with this project's MIDP stubs as the compile
# classpath.
#
#     tools/build_j2me_source.sh <git-url|dir> <out.jar> [source-subdir]
#
# WHY BUILD FROM SOURCE RATHER THAN FETCH A JAR.  A prebuilt JAR only proves the
# runtime agrees with whatever javac emitted years ago.  Compiling a game HERE,
# against tools/midp_stubs.py, proves something stronger: that the API surface
# this runtime implements is the one javac resolves against.  A missing or
# differently-described method is a compile error rather than a
# NoSuchMethodError found later by a player.  That difference is not theoretical
# - it found six bugs the first time it ran.
#
# The MIDlet's name, icon and class come from the project's own Application
# Descriptor, so the entry point is read rather than guessed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_ARG="${1:?usage: build_j2me_source.sh <git-url|dir> <out.jar> [subdir]}"
OUT="${2:?usage: build_j2me_source.sh <git-url|dir> <out.jar> [subdir]}"
SUBDIR="${3:-}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if [ -d "$SRC_ARG" ]; then
    SRC="$SRC_ARG/$SUBDIR"
else
    echo "== fetching $SRC_ARG"
    timeout 300 git clone -q --depth 1 "$SRC_ARG" "$WORK/clone" 2>/dev/null || {
        echo "cannot fetch $SRC_ARG" >&2; exit 1; }
    SRC="$WORK/clone/$SUBDIR"
fi

DESC="$SRC/Application Descriptor"
[ -f "$DESC" ] || DESC="$SRC/application descriptor"
[ -f "$DESC" ] || DESC="$(find "$SRC" -maxdepth 2 -name '*.jad' 2>/dev/null | head -1)"

if [ -n "$DESC" ] && [ -f "$DESC" ]; then
    # The project states its own entry point; read it rather than guess.
    MIDLET="$(sed -n 's/^MIDlet-1:[[:space:]]*//p' "$DESC" | head -1)"
    NAME="$(printf '%s' "$MIDLET" | cut -d, -f1 | tr -d ' ')"
    CLS="$(printf '%s' "$MIDLET" | cut -d, -f3 | tr -d ' ')"
    ICON="$(printf '%s' "$MIDLET" | cut -d, -f2 | tr -d ' ')"
else
    # No descriptor.  Most small homebrew games have none, and refusing them
    # would exclude exactly the projects that are easiest to check - so the
    # entry point is DERIVED from the source instead: the one class that
    # extends MIDlet, with its package read from its own declaration.
    HOST="$(grep -rl 'extends MIDlet' "$SRC" --include=*.java 2>/dev/null | head -1)"
    [ -n "$HOST" ] || { echo "no MIDlet class under $SRC" >&2; exit 1; }
    PKG="$(sed -n 's/^[[:space:]]*package[[:space:]]\+\([A-Za-z0-9_.]*\)[[:space:]]*;.*/\1/p' "$HOST" | head -1)"
    BASE="$(basename "$HOST" .java)"
    CLS="${PKG:+$PKG.}$BASE"
    NAME="$BASE"
    ICON=""
    echo "   no descriptor; derived $CLS from $HOST"
fi
[ -n "$CLS" ] || { echo "cannot determine the MIDlet class" >&2; exit 1; }
echo "   $NAME ($CLS)"

echo "== compiling"
CP="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
mkdir -p "$WORK/classes"
find "$SRC" -name '*.java' > "$WORK/files"
wc -l < "$WORK/files" | xargs printf "   %s source files\n"
# Encoding: a J2ME project from 2004 predates the idea that source files are
# UTF-8, and one here has a Latin-1 (c) in a copyright header that javac refuses
# outright.  UTF-8 is tried first because it is what a modern project means, and
# a build that fails ONLY on unmappable characters is retried as Latin-1 rather
# than making the caller know which decade their source is from.
if ! javac -source 8 -target 8 -nowarn -encoding UTF-8 -cp "$CP" \
           -d "$WORK/classes" @"$WORK/files" 2>"$WORK/javac.log"; then
    if grep -q "unmappable character\|不可映射字符" "$WORK/javac.log"; then
        echo "   source is not UTF-8; retrying as ISO-8859-1"
        javac -source 8 -target 8 -nowarn -encoding ISO-8859-1 -cp "$CP" \
              -d "$WORK/classes" @"$WORK/files"
    else
        cat "$WORK/javac.log" >&2
        exit 1
    fi
fi

echo "== packaging"
mkdir -p "$WORK/classes/META-INF"
{
    printf 'Manifest-Version: 1.0\r\n'
    printf 'MIDlet-Name: %s\r\n' "$NAME"
    printf 'MIDlet-Version: 1.0.0\r\n'
    printf 'MIDlet-Vendor: see the source project\r\n'
    printf 'MIDlet-1: %s, %s, %s\r\n' "$NAME" "$ICON" "$CLS"
    printf '\r\n'
} > "$WORK/classes/META-INF/MANIFEST.MF"

# Resources the MIDlet asks for BY NAME, so the path inside the JAR has to match
# what the game's createImage("/...") says.
#
# There is no single convention: a project may keep them in res/, in resources/,
# or - NetBeans-style, which is what FlappyPlaneJM does - in src/Resources/ with
# a capital R.  So every directory whose NAME is one of those is copied to the
# JAR root under that same name, wherever it sits in the tree.  Looking only for
# a lowercase res/ next to the sources found none of FlappyPlane's, and the game
# died on its first createImage with "Error initializing the canvas".
copied=""
while IFS= read -r d; do
    [ -n "$d" ] || continue
    base="$(basename "$d")"
    [ -e "$WORK/classes/$base" ] && continue
    cp -r "$d" "$WORK/classes/$base"
    copied="$copied $base"
done < <(find "$SRC" -maxdepth 3 -type d \( -iname res -o -iname resources \) 2>/dev/null)
[ -n "$copied" ] && echo "   resources:$copied"

mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF . )
echo "wrote $OUT"
