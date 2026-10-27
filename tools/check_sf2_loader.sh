#!/bin/sh
# 街头霸王2冠军典藏版_1812 - the sound-on/off prompt answered with a soft key
# ("yes") starts the title's asset loader, and that loader's state machine
# lives inside paint() and sleeps between asset reads.
#
# The runtime used to pump a whole nested frame from inside that Thread.sleep()
# (the frame pump in vm_invoke() exists so a MIDlet that runs its own loop in
# startApp() still presents).  The nested nj_midp_frame() cleared
# vm->painted_in_frame on its way out, so the very next pump iteration saw an
# unpainted frame and re-entered paint() re-entrantly.  The title's loader
# was then advanced twice per pass: it skipped the step that fills the sample
# table (field dB), the next frame read that NULL buffer, paint() threw
# NullPointerException, and the title's own catch handler is an empty infinite
# loop - the run hangs until an external timeout (rc 124).
#
# The fix makes a paint atomic: nj_midp_paint() sets vm->paint_active for the
# whole call (blocking included) and nj_midp_frame() refuses a pumped frame
# while that is set.  Frames pumped while no paint is executing are untouched,
# so a MIDlet that runs its own loop in startApp() or a parked TimerTask still
# gets presented (see tools/check_timer_wait.sh).
#
# The budget below is 600 on purpose: 550 frames passes even with the defect
# present, 600 hangs (the loader reaches its first sample read around frame
# 560).  With the atomic paint the run reaches the main title screen and the
# budget ends it cleanly.  The mechanism is written down in
# docs/notes/2026-10-27-street-fighter-loader-hang.md.
#
# Usage: check_sf2_loader.sh <nj2me-binary> [games-directory]
set -u

bin=${1:-}
games=${2:-tests/games}
if [ -z "$bin" ] || [ ! -x "$bin" ]; then
    echo "usage: $0 <nj2me-binary> [games-directory]" >&2
    exit 2
fi

jar=
for f in "$games/街头霸王2冠军典藏版_1812"*.jar; do
    [ -f "$f" ] && jar=$f && break
done
if [ -z "$jar" ]; then
    echo "SKIP street_fighter_loader (no fixture 街头霸王2冠军典藏版_1812*.jar)"
    exit 77
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/njsf2.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT

log=$tmp/run.log
"$bin" --port headless --heap 16 --size 176x208 --fps 30 \
       --run-frames 600 --keys "400:soft1" \
       --rms-dir "$tmp/rms" "$jar" >"$log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL street_fighter_loader: exit $rc, expected 0"
    echo "     (the loader re-entered paint() and hung; see the note)"
    tail -20 "$log"
    exit 1
fi
if grep -qF "NullPointerException" "$log"; then
    echo "FAIL street_fighter_loader: the run threw NullPointerException"
    grep -F "NullPointerException" "$log" | head -5
    exit 1
fi
if ! grep -qF "running NewFighter" "$log"; then
    echo "FAIL street_fighter_loader: the MIDlet never started"
    tail -20 "$log"
    exit 1
fi
if ! grep -qF "the frame budget ran out" "$log"; then
    echo "FAIL street_fighter_loader: the run did not end on the frame budget"
    tail -20 "$log"
    exit 1
fi

echo "OK   street_fighter_loader (600 frames, loader survived, no NPE)"
