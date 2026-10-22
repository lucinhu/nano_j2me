#!/usr/bin/env bash
#
# Build the nano_j2me libretro core for one or more platform/architecture
# targets.  One core binary is not portable: a libretro core is a shared
# library, and the frontend can only dlopen a core built for the same OS/CPU
# (and, on Android, the same ABI) as the frontend itself.  This script drives
# the CMake cross-builds and drops ready-to-install cores into
# build/libretro-dist/<target>/ together with the .info RetroArch reads.
#
#     tools/build_libretro_cores.sh --list
#     tools/build_libretro_cores.sh linux-x86_64
#     tools/build_libretro_cores.sh android-arm64 android-armv7
#     tools/build_libretro_cores.sh all
#
# Targets:
#   linux-x86_64       native x86-64 Linux (host toolchain)
#   linux-aarch64      ARM64 Linux (Raspberry Pi OS, Armbian, handheld
#                      firmware) - needs zig, built with zig cc
#   linux-armv7        ARMv7 hard-float Linux (older handhelds) - needs zig
#   android-arm64      Android arm64-v8a - needs the Android NDK
#   android-armv7      Android armeabi-v7a (32-bit RetroArch) - needs the NDK
#   android-x86_64     Android x86_64 (emulators/Chromebooks) - needs the NDK
#   all                every target this host can actually build
#
# Options:
#   --clean            delete the per-target build directory before configuring
#   --jobs N           parallel build jobs (default: CPU count)
#   --out DIR          where the installable cores go
#                      (default: build/libretro-dist)
#   --api N            Android API level (default 21; the core needs >= 21)
#   --ndk DIR          Android NDK to use (default: $ANDROID_NDK_HOME, else the
#                      newest one under ~/Android/Sdk/ndk)
#   --no-strip         keep debug symbols in the installed copies
#
# Environment: ANDROID_NDK_HOME, NJ_LIBRETRO_OUT, NJ_ZIG_GLIBC (glibc baseline
# used for the zig cross-builds, default 2.17), CMAKE/GEN/Ninja as usual.
#
# Platform notes:
#   * Android cores run inside the RetroArch app, so the ABI must match the
#     app: a 64-bit RetroArch loads arm64-v8a, a 32-bit one armeabi-v7a.
#     Copy the .so and the .info into RetroArch's core directory (on Android
#     that directory is exposed under Settings > Directory > Core), then pick
#     "Load Core" and the JAR.
#   * Linux handhelds: copy both files into ~/.config/retroarch/cores/ (or the
#     frontend's core directory) and restart RetroArch.
#   * Windows and macOS are not targets of this script yet: the runtime's
#     hosted-file layer is POSIX (mkdir/opendir), so a Windows core needs that
#     port first even though the libretro adapter itself already compiles for
#     Win32.  On macOS the native build works: run cmake directly (the core
#     builds as .dylib) - see docs/LIBRETRO.md.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

JOBS="$( (command -v nproc >/dev/null 2>&1 && nproc) \
         || (command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu) \
         || echo 4)"
OUT="${NJ_LIBRETRO_OUT:-$ROOT/build/libretro-dist}"
API=21
DO_CLEAN=0
DO_STRIP=1
NDK_ARG=""
TARGETS=()

usage()
{
    sed -n '2,60p' "$0" | sed 's/^# \{0,1\}//; s/^#$//'
}

list_targets()
{
    cat <<'EOF'
linux-x86_64    native x86-64 Linux
linux-aarch64   ARM64 Linux (Raspberry Pi OS, Armbian, handhelds)
linux-armv7     ARMv7 hard-float Linux (older handhelds)
android-arm64   Android arm64-v8a
android-armv7   Android armeabi-v7a
android-x86_64  Android x86_64
all             every target this host can build
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list)      list_targets; exit 0 ;;
        --clean)     DO_CLEAN=1; shift ;;
        --jobs)      JOBS="$2"; shift 2 ;;
        --out)       OUT="$2"; shift 2 ;;
        --api)       API="$2"; shift 2 ;;
        --ndk)       NDK_ARG="$2"; shift 2 ;;
        --no-strip)  DO_STRIP=0; shift ;;
        -h|--help)   usage; exit 0 ;;
        all)         TARGETS+=(all); shift ;;
        -*)
            echo "unknown option: $1 (try --help)" >&2
            exit 2 ;;
        *)
            TARGETS+=("$1"); shift ;;
    esac
