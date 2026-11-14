#!/usr/bin/env bash
#
# Build the nano_j2me libretro core for one or more platform/architecture
# targets.  One core binary is not portable: a libretro core is a shared
# library, and the frontend can only dlopen a core built for the same OS/CPU
# (and, on Android, the same ABI) as the frontend itself.  This script drives
# the CMake cross-builds and drops ready-to-install cores into
# build/libretro-dist/<target>/ together with the .info RetroArch reads.
# Each directory also retains license/font notices and an unresolved codec
# review status. These local validation candidates are not cleared releases.
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
#   linux-armv7        ARMv7+NEON hard-float Linux (handhelds) - needs zig
#   android-arm64      Android arm64-v8a - needs the Android NDK
#   android-armv7      Android armeabi-v7a (32-bit RetroArch) - needs the NDK
#   android-x86_64     Android x86_64 (emulators/Chromebooks) - needs the NDK
#   windows-x86_64     64-bit Windows (64-bit RetroArch/other frontends)
#                      - needs mingw-w64 in PATH
#   windows-i686       32-bit Windows (a 32-bit frontend cannot load a 64-bit
#                      DLL) - needs the 32-bit mingw-w64 cross gcc
#   all                every target this host can actually build
#
# Options:
#   --clean            delete the per-target build directory before configuring
#   --jobs N           parallel build jobs (default: CPU count)
#   --out DIR          where the installable cores go
#                      (default: build/libretro-dist)
#   --api N            Android API level (default 21; the core needs >= 21)
#   --ndk DIR          Android NDK to use (default: $ANDROID_NDK_HOME or
#                      $ANDROID_NDK_ROOT, else newest under the SDK's ndk/)
#   --no-strip         keep debug symbols in the installed copies
#
# Environment: ANDROID_NDK_HOME/ANDROID_NDK_ROOT, ANDROID_HOME/ANDROID_SDK_ROOT,
# NJ_LIBRETRO_OUT, NJ_ZIG_GLIBC (Zig cross-build glibc baseline, default 2.17).
# Requires cmake and ninja in PATH.
#
# Platform notes:
#   * Android cores run inside the RetroArch app, so the ABI must match the
#     app: a 64-bit RetroArch loads arm64-v8a, a 32-bit one armeabi-v7a.
#     Copy the .so and the .info into RetroArch's core directory (on Android
#     that directory is exposed under Settings > Directory > Core), then pick
#     "Load Core" and the JAR.
#   * Linux handhelds: copy both files into ~/.config/retroarch/cores/ (or the
#     frontend's core directory) and restart RetroArch.
#   * Windows: the runtime and the libretro adapter have a Win32 hosted-file
#     and thread layer, so the core is a native PE DLL (no POSIX emulation at
#     runtime, it links only kernel32 and the mingw C runtime).  Copy the .dll
#     and the .info into the frontend's core directory.  Cross-building from
#     Linux only needs the mingw-w64 gcc in PATH; tools/check_libretro_windows.sh
#     runs the resulting DLL under wine and asserts it presents frames/audio.
#   * macOS: not a target of this script yet. Native CMake configures a .dylib,
#     but this script does not test it - see docs/LIBRETRO.md.
#   * Native Linux x86-64 uses the host's libc and can require a newer glibc
#     than deployment machines. Inspect symbol versions before distributing.
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
    # Print the comment banner and nothing else: stop at the first command so
    # a new target added to the banner cannot leak script lines into --help.
    sed -n '2,/^set -uo pipefail$/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//; s/^#$//'
}

list_targets()
{
    cat <<'EOF'
linux-x86_64    native x86-64 Linux
linux-aarch64   ARM64 Linux (Raspberry Pi OS, Armbian, handhelds)
linux-armv7     ARMv7+NEON hard-float Linux (handhelds)
android-arm64   Android arm64-v8a
android-armv7   Android armeabi-v7a
android-x86_64  Android x86_64
windows-x86_64  64-bit Windows (mingw-w64 cross build)
windows-i686    32-bit Windows (mingw-w64 cross build)
all             every target this host can build
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list)      list_targets; exit 0 ;;
        --clean)     DO_CLEAN=1; shift ;;
        --jobs|--out|--api|--ndk)
            [ "$#" -ge 2 ] && [ -n "$2" ] || {
                echo "error: $1 needs a value" >&2; exit 2; }
            case "$1" in
                --jobs) JOBS="$2" ;;
                --out)  OUT="$2" ;;
                --api)  API="$2" ;;
                --ndk)  NDK_ARG="$2" ;;
            esac
            shift 2 ;;
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

for setting in "$JOBS" "$API"; do
    case "$setting" in
        ''|*[!0-9]*) echo "error: --jobs and --api require positive integers" >&2; exit 2 ;;
    esac
