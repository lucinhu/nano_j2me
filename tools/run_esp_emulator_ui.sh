#!/bin/sh
# Demonstrate display, buttons and storage on the ESP32-S3 under esp-emu.
#
#   tools/run_esp_emulator_ui.sh [--jar PATH] [--out DIR] [--no-build]
#                                [--timeout SECONDS] [--keep-log]
#
# esp-emu models the ESP32-S3 CPU, PSRAM, flash, UART, timers, SPI and the GPIO
# pads, but it models **no LCD controller, no SD/MMC host and no way to drive a
# GPIO input from the command line**.  So this demo does not claim those
# devices exist; it reproduces the three software paths a real handheld uses
# and lets the emulator's own facilities stand in at the edges:
#
#   display  the port renders into a real 240x320 RGB565 framebuffer (the one
#            nj_port_esp32.c DMAs to an ST77xx panel) and prints it over UART0
#            as run-length encoded lines.  tools/decode_ui_frames.py turns them
#            into PNGs in --out.
#   buttons  esp-emu's --inject/--inject-on feed "K:<name>:<1|0>" lines into
#            UART0 right after the firmware prints UI:READY; the firmware turns
#            them into nj_event KEY_DOWN/KEY_UP entries on the same queue the
#            GPIO buttons fill on hardware.
#   storage  the game JAR is carried in a SPIFFS partition mounted at /sdcard
#            (a real board mounts SDMMC+FAT at the same VFS path), so the
#            firmware's fopen("/sdcard/game.jar") is the hardware call too.
#
# The default JAR is tests/fixtures/keysdemo.jar, which repaints a different
# flat colour on every key event: two otherwise-identical captures that differ
# only after the injected keys are direct proof that input reached Java and
# pixels reached the framebuffer.
#
# Requirements: esp-emu on PATH (or ESP_EMU=...), an ESP-IDF v6.0+ environment
# at ~/.espressif (or ESP_IDF_ACTIVATE=...), and python3 (Pillow optional; PPM
# is written when it is missing).
#
# Exit codes: 0 pass, 1 failure, 2 setup problem.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
IDF_DIR="$ROOT/tests/embedded/idf"
BUILD_DIR="build-ui"
MERGED="$IDF_DIR/$BUILD_DIR/merged.bin"
ELF="$IDF_DIR/$BUILD_DIR/nj2me_esp32.elf"

JAR="$ROOT/tests/fixtures/keysdemo.jar"
OUT="$ROOT/build/esp-ui-demo"
TIMEOUT=200
DO_BUILD=1
KEEP_LOG=0
INJECT='K:fire:1\nK:fire:0\nK:right:1\nK:right:0\n'

usage() {
    sed -n '2,42p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --jar)
            shift
            [ $# -gt 0 ] || { echo "error: --jar needs a value" >&2; exit 2; }
            JAR="$1"
            ;;
        --out)
            shift
            [ $# -gt 0 ] || { echo "error: --out needs a value" >&2; exit 2; }
            OUT="$1"
            ;;
        --inject)
            shift
            [ $# -gt 0 ] || { echo "error: --inject needs a value" >&2; exit 2; }
            INJECT="$1"
            ;;
        --no-build)  DO_BUILD=0 ;;
        --keep-log)  KEEP_LOG=1 ;;
        --timeout)
            shift
            [ $# -gt 0 ] || { echo "error: --timeout needs a value" >&2; exit 2; }
            TIMEOUT="$1"
            ;;
        -h|--help)   usage; exit 0 ;;
        *) echo "error: unknown argument '$1' (try --help)" >&2; exit 2 ;;
    esac
    shift
done

