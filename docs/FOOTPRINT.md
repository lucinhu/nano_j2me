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
* `<math.h>`: `fmod sqrt sin cos tan asin acos atan atan2 exp log log10 pow
  ceil floor cbrt` - and `NJ_ENABLE_FLOAT=0` removes every one of them, by
  compiling `java.lang.Math` down to its integer operations.

Everything else goes through `nj_port`: allocation, the clock, sleeping, file
access and logging. `tools/size_report.sh` compiles all 30 sources in that mode,
so this list is checked rather than claimed; an undefined reference outside it is
a bug in the runtime, not a gap for the port to fill.

newlib - and so ESP-IDF - provides all of the above already. A bare-metal port
with no FPU would use `NJ_ENABLE_FLOAT=0` instead of supplying a soft-float
library.
