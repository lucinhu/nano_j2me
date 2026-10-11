#!/usr/bin/env bash
#
# Build libnj2me.so with the Android NDK, for every ABI the SDK ships.
#
#     tools/build_android.sh [--abi arm64-v8a] [--api 21] [--out DIR]
#
# This is a REAL cross-build with the real NDK toolchain, not a stub-header
# compile: nj_port_android.c includes <jni.h>, exports the JNI entry points
# Java_com_example_nj2me_Nj2meActivity_native*, and links against liblog and
# libandroid.  What it does not do is run on a device or an emulator - see
# docs/PORTING.md for what that would add.
#
# The NDK is found under ~/Android/Sdk/ndk; set ANDROID_NDK_HOME to override.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
API=21
OUT="${NJ_ANDROID_OUT:-$ROOT/build/android}"
ABIS="arm64-v8a armeabi-v7a x86_64"

while [ $# -gt 0 ]; do
    case "$1" in
        --abi)  ABIS="$2"; shift 2 ;;
        --api)  API="$2"; shift 2 ;;
        --out)  OUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

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

fail=0
for abi in $ABIS; do
    case "$abi" in
        arm64-v8a)   triple=aarch64-linux-android ;;
        armeabi-v7a) triple=armv7a-linux-androideabi ;;
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
                -Wall -I"$ROOT/include" -I"$ROOT/src/core" -I"$ROOT/src/midp" \
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

    # -shared, and the two system libraries the port actually calls.
    # --no-undefined is not optional here.  A shared library is allowed to have
    # unresolved symbols, so a missing source file produces a .so that builds,
    # signs, installs and then fails at dlopen with UnsatisfiedLinkError on the
    # device - which is exactly what happened with src/runner/nj_run.c.
    if ! "$cc" -shared -Wl,--gc-sections -Wl,--no-undefined \
            -Wl,-soname,libnj2me.so \
            -o "$dir/libnj2me.so" $objs -llog -landroid -lm 2>"$dir/err"; then
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
