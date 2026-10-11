#!/usr/bin/env bash
#
# vNes - Summeli/vNes-Asha, GPL v3, a NES emulator written as a J2ME MIDlet.
#
#     tools/make_vnes_jar.sh [out.jar]
#
# Not a game: it is an emulator, which makes it a much harder test of the
# runtime than one.  Its inner loop is 6502 interpretation over byte arrays, and
# it leans on integer arithmetic, array access and type conversion in a way a
# puzzle game never does.
#
# ONE SOURCE PATCH, AND WHY IT IS NOT A HACK.  vNesFileSelector.java contains
#
#     import OperationsQueue;
#     import Operation;
#
# which is not valid Java: a type in the default package cannot be imported.
# Both classes live in the default package, in the same directory as the file
# importing them, so the imports are redundant - Java resolves same-package
# types without one.  The project compiled in Nokia's IDE, whose compiler was
# lenient about this; javac is not.
#
# So the file is patched to say what it means.  Nothing else about the source is
# touched, and if the pattern is not found the build stops rather than silently
# proceeding, because a patch that quietly stops applying is worse than no patch.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/games/vnes.jar}"
REPO="${NJ_VNES_SRC:-https://github.com/Summeli/vNes-Asha}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== fetching $REPO"
timeout 300 git clone -q --depth 1 "$REPO" "$WORK/vnes" 2>/dev/null || {
    echo "cannot fetch $REPO" >&2; exit 1; }

F="$WORK/vnes/src/vNesFileSelector.java"
[ -f "$F" ] || { echo "vNesFileSelector.java is not where it was" >&2; exit 1; }
before="$(grep -c '^import \(OperationsQueue\|Operation\);' "$F" || true)"
[ "$before" -eq 2 ] || {
    echo "expected 2 default-package imports to remove, found $before" >&2
    echo "the source changed; the patch below needs revisiting" >&2
    exit 1; }
sed -i '/^import OperationsQueue;$/d;/^import Operation;$/d' "$F"
echo "   removed 2 redundant default-package imports"

exec "$ROOT/tools/build_j2me_source.sh" "$WORK/vnes" "$OUT"
