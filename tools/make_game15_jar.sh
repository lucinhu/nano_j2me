#!/usr/bin/env bash
#
# Game15 - GoldmanYT/Game15MIDlet, a 4x4 sliding puzzle ("15-puzzle").
#
#     tools/make_game15_jar.sh [out.jar]
#
# Its layout is computed in sizeChanged(), so it drew a single black dot until
# the runtime started delivering that callback - see the commit that added it.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$ROOT/tools/build_j2me_source.sh" \
    "${NJ_GAME15_SRC:-https://github.com/GoldmanYT/Game15MIDlet}" \
    "${1:-$ROOT/tests/games/game15.jar}"
