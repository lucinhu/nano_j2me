#!/bin/sh
#
# m3g_fuzz2_state_evidence.sh - re-runnable evidence for the round-5 M3G fix
# that changed a builtin class's superclass (AnimationTrack now extends
# Object3D, per JSR 184) and the bounded migration in src/core/nj_save.c that
# keeps states saved by older runtimes loadable.
#
# The saved-state format records class shape (instance_size, field/method
# counts, an ABI hash).  Correcting AnimationTrack's superclass grew its
# instance_size, so a state written by a legacy runtime would be refused
# outright.  nj_vm_load now accepts exactly one legacy shape: the class grew,
# every other declaration is identical, the ABI hash is recomputed with the
# old field offsets, and the state is refused if it actually holds an instance
# of the migrated class (nothing may dereference the old offsets).
#
# This script proves all three paths end to end:
#   1. a legacy state holding an AnimationTrack instance is refused with the
#      explicit diagnostic;
#   2. a fresh (new-format) state still saves and resumes;
#   3. optionally, a real legacy world state with no AnimationTrack instance
#      migrates and resumes (needs the untouched original state).
#
# Usage:
#   tools/m3g_fuzz2_state_evidence.sh <new-nj2me> <legacy-nj2me> \
#       [<legacy-world-state> [<minecraft-jar>]]
#
#   <new-nj2me>           build/nj2me of a tree that has both the AnimationTrack
#                         fix and the nj_save.c migration.
#   <legacy-nj2me>        a build of a pre-fix tree (AnimationTrack still
#                         extends java/lang/Object); used only to WRITE a
#                         legacy state.  The main tree's build works while the
#                         fix is not yet merged there.
#   <legacy-world-state>  optional: an old saved state whose class table has
#                         AnimationTrack linked but no saved instance of it
#                         (the Minecraft saved world in this workspace is one).
#                         The file is copied - the original is never touched.
#   <minecraft-jar>       the JAR that goes with <legacy-world-state>.
#
# Exit status: 0 when every run check passes, 1 otherwise.  Logs stay in a
# temporary directory that is printed on failure.
#
set -u

