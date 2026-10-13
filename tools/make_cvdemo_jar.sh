#!/usr/bin/env bash
#
# Build the ConstantValue fixture.
#
# Holder is compiled twice on purpose: first as a plain class, so CVRead's reads
# become getstatic fieldrefs instead of inlined constants, then as a class of
# `static final` constants, so those same reads can only be answered from the
# ConstantValue attributes javac writes.  tools/check_constant_value.sh holds
# the expectation; this only builds it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/cvdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF" "$WORK/src"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"

# Pass 1: plain statics.  CVRead (and only CVRead) is compiled here, so its
# reads of Holder are fieldrefs.
cp "$ROOT/tests/cvdemo/Holder.plain.java" "$WORK/src/Holder.java"
cp "$ROOT/tests/cvdemo/CVRead.java" "$ROOT/tests/cvdemo/CVDemo.java" "$WORK/src/"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$WORK/src/Holder.java" "$WORK/src/CVRead.java" "$WORK/src/CVDemo.java"

# Pass 2: the same fields as constants.  Only Holder is recompiled, so
# CVRead.class keeps the fieldrefs from pass 1.
cp "$ROOT/tests/cvdemo/Holder.final.java" "$WORK/src/Holder.java"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$WORK/src/Holder.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: CVDemo\r\nMIDlet-1: CVDemo, , CVDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
