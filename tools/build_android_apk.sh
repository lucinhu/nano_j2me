#!/usr/bin/env bash
#
# Build, sign and install a debug APK that runs a MIDlet on Android.
#
#     tools/build_android_apk.sh [--jar FILE] [--abi arm64-v8a] [--install]
#
# Built by hand with aapt2/d8/apksigner rather than Gradle, on purpose: this
# repository has no Gradle wrapper, no dependency graph and nothing to download,
# and the APK it produces is a normal, installable, signed APK.  A real project
# would use Gradle; what this is for is proving the native side is complete -
# a valid manifest, dexed Java, the .so in lib/<abi>/, the game in assets/.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ABI=arm64-v8a
SCREEN=""
JAR="${NJ_ANDROID_JAR:-$ROOT/tests/games/jump.jar}"
INSTALL=0
OUT="$ROOT/build/android"

while [ $# -gt 0 ]; do
    case "$1" in
        --jar) JAR="$2"; shift 2 ;;
        --abi) ABI="$2"; shift 2 ;;
        --screen) SCREEN="$2"; shift 2 ;;
        --install) INSTALL=1; shift ;;
        -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

SDK="${ANDROID_HOME:-$HOME/Android/Sdk}"
BT="$(ls -d "$SDK"/build-tools/*/ 2>/dev/null | sort -V | tail -1)"; BT="${BT%/}"
PLATFORM="$(ls -d "$SDK"/platforms/*/ 2>/dev/null | sort -V | tail -1)"; PLATFORM="${PLATFORM%/}"
JAVA_HOME_DIR="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"

for t in aapt2 d8 zipalign apksigner; do
    [ -x "$BT/$t" ] || { echo "missing $BT/$t" >&2; exit 2; }
done
[ -f "$PLATFORM/android.jar" ] || { echo "no android.jar in $PLATFORM" >&2; exit 2; }
[ -f "$JAR" ] || { echo "no JAR at $JAR (run tools/fetch_test_games.sh)" >&2; exit 2; }

# 1. The native side, for one ABI.
echo "== native ($ABI)"
if [ -n "$SCREEN" ]; then
    "$ROOT/tools/build_android.sh" --abi "$ABI" --out "$OUT" --screen "$SCREEN" || exit 1
else
    "$ROOT/tools/build_android.sh" --abi "$ABI" --out "$OUT" || exit 1
fi
SO="$OUT/$ABI/libnj2me.so"

W="$OUT/apk"; rm -rf "$W"; mkdir -p "$W/classes" "$W/dex" "$W/assets" "$W/lib/$ABI" "$W/gen"
cp "$SO" "$W/lib/$ABI/"
cp "$JAR" "$W/assets/game.jar"

# 2. Java -> .class -> .dex.
echo "== java"
"$JAVA_HOME_DIR/bin/javac" -source 8 -target 8 -nowarn \
    -bootclasspath "$PLATFORM/android.jar" -classpath "$PLATFORM/android.jar" \
    -d "$W/classes" \
    "$ROOT/src/port/android/java/com/example/nj2me/Nj2meActivity.java" || exit 1
"$BT/d8" --lib "$PLATFORM/android.jar" --min-api 21 --output "$W/dex" \
    $(find "$W/classes" -name '*.class') || exit 1

# 3. Manifest + dex + lib + assets -> an unsigned APK.
echo "== package"
"$BT/aapt2" link -o "$W/base.apk" \
    --manifest "$ROOT/src/port/android/manifest/AndroidManifest.xml" \
    -I "$PLATFORM/android.jar" --min-sdk-version 21 --target-sdk-version 34 \
    --version-code 1 --version-name 1.0 || exit 1

# dex/, lib/<abi>/ and assets/ are added with Python's zipfile rather than the
# zip(1) command: zip is not installed everywhere and is not worth a dependency
# for three entries.  aapt2 produced a valid APK; this appends to it.
python3 - "$W" "$ABI" <<'PY' || exit 1
import os, sys, zipfile
w, abi = sys.argv[1], sys.argv[2]
add = [(os.path.join(w, "dex", "classes.dex"), "classes.dex"),
       (os.path.join(w, "lib", abi, "libnj2me.so"), "lib/%s/libnj2me.so" % abi),
       (os.path.join(w, "assets", "game.jar"), "assets/game.jar")]
with zipfile.ZipFile(os.path.join(w, "base.apk"), "a", zipfile.ZIP_DEFLATED) as z:
    for src, dst in add:
        if not os.path.exists(src):
            print("missing %s" % src); sys.exit(1)
        z.write(src, dst)
print("  added %d entries" % len(add))
PY

# 4. Align, sign.
KS="$OUT/debug.keystore"
if [ ! -f "$KS" ]; then
    "$JAVA_HOME_DIR/bin/keytool" -genkeypair -keystore "$KS" -storepass android \
        -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 \
        -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi
"$BT/zipalign" -f 4 "$W/base.apk" "$W/aligned.apk" || exit 1
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
    --ks-key-alias androiddebugkey --out "$OUT/nj2me.apk" "$W/aligned.apk" || exit 1
"$BT/apksigner" verify "$OUT/nj2me.apk" >/dev/null 2>&1 || { echo "signature did not verify" >&2; exit 1; }

echo "== ok"
printf '  %s  (%s bytes, %s, %s)\n' "$OUT/nj2me.apk" "$(stat -c%s "$OUT/nj2me.apk")" "$ABI" "$(basename "$JAR")"

if [ "$INSTALL" -eq 1 ]; then
    "$SDK/platform-tools/adb" install -r "$OUT/nj2me.apk"
    "$SDK/platform-tools/adb" shell am start -n com.example.nj2me/.Nj2meActivity
fi
