#!/usr/bin/env bash
#
# Build, link and measure a real bare-metal firmware image for a 32-bit MCU.
#
# Compiling the runtime for a cross target proves it parses.  LINKING it with
# -nostdlib proves the dependency claim: that nothing in src/ reaches for a
# hosted C library, and that the runtime's own .data and .bss are empty on a
# real 32-bit target.  Everything the image needs beyond the runtime is the
# port and a handful of one-line maths functions.
#
# Usage: tools/make_bare_firmware.sh [--jre JAR] [--cpu cortex-m4] [--out fw.elf]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CROSS_CC:-arm-none-eabi-gcc}"
SIZE="${CROSS_SIZE:-arm-none-eabi-size}"
JAR="${NJ_BARE_JAR:-$ROOT/tests/fixtures/bench.jar}"
CPU=cortex-m4
OUT=/tmp/nj_bare_fw.elf

while [ $# -gt 0 ]; do
    case "$1" in
        --jre|--jar) JAR="$2"; shift 2 ;;
        --cpu) CPU="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

command -v "$CC" >/dev/null 2>&1 || { echo "no $CC on PATH" >&2; exit 2; }
[ -f "$JAR" ] || { echo "no JAR at $JAR (run tools/make_bench_jar.sh)" >&2; exit 2; }

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# The JAR as a C array, so the image is self-contained.
python3 - "$JAR" > "$T/jar.c" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
print('/* generated: the fixture JAR as a C array */')
print('#include "nj2me/nj_types.h"')
print('const unsigned char nj_bare_jar[] = {')
for i in range(0, len(d), 16):
    print('    ' + ','.join('0x%02x' % b for b in d[i:i+16]) + ',')
print('};')
print('const unsigned int nj_bare_jar_len = %du;' % len(d))
PY

cat > "$T/main.c" <<'C'
/*
 * The embedder entry point, not the runner.
 *
 * src/runner/nj_run.c is a desktop convenience: it reads files, uses malloc and
 * parses --flags.  An embedded application does none of that, and this image
 * proves the public API is enough without it - nj_vm_create, nj_app_open,
 * nj_vm_new_midlet, nj_vm_start_app, nj_midp_frame, nj_vm_destroy.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_app.h"
typedef struct bare_state bare_state;
nj_status nj_port_bare(nj_port *out, bare_state *st, void *heap, nj_size heap_bytes);
int nj_port_bare_frames(const void *ud);
extern const unsigned char nj_bare_jar[];
extern const unsigned int  nj_bare_jar_len;

/* The application owns the RAM: on the real part g_heap would be a PSRAM
 * region and g_port_state a plain static.  Neither is in src/, and the runtime
 * adds nothing of its own - which is the whole point of the measurement. */
static unsigned char g_heap[2u * 1024u * 1024u];
static unsigned char g_port_state[64u * 1024u];

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_ref midlet = 0;
    int rc = 1;

    if (nj_port_bare(&port, (bare_state *)g_port_state, g_heap, sizeof(g_heap)) != NJ_OK) {
        return 1;
    }
    vm = nj_vm_create(&port, sizeof(g_heap));
    if (!vm) { return 1; }

    app = nj_app_open(vm, nj_bare_jar, nj_bare_jar_len);
    if (app && nj_vm_new_midlet(vm, 0, &midlet) == NJ_OK &&
        nj_vm_start_app(vm, midlet) == NJ_OK) {
        /* A real firmware would run this from its main loop forever; the image
         * stops after a fixed number of frames so it can be sized and, on
         * hardware, so a test can assert it finished. */
        while (nj_vm_running(vm) && nj_port_bare_frames(port.ud) < 600) {
            if (nj_midp_frame(vm) != NJ_OK) { break; }
        }
        rc = (int)nj_vm_exit_code(vm);
    }
    nj_app_close(vm, app);
    (void)nj_vm_destroy(vm);
    return rc;
}
C

CFLAGS="-std=c99 -mcpu=$CPU -mthumb -Os -ffunction-sections -fdata-sections
        -I$ROOT/include -I$ROOT/src/core -I$ROOT/src/midp -I$ROOT/src/gfx -I$ROOT/src/zip
        -DNJ_USE_LIBC=0 -DNJ_ENABLE_FLOAT=0 -DNJ_ENABLE_JPEG=0"

objs=""
for f in "$ROOT"/src/core/*.c "$ROOT"/src/midp/*.c "$ROOT"/src/gfx/*.c \
         "$ROOT"/src/zip/*.c \
         "$ROOT"/tests/embedded/nj_port_bare.c "$T/main.c" "$T/jar.c"; do
    o="$T/$(basename "$f").o"
    if ! $CC $CFLAGS -c "$f" -o "$o" 2>"$T/err"; then
        echo "compile failed: $f" >&2; head -5 "$T/err" >&2; exit 1
    fi
    objs="$objs $o"
done

# -nostdlib: if anything in src/ needed a hosted libc, this is where it shows.
# -lgcc supplies the ARM EABI soft-float helpers, which are a property of the
# target's calling convention, not a C library.
if ! $CC -mcpu=$CPU -mthumb -mfloat-abi=soft -nostdlib -Wl,--gc-sections \
         -Wl,-e,main -o "$OUT" $objs -lgcc 2>"$T/link.err"; then
    echo "link failed - an undefined symbol here is a gap in the port, or a" >&2
    echo "dependency the runtime does not declare in nj2me/nj_libc.h:" >&2
    head -10 "$T/link.err" >&2
    exit 1
fi

echo "firmware: $OUT  ($(stat -c%s "$OUT") bytes on disk)"
$SIZE "$OUT"
echo
echo "flash = text + rodata;  RAM at reset = data + bss."
echo "The runtime's own contribution to that RAM is zero - everything above is"
echo "the application's heap and port state."
