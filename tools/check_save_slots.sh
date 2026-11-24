#!/bin/sh
# Save-state slots (command line side).
#
# One snapshot per slot, named "<stem>-slot<N>.state" in --state-dir (default:
# "<jar dir>/states"), resumable with --load-slot, overwritable, and removable
# by deleting the file.  The SDL2 port writes the same names through its
# F5/F6/F7/F8/F9 hotkeys; the CLI is the part that can be driven headlessly.
# shellcheck shell=sh
set -u

BIN="${1:?usage: check_save_slots.sh <nj2me> <jar>}"
JAR="${2:?usage: check_save_slots.sh <nj2me> <jar>}"
if [ ! -f "$JAR" ]; then
  echo "SKIP save slots: missing $JAR"
  exit 77
fi

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
stem=$(basename "$JAR"); stem=${stem%.*}
fail() { echo "FAIL save slots: $*"; exit 1; }

run() { "$BIN" --port headless --size 240x320 --heap 16 --fps 30 "$@"; }

slot_path="$tmp/$stem-slot0.state"
run --state-dir "$tmp" --run-frames 40 --save-slot 0 --save-state-frame 20 "$JAR" \
  >"$tmp/save0.log" 2>&1 || fail "save slot 0 exited $?"
[ -s "$slot_path" ] || fail "slot 0 file missing: $slot_path"
grep -q "saved state to $slot_path" "$tmp/save0.log" || fail "save log did not name slot 0"

run --state-dir "$tmp" --run-frames 10 --load-slot 0 "$JAR" >"$tmp/load0.log" 2>&1 \
  || fail "load slot 0 exited $?"
grep -q "resumed .* from $slot_path" "$tmp/load0.log" || fail "load log did not resume slot 0"

# A later save to the same slot replaces it (same path, different bytes).
first=$(sha256sum "$slot_path" | cut -d' ' -f1)
run --state-dir "$tmp" --run-frames 80 --save-slot 0 --save-state-frame 60 "$JAR" \
  >"$tmp/save0b.log" 2>&1 || fail "overwrite slot 0 exited $?"
second=$(sha256sum "$slot_path" | cut -d' ' -f1)
[ "$first" != "$second" ] || fail "slot 0 did not change after a later save"

# A second slot is independent and both stay loadable.
run --state-dir "$tmp" --run-frames 40 --save-slot 2 --save-state-frame 20 "$JAR" \
  >"$tmp/save2.log" 2>&1 || fail "save slot 2 exited $?"
[ -s "$tmp/$stem-slot2.state" ] || fail "slot 2 file missing"
run --state-dir "$tmp" --run-frames 10 --load-slot 2 "$JAR" >"$tmp/load2.log" 2>&1 \
  || fail "load slot 2 exited $?"
rm -f "$tmp/$stem-slot2.state" || fail "cannot delete slot 2"
[ ! -e "$tmp/$stem-slot2.state" ] || fail "slot 2 still there after deletion"

# The default directory follows the JAR; hand it a copy so the tree stays clean.
mkdir "$tmp/game" || fail "cannot make game dir"
cp "$JAR" "$tmp/game/" || fail "cannot copy jar"
run --run-frames 40 --save-slot 1 --save-state-frame 20 "$tmp/game/$(basename "$JAR")" \
  >"$tmp/save1.log" 2>&1 || fail "save slot 1 (default dir) exited $?"
[ -s "$tmp/game/states/$stem-slot1.state" ] || fail "default states dir was not used"

# Clear errors instead of silent bad paths.
run --save-slot x "$JAR" >"$tmp/bad.log" 2>&1
[ $? -eq 2 ] || fail "--save-slot x did not exit 2"
grep -q -- "--save-slot needs an integer" "$tmp/bad.log" || fail "no message for a bad slot"
run --save-slot 3 --save-state "$tmp/x.state" "$JAR" >"$tmp/both.log" 2>&1
[ $? -eq 2 ] || fail "--save-slot with --save-state did not exit 2"

echo "PASS save slots: two slots saved/resumed, overwrite changed the blob, default dir used"
