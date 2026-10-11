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

# THIRD lenient-compiler artefact: `return this.screenView;;` - a second
# semicolon after a return.  JLS 14.21 makes that empty statement unreachable
# and javac rejects it; Eclipse's compiler, which the Nokia IDE used, does not
# flag a lone ';' as unreachable.  Removing it changes nothing about what the
# method does.
python3 - "$WORK/vnes/src" <<'PY'
import glob, os, re, sys
root = sys.argv[1]
n = 0
for path in glob.glob(os.path.join(root, "**", "*.java"), recursive=True):
    src = open(path, encoding="latin-1").read()
    new = re.sub(r"(\breturn\b[^;\n]*;)\s*;", r"\1", src)
    if new != src:
        open(path, "w", encoding="latin-1").write(new)
        n += len(re.findall(r"\breturn\b[^;\n]*;\s*;", src))
        print("   dropped a stray ';' after return in %s" % os.path.basename(path))
if n == 0:
    sys.exit("no stray-semicolon patch was needed; revisit this patch")
PY

# SECOND TOOLCHAIN WORKAROUND, and this one is about the JDK rather than the
# source being wrong.
#
# vNesFileSelector imports BOTH java.util.* and javax.microedition.lcdui.*, and
# writes `extends List`.  On a CLDC device java.util contains no List, so this
# resolves to javax.microedition.lcdui.List and compiles.  javac here resolves
# java.* from the DESKTOP JDK, where java.util.List exists, so it reports the
# reference as ambiguous.
#
# Narrowing the wildcard to the CLDC classes the file actually uses is
# semantically IDENTICAL on a device - those are the only java.util classes a
# device has - and it removes the desktop-only ambiguity.  Done generically:
# every file that imports both wildcards is rewritten, and the script stops if
# the pattern it expects is not there, because a patch that quietly stops
# applying is worse than no patch.
python3 - "$WORK/vnes/src" <<'PY'
import glob, os, re, sys
root = sys.argv[1]
# java.util classes CLDC 1.1 actually has.  A file needing anything else would
# not run on a device either, and tools/check_cldc_api.py would say so.
CLDC_UTIL = ["Enumeration", "Vector", "Hashtable", "Stack", "Random", "Timer",
             "TimerTask", "Calendar", "Date", "TimeZone"]
patched = 0
for path in glob.glob(os.path.join(root, "**", "*.java"), recursive=True):
    src = open(path, encoding="latin-1").read()
    if "import java.util.*;" not in src:
        continue
    if "import javax.microedition.lcdui.*;" not in src:
        continue        # no ambiguity: lcdui's List is not in scope
    used = [c for c in CLDC_UTIL
            if re.search(r"\b%s\b" % c, src.replace("import java.util.*;", ""))]
    if not used:
        sys.exit("no java.util class found in %s; revisit this patch" % path)
    repl = "\n".join("import java.util.%s;" % c for c in sorted(used))
    src = src.replace("import java.util.*;", repl)
    open(path, "w", encoding="latin-1").write(src)
    patched += 1
    print("   narrowed java.util.* in %s -> %s"
          % (os.path.basename(path), ", ".join(sorted(used))))
if patched == 0:
    sys.exit("no file needed the java.util narrowing; revisit this patch")
PY

exec "$ROOT/tools/build_j2me_source.sh" "$WORK/vnes" "$OUT"
