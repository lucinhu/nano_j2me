#!/bin/bash
# Encoding names are case-insensitive, and the aliases agree with the canonical
# spelling byte for byte.
#
# 疯狂战机_2013 called String.getBytes("utf-8") and the runtime answered
# UnsupportedEncodingException: the name table was matched with strcmp.  48 of
# the collected titles carry a non-canonical spelling of UTF-8, so the fix is a
# class fix and this is its regression test.
#
# The assertions are counted rather than eyeballed: the byte LENGTHS say which
# decoder ran (13 = UTF-8, 11 = one byte per char, for text chosen so the two
# cannot coincide), the *same flags say every spelling agrees with the canonical
# name, and the refusal flags say an unimplemented name still refuses instead of
# silently decoding as Latin-1.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/encdemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
timeout 60 "$BIN" --port headless --run-frames 3 "$JAR" >"$log" 2>&1
line="$(grep -m1 '^ENC ' "$log" || true)"
echo "$line"
fail=0
case "$line" in
    "ENC FAILED"*) echo "FAIL: the fixture itself threw"; fail=1 ;;
esac
expect() {   # field, expected, message
    case "$line" in
        *"$1=$2"*) ;;
        *) echo "FAIL: $3 ($1 is not $2)"; fail=1 ;;
    esac
}
expect utf8 13       "the UTF-8 spellings did not produce 13 bytes for the mixed text"
expect utf8same true "some UTF-8 spelling disagrees with \"UTF-8\""
expect latin 11      "the Latin-1 spellings did not produce 11 bytes for the Latin-1 text"
expect latinsame true "some Latin-1 spelling disagrees with \"ISO-8859-1\""
expect default true  "getBytes() without a name is not the platform encoding"
expect defaultround true "the platform encoding did not round-trip"
expect refuse true   "getBytes(\"Shift_JIS\") was accepted instead of refused"
expect ctorrefuse true "new String(b, \"Shift_JIS\") was accepted instead of refused"
expect nullrefuse true "getBytes((String) null) did not throw NullPointerException"
[ "$fail" = 0 ] || exit 1
echo "PASS: encoding names are case-insensitive, aliased to the same bytes, and unimplemented names still refuse"
