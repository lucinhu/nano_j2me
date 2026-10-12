#!/usr/bin/env bash
#
# Does input actually REACH the game?
#
# The corpus script can only claim "it started, it drew something".  A title
# that is wedged on a splash screen passes that.  This tool asks the stronger
# question for any title, with no per-game script to write:
#
#   run A   no input, N frames          -> frame checksum A1
#   run C   no input again              -> frame checksum C
#   run B   input mashed from frame K   -> frame checksum B
#
#   A1 != C   the title is not deterministic here; the comparison cannot say
#             anything, so it is reported INCONCLUSIVE rather than guessed at
#   B  == A1  the input changed nothing that reaches the screen -> FAIL
#   B  != A1  the input reached the game                        -> PASS
#
# The middle run is what makes the answer mean something.  An animated title
# differs between any two frames anyway, so without it "B != A1" would be a
# statement about the animation, not about the input - exactly the false
# positive that a single before/after comparison invites.
#
# Crash and hang are fatal on every run: "an error is fine, a deadlock is not".
#
# Usage:
#   tools/check_input_effect.sh [--dir DIR] [--frames N] [--timeout SECS]
#                              [--jobs N] [--verbose] [JAR ...]
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
DIR="${NJ_GAMES:-$ROOT/tests/games}"
# Titles gate themselves on the screen size, so the size is probed per title
# rather than assumed; the same list the corpus runner uses.
SIZES="${NJ_SIZES:-176x208 240x320 128x160 320x240}"
FRAMES="${NJ_FRAMES:-1200}"
TMO="${NJ_TMO:-25}"
JOBS="${NJ_JOBS:-4}"
VERBOSE=0
SIZE=""
JARS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --dir)     DIR="$2"; shift 2 ;;
        --frames)  FRAMES="$2"; shift 2 ;;
        --timeout) TMO="$2"; shift 2 ;;
        --jobs)    JOBS="$2"; shift 2 ;;
        --verbose) VERBOSE=1; shift ;;
        -*)        echo "unknown option: $1" >&2; exit 2 ;;
        *)         JARS+=("$1"); shift ;;
    esac
done

[ -x "$BIN" ] || { echo "no emulator at $BIN" >&2; exit 2; }

# Keys are pressed every KEYGAP frames from INKEY onwards, so a title gets
# long enough on its splash or title screen to have drawn something stable
# first, then a steady stream of everything a 12-key pad can send.  The set
# covers the navigation keys, both soft keys and the game/numeric keys,
# because a title that ignores `fire` may still respond to `5`.
INKEY="${NJ_INKEY:-300}"
KEYGAP="${NJ_KEYGAP:-18}"
KEYNAMES=(up down left right fire soft1 soft2 1 2 3 4 5 6 7 8 9 0 star pound a b c d)

mask_keys() {
    local f=$INKEY i=0 k
    : > "$1"
    while [ "$f" -lt "$FRAMES" ]; do
        k="${KEYNAMES[$((i % ${#KEYNAMES[@]}))]}"
        printf '%d:%s\n' "$f" "$k" >> "$1"
        f=$((f + KEYGAP))
        i=$((i + 1))
    done
}

