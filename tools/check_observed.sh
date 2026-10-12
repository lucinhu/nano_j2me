#!/bin/bash
#
# The headless port must say what it observed, and a script must be able to
# read it.
#
# Why this exists: the corpus runner used to judge every title on pixels alone,
# and pixels cannot tell "saved the score" from "silently did nothing" - both
# are the same sequence of frames.  `--stats` now emits an `observed:` line
# carrying the thread count, the GC count, and the headless port's audio and
# file counters.
#
# The check that matters is the LAST one.  A missing observation and a genuine
# zero are different answers, and the port reports -1 for the first.  A test
# that only asserted "file_opens is a number" would pass on a port that was
# never wired up at all - which is precisely the bug this line was added to
# catch.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-$ROOT/tests/fixtures/gamedemo.jar}"
[ -x "$BIN" ] || { echo "no emulator at $BIN" >&2; exit 1; }
[ -f "$JAR" ] || { echo "no fixture at $JAR" >&2; exit 1; }

out="$("$BIN" --port headless --heap 16 --stats --run-frames 20 "$JAR" 2>&1)"
line="$(printf '%s\n' "$out" | grep -m1 '^observed:')"
fail=0
note() { printf '  %-4s %s\n' "$1" "$2"; }

# The fixture has to really be a MIDlet, and the manifest does not settle it:
# tests/fixtures/hello.jar declares MIDlet-1 and names a plain HelloWorld with
# main(), so every check below used to answer questions about a program the
# emulator could not start - while saying so in a line nobody read.
if printf '%s\n' "$out" | grep -q "startApp() threw"; then
    note FAIL "the fixture is not runnable as a MIDlet: $(printf '%s\n' "$out" | grep -m1 'startApp() threw')"
    fail=1
fi

if [ -z "$line" ]; then
    note FAIL "no observed: line at all"
    exit 1
fi
note ok "the port reports: ${line#observed: }"

field() {
    printf '%s\n' "$line" | sed -n "s/.*[[:space:]]$1=\(-\?[0-9]*\).*/\1/p"
}

for f in threads gc tones pcm_frames file_opens file_writes file_reads bytes_written; do
    v="$(field "$f")"
    if [ -z "$v" ]; then
        note FAIL "$f is absent from the observed line"
        fail=1
    elif [ "$v" = "-1" ]; then
        note FAIL "$f reported -1: the headless port is not recording it"
        fail=1
    fi
done

# The fixture builds a RecordStore, so the write path must have been taken.
for f in file_opens file_writes bytes_written; do
    v="$(field "$f")"
    if [ -n "$v" ] && [ "$v" != "-1" ] && [ "$v" -le 0 ]; then
        note FAIL "$f is $v but this fixture writes a RecordStore"
        fail=1
    fi
done

# Every run must say why it ended.  "Nothing was ever presented" and "the
# MIDlet asked to be destroyed" leave the same empty frame directory, and the
# reason is the only thing that tells a missing class from a game that quit.
ended="$(printf '%s\n' "$out" | grep -m1 '^run ended:')"
if [ -z "$ended" ]; then
    note FAIL "the run never said why it ended"
    fail=1
elif ! printf '%s' "$ended" | grep -q "frame budget"; then
    note FAIL "this fixture should end on the frame budget, but: $ended"
    fail=1
fi

[ "$fail" -eq 0 ] && note ok "every observation is a real number, not a missing one"
[ "$fail" -eq 0 ] && echo "PASS" || echo "FAILED"
exit "$fail"
