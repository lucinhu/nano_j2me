#!/bin/bash
# The Windows release build.
#
# Cross-compiles the desktop client and the CLI with mingw-w64 and the official
# SDL2 mingw development package, stages a directory with the runtime pieces a
# user actually needs, and zips it:
#
#   build/nj2me-<version>-windows-x86_64.zip
#     nj2me-<version>-windows-x86_64/
#       nj2me.exe            the console-subsystem binary (log stays visible)
#       SDL2.dll             the SDL2 runtime the .exe was linked against
#       README.txt           bilingual quick start and build-from-source text
#       samples/*.jar        the five demo suites from tests/fixtures
#
# Everything is reproducible from a clean tree: SDL2 is downloaded into
# .local-tools/sdl2-mingw/ (git-ignored) when it is not there, and the build
# goes into build-windows/ so the native Linux build in build/ is untouched.
#
# Requirements: x86_64-w64-mingw32-gcc, cmake, curl, tar, python3 (for zip).
# Ninja is used when installed, Unix Makefiles otherwise.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sed -n 's/^project(nano_j2me VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -n 1)"
[ -n "$VERSION" ] || { echo "error: cannot read the version from CMakeLists.txt" >&2; exit 1; }

SDL_VERSION="${SDL2_VERSION:-2.32.10}"
SDL_TOOLS="$ROOT/.local-tools/sdl2-mingw"
SDL_TAR="$SDL_TOOLS/SDL2-devel-$SDL_VERSION-mingw.tar.gz"
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz"
BD="$ROOT/build-windows"
NAME="nj2me-$VERSION-windows-x86_64"
DIST="$ROOT/build"
ZIP="$DIST/$NAME.zip"

for tool in x86_64-w64-mingw32-gcc cmake curl tar python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: $tool is not installed" >&2
        exit 1
    }
done

# The SDL2 mingw package is a development tree, not an installed prefix: it
# carries headers, .a import libraries and the runtime DLL in one tar.
if [ ! -d "$SDL_TOOLS/SDL2-$SDL_VERSION/x86_64-w64-mingw32" ]; then
    if [ ! -f "$SDL_TAR" ]; then
        echo "== downloading SDL2 $SDL_VERSION (mingw development package)"
        curl -fsSL --retry 3 -o "$SDL_TAR.part" "$SDL_URL"
        mv "$SDL_TAR.part" "$SDL_TAR"
    fi
    echo "== unpacking SDL2 into .local-tools/sdl2-mingw"
    mkdir -p "$SDL_TOOLS"
    tar -xzf "$SDL_TAR" -C "$SDL_TOOLS"
fi
SDL="$SDL_TOOLS/SDL2-$SDL_VERSION/x86_64-w64-mingw32"
[ -f "$SDL/include/SDL2/SDL.h" ] && [ -f "$SDL/lib/libSDL2.dll.a" ] && [ -f "$SDL/bin/SDL2.dll" ] || {
    echo "error: $SDL does not look like an SDL2 mingw package" >&2
    exit 1
}

# A stale cache from another toolchain (or a native configure) would make the
# cross build silently pick host headers; start clean when the cache exists but
# does not name the cross compiler.
if [ -f "$BD/CMakeCache.txt" ] && ! grep -q '^CMAKE_C_COMPILER:FILEPATH=.*x86_64-w64-mingw32-gcc' "$BD/CMakeCache.txt"; then
    echo "== removing stale build-windows cache"
    rm -rf "$BD"
fi
GENERATOR=""
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="-G Ninja"
fi
# The include path needs both entries: the fallback cache variables replace
# pkg-config's flags, and SDL.h lives in include/SDL2 while SDL_config.h etc.
# are reached from include/.
# shellcheck disable=SC2086
cmake -S "$ROOT" -B "$BD" $GENERATOR \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/toolchain-mingw-w64.cmake" \
    -DSDL2_INCLUDE_DIRS="$SDL/include;$SDL/include/SDL2" \
    -DSDL2_LIBRARIES="$SDL/lib/libSDL2.dll.a" \
    -DNJ_BUILD_TESTS=OFF \
    -DNJ_BUILD_LIBRETRO=OFF

cmake --build "$BD" --target nj2me-cli -j "$(nproc 2>/dev/null || echo 4)"
[ -f "$BD/nj2me.exe" ] || { echo "error: build produced no nj2me.exe" >&2; exit 1; }

echo "== staging $NAME"
rm -rf "$DIST/$NAME"
mkdir -p "$DIST/$NAME/samples"
cp "$BD/nj2me.exe" "$DIST/$NAME/nj2me.exe"
cp "$SDL/bin/SDL2.dll" "$DIST/$NAME/SDL2.dll"
cp "$ROOT/tools/windows/README.txt" "$DIST/$NAME/README.txt"
for demo in keysdemo colordemo textdemo audiodemo mididemo; do
    [ -f "$ROOT/tests/fixtures/$demo.jar" ] || { echo "error: missing tests/fixtures/$demo.jar" >&2; exit 1; }
    cp "$ROOT/tests/fixtures/$demo.jar" "$DIST/$NAME/samples/$demo.jar"
done
# Release symbols are of no use to a user; keep the binary small.
if command -v x86_64-w64-mingw32-strip >/dev/null 2>&1; then
    x86_64-w64-mingw32-strip --strip-unneeded "$DIST/$NAME/nj2me.exe"
fi

# zip(1) is not installed everywhere; the Android packaging uses python3 too.
(cd "$DIST" && rm -f "$NAME.zip" && python3 - "$NAME" <<'PY'
import os, sys, zipfile
name = sys.argv[1]
with zipfile.ZipFile(name + ".zip", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for base, dirs, files in os.walk(name):
        dirs.sort()
        for f in sorted(files):
            path = os.path.join(base, f)
            z.write(path, path)
PY
)

echo
echo "== $ZIP"
ls -l "$ZIP"
sha256sum "$ZIP"
if command -v wine >/dev/null 2>&1; then
    WINEDEBUG=-all wine "$DIST/$NAME/nj2me.exe" --version 2>/dev/null || true
fi
