#!/usr/bin/env bash
# Build the Android 2.0+ (API 5), ARMv5-compatible edition.
# Usage: tools/build_android_legacy.sh [--out DIR] [--native-only]
# Requires NDK r10e (GCC 4.9), an API 5 android.jar and current SDK build-tools.
# ANDROID_NDK_LEGACY_HOME and ANDROID_LEGACY_JAR override their locations.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/android-legacy"
NATIVE_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="${2:?--out needs a directory}"; shift 2 ;;
        --native-only) NATIVE_ONLY=1; shift ;;
        -h|--help) sed -n '2,5p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
NDK="${ANDROID_NDK_LEGACY_HOME:-$ROOT/.local-tools/android-legacy/android-ndk-r10e}"
SDK="${ANDROID_HOME:-$HOME/Android/Sdk}"
CC="$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc"
READELF="${CC%gcc}readelf"
# Compile against Android 2.0 libc stubs to enforce the API 5 native floor.
SYSROOT="$NDK/platforms/android-5/arch-arm"
[ -x "$CC" ] && [ -d "$SYSROOT" ] || {
    echo 'NDK r10e with GCC 4.9 and platforms/android-5/arch-arm is required.' >&2
    echo 'Set ANDROID_NDK_LEGACY_HOME; modern NDKs cannot target ARMv5/API 5.' >&2
    exit 2
}
JAR="${ANDROID_LEGACY_JAR:-$SDK/platforms/android-5/android.jar}"
if [ ! -f "$JAR" ] && [ -z "${ANDROID_LEGACY_JAR:-}" ]; then
    JAR="$ROOT/.local-tools/android-legacy/sdk5/android-2.0_r01-linux/android.jar"
fi
BT=""
if [ "$NATIVE_ONLY" = 0 ]; then
    [ -f "$JAR" ] || { echo 'Set ANDROID_LEGACY_JAR to the API 5 android.jar.' >&2; exit 2; }
    BT="$(ls -d "$SDK"/build-tools/*/ 2>/dev/null | sort -V | tail -1)"; BT="${BT%/}"
    for t in aapt d8 zipalign apksigner; do
        [ -x "$BT/$t" ] || { echo "missing SDK build tool: $t" >&2; exit 2; }
    done
fi
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
# A fresh directory keeps interrupted builds from mixing objects or resources.
# Intermediates are retained for diagnosing cross-build failures.
W="$(mktemp -d "$OUT/work.XXXXXX")"
mkdir -p "$W/lib/armeabi" "$W/classes" "$W/dex"
# Old bionic stdint.h omits SIZE_MAX; GCC provides the exact target value.
DEFS=(-DNJ_ANDROID_LEGACY=1 -DSIZE_MAX=__SIZE_MAX__ -DNJ_PIXFMT=NJ_PIXFMT_RGB565
      -DNJ_ENABLE_PNG=1 -DNJ_ENABLE_JPEG=1 -DNJ_ENABLE_GIF=1 -DNJ_ENABLE_INFLATE=1
      -DNJ_ENABLE_WAV=1 -DNJ_ENABLE_RMS=1 -DNJ_ENABLE_IO=1
      -DNJ_ENABLE_FLOAT=1 -DNJ_ENABLE_M3G=1 -DNJ_ENABLE_MASCOT=1 -DNJ_ENABLE_TTF=1
      -DNJ_ENABLE_FONT_CJK=0 -DNJ_THREADED_DISPATCH=0)
FLAGS=(--sysroot="$SYSROOT" -std=c99 -Os -fPIC -ffunction-sections -fdata-sections
       -march=armv5te -mtune=xscale -mthumb -mfloat-abi=soft -Wall
       -I"$ROOT/include" -I"$ROOT/src/core" -I"$ROOT/src/midp" -I"$ROOT/src/gfx" -I"$ROOT/src/zip")
