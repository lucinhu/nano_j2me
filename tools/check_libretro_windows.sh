#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# tools/check_libretro_windows.sh -- validate the Windows libretro core on a
# Linux build host.
#
# Two things are checked, in this order:
#
#   1. The PE DLL exports every entry point a libretro frontend needs.  A core
#      that is missing retro_run or retro_get_system_info still loads, but it
#      is useless once installed in RetroArch, so the export table is tested
#      directly with objdump.
#   2. The core actually runs in a separate Win32 process: tests/
#      test_libretro_wine.c is compiled with mingw-w64 into a tiny mock
#      frontend (LoadLibraryA + GetProcAddress, environment callback, video/
#      audio/input callbacks) and executed under wine.  It loads the JAR, runs
#      retro_run frames, and requires video frames plus (depending on the
#      fixture) non-black pixels and nonzero audio samples.
#
# The linux counterpart of this script is tools/check_libretro_frontend.sh.
#
# Usage:
#   tools/check_libretro_windows.sh [options] [GAME.jar ...]
#
# Options:
#   --core PATH     core DLL to test (default: the built windows-x86_64 core)
#   --frames N      retro_run frames per fixture, >= 60 (default 120)
#   --font MODE     auto (default), system, or builtin
#   --expect-font MODE  require loaded, builtin, or fallback font log
#   --no-build      do not build the core if it is missing, just fail
#   -h, --help      this help
#
# With no GAME.jar arguments, tests/fixtures/loopdemo.jar (must present a
# non-black frame and pass savestate roundtrip/corruption/continued-frame
# checks) and tests/fixtures/audiodemo.jar (must present nonzero audio) are used.
#
# Environment:
#   ARCH       x86_64 (default) or i686; selects the target and mingw prefix
#   CORE       core DLL path; same as --core
#   FRAMES     frames per fixture; same as --frames
#   SAVE_DIR   directory reported to the core as save/system dir
#              (default: build/libretro-wine-save)
#   BUILD=0    same as --no-build
#
# Requires: wine, winepath, x86_64-w64-mingw32-{gcc,objdump,strip} (or the
# i686 variants when ARCH=i686).  Wine needs working 32-bit support for the
# windows-i686 target.
#
# Exit codes: 0 all checks passed, 1 a check failed, 2 usage/setup error.
# ---------------------------------------------------------------------------

set -uo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

ARCH="${ARCH:-x86_64}"
case "$ARCH" in
    x86_64|amd64)
        ARCH=x86_64
        TARGET=windows-x86_64
        PREFIX=x86_64-w64-mingw32
        HARNESS_EXE=build/test_libretro_wine.exe
        EXPECT_ARCH="x86-64"
        ;;
    i686|x86)
        ARCH=i686
        TARGET=windows-i686
        PREFIX=i686-w64-mingw32
        HARNESS_EXE=build/test_libretro_wine32.exe
        EXPECT_ARCH="Intel i386"
        ;;
    *)
        echo "error: ARCH must be x86_64 or i686 (got '$ARCH')" >&2
        exit 2
        ;;
esac

MINGW_CC="$PREFIX-gcc"
MINGW_OBJDUMP="$PREFIX-objdump"

