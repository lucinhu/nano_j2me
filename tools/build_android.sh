#!/usr/bin/env bash
#
# Build libnj2me.so with the Android NDK, for every ABI the SDK ships.
#
#     tools/build_android.sh [--abi arm64-v8a] [--api 21] [--out DIR]
#
# This is a REAL cross-build with the real NDK toolchain, not a stub-header
# compile: nj_port_android.c includes <jni.h>, exports the JNI entry points
# Java_com_example_nj2me_GameActivity_native*, and links against liblog,
# libandroid and libOpenSLES.  What it does not do is run on a device or an
# emulator - see docs/PORTING.md for how to do that end to end.
#
# API 21 is the floor: it is the oldest level the NDK still ships a compiler
# for.  AAudio is NOT linked - it is dlopen()ed at runtime and used when the
# phone has it (API 26+), with OpenSL ES driving the same mixer on anything
# older, so one APK covers both.
#
# The NDK is found under ~/Android/Sdk/ndk; set ANDROID_NDK_HOME to override.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
API=21
OUT="${NJ_ANDROID_OUT:-$ROOT/build/android}"
ABIS="arm64-v8a armeabi-v7a x86 x86_64"
SCREEN=""     # e.g. 176x208; empty leaves the runtime's default (240x320)

while [ $# -gt 0 ]; do
    case "$1" in
        --abi)  ABIS="$2"; shift 2 ;;
        --api)  API="$2"; shift 2 ;;
        --out)  OUT="$2"; shift 2 ;;
        --screen) SCREEN="$2"; shift 2 ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# Refused rather than built: the NDK has no compiler for a level below 21, and
# the failure it produces reads like a broken toolchain instead of a floor.
if [ "$API" -lt 21 ]; then
    echo "API $API is below this port's floor (21): the NDK ships no compiler" >&2
    echo "for an older level, and nothing in this port needs one." >&2
    exit 2
fi

NDK="${ANDROID_NDK_HOME:-}"
if [ -z "$NDK" ]; then
    NDK="$(ls -d "$HOME"/Android/Sdk/ndk/*/ 2>/dev/null | sort -V | tail -1)"
fi
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "no Android NDK found (set ANDROID_NDK_HOME)" >&2
    exit 2
fi
NDK="${NDK%/}"
BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"

# The Android port is a port like any other: it fills in a caller-owned nj_port
# struct and adds no globals, so the same zero-static-state gate applies.
SOURCES="$(cd "$ROOT" && ls src/core/*.c src/midp/*.c src/gfx/*.c src/zip/*.c)"
# The runner too: the JNI entry points call nj_run(), and leaving it out links
# happily - a shared library may have undefined symbols by default - and then
# fails to dlopen on the device with UnsatisfiedLinkError.  --no-undefined below
# is what turns that runtime crash into a link error.
SOURCES="$SOURCES src/runner/nj_run.c src/port/android/nj_port_android.c"

echo "NDK  $NDK"
echo "API  $API"
[ -n "$SCREEN" ] && echo "MIDlet screen $SCREEN"

# The MIDlet's logical screen.  A title that gates on the screen size - "Screen
# too large" - needs one it will accept, and the Surface is scaled to fit, so
# this is a build-time choice rather than a hardware constraint.
SCREEN_DEFS=""
if [ -n "$SCREEN" ]; then
    SCREEN_DEFS="-DNJ_SCREEN_WIDTH=${SCREEN%x*} -DNJ_SCREEN_HEIGHT=${SCREEN#*x}"
fi

# The feature switches, spelled out exactly as CMakeLists.txt passes them.
#
# This is not decoration.  nj_config.h nests its NJ_THREADED_DISPATCH default
# INSIDE the `#ifndef NJ_PIXFMT` block (include/nj2me/nj_config.h:268-286), so a
# build that does not define NJ_PIXFMT - which is what this script used to do -
# silently gets NJ_THREADED_DISPATCH 1, the computed-goto interpreter, while
# every desktop build gets 0 and the portable switch.  Two builds of the same
# sources then disagree about how the interpreter is written, and the threaded
# path is the one with a live miscompile: tests/fixtures/keysdemo.jar throws
#
#     java.lang.VerifyError at KeysDemo$Pad.<init>()V pc=14:
#     receiver is not an object reference
#
# under the threaded dispatch and runs perfectly under the switch.  Defining
# NJ_PIXFMT here is therefore what makes the .so behave like the runtime the
# corpus, the tests and docs/PERFORMANCE.md were measured on.
#
# See docs/notes/OPEN-issue-threaded-dispatch.md.
#
# NJ_ENABLE_FONT_CJK=0 is the other deliberate departure from a desktop build.
# The built-in wide-glyph table is 7445 GB2312 characters at 12x12 and 16x16 -
# about 480 KB of rodata per ABI, more than half of libnj2me.so and 1.4 MB
# across the three ABIs in the APK.  Android has those glyphs already, so
# src/port/android/nj_port_android.c installs a fallback face per slot and
# Java (GlyphSource) rasterises each character on demand from the system font.
# Building with the table still in place would ship both copies and never use
# the provider: a fallback is only consulted for what the built-in face cannot
# draw.
FEATURE_DEFS="-DNJ_PIXFMT=NJ_PIXFMT_RGB565 \
-DNJ_ENABLE_PNG=1 -DNJ_ENABLE_JPEG=1 -DNJ_ENABLE_RMS=1 -DNJ_ENABLE_IO=1 \
-DNJ_ENABLE_FLOAT=1 -DNJ_ENABLE_M3G=1 -DNJ_ENABLE_MASCOT=1 -DNJ_ENABLE_TTF=1 \
-DNJ_ENABLE_FONT_CJK=0"

