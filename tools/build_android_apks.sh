#!/usr/bin/env bash
#
# Build the per-architecture Android release APKs.
#
#     tools/build_android_apks.sh [--out DIR] [--no-legacy] [--skip-native]
#                                 [--gles2] [--no-gles2]
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
VERSION_CODE="${VERSION//./}"
VERSION_CODE="${VERSION_CODE#"${VERSION_CODE%%[!0]*}"}"
[ -n "$VERSION_CODE" ] || VERSION_CODE=0
OUT="$ROOT/build/android-release"
LEGACY=1
GLES2=1
ABIS="arm64-v8a armeabi-v7a x86 x86_64"

while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="${2:?--out needs a directory}"; shift 2 ;;
        --no-legacy) LEGACY=0; shift ;;
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
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAILED: $*" >&2; exit 1; }

for abi in $ABIS; do
    echo "== $abi"
    rm -rf "$TMP/$abi"; mkdir -p "$TMP/$abi"
    extra=()
    [ "$GLES2" = 1 ] && extra+=(--gles2)
    "$ROOT/tools/build_android_apk.sh" --abi "$abi" --out "$TMP/$abi" \
        "${extra[@]}" || fail "build_android_apk.sh --abi $abi"
    [ -f "$TMP/$abi/nj2me.apk" ] || fail "no APK for $abi"
    cp "$TMP/$abi/nj2me.apk" "$OUT/nj2me-$VERSION-$abi.apk"
done

if [ "$LEGACY" = 1 ]; then
    echo "== legacy (ARMv5, API 5)"
    rm -rf "$TMP/legacy"; mkdir -p "$TMP/legacy"
    "$ROOT/tools/build_android_legacy.sh" --out "$TMP/legacy" \
        || fail "build_android_legacy.sh"
    [ -f "$TMP/legacy/nj2me-legacy.apk" ] || fail "no legacy APK"
    cp "$TMP/legacy/nj2me-legacy.apk" "$OUT/nj2me-$VERSION-legacy.apk"
fi

# Verify what was built: the ABI(s) recorded in the APK, the version, and the
# signature.  A universal APK slipping through here would defeat the point.
for f in "$OUT"/nj2me-"$VERSION"-*.apk; do
    [ -f "$f" ] || fail "no APKs produced"
    code="$("$BT/aapt2" dump badging "$f" 2>/dev/null | sed -n "s/.*versionCode='\([0-9]*\)'.*/\1/p" | head -1)"
    [ "$code" = "$VERSION_CODE" ] || fail "$(basename "$f") versionCode $code != $VERSION_CODE"
    abis="$("$BT/aapt2" dump badging "$f" 2>/dev/null | sed -n "s/^native-code: //p" | head -1)"
    case "$(basename "$f")" in
        *-legacy.apk) [ "$abis" = "'armeabi'" ] || fail "legacy APK ABI $abis" ;;
        nj2me-"$VERSION"-arm64-v8a.apk) [ "$abis" = "'arm64-v8a'" ] || fail "arm64 APK ABI $abis" ;;
        nj2me-"$VERSION"-armeabi-v7a.apk) [ "$abis" = "'armeabi-v7a'" ] || fail "armv7 APK ABI $abis" ;;
        nj2me-"$VERSION"-x86.apk) [ "$abis" = "'x86'" ] || fail "x86 APK ABI $abis" ;;
        nj2me-"$VERSION"-x86_64.apk) [ "$abis" = "'x86_64'" ] || fail "x86_64 APK ABI $abis" ;;
        *) fail "unexpected release APK $(basename "$f")" ;;
    esac
    "$BT/apksigner" verify "$f" >/dev/null 2>&1 || fail "unsigned APK $(basename "$f")"
done

( cd "$OUT" && sha256sum nj2me-"$VERSION"-*.apk > "SHA256SUMS-$VERSION-apks.txt" )
echo
echo "== split APKs for $VERSION ($OUT)"
ls -l "$OUT"/nj2me-"$VERSION"-*.apk
echo
cat "$OUT/SHA256SUMS-$VERSION-apks.txt"
