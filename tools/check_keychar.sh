#!/usr/bin/env bash
#
# What Displayable.getKeyChar() answers (tests/keychardemo/KeyCharDemo.java).
#
# MIDP 2.0 puts getKeyChar() on Displayable: "Gets the character representing
# the key that was pressed."  A Canvas that draws its own editor asks for it
# in keyPressed() - the key code only says which button was hit, the
# character is what the user typed.
#
# The runtime had no getKeyChar() at all, so a title that called it got
# NoSuchMethodError on the first key press and the input was lost.
#
#   tools/check_keychar.sh [jar]
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/keychardemo.jar}"

if [ ! -x "$BIN" ]; then echo "FAIL no runner at $BIN" >&2; exit 2; fi
if [ ! -f "$JAR" ]; then
  # The fixture is generated, not checked in (tests/fixtures/ is gitignored).
  if [ -x "$ROOT/tools/make_keychardemo_jar.sh" ]; then
    bash "$ROOT/tools/make_keychardemo_jar.sh" "$JAR" >/dev/null
  else
    echo "FAIL no fixture at $JAR" >&2; exit 2
  fi
fi
if [ ! -f "$JAR" ]; then echo "FAIL no fixture at $JAR" >&2; exit 2; fi

OUT="$(mktemp)"
RMS="$(mktemp -d)"
trap 'rm -f "$OUT"; rm -rf "$RMS"' EXIT

set +e
"$BIN" --port headless --heap 8 --size 176x208 --run-frames 120 \
       --keys '20:5 60:fire 100:star' --rms-dir "$RMS" "$JAR" >"$OUT" 2>&1
rc=$?
set -e

python3 - "$OUT" "$rc" <<'PY'
import re, sys
text = open(sys.argv[1], "rb").read().decode("utf-8", "replace")
rc = int(sys.argv[2])
fails = []

pairs = re.findall(r"^KEYCHAR code=(-?\d+) char=(-?\d+)$", text, re.M)
if not pairs:
    fails.append("no KEYCHAR line at all: getKeyChar() did not answer")

def check(code, char, why):
    got = [(int(c), int(k)) for c, k in pairs]
    if (code, char) not in got:
        fails.append("code %d: no answer %r, want char %r (%s)"
                     % (code, char, char, why))
    else:
        print("ok   code %d -> char %d (%s)" % (code, char, why))

if rc != 0:
    fails.append("the run exits cleanly (exit %d)" % rc)
if "threw" in text or "NoSuchMethodError" in text:
    fails.append("getKeyChar() threw instead of answering")

# MIDP's keypad: the ten digits, '*' and '#' have characters, the rest have
# none.
check(53, 53, "KEY_NUM5 is the character '5'")
check(42, 42, "KEY_STAR is the character '*'")
check(-5, 0, "KEY_FIRE has no character")

if fails:
    print("FAIL check_keychar")
    for f in fails:
        print("  FAIL " + f)
    sys.exit(1)
print("PASS check_keychar")
PY
