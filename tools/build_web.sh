#!/usr/bin/env bash
#
# Build the Emscripten/WASM browser port and assemble a servable site.
#
#   tools/build_web.sh            release build, assemble build/web/
#   tools/build_web.sh --debug    debug build with Emscripten assertions
#   tools/build_web.sh --clean    remove the build directory first
#
# The result is a directory you can serve from anywhere:
#
#   python3 -m http.server -d build/web 8000
#   # then open http://localhost:8000/
#
# The SDK is looked up in this order: emcc already on PATH, $EMSDK, then
# .local-tools/emsdk (the layout tools/build_android*.sh uses as well).
# tools/build_web.sh --env-check prints what it found and exits; it is what CI
# and the docs use to fail early with a useful message.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$here/build-web"
out_dir="$build_dir/web"
site_dir="$here/build/web"
build_type=Release
do_clean=0
env_check=0

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

for arg in "$@"; do
    case "$arg" in
    --debug)     build_type=Debug ;;
    --clean)     do_clean=1 ;;
    --env-check) env_check=1 ;;
    -h|--help)   usage; exit 0 ;;
    *) echo "build_web.sh: unknown argument: $arg" >&2; usage >&2; exit 2 ;;
    esac
done

# ---- locate the Emscripten SDK ------------------------------------------------

find_emsdk() {
    if command -v emcc >/dev/null 2>&1; then
        return 0
    fi
    local candidate=""
    if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
        candidate="$EMSDK"
    elif [ -f "$here/.local-tools/emsdk/emsdk_env.sh" ]; then
        candidate="$here/.local-tools/emsdk"
    fi
    if [ -n "$candidate" ]; then
        # shellcheck disable=SC1091
        ( set +u; . "$candidate/emsdk_env.sh" >/dev/null 2>&1 )
        # emsdk_env.sh exports EMSDK and prepends emcc to PATH; both live in
        # the caller shell, not this subshell, so redo it without a subshell.
        set +u
        . "$candidate/emsdk_env.sh" >/dev/null 2>&1
        set -u
    fi
    command -v emcc >/dev/null 2>&1
}

if [ "$env_check" = 1 ]; then
    if find_emsdk; then
        echo "emcc: $(command -v emcc)"
        echo "emcc version: $(emcc --version | head -1)"
        echo "build: $build_dir"
        exit 0
    fi
    echo "build_web.sh: no Emscripten SDK found." >&2
    echo "  Install it with tools/emsdk_setup.sh (or clone emsdk into" >&2
    echo "  .local-tools/emsdk and run ./emsdk install latest && ./emsdk activate latest)." >&2
    exit 1
fi

if ! find_emsdk; then
    echo "build_web.sh: emcc not found." >&2
    echo "  Run tools/emsdk_setup.sh first (downloads the SDK into .local-tools/emsdk)," >&2
    echo "  or source an existing emsdk: . /path/to/emsdk/emsdk_env.sh" >&2
    exit 1
fi

if [ "$do_clean" = 1 ]; then
    rm -rf "$build_dir" "$site_dir"
fi

# ---- configure + build --------------------------------------------------------

echo "== emcc: $(emcc --version | head -1)"
echo "== configure: $build_dir ($build_type)"
emcmake cmake -S "$here" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DNJ_BUILD_WEB=ON
echo "== build"
cmake --build "$build_dir" -j "$(nproc)"

if [ ! -f "$build_dir/web/nj2me.js" ] || [ ! -f "$build_dir/web/nj2me.wasm" ]; then
    echo "build_web.sh: the build did not produce nj2me.js/nj2me.wasm" >&2
    exit 1
fi

# ---- assemble the servable site ----------------------------------------------
#
# The shell assets come from web/, the compiled module is dropped next to
# web/js/module.js so `import './nj2me.js'` resolves with a plain static server
# (no bundler, no import maps, no build step for the JS side).

echo "== assemble: $site_dir"
rm -rf "$site_dir"
mkdir -p "$site_dir"
cp -R "$here/web/." "$site_dir/"
cp "$build_dir/web/nj2me.js" "$build_dir/web/nj2me.wasm" "$site_dir/js/"

# The IDBFS image belongs to the browser profile; never ship one from a build
# tree that happened to contain it.
rm -rf "$site_dir"/nano_j2me || true

echo
echo "built: $site_dir"
echo "  nj2me.js   $(wc -c < "$site_dir/js/nj2me.js") bytes"
echo "  nj2me.wasm $(wc -c < "$site_dir/js/nj2me.wasm") bytes"
echo
echo "serve it with:"
echo "  python3 -m http.server -d $site_dir 8000"
echo "then open http://localhost:8000/"
