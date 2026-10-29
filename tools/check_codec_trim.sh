#!/usr/bin/env bash
#
# Enforce that the decoder trim switches really cut code out.
#
# Every decoder feature in nj_config.h is documented as a switch a port can
# turn off, so each one has to remove bytes - not just change a runtime test.
# Two of them used to lie: NJ_ENABLE_M4V only guarded the provider table (the
# object kept ~23 KB of tables), and NJ_MP3_LAYER3_ONLY was never read at all.
# This check compiles the affected units with each switch on and off and reads
# the object, so a switch that stops trimming fails the build instead of
# quietly costing ROM.
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

defs="-DNJ_ENABLE_H263=1 -DNJ_ENABLE_M4V=1 -DNJ_ENABLE_MP3=1 -DNJ_ENABLE_AMR=1"
incs="-Iinclude -Isrc/core -Isrc/midp"

text_of() {   # $1 = object
    size -A "$1" 2>/dev/null | awk '/^\.text[[:space:]]/{t=$2} END{print t+0}'
}

fail=0

# The base -D set is dropped for any macro the caller names itself: passing
# -DNJ_ENABLE_MP3=1 and -DNJ_ENABLE_MP3=0 to one command is a redefinition,
# which -Werror (correctly) refuses - and with it silently accepted, the "off"
# variant was never really compiled.
compile() {   # $1 out, $2 src, rest: extra flags
    local out src kept d macro skip a
    out=$1; src=$2; shift 2
    kept=""
    for d in $defs; do
        macro=${d#-D}
        macro=${macro%%=*}
        skip=0
        for a in "$@"; do
            case "$a" in -D"$macro"=*) skip=1 ;; esac
        done
        if [ "$skip" -eq 0 ]; then
            kept="$kept $d"
        fi
    done
    # shellcheck disable=SC2086
    $CC -Os -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter \
        -c "$src" -o "$out" $kept $incs "$@" 2>"$out.log"
}

has_symbol() { # $1 object, $2 symbol
    [ -n "$(nm --defined-only "$1" 2>/dev/null | grep " $2\$")" ]
}

# A switch that removes a whole translation unit: text must be present with 1
# and empty with 0, and the provider symbol must come and go with it.
check_unit() {   # $1 macro, $2 source, $3 provider symbol ("" if none)
    macro=$1; src=$2; sym=$3
    base=$(basename "$src" .c)
    on="$work/${base}.on.o"; off="$work/${base}.off.o"

    if ! compile "$on" "$src" "-D$macro=1"; then
        echo "FAIL: $src does not compile with $macro=1"; sed -n '1,5p' "$on.log"; fail=1; return
    fi
    if ! compile "$off" "$src" "-D$macro=0"; then
        echo "FAIL: $src does not compile with $macro=0"; sed -n '1,5p' "$off.log"; fail=1; return
    fi
    on_text=$(text_of "$on"); off_text=$(text_of "$off")
    if [ "$on_text" -lt 1000 ]; then
        echo "FAIL: $src is only $on_text bytes with $macro=1 (expected the decoder)"; fail=1
    fi
    if [ "$off_text" -ne 0 ]; then
        echo "FAIL: $macro=0 leaves $off_text bytes of .text in $src"; fail=1
    fi
    if [ -n "$sym" ]; then
        if has_symbol "$off" "$sym"; then echo "FAIL: $macro=0 still defines $sym"; fail=1; fi
        if ! has_symbol "$on" "$sym"; then echo "FAIL: $macro=1 does not define $sym"; fail=1; fi
    fi
    echo "ok: $macro=1 -> $on_text bytes of .text, $macro=0 -> 0"
}

# A sub-feature switch that removes part of a unit: both states must compile
# and the "off" state must be measurably smaller.
check_shrink() { # $1 macro, $2 source
    macro=$1; src=$2
    base=$(basename "$src" .c)
    on="$work/${base}.${macro}.on.o"; off="$work/${base}.${macro}.off.o"

    if ! compile "$on" "$src" "-D$macro=1"; then
        echo "FAIL: $src does not compile with $macro=1"; sed -n '1,5p' "$on.log"; fail=1; return
    fi
    if ! compile "$off" "$src" "-D$macro=0"; then
        echo "FAIL: $src does not compile with $macro=0"; sed -n '1,5p' "$off.log"; fail=1; return
    fi
    on_text=$(text_of "$on"); off_text=$(text_of "$off")
    if [ "$on_text" -le "$off_text" ]; then
        echo "FAIL: $macro=0 does not shrink $src ($on_text -> $off_text bytes)"; fail=1
    else
        echo "ok: $macro=0 saves $((on_text - off_text)) bytes in $src"
    fi
}

