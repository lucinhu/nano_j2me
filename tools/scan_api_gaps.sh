#!/bin/sh
# What the titles say the runtime is missing.
#
# The corpus sweep answers "did the screen change?"; it does not answer "what
# did the title ask for and not get?".  A missing method is not a link error in
# this runtime - the call resolves up the hierarchy, finds nothing, and throws
# NoSuchMethodError inside the title, which then dies or takes a fallback path.
# The wrapper round started exactly there: 模拟人生2_10762 asked
# java.lang.Integer for an instance toString(), got java.lang.Object's identity
# string, and built the resource name "global_data/java.lang.Integer@c1803e7".
#
# This walks every collected title with a short frame budget, greps the log for
# the errors that name a missing member, and prints one line per hit with the
# title in front, so the list can be sorted and counted:
#
#   tools/scan_api_gaps.sh | cut -d: -f2- | sort | uniq -c | sort -rn | head -40
#
# NJ_FRAMES (default 400), NJ_BIN, NJ_OUT (default /tmp/api-gaps.txt).
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
FRAMES="${NJ_FRAMES:-400}"
OUT="${NJ_OUT:-/tmp/api-gaps.txt}"
GAMES="${NJ_GAMES:-$ROOT/tests/games}"

[ -x "$BIN" ] || { echo "no emulator at $BIN (build it first)" >&2; exit 1; }
[ -d "$GAMES" ] || { echo "no titles in $GAMES" >&2; exit 1; }

: > "$OUT"
count=0
for jar in "$GAMES"/*.jar; do
    name=$(basename "$jar" .jar)
    log=$(timeout 60 "$BIN" --port headless --heap 16 --size 240x320 \
            --fps 0 --run-frames "$FRAMES" --stats "$jar" 2>&1)
    hits=$(printf '%s\n' "$log" | grep -E \
        'NoSuchMethodError|NoSuchFieldError|ClassNotFoundException|UnsatisfiedLinkError|AbstractMethodError' \
        | sed "s/^/$name: /")
    if [ -n "$hits" ]; then
        printf '%s\n' "$hits" >> "$OUT"
        count=$((count + 1))
    fi
done

echo "titles with a missing member: $count"
echo "lines written to $OUT"
