#!/usr/bin/env bash
#
# Build the video fixture MIDlet: decodes the MPEG-4 Part 2 track in
# res/movie.3gp and shows it through VideoControl's direct-video overlay.
#
#   tools/make_movie_jar.sh                        # tests/fixtures/moviedemo.jar
#   tools/make_movie_jar.sh out.jar movie.3gp movie_h263.3gp
#
# The 3gp assets under tests/fixtures/3gp/ are synthetic (ffmpeg testsrc, 6
# frames each, one MPEG-4 Part 2 at 64x48 and one H.263 at 128x96, no audio)
# and redistributable; the fixture exists so the
# JPEG/GIF-style fuzzing can also cover the video container path and so the
# libretro/CLI checks have a video Player that works without a corpus title.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/moviedemo.jar}"
MOVIE="${2:-$ROOT/tests/fixtures/3gp/movie.3gp}"
MOVIE_H263="${3:-$ROOT/tests/fixtures/3gp/movie_h263.3gp}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
if [ ! -f "$MOVIE" ] || [ ! -f "$MOVIE_H263" ]; then
    echo "missing video fixture asset" >&2
    exit 1
fi
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF" "$WORK/classes/res"
STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/moviedemo/MovieDemo.java"
cp "$MOVIE" "$WORK/classes/res/movie.3gp"
cp "$MOVIE_H263" "$WORK/classes/res/movie_h263.3gp"
printf 'Manifest-Version: 1.0\r\nMIDlet-Name: MovieDemo\r\nMIDlet-1: MovieDemo, , MovieDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class \
      res/movie.3gp res/movie_h263.3gp )
echo "wrote $OUT"