usage() {
    sed -n '2,52p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

[ $# -ge 2 ] || usage
NEW=$1
LEGACY=$2
OLD_STATE=${3:-}
OLD_JAR=${4:-}

for bin in "$NEW" "$LEGACY"; do
    if [ ! -x "$bin" ]; then
        echo "not an executable: $bin" >&2
        exit 2
    fi
done
if [ -n "$OLD_STATE" ] && [ ! -f "$OLD_STATE" ]; then
    echo "no such legacy state: $OLD_STATE" >&2
    exit 2
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd) || exit 1
WORK=$(mktemp -d "${TMPDIR:-/tmp}/nj-fuzz2-evidence.XXXXXX") || exit 1
KEEP=0
trap '[ "$KEEP" = 1 ] || rm -rf "$WORK"' EXIT INT TERM

fail() {
    KEEP=1
    echo "FAIL: $*" >&2
    echo "logs kept in $WORK" >&2
    exit 1
}

# --- build the small fixture jar that holds an AnimationTrack instance -------
JAR=$WORK/animstate.jar
mkdir -p "$WORK/classes" || fail "mkdir classes"
CP=$(python3 "$ROOT/tools/midp_stubs.py" --quiet):$(python3 "$ROOT/tools/m3g_stubs.py" --quiet)
javac --release 8 -nowarn -classpath "$CP" -d "$WORK/classes" \
      "$ROOT/tests/fuzzdemo/AnimStateDemo.java" >"$WORK/javac.log" 2>&1 \
    || fail "javac AnimStateDemo (see javac.log)"
printf 'MIDlet-1: AnimStateDemo, , AnimStateDemo\nMIDlet-Name: AnimStateDemo\nMIDlet-Version: 1.0\nMIDlet-Vendor: nano_j2me\n' \
       > "$WORK/MANIFEST.MF"
( cd "$WORK/classes" && jar cfm "$JAR" "$WORK/MANIFEST.MF" . ) \
    >"$WORK/jar.log" 2>&1 || fail "jar AnimStateDemo"

HEADLESS="--port headless --size 240x320 --fps 0"

# --- 1. legacy state with an AnimationTrack instance must be refused ---------
echo "== 1. legacy state holding an AnimationTrack instance is refused"
"$LEGACY" $HEADLESS --run-frames 3 --save-state-frame 1 \
    --save-state "$WORK/atrack-legacy.state" \
    --rms-dir "$WORK/rms-legacy" --fs-dir "$WORK/fs-legacy" "$JAR" \
    >"$WORK/legacy-save.log" 2>&1 \
    || fail "legacy runtime could not save the instance state (legacy-save.log)"
NJ_SAVE_DEBUG=1 "$NEW" $HEADLESS --run-frames 3 --load-state "$WORK/atrack-legacy.state" \
    --rms-dir "$WORK/rms-refuse" --fs-dir "$WORK/fs-refuse" "$JAR" \
    >"$WORK/refuse.log" 2>&1 || true
grep -q "saved state holds an instance of javax/microedition/m3g/AnimationTrack" \
     "$WORK/refuse.log" \
    || fail "new runtime did not report the legacy instance (refuse.log)"
grep -q "cannot resume from" "$WORK/refuse.log" \
    || fail "new runtime did not refuse the legacy instance (refuse.log)"
echo "   PASS: $(grep -m1 'legacy instance_size' "$WORK/refuse.log")"

# --- 2. new-format state still saves and resumes -----------------------------
echo "== 2. new-format state round-trips"
"$NEW" $HEADLESS --run-frames 3 --save-state "$WORK/new.state" \
    --save-state-frame 1 --rms-dir "$WORK/rms-new" --fs-dir "$WORK/fs-new" \
    "$JAR" >"$WORK/new-save.log" 2>&1 \
    || fail "new runtime could not save a state (new-save.log)"
"$NEW" $HEADLESS --run-frames 3 --load-state "$WORK/new.state" \
    --rms-dir "$WORK/rms-new2" --fs-dir "$WORK/fs-new2" "$JAR" \
    >"$WORK/new-load.log" 2>&1 \
    || fail "new runtime could not load its own state (new-load.log)"
grep -q "resumed AnimStateDemo" "$WORK/new-load.log" \
    || fail "new-format state did not resume (new-load.log)"
echo "   PASS: $(grep -m1 '^resumed ' "$WORK/new-load.log")"

# --- 3. optional: a real legacy world state with no ATrack instance migrates -
if [ -n "$OLD_STATE" ]; then
    echo "== 3. legacy world state migrates and resumes"
    [ -f "$OLD_JAR" ] || fail "no such JAR: $OLD_JAR"
    cp "$OLD_STATE" "$WORK/old.world.state" \
        || fail "could not copy $OLD_STATE"
    SHA_BEFORE=$(sha256sum "$OLD_STATE" | awk '{print $1}')
    NJ_SAVE_DEBUG=1 "$NEW" $HEADLESS --heap 32 --run-frames 5 --load-state "$WORK/old.world.state" \
        --rms-dir "$WORK/rms-world" --fs-dir "$WORK/fs-world" "$OLD_JAR" \
        >"$WORK/old-load.log" 2>&1 || fail "legacy world state did not run (old-load.log)"
    grep -q "migrating legacy shape" "$WORK/old-load.log" \
        || fail "no class was migrated for the legacy world state (old-load.log)"
    grep -q "^resumed " "$WORK/old-load.log" \
        || fail "legacy world state did not resume (old-load.log)"
    SHA_AFTER=$(sha256sum "$OLD_STATE" | awk '{print $1}')
    [ "$SHA_BEFORE" = "$SHA_AFTER" ] \
        || fail "the original legacy state was modified"
    echo "   PASS: $(grep -m1 'migrating legacy shape' "$WORK/old-load.log")"
    echo "   PASS: $(grep -m1 '^resumed ' "$WORK/old-load.log")"
else
    echo "== 3. skipped (no legacy world state given)"
fi

echo "OK: all state-compatibility checks passed"
