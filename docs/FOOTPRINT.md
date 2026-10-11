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

### Under ESP-IDF, on the real target

`tests/embedded/idf/` is a complete ESP-IDF project with the runtime as a
component (`components/nj2me`). It builds, and it boots:

    . ~/.espressif/v6.0.1/esp-idf/export.sh      # v6.0.1 matches the 15.2.0 toolchain
    cd tests/embedded/idf && idf.py set-target esp32s3 build
    idf.py merge-bin -o flash.bin && truncate -s 2M flash.bin
    qemu-system-xtensa -machine esp32s3 -nographic \
        -drive file=flash.bin,if=mtd,format=raw \
        -global driver=timer.esp32s3.timg,property=wdt_disable,value=true

Everything compiles against real IDF headers - including `nj_port_esp32.c`,
which is a stronger check than any stub-header compile - and QEMU runs the
result on an emulated ESP32-S3:

    ESP-ROM:esp32s3-20210327
    rst:0x1 (POWERON),boot:0x4 (SPI_FLASH_BOOT)
    I (12) boot: ESP-IDF v6.0.1 2nd stage bootloader
    I (18) boot: Partition Table:
    I (18) esp_image: segment 0: paddr=00010020 vaddr=3c040020 size=2126ch (135788) map
    ...
    I (64) app_init: Project name:     nj2me_esp32
    I (67) main_task: Calling app_main()
    I (70) nj2me: nano_j2me on esp32s3, heap 120 KiB, free internal 193 KiB
    I (70) nj2me: sizeof(nj_vm)=5368, arena budget 65536
    E (70) nj2me: nj_vm_create failed

So the ROM boots, the second-stage bootloader reads the partition table, the
application image is loaded and `app_main` runs.

**The runtime now comes up on emulated ESP32-S3.** `nj_vm_create()` succeeds,
`nj_app_open()` succeeds, `nj_vm_new_midlet()` succeeds - the whole VM with every
builtin class registered - and then `nj_vm_start_app()` returns a failure status
with no exception set, so the MIDlet's `startApp()` never runs:

    I (72) nj2me: sizeof(nj_vm)=3832, arena 136 KiB
    E (82) nj2me: nj_vm_start_app failed: (no exception)
    NJ2ME DONE rc=1 frames=0

With the status printed rather than just "failed", the picture is precise:

    I (70) nj2me: sizeof(nj_vm)=3832, arena 136 KiB
    E (80) nj2me: nj_vm_start_app failed: status -1, exception (no exception)
    I (190) nj2me: frames=300 bytecodes=0 natives=0 heap=720/720
    NJ2ME DONE rc=0 frames=600

Three facts together, and the third is the one that matters:

* the status is `NJ_ERR`, the generic failure, and there is no exception;
* **zero bytecodes were executed** - the interpreter loop never ran once, so
  `nj_vm_start_app` returned before reaching `nj_exec`;
* **`heap=720` bytes**.  A desktop run of the same fixture ends with about
  1.1 MB live.  The VM is essentially empty, so this is not "the MIDlet failed
  to start" - it is that almost nothing was loaded in the first place.

`vm_invoke` has exactly two ways to return `NJ_ERR` without an exception, and
both are in the path taken before `nj_exec`: `!vm->running` (which returns
`NJ_ERR_STATE`, -8, so not this one) and `nj_class_init()` failing with an
exception already pending that is then not visible.  The 720-byte heap points
at the same place from the other side: whatever should have loaded the MIDlet
class did not.

Next: log the class count and the interned-string count right after
`nj_app_open()`, and check `nj_class_init()`'s failure paths for one that can
return without leaving a visible exception.  A `nj_vm_get_stats()` call there
would say in one line whether the app opened at all.

## Three bugs the ESP32 bring-up found, all fixed

**The port allocator contract was undocumented and unmet.** `nj_heap_init()`
rejects an arena whose base is not `NJ_OBJ_ALIGN` (16) aligned, because the arena
holds long and double fields - and it can only reject, so a port that aligns to 8
fails with "nj_vm_create: heap failed" and nothing else. The desktop `malloc`
happened to satisfy it; a bump allocator over a static array did not.
`NJ_OBJ_ALIGN` is now public in `nj_config.h`, the contract is written in
`nj_port.h`, and `nj_heap_init()` says which of the two possibilities failed.
The IDF image now uses `heap_caps_aligned_alloc(NJ_OBJ_ALIGN, n,
MALLOC_CAP_SPIRAM)` and gets both PSRAM and alignment from the SDK, which is also
what `nj_port_esp32.c` does on real hardware.

**The pool handed to `nj_vm_create()` covers three things, not one:** the VM
instance, the Java arena, and the interned-string/constant tables plus the native
registry, which want about 32 KiB each. Sizing it as "sizeof(nj_vm) + java heap"
fails with "statics failed".

