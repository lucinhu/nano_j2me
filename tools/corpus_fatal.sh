#!/usr/bin/env bash
#
# Count only the failures that MATTER in a corpus run.
#
# The runtime prints `thrown: <class> at <method> pc=N` for every throw it
# performs, including exceptions a MIDlet catches and recovers from.  A sweep
# that counts that output reports hundreds of "errors" for titles that are
# perfect - and, worse, it reports a title that got FURTHER as a regression
# (an NPE inside a retry loop prints once per frame).
#
# A title has really failed only when the process exits non-zero, or when an
# exception escapes into the frame loop.  This script reports exactly those.
#
#   tools/corpus_fatal.sh <nj2me-binary> [games-dir] [parallel]
#
# Exit status is 0 when every title ran, 1 when at least one failed (so CI can
# use it directly).
set -u

BIN="${1:-}"
GAMES="${2:-tests/games}"
JOBS="${3:-8}"

if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "usage: $0 <nj2me-binary> [games-dir] [parallel]" >&2
    exit 2
fi
if [ ! -d "$GAMES" ]; then
    echo "$0: no such games directory: $GAMES" >&2
    exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/rms" "$WORK/log"

one()
{
    jar="$1"
    name="$(basename "$jar" .jar)"
    log="$WORK/log/$name.log"
    # A save directory that exists: a missing one makes every record store
    # call throw, which turns working titles into silent black frames.  Each
    # title gets its OWN directory: one shared directory let titles with the
    # same store name (most call it "config") read each other's records, so a
    # title could fail on another game's data - 3D台球之王_5014 exited 1 in the
    # shared-directory sweep and runs clean when isolated.  That was the
    # harness lying, not the runtime.
    mkdir -p "$WORK/rms/$name"
    timeout 30 "$BIN" --port headless --heap 16 --run-frames 150 \
        --size 176x208 --fps 30 --rms-dir "$WORK/rms/$name" "$jar" \
        > "$log" 2>&1
    rc=$?
    escaped="$(grep -c "exception escaped into the frame loop" "$log")"
    printf '%s\t%s\t%s\n' "$rc" "$escaped" "$name"
}
export -f one
export BIN WORK

find "$GAMES" -maxdepth 1 -name '*.jar' -print0 \
    | xargs -0 -P "$JOBS" -I{} bash -c 'one "$@"' _ {} \
    | sort > "$WORK/results.tsv"

# A title can be known-failing for a reason that is not this runtime's: a
# truncated download, a platform gate that needs --device, or its own NPE
# inside a callback.  Those live in tools/corpus_fatal_known.txt with a reason
# each, so the gate stays usable; a title NOT on that list is always fatal,
# and the list is a baseline, not a place to hide a new failure.
KNOWN="${NJ_CORPUS_KNOWN_FATAL:-$(dirname "$0")/corpus_fatal_known.txt}"
known_match()
{
    [ -f "$KNOWN" ] || return 1
    awk -F '\t' -v n="$1" '!/^#/ && NF >= 1 && $1 == n { found = 1 } END { exit !found }' "$KNOWN"
}

total="$(wc -l < "$WORK/results.tsv")"
fatal=0
known=0
while IFS="$(printf '\t')" read -r rc escaped name; do
    if [ "$rc" != "0" ] || [ "$escaped" != "0" ]; then
        if known_match "$name"; then
            known=$((known + 1))
            printf 'known %-14s rc=%-3s escaped=%s\n' "$name" "$rc" "$escaped"
        else
            fatal=$((fatal + 1))
            printf 'FATAL %-14s rc=%-3s escaped=%s\n' "$name" "$rc" "$escaped"
        fi
    fi
done < "$WORK/results.tsv"

printf 'fatal %d / %d, known %d  (heap 16, 150 frames, fps 30)\n' \
    "$fatal" "$total" "$known"
[ "$fatal" = "0" ]
