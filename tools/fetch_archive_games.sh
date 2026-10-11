#!/usr/bin/env bash
#
# Fetch J2ME titles from archive.org into the corpus directory.
#
#     tools/fetch_archive_games.sh [identifier ...]
#
# With no arguments it fetches the list below.
#
# ---------------------------------------------------------------------------
# THESE ARE OTHER PEOPLE'S GAMES AND THEY ARE NEVER COMMITTED
# ---------------------------------------------------------------------------
# archive.org serves preserved copies of commercial J2ME titles.  They are
# downloaded into tests/games/, which .gitignore excludes, for the same reason
# tools/fetch_test_games.sh does it with the Apress JARs: compatibility testing
# needs something real to run, and redistributing someone's game is not this
# project's business.  Every fetch prints the destination so it is obvious where
# they went.
#
# `git status --short` must never show a .jar.  The goal states that as a hard
# rule and this script does not change what .gitignore already covers.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${NJ_GAMES:-$ROOT/tests/games}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# A spread of publishers and sizes rather than one genre: the point is to find
# where the runtime breaks, and a corpus that is all Gameloft finds Gameloft's
# gaps only.
DEFAULT_IDS=(
    bounce_virtual_me                        # Nokia, small, J2ME-era
    j2me-game-bubble-bash-3                  # Gameloft, zip
    j2me-game-chess-king-quest               # small
    j2me-game-cocoasoft-crash-rally-2d       # small
    j2me-game-i-play-24-agent-down           # i-play
)

IDS=("$@")
[ "${#IDS[@]}" -gt 0 ] || IDS=("${DEFAULT_IDS[@]}")
mkdir -p "$DEST"

got=0
for id in "${IDS[@]}"; do
    echo "== $id"
    meta="$(timeout 120 curl -sS "https://archive.org/metadata/$id" || true)"
    if [ -z "$meta" ]; then
        echo "   metadata fetch failed"
        continue
    fi
    # Every .jar the item holds, plus any .zip that will be unpacked for one.
    printf '%s' "$meta" | python3 -c '
import json, sys
d = json.load(sys.stdin)
for f in d.get("files", []):
    n = f["name"]
    if n.lower().endswith((".jar", ".zip")):
        print(n)
' > "$WORK/list"
    if [ ! -s "$WORK/list" ]; then
        echo "   no .jar or .zip in this item"
        continue
    fi
    while IFS= read -r name; do
        url="https://archive.org/download/$id/$name"
        out="$WORK/$(basename "$name")"
        timeout 300 curl -sSL -o "$out" "$url" || { echo "   ! $name"; continue; }
        case "$name" in
            *.jar)
                cp "$out" "$DEST/"
                echo "   + $(basename "$name") -> $DEST/"
                got=$((got + 1))
                ;;
            *.zip)
                mkdir -p "$WORK/z"
                rm -rf "$WORK/z"/*
                if unzip -qq -o "$out" -d "$WORK/z" 2>/dev/null; then
                    # A zip may hold several titles, or a title plus its .jad.
                    find "$WORK/z" -name '*.jar' | while IFS= read -r j; do
                        cp "$j" "$DEST/"
                        echo "   + $(basename "$j") (from $name) -> $DEST/"
                    done
                    got=$((got + $(find "$WORK/z" -name '*.jar' | wc -l)))
                else
                    echo "   ! $name could not be unpacked"
                fi
                ;;
        esac
    done < "$WORK/list"
done

echo
echo "$got JAR(s) in $DEST"
echo "REMINDER: $DEST is gitignored.  Run 'git status --short' and confirm no .jar appears."