**`NJ_MAX_STACK_SLOTS` defaults to 64 KiB of slots per Java thread** (8192 slots
x 8-byte `nj_value`), which is a desktop number - with 16 threads it is a
megabyte of stacks, and the symptom is "nj_vm_create: main thread failed" with
nothing to suggest memory. The IDF component now sets 1024 slots (8 KiB), 96
frames and 128 interned strings, next to the reason, rather than leaving an
embedder to discover them one crash at a time.

## The RISC-V side: builds, boots, and runs out of internal RAM

The same project builds for **ESP32-C3** and boots on an emulated one.  The
whole runtime compiles for RV32IMC, and the link needed one fix:

    multiple definition of `memcpy'; esp-idf/esp_libc/libesp_libc.a(memcpy.c.obj)

`tests/embedded/nj_port_bare.c` supplies the nine C library functions the
freestanding image needs, and under ESP-IDF there is already a libc.  They are
now behind `#if !NJ_USE_LIBC`, which is what they were always for.  The Xtensa
link happened to tolerate the duplicate; relying on that would have been luck.

Once it links, the C3 does not have the RAM:

    I nj2me: free: psram 0 KiB, internal 295 KiB
    I nj2me: pool 244 KiB
    I nj2me: sizeof(nj_vm)=3832, arena 122 KiB, reserved 122 KiB
    nj2me: nj_vm_create: main thread failed

An ESP32-S3 leaves 356 KiB free and this VM fits; an ESP32-C3 leaves 295 KiB and
it does not - the pool covers the VM instance, the Java arena, the statics, the
native registry and the builtin classes, and those want more than the C3 has.
That is not a defect in the runtime, it is the reason `nj_port_esp32.c` routes
the Java heap to PSRAM in the first place: a part without it cannot run this.
Which is also why this environment cannot finish the RISC-V demonstration - the
path a real C3 would take is the PSRAM path QEMU does not provide.

So the two-architecture claim, stated precisely:

| Target | Architecture | State |
|---|---|---|
| ESP32-S3 | Xtensa LX7 | **runs a MIDlet** - 112,117,067 bytecodes, same as the desktop |
| ESP32-C3 | RISC-V RV32IMC | compiles, links, boots, creates the VM; needs PSRAM for the rest |

## Why PSRAM does not work under QEMU