SOURCES=("$ROOT"/src/core/*.c "$ROOT"/src/midp/*.c "$ROOT"/src/gfx/*.c "$ROOT"/src/zip/*.c
         "$ROOT/src/runner/nj_run.c" "$ROOT/src/port/android/nj_port_android.c")
OBJS=()
echo '== native: armeabi / ARMv5TE, soft float, API 5 libc, API 5 frontend'
for src in "${SOURCES[@]}"; do
    obj="$W/$(basename "${src%.c}").o"
    "$CC" "${FLAGS[@]}" "${DEFS[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done
SO="$W/lib/armeabi/libnj2me.so"
"$CC" --sysroot="$SYSROOT" -march=armv5te -mthumb -mfloat-abi=soft -shared \
    -Wl,--gc-sections -Wl,--no-undefined -Wl,--hash-style=sysv -Wl,-soname,libnj2me.so \
    -o "$SO" "${OBJS[@]}" -llog -lz -ldl -lm
# Never introduce either API 9 library into the legacy loader's dependencies.
"$READELF" -d "$SO" > "$W/native-dynamic.txt"
if grep -Eq 'lib(android|OpenSLES|aaudio)\.so' "$W/native-dynamic.txt"; then
    echo 'legacy library links an unsupported Android audio/window library' >&2; exit 1
fi
"$READELF" -A "$SO" > "$W/native-attributes.txt"
cp "$SO" "$OUT/libnj2me.so"
if [ "$NATIVE_ONLY" = 1 ]; then
    echo "== ok: $OUT/libnj2me.so"; exit 0
fi
VERSION="$(sed -n 's/^project(nano_j2me VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || VERSION=0.0.0
VERSION_CODE="${VERSION//./}"
echo '== resources and API 5 manifest'
# aapt2 output triggers StringBlock errors in Eclair's package parser.
# aapt's UTF-16 resource format is accepted by both Eclair and Gingerbread.
mkdir -p "$W/gen" "$W/res"
cp -R "$ROOT/src/port/android/legacy/res/." "$W/res/"
# Reuse the modern edition's raster icon; API 5 cannot use adaptive icons.
for density in mdpi hdpi; do
    mkdir -p "$W/res/drawable-$density"
    cp "$ROOT/src/port/android/res/mipmap-$density/ic_launcher.png" \
       "$W/res/drawable-$density/ic_launcher.png"
done
"$BT/aapt" package -f -m --utf16 -M "$ROOT/src/port/android/legacy/AndroidManifest.xml" \
    -I "$JAR" -S "$W/res" -F "$W/base.apk" -J "$W/gen" \
    --min-sdk-version 5 --target-sdk-version 5 --replace-version \
    --version-code "$VERSION_CODE" --version-name "$VERSION-legacy"
echo '== java: compile against Android 2.0 framework'
SOURCES_JAVA=("$ROOT"/src/port/android/legacy/java/com/lucinhu/nj2me/*.java "$W/gen/com/lucinhu/nj2me/R.java")
javac -source 8 -target 8 -encoding UTF-8 -nowarn -Xlint:-options \
    -bootclasspath "$JAR" -classpath "$JAR" -d "$W/classes" "${SOURCES_JAVA[@]}"
mapfile -t CLASSES < <(find "$W/classes" -name '*.class' | sort)
"$BT/d8" --lib "$JAR" --min-api 5 --output "$W/dex" "${CLASSES[@]}"
python3 - "$W" <<'PY'
import os, sys, zipfile
w = sys.argv[1]
with zipfile.ZipFile(os.path.join(w, 'base.apk'), 'a', zipfile.ZIP_DEFLATED) as z:
    z.write(os.path.join(w, 'dex/classes.dex'), 'classes.dex')
    z.write(os.path.join(w, 'lib/armeabi/libnj2me.so'), 'lib/armeabi/libnj2me.so')
PY
# Use the same development certificate as the standard Android build.
KS="$ROOT/build/android/debug.keystore"
mkdir -p "$(dirname "$KS")"
if [ ! -f "$KS" ]; then
    keytool -genkeypair -keystore "$KS" -storepass android -keypass android \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
        -dname 'CN=Android Debug,O=Android,C=US'
fi
"$BT/zipalign" -f 4 "$W/base.apk" "$W/aligned.apk"
# Android 2.x needs JAR/v1 signatures and SHA-1 digests. apksigner selects
# compatible algorithms from --min-sdk-version; v2/v3 are not used here.
"$BT/apksigner" sign --min-sdk-version 5 --v1-signing-enabled true \
    --v2-signing-enabled false --v3-signing-enabled false --v4-signing-enabled false \
    --ks "$KS" --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey \
    --out "$OUT/nj2me-legacy.apk" "$W/aligned.apk"
"$BT/apksigner" verify --min-sdk-version 5 --verbose "$OUT/nj2me-legacy.apk"
echo "== ok: $OUT/nj2me-legacy.apk ($(stat -c%s "$OUT/nj2me-legacy.apk") bytes)"
