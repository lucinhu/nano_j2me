#!/usr/bin/env bash
#
# Build, sign and install a debug APK of the Android port.
#
#     tools/build_android_apk.sh [--abi arm64-v8a] [--screen 240x320]
#                                [--opt 0|1|2|3|s|g|z] [--install]
#                                [--push FILE.jar] [--no-midi]
#                                [--no-png] [--no-jpeg] [--no-gif]
#                                [--no-inflate] [--no-wav] [--no-amr]
#
# --opt is the native optimisation level, forwarded to tools/build_android.sh.
# Both scripts default to -Os; see docs/notes/2026-10-30-android-optimisation-
# level.md for the measurements behind that - the knob is for experiments, not
# a speed switch.
#
# --no-midi drops the built-in MIDI renderer from the .so (~13 KB per ABI; the
# platform's own synth still plays .mid, and MIDIControl stops working - see
# the comment in tools/build_android.sh).
#
# Built by hand with aapt2/d8/apksigner rather than Gradle, on purpose: this
# repository has no Gradle wrapper, no dependency graph and nothing to download,
# and the APK this produces is a normal, installable, signed APK.  A real
# project would use Gradle; what this is for is proving the port is complete -
# a valid manifest, linked resources, dexed Java, the .so in lib/<abi>/, an
# activity that picks a .jar and an overlay keypad that can be rearranged.
#
# No game is bundled.  The launcher reads jars from the app's own storage
# (files/jars) and from its external storage
# (/sdcard/Android/data/<package>/files/jars), which is the directory a user can
# fill with a cable or one adb push - see --push below.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG=com.lucinhu.nj2me
# The version comes from CMakeLists.txt, so an APK can never claim a version
# the release does not have.  versionCode stays 1 until there is a second one.
VERSION="$(sed -n 's/^project(nano_j2me VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || VERSION=0.0.0
VERSION_CODE="${VERSION//./}"
ABIS="arm64-v8a armeabi-v7a x86 x86_64"
SCREEN=""
INSTALL=0
PUSH=""
OUT="$ROOT/build/android"
NO_MIDI=0
NO_CODECS=""   # --no-<codec> flags, forwarded to tools/build_android.sh
OPT_FLAG=""    # --opt LEVEL, forwarded to tools/build_android.sh

while [ $# -gt 0 ]; do
    case "$1" in
        --abi) ABIS="$2"; shift 2 ;;
        --screen) SCREEN="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --opt) OPT_FLAG="--opt $2"; shift 2 ;;
        --install) INSTALL=1; shift ;;
        --push) PUSH="$2"; shift 2 ;;
        --no-midi) NO_MIDI=1; shift ;;
        --no-png|--no-jpeg|--no-gif|--no-inflate|--no-wav|--no-amr)
            NO_CODECS="$NO_CODECS $1"; shift ;;
        -h|--help) sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

SDK="${ANDROID_HOME:-$HOME/Android/Sdk}"
BT="$(ls -d "$SDK"/build-tools/*/ 2>/dev/null | sort -V | tail -1)"; BT="${BT%/}"
PLATFORM="$(ls -d "$SDK"/platforms/*/ 2>/dev/null | sort -V | tail -1)"; PLATFORM="${PLATFORM%/}"
JAVA_HOME_DIR="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
ADB="$SDK/platform-tools/adb"

for t in aapt2 d8 zipalign apksigner; do
    [ -x "$BT/$t" ] || { echo "missing $BT/$t" >&2; exit 2; }
done
[ -f "$PLATFORM/android.jar" ] || { echo "no android.jar in $PLATFORM" >&2; exit 2; }
[ -f "$ROOT/src/port/android/manifest/AndroidManifest.xml" ] || { echo "no manifest" >&2; exit 2; }

if [ -n "$PUSH" ]; then
    [ -f "$PUSH" ] || { echo "no jar at $PUSH" >&2; exit 2; }
    # mkdir first: the directory under Android/data belongs to the app and is
    # only created once the app has run, but the shell user may create it too,
    # which is what makes one-command testing possible.  A directory created
    # that way is owned by root/shell with mode 2770, and the app then sees an
    # EMPTY library - the jar is there but unreadable.  chmod it open; the app
    # is the only reader, and only its own directory is touched.
    DIR="/sdcard/Android/data/$PKG/files/jars"
    "$ADB" shell mkdir -p "$DIR" || exit 1
    "$ADB" shell chmod 0777 "$DIR" >/dev/null 2>&1 \
        || echo "note: could not chmod $DIR - if the library looks empty, launch the app once and push again" >&2
    "$ADB" push "$PUSH" "$DIR/" || exit 1