fail=0
for abi in $ABIS; do
    case "$abi" in
        arm64-v8a)   triple=aarch64-linux-android ;;
        armeabi-v7a) triple=armv7a-linux-androideabi ;;
        x86)         triple=i686-linux-android ;;
        x86_64)      triple=x86_64-linux-android ;;
        *) echo "unknown ABI: $abi" >&2; fail=1; continue ;;
    esac
    cc="$BIN/${triple}${API}-clang"
    if [ ! -x "$cc" ]; then
        echo "  $abi: no compiler at $cc"
        fail=1
        continue
    fi

    dir="$OUT/$abi"
    mkdir -p "$dir"
    objs=""
    built=1
    for f in $SOURCES; do
        o="$dir/$(echo "$f" | tr / _).o"
        if ! "$cc" -std=c99 -Os -fPIC -ffunction-sections -fdata-sections \
                -Wall $SCREEN_DEFS $FEATURE_DEFS -I"$ROOT/include" -I"$ROOT/src/core" -I"$ROOT/src/midp" \
                -I"$ROOT/src/gfx" -I"$ROOT/src/zip" \
                -c "$ROOT/$f" -o "$o" 2>"$dir/err"; then
            echo "  $abi: compile failed in $f"
            head -3 "$dir/err"
            built=0
            fail=1
            break
        fi
        objs="$objs $o"
    done
    [ "$built" -eq 1 ] || continue

    # -shared, and the three system libraries the port actually calls: log for
    # __android_log_write, android for ANativeWindow, and OpenSLES for the
    # mixer on phones that predate AAudio (libaaudio.so is dlopen()ed, not
    # linked: a hard link would make the loader refuse this library entirely on
    # a device that does not have it).
    # --no-undefined is not optional here.  A shared library is allowed to have
    # unresolved symbols, so a missing source file produces a .so that builds,
    # signs, installs and then fails at dlopen with UnsatisfiedLinkError on the
    # device - which is exactly what happened with src/runner/nj_run.c.
    if ! "$cc" -shared -Wl,--gc-sections -Wl,--no-undefined \
            -Wl,-soname,libnj2me.so \
            -o "$dir/libnj2me.so" $objs -llog -landroid -lOpenSLES -lm 2>"$dir/err"; then
        echo "  $abi: link failed"
        head -5 "$dir/err"
        fail=1
        continue
    fi

    # The gate that matters, applied the same way tools/check_no_statics.sh
    # applies it on the desktop: BY SECTION NAME, not by nm's letter.
    #
    # A const array that contains pointers needs a relocation, and in a shared
    # library it lands in .data.rel.ro - which nm reports as 'd' and which is
    # read-only after the dynamic linker has finished, RELRO being the norm on
    # Android.  Judging by the letter flags every builtin class table in the
    # runtime as mutable state, which is the false positive the desktop gate was
    # written to avoid.
    RE="${BIN}/llvm-readelf"
    [ -x "$RE" ] || RE=readelf
    sz=$("$RE" -S "$dir/libnj2me.so" 2>/dev/null | awk '
            $2 == ".data" || $2 == ".bss" { n += strtonum("0x" $6) }
            $2 == ".data.rel.ro"           { r += strtonum("0x" $6) }
            END { printf "%d %d", n, r }')
    n=${sz% *}; relro=${sz#* }
    if [ "${n:-0}" -ne 0 ]; then
        echo "  $abi: WRITABLE STATIC STATE: $n bytes in .data/.bss"
        fail=1
    fi

    printf '  %-12s %9s bytes .so   data+bss=0   .data.rel.ro=%s (read-only after relocation)\n' \
           "$abi" "$(stat -c%s "$dir/libnj2me.so")" "${relro:-0}"
    rm -f "$dir"/*.o "$dir/err"
done

exit $fail
