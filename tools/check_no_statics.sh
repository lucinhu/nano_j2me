#!/usr/bin/env bash
#
# Enforce the runtime's "no mutable global state" rule.
#
# Every byte of runtime state must live in an nj_vm instance so that
# nj_vm_destroy() is a complete teardown and several VMs can coexist.  That
# means src/ may contain:
#
#   - `const` file-scope data (native tables, font bitmaps)  -> .rodata, no RAM
#   - stack and heap objects
#
# and nothing else.
#
# WHY THIS RESOLVES SECTION NAMES INSTEAD OF TRUSTING nm LETTERS
# A `const` array of structs that contain pointers (the native registration
# tables, for instance) cannot go in .rodata on a hosted ELF: it needs dynamic
# relocation.  The linker puts it in .data.rel.ro, which the loader maps
# read-only once relocation is done (it lives in the RELRO segment).  nm
# reports those symbols with the same letters as genuine .data, so a
# letter-based check produces false positives.  This script resolves each
# symbol's section index instead, and only .data/.bss/.sdata/.sbss count as
# mutable state.
#
# Usage:
#   tools/check_no_statics.sh            # builds the objects itself
#   tools/check_no_statics.sh a.o b.o …  # checks the given objects
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-cc}"

for tool in readelf nm; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "SKIP: $tool not available" >&2
        exit 0
    }
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ "$#" -gt 0 ]; then
    OBJS=("$@")
else
    SRCS=()
    while IFS= read -r f; do SRCS+=("$f"); done < <(
        find "$ROOT/src" -name '*.c' 2>/dev/null | sort)
    OBJS=()
    for s in "${SRCS[@]}"; do
        o="$TMP/$(echo "${s#"$ROOT"/}" | tr '/' '_').o"
        if ! "$CC" -std=c99 -O2 -I"$ROOT/include" -I"$ROOT/src/core" \
                   -c "$s" -o "$o" 2>"$TMP/err"; then
            echo "FAIL: cannot compile ${s#"$ROOT"/}" >&2
            cat "$TMP/err" >&2
            exit 2
        fi
        OBJS+=("$o")
    done
fi

# Section names that mean "mutable global state".
MUTABLE_RE='^\.(data|bss|sbss|sdata)$'

BAD=""
RELRO=0
TOTAL=0

for o in "${OBJS[@]}"; do
    sections="$(readelf -SW "$o" 2>/dev/null |
        sed -n 's/^ *\[ *\([0-9]*\)\] *\([^ ]*\).*/\1 \2/p')"
    [ -z "$sections" ] && continue

    while read -r idx name; do
        [ -z "${idx:-}" ] && continue
        sec="$(printf '%s\n' "$sections" | awk -v i="$idx" '$1 == i { print $2; exit }')"
        if [ "$sec" = ".data.rel.ro" ]; then
            RELRO=$((RELRO + 1))
            continue
        fi
        if printf '%s' "$sec" | grep -qE "$MUTABLE_RE"; then
            BAD="${BAD}$(basename "$o"): $name is in $sec"$'\n'
        fi
    done < <(readelf -sW "$o" 2>/dev/null |
             awk '$4 == "OBJECT" || $4 == "FUNC" { print $7, $8 }')
    TOTAL=$((TOTAL + 1))
done

if [ -n "$BAD" ]; then
    echo "FAIL: the runtime defines writable global state:" >&2
    printf '%s' "$BAD" >&2
    echo >&2
    echo "Every piece of mutable state must belong to an nj_vm instance." >&2
    echo "Read-only tables must be declared 'const' so they land in .rodata" >&2
    echo "(or .data.rel.ro, which is read-only after relocation)." >&2
    exit 1
fi

echo "OK: no writable statics across $TOTAL object files" \
     "($RELRO const-pointer tables in .data.rel.ro, read-only at runtime)"
exit 0
