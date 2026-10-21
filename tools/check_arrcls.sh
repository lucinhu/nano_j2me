#!/usr/bin/env bash
#
# An array the runtime created has no class, and the empty string has no
# characters (tests/arrcls/ArrClsDemo.java).
#
# String.getBytes() hands out a byte[] that was never given a class, and a
# title calls methods on it.  A phone answers such a call the way it answers
# any Object call - "[B@1a2b3c" for toString().  The emulator used to look
# the method up in the missing class, find nothing, and then build the error
# message "X does not implement Y" FROM the missing class: a null
# dereference, a SIGSEGV, and the whole emulator gone.  The UC browser title
# is what hit it.
#
# The other half is the empty string.  The runtime stores one as a NULL
# payload, and the C standard does not allow a NULL pointer to be handed to
# memcmp even when the length is zero, so String.startsWith("") has to be
# answered without attempting the comparison.
#
#   tools/check_arrcls.sh [jar]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/arrcls.jar}"

if [ ! -x "$BIN" ]; then echo "FAIL no runner at $BIN" >&2; exit 2; fi
if [ ! -f "$JAR" ]; then
  # The fixture is generated, not checked in (tests/fixtures/ is gitignored).
  if [ -x "$ROOT/tools/make_arrcls_jar.sh" ]; then
    bash "$ROOT/tools/make_arrcls_jar.sh" "$JAR" >/dev/null
  else
    echo "FAIL no fixture at $JAR" >&2; exit 2
  fi
fi
if [ ! -f "$JAR" ]; then echo "FAIL no fixture at $JAR" >&2; exit 2; fi

OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

set +e
"$BIN" --port headless --heap 8 --size 176x208 --run-frames 5 "$JAR" >"$OUT" 2>&1
rc=$?
set -e

python3 - "$OUT" "$rc" <<'PY'
import re, sys
text = open(sys.argv[1], "rb").read().decode("utf-8", "replace")
rc = int(sys.argv[2])
fails = []

def val(name):
    m = re.search(r"^ARRCLS %s=(.*)$" % re.escape(name), text, re.M)
    return m.group(1) if m else None

def field(name, want, why):
    got = val(name)
    if got is None:
        fails.append("%s: no ARRCLS %s= line" % (name, name))
    elif want is not None and not got.startswith(want):
        fails.append("%s: %r, want %r (%s)" % (name, got, want, why))
    else:
        print("ok   %s = %r (%s)" % (name, got, why))

if rc != 0:
    fails.append("the run exits cleanly (exit %d)" % rc)

# 1. The array the MIDlet allocated has a class, and the one String.getBytes()
#    handed out has none: both answer a call to Object.toString() the way a
#    phone does.
field("heap.threw", "none", "new byte[3].toString() does not throw")
field("heap.text", "[B@", "an array prints its type and its identity hash")
field("native.threw", "none", "getBytes().toString() does not throw")
field("native.text", "[B@", "the classless array prints like the other one")

# 2. The empty string.
field("sw.empty", "true", "every string starts with the empty string")
field("sw.hit", "true", "\"abc\".startsWith(\"ab\")")
field("sw.miss", "false", "\"abc\".startsWith(\"x\")")
field("ew.empty", "true", "every string ends with the empty string")
field("ew.hit", "true", "\"abc\".endsWith(\"bc\")")
field("ew.miss", "false", "\"abc\".endsWith(\"x\")")
field("eq.empty", "true", "two empty strings are equal")
field("eq.hit", "true", "\"abc\".equals(\"abc\")")
field("eq.miss", "false", "\"abc\".equals(\"abd\")")

# 3. An argument that is not a String: the cast has to be checked, and the
#    answer has to be an exception, not a crash.
field("sw.nonstring", "threw java.lang.ClassCastException",
      "a non-String argument is a ClassCastException")

if fails:
    print("FAIL check_arrcls")
    for f in fails:
        print("  FAIL " + f)
    sys.exit(1)
print("PASS check_arrcls")
PY