done

if [ "${#TARGETS[@]}" -eq 0 ]; then
    usage
    exit 2
fi

if [ "$API" -lt 21 ]; then
    echo "Android API $API is below the floor (21): the NDK ships no compiler" >&2
    echo "for an older level and nothing in this core needs one." >&2
    exit 2
fi

# ---------------------------------------------------------------------------
# Host details
# ---------------------------------------------------------------------------
HOST_OS="$(uname -s)"
HOST_CPU="$(uname -m)"

case "$HOST_OS-$HOST_CPU" in
    Linux-x86_64|Linux-amd64)   HOST_TARGET=linux-x86_64 ;;
    Linux-aarch64|Linux-arm64)  HOST_TARGET=linux-aarch64 ;;
    Darwin-arm64)               HOST_TARGET=macos-arm64 ;;
    Darwin-x86_64)              HOST_TARGET=macos-x86_64 ;;
    *)                          HOST_TARGET="" ;;
esac

ZIG="$(command -v zig 2>/dev/null || true)"
# Zig can pin the glibc symbol version it links against.  2.17 is the oldest
# glibc aarch64 ever shipped with, so an aarch64 core built for 2.17 runs on
# everything from Raspberry Pi OS Buster to current firmware; without the pin
# zig picked 2.29/2.34 and the core refused to load on older handhelds.
ZIG_GLIBC="${NJ_ZIG_GLIBC:-2.17}"

# NDK lookup, in the same order as tools/build_android.sh so a machine that
# builds the Android port can build the Android core with no extra setup.
find_ndk()
{
    local ndk base
    if [ -n "$NDK_ARG" ]; then
        echo "$NDK_ARG"
        return 0
    fi
    if [ -n "${ANDROID_NDK_HOME:-}" ]; then
        echo "${ANDROID_NDK_HOME%/}"
        return 0
    fi
    base="${ANDROID_HOME:-$HOME/Android/Sdk}/ndk"
    if [ -d "$base" ]; then
        ndk="$(ls -d "$base"/*/ 2>/dev/null | sort -V | tail -1)"
        if [ -n "$ndk" ]; then
            echo "${ndk%/}"
            return 0
        fi
    fi
    return 1
}

NDK=""
NDK_BIN=""
if NDK_TMP="$(find_ndk)"; then
    NDK="$NDK_TMP"
    case "$HOST_OS" in
        Darwin) NDK_PREBUILT="darwin-x86_64" ;;
        *)      NDK_PREBUILT="linux-x86_64" ;;
    esac
    NDK_BIN="$NDK/toolchains/llvm/prebuilt/$NDK_PREBUILT/bin"
fi

have_target()
{
    case "$1" in
        linux-x86_64)   [ "$HOST_TARGET" = "linux-x86_64" ] ;;
        linux-aarch64)  { [ -n "$HOST_TARGET" ] && [ "$HOST_TARGET" = "linux-aarch64" ]; } \
                        || { [ -n "$ZIG" ] && [ "$HOST_OS" = "Linux" ]; } ;;
        linux-armv7)    [ -n "$ZIG" ] && [ "$HOST_OS" = "Linux" ] ;;
        android-*)      [ -n "$NDK_BIN" ] && [ -d "$NDK_BIN" ] ;;
        macos-*)        [ "$HOST_OS" = "Darwin" ] ;;
        *)              return 1 ;;
    esac
}

# ---------------------------------------------------------------------------
# Build helpers
# ---------------------------------------------------------------------------
COMMON_CMAKE=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DNJ_BUILD_TESTS=OFF
    -DNJ_BUILD_CLI=OFF
    -DNJ_BUILD_SDL2=OFF
    -DNJ_BUILD_EMBEDDED_PORTS=OFF
    -DNJ_BUILD_POSIX_NETWORK=OFF
)

