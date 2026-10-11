#!/usr/bin/env bash
#
# A benchmark that can actually resolve a difference.
#
#     tools/bench.sh [runs] [jar]
#
# ---------------------------------------------------------------------------
# WHY THIS EXISTS
# ---------------------------------------------------------------------------
# The first two optimisation attempts in this project were judged on WALL CLOCK
# time over a 0.19 s run, and the run-to-run spread of that measurement is
# 12-13%.  Anything worth less than about 15% is invisible in it, which makes
# every conclusion drawn from it unfalsifiable - in either direction.  "No
# measurable gain" and "no gain" are not the same claim, and the old method
# could not tell them apart.
#
# Three changes fix it:
#
#   * CPU TIME, not wall clock.  The bench is single-threaded and pure
#     computation, so user time is exactly the work done; wall clock also
#     measures whatever else the machine was doing.
#   * CPU PINNING with taskset.  A process that migrates between cores pays for
#     cache it no longer has, which is most of the 12%.
#   * THE MINIMUM of N runs, not the mean.  Noise only ever adds time, so the
#     fastest run is the closest thing to the machine's actual speed.  A mean
#     reports how busy the machine was as much as how fast the code is.
#
# With all three the spread falls to about 2.4%, so a 5% change is visible and a
# 10% change is unambiguous.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNS="${1:-8}"
JAR="${2:-$ROOT/tests/fixtures/bench.jar}"
BIN="${NJ_BENCH_BIN:-$ROOT/build/nj2me}"
CPU="${NJ_BENCH_CPU:-2}"
[ -x "$BIN" ] || { echo "no binary at $BIN" >&2; exit 1; }
[ -f "$JAR" ] || { echo "no fixture at $JAR" >&2; exit 1; }

export TIMEFORMAT='%3U'
times_file="$(mktemp)"
trap 'rm -f "$times_file"' EXIT

# `|| true` because a MIDlet with no Displayable exits 1 by design, and an
# unrelated exit code must not abort the benchmark under `set -e`.
for _ in $(seq "$RUNS"); do
    { time taskset -c "$CPU" "$BIN" --port headless --heap 16 --fps 0 \
        --stats "$JAR" >/dev/null 2>&1 || true; } 2>>"$times_file"
done

BC="$("$BIN" --port headless --heap 16 --fps 0 --stats "$JAR" 2>&1 \
      | sed -n 's/.*bytecodes=\([0-9]*\).*/\1/p' | head -1 || true)"
[ -n "$BC" ] || BC=0

sort -n "$times_file" | awk -v bc="$BC" -v n="$RUNS" '
    { a[NR] = $1 }
    END {
        min = a[1]; med = a[int((NR + 1) / 2)]; max = a[NR]
        printf "runs %d   bytecodes %s\n", n, bc
        printf "cpu  min %.3fs  med %.3fs  max %.3fs  spread %.1f%%\n",
               min, med, max, (max - min) * 100.0 / min
        if (min > 0 && bc > 0)
            printf "min-run throughput  %.1f M bytecodes/s\n", bc / min / 1e6
    }'