Not a configuration mistake, and not a branch problem. QEMU's `esp32s3` machine
does create an `ssi_psram` device (asking for another one reports "CS index 0x0
in use"), but IDF's PSRAM init reads the chip's ID over SPI and gets nothing
back, so it reports:

    E (59) quad_psram: PSRAM chip is not connected, or wrong PSRAM line mode

and retries until it gives up. That is [espressif/qemu#129][psram], still open.
It is **not** fixed by the `esp-develop` branch as installed: this was tested
against `esp_develop_9.2.2_20260417`, the newest release, downloaded and run for
the purpose, and it fails identically to `20250817`.

[psram]: https://github.com/espressif/qemu/issues/129

So the QEMU image runs with `CONFIG_SPIRAM=n`. That is not a workaround that
hides anything: with SPIRAM off IDF leaves **356 KiB** free rather than 133 KiB,
because the PSRAM init itself is what consumes the difference - so the emulated
run has more usable RAM without it. The code keeps the PSRAM path
(`MALLOC_CAP_SPIRAM` first, internal RAM as fallback), so a real board takes the
path this environment cannot exercise.

DECISION: the framebuffer is 64x64 and the heap comes from internal SRAM.  The
real port puts the heap in PSRAM and draws to a panel; this image exists to prove
the runtime executes on the real instruction set, and QEMU's `esp32s3` machine
did not bring quad PSRAM up in testing (`quad_psram: PSRAM chip is not
connected`).  QEMU does model it - ESP-IDF's own `qemu_ext.py` passes
`-global driver=ssi_psram,property=is_octal,value=true` when `CONFIG_SPIRAM_MODE_OCT`
is set - so that is worth revisiting once the heap question above is answered.

Until that is done the claim this project makes is **"the runtime compiles
against ESP-IDF, links into a bootable ESP32-S3 image, and that image boots to
app_main on an emulated ESP32-S3"** - not "runs a MIDlet on it". The remaining
gap is arena placement, which is a port configuration question.

## NJ_ENABLE_FLOAT now does what it says (it used to do nothing)

`nj_config.h` documents `NJ_ENABLE_FLOAT=0` as removing the libm transcendentals
from the external-symbol set.  It was broken in **two independent ways at once**:
nothing in `src/` read the switch, and CMake never passed it to the compiler - so
`-DNJ_ENABLE_FLOAT=0` built a runtime that still linked every one of them.

Both ends are fixed.  With the switch off the twelve transcendental natives
raise `ArithmeticException` naming the method and the reason, and the object file
contains **zero** references to them; `ceil`, `floor`, `rint` and `fmod` stay,
because the numeric bytecodes need them.

So the honest description of this runtime is *"freestanding except for a minimal
libm subset"*, and the required subset is larger than the switch claims:

* always required: `memcpy memmove memset memcmp strlen strcmp strncmp strchr`,
  `tolower toupper`, `fmod floor ceil rint` (`fmod` for the `frem`/`drem`
  bytecodes, the rounding ones for `java.lang.Math`'s integer paths)
* required unless the MIDlet never calls those methods: the transcendentals

`tests/embedded/nj_port_bare.c` supplies all of them - real implementations for
the small ones, explicit stubs for the transcendentals - so the dependency is
**visible in one file** rather than spread across a link error.

`tools/check_no_float.sh` keeps it honest: it compiles `nj_lib_math.c` both ways
and counts the symbols, so the switch cannot rot back into a no-op.

     NJ_ENABLE_FLOAT=1: 12 libm transcendentals
     NJ_ENABLE_FLOAT=0:  0 libm transcendentals

A full build with the switch off reproduces the benchmark byte for byte
(112,117,067), and a MIDlet that calls `Math.sin` gets

     java.lang.ArithmeticException: java.lang.Math.sin is not available:
     NJ_ENABLE_FLOAT=0 removes the libm transcendentals from this build

which is the loud failure this project prefers to a link error that names a
symbol and not the code that wanted it.

## Android: built with the real NDK, and run on an emulator

    tools/build_android.sh          # libnj2me.so for arm64-v8a, armeabi-v7a, x86_64
    tools/build_android_apk.sh      # a signed, installable APK
    tools/build_android_apk.sh --install    # ...and launch it

Both are real cross-builds with NDK r28b, not stub-header compiles:
`nj_port_android.c` includes `<jni.h>`, exports
`Java_com_example_nj2me_Nj2meActivity_native*`, and links against liblog and
libandroid. The shared library passes the same zero-writable-state gate as
everything else, by section name:

| ABI | libnj2me.so | `.data` + `.bss` | `.data.rel.ro` |
|---|---|---|---|
| arm64-v8a | 758,952 | **0** | 67,216 |
| armeabi-v7a | 572,768 | **0** | 34,204 |
| x86_64 | 684,536 | **0** | 67,840 |

The APK is assembled by hand from aapt2, d8, zipalign and apksigner rather than
Gradle - this repository has no Gradle wrapper and nothing to download, and what
the hand-built APK is for is proving the native side is complete: a valid
manifest, dexed Java, the `.so` in `lib/<abi>/`, the game in `assets/`.

**It runs.** Installed on an API 26 x86_64 emulator, the activity starts, the
library loads, the surface is created and a real MIDlet draws through the
built-in LCDUI - [screenshot](images/android-maze.png), `maze.jar` at 320x640.
[images/android-alert.png](images/android-alert.png) is the same port rendering
an Alert, which is what the size-gated titles do (below).

### The bug that only running found

The first APK installed, started, and died:

    java.lang.UnsatisfiedLinkError: dlopen failed: cannot locate symbol "nj_run"
      referenced by libnj2me.so

The source list did not include `src/runner/nj_run.c`. A shared library is
allowed to have undefined symbols, so it built, signed, installed and then could
never load. Two fixes, and the second is the one that matters:

* add the missing source;
* **link with `-Wl,--no-undefined`**, so the next missing file is a link error
  instead of a crash on someone's phone.

### The MIDlet gets a device-sized screen, letterboxed into the Surface

A J2ME game was written for a fixed device screen and many check it: Tumbleweed
throws "Screen too large" above 250 in either axis.  A modern phone reports
640x1280, so handing the game the Surface meant every such title refused to
start - which is what happened the first time this port ran a real one.

The port now keeps a LOGICAL screen (240x320 by default, `--screen WxH` at build
time) and scales it into the Surface:

* the runtime sees the logical size, so a title's own screen-size gate passes;
* `display_present()` scales by the largest INTEGER factor that fits, centred,
  with black bars - integer because J2ME art is pixel art and a fractional scale
  blurs every sprite edge;
* input is forwarded in logical coordinates.

[screenshot](images/android-tumbleweed.png) is Tumbleweed playing on an emulated
Pixel-class device: 176x208, letterboxed, with the cowboy, an enemy, the grass,
a countdown and a score.

**The bug this exposed is worth recording.**  `nj_port_android_set_window()`
overwrote the logical size with the window size - a leftover from when the
window *was* the screen - so `nj_port_android_ex(out, 176, 208)` had no effect
and the port kept reporting 640x1280.  From outside, "it started and shows an
Alert" and "it started with the wrong screen" are the same observation.  What
told them apart was one log line:

    display_init: logical 640x1280, window 640x1280    <- before
    display_init: logical 176x208, window 640x1280     <- after

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
