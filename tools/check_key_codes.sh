#!/usr/bin/env bash
#
# Does a port key arrive at the MIDlet as the right MIDP key CODE?
#
# "an event arrived" and "the right event arrived" are different claims, and
# only the second one is worth anything to a game.  The whole port-to-MIDP key
# translation used to be wrong in a way that the weaker claim could not see:
#
#   * nj_port.h numbers the keys with an enum (UP = 13, FIRE = 17);
#   * nj_midp.h defined the MIDP codes with THE SAME NAMES (UP = -1,
#     FIRE = -5), so in every translation unit that included it the macros
#     shadowed the enum;
#   * nj_key_to_midp() therefore compared its input - an enum value - against
#     MIDP values and matched nothing.
#
# Every arrow and FIRE press from every port reached the MIDlet as keyCode 13
# instead of -1.  Numeric keys worked, because the enum is 1..10 for the digits
# and the case that mapped them happened to read the enum: KEY_NUM2 came out as
# 50 while UP did not come out as -1.  A game whose menu listens for UP sat on
# that menu forever, and Age of Empires III did exactly that.
#
# This check drives the keysdemo fixture - which now prints the code it was
# handed - through one scripted run and compares the codes against MIDP's.
#
# Usage: tools/check_key_codes.sh [keysdemo.jar]
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/keysdemo.jar}"

# name:expected MIDP key code.  These are javax.microedition.lcdui.Canvas's own
# numbers: KEY_NUM0..9 are 48..57, KEY_STAR 42, KEY_POUND 35, and the arrows and
# FIRE are NEGATIVE -1..-5.  GAME_A..GAME_D are -6..-9, and MIDP gives the soft
# keys the same codes as GAME_A..GAME_C.
TABLE="
up:-1
down:-2
left:-3
right:-4
fire:-5
soft1:-6
soft2:-7
a:-6
b:-7
c:-8
d:-9
star:42
pound:35
0:48
1:49
5:53
9:57
"

[ -x "$BIN" ] || { echo "no emulator at $BIN" >&2; exit 2; }
[ -f "$JAR" ] || { echo "no fixture at $JAR (run tools/make_keys_jar.sh)" >&2; exit 2; }

# One run, one key per frame slot, so the ORDER is checked as well as the value.
SPEC=""
WANT=""
frame=30
for row in $TABLE; do
    name="${row%%:*}"
    code="${row##*:}"
    SPEC="${SPEC}${SPEC:+,}${frame}:${name}"
    WANT="${WANT}${WANT:+ }${code}"
    frame=$((frame + 10))
done

OUT="$(timeout 90 "$BIN" --port headless --size 176x208 --fps 0 \
        --run-frames $((frame + 40)) --keys "$SPEC" "$JAR" 2>&1)"

GOT="$(printf '%s\n' "$OUT" | sed -n 's/^KEY down //p' | tr '\n' ' ' | sed 's/ *$//')"

if [ "$GOT" = "$WANT" ]; then
    n=$(printf '%s\n' "$WANT" | wc -w)
    echo "OK: $n key(s) arrived with the MIDP codes the script asked for"
    exit 0
fi

echo "FAIL: the codes that reached keyPressed() are not the MIDP ones"
echo "  asked for: $SPEC"
echo "  expected : $WANT"
echo "  got      : ${GOT:-(nothing - no key event reached the Canvas at all)}"
echo
echo "A code equal to the port's own enum number (UP = 13, FIRE = 17) means"
echo "nj_key_to_midp() is not translating, or its MIDP names shadow the enum"
echo "again.  See the comment on NJ_MIDP_KEY_* in src/midp/nj_midp.h."
exit 1
