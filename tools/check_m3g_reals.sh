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
poly_ok=0; poly_n=0; img_ok=0; img_n=0; va_ok=0; va_n=0; obj_bad=0
for jar in "$GAMES"/*.jar; do
    d="$tmp/$(basename "$jar" .jar)"
    mkdir -p "$d"
    unzip -o -q "$jar" -d "$d" 2>/dev/null || continue
    while IFS= read -r f; do
        n=$((n + 1))
        out="$("$BIN" "$f" 2>&1)"
        rc=$?
        # the probe reports how many decoded objects landed in the sets the
        # format defines; summed here it becomes a corpus-wide number
        line="$(printf '%s\n' "$out" | grep -m1 '^LAYOUT ' || true)"
        if [ -n "$line" ]; then
            obj_bad=$((obj_bad + $(printf '%s' "$line" | sed -n 's/.*refused=\([0-9]*\).*/\1/p')))
            poly_ok=$((poly_ok + $(printf '%s' "$line" | sed -n 's/.*poly=\([0-9]*\)\/\([0-9]*\).*/\1/p')))
            poly_n=$((poly_n   + $(printf '%s' "$line" | sed -n 's/.*poly=\([0-9]*\)\/\([0-9]*\).*/\2/p')))
            img_ok=$((img_ok   + $(printf '%s' "$line" | sed -n 's/.*img=\([0-9]*\)\/\([0-9]*\).*/\1/p')))
            img_n=$((img_n     + $(printf '%s' "$line" | sed -n 's/.*img=\([0-9]*\)\/\([0-9]*\).*/\2/p')))
            va_ok=$((va_ok     + $(printf '%s' "$line" | sed -n 's/.*va=\([0-9]*\)\/\([0-9]*\).*/\1/p')))
            va_n=$((va_n       + $(printf '%s' "$line" | sed -n 's/.*va=\([0-9]*\)\/\([0-9]*\).*/\2/p')))
        fi
        if [ "$rc" -ne 0 ]; then
            bad=$((bad + 1))
            [ "$bad" -le 10 ] && echo "REJECTED $(basename "$jar"): ${f#"$d"/} -> $out"
        fi
    done < <(find "$d" -type f -iname '*.m3g')
    rm -rf "$d"
done

echo "m3g files probed=$n rejected=$bad"
echo "objects the decoder refused: $obj_bad"
echo "decoded objects: PolygonMode $poly_ok/$poly_n in range, Image2D $img_ok with an image of $img_n, VertexArray $va_ok/$va_n"
[ "$bad" -eq 0 ]
# and the object layer is a gate now too: every one of the 36859 objects in the
# corpus decodes, so a single refusal is a regression
[ "$obj_bad" -eq 0 ]
