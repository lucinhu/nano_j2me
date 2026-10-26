#!/usr/bin/env bash
#
# Build and run tools/m4v_player_probe.c: decode a movie through the runtime's
# Java-level media Player and dump its RGB565 frames as PPM + FNV hashes.
#
#     tools/m4v_player_probe.sh <build-dir> <clip.3gp> <out-prefix> [frames] [heap-mb]
#
# <build-dir> is a configured nano_j2me build (for example /tmp/nj-m4v-build).
# The probe touches the VM structs, so it is compiled with the same
# NJ_ENABLE_* switches as the library, read from the build's own flags file.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:?usage: m4v_player_probe.sh <build-dir> <clip.3gp> <out-prefix> [frames] [heap-mb]}"
CLIP="${2:?usage: m4v_player_probe.sh <build-dir> <clip.3gp> <out-prefix> [frames] [heap-mb]}"
PREFIX="${3:?usage: m4v_player_probe.sh <build-dir> <clip.3gp> <out-prefix> [frames] [heap-mb]}"
FRAMES="${4:-8}"
HEAP_MB="${5:-16}"

FLAGS="$BUILD/CMakeFiles/test_lib.dir/flags.make"
if [ ! -f "$FLAGS" ]; then
    echo "error: $FLAGS not found; configure the build first" >&2
    exit 1
fi
DEFS="$(sed -n 's/^C_DEFINES = //p' "$FLAGS")"
INCS="$(sed -n 's/^C_INCLUDES = //p' "$FLAGS")"
BIN="${TMPDIR:-/tmp}/m4v_player_probe"

# The library's link line carries zlib and libcurl (the POSIX network port).
CURL_LIBS=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcurl; then
    CURL_LIBS="$(pkg-config --libs libcurl)"
fi
# shellcheck disable=SC2086
cc -O2 -g -std=c99 $DEFS $INCS \
   "$ROOT/tools/m4v_player_probe.c" "$BUILD/libnj2me.a" -lm -lz $CURL_LIBS -o "$BIN"
exec "$BIN" "$CLIP" "$PREFIX" "$FRAMES" "$HEAP_MB"
