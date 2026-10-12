#!/bin/sh
#
# Pointer input, driven from a script.
#
# The bug this exists to catch: NJ_EV_POINTER_MOVE arrived as the value 2 and
# every call site tested it for truth, so a drag reached the Canvas as another
# pointerPressed() at the new coordinates and pointerDragged() was never called
# by anything.  The fixture prints each callback at its own coordinates, so the
# two behaviours are distinguishable in the output rather than merely in
# principle.
#
# The drag case below is the whole point: it asserts that `PTR dragged` is
# there AND that `PTR pressed` is NOT.  Checking only the first would pass even
# with the bug in place, because the old code called pointerPressed *and* the
# fixture's drag body printed nothing - absence of proof either way.
#
# Usage: check_pointer.sh <keysdemo.jar>
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
JAR=${1:-$ROOT/tests/fixtures/keysdemo.jar}
BIN=$ROOT/build/nj2me
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$BIN" ]; then
    echo "check_pointer: $BIN not built" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    echo "check_pointer: $JAR not found (run tools/make_keys_jar.sh)" >&2
    exit 2
fi

fail=0
pass=0

check() {
    what=$1
    want=$2
    got=$3
    if [ "$want" = "$got" ]; then
        pass=$((pass + 1))
        echo "  ok    $what"
    else
        fail=$((fail + 1))
        echo "  FAIL  $what"
        echo "        wanted: $want"
        echo "        got:    $got"
    fi
}

run() {
    script=$1
    printf '%s\n' "$script" > "$TMP/keys.txt"
    timeout 30 "$BIN" --port headless --keys "@$TMP/keys.txt" --run-frames 20 \
        "$JAR" 2>&1
}

# Lines mentioning a pointer callback, without their frame numbering, so the
# comparison is about which callback ran and not about when.
ptrs() {
    printf '%s\n' "$1" | grep '^PTR ' | sort | uniq -c |
        sed 's/^ *//' | awk '{print $1"x "$2" "$3" "$4}' | tr '\n' ';'
}

echo "== nano_j2me pointer input =="

# ---- a drag must be a drag -------------------------------------------------
out=$(run '5:dragged@50,70')
check "a move calls pointerDragged at its coordinates" \
      "1x PTR dragged 50,70;" "$(ptrs "$out")"

# ---- ...and must NOT be a press -------------------------------------------
check "a move does not call pointerPressed" \
      "" "$(printf '%s\n' "$out" | grep '^PTR pressed' || true)"

# ---- a press and a release, at their own coordinates ----------------------
out=$(run '5:pressed@40,60
9:released@41,61')
check "press and release arrive with the coordinates they were sent" \
      "1x PTR pressed 40,60;1x PTR released 41,61;" "$(ptrs "$out")"

# ---- a whole gesture, in order --------------------------------------------
# The order matters: a MIDlet dragging an object depends on down, then moves,
# then up.  Sorting inside ptrs() hides order, so compare the raw sequence.
out=$(run '5:pressed@10,10
6:dragged@20,20
7:dragged@30,30
8:released@30,30')
check "down, two moves and up arrive in that order" \
      "PTR pressed 10,10|PTR dragged 20,20|PTR dragged 30,30|PTR released 30,30" \
      "$(printf '%s\n' "$out" | grep '^PTR ' | tr '\n' '|' | sed 's/|$//')"

# ---- no script, no pointer events -----------------------------------------
# The control for all of the above: without a script nothing is delivered, so
# the assertions are about the script and not about a runtime that emits
# pointer events on its own.
out=$(printf '%s\n' "$(run '')")
check "with no script at all, no pointer callback fires" \
      "" "$(printf '%s\n' "$out" | grep '^PTR ' || true)"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
exit 0