artifact_of()
{
    # $1 = build directory; prints the core path, empty if the build failed.
    find "$1/libretro" -maxdepth 1 -type f \
         \( -name 'nano_j2me_libretro.so' -o -name 'nano_j2me_libretro.dylib' \
            -o -name 'nano_j2me_libretro.dll' \) 2>/dev/null | head -1
}

strip_tool_for()
{
    # $1 = target name; prints a strip tool that understands the artifact.
    case "$1" in
        android-*)
            if [ -x "$NDK_BIN/llvm-strip" ]; then echo "$NDK_BIN/llvm-strip"; return; fi ;;
    esac
    if command -v llvm-strip >/dev/null 2>&1; then
        command -v llvm-strip
    elif command -v strip >/dev/null 2>&1; then
        command -v strip
    fi
}

readelf_tool_for()
{
    case "$1" in
        android-*)
            if [ -x "$NDK_BIN/llvm-readelf" ]; then echo "$NDK_BIN/llvm-readelf"; return; fi ;;
    esac
    if command -v llvm-readelf >/dev/null 2>&1; then
        command -v llvm-readelf
    elif command -v readelf >/dev/null 2>&1; then
        command -v readelf
    fi
}

check_arch()
{
    # $1 = target name, $2 = artifact.  Guards against a mis-configured
    # toolchain that "succeeds" while producing a core for the wrong CPU
    # (the classic way: a host library or compiler leaking into a cross build).
    local name="$1" art="$2" expect desc
    command -v file >/dev/null 2>&1 || return 0
    case "$name" in
        linux-x86_64)   expect="x86-64" ;;
        linux-aarch64)  expect="ARM aarch64" ;;
        linux-armv7)    expect="ARM, EABI5" ;;
        android-arm64)  expect="ARM aarch64" ;;
        android-armv7)  expect="ARM, EABI5" ;;
        android-x86_64) expect="x86-64" ;;
        *)              return 0 ;;
    esac
    desc="$(file -b "$art" 2>/dev/null || true)"
    case "$desc" in
        *"$expect"*) : ;;
        *) echo "error: $art is not for $name (file says: $desc)" >&2
           return 1 ;;
    esac
    case "$name" in
        android-*)
            case "$desc" in
                *Android*) : ;;
                *) echo "error: $art is not an Android core (file says: $desc)" >&2
                   return 1 ;;
            esac ;;
    esac
    return 0
}

check_exports()
{
    # $1 = target name, $2 = artifact.  A core with no exported retro_* entry
    # points dlopens fine and then looks broken in the frontend menu, so say so.
    local name="$1" art="$2" reader
    case "$art" in *.dll) return 0 ;; esac
    reader="$(readelf_tool_for "$name")" || true
    [ -n "$reader" ] && [ -x "$reader" ] || return 0
    # Read into a file instead of piping into `grep -q`: with `set -o pipefail`
    # the reader would die of SIGPIPE the moment grep found the symbol and the
    # pipeline would be reported as a failure (llvm-readelf even prints
    # "LLVM ERROR: IO failure on output stream: Broken pipe").
    local tmp ok=1
    tmp="$(mktemp 2>/dev/null)" || return 0
    if "$reader" --dyn-syms "$art" >"$tmp" 2>/dev/null; then
        grep -qE '[[:space:]]retro_run$' "$tmp" || ok=0
    else
        ok=0
    fi
    rm -f "$tmp"
    if [ "$ok" -ne 1 ]; then
        echo "warning: $art does not export retro_run" >&2
        return 1
    fi
    return 0
}