OUT="${NJ_LIBRETRO_OUT:-$ROOT/build/libretro-dist}"
case "$OUT" in
    /*) : ;;
    *) OUT="$ROOT/$OUT" ;;
esac
CORE_GIVEN=0
[ -z "${CORE:-}" ] || CORE_GIVEN=1
CORE="${CORE:-$OUT/$TARGET/nano_j2me_libretro.dll}"
FRAMES="${FRAMES:-120}"
SAVE_DIR="${SAVE_DIR:-$ROOT/build/libretro-wine-save}"
BUILD="${BUILD:-1}"
FONT_MODE=auto
EXPECT_FONT=""

# Entry points the frontend resolves with GetProcAddress.  These are required
# by the libretro API for a loadable, playable core; tests/test_libretro_wine.c
# fails if any of them is missing.
EXPECTED_EXPORTS="retro_set_environment retro_set_video_refresh
retro_set_audio_sample retro_set_audio_sample_batch retro_set_input_poll
retro_set_input_state retro_init retro_deinit retro_api_version
retro_get_system_info retro_get_system_av_info retro_get_region
retro_load_game retro_run retro_unload_game retro_serialize retro_serialize_size
retro_unserialize retro_reset retro_set_controller_port_device retro_cheat_reset
retro_cheat_set retro_load_game_special retro_get_memory_data retro_get_memory_size"
# Unsupported features must still provide the API's entry points as stubs.

usage()
{
    sed -n '3,/^set -uo pipefail$/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//; s/^#$//'
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
JARS=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --core)
            [ "$#" -ge 2 ] || { echo "error: --core needs a path" >&2; exit 2; }
            CORE="$2"
            CORE_GIVEN=1
            shift 2
            ;;
        --frames)
            [ "$#" -ge 2 ] || { echo "error: --frames needs a number" >&2; exit 2; }
            case "$2" in
                ''|*[!0-9]*) echo "error: --frames must be a number: $2" >&2; exit 2 ;;
            esac
            FRAMES="$2"
            shift 2
            ;;
        --font|--expect-font)
            [ "$#" -ge 2 ] || { echo "error: $1 needs a value" >&2; exit 2; }
            if [ "$1" = --font ]; then FONT_MODE="$2"; else EXPECT_FONT="$2"; fi
            shift 2
            ;;
        --no-build)
            BUILD=0
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        -*)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            JARS+=("$1")
            shift
            ;;
    esac
done

case "$FONT_MODE" in
    auto|system|builtin) : ;;
    *) echo "error: --font must be auto, system, or builtin" >&2; exit 2 ;;
esac
case "$EXPECT_FONT" in
    ''|loaded|builtin|fallback) : ;;
    *) echo "error: --expect-font must be loaded, builtin, or fallback" >&2; exit 2 ;;
esac

if ! [ "$FRAMES" -ge 60 ] 2>/dev/null; then
    echo "error: --frames must be at least 60 (got '$FRAMES')" >&2
    exit 2
fi

if [ "${#JARS[@]}" -eq 0 ]; then
    JARS=("$ROOT/tests/fixtures/loopdemo.jar" "$ROOT/tests/fixtures/audiodemo.jar")
fi

# ---------------------------------------------------------------------------
# Tool checks
# ---------------------------------------------------------------------------
need_tool()
{
    command -v "$1" >/dev/null 2>&1 || {
        echo "error: required tool not found: $1" >&2
        exit 2
    }
}

need_tool "$MINGW_CC"
need_tool "$MINGW_OBJDUMP"
need_tool wine
need_tool winepath

TIMEOUT_CMD=()
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD=(timeout 180)
fi

to_wine_path()
{
    # winepath prints the drive-letter form (for a Linux path usually
    # Z:\...).  Wine accepts that form directly, which has been tested with
    # Wine 11 on this host; if a future Wine version refuses it, strip the
    # leading "Z:" here.
    local out
    out=$(winepath -w "$1" 2>/dev/null) || return 1
    out=${out%$'\r'}
    [ -n "$out" ] || return 1
    printf '%s' "$out"
}

PASSED=0
FAILED=0

pass_line()
{
    printf 'PASS: %s\n' "$1"
    PASSED=$((PASSED + 1))
}

fail_line()
{
    printf 'FAIL: %s\n' "$1"
    FAILED=$((FAILED + 1))
}

# ---------------------------------------------------------------------------
# 0. Locate or build the core
# ---------------------------------------------------------------------------
if [ ! -f "$CORE" ]; then
    if [ "$CORE_GIVEN" -eq 1 ]; then
        echo "error: --core not found: $CORE" >&2
        exit 2
    fi
    if [ "$BUILD" -eq 0 ]; then
        echo "error: core not found: $CORE" >&2
        echo "       build it first: tools/build_libretro_cores.sh $TARGET" >&2
        exit 2
    fi
    echo "==> core missing, building $TARGET"
    if ! "$ROOT/tools/build_libretro_cores.sh" "$TARGET"; then
        echo "error: building $TARGET failed" >&2
        fail_line "build $TARGET"
        echo "== result: $PASSED passed, $FAILED failed =="
        exit 1
    fi
fi
[ -f "$CORE" ] || {
    echo "error: core not found: $CORE" >&2
    exit 1
}
echo "==> core: $CORE"

# A dist core that predates the sources passes this check and then surprises
# the user after the next real build, so point at the stale artifact.
if command -v find >/dev/null 2>&1; then
    NEWER="$(find "$ROOT/src" "$ROOT/include" "$ROOT/CMakeLists.txt" \
                  -newer "$CORE" -print -quit 2>/dev/null || true)"
    if [ -n "$NEWER" ]; then
        echo "==> note: core is older than $NEWER"
        echo "           rebuild: tools/build_libretro_cores.sh $TARGET"
    fi
fi

if [ "$(uname -s 2>/dev/null || true)" = "Linux" ]; then
    echo "==> wine: $(wine --version 2>/dev/null | head -n 1 || true)"
fi

# ---------------------------------------------------------------------------
# 1. PE format + export table
# ---------------------------------------------------------------------------
TMP_EXPORTS="$(mktemp "${TMPDIR:-/tmp}/nanj2me-exports.XXXXXX")" || exit 1
trap 'rm -f "$TMP_EXPORTS"' EXIT

if command -v file >/dev/null 2>&1; then
    DESC="$(file -b "$CORE" 2>/dev/null || true)"
    case "$DESC" in
        *"PE32"*"MS Windows"*)
            case "$DESC" in
                *"$EXPECT_ARCH"*) pass_line "PE format ($EXPECT_ARCH DLL)" ;;
                *) fail_line "PE format (expected $EXPECT_ARCH, file says: $DESC)" ;;
            esac
            ;;
        *)
            fail_line "PE format (not a Windows PE DLL: $DESC)"
            ;;
    esac
else
    echo "-- file(1) not installed, skipping PE format check"
fi

if ! "$MINGW_OBJDUMP" -p "$CORE" > "$TMP_EXPORTS" 2>/dev/null; then
    fail_line "objdump could not read $CORE"
else
    MISSING=""
    for sym in $EXPECTED_EXPORTS; do
        if ! grep -Eq "[[:space:]]${sym}\$" "$TMP_EXPORTS"; then
            MISSING="$MISSING $sym"
        fi
    done
    if [ -z "$MISSING" ]; then
        pass_line "exports (all required libretro entry points present)"
    else
        fail_line "exports (missing:$MISSING)"
    fi

fi

# ---------------------------------------------------------------------------
# 2. Compile the Win32 mock frontend
# ---------------------------------------------------------------------------
mkdir -p "$ROOT/build" || { echo "error: cannot create $ROOT/build" >&2; exit 1; }
COMPILE_LOG="$ROOT/build/test_libretro_wine-$ARCH.compile.log"
echo "==> compiling $HARNESS_EXE"
if ! "$MINGW_CC" -std=c99 -O2 -Wall -Wextra \
        -I "$ROOT/src/port/libretro" \
        -o "$ROOT/$HARNESS_EXE" "$ROOT/tests/test_libretro_wine.c" \
        2> "$COMPILE_LOG"; then
    echo "error: harness compilation failed:" >&2
    sed 's/^/    /' "$COMPILE_LOG" >&2
    fail_line "compile $HARNESS_EXE"
    echo "== result: $PASSED passed, $FAILED failed =="
    exit 1
fi
if [ -s "$COMPILE_LOG" ]; then
    echo "-- compiler warnings:"
    sed 's/^/    /' "$COMPILE_LOG"
fi

# ---------------------------------------------------------------------------
# 3. Run the fixtures under wine
# ---------------------------------------------------------------------------
mkdir -p "$SAVE_DIR" || { echo "error: cannot create $SAVE_DIR" >&2; exit 1; }
SAVE_W=$(to_wine_path "$SAVE_DIR") || {
    echo "error: winepath could not convert $SAVE_DIR" >&2
    exit 2
}
CORE_W=$(to_wine_path "$CORE") || {
    echo "error: winepath could not convert $CORE" >&2
    exit 2
}

run_case()
{
    local jar="$1"
    shift
    local label jar_w log errlog status extra=()

    label="$(basename -- "$jar")"
    if [ ! -f "$jar" ]; then
        fail_line "$label (fixture not found: $jar)"
        return
    fi
    extra+=(--font "$FONT_MODE")
    [ -z "$EXPECT_FONT" ] || extra+=(--expect-font "$EXPECT_FONT")
    case "$label" in
        *loopdemo*)   extra+=(--visual-required --state-required) ;;
        *audiodemo*)  extra+=(--audio-required) ;;
    esac

    jar_w=$(to_wine_path "$jar") || {
        fail_line "$label (winepath could not convert $jar)"
        return
    }
    log="$ROOT/build/libretro-wine-$label-$ARCH.log"
    errlog="$log.stderr"

    echo "==> wine $ARCH: $label ($FRAMES frames)"
    WINEDEBUG=-all "${TIMEOUT_CMD[@]}" wine "$ROOT/$HARNESS_EXE" \
        --core "$CORE_W" --jar "$jar_w" \
        --frames "$FRAMES" --save-dir "$SAVE_W" \
        ${extra[@]+"${extra[@]}"} "$@" > "$log" 2> "$errlog"
    status=$?

    cat "$log"
    if [ "$status" -eq 0 ] && grep -q '^\[result\] PASS' "$log"; then
        pass_line "$label under wine ($ARCH)"
    else
        fail_line "$label under wine ($ARCH) (exit $status)"
        grep '^\[check\] FAIL' "$log" 2>/dev/null || true
        if [ -s "$errlog" ]; then
            echo "-- stderr tail ($errlog):"
            tail -n 10 "$errlog" | sed 's/^/    /'
        fi
    fi
}

for jar in "${JARS[@]}"; do
    run_case "$jar"
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo "== result: $PASSED passed, $FAILED failed =="
if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
exit 0
