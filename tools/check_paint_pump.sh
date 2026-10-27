#!/bin/sh
# 纽约人生_1339 - a title whose paint() blocks while its loader reads assets.
#
# The frame pump in vm_invoke() runs a nested nj_midp_frame() while a Java
# thread is parked in a blocking native, so a MIDlet that runs its own loop in
# startApp() (or a parked TimerTask) still presents.  If the parked thread is
# inside paint(), the nested frame must not deliver another paint() - that is
# the re-entrancy that hung 街头霸王2冠军典藏版_1812 (see
# tools/check_sf2_loader.sh).  But refusing the whole frame was too much: this
# title keeps its loader inside paint() across a blocking read, and
# run_frame_hook() counts every pumped frame towards the run budget, so the
# refusals spent the entire budget and the run presented a single picture.  The
# corpus then recorded "nothing was ever presented" for a title that was still
# loading normally.
#
# The fix lets the nested frame run (input, scheduler, present) and only skips
# the paint while vm->paint_active is set.  This title then presents frames at
# the pace of the loop and the run ends on its budget instead of on a
# one-frame wall-clock sprint.
#
# The bound below is 180 of 200 frames: a healthy run ends at frames=200+,
# the refused-frame defect ended at frames=1 in about 10 ms.  The budget is
# 200 (not 600) so the test stays a few seconds long at 30 fps.
#
# Usage: check_paint_pump.sh <nj2me-binary> [games-directory]
set -u

bin=${1:-}
games=${2:-tests/games}
if [ -z "$bin" ] || [ ! -x "$bin" ]; then
    echo "usage: $0 <nj2me-binary> [games-directory]" >&2
    exit 2
fi

jar=
for f in "$games/纽约人生_1339"*.jar; do
    [ -f "$f" ] && jar=$f && break
done
if [ -z "$jar" ]; then
    echo "SKIP paint_pump (no fixture 纽约人生_1339*.jar)"
    exit 77
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/njpaintpump.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT

log=$tmp/run.log
"$bin" --port headless --heap 16 --size 320x240 --fps 30 \
       --run-frames 200 --stats "$jar" >"$log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL paint_pump: exit $rc, expected 0"
    tail -20 "$log"
    exit 1
fi
if ! grep -qF "the frame budget ran out" "$log"; then
    echo "FAIL paint_pump: the run did not end on the frame budget"
    tail -20 "$log"
    exit 1
fi
if grep -qF "never produced a frame" "$log"; then
    echo "FAIL paint_pump: the title never presented a frame"
    tail -20 "$log"
    exit 1
fi
frames=$(sed -n 's/.*stats: frames=\([0-9][0-9]*\).*/\1/p' "$log" | head -1)
if [ -z "$frames" ]; then
    echo "FAIL paint_pump: no stats line in the log"
    tail -20 "$log"
    exit 1
fi
if [ "$frames" -lt 180 ]; then
    echo "FAIL paint_pump: only $frames frames presented (expected at least 180)"
    echo "     (a refused pump spends the budget; see the note in nj_lib_midlet.c)"
    grep -E "stats:|run ended" "$log" | head -3
    exit 1
fi

echo "OK   paint_pump (200 frames budget, $frames frames presented)"
