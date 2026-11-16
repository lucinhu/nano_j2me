#!/usr/bin/env bash
#
# Check that multi-touch reaches the Nokia MultipointTouch listener with every
# finger's own id (Minecraft J2ME PE's control scheme), while the MIDP Canvas
# pointer API stays on the first finger down.
#
#   tools/check_multitouch.sh [fixture.jar]
#
# Env: NJ_BIN (default build/nj2me), NJ_KEEP (keep the log), TIMEOUT.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JAR="${1:-$ROOT/tests/fixtures/multitouchdemo.jar}"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
TIMEOUT="${TIMEOUT:-90}"

if [ ! -x "$BIN" ]; then
    echo "skip: no runtime binary at $BIN"
    exit 0
fi
if [ ! -f "$JAR" ]; then
    echo "skip: no fixture at $JAR"
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
KEYS="$WORK/keys.txt"
cat > "$KEYS" <<'EOF'
30:pressed#1@40,50
40:pressed#2@120,90
60:dragged#1@45,55
70:dragged#2@125,95
90:released#1@45,55
100:released#2@125,95
EOF

LOG="$WORK/multitouch.log"
"$BIN" --port headless --size 240x320 --heap 16 --fps 30 \
    --run-frames 150 --keys "@$KEYS" --verbose 3 --stats \
    --rms-dir "$WORK/rms" "$JAR" >"$LOG" 2>&1 || true
if [ -n "${NJ_KEEP:-}" ]; then
    cp "$LOG" "$NJ_KEEP"
fi

PASS=0
FAIL=0
ok() {
    if grep -qF -- "$2" "$LOG"; then
        echo "ok   $1"
        PASS=$((PASS + 1))
    else
        echo "FAIL $1: missing \"$2\""
        FAIL=$((FAIL + 1))
    fi
}
not_ok() {
    if grep -qF -- "$2" "$LOG"; then
        echo "FAIL $1: unexpected \"$2\""
        FAIL=$((FAIL + 1))
    else
        echo "ok   $1"
        PASS=$((PASS + 1))
    fi
}

ok "the fixture found the singleton" "MPT ready=true"
ok "finger 1 arrives on the listener" "nokia mpt: pointer 1 state 0"
ok "finger 2 arrives on the listener" "nokia mpt: pointer 2 state 0"
ok "finger 1 reports its drag" "nokia mpt: pointer 1 state 1"
ok "finger 2 reports its drag" "nokia mpt: pointer 2 state 1"
ok "finger 1 reports its release" "nokia mpt: pointer 1 state 2"
ok "finger 2 reports its release" "nokia mpt: pointer 2 state 2"
ok "the listener reports finger 1 pressed" "MPT id=1 state=0"
ok "the listener reports finger 2 pressed" "MPT id=2 state=0"
ok "the first finger owns the Canvas pointer" "CANVAS down 40 50"
ok "the first finger drags the Canvas pointer" "CANVAS drag 45 55"
ok "the first finger lifts the Canvas pointer" "CANVAS up 45 55"
not_ok "the second finger does not steal the Canvas pointer" "CANVAS down 120 90"

echo
echo "check_multitouch: $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
