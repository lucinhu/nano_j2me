#!/usr/bin/env bash
#
# tools/simulate_ci_builds.sh -- locally simulate the commands that the
# libretro-infrastructure CI templates run for this core, so that a broken
# .gitlab-ci.yml contract is visible before a repository is submitted upstream.
#
# The templates are fetched from git.libretro.com and are not vendored here.
# This script reproduces their command shape:
#
#   cmake -DCMAKE_BUILD_TYPE=Release $CORE_ARGS "$CMAKE_SOURCE_ROOT" -B$BUILD_DIR
#   cmake --build $BUILD_DIR --target ${CORENAME}_libretro --config Release -- -j $NUMPROC
#
# and then checks that the artifact landed where the template's
# `mv $BUILD_DIR/$EXTRA_PATH/$LIBNAME` step expects it.
#
# This is a build-contract check, not the real GitLab pipeline: the upstream
# jobs run in libretro's Docker images and their results are collected by the
# libretro buildbot. A passing simulation does not mean a core has been
# accepted, nor that its runtime behavior was validated.
#
# Usage:
#   tools/simulate_ci_builds.sh                     # linux + windows + android
#   tools/simulate_ci_builds.sh --only linux,windows
#   tools/simulate_ci_builds.sh --out /tmp/ci-sim --jobs 10
#
# Environment:
#   ANDROID_NDK_ROOT / ANDROID_NDK_HOME / ANDROID_HOME / ~/Android/Sdk
#       where to find the Android NDK (the newest installed one is used)
#   MINGW_PREFIX
#       mingw-w64 prefix for the Windows contract (default x86_64-w64-mingw32)
#
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=/tmp/nano-j2me-ci-sim
JOBS=$(nproc 2>/dev/null || echo 4)
ONLY="linux windows android"
CORE=nano_j2me
EXTRA_PATH=libretro
GENERATOR="Unix Makefiles"

usage() {
    sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT=$2; shift 2 ;;
        --jobs) JOBS=$2; shift 2 ;;
        --only) ONLY=${2//,/ }; shift 2 ;;
        --generator) GENERATOR=$2; shift 2 ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

# Read CORE_ARGS straight out of the committed CI file so the simulation cannot
# drift from what the real jobs pass.
core_args=()
while IFS= read -r line; do
    for tok in $line; do
        case $tok in
            -D*) core_args+=("$tok") ;;
        esac
    done
done < <(awk '
    /^ *CORE_ARGS: *>-$/ { inside = 1; next }
    inside && /^ *[A-Za-z_]/ { inside = 0 }
    inside { print }
' "$ROOT/.gitlab-ci.yml")
if [ "${#core_args[@]}" -eq 0 ]; then
    echo "simulate_ci_builds: could not read CORE_ARGS from .gitlab-ci.yml" >&2
    exit 1
fi

find_ndk() {
    local c
    for c in "${ANDROID_NDK_ROOT:-}" "${ANDROID_NDK_HOME:-}" \
             ${ANDROID_HOME:+"$ANDROID_HOME/ndk"} \
             "$HOME/Android/Sdk/ndk"; do
        [ -d "$c" ] || continue
        if [ -f "$c/build/cmake/android.toolchain.cmake" ]; then echo "$c"; return; fi
        ls -d "$c"/*/ 2>/dev/null | sort -V | tail -1 | sed 's:/*$::' | {
            read -r d
            [ -n "$d" ] && [ -f "$d/build/cmake/android.toolchain.cmake" ] && echo "$d"
        }
    done
}

fail=0
pass=0
summary=()

check_artifact() { # target build-subdir expected-relative-path
    local label=$1 rel=$2
    if [ -e "$rel" ] && [ -s "$rel" ]; then
        summary+=("PASS  $label: $rel ($(stat -c %s "$rel") bytes)")
        pass=$((pass + 1))
    else
        summary+=("FAIL  $label: missing artifact $rel")
        fail=$((fail + 1))
    fi
}

build() { # label generator-args... ; sets BUILD_DIR
    local label=$1; shift
    BUILD_DIR="$OUT/$label"
    rm -rf "$BUILD_DIR"
    echo "=== $label: configure"
    cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" "${core_args[@]}" "$@" \
        > "$OUT/$label-configure.log" 2>&1 || {
        summary+=("FAIL  $label: configure (see $OUT/$label-configure.log)")
        fail=$((fail + 1)); return 1; }
    echo "=== $label: build target ${CORE}_libretro"
    cmake --build "$BUILD_DIR" --target "${CORE}_libretro" --config Release -- -j "$JOBS" \
        > "$OUT/$label-build.log" 2>&1 || {
        summary+=("FAIL  $label: build (see $OUT/$label-build.log)")
        fail=$((fail + 1)); return 1; }
    return 0
}

mkdir -p "$OUT"

for target in $ONLY; do
    case $target in
        linux)
            if build linux; then
                check_artifact linux "$BUILD_DIR/$EXTRA_PATH/${CORE}_libretro.so"
            fi
            ;;
        windows)
            prefix=${MINGW_PREFIX:-x86_64-w64-mingw32}
            if command -v "$prefix-gcc" >/dev/null; then
                if build windows \
                    -DCMAKE_SYSTEM_NAME=Windows \
                    -DCMAKE_C_COMPILER="$prefix-gcc" \
                    -DCMAKE_RC_COMPILER="$prefix-windres"; then
                    check_artifact windows "$BUILD_DIR/$EXTRA_PATH/${CORE}_libretro.dll"
                fi
            else
                summary+=("SKIP  windows: $prefix-gcc not installed")
            fi
            ;;
        android)
            ndk=$(find_ndk || true)
            if [ -n "$ndk" ]; then
                if build android \
                    -DANDROID_PLATFORM=android-21 \
                    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
                    -DANDROID_STL=c++_static \
                    -DANDROID_ABI=arm64-v8a; then
                    check_artifact "android (template renames to ${CORE}_libretro_android.so)" \
                        "$BUILD_DIR/$EXTRA_PATH/${CORE}_libretro.so"
                fi
            else
                summary+=("SKIP  android: no NDK with build/cmake/android.toolchain.cmake")
            fi
            ;;
        *)
            echo "simulate_ci_builds: unknown target '$target'" >&2
            exit 1
            ;;
    esac
done

echo
echo "=== CI template command simulation ($(uname -sm), cmake $(cmake --version | head -1 | awk '{print $3}'))"
printf '%s\n' "${summary[@]}"
echo
if [ "$fail" -ne 0 ]; then
    echo "simulate_ci_builds: $fail failed, $pass passed"
    exit 1
fi
echo "simulate_ci_builds: all $pass contracts satisfied"
