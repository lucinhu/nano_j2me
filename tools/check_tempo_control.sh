#!/usr/bin/env bash
#
# JSR 135 TempoControl / RateControl, end to end through the interpreter.
#
# 速度与激情6_9 asks its music Player for "TempoControl", null-checks the result
# and catches the Exception a missing class throws - so before this existed the
# music simply played at the file's own tempo and setTempo() changed nothing.
# An answered control has to really re-time the song: the renderer's
# tick->sample map is what moves, and the length the Player reports follows.
#
# The fixture builds a one-note MIDI file at 120 bpm (so the default tempo is
# the General MIDI 120000 milli-bpm), asks for the control, sets 240000 and 500
# and reads the values back, and checks getControls() has no NULL in it.
#
#   build/nj2me --port headless tests/.../tempodemo.jar
#     -> TEMPO control=1 isTempo=true isRate=true t0=120000 set=240000
#              rate=2000 rate_set=500 tempo_back=60000 dur_fast=true
#              dur_slow=true nulls=0 tempos=1
#
# Usage: check_tempo_control.sh [jar]
#   NJ_BIN overrides the emulator under test (default build/nj2me).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if ! command -v javac >/dev/null 2>&1 || ! command -v jar >/dev/null 2>&1; then
    echo "SKIP: no JDK (javac/jar) to build the fixture"
    exit 77
fi
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 77; }

if [ -z "$JAR" ]; then
    STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)" || {
        echo "SKIP: the stub generator failed"; exit 77; }
    mkdir -p "$WORK/classes/META-INF" "$WORK/classes/res"
    if ! javac --release 8 -nowarn -classpath "$STUBS" \
            -d "$WORK/classes" "$ROOT/tests/tempodemo/TempoDemo.java"; then
        echo "FAIL: the fixture does not compile against the stub API"
        exit 1
    fi
    # One quarter note held for 480 ticks at division 480 and 500000 us/qn:
    # 120 bpm, General MIDI's default, which is what the fixture asserts.
    python3 - "$WORK/classes/res/tempo.mid" <<'PY'
import struct, sys
trk = bytes([0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,      # Set Tempo 500000
             0x00, 0x90, 69, 100,                            # note on C
             0x83, 0x60, 0x80, 69, 64,                       # after 480 ticks, off
             0x00, 0xFF, 0x2F, 0x00])                        # end of track
hdr = b'MThd' + struct.pack('>IHHH', 6, 0, 1, 480)
open(sys.argv[1], 'wb').write(hdr + b'MTrk' + struct.pack('>I', len(trk)) + trk)
PY
    printf 'Manifest-Version: 1.0\r\nMIDlet-Name: TempoDemo\r\nMIDlet-1: TempoDemo, , TempoDemo\r\n\r\n' \
        > "$WORK/classes/META-INF/MANIFEST.MF"
    JAR="$WORK/tempodemo.jar"
    ( cd "$WORK/classes" && jar cfm "$JAR" META-INF/MANIFEST.MF *.class res ) || {
        echo "FAIL: could not package the fixture"; exit 1; }
fi

LOG="$WORK/run.log"
if ! timeout 120 "$BIN" --port headless --run-frames 120 --heap 8 "$JAR" \
        >"$LOG" 2>&1; then
    echo "FAIL: the emulator failed to run the fixture"
    sed -n '1,20p' "$LOG"
    exit 1
fi

LINE="$(grep -m1 '^TEMPO ' "$LOG" || true)"
if [ -z "$LINE" ]; then
    echo "FAIL: the fixture printed no TEMPO line"
    sed -n '1,20p' "$LOG"
    exit 1
fi
case "$LINE" in
    "TEMPO ERROR"*)
        echo "FAIL: the tempo control did not work: $LINE"
        exit 1
        ;;
esac

fail=0
expect() {   # $1 = field=value
    case "$LINE" in
        *"$1"*) ;;
        *) echo "FAIL: expected $1 in: $LINE"; fail=1 ;;
    esac
}
expect "control=1"           # a MIDI Player has a TempoControl
expect "isTempo=true"        # and it is one, through checkcast/instanceof
expect "isRate=true"         # TempoControl extends RateControl
expect "t0=120000"           # the file's 500000 us/qn is 120 bpm
expect "set=240000"          # setTempo returns what it set
expect "rate=2000"           # twice the tempo is twice the rate
expect "rate_set=500"        # half the rate ...
expect "tempo_back=60000"    # ... is half the tempo
expect "dur_fast=true"       # a faster tempo is a shorter song
expect "dur_slow=true"       # and a slower one is a longer song
expect "nulls=0"             # getControls() has no NULL in it
expect "tempos=1"            # and lists the control once

if [ "$fail" -ne 0 ]; then
    echo "  line: $LINE"
    exit 1
fi
echo "ok: $LINE"
exit 0