fi

# 1. The native side, once per ABI.
W="$OUT/apk"; rm -rf "$W"; mkdir -p "$W/classes" "$W/dex" "$W/assets" "$W/gen" "$W/res"
for abi in $ABIS; do
    echo "== native ($abi)"
    MIDI_FLAG=""
    [ "$NO_MIDI" = "1" ] && MIDI_FLAG="--no-midi"
    MIDI_FLAG="$MIDI_FLAG $NO_CODECS $OPT_FLAG"
    if [ -n "$SCREEN" ]; then
        "$ROOT/tools/build_android.sh" --abi "$abi" --out "$OUT" --screen "$SCREEN" $MIDI_FLAG || exit 1
    else
        "$ROOT/tools/build_android.sh" --abi "$abi" --out "$OUT" $MIDI_FLAG || exit 1
    fi
    mkdir -p "$W/lib/$abi"
    cp "$OUT/$abi/libnj2me.so" "$W/lib/$abi/"
done

# 2. Java -> .class -> .dex.  Compiled against android.jar as the boot class
#    path rather than against the host JDK's, so the API level is the one the
#    manifest asks for; -source/-target 8 keeps d8 and old tooling happy.
echo "== java"
find "$ROOT/src/port/android/java" -name '*.java' > "$W/sources.txt"
"$JAVA_HOME_DIR/bin/javac" -source 8 -target 8 -nowarn -Xlint:-options \
    -bootclasspath "$PLATFORM/android.jar" -classpath "$PLATFORM/android.jar" \
    -d "$W/classes" @"$W/sources.txt" || exit 1
"$BT/d8" --lib "$PLATFORM/android.jar" --min-api 21 --output "$W/dex" \
    $(find "$W/classes" -name '*.class') || exit 1

# 3. Resources, then manifest + resources -> an unsigned APK.
echo "== resources"
"$BT/aapt2" compile --dir "$ROOT/src/port/android/res" -o "$W/res.zip" || exit 1
# --auto-add-overlay: aapt2 treats every -R input as an overlay of the base
# table, and with no base table there is nothing for these resources to
# override - without the flag it refuses them as redundant.
"$BT/aapt2" link -o "$W/base.apk" \
    --manifest "$ROOT/src/port/android/manifest/AndroidManifest.xml" \
    -I "$PLATFORM/android.jar" -R "$W/res.zip" --auto-add-overlay \
    --min-sdk-version 21 --target-sdk-version 34 \
    --version-code "$VERSION_CODE" --version-name "$VERSION" --no-version-vectors || exit 1

# dex/, lib/<abi>/ and assets/ are added with Python's zipfile rather than the
# zip(1) command: zip is not installed everywhere and is not worth a dependency
# for a handful of entries.  aapt2 produced a valid APK; this appends to it.
python3 - "$W" "$ABIS" <<'PY' || exit 1
import os, sys, zipfile
w, abis = sys.argv[1], sys.argv[2].split()
add = [(os.path.join(w, "dex", "classes.dex"), "classes.dex")]
for abi in abis:
    add.append((os.path.join(w, "lib", abi, "libnj2me.so"), "lib/%s/libnj2me.so" % abi))
with zipfile.ZipFile(os.path.join(w, "base.apk"), "a", zipfile.ZIP_DEFLATED) as z:
    for src, dst in add:
        if not os.path.exists(src):
            print("missing %s" % src); sys.exit(1)
        z.write(src, dst)
print("  added %d entries" % len(add))
PY

# 4. Align, sign.
# One keystore for every build: a per-OUT key would make a rebuilt APK
# impossible to install over the previous one (INSTALL_PARSE_FAILED_
# INCONSISTENT_CERTIFICATES). The default lives under build/, so an --out
# elsewhere still signs with the same debug key.
KS="$ROOT/build/android/debug.keystore"
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
printf '  %s  (%s bytes, %s)\n' "$OUT/nj2me.apk" "$(stat -c%s "$OUT/nj2me.apk")" "$ABIS"

if [ "$INSTALL" -eq 1 ]; then
    "$ADB" install -r "$OUT/nj2me.apk" || exit 1
    # The launch screen, not a game: which game to run is the user's choice in
    # the UI, and that is the point of this port.
    "$ADB" shell am start -n "$PKG/.LibraryActivity"
fi
