# Static footprint

What the runtime costs before a single MIDlet byte is loaded, measured with the
flags an ESP32-S3 build would use. Reproduce with:

    tools/size_report.sh              # freestanding, -Os, PIC
    tools/size_report.sh --libc       # hosted build, for comparison

The report compiles every source in `src/` that does not need a platform SDK,
sums `.text`, `.rodata`, `.data` and `.bss` per file, and **fails if the last two
are not zero** - so the memory rule is checked, not asserted.

## Totals

| | bytes | KiB |
|---|---|---|
| `.text` (code) | 213,558 | 208.6 |
| `.rodata` (const tables) | 136,842 | 133.6 |
| **flash** | **350,400** | **342.2** |
| `.data` (initialised writable) | **0** | **0.0** |
| `.bss` (zero-initialised) | **0** | **0.0** |
| **RAM** | **0** | **0.0** |

That zero is the point of the whole design. Every byte the runtime needs at run
time comes from the heap the port hands it, and `nj_vm_destroy()` gives all of
it back - which is what `tests/test_lifecycle.c` holds to account over repeated
create/run/destroy cycles.

## Where the flash goes

| File | text | rodata |
|---|---|---|
| `nj_lib_lcdui.c` | 65,955 | 38,998 |
| `nj_lib_game.c` | 25,031 | 8,340 |
| `nj_lib_util.c` | 24,526 | 14,710 |
| `nj_interp.c` | 11,306 | 1,640 |
| `nj_gfx.c` | 10,428 | 210 |
| `nj_lib_string.c` | 8,541 | 6,672 |
| `nj_class.c` | 8,074 | 535 |
| `nj_jpeg.c` | 6,844 | 2,191 |
| `nj_vm.c` | 6,347 | 523 |
| `nj_lib_io.c` | 5,874 | 6,446 |
| `nj_lib_rms.c` | 5,451 | 3,130 |
| `nj_png.c` | 4,979 | 1,754 |
| `nj_font.c` | 860 | **33,452** |
| everything else | 29,342 | 18,241 |

Two things stand out, and both are choices rather than accidents:

* **`nj_lib_lcdui.c` is a third of the runtime.** It is 24 MIDP classes and 353
  native bindings. A build that does not need, say, `TextBox` or `DateField`
  could drop them, but nothing does yet - the class tables are `const` arrays
  with no per-class switch.
* **`nj_font.c` is 33 KB of `.rodata` and almost no code** - three bitmap fonts
  in plain, bold and two sizes each. That is the single easiest thing to shrink:
  a port that wants one 8x16 font instead of six faces saves about 25 KB of
  flash. It is `.rodata`, so it costs nothing in RAM either way.

JPEG (2.2 KB of tables) and PNG (1.8 KB) are both small because they are
arithmetic rather than table-driven - the JPEG IDCT is one 8x8 kernel, not the
usual four.

## A real firmware image, linked, for the real targets

The x86 numbers above are useful for comparing modules. The ones that matter for
an MCU come from cross-compiling and linking for the actual part:

    tools/make_bare_firmware.sh --target xtensa     # ESP32-S3 (Xtensa LX7)
    tools/make_bare_firmware.sh --target riscv32    # ESP32-C3/C6/H2 (RV32IMC)

The two ESP32 families are **different architectures** and need different
toolchains; the script finds the ESP-IDF ones under `~/.espressif/tools`
automatically.

Each run builds a complete firmware ELF with `-nostdlib`: the whole runtime, a
bare-metal `nj_port` (`tests/embedded/nj_port_bare.c`), a small implementation of
the maths functions it needs, and `bench.jar` embedded as a C array. It links,
which is the point - if anything in `src/` reached for a hosted C library, this
is where it would fail.

| Target | Architecture | `.text` | `.data` | `.bss` |
|---|---|---|---|---|
| **ESP32-S3** | Xtensa LX7 | 251,413 | **0** | 2,162,688 |
| **ESP32-C3/C6** | RISC-V RV32IMC | 303,582 | **0** | 2,162,688 |
| Cortex-M4 | ARMv7E-M | 244,550 | **0** | 2,162,688 |

`.text` includes `.rodata`, and is flash. The `.bss` figure is identical in all
three because it is **the application's**, not the runtime's: 2 MB of Java heap
plus 64 KB of port state, declared in the generated `main.c`. **The runtime's own
contribution to RAM is zero on every one of them** - measured by a real linker
for a real architecture, not asserted.

The RISC-V build is 21% larger than the Xtensa one; both are the same C at -Os,
so that is the compilers, not the code.

The `.bss` figure is 2 MB of Java heap plus 64 KB of port state, both declared by
the application in `main.c`. **The runtime's own contribution to RAM is zero** -
measured on a 32-bit target with a real linker, not asserted.

Two things this image demonstrates that the x86 report cannot:

