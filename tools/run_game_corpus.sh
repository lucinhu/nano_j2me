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
# The heap used for every title.  This was hard-coded to 8 MB, which is below
# the emulator's own default of 16 MB, so the corpus reported OutOfMemoryError
# for titles that run perfectly well when a person runs them by hand.  A test
# harness may not be stricter than the product it tests without saying so.
HEAP="${NJ_HEAP:-16}"
DIR="${NJ_GAMES:-$ROOT/tests/games}"
# Titles gate themselves on the screen size and the gates differ: Tumbleweed
# refuses anything larger than 250 in either axis ("Screen too large") and
# anything smaller than 150x170 ("Screen too small").  A single hard-coded size
# therefore reports a perfectly good title as broken, so the corpus tries a
# list and records which one worked.
SIZES="${NJ_SIZES:-176x208 240x320 128x160 320x240}"
SECS="${NJ_TIMEOUT:-20}"
MIN_COLOURS="${NJ_MIN_COLOURS:-2}"
FPS="${NJ_FPS:-0}"
# A frame COUNT bounds each run, not a wall-clock timeout.
#
# --fps 0 means uncapped, so 600 frames of a light title is well under a second
# - where the old wall-clock bound spent the whole timeout asleep in the frame
# pacer and managed about 300 frames in TEN seconds.  A fixed frame count also
# makes a run identical on a loaded machine and an idle one, which a timeout
# never is.
FRAMES="${NJ_FRAMES:-8000}"
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
# THR and IO come from the port, not from the pixels.  A title that saves a
# high score and one that silently fails to are the same sequence of frames,
# so those two columns are the only way this table can tell them apart.
printf '%-14s %-9s %-8s %-8s %-9s %-4s %-6s %-6s %s\n' TITLE SIZE FRAMES COLOURS CHANGED THR IO GFX RESULT
printf '%-14s %-9s %-8s %-8s %-9s %-4s %-6s %-6s %s\n' ----- ---- ------ ------- ------- --- ---- --- ------

# Keys a title needs to get past its splash screen, as "frame:key" pairs.
#
# A corpus that only watches can prove a title screen draws and nothing more,
# which is a weak claim about a game whose first screen is a logo.  Each entry
# here is a fact about that game, discovered by reading its source, not a
# guess - most of these titles show nothing until something is pressed.
#
#   slimevolley  Command on the RIGHT SOFT KEY (StartCanvas.commandAction)
#   bubblet      no splash screen: the grid is the first frame
#   game15       static puzzle: a grid drawn once, no animation until a key
#   book4-7      single screen, no input needed
declare -A KEYS=(
    [slimevolley]="15:soft1,70:left,140:right"
)
keys_for() {
    printf '%s' "${KEYS[$1]:-}"
}

# Every title that has no script of its own gets this one.
#
# The purpose is T3/T4: a verdict of "flat frame: the game is not drawing" is
# not trustworthy if nothing was ever pressed, because a game waiting on its
# title screen looks exactly like a game that cannot draw.  Sending the whole
# key set - one key per frame, each released the next frame - costs nothing and
# turns "does not draw" into "does not draw even when poked", which is a claim
# the corpus can actually defend.
#
# A title that throws because of this is fine (T3 allows errors); a title that
# crashes or hangs because of it is a real finding, and that is the point.
POKE="${NJ_POKE:-5:fire,6:up,7:down,8:left,9:right,10:soft1,11:soft2,12:1,13:2,14:5,15:star,16:pound,20:fire,21:fire,25:left,30:right,35:fire}"

# The per-run verdict, as a function: the size loop has to be able to ask
# "does it draw HERE?" more than once per title, because whether a title
# draws at all depends on the screen size it was given.
verdict_for() {
    python3 - "${out:-}" "$MIN_COLOURS" "${log:-}" "$STRICT_MOTION" <<'PY'
import glob, sys, hashlib
from collections import Counter
try:
    from PIL import Image
except ImportError:
    print("SKIP|PIL missing"); raise SystemExit

def why(log):
    """Name the concrete thing that is missing, not just "could not start".

    A verdict of "could not start" sends the next person hunting through the
    log by hand, and it hides the difference between a class that was never
    written and an allocation that ran out of room.  Every one of these is now
    a line you can act on.
    """
    import re
    for pat, fmt in (
        (r"NoClassDefFoundError: (\S+)", "missing class %s"),
        (r"NoSuchMethodError: (\S+)", "missing method %s"),
        (r"NoSuchFieldError: (\S+)", "missing field %s"),
        (r"UnsatisfiedLinkError: (\S+)", "unbound native %s"),
        (r"ClassNotFoundException: (\S+)", "class not found %s"),
    ):
        m = re.search(pat, log)
        if m:
            return fmt % m.group(1)
    m = re.search(r"OutOfMemoryError[^\n]*", log)
    if m:
        return m.group(0)[:70]
    m = re.search(r"InternalError: ([^\n]+)", log)
    if m:
        return m.group(1)[:70]
    lines = [l for l in log.splitlines() if l.strip()]
    return (lines[-1] if lines else "no output")[:70]


d, min_colours, logpath = sys.argv[1], int(sys.argv[2]), sys.argv[3]
strict = sys.argv[4] == "1"
log = open(logpath, errors="replace").read()
frames = sorted(glob.glob(d + "/*.ppm"))

if "not a usable MIDlet suite" in log:
    print("FAIL|0|0|0|not a MIDlet suite"); raise SystemExit
if "cannot instantiate" in log or "cannot read the JAR" in log:
    print("FAIL|0|0|0|could not start: %s" % why(log)); raise SystemExit
if "uncaught exception" in log:
    print("FAIL|%d|0|0|%s" % (len(frames), why(log)))
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
    # No claim about WHY it is flat.  Whether paint() asked for nothing or
    # asked for a great deal is measured below and reported as numbers; the
    # verdict line used to assert "the game is not drawing" without having
    # looked, and that phrase sent a reader after a rendering bug that was not
    # there (合金弹头4(完美汉化版)_4053 draws 3 times per paint and is flat).
    print("FAIL|%d|%d|%d|flat frame even after the whole key set was sent" % (len(frames), cols, changed))
    raise SystemExit
if changed < 2:
    tag = "FAIL" if strict else "PASS"
    print("%s|%d|%d|%d|static picture" % (tag, len(frames), cols, changed))
    raise SystemExit
print("PASS|%d|%d|%d|" % (len(frames), cols, changed))
PY
}

