#!/bin/bash
# The SDL2 port's save/reload round trip, run HEADLESSLY.
#
# The port has had an F5 snapshot key since the speed badge landed, but that
# path can only be exercised with a real window and a real key event.  What the
# runner offers instead is the same core calls on the command line:
#
#   --save-state FILE --save-state-frame N   nj_vm_save_file() at a frame
#                                            boundary, in the middle of the run
#   --load-state FILE                        nj_vm_load_file() before the
#                                            MIDlet is constructed
#
# So this script runs the SDL2 port twice with the dummy video and audio
# drivers - no window, no display - and asserts that the second process
# RESUMED the MIDlet instead of restarting it:
#
#   * fielddemo.jar prints exactly one "FIELD before=" banner, in startApp();
#     the reloaded run prints none, because startApp() never ran again.  That
#     absence is the whole point of the check: a run that restarts prints the
#     banner a second time.
#   * the reloaded run says "resumed FieldDemo from <file>" instead of the
#     runner's "cannot resume from <file>; starting the MIDlet instead".
#
# A snapshot that cannot be used must not be fatal - a stale or foreign file
# costs the caller the convenience, not the run - so a third process is given a
# file that is not a snapshot at all and must start the MIDlet normally.
#
# The port's own F5 key is NOT covered here: it needs an SDL key event, which
# the dummy video driver never delivers.  The interactive check (F5 wrote the
# file, the log said "state: save to ... -> 0") and the fact that such a file
# loads through this same --load-state path were verified by hand; a headless
# test can only reach the path through the CLI flags above.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/fielddemo.jar}"
[ -f "$JAR" ] || { echo "SKIP: no fixture at $JAR"; exit 0; }
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 0; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STATE="$WORK/sdl.state"

fail=0
say() {
    if [ "$1" = 1 ]; then
        echo "FAIL $2"
        fail=1
    else
        echo "ok   $2"
    fi
}

# The dummy drivers are the point: this must work with no window at all.
run() {
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 "$BIN" "$@" 2>&1
}

echo "== 1. save: run the MIDlet and snapshot it at frame 5 of 30"
save_out="$(run --port sdl2 --run-frames 30 \
                --save-state "$STATE" --save-state-frame 5 "$JAR")"
save_rc=$?
echo "$save_out" | sed 's/^/   | /'
echo

[ "$save_rc" = 0 ]; say $? "the saving run exits cleanly (exit $save_rc)"

n_field="$(printf '%s\n' "$save_out" | grep -c 'FIELD before=')"
[ "$n_field" = 1 ]; say $? "the MIDlet started once and printed its banner ($n_field banner(s))"

case "$save_out" in
    *"saved state to $STATE after 5 frame(s)"*)
        say 0 "the snapshot was written at the requested frame" ;;
    *) say 1 "the runner did not report writing the snapshot at frame 5" ;;
esac

if [ -s "$STATE" ]; then
    say 0 "the snapshot file exists and is not empty ($(wc -c <"$STATE") bytes)"
else
    say 1 "no snapshot file at $STATE"
fi

echo
echo "== 2. reload: start a SECOND process on that snapshot"
load_out="$(run --port sdl2 --run-frames 30 --load-state "$STATE" "$JAR")"
load_rc=$?
echo "$load_out" | sed 's/^/   | /'
echo

[ "$load_rc" = 0 ]; say $? "the reloading run exits cleanly (exit $load_rc)"

case "$load_out" in
    *"resumed FieldDemo from $STATE"*)
        say 0 "the snapshot was loaded and the VM resumed" ;;
    *) say 1 "the runner did not report resuming from $STATE" ;;
esac

n_field="$(printf '%s\n' "$load_out" | grep -c 'FIELD before=')"
[ "$n_field" = 0 ]; say $? \
    "startApp() did not run again ($n_field banner(s); a restarted run prints one)"

case "$load_out" in
    *"cannot resume"*) say 1 "the snapshot was refused" ;;
    *) say 0 "the snapshot was not refused" ;;
esac

case "$load_out" in
    *"run ended"*) say 0 "the resumed run ended on its own terms" ;;
    *) say 1 "the resumed run did not report how it ended" ;;
esac

echo
echo "== 3. a file that is not a snapshot must not cost the caller the run"
printf 'not a snapshot\n' > "$WORK/garbage.state"
bad_out="$(run --port sdl2 --run-frames 10 --load-state "$WORK/garbage.state" "$JAR")"
bad_rc=$?
echo "$bad_out" | sed 's/^/   | /'
echo

[ "$bad_rc" = 0 ]; say $? "the run still exits cleanly (exit $bad_rc)"

case "$bad_out" in
    *"cannot resume from $WORK/garbage.state"*)
        say 0 "the bad file was refused, loudly" ;;
    *) say 1 "a file that is not a snapshot was not reported" ;;
esac

n_field="$(printf '%s\n' "$bad_out" | grep -c 'FIELD before=')"
[ "$n_field" = 1 ]; say $? \
    "the MIDlet started normally instead ($n_field banner(s))"

echo
[ "$fail" = 0 ] && echo "PASS: the SDL2 port resumes a snapshot instead of restarting the MIDlet"
exit "$fail"
