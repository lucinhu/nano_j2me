#!/usr/bin/env bash
#
# Bubblet - Summeli/Bubblet-Asha, GPL v2, ~900 lines, a Nokia SDK 2.0 project.
# The first non-Apress title in the corpus, and the one that found six bugs in
# the MIDP stubs the first time a real game was compiled against them.
#
#     tools/make_bubblet_jar.sh [out.jar]
#
# Fetched and built, never committed - see tools/fetch_test_games.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$ROOT/tools/build_j2me_source.sh" \
    "${NJ_BUBBLET_SRC:-https://github.com/Summeli/Bubblet-Asha}" \
    "${1:-$ROOT/tests/games/bubblet.jar}"
