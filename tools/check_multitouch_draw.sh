#!/usr/bin/env bash
#
# Check that the multiplexed touch pointers are *drawn* per finger: the
# multitouch fixture paints one coloured dot per active pointer (id 1 green,
# id 2 blue, see MultitouchDemo.FINGER_RGB), so a headless frame dump proves
# that each finger arrives with its own coordinates rather than one merged
# pointer.  This complements check_multitouch.sh, which checks the listener
# callbacks and the keyboard of the MIDP Canvas pointer.
#
#   tools/check_multitouch_draw.sh [fixture.jar]
#
# Env: NJ_BIN (default build/nj2me), NJ_KEEP (keep log + frame), TIMEOUT.
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
command -v python3 >/dev/null 2>&1 || { echo "skip: no python3"; exit 0; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/frames"
KEYS="$WORK/keys.txt"
cat > "$KEYS" <<'EOF'
20:pressed#1@40,50
20:pressed#2@150,120
25:dragged#1@42,52
25:dragged#2@152,122
EOF

LOG="$WORK/multitouch-draw.log"
"$BIN" --port headless --size 240x320 --heap 16 --fps 30 \
    --run-frames 120 --keys "@$KEYS" --verbose 3 --stats \
    --dump-frames "$WORK/frames/f" --dump-every 10 \
    --rms-dir "$WORK/rms" "$JAR" >"$LOG" 2>&1 || true
if [ -n "${NJ_KEEP:-}" ]; then
    cp "$LOG" "$NJ_KEEP"
    cp "$WORK"/frames/*.ppm "$NJ_KEEP.frames" 2>/dev/null || true
fi

PASS=0
FAIL=0
ok() { echo "ok   $1"; PASS=$((PASS + 1)); }
bad() { echo "FAIL $1"; FAIL=$((FAIL + 1)); }

# The listener must have seen both ids; the drawing check follows.
grep -qF "nokia mpt: pointer 1 state 0" "$LOG" && ok "finger 1 reached the listener" \
    || bad "finger 1 never reached the listener"
grep -qF "nokia mpt: pointer 2 state 0" "$LOG" && ok "finger 2 reached the listener" \
    || bad "finger 2 never reached the listener"

LAST="$(ls "$WORK"/frames/*.ppm 2>/dev/null | sort | tail -1 || true)"
if [ -z "$LAST" ]; then
    bad "the run dumped no frame"
else
    RESULT="$(python3 - "$LAST" <<'PY'
import sys

def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(b"\n", 3)
    if parts[0].strip() != b"P6":
        raise SystemExit("not a P6 ppm")
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

def pixel(pix, w, x, y):
    i = (y * w + x) * 3
    return pix[i], pix[i + 1], pix[i + 2]

def near(rgb, want, tol=48):
    return all(abs(a - b) <= tol for a, b in zip(rgb, want))

w, h, pix = read_ppm(sys.argv[1])
green = (67, 160, 71)     # FINGER_RGB[1], id 1
blue = (30, 136, 229)     # FINGER_RGB[2], id 2
checks = [
    (w == 240 and h == 320, "the dumped frame is 240x320"),
    (near(pixel(pix, w, 40, 50), green), "finger 1's dot is at its own position"),
    (near(pixel(pix, w, 150, 120), blue), "finger 2's dot is at its own position"),
    (not near(pixel(pix, w, 40, 50), blue), "the two dots are independent"),
]
# Both colours must cover a dot's worth of pixels, so a single stray pixel
# cannot satisfy the position check.
green_px = blue_px = 0
for i in range(0, len(pix), 3):
    rgb = pix[i], pix[i + 1], pix[i + 2]
    if near(rgb, green, 24):
        green_px += 1
    elif near(rgb, blue, 24):
        blue_px += 1
checks.append((green_px >= 50 and blue_px >= 50,
               "both finger dots cover their area (green=%d blue=%d)"
               % (green_px, blue_px)))
for good, label in checks:
    print(("ok   " if good else "FAIL ") + label)
raise SystemExit(0 if all(c[0] for c in checks) else 1)
PY
)" || true
    # Re-emit the python verdict lines and count them here.
    while IFS= read -r line; do
        case "$line" in
            ok*) ok "${line#ok   }" ;;
            FAIL*) bad "${line#FAIL }" ;;
            *) echo "$line" ;;
        esac
    done <<<"$RESULT"
fi

echo
echo "check_multitouch_draw: $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
