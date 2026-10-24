#!/bin/sh
# Replay the titles that found (and later proved) the compatibility fixes of
# rounds 33-37.  Each line below is a defect that was fixed with evidence from
# the corpus; these runs are the cheap, repeatable version of that evidence.
#
#   1. 海绵宝宝卡丁车_4684 - thread slots leaked while a thread's run() had
#      returned (nj_exec reported NJ_OK on an empty stack, so the slot stayed
#      RUNNING).  116 "too many threads" OOMs and 214k bytecodes before
#      c4e6bfc; 0 and billions after.
#   2. 拳皇98终极之战_7534 - Canvas.getGameAction/getKeyCode/getKeyName were
#      declared instance methods, so `invokestatic` on them threw
#      IncompatibleClassChangeError on every key press (a278807).
#   3. 毁灭战士3_10598 - the M3G spot light lost its cone angle in the loader
#      (822bb80); the geometry used to render black, and the title has to keep
#      running through its 3D path without exceptions either way.
#
# Usage: check_compat_fixes.sh <nj2me-binary> [games-directory]
set -u

bin=${1:-}
games=${2:-tests/games}
if [ -z "$bin" ] || [ ! -x "$bin" ]; then
    echo "usage: $0 <nj2me-binary> [games-directory]" >&2
    exit 2
fi

fail=0
tmp=$(mktemp -d "${TMPDIR:-/tmp}/njcompat.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT

run_case() {
    name=$1; prefix=$2; forbid=$3; minbc=$4
    jar=
    for f in "$games/$prefix"*.jar; do
        [ -f "$f" ] && jar=$f && break
    done
    if [ -z "$jar" ]; then
        echo "SKIP $name (no fixture $prefix*.jar)"
        return 0
    fi
    log=$tmp/$name.log
    mkdir -p "$tmp/$name.rms"
    "$bin" --port headless --size 240x320 --heap 16 --fps 0 \
           --run-frames 400 --rms-dir "$tmp/$name.rms" --stats "$jar" \
           >"$log" 2>&1
    rc=$?
    bc=$(sed -n 's/.*bytecodes=\([0-9]*\).*/\1/p' "$log" | tail -1)
    bc=${bc:-0}
    if [ "$rc" -ne 0 ]; then
        echo "FAIL $name: exit $rc"; fail=1; return 0
    fi
    if [ -n "$forbid" ] && grep -qF "$forbid" "$log"; then
        echo "FAIL $name: log still contains \"$forbid\""; fail=1; return 0
    fi
    if [ "$bc" -lt "$minbc" ]; then
        echo "FAIL $name: bytecodes=$bc, expected at least $minbc"; fail=1; return 0
    fi
    echo "OK   $name (bytecodes=$bc)"
}

run_case thread_reap        海绵宝宝卡丁车_4684  "too many threads"          1000000
run_case canvas_statics     拳皇98终极之战_7534  "IncompatibleClassChangeError" 100000
run_case m3g_spot_light     毁灭战士3_10598      "compression scheme"         30000

if [ "$fail" -ne 0 ]; then
    echo "FAIL: a compatibility fix regressed" >&2
    exit 1
fi
echo "PASS: the compatibility fixes still hold"
