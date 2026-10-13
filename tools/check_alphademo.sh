#!/usr/bin/env bash
#
# A mutable Image is white and fully opaque (tests/alphademo/AlphaDemo.java).
#
# MIDP 2.0, Image.createImage(int width, int height): "Every pixel within the
# newly created image is white", and of the class: "Every pixel within a
# mutable image is always fully opaque."  The fixture prints one line per
# promise; this checks all of them, field by field, so a failure names the
# promise that broke.
#
#   tools/check_alphademo.sh [jar]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build-lead/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/alphademo.jar}"

if [ ! -x "$BIN" ]; then echo "FAIL no runner at $BIN" >&2; exit 2; fi
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
    m = re.search(r"ALPHA %s=([0-9a-f]+)" % re.escape(name), text)
    return m.group(1).lower().zfill(8) if m else None

def field(name, want, why):
    got = val(name)
    if got is None:
        fails.append("%s: no ALPHA %s= line" % (name, name))
    elif got != want:
        fails.append("%s: 0x%s, want 0x%s (%s)" % (name, got, want, why))
    else:
        print("ok   %s = 0x%s (%s)" % (name, got, why))

if rc != 0:
    fails.append("the run exits cleanly (exit %d)" % rc)
if "uncaught" in text or "thrown:" in text:
    fails.append("nothing was thrown")

field("fresh", "ffffffff", "a new mutable image is white and opaque")
field("drawn", "ffff7f7f", "50% red over white is an opaque pink")
field("blend", "ffff7f7f", "the same blend against the image's own white")
field("rgbkeep", "ff00ff00", "processAlpha=false keeps the colour, ignores alpha")
field("copy", "ffff7f7f", "the copy holds what the mutable image held")

a = val("drawn")
if a and a[:2] != "ff":
    fails.append("drawn: alpha 0x%s, a mutable image is always fully opaque" % a[:2])

if fails:
    print("FAIL check_alphademo")
    for f in fails:
        print("  FAIL " + f)
    sys.exit(1)
print("PASS check_alphademo")
PY