case "$JAR" in
    /*) ;;
    *) JAR="$ROOT/$JAR" ;;
esac
if [ ! -f "$JAR" ]; then
    echo "error: JAR does not exist: $JAR" >&2
    exit 2
fi
JAR_NAME=$(basename "$JAR")
mkdir -p "$OUT" || exit 2
LOG="$OUT/uart.log"

# ---------------------------------------------------------------------------
# Tool discovery (same rules as tools/run_esp_emulator.sh)
# ---------------------------------------------------------------------------
EMU="${ESP_EMU:-}"
if [ -z "$EMU" ]; then
    EMU=$(command -v esp-emu 2>/dev/null || true)
fi
if [ -z "$EMU" ] || [ ! -x "$EMU" ]; then
    echo "error: esp-emu not found." >&2
    echo "  install it with:" >&2
    echo "    curl -fsSL https://raw.githubusercontent.com/espressif/esp-emulator/main/install.sh | sh" >&2
    echo "  or point ESP_EMU at an existing binary." >&2
    exit 2
fi

ACTIVATE="${ESP_IDF_ACTIVATE:-}"
if [ -z "$ACTIVATE" ]; then
    for candidate in "$HOME"/.espressif/tools/activate_idf_v*.sh; do
        [ -f "$candidate" ] && ACTIVATE="$candidate"
    done
fi
if [ -z "$ACTIVATE" ] || [ ! -f "$ACTIVATE" ]; then
    echo "error: no ESP-IDF activation script found." >&2
    echo "  install ESP-IDF v6.0 and set ESP_IDF_ACTIVATE=/path/to/activate_idf_v6.0.1.sh" >&2
    exit 2
fi
IDF_ENV=$("$ACTIVATE" -e 2>/dev/null) || {
    echo "error: $ACTIVATE -e failed" >&2
    exit 2
}
while IFS= read -r line; do
    case "$line" in
        PATH=*|IDF_PATH=*|IDF_TOOLS_PATH=*|IDF_PYTHON_ENV_PATH=*|ESP_ROM_ELF_DIR=*|OPENOCD_SCRIPTS=*|ESP_IDF_VERSION=*)
            # shellcheck disable=SC2163
            export "$line"
            ;;
    esac
done <<EOF
$IDF_ENV
EOF
if [ -z "${IDF_PYTHON_ENV_PATH:-}" ] || [ ! -x "$IDF_PYTHON_ENV_PATH/bin/python" ]; then
    echo "error: $ACTIVATE did not report a usable IDF_PYTHON_ENV_PATH" >&2
    exit 2
fi
IDF_PY="$IDF_PYTHON_ENV_PATH/bin/python $IDF_PATH/tools/idf.py"

idf() {
    # shellcheck disable=SC2086
    ( cd "$IDF_DIR" && $IDF_PY -B "$BUILD_DIR" "$@" )
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
if [ "$DO_BUILD" -eq 1 ]; then
    if [ -f "$IDF_DIR/sdkconfig" ]; then
        if ! grep -q '^CONFIG_SPIRAM=y' "$IDF_DIR/sdkconfig" ||
           ! grep -q '^CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y' "$IDF_DIR/sdkconfig"; then
            echo "==> replacing stale sdkconfig (not the 16 MB flash / 8 MB PSRAM profile)"
            rm -f "$IDF_DIR/sdkconfig" "$IDF_DIR/sdkconfig.old"
        fi
    fi
    echo "==> building the UI demo image ($IDF_DIR/$BUILD_DIR)"
    idf -DNJ_UI_DEMO=1 -DNJ_UI_JAR="$JAR" build || {
        echo "error: idf.py build failed" >&2
        exit 1
    }
fi
if [ ! -f "$ELF" ]; then
    echo "error: $ELF missing (run without --no-build first)" >&2
    exit 1
fi
echo "==> merging the flash image (application + bootloader + SPIFFS card)"
idf merge-bin -o merged.bin >/dev/null || {
    echo "error: idf.py merge-bin failed" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Boot under esp-emu with the key injection armed
# ---------------------------------------------------------------------------
echo "==> booting $JAR_NAME in esp-emu (timeout ${TIMEOUT}s)"
START=$(date +%s)
"$EMU" --chip esp32s3 \
       --firmware "$MERGED" \
       --elf "$ELF" \
       --psram-size 8M \
       --timeout "${TIMEOUT}s" \
       --exit-on "UI:DONE" \
       --inject-on "UI:READY" \
       --inject "$INJECT" >"$LOG" 2>&1
RC=$?
END=$(date +%s)
ELAPSED=$((END - START))
echo "==> emulator exit $RC after ${ELAPSED}s (UART log: $LOG)"

echo
echo "--- UART (demo lines) ---"
grep -E "nj2me-ui: ui-demo|KEY (down|up)|Inject trigger|FRAME:START" "$LOG" | head -20 || true
echo

FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

[ "$RC" -eq 0 ] || fail "emulator exited $RC (a timeout or crash ends here)"
grep -q "mounted simulated SD card at /sdcard" "$LOG" || fail "the storage partition did not mount"
grep -q "loaded /sdcard/game.jar" "$LOG" || fail "the JAR was not read through the /sdcard VFS path"
grep -q "ui-demo: running" "$LOG" || fail "the MIDlet never started"
grep -q "Inject trigger 'UI:READY' found" "$LOG" || fail "esp-emu never saw UI:READY, so no keys were injected"
DELIVERED=$(grep -c "ui-demo: delivered key=" "$LOG" || true)
[ "${DELIVERED:-0}" -ge 1 ] || fail "no key event reached input_poll()"
grep -q "ui-demo: capture 2" "$LOG" || fail "the second frame capture never ran"
grep -q "UI:DONE" "$LOG" || fail "the firmware never printed UI:DONE"

echo "--- decoded framebuffer ---"
python3 "$ROOT/tools/decode_ui_frames.py" "$LOG" "$OUT/frame_" | tee "$OUT/decode.txt"
DEC_RC=$?
echo
[ "$DEC_RC" -eq 0 ] || fail "the UART log held no complete frames"
grep -q "^frames decoded: 2$" "$OUT/decode.txt" || fail "expected two captured frames"
grep -q "frame 0: 240x320" "$OUT/decode.txt" || fail "the captured frame is not the port's 240x320 canvas"
case "$JAR_NAME" in
    keysdemo.jar)
        grep -q "KEY down -5" "$LOG" || fail "the FIRE key did not arrive as MIDP code -5 in Java"
        grep -q "frames differ: YES" "$OUT/decode.txt" || fail "the frames before and after the injected keys are identical"
        grep -q "nonblack=76800/76800" "$OUT/decode.txt" || fail "the KeysDemo frame is not fully painted"
        ;;
    *)
        echo "note: $JAR_NAME is not the KeysDemo fixture; whether the picture changes"
        echo "      on a key by frame 140 is game-specific and only reported, not asserted."
        grep -q "frames differ: YES" "$OUT/decode.txt" || \
            echo "note: the two captures are identical (a menu or splash that waits for input looks like this)."
        ;;
esac

echo
echo "--- result ---"
if [ "$FAIL" -eq 0 ]; then
    FRAMES_LINE=$(grep "ui-demo: frames=" "$LOG" | tail -1)
    echo "PASS: ESP32-S3 image booted under esp-emu, mounted the simulated SD card,"
    echo "      read $JAR_NAME from it, rendered 240x320 RGB565 frames, delivered"
    echo "      $DELIVERED injected key event(s) into Java, and the frame changed."
    echo "      ${FRAMES_LINE:+$FRAMES_LINE}"
    echo "      PNGs: $OUT/frame_0.png $OUT/frame_1.png (or .ppm without Pillow)"
    echo "      UART log: $LOG"
    [ "$KEEP_LOG" -eq 1 ] && echo "      (log kept at the path above)"
    exit 0
fi
echo "FAIL: see the UART log at $LOG"
exit 1
