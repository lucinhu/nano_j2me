#!/usr/bin/env bash
#
# Build the per-architecture Android release APKs.
#
#     tools/build_android_apks.sh [--out DIR] [--no-legacy] [--skip-native]
#                                 [--gles2] [--no-gles2] [--fsr2-sdk PREFIX]
#
# --fsr2-sdk additionally builds the enhanced arm64-v8a APK for API 28+
# with Vulkan, AMD FSR2 and JIT. The standard APKs keep their existing defaults.
#
# Android releases ship one APK per ABI instead of a universal APK: a merged
# APK carries four copies of libnj2me.so (about 2.4 MB of dead weight for any
# single device) and forces the launcher to pick the right one at install
# time.  This script drives tools/build_android_apk.sh once per ABI and the
# legacy ARMv5 script once, renames the results to
# nj2me-<version>-<abi>.apk / nj2me-<version>-legacy.apk, checks the ABI and
# versionCode recorded in each APK, verifies the signatures and writes
# SHA256SUMS-<version>-apks.txt next to them.
#
# Release APKs are built with the GLES2 hardware-acceleration backend by
# default (--no-gles2 produces the older software-only binaries, still useful
# as a fallback for devices or titles where the GPU path misbehaves).  Native
# code is compiled per ABI by the called scripts; --skip-native is only useful
# together with an already-populated build tree (it is forwarded to nothing
# today and exists for symmetry with the other release tools).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(sed -n 's/^project(nano_j2me VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || { echo "cannot read the version from CMakeLists.txt" >&2; exit 2; }
VERSION_CODE=$((10#${VERSION//./}))
FSR2_SDK=""
OUT="$ROOT/build/android-release"
LEGACY=1
GLES2=1
ABIS="arm64-v8a armeabi-v7a x86 x86_64"

while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="${2:?--out needs a directory}"; shift 2 ;;
        --no-legacy) LEGACY=0; shift ;;
        --fsr2-sdk) FSR2_SDK="${2:?--fsr2-sdk needs an SDK prefix}"; shift 2 ;;
        --gles2) GLES2=1; shift ;;
        --no-gles2) GLES2=0; shift ;;
        --skip-native) shift ;;
        -h|--help) sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

SDK="${ANDROID_HOME:-$HOME/Android/Sdk}"
BT="$(ls -d "$SDK"/build-tools/*/ 2>/dev/null | sort -V | tail -1)"; BT="${BT%/}"
[ -x "$BT/aapt2" ] || { echo "missing $BT/aapt2" >&2; exit 2; }
[ -x "$BT/apksigner" ] || { echo "missing $BT/apksigner" >&2; exit 2; }
mkdir -p "$OUT" || exit 1
OUT="$(cd "$OUT" && pwd)" || exit 1

TMP="$(mktemp -d)" || exit 1
trap 'rm -rf -- "${TMP:?}"' EXIT

fail() { echo "FAILED: $*" >&2; exit 1; }

# Record only APKs produced by this invocation (OUT may contain earlier runs).
artifacts=()
collect_apk() {
    local name="nj2me-$VERSION-$2.apk"
    cp "$1" "$OUT/$name" || fail "copy $name"
    artifacts+=("$name")
}

extra=()
[ "$GLES2" = 1 ] && extra+=(--gles2)
for abi in $ABIS; do
    echo "== $abi"
    mkdir -p "$TMP/$abi"
    "$ROOT/tools/build_android_apk.sh" --abi "$abi" --out "$TMP/$abi" \
        "${extra[@]}" || fail "build_android_apk.sh --abi $abi"
    [ -f "$TMP/$abi/nj2me.apk" ] || fail "no APK for $abi"
    collect_apk "$TMP/$abi/nj2me.apk" "$abi"
done

if [ -n "$FSR2_SDK" ]; then
    echo "== arm64-v8a with Vulkan, FSR2 and JIT"
    NJ_ANDROID_JIT=1 "$ROOT/tools/build_android_apk.sh" --abi arm64-v8a \
        --out "$TMP/enhanced" "${extra[@]}" --vulkan --fsr2-sdk "$FSR2_SDK" \
        || fail "enhanced arm64 APK"
    collect_apk "$TMP/enhanced/nj2me.apk" arm64-v8a-vulkan-fsr2
fi

if [ "$LEGACY" = 1 ]; then
    echo "== legacy (ARMv5, API 5)"
    mkdir -p "$TMP/legacy"
    "$ROOT/tools/build_android_legacy.sh" --out "$TMP/legacy" \
        || fail "build_android_legacy.sh"
    collect_apk "$TMP/legacy/nj2me-legacy.apk" legacy
fi

# Read each manifest once and verify the platform floor and the exact ABI.
for name in "${artifacts[@]}"; do
    f="$OUT/$name"
    variant=${name#nj2me-"$VERSION"-}; variant=${variant%.apk}
    case $variant in
        legacy) abi=armeabi; api=5; version_name="$VERSION-legacy" ;;
        arm64-v8a-vulkan-fsr2) abi=arm64-v8a; api=28; version_name="$VERSION" ;;
        *) abi=$variant; api=21; version_name="$VERSION" ;;
    esac
    manifest=$("$BT/aapt2" dump badging "$f") || fail "read $name"
    grep -Fq "versionCode='$VERSION_CODE' versionName='$version_name'" <<< "$manifest" || fail "$name version"
    grep -Exq "(minSdkVersion|sdkVersion):'$api'" <<< "$manifest" || fail "$name minimum SDK"
    grep -Fxq "native-code: '$abi'" <<< "$manifest" || fail "$name ABI"
    "$BT/apksigner" verify "$f" || fail "$name signature"
done

( cd "$OUT" && sha256sum "${artifacts[@]}" > "SHA256SUMS-$VERSION-apks.txt" ) || exit 1
printf 'Verified %d APKs in %s\n' "${#artifacts[@]}" "$OUT"
cat "$OUT/SHA256SUMS-$VERSION-apks.txt"
