#!/usr/bin/env bash
#
# Enforce that the per-codec video switches really cut the decoder out.
#
# NJ_ENABLE_VIDEO covers both video decoders, but a port that carries only one
# of them sets NJ_ENABLE_H263=0 or NJ_ENABLE_M4V=0.  That promise is only worth
# making if the object file actually disappears: before this check existed the
# M4V translation unit kept its tables and helpers (~23 KB) while only the
# provider table was guarded, so -DNJ_ENABLE_M4V=0 saved nothing.
#
# The check compiles each unit twice with the flag on and off, and asks the
# object size: on must be non-empty, off must be empty and must not export the
# provider symbol.
#
# Usage:
#   tools/check_codec_trim.sh [CC]
#
set -u

CC=${1:-cc}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

work=${TMPDIR:-/tmp}/nj-codec-trim.$$
trap 'rm -rf "$work"' EXIT
mkdir -p "$work" || exit 1

defs="-DNJ_ENABLE_VIDEO=1 -DNJ_ENABLE_MP4=1 -DNJ_ENABLE_MP3=1 -DNJ_ENABLE_AMR=1 \
      -DNJ_ENABLE_MIDI=1 -DNJ_ENABLE_MEMORY_HEAP=1"
incs="-Iinclude -Isrc/core -Isrc/midp"

text_of() {   # $1 = object
    size -A "$1" 2>/dev/null | awk '/^\.text[[:space:]]/{t=$2} END{print t+0}'
}

fail=0
check() {     # $1 = macro, $2 = source, $3 = symbol
    macro=$1; src=$2; sym=$3
    base=$(basename "$src" .c)
    on="$work/${base}.on.o"; off="$work/${base}.off.o"

    # shellcheck disable=SC2086
    if ! $CC -Os -std=c99 -c "$src" -o "$on" $defs "-D$macro=1" $incs 2>"$work/on.log"; then
        echo "FAIL: $src does not compile with $macro=1"
        sed -n '1,5p' "$work/on.log"
        fail=1; return
    fi
    # shellcheck disable=SC2086
    if ! $CC -Os -std=c99 -c "$src" -o "$off" $defs "-D$macro=0" $incs 2>"$work/off.log"; then
        echo "FAIL: $src does not compile with $macro=0"
        sed -n '1,5p' "$work/off.log"
        fail=1; return
    fi

    on_text=$(text_of "$on"); off_text=$(text_of "$off")
    if [ "$on_text" -lt 1000 ]; then
        echo "FAIL: $src is only $on_text bytes with $macro=1 (expected the decoder)"
        fail=1
    fi
    if [ "$off_text" -ne 0 ]; then
        echo "FAIL: $macro=0 leaves $off_text bytes of .text in $src"
        fail=1
    fi
    if [ -n "$(nm --defined-only "$off" 2>/dev/null | grep " $sym\$")" ]; then
        echo "FAIL: $macro=0 still defines $sym"
        fail=1
    fi
    if [ -z "$(nm --defined-only "$on" 2>/dev/null | grep " $sym\$")" ]; then
        echo "FAIL: $macro=1 does not define $sym"
        fail=1
    fi
    echo "ok: $macro=1 -> $on_text bytes of .text, $macro=0 -> 0 (symbol $sym cut)"
}

check NJ_ENABLE_H263 src/midp/nj_h263.c nj_av_video_h263
check NJ_ENABLE_M4V  src/midp/nj_m4v.c  nj_av_video_mpeg4

# The historical alias must still mean "H.263 on, MPEG-4 off".
legacy="$work/nj_m4v.legacy.o"
# shellcheck disable=SC2086
if ! $CC -Os -std=c99 -c src/midp/nj_m4v.c -o "$legacy" $defs -DNJ_VIDEO_H263_ONLY=1 "-DNJ_ENABLE_H263=1" $incs 2>"$work/legacy.log"; then
    echo "FAIL: the NJ_VIDEO_H263_ONLY alias does not compile"
    sed -n '1,5p' "$work/legacy.log"
    fail=1
elif [ "$(text_of "$legacy")" -ne 0 ]; then
    echo "FAIL: NJ_VIDEO_H263_ONLY=1 still builds the MPEG-4 decoder"
    fail=1
else
    echo "ok: NJ_VIDEO_H263_ONLY=1 keeps H.263 and cuts MPEG-4"
fi

if [ "$fail" -ne 0 ]; then
    echo "codec trims are broken"
    exit 1
fi
echo "OK: both video codecs can be cut independently"
