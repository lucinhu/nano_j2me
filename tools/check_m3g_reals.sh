#!/bin/bash
#
# Every .m3g file the collected games actually ship must be accepted by the
# container probe.
#
# This is the check tests/test_m3g_file.c cannot be.  That test builds its
# fixtures out of bytes written by hand, so it encodes the same understanding
# of the format as the code does: write the format down wrongly and the test
# agrees with the mistake.  That is exactly what happened once already - the
# first probe read a version byte at offset 0 and big endian section lengths,
# and both the code and its test were wrong together.  Real files are not
# written by hand and cannot agree with anything.
#
# Skips (successfully) when the corpus is not there, because tests/games/ is
# gitignored and a fresh clone has no games in it.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_M3G_PROBE:-$ROOT/build/nj_m3g_probe}"
GAMES="${NJ_GAMES:-$ROOT/tests/games}"

if [ ! -x "$BIN" ]; then echo "no probe at $BIN"; exit 2; fi
if [ ! -d "$GAMES" ]; then echo "SKIP: $GAMES is not there"; exit 0; fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

n=0
bad=0
for jar in "$GAMES"/*.jar; do
    d="$tmp/$(basename "$jar" .jar)"
    mkdir -p "$d"
    unzip -o -q "$jar" -d "$d" 2>/dev/null || continue
    while IFS= read -r f; do
        n=$((n + 1))
        if ! out="$("$BIN" "$f" 2>&1)"; then
            bad=$((bad + 1))
            [ "$bad" -le 10 ] && echo "REJECTED $(basename "$jar"): ${f#"$d"/} -> $out"
        fi
    done < <(find "$d" -type f -iname '*.m3g')
    rm -rf "$d"
done

echo "m3g files probed=$n rejected=$bad"
[ "$bad" -eq 0 ]
