# Android baseline JIT: executable memory and a measured result (2026-11-15)

Minecraft PE on Android is interpreter bound (see
[2026-11-01-minecraft-android-costs.md](2026-11-01-minecraft-android-costs.md)),
so the obvious next lever was the opt-in baseline JIT in this tree. It had
never run on Android: the port lacked the executable-memory callbacks the JIT
contract requires (`code_alloc`/`code_commit`/`code_free` in
`include/nj2me/nj_port.h`), and only the POSIX headless port implemented them.
This note records what was added and the measurement that followed, which is
negative: on this workload the JIT is slower than the interpreter, so it stays
opt-in and the APK keeps shipping the interpreter.

## What was added

`src/port/android/nj_port_android.c`:

* `android_code_alloc` maps anonymous read/write pages with `mmap`, records the
  mapping length in a header word, and returns the payload.
* `android_code_commit` synchronises the instruction cache and flips the
  mapping to `PROT_READ|PROT_EXEC` (W^X: never writable and executable at the
  same time). `android_code_free` unmaps the same range.
* The instruction-cache flush is inline aarch64 code (`dc cvau` / `dsb ish` /
  `ic ivau` / `dsb ish; isb`) rather than `__builtin___clear_cache`, because
  the compiler-rt routine keeps a cached `CTR_EL0` in `.bss` and
  `tools/build_android.sh` enforces zero writable static state. x86 has
  coherent instruction caches, so the helper is a no-op there.
* All of it is behind `#if NJ_ENABLE_JIT`; the shipped build compiles it out.

`tools/build_android.sh`:

* `NJ_ANDROID_JIT=1` adds `-DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=1
  -DNJ_JIT_STATS=1 -DNJ_JIT_DETAILED_STATS=1`.
* `NJ_ANDROID_JIT_CACHE`, `NJ_ANDROID_JIT_METHODS` and `NJ_ANDROID_JIT_ARENA`
  override `NJ_JIT_CACHE_ENTRIES`, `NJ_JIT_METHOD_ENTRIES` and
  `NJ_JIT_METHOD_ARENA_BYTES` for A/B experiments.

The resulting APK builds clean: both ABIs report `data+bss=0` in the static
gate, and the JIT really executes on device — the app process shows anonymous
`r-xp` mappings of a few pages, which are the emitted blocks.

## Measurement

Host: API-36 x86_64 emulator (`OpenHV_API36_googleapis`, userdebug, KVM, host
GPU, `-memory 6144 -cores 4`), 240x320 MIDlet screen, heap 32 MB, unmodified
`MinecraftJ2ME_PE_0.6.1_V2.2.jar`. The same saved world is loaded by each build
through the same `GameActivity` intent; the game prints `MCJ: LOAD` itself.
`-Os` NDK clang for all three builds; they differ only in the JIT defines.

| build | `LOAD read` | `LOAD prepare` |
| --- | ---: | ---: |
| interpreter (shipped configuration) | **6,951 ms** | **7,818 ms** |
| JIT, default cache (64 entries / 32 probe maps) | 10,753 ms | 16,150 ms |
| JIT, tuned (512 entries / 256 maps / 64 KB arena) | 11,837 ms | 16,233 ms |

World creation on the same emulator with the interpreter build measured
`MCJ: GEN 26,539 ms, save 5,382 ms`; the JIT was not measured there because the
load result already settles the question.

The desktop fixed-work GEN benchmark agrees in direction. With the current tree
(`tools/bench_minecraft_gen.c`, original JAR, seed 12345, CPU-pinned, one run
per mode) the interpreter takes 14,707 ms of CPU and JIT execute 14,789 ms —
level, and that is with the JIT disabling superinstructions. The older
2026-11-10 diagnostic measured JIT *losing* by 24% on the same benchmark; the
recent JIT commits closed most of that gap, but did not turn it into a win.

## Why it loses here

* Installing the JIT disables the interpreter's superinstructions, and the
  Android APK depends on them: the threaded body alone was worth 1.65x on
  world creation. A block compiler that only covers ~44% of executed bytecodes
  cannot pay that back.
* Minecraft's hot code is calls, allocations, static fields and non-int array
  accesses. The ARM64 encoder ends blocks before `getstatic`/`putstatic` and
  non-`int` arrays, so native windows stay short (10.7 bytecodes per native
  entry in the desktop diagnostic) and every window pays probe and entry cost.
* The bounded cache thrashes on this method mix: the 2026-11-10 diagnostic
  counted 991k compiled blocks and 991k evictions over one GEN run. Enlarging
  the cache here did not change the outcome, so entry/probe cost and lost
  superinstructions dominate, not capacity.

## Decision

The JIT stays opt-in and unshipped. `NJ_ANDROID_JIT=1` builds an experimental
APK for device experiments; the release build keeps the interpreter, which is
the fastest measured configuration for Minecraft on this class of device. A
future JIT would need superinstruction-compatible dispatch and coverage of
static fields and typed arrays before it is worth revisiting.

Reproduce:

```sh
NJ_ANDROID_JIT=1 tools/build_android_apk.sh --out /tmp/jit-apk --abi x86_64
# install on an API-36 x86_64 emulator, push the JAR to
# /sdcard/Android/data/com.lucinhu.nj2me/files/jars/, launch .GameActivity with
# --es jar/--es jarpath/--ei w 240 --ei h 320 --ei heap 32, load a saved world,
# and compare the MCJ: LOAD lines against the interpreter build.
```
