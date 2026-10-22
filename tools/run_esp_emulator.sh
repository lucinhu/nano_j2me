#!/bin/sh
# Verify the ESP32-S3 port on Espressif's emulator (esp-emu).
#
#   tools/run_esp_emulator.sh [--no-build] [--timeout SECONDS] [--keep-log]
#
# What it does
#   1. sources an ESP-IDF environment (activate_idf_v*.sh) and builds
#      tests/embedded/idf for esp32s3 into a 16 MB flash / 8 MB PSRAM image;
#   2. merges the bootloader, partition table and application into one flash
#      image with `idf.py merge-bin`;
#   3. boots that image under esp-emu, which models the real ESP32-S3 mask ROM,
#      bootloader, 16 MB flash and the octal PSRAM chip;
#   4. checks the UART log for the image's own result line and for the runtime
#      counters, so a boot that never reaches Java cannot pass.
#
# Requirements
#   - esp-emu on PATH, or ESP_EMU=/path/to/esp-emu:
#       curl -fsSL https://raw.githubusercontent.com/espressif/esp-emulator/main/install.sh | sh
#   - an ESP-IDF v6.0+ environment at ~/.espressif (ESP-IDF 6.0.1 is what this
#     image is built and tested with).  Override the activation script with
#     ESP_IDF_ACTIVATE=/path/to/activate_idf_v6.0.1.sh.
#
# Why esp-emu and not QEMU: QEMU's esp32s3 machine cannot probe PSRAM
# (espressif/qemu#129) and models neither the 16 MB flash header nor an 8 MB
# PSRAM die, so it runs the internal-RAM fallback with a 340 KiB pool.  esp-emu
# models both, which is the board this port targets.  A QEMU run is still
# possible: build with
#   idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.qemu" build
# and boot build/merged.bin in QEMU's esp32s3 machine.
#
# Exit codes: 0 pass, 1 failure, 2 setup problem.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
IDF_DIR="$ROOT/tests/embedded/idf"
BUILD="$IDF_DIR/build"
MERGED="$BUILD/merged.bin"
ELF="$BUILD/nj2me_esp32.elf"

TIMEOUT=200
DO_BUILD=1
KEEP_LOG=0

usage() {
    sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
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

# ---------------------------------------------------------------------------
# Tool discovery
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

# The activation script is what sets IDF_PYTHON_ENV_PATH, IDF_PATH and PATH for
# the toolchain; the SDK's own export.sh points at a different venv layout on
# this machine.  Sourcing it directly is not an option: it decides whether it
# was sourced by looking at $0, which here is this script's name, and then
# exits 1 thinking it was executed.  Its documented -e mode prints the
# bindings instead, which is exactly what importing them needs.
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

# Run idf.py through the interpreter the activation script selected, from the
# project directory (merge-bin writes relative to build/).
idf() {
    # shellcheck disable=SC2086
    ( cd "$IDF_DIR" && $IDF_PY "$@" )
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
if [ "$DO_BUILD" -eq 1 ]; then
    # sdkconfig is generated and gitignored, and a QEMU build leaves one with
    # PSRAM off and 4 MB flash.  It takes precedence over sdkconfig.defaults, so
    # a stale one would silently produce an image that cannot pass the checks
    # below; replace it rather than build the wrong board.
    if [ -f "$IDF_DIR/sdkconfig" ]; then
        if ! grep -q '^CONFIG_SPIRAM=y' "$IDF_DIR/sdkconfig" ||
           ! grep -q '^CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y' "$IDF_DIR/sdkconfig"; then
            echo "==> replacing stale sdkconfig (not the 16 MB flash / 8 MB PSRAM profile)"
            rm -f "$IDF_DIR/sdkconfig" "$IDF_DIR/sdkconfig.old"
        fi
    fi
    echo "==> building ($IDF_DIR)"
    idf build || {
        echo "error: idf.py build failed" >&2
        exit 1
    }
fi
if [ ! -f "$ELF" ]; then
    echo "error: $ELF missing (run without --no-build first)" >&2
    exit 1
fi
idf merge-bin -o merged.bin >/dev/null || {
    echo "error: idf.py merge-bin failed" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
LOG="${TMPDIR:-/tmp}/nano_j2me_esp_emu.$$.log"
cleanup() { [ "$KEEP_LOG" -eq 1 ] || rm -f "$LOG"; }
trap cleanup EXIT HUP INT TERM

echo "==> booting $(basename "$MERGED") in esp-emu (timeout ${TIMEOUT}s)"
START=$(date +%s)
"$EMU" --chip esp32s3 \
       --firmware "$MERGED" \
       --elf "$ELF" \
       --psram-size 8M \
       --timeout "${TIMEOUT}s" \
       --exit-on "NJ2ME END" >"$LOG" 2>&1
RC=$?
END=$(date +%s)
ELAPSED=$((END - START))

echo "==> emulator exit $RC after ${ELAPSED}s (log: $LOG)"
echo
echo "--- UART (runtime lines) ---"
grep -E "nano_j2me|nj2me:|NJ2ME DONE|pool |arena |bytecodes=" "$LOG" | tail -25 || true
echo

FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

[ "$RC" -eq 0 ] || fail "emulator exited $RC (expected 0; a timeout or crash ends here)"
grep -q "NJ2ME DONE rc=0" "$LOG" || fail "no 'NJ2ME DONE rc=0' marker; the VM did not finish cleanly"
grep -q "cannot create builtin class" "$LOG" && fail "the VM could not materialise the class library (out of memory)"
BYTECODES=$(sed -n 's/.*bytecodes=\([0-9][0-9]*\).*/\1/p' "$LOG" | tail -1)
[ -n "$BYTECODES" ] || fail "no bytecode counter in the log; the interpreter never ran"
if [ -n "$BYTECODES" ] && [ "$BYTECODES" -lt 1000000 ]; then
    fail "bytecodes=$BYTECODES is too small to be the benchmark workload"
fi
grep -q "(psram)" "$LOG" || fail "the pool did not come from PSRAM; check CONFIG_SPIRAM and --psram-size"
grep -qE "free: psram [1-9][0-9]* KiB" "$LOG" || fail "IDF reports no free PSRAM; the emulated die was not initialised"

echo "--- result ---"
if [ "$FAIL" -eq 0 ]; then
    echo "PASS: ESP32-S3 image booted, created the full class library from PSRAM,"
    echo "      ran bench.jar and reported bytecodes=$BYTECODES in ${ELAPSED}s."
    exit 0
fi
echo "FAIL: see the UART log above."
exit 1
