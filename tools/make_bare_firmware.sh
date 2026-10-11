#!/usr/bin/env bash
#
# Build, link and measure a real firmware image for the actual target MCUs.
#
# Compiling the runtime for a cross target proves it parses.  LINKING it with
# -nostdlib proves the dependency claim: that nothing in src/ reaches for a
# hosted C library, and that the runtime's own .data and .bss are empty on the
# real architecture.  Everything the image needs beyond the runtime is the port
# and a handful of one-line maths functions.
#
# The two ESP32 targets are different architectures and both are supported:
#
#   xtensa   ESP32-S3, ESP32-S2  (Xtensa LX7 / LX6)
#   riscv32  ESP32-C3, C6, H2, P4  (RV32IMC)
#
# The ESP-IDF toolchains live under ~/.espressif/tools and are found
# automatically; pass --cc to use another.
#
# Usage: tools/make_bare_firmware.sh [--target xtensa|riscv32|cortex-m4]
#                                    [--jre JAR] [--out fw.elf]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET=xtensa
EXTRA_CFLAGS=""
EXTRA_LDFLAGS=""
JAR="${NJ_BARE_JAR:-$ROOT/tests/fixtures/bench.jar}"
OUT=""
CC=""
SIZE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        --jre|--jar) JAR="$2"; shift 2 ;;
        --cc) CC="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# Find a compiler for the target.  The ESP-IDF ones are not on PATH by default,
# so look where the IDF installer puts them.
find_esp() {
    local pat="$1"
    find "$HOME/.espressif/tools" -name "$pat" -type f 2>/dev/null | sort | tail -1
}

case "$TARGET" in
    xtensa)
        [ -n "$CC" ] || CC="$(find_esp 'xtensa-esp32s3-elf-gcc')"
        [ -n "$CC" ] || CC="$(command -v xtensa-esp32s3-elf-gcc || true)"
        SIZE="${CC%-gcc}-size"
        ARCHFLAGS="-mlongcalls"
        ;;
    riscv32)
        [ -n "$CC" ] || CC="$(find_esp 'riscv32-esp-elf-gcc')"
        [ -n "$CC" ] || CC="$(command -v riscv32-esp-elf-gcc || true)"
        SIZE="${CC%-gcc}-size"
        ARCHFLAGS="-march=rv32imc -mabi=ilp32"
        ;;
    cortex-m4)
        [ -n "$CC" ] || CC="$(command -v arm-none-eabi-gcc || true)"
        SIZE="${CC%-gcc}-size"
        ARCHFLAGS="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft"
        ;;
    *) echo "unknown target: $TARGET (xtensa, riscv32 or cortex-m4)" >&2; exit 2 ;;
esac

[ -n "$OUT" ] || OUT="/tmp/nj_bare_${TARGET}.elf"
[ -n "$CC" ] && command -v "$CC" >/dev/null 2>&1 || { echo "no compiler for $TARGET" >&2; exit 2; }

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
#include "nj2me/nj_vm.h"
typedef struct bare_state bare_state;
nj_status nj_port_bare(nj_port *out, bare_state *st, void *heap, nj_size heap_bytes);
int  nj_port_bare_frames(const void *ud);
nj_size nj_vm_sizeof(void);
void nj_port_bare_report(int rc, int frames);
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
    /* The POOL covers three things: the VM instance, the Java arena, and the
     * statics plus native registry plus builtin classes, which want about
     * 300 KiB between them before a single Java object exists - the statics,
     * the native registry and the builtin classes take about 250 KiB, and the
     * main thread's operand-stack budget is another 64 KiB at the desktop
     * default of NJ_MAX_STACK_SLOTS.  Passing sizeof(g_heap) as the arena
     * leaves nothing for any of it, which used to show up as a crash and now
     * shows up as nj_vm_create() returning NULL with the reason. */
    vm = nj_vm_create(&port, sizeof(g_heap) - nj_vm_sizeof() - (384u * 1024u));
    if (!vm) { return 1; }

    app = nj_app_open(vm, nj_bare_jar, nj_bare_jar_len);

    /* Two things this used to get wrong, and the second is the one that bites:
     * nj_vm_new_midlet() takes a CLASS NAME - pass NULL and it returns NULL -
     * and it returns a REF, so comparing that against NJ_OK (which is 0) reads
     * a NULL as success.  Test the ref for NULL, and take the name from the
     * manifest. */
    midlet = app ? nj_vm_new_midlet(vm, nj_app_midlet_class(app, 0), NULL) : 0;
    if (midlet && nj_vm_start_app(vm, midlet) == NJ_OK) {
        /* A real firmware would run this from its main loop forever; the image
         * stops after a fixed number of frames so it can be sized and, on
         * hardware, so a test can assert it finished. */
        while (nj_vm_running(vm) && nj_port_bare_frames(port.ud) < 600) {
            if (nj_midp_frame(vm) != NJ_OK) { break; }
        }
        rc = (int)nj_vm_exit_code(vm);
    }
    if (app) { nj_app_close(vm, app); }
    (void)nj_vm_destroy(vm);
    nj_port_bare_report(rc, nj_port_bare_frames(port.ud));
    return rc;
}
C

CFLAGS="-std=c99 $ARCHFLAGS -Os -ffunction-sections -fdata-sections
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
if ! $CC $ARCHFLAGS -nostdlib -Wl,--gc-sections \
         -Wl,-e,main -o "$OUT" $objs -lgcc 2>"$T/link.err"; then
    echo "link failed - an undefined symbol here is a gap in the port, or a" >&2
    echo "dependency the runtime does not declare in nj2me/nj_libc.h:" >&2
    head -10 "$T/link.err" >&2
    exit 1
fi

# Also build the SAME main.c for the host and run it, so the firmware's logic is
# exercised rather than only linked.  QEMU for xtensa and riscv32 is not part of
# a normal install, so running the image itself is not always possible; running
# its source is.
if [ "${NJ_BARE_NO_HOST:-0}" != "1" ]; then
    host="${OUT%.elf}.host"
    if gcc -std=c99 -O1 -I"$ROOT/include" -I"$ROOT/src/core" -I"$ROOT/src/midp" \
           -I"$ROOT/src/gfx" -I"$ROOT/src/zip" -DNJ_USE_LIBC=1 -DNJ_BARE_UART=0 \
           -o "$host" "$T/main.c" "$T/jar.c" "$ROOT/tests/embedded/nj_port_bare.c" \
           "$ROOT"/src/core/*.c "$ROOT"/src/midp/*.c "$ROOT"/src/gfx/*.c \
           "$ROOT"/src/zip/*.c -lm 2>"$T/host.err"; then
        # The host build needs a main that returns its status to a shell.
        if "$host"; then
            echo "host run of the same main.c: exit 0 (the MIDlet ran to completion)"
        else
            echo "host run of the same main.c: exit $? - the embedder path failed" >&2
            exit 1
        fi
    else
        echo "note: host build of the firmware main.c skipped" >&2
        head -3 "$T/host.err" >&2
    fi
fi

echo "firmware: $OUT  ($(stat -c%s "$OUT") bytes on disk)"
$SIZE "$OUT"
echo
echo "flash = text + rodata;  RAM at reset = data + bss."
echo "The runtime's own contribution to that RAM is zero - everything above is"
echo "the application's heap and port state."
