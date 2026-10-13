#!/usr/bin/env bash
#
# Did the speed control change how fast the MIDlet's clock runs?
#
# SpeedDemo sleeps a fixed amount of MIDlet time and exits, so the same jar
# under NJ_SDL2_SPEED=100 / 300 / 50 has to take ~1x / ~0.33x / ~2x the wall
# time.  This is the check that catches a badge which updates a number nobody
# reads: the MIDlet's own clock always measures 3000 ms, so only the process's
# wall time can tell the two apart.
#
#   tests/speeddemo/run.sh [jar]
#
# NJ_BIN picks the runner (default build/nj2me).
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/speeddemo.jar}"

[ -x "$BIN" ] || { echo "FAIL no runner at $BIN (build it, or set NJ_BIN)" >&2; exit 2; }
[ -f "$JAR" ] || { echo "FAIL no fixture at $JAR (run tests/speeddemo/build.sh)" >&2; exit 2; }

# run_at <percent>: the port's own log lines go to stderr, the wall time in
# milliseconds to stdout.
run_at() {
    local log t0 t1
    log="$(mktemp)"
    t0="$(date +%s%N)"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy NJ_SDL2_SPEED="$1" \
        "$BIN" --port sdl2 --run-frames 2000 "$JAR" >"$log" 2>&1 || true
    t1="$(date +%s%N)"
    grep -h "speed: MIDlet time runs" "$log" | tail -1 | sed 's/^/    /' >&2
    grep -h "SPEED slept" "$log" | sed 's/^/    /' >&2
    rm -f "$log"
    echo $(( (t1 - t0) / 1000000 ))
}

echo "== 100% (real time) ==" >&2
t100="$(run_at 100)"
echo "== 300% ==" >&2
t300="$(run_at 300)"
echo "== 50% ==" >&2
t50="$(run_at 50)"

echo
echo "wall time: 100% = ${t100} ms   300% = ${t300} ms   50% = ${t50} ms"

fail=0
# Generous bounds: process start-up is inside every number, so the ratios are
# never as sharp as 3.00x and 2.00x.
if [ "$t300" -ge "$(( t100 * 2 / 3 ))" ]; then
    echo "FAIL 300% was not at least 1.5x faster than 100%"; fail=1
fi
if [ "$t50" -le "$t100" ]; then
    echo "FAIL 50% was not slower than 100%"; fail=1
fi
[ "$fail" = 0 ] && echo "PASS the MIDlet clock follows the port's speed setting"
exit "$fail"