for jar in "${JARS[@]}"; do
    title="$(basename "$jar" .jar)"
    KARGS=()
    KS="$(keys_for "$title")"
    if [ -n "$KS" ]; then
        KARGS=(--keys "$KS")
    elif [ -n "$POKE" ]; then
        KARGS=(--keys "$POKE")
    fi
    out="$(mktemp -d)"
    log="$out/log.txt"

    # One run of this title at the size already chosen in $size, dumping every
    # $1 frames.  It is a function because a title that ends inside the first
    # dump interval has to be run again with a dump every frame - and a second
    # hand-written copy of this command line is a second thing to get wrong.
    run_here() {
        : > "$log"
        rm -f "$out"/*.ppm
        timeout "$SECS" "$BIN" --port headless --heap "$HEAP" --size "$size" \
                --fps "$FPS" --run-frames "$FRAMES" --stats \
                --dump-frames "$out/" --dump-every "$1" \
                "${KARGS[@]}" \
                "$jar" >"$log" 2>&1
    }

    used=""
    tried=""
    for size in $SIZES; do
        run_here 40
        if grep -q "Screen too" "$log"; then
            rm -f "$out"/*.ppm
            continue        # this title rejects this size; try the next
        fi
        used="$size"
        tried="$tried$size "

        # Does it actually draw HERE?  Stopping at the first size that merely
        # starts is how a size-dependent game gets written off as "the game is
        # not drawing": 宠物王国2紫_5550 renders at 240x320 and is a single
        # black colour at 176x208, and SIZES lists 176x208 first.  So a verdict
        # of "flat" or "could not start" is not final while sizes remain.
        verdict="$(verdict_for)"
        if [ "${verdict#*nothing was ever presented}" != "$verdict" ]; then
            # Nothing was dumped, which is not the same as nothing drawn: these
            # titles finish in a handful of frames and the smoke key set often
            # ends them.  Seven Apress demos went from PASS to FAIL the moment
            # the keys arrived, because a 1-in-40 dump interval sampled none of
            # the three frames they run for.  Ask again, this time every frame.
            run_here 1
            verdict="$(verdict_for)"
        fi
        case "$verdict" in
            PASS*) break ;;
            *"flat frame"*|*"could not start"*) continue ;;
            *) break ;;
        esac
    done
    if [ -z "$used" ]; then
        if [ -n "$SIZE" ]; then :; fi
        used="(no size accepted)"
    fi

    # Read the port's own report BEFORE deleting the temp dir it lives in.
    # It did live after the rm once, and every title then reported "?" - which
    # looked like a missing observation rather than a missing file.
    obs="$(grep -m1 '^observed:' "$log" 2>/dev/null || true)"
    thr="$(printf '%s' "$obs" | sed -n 's/.*threads=\([0-9-]*\).*/\1/p')"
    io="$(printf '%s' "$obs" | sed -n 's/.*file_writes=\([0-9-]*\).*/\1/p')"
    [ -n "$thr" ] || thr='?'
    [ -n "$io" ] || io='?'

    # How many drawing calls the game made.  Without it, every flat frame reads
    # the same, and the two situations it covers need opposite work: gfx=0
    # means paint() asked for nothing (the game is waiting for something), and
    # gfx>0 means it drew and the pixels did not survive to the port.
    gfx="$(sed -n 's/.*[^a-z]gfx=\([0-9]*\).*/\1/p' "$log" 2>/dev/null | head -1)"
    [ -n "$gfx" ] || gfx='?'
    paints="$(sed -n 's/.*[^a-z]paints=\([0-9]*\).*/\1/p' "$log" 2>/dev/null | head -1)"
    [ -n "$paints" ] || paints='?'
    # Where the title first threw.  A failing line that says "flat frame" names
    # a symptom; "first throw: NullPointerException at DCTuxedo.i()V pc=26"
    # names the call that did not work, which is what has to be chased.
    thrown="$(sed -n 's/^thrown: //p' "$log" 2>/dev/null | head -1)"
    rm -rf "$out"

    IFS='|' read -r status nframes ncols nchanged note <<<"$verdict"
    # Say which of the two flat-frame situations this is, in the line itself.
    case "$note" in
        *"flat frame"*)
            case "$gfx" in
                '?') note="$note; the run printed no gfx= count" ;;
                0)   note="$note; paint() made no Graphics call at all" ;;
                *)   note="$note; $gfx Graphics calls over $paints paint() calls left the picture unchanged" ;;
            esac
            ;;
    esac
    case "$status" in
        PASS|SKIP) ;;
        *) [ -n "$thrown" ] && note="$note; first throw: $thrown" ;;
    esac
    printf '%-14s %-9s %-8s %-8s %-9s %-4s %-6s %-6s %s\n' "$title" "$used" "$nframes" "$ncols" \
           "$nchanged" "$thr" "$io" "$gfx" "$status${note:+ ($note)}"
    if [ "$status" = "PASS" ] || [ "$status" = "SKIP" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done

echo
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ] || exit 1