* An embedded application uses the **public API only** - `nj_vm_create`,
  `nj_app_open`, `nj_vm_new_midlet`, `nj_vm_start_app`, `nj_midp_frame`,
  `nj_vm_destroy`. `src/runner/nj_run.c` is deliberately **not** in the image: it
  is a desktop convenience that reads files and calls `malloc`.
* `-nostdlib` is a real constraint, and satisfying it found two genuine bugs
  (see below).

## Running it, not just linking it

`~/.espressif/tools` also provides the ESP-IDF QEMU builds, which model both
parts:

    qemu-system-xtensa  -machine esp32s3  ...   # Espressif ESP32S3 machine
    qemu-system-riscv32 -machine esp32c3  ...   # Espressif ESP32-C3 machine

`tests/embedded/nj_port_bare.c` writes its log to the UART0 FIFO at
`0x60000000` - the same address on both parts - so the image reports for itself
rather than depending on a debugger:

    NJ2ME DONE rc=0 frames=600

**Status: not yet running under QEMU.** `-kernel fw.elf` loads the image and the
machine starts, but an ESP32 does not begin at the ELF entry point: the mask ROM
runs first and expects a bootloader at the start of flash, so `main` is never
reached and nothing is printed. Two ways forward, both contained:

1. Build a flash image (bootloader + partition table + app) and boot it with
   `-drive file=flash.bin,if=mtd,format=raw`. ESP-IDF's `idf.py qemu` does
   exactly this, and the IDF tree at `~/.espressif/v5.5.4/esp-idf` is present, so
   this is a matter of wrapping the runtime as an IDF component.
2. Use QEMU's `boot=` machine option, which the esp32s3 machine exposes, to skip
   the ROM.

Until one of those is done, the claim this project can make is **"builds, links
and is sized for the real architecture"**, not "runs on it". The image is one
step away, and the step is a bootloader - not a gap in the runtime.

## Known gap: NJ_ENABLE_FLOAT does nothing

`nj_config.h` documents `NJ_ENABLE_FLOAT=0` as compiling `java.lang.Math` down to
integer operations and removing the libm transcendentals. **It does not.** The
macro is defined and nothing honours it: `src/core/nj_lib_math.c` calls `pow`,
`log`, `exp`, `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`,
`log10` and `cbrt` unconditionally.

So the honest description of this runtime is *"freestanding except for a minimal
libm subset"*, and the required subset is larger than the switch claims:

* always required: `memcpy memmove memset memcmp strlen strcmp strncmp strchr`,
  `tolower toupper`, `fmod floor ceil rint` (`fmod` for the `frem`/`drem`
  bytecodes, the rounding ones for `java.lang.Math`'s integer paths)
* required unless the MIDlet never calls those methods: the transcendentals

`tests/embedded/nj_port_bare.c` supplies all of them - real implementations for
the small ones, explicit stubs for the transcendentals - so the dependency is
**visible in one file** rather than spread across a link error.

Making `NJ_ENABLE_FLOAT=0` real is a contained job: guard the libm-using natives
in `nj_lib_math.c` so they raise `ArithmeticException` (or return the integer
approximation) when the switch is off. Until then, treat the switch as
unimplemented.

## What is not in the report

* `src/port/esp32/nj_port_esp32.c` compiles (34 bytes of code) but its real size
  depends on ESP-IDF's own drivers, which are not part of this runtime.
* The Android port needs its NDK, so it is not counted.
* The Java heap is not here: it is `NJ_MAX_HEAP` (default 2 MB, set per port),
  allocated once at `nj_vm_create()` from the port's allocator. On an ESP32-S3
  that is the number to tune, and PSRAM is what it should come from for anything
  larger than a few hundred kilobytes.

## External dependencies in a freestanding build

`NJ_USE_LIBC=0` is not "no libc at all" and the documentation used to say it was.
The complete list is in `include/nj2me/nj_libc.h` and it is:

* `<string.h>`: `memcpy memmove memset memcmp strlen strcmp strncmp strchr`
* `<ctype.h>`: `tolower toupper`
* `<math.h>`: `fmod floor ceil rint` always, plus `sqrt sin cos tan asin acos
  atan atan2 exp log log10 pow cbrt` for `java.lang.Math`. `NJ_ENABLE_FLOAT=0`
  is documented as removing the second group and does not - see the known gap
  above.

Everything else goes through `nj_port`: allocation, the clock, sleeping, file
access and logging. `tools/size_report.sh` compiles all 30 sources in that mode,
so this list is checked rather than claimed; an undefined reference outside it is
a bug in the runtime, not a gap for the port to fill.

newlib - and so ESP-IDF - provides all of the above already. A bare-metal port
with no FPU would use `NJ_ENABLE_FLOAT=0` instead of supplying a soft-float
library.