# One run.  Echoes "checksum" on success, or "FAIL:<why>".
run_once() {   # run_once <jar> <tag> <keyscript-or-empty>
    local jar=$1 tag=$2 keys=$3 out log rc sum
    out="$(mktemp -d)"; log="$(mktemp)"
    local args=(--port headless --heap 8 --size "$SIZE" --fps 0
                --run-frames "$FRAMES" --dump-frames "$out/" --dump-every 40)
    # ALWAYS pass a key script, even an empty one.  A build that takes a
    # different frame-loop path when keys are scripted would otherwise make the
    # control runs differ from the input run for a reason that has nothing to
    # do with the input, and the comparison would be measuring the harness.
    args+=(--keys "@$keys")

    timeout "$TMO" "$BIN" "${args[@]}" "$jar" >"$log" 2>&1
    rc=$?

    if [ "$rc" -eq 124 ]; then echo "FAIL:hang (no exit within ${TMO}s)"; rm -rf "$out" "$log"; return; fi
    if grep -qi "uncaught exception" "$log"; then
        echo "FAIL:$(grep -i 'uncaught exception' "$log" | head -1 | cut -c1-70)"
        rm -rf "$out" "$log"; return
    fi

    sum="$(python3 - "$out" <<'PY'
import glob, hashlib, sys
f = sorted(glob.glob(sys.argv[1] + "/*.ppm"))
print("none" if not f else hashlib.sha256(open(f[-1], "rb").read()).hexdigest()[:16])
PY
)"
    if [ "$VERBOSE" = 1 ]; then
        printf '    %-4s rc=%-3s frames=%-4s %s\n' "$tag" "$rc" \
            "$(ls "$out"/*.ppm 2>/dev/null | wc -l)" "$sum" >&2
    fi
    rm -rf "$out" "$log"
    if [ "$sum" = "none" ]; then echo "FAIL:nothing was ever presented"; return; fi
    echo "$sum"
}

# The first size this title does not reject.  A title that gates on the screen
# size presents nothing at a size it dislikes, which would otherwise be
# reported as "input never reached the screen" - a wrong answer to the
# question being asked.
pick_size() {
    local jar=$1 sz log
    for sz in $SIZES; do
        log="$(mktemp)"
        timeout "$TMO" "$BIN" --port headless --heap 8 --size "$sz" --fps 0 \
                --run-frames 60 "$jar" >"$log" 2>&1
        if ! grep -q "Screen too" "$log"; then
            rm -f "$log"; echo "$sz"; return
        fi
        rm -f "$log"
    done
    echo "${SIZES%% *}"
}

check_one() {
    local jar=$1 title base ks a1 c b verdict
    base="$(mktemp -d)"
    title="$(basename "$jar" .jar)"
    SIZE="$(pick_size "$jar")"
    ks="$base/keys.txt"
    mask_keys "$ks"

    : > "$base/empty.txt"
    a1="$(run_once "$jar" A "$base/empty.txt")"
    c="$(run_once  "$jar" C "$base/empty.txt")"
    b="$(run_once  "$jar" B "$ks")"
    rm -rf "$base"

    case "$a1$c$b" in
        *FAIL:*) verdict="$a1$c$b"; verdict="${verdict#*FAIL:}"; verdict="FAIL:${verdict%%FAIL:*}" ;;
        *)
            if [ "$a1" != "$c" ]; then
                verdict="INCONCLUSIVE:not deterministic without input ($a1 vs $c)"
            elif [ "$b" = "$a1" ]; then
                verdict="FAIL:input never reached the screen (${#KEYNAMES[@]} keys sent, frame unchanged)"
            else
                verdict="PASS"
            fi
            ;;
    esac
    printf '%s|%s\n' "$title" "$verdict"
    case "$verdict" in PASS|INCONCLUSIVE*) return 0 ;; *) return 1 ;; esac
}

if [ "${#JARS[@]}" -eq 0 ]; then
    shopt -s nullglob
    JARS=("$DIR"/*.jar)
    shopt -u nullglob
fi
[ "${#JARS[@]}" -gt 0 ] || { echo "no JARs under $DIR" >&2; exit 2; }

printf 'input-effect check: %d title(s), %d frames, keys from %d every %d\n' \
    "${#JARS[@]}" "$FRAMES" "$INKEY" "$KEYGAP"
pass=0; fail=0; incon=0
for jar in "${JARS[@]}"; do
    # Run once: check_one is three emulator runs, so calling it twice to
    # classify the result would triple the cost of the whole sweep.
    v="$(check_one "$jar")"
    t="${v%%|*}"; r="${v#*|}"
    printf '%-46s %s\n' "${t:0:45}" "$r" >&2
    case "$r" in
        PASS)          pass=$((pass+1)) ;;
        INCONCLUSIVE*) incon=$((incon+1)) ;;
        *)             fail=$((fail+1)) ;;
    esac
done

echo
echo "reached the game: $pass   failed: $fail   inconclusive: $incon"
[ "$fail" -eq 0 ] || exit 1
