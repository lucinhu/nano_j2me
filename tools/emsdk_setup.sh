#!/usr/bin/env bash
#
# Install the Emscripten SDK this repository's web build uses.
#
#   tools/emsdk_setup.sh            install "latest" into .local-tools/emsdk
#   tools/emsdk_setup.sh 6.0.0      install one specific version
#   tools/emsdk_setup.sh --check    report whether a usable emcc is present
#
# The SDK is kept under .local-tools/, which is excluded from git through
# .git/info/exclude, exactly like the MinGW/SDL2 toolchain that
# tools/build_windows.sh downloads. Nothing here is committed.
#
# A full install is roughly 1.5 GiB (emscripten + node + clang wasm backend).
# Once it is installed, tools/build_web.sh sources .local-tools/emsdk/
# emsdk_env.sh automatically, so no shell setup is needed per session.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_dir="$here/.local-tools/emsdk"
repo_url="https://github.com/emscripten-core/emsdk.git"
version=latest
check_only=0

usage() {
    sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

for arg in "$@"; do
    case "$arg" in
    --check) check_only=1 ;;
    -h|--help) usage; exit 0 ;;
    -*) echo "emsdk_setup.sh: unknown argument: $arg" >&2; usage >&2; exit 2 ;;
    *) version="$arg" ;;
    esac
done

check_emcc() {
    if command -v emcc >/dev/null 2>&1; then
        echo "emcc: $(command -v emcc)"
        emcc --version | head -1
        return 0
    fi
    if [ -f "$sdk_dir/emsdk_env.sh" ]; then
        echo "emcc not on PATH, but an SDK exists at $sdk_dir"
        echo "source it with: . $sdk_dir/emsdk_env.sh"
        return 1
    fi
    echo "no Emscripten SDK installed (expected $sdk_dir)"
    return 1
}

if [ "$check_only" = 1 ]; then
    check_emcc
    exit $?
fi

# ---- git ---------------------------------------------------------------------

if ! command -v git >/dev/null 2>&1; then
    echo "emsdk_setup.sh: git is required" >&2
    exit 1
fi

if [ ! -d "$sdk_dir/.git" ]; then
    echo "== clone: $repo_url -> $sdk_dir"
    mkdir -p "$(dirname "$sdk_dir")"
    git clone "$repo_url" "$sdk_dir"
else
    echo "== update: $sdk_dir"
    git -C "$sdk_dir" pull --ff-only || true
fi

# ---- install + activate ------------------------------------------------------

cd "$sdk_dir"
echo "== install: emsdk $version"
./emsdk install "$version"
echo "== activate: emsdk $version"
./emsdk activate "$version"

# shellcheck disable=SC1091
set +u
. "$sdk_dir/emsdk_env.sh" >/dev/null 2>&1 || true
set -u

echo
if command -v emcc >/dev/null 2>&1; then
    echo "Emscripten ready: $(emcc --version | head -1)"
else
    echo "Emscripten installed; open a new shell or run:"
    echo "  . $sdk_dir/emsdk_env.sh"
fi
echo
echo "Next:"
echo "  tools/build_web.sh"
echo "  python3 -m http.server -d build/web 8000"
