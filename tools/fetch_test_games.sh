#!/usr/bin/env bash
#
# Fetch real third-party MIDlet JARs to validate the runtime against.
#
# These are NOT committed: they are somebody else's copyrighted work, and the
# runtime's own tests must stay self-contained.  This script exists so the
# compatibility check is reproducible rather than a claim.
#
# The source is the published example code for "J2ME Games with MIDP 2" by
# Carol Hamer (Apress, 2004), whose binaries are hosted on GitHub.  They are
# ideal test material: built by javac 1.4 targeting CLDC 1.0 / MIDP 2.0, so
# class file version 46, with the legacy jsr/ret finally blocks and the exact
# API surface a 2004 phone had.
#
# Usage: tools/fetch_test_games.sh [dest-dir]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-$ROOT/tests/games}"
BASE="https://raw.githubusercontent.com/apress/j2me-games-w-midp2/master/J2ME_Games_src"

mkdir -p "$DEST"

# ch01 hello - the smallest possible MIDlet
# ch02 maze  - Canvas, menus, persistence
# ch03 jump  - the full MIDP 2 game API: Sprite, TiledLayer, LayerManager,
#              GameCanvas, a background thread and key polling
# ch04-07 book - four more from the later chapters; they all build to the name
#              "book.jar" so they are stored as book4.jar .. book7.jar
# The later chapters each build to "book.jar", so they are stored by chapter -
# four more independent MIDlets for very little extra work, from the same 2004
# toolchain and the same book.
for g in ch01/bin/hello ch02/bin/maze ch03/bin/jump \
         ch04/bin/book ch05/bin/book ch06/bin/book ch07/bin/book; do
    name="$(basename "$g")"
    case "$g" in
        ch0[4-7]/*) name="book$(printf '%s' "$g" | sed 's|^ch0*\([0-9]*\)/.*|\1|')" ;;
    esac
    out="$DEST/$name.jar"
    if [ -s "$out" ]; then
        echo "have $out"
        continue
    fi
    if ! command -v curl >/dev/null 2>&1; then
        echo "curl not available; skipping $name" >&2
        continue
    fi
    echo "fetching $name.jar"
    curl -sSL --fail -o "$out" "$BASE/$g.jar"
done

echo
echo "now run:"
echo "  tools/nj_inspect $DEST/jump.jar --parse-only   # class loader"
echo "  build/nj2me $DEST/jump.jar                     # play it"
