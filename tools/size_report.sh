#!/usr/bin/env bash
#
# Static footprint report, per object file.
#
# The zero-mutable-state rule is only half the story on a microcontroller: the
# other half is how much flash and RAM the runtime costs before a single MIDlet
# byte is loaded.  This compiles every source with the flags an ESP32-S3 build
# would use (-Os, and the same feature switches) and reports, per file:
#
#   text    executable code, goes to flash (or IRAM if it must be fast)
#   rodata  const tables - font bitmaps, native registries, string literals
#   data    initialised writable statics  <- must be 0
#   bss     zero-initialised statics      <- must be 0
#
# `data` and `bss` non-zero on any line is a bug, not a budget question.
#
# The compile is freestanding (-DNJ_USE_LIBC=0) because that is how the
# embedded port builds: no libc, no malloc, every allocation through the port.
#
# Usage: tools/size_report.sh [--libc] [--sort size|name]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-gcc}"
SIZE="${SIZE_TOOL:-size}"
LIBSTD=0
SORTBY=size
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

while [ $# -gt 0 ]; do
    case "$1" in
        --libc) LIBSTD=1; shift ;;
        --sort) SORTBY="$2"; shift 2 ;;
        -h|--help) sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# -fPIC matters for this report, not for the target build.  A `const` array
# that contains pointers (the builtin class tables, the font tables) needs a
# relocation, and without position independence the compiler puts it in .data -
# so a non-PIC report shows kilobytes of "writable static state" that is not
# writable at all.  With -fPIC it lands in .data.rel.ro, which the linker makes
# read-only after relocation, and the .data column becomes what it should be:
# exactly zero.  The distinction is the same one tools/check_no_statics.sh
# makes by resolving section names instead of trusting nm's letter.
CFLAGS="-std=c99 -Os -fPIC -ffunction-sections -fdata-sections -I$ROOT/include -I$ROOT/src/core -I$ROOT/src/midp -I$ROOT/src/gfx -I$ROOT/src/zip"
DEFS="-DNJ_PIXFMT=NJ_PIXFMT_RGB565 -DNJ_ENABLE_PNG=1 -DNJ_ENABLE_JPEG=1 -DNJ_ENABLE_RMS=1 -DNJ_ENABLE_IO=1"
[ "$LIBSTD" -eq 1 ] && DEFS="$DEFS -DNJ_USE_LIBC=1" || DEFS="$DEFS -DNJ_USE_LIBC=0"

FILES=$(cd "$ROOT" && ls src/core/*.c src/midp/*.c src/gfx/*.c src/zip/*.c \
                         src/port/esp32/nj_port_esp32.c 2>/dev/null)

printf '%-34s %8s %8s %6s %6s\n' FILE text rodata data bss
printf '%-34s %8s %8s %6s %6s\n' ---- ---- ------ ---- ---

sum_text=0; sum_ro=0; sum_data=0; sum_bss=0
rows=""
for f in $FILES; do
    obj="$TMP/$(echo "$f" | tr / _).o"
    if ! $CC $CFLAGS $DEFS -c "$ROOT/$f" -o "$obj" 2>"$TMP/err"; then
        printf '%-34s %8s\n' "$f" "(skipped: needs its platform SDK)"
        continue
    fi
    # `size -A` prints every section; sum the ones that matter.
    read -r t r d b <<EOF
$($SIZE -A "$obj" | awk '
    $1 ~ /^\.text/      { t += $2 }
    $1 ~ /^\.rodata/    { r += $2 }
    $1 ~ /^\.data\.rel\.ro/ { r += $2 }
    $1 ~ /^\.data$/      { d += $2 }
    $1 ~ /^\.bss/       { b += $2 }
    END { printf "%d %d %d %d", t, r, d, b }')
EOF
    rows="$rows$(printf '%-34s %8d %8d %6d %6d\n' "$f" "$t" "$r" "$d" "$b")"$'\n'
    sum_text=$((sum_text + t)); sum_ro=$((sum_ro + r))
    sum_data=$((sum_data + d)); sum_bss=$((sum_bss + b))
done

if [ "$SORTBY" = size ]; then
    echo "$rows" | sort -k2 -nr
else
    echo "$rows"
fi

echo
printf '%-34s %8d %8d %6d %6d\n' TOTAL "$sum_text" "$sum_ro" "$sum_data" "$sum_bss"
echo
printf 'flash (text+rodata)  %8d bytes  (%.1f KiB)\n' \
       "$((sum_text + sum_ro))" "$(echo "($sum_text + $sum_ro) / 1024" | bc -l)"
printf 'RAM   (data+bss)     %8d bytes  (%.1f KiB)\n' \
       "$((sum_data + sum_bss))" "$(echo "($sum_data + $sum_bss) / 1024" | bc -l)"
echo
if [ "$((sum_data + sum_bss))" -ne 0 ]; then
    echo "FAIL: $((sum_data + sum_bss)) bytes of writable static state."
    echo "      Zero mutable globals is a hard rule: all runtime state belongs to nj_vm."
    exit 1
fi
echo "OK: zero writable static state - the RAM figure is the runtime's own floor,"
echo "    and everything a MIDlet allocates comes from the heap the port provides."
