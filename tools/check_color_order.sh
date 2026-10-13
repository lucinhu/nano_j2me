#!/usr/bin/env bash
#
# The colour-order gate.
#
# A 16-bit pixel is handed over three times before it reaches a panel - the
# rasteriser writes it, the port expands it into the window buffer, the
# compositor scans it out - and red and blue can be swapped at the last of
# those without anything earlier noticing.  The frame the runtime dumps is
# right, the panel is wrong, and "it looks wrong on my phone" is the only
# report that ever arrives.
#
# So: paint pure primaries, in a known order, and read them back.
#
#   tools/check_color_order.sh                     # headless frame dump
#   tools/check_color_order.sh /path/screen.png    # a device screenshot
#
# The device form is the one that matters for a port (the screenshot can only
# have come from the panel path); the headless form is what says the fixture
# and the runtime are not the problem when it fails.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ "$#" -ge 1 ]; then
    exec python3 "$ROOT/tools/sample_screenshot.py" "$1" --expect red,green,blue
fi

JAR="$ROOT/tests/fixtures/colordemo.jar"
RUNNER="$ROOT/build/nj2me"
if [ ! -x "$RUNNER" ]; then
    echo "check_color_order: build/nj2me is missing - build the host first" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    "$ROOT/tools/make_colordemo_jar.sh" >/dev/null
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
"$RUNNER" --port headless --size 240x320 --heap 8 --fps 0 \
    --run-frames 3 --dump-frames "$WORK" --dump-every 1 "$JAR" >/dev/null

FRAME="$(ls "$WORK"/*.ppm | tail -1)"
exec python3 "$ROOT/tools/sample_screenshot.py" "$FRAME" --expect red,green,blue
