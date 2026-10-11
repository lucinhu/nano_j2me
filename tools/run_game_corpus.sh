#!/usr/bin/env bash
#
# Compatibility regression over a corpus of real MIDlet JARs.
#
# For every JAR it runs the emulator headless, dumps frames, and asserts the
# things that distinguish "a game is running" from "something did not crash":
#
#   - the MIDlet loaded and started (no "cannot instantiate" / "not a usable")
#   - no uncaught exception was reported in any thread
#   - frames were actually presented
#   - the frames are not a single flat colour (a blank screen is the failure
#     mode that a crash-free run hides best)
#
# It also REPORTS whether the picture changes over time.  That is the signal
# that separates a live game loop from a dead thread - but it is not a failure
# on its own, because a title is allowed to be static: hello.jar draws
# "Hello World!" once and never changes a pixel, and calling that a failure
# would be the test being wrong, not the runtime.  Use --strict-motion to make
# it fatal when the corpus is known to be animated.
#
# Usage:
#   tools/run_game_corpus.sh [--dir DIR] [--size WxH] [--timeout SECS] [--jobs N]
#
# Exits non-zero if any title fails, so it can be a CI gate.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
DIR="${NJ_GAMES:-$ROOT/tests/games}"
SIZE="${NJ_SIZE:-176x208}"
SECS="${NJ_TIMEOUT:-20}"
MIN_COLOURS="${NJ_MIN_COLOURS:-2}"
FPS="${NJ_FPS:-0}"
STRICT_MOTION="${NJ_STRICT_MOTION:-0}"

while [ $# -gt 0 ]; do
    case "$1" in
        --dir)      DIR="$2"; shift 2 ;;
        --size)     SIZE="$2"; shift 2 ;;
        --timeout)  SECS="$2"; shift 2 ;;
        --bin)      BIN="$2"; shift 2 ;;
        --min-colours) MIN_COLOURS="$2"; shift 2 ;;
        --strict-motion) STRICT_MOTION=1; shift ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [ ! -x "$BIN" ]; then
    echo "no emulator at $BIN (build it first)" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is needed to inspect the dumped frames" >&2
    exit 2
fi

mapfile -t JARS < <(find "$DIR" -maxdepth 1 -name '*.jar' 2>/dev/null | sort)
if [ "${#JARS[@]}" -eq 0 ]; then
    echo "no JARs in $DIR - run tools/fetch_test_games.sh first" >&2
    exit 2
fi

pass=0
fail=0
printf '%-14s %-8s %-8s %-9s %s\n' TITLE FRAMES COLOURS CHANGED RESULT
printf '%-14s %-8s %-8s %-9s %s\n' ----- ------ ------- ------- ------

for jar in "${JARS[@]}"; do
    title="$(basename "$jar" .jar)"
    out="$(mktemp -d)"
    log="$out/log.txt"

    timeout "$SECS" "$BIN" --port headless --heap 8 --size "$SIZE" \
            --fps "$FPS" --dump-frames "$out/" --dump-every 40 \
            "$jar" >"$log" 2>&1

    verdict="$(python3 - "$out" "$MIN_COLOURS" "$log" "$STRICT_MOTION" <<'PY'
import glob, sys, hashlib
from collections import Counter
try:
    from PIL import Image
except ImportError:
    print("SKIP|PIL missing"); raise SystemExit

d, min_colours, logpath = sys.argv[1], int(sys.argv[2]), sys.argv[3]
strict = sys.argv[4] == "1"
log = open(logpath, errors="replace").read()
frames = sorted(glob.glob(d + "/*.ppm"))

if "not a usable MIDlet suite" in log:
    print("FAIL|0|0|0|not a MIDlet suite"); raise SystemExit
if "cannot instantiate" in log or "cannot read the JAR" in log:
    print("FAIL|0|0|0|could not start"); raise SystemExit
if "uncaught exception" in log:
    line = [l for l in log.splitlines() if "uncaught exception" in l][0]
    print("FAIL|%d|0|0|%s" % (len(frames), line.split(": ", 1)[-1][:44]))
    raise SystemExit
if not frames:
    print("FAIL|0|0|0|nothing was ever presented"); raise SystemExit

# The last frame is the most likely to be past any splash screen, but a game
# that has already exited may have dumped its last frame mid-teardown, so
# measure the richest frame instead.
cols = 0
for p in frames[-6:]:
    try:
        n = len(Counter(Image.open(p).getdata()))
    except Exception:
        continue
    cols = max(cols, n)

digests = {hashlib.md5(open(p, "rb").read()).hexdigest() for p in frames}
changed = len(digests)

if cols < min_colours:
    print("FAIL|%d|%d|%d|flat frame: the game is not drawing" % (len(frames), cols, changed))
    raise SystemExit
if changed < 2:
    tag = "FAIL" if strict else "PASS"
    print("%s|%d|%d|%d|static picture" % (tag, len(frames), cols, changed))
    raise SystemExit
print("PASS|%d|%d|%d|" % (len(frames), cols, changed))
PY
)"
    rm -rf "$out"

    IFS='|' read -r status nframes ncols nchanged note <<<"$verdict"
    printf '%-14s %-8s %-8s %-9s %s\n' "$title" "$nframes" "$ncols" "$nchanged" \
           "$status${note:+ ($note)}"
    if [ "$status" = "PASS" ] || [ "$status" = "SKIP" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done

echo
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ] || exit 1