build_target()
{
    local name="$1"
    local dir="$ROOT/build/libretro-$name"
    local extra=()
    local art dist size

    case "$name" in
        linux-x86_64)
            [ "$HOST_TARGET" = "linux-x86_64" ] || {
                echo "$name: only builds on x86-64 Linux" >&2; return 1; } ;;
        linux-aarch64)
            if [ "$HOST_TARGET" = "linux-aarch64" ]; then
                : # native compiler
            else
                [ -n "$ZIG" ] || {
                    echo "$name: needs zig (or run on ARM64 Linux)" >&2; return 1; }
                extra+=(
                    -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/cmake/toolchain-zig-linux.cmake"
                    -DNJ_ZIG_TARGET="aarch64-linux-gnu.$ZIG_GLIBC"
                    -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
                )
            fi ;;
        linux-armv7)
            [ -n "$ZIG" ] || { echo "$name: needs zig" >&2; return 1; }
            extra+=(
                -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/cmake/toolchain-zig-linux.cmake"
                -DNJ_ZIG_TARGET="arm-linux-gnueabihf.$ZIG_GLIBC"
                -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
            ) ;;
        android-arm64|android-armv7|android-x86_64)
            [ -n "$NDK_BIN" ] && [ -d "$NDK_BIN" ] || {
                echo "$name: needs the Android NDK (set ANDROID_NDK_HOME)" >&2; return 1; }
            case "$name" in
                android-arm64)  ABI=arm64-v8a ;;
                android-armv7)  ABI=armeabi-v7a ;;
                android-x86_64) ABI=x86_64 ;;
            esac
            extra+=(
                -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
                -DANDROID_ABI="$ABI"
                -DANDROID_PLATFORM="android-$API"
                -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
            ) ;;
        macos-*)
            [ "$HOST_OS" = "Darwin" ] || {
                echo "$name: only builds on macOS" >&2; return 1; } ;;
        *)
            echo "unknown target: $name (try --list)" >&2
            return 2 ;;
    esac

    echo "==> $name: configure  ($dir)"
    if [ "$DO_CLEAN" -eq 1 ]; then
        rm -rf "$dir"
    fi
    if ! cmake -S "$ROOT" -B "$dir" "${COMMON_CMAKE[@]}" "${extra[@]}" \
         > "$dir.configure.log" 2>&1; then
        echo "$name: configure failed (see $dir.configure.log)" >&2
        tail -20 "$dir.configure.log" >&2
        return 1
    fi

    echo "==> $name: build"
    if ! cmake --build "$dir" --target nano_j2me_libretro -j "$JOBS" \
         > "$dir.build.log" 2>&1; then
        echo "$name: build failed (see $dir.build.log)" >&2
        tail -20 "$dir.build.log" >&2
        return 1
    fi

    art="$(artifact_of "$dir")"
    if [ -z "$art" ] || [ ! -f "$art" ]; then
        echo "$name: the build produced no core in $dir/libretro" >&2
        return 1
    fi

    check_exports "$name" "$art" || true

    dist="$OUT/$name"
    mkdir -p "$dist" || return 1
    cp -f "$art" "$dist/" || return 1
    cp -f "$ROOT/src/port/libretro/nano_j2me_libretro.info" "$dist/" || return 1
    art="$dist/$(basename "$art")"

    if [ "$DO_STRIP" -eq 1 ]; then
        local tool
        tool="$(strip_tool_for "$name")"
        if [ -n "$tool" ] && [ -x "$tool" ]; then
            if cp -f "$art" "$art.tmp" \
               && "$tool" --strip-unneeded "$art.tmp" 2>/dev/null; then
                mv -f "$art.tmp" "$art"
            else
                rm -f "$art.tmp"
            fi
        fi
    fi

    check_arch "$name" "$art" || return 1

    size="$(wc -c < "$art")"
    printf '%-14s %10s bytes  %s\n' "$name" "$size" "$dist"
    return 0
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
FAILED=0
for t in "${TARGETS[@]}"; do
    if [ "$t" = "all" ]; then
        CANDIDATES=(linux-x86_64 linux-aarch64 linux-armv7
                    android-arm64 android-armv7 android-x86_64)
        for c in "${CANDIDATES[@]}"; do
            if have_target "$c"; then
                build_target "$c" || FAILED=1
            else
                echo "-- $c: not buildable on this host (skipped)"
            fi
        done
        continue
    fi
    build_target "$t" || FAILED=1
done

if [ "$FAILED" -ne 0 ]; then
    echo "one or more cores failed; artifacts that did build are in $OUT" >&2
    exit 1
fi

echo
echo "done: installable cores in $OUT"
echo "RetroArch: copy the core and its .info into the frontend's core"
echo "directory (on Android: Settings > Directory > Core)."