done
if ! [ "$JOBS" -gt 0 ] 2>/dev/null || ! [ "$API" -ge 21 ] 2>/dev/null; then
    echo "error: --jobs must be positive and --api must be at least 21" >&2
    exit 2
fi

# Resolve output relative to the repository, independent of the caller's cwd.
case "$OUT" in
    /*) : ;;
    *) OUT="$ROOT/$OUT" ;;
esac

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
# mingw-w64 toolchains: the Windows core is a native PE DLL built by cross-gcc.
# Both Debian/Ubuntu and Fedora install these driver binaries in PATH.
MINGW64_CC="x86_64-w64-mingw32-gcc"
MINGW64_STRIP="x86_64-w64-mingw32-strip"
MINGW64_OBJDUMP="x86_64-w64-mingw32-objdump"
MINGW32_CC="i686-w64-mingw32-gcc"
MINGW32_STRIP="i686-w64-mingw32-strip"
MINGW32_OBJDUMP="i686-w64-mingw32-objdump"
# Zig can pin the glibc symbol version it links against.  2.17 is the oldest
# glibc aarch64 ever shipped with. Pinning avoids newer symbol requirements
# (2.29/2.34 without the pin) that prevent loading on older handheld firmware.
# Actual device runtime compatibility still needs testing.
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
    ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
    if [ -n "$ndk" ]; then
        echo "${ndk%/}"
        return 0
    fi
    base="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}/ndk"
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
    case "$NDK" in
        /*) : ;;
        *) NDK="$(pwd)/$NDK" ;;
    esac
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
        windows-x86_64) command -v "$MINGW64_CC" >/dev/null 2>&1 ;;
        windows-i686)   command -v "$MINGW32_CC" >/dev/null 2>&1 ;;
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
    -DNJ_BUILD_LIBRETRO=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
    -DNJ_BUILD_TESTS=OFF
    -DNJ_BUILD_CLI=OFF
    -DNJ_BUILD_SDL2=OFF
    -DNJ_BUILD_EMBEDDED_PORTS=OFF
    -DNJ_BUILD_POSIX_NETWORK=OFF
)

artifact_of()
{
    # $1 = build directory; prints the core path, empty if the build failed.
    local path
    for path in "$1/libretro/nano_j2me_libretro."{so,dylib,dll}; do
        if [ -f "$path" ]; then
            printf '%s\n' "$path"
            return 0
        fi
    done
    return 1
}

strip_tool_for()
{
    # $1 = target name; prints a strip tool that understands the artifact.
    case "$1" in
        android-*)
            if [ -x "$NDK_BIN/llvm-strip" ]; then echo "$NDK_BIN/llvm-strip"; return; fi ;;
        windows-x86_64)
            if command -v "$MINGW64_STRIP" >/dev/null 2>&1; then
                command -v "$MINGW64_STRIP"; return
            fi ;;
        windows-i686)
            if command -v "$MINGW32_STRIP" >/dev/null 2>&1; then
                command -v "$MINGW32_STRIP"; return
            fi ;;
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

objdump_tool_for()
{
    # Windows PE exports: mingw objdump -p prints an "Export Address Table".
    case "$1" in
        windows-x86_64)
            if command -v "$MINGW64_OBJDUMP" >/dev/null 2>&1; then
                command -v "$MINGW64_OBJDUMP"; return
            fi ;;
        windows-i686)
            if command -v "$MINGW32_OBJDUMP" >/dev/null 2>&1; then
                command -v "$MINGW32_OBJDUMP"; return
            fi ;;
    esac
    command -v objdump 2>/dev/null || true
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
        windows-x86_64) expect="x86-64" ;;
        windows-i686)   expect="Intel i386" ;;
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
        windows-*)
            # A mis-configured mingw build that fell back to the host compiler
            # would leave a Linux ELF shared object named .dll; refuse it.
            case "$desc" in
                *"PE32"*"MS Windows"*) : ;;
                *) echo "error: $art is not a Windows PE DLL (file says: $desc)" >&2
                   return 1 ;;
            esac ;;
    esac
    return 0
}

check_exports()
{
    # All API entry points must exist, even for unsupported features (stubs).
    local name="$1" art="$2" reader tmp symbol pattern ok=1
    local symbols="retro_set_environment retro_set_video_refresh
retro_set_audio_sample retro_set_audio_sample_batch retro_set_input_poll
retro_set_input_state retro_init retro_deinit retro_api_version
retro_get_system_info retro_get_system_av_info retro_get_region
retro_load_game retro_run retro_unload_game retro_serialize retro_serialize_size
retro_unserialize retro_reset retro_set_controller_port_device retro_cheat_reset
retro_cheat_set retro_load_game_special retro_get_memory_data retro_get_memory_size"
    case "$art" in
        *.dll) reader="$(objdump_tool_for "$name")" ;;
        *.dylib) reader="$(command -v nm 2>/dev/null || true)" ;;
        *)     reader="$(readelf_tool_for "$name")" ;;
    esac
    [ -n "$reader" ] && [ -x "$reader" ] || {
        echo "error: no export inspection tool available for $name" >&2
        return 1
    }
    # Read into a file instead of piping into `grep -q`: with `set -o pipefail`
    # the reader would die of SIGPIPE the moment grep found the symbol and the
    # pipeline would be reported as a failure (llvm-readelf even prints
    # "LLVM ERROR: IO failure on output stream: Broken pipe").
    tmp="$(mktemp 2>/dev/null)" || {
        echo "error: cannot create temporary export report" >&2
        return 1
    }
    case "$art" in
        *.dll)
            "$reader" -p "$art" >"$tmp" 2>/dev/null || ok=0 ;;
        *.dylib)
            "$reader" -gU "$art" >"$tmp" 2>/dev/null || ok=0 ;;
        *)
            "$reader" --dyn-syms --wide "$art" >"$tmp" 2>/dev/null || ok=0 ;;
    esac
    if [ "$ok" -eq 1 ]; then
        for symbol in $symbols; do
            case "$art" in
                *.dll) pattern="[[:space:]]${symbol}\$" ;;
                *.dylib) pattern="[[:space:]]_${symbol}\$" ;;
                *) pattern="[[:space:]][1-9][0-9]*[[:space:]]+${symbol}\$" ;;
            esac
            if ! grep -qE "$pattern" "$tmp"; then
                echo "error: $art does not export $symbol" >&2
                ok=0
            fi
        done
    fi
    rm -f "$tmp"
    if [ "$ok" -ne 1 ]; then
        echo "error: could not validate libretro exports in $art" >&2
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
        windows-x86_64)
            command -v "$MINGW64_CC" >/dev/null 2>&1 || {
                echo "$name: needs $MINGW64_CC (install the mingw-w64 package)" >&2
                return 1; }
            extra+=( -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/toolchain-mingw-w64.cmake" ) ;;
        windows-i686)
            command -v "$MINGW32_CC" >/dev/null 2>&1 || {
                echo "$name: needs $MINGW32_CC (install the mingw-w64-i686 package)" >&2
                return 1; }
            extra+=( -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/toolchain-mingw-w64-i686.cmake" ) ;;
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

    check_arch "$name" "$art" || return 1
    check_exports "$name" "$art" || return 1

    dist="$OUT/$name"
    mkdir -p "$dist" || return 1
    cp -f "$art" "$dist/" || return 1
    cp -f "$ROOT/src/port/libretro/nano_j2me_libretro.info" "$dist/" || return 1
    # Keep implementation, font and separately licensed AMR data notices next
    # to each artifact, together with the source trail and verification limits.
    mkdir -p "$dist/docs/licenses" "$dist/docs/notes" || return 1
    local notice
    for notice in LICENSE docs/FONT_LICENSES.md docs/CODEC_PROVENANCE.md \
                  docs/M4V_TABLE_PROVENANCE.md \
                   docs/AMR_DATA_LICENSES.md docs/SYSTEM_AMR.md \
                   docs/notes/2026-11-12-amr-independent-implementation.md \
                   docs/licenses/AMR-Apache-2.0.txt \
                   docs/licenses/AMR-OpenCORE-NOTICE.txt \
                  docs/notes/2026-11-12-mpeg4-independent-implementation.md \
                  docs/licenses/third-party-code-MIT.txt \
                  docs/licenses/Liberation-Fonts-OFL.txt \
                  docs/licenses/source-han-sans-OFL.txt; do
        cp -f "$ROOT/$notice" "$dist/$notice" || return 1
    done
    cat > "$dist/BUILD_STATUS.txt" <<'EOF'
Local build: MIT runtime code with separately licensed AMR and font data.
Retain LICENSE and docs/licenses/third-party-code-MIT.txt for runtime notices.
AMR numeric data retain Apache-2.0 attribution: keep docs/AMR_DATA_LICENSES.md,
docs/licenses/AMR-Apache-2.0.txt and docs/licenses/AMR-OpenCORE-NOTICE.txt.
Fallback font data retain SIL OFL 1.1: keep docs/FONT_LICENSES.md and both
font notices in docs/licenses/ with any package containing these data.
See docs/CODEC_PROVENANCE.md, docs/M4V_TABLE_PROVENANCE.md and the included
AMR/MPEG-4 implementation reports for source evidence and bounded review scope.
System AMR is runtime-probed and falls back to the built-in decoder; see
docs/SYSTEM_AMR.md for availability and platform verification limits.
This local artifact is not an official libretro build or acceptance notice.
EOF
    [ "$?" -eq 0 ] || return 1
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
    check_exports "$name" "$art" || return 1

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
                    android-arm64 android-armv7 android-x86_64
                    windows-x86_64 windows-i686)
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
echo "directory (on Android: Settings > Directory > Core; on Windows: the"
echo "cores directory under the frontend's install or config folder)."
