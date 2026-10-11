#!/usr/bin/env bash
#
# Slime Volleyball - isaacflaum/SlimeVolleyball, a Java MIDlet volleyball game.
#
#     tools/make_slimevolley_jar.sh [out.jar]
#
# It needs a key to get past its start screen, which is a Command on the right
# soft key rather than a Canvas key press - see the corpus runner's key table.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$ROOT/tools/build_j2me_source.sh" \
    "${NJ_SLIME_SRC:-https://github.com/isaacflaum/SlimeVolleyball}" \
    "${1:-$ROOT/tests/games/slimevolley.jar}"