# A unit that must at least keep compiling with a feature off.
check_compiles() { # $1 source, rest: extra flags
    src=$1; shift
    tag=$(printf '%s' "$*" | tr -c 'A-Za-z0-9' '_')
    out="$work/$(basename "$src" .c).compile${tag}.o"
    if compile "$out" "$src" "$@"; then
        echo "ok: $src compiles with $*"
    else
        echo "FAIL: $src does not compile with $*"; sed -n '1,5p' "$out.log"; fail=1
    fi
}

check_unit NJ_ENABLE_H263 src/midp/nj_h263.c nj_av_video_h263
check_unit NJ_ENABLE_M4V  src/midp/nj_m4v.c  nj_av_video_mpeg4
check_unit NJ_ENABLE_MP3  src/midp/nj_mp3.c  nj_av_audio_mp3
check_unit NJ_ENABLE_AMR  src/midp/nj_amr.c  ""

# The Player glue has to keep compiling when decoders are switched off; the
# AMR-less build used to fail on NJ_AMR_RATE, which only came in through
# nj_amr.h and that header was included behind NJ_ENABLE_AMR.
check_compiles src/midp/nj_lib_media.c -DNJ_ENABLE_AMR=0
check_compiles src/midp/nj_lib_media.c -DNJ_ENABLE_MP3=0
check_compiles src/midp/nj_lib_media.c -DNJ_ENABLE_VIDEO=0
check_compiles src/midp/nj_lib_media.c \
               -DNJ_ENABLE_AMR=0 -DNJ_ENABLE_MP3=0 -DNJ_ENABLE_VIDEO=0

# A decoder that is compiled out leaves its headers unused too: the IDCT and
# the intra VLC tables are static data, so a -Wall -Wextra -Werror build names
# them as dead code unless the include sits inside the feature guard.
check_compiles src/midp/nj_h263.c -DNJ_ENABLE_H263=0
check_compiles src/midp/nj_m4v.c  -DNJ_ENABLE_M4V=0

# Mascot Capsule's own units, in their normal and compiled-out shapes.  The
# default shape is the one that caught an operator-precedence defect in
# AffineTrans.setRotation (the shift divided the cosine as well as the
# product), so the check is both a build gate and a regression for it.
check_compiles src/midp/nj_lib_mascot.c
check_compiles src/midp/nj_lib_mascot.c -DNJ_ENABLE_MASCOT=0

check_shrink NJ_MP3_ENABLE_ID3       src/midp/nj_mp3.c
check_shrink NJ_MP3_ENABLE_SEEK      src/midp/nj_mp3.c
check_shrink NJ_MP3_ENABLE_INTENSITY src/midp/nj_mp3.c

# The historical alias must still mean "H.263 on, MPEG-4 off".
legacy="$work/nj_m4v.legacy.o"
legacy_defs=$(printf '%s' "$defs" | sed 's/-DNJ_ENABLE_M4V=1//')
# shellcheck disable=SC2086
if ! $CC -Os -std=c99 -c src/midp/nj_m4v.c -o "$legacy" $legacy_defs $incs \
        -DNJ_ENABLE_H263=1 -DNJ_VIDEO_H263_ONLY=1 2>"$legacy.log"; then
    echo "FAIL: the NJ_VIDEO_H263_ONLY alias does not compile"
    sed -n '1,5p' "$legacy.log"; fail=1
elif [ "$(text_of "$legacy")" -ne 0 ]; then
    echo "FAIL: NJ_VIDEO_H263_ONLY=1 still builds the MPEG-4 decoder"; fail=1
else
    echo "ok: NJ_VIDEO_H263_ONLY=1 keeps H.263 and cuts MPEG-4"
fi

if [ "$fail" -ne 0 ]; then
    echo "codec trims are broken"
    exit 1
fi
echo "OK: every decoder trim removes the code it claims to"
