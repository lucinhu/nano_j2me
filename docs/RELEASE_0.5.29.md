# nano_j2me 0.5.29

Release date: 2026-11-22.  This is **r48** of the libretro core.

## Highlights

### Android: hardware acceleration (GLES2) is now the default

The Android release APKs used to be software-only binaries even though the
runtime carried a complete GLES2 backend.  Two separate switches kept it off:
the release scripts built without `--gles2`, and the in-game setting defaulted
to software.  Both are fixed:

* `tools/build_android_apks.sh` builds every release APK with the GLES2 backend
  (pass `--no-gles2` for the old software-only binaries).
* A capable build now requests hardware acceleration on the next game launch;
  the panel row **硬件加速（GLES2）：开/关（下次启动）** is the per-device
  software fallback.
* The legacy ARMv5/API 5 APK stays software-only.

The backend creates a private EGL pbuffer context and replaces the port's
raster/triangle callbacks, with a one-time calibration and a per-device
software fallback.  The optional 2D fill/blit wrappers stay off: their
synchronous readback measured slower than the software blitter, so 2D canvases
keep the software renderer.  Verified on an AVD (API 36, x86_64):

```
I nj2me : gles2: enabled renderer=Android Emulator OpenGL ES Translator (Google SwiftShader) (private pbuffer)
```

and the in-game panel reports `硬件加速（GLES2）：开` with the live
`本局启动选择：硬件加速（GLES2）` status.

### Cheat search: control-variable filtering and one-shot writes

The Android cheat panel already searched exact values and narrowed the list.
It now also supports the classic unknown-value workflow and both write modes:

* Core API — `nj_vm_cheat_snapshot`, `nj_vm_cheat_scan_changed`,
  `nj_vm_cheat_filter_changed` and `nj_vm_cheat_write` in
  `src/core/nj_cheat.c`: record a baseline, then keep addresses whose 1/2/4-byte
  value changed, stayed equal, increased or decreased.
* Android UI — panel → **搜索作弊数值** → **变化搜索**: record a baseline,
  change the value in the game, then tap 变了/没变/变大/变小; the list rebases
  after each pass so repeated passes keep narrowing.  Tapping a candidate now
  offers **持续修改** (write a code re-applied every frame) or **单次修改**
  (write once and let the game overwrite it).
* Verified on the emulator: `cheats: snapshot 125829120 byte(s)`,
  `cheats: change scan filter=0 width=4 -> 73 hit(s)`,
  `cheats: single write width=4 -> ok`; the candidate dialog showed live values.
* Limits: the baseline is one arena-sized native copy (recording fails cleanly
  when memory is short), candidate lists keep the first 512 hits and display
  the first 30, and a whole-arena change scan is intentionally noisy until the
  narrowing steps.

### README

The README now states that the project is AI-programming-driven with thanks to
DeepSeek, and links the [web emulator](https://lucinhu.github.io/nano_j2me/)
and the [ESP32-S3 Korvo port](https://github.com/lucinhu/esp32s31-korvo-nano-j2me)
from the top and from the platform table.

## Verification

* Full CTest: **207/207** tests.
* libretro mock frontend matrix: **20/20**.
* Real RetroArch 1.22.2, LoopDemo 120 frames: **PASS**.
* Windows x86_64 under Wine, four scenarios at 300 frames: **4/4**.
* Submission bundle validator: **32/32**.
* Android emulator (API 36, x86_64): GLES2 enabled by default and the cheat
  change search exercised end to end (see above).


* 900-frame whole-corpus deep probe (1036 rendered titles): **1023 passed,
  11 early exits, 2 load/budget timeouts** (`TETRIS®`, `时光樱花册-遗失之心` —
  both self-paced/load-heavy titles that pass standalone).


* Full 1050-row corpus sweep on the 0.5.29 CLI: **1036 ok / 4 title-exception /
  4 bad-jar / 2 timeout / 2 title-exit / 2 no-frame** — identical verdict set
  to 0.5.28 (zero changes); matrix merged to **1817 titles / 1799 ok**.

## Download

| File | Size (bytes) | SHA-256 |
| --- | --- | --- |
| `nj2me-0.5.29-libretro-cores.zip` | 12,086,256 | `c1bef7e1f3de139c3cae067fcd2cc182d217d85f20c40589051cafe019c8436a` |
| `nj2me-0.5.29-arm64-v8a.apk` | 1,042,588 | `976594e82e8deced535fe35971bce741ea2440ab0b69d22cd2af5decb9f0ca99` |
| `nj2me-0.5.29-armeabi-v7a.apk` | 1,054,878 | `f708879b814d4613aa921f8de8d835a24dd5247f51041896b2d65591fb2d409b` |
| `nj2me-0.5.29-x86.apk` | 1,034,390 | `bd1655d6c38bdce8884349e03f1016350291463ef5bf82fee45565b4dcdc105d` |
| `nj2me-0.5.29-x86_64.apk` | 1,050,777 | `0e9d5148c0ce699c8c69987f5c09a08d3249fbe120cbd68c22d8171795a2439a` |
| `nj2me-0.5.29-legacy.apk` | 732,635 | `3122ef9c8db58974848481c86f4717c416b45ddfeeae788698bbcd5bdb056162` |

Android APKs are split per ABI (`versionCode 529`); use the one matching the
device (`arm64-v8a` for current phones, `armeabi-v7a` for older 32-bit phones,
`x86`/`x86_64` for emulators).  The legacy APK is the ARMv5 / API 5 build.

## Core binaries

All eight libretro cores in the zip identify as 0.5.29 (`.info`
`display_version = "0.5.29"`, SHA-256
`88b6276d42f6dc8af2c8f17139c1aa86efad45437337e757d833eb8c9c95e81c`).

| Target | Size (bytes) | SHA-256 |
| --- | --- | --- |
| linux-x86_64 | 4,003,760 | `7915785243bbd235452fd66394d87cd8e74fcf3e6b5b482fdc816c7e4873a791` |
| linux-aarch64 | 3,478,912 | `d974246627d215f77dde9cc1e95a9c10a1411185b483ae4183b88912a086cb51` |
| linux-armv7 | 3,142,948 | `fbc1a636079b420705aea49ba6bc669309ec432fc4eaef40eb9bb55b78b82472` |
| android-arm64 | 3,720,824 | `9268693e39a2bd71c9bd181bbba7c508329f368137e6fc5c3b42fc0df14e1f60` |
| android-armv7 | 2,837,100 | `3020b17fd80dee38f59d30de15e4ef241e67345bf9eef53d4fbbfbddc56bcdfa` |
| android-x86_64 | 3,749,760 | `e8d4bf68500293bafd6d174f0502187b5ba2c14dbd45b0d197e7f0fa3b4705d7` |
| windows-i686 | 3,568,128 | `0e1865584d2264b4ecc99858b6665bf071ab3d4979008ebd89e9d0809aa9b4ce` |
| windows-x86_64 | 3,339,264 | `f7f880d69294ba497ac768c440dda92789b19e878365a68bff40654b4594d813` |

## Upgrade notes

* Save states are tied to the runtime version: states written by 0.5.8 through
  0.5.28 are refused by the version CRC and need a fresh save.
* The GLES2 default only applies to Android release APKs built from this
  version onward; older APKs remain software-only.  Use the panel row to fall
  back per device if a title misbehaves on the GPU path.

## Known limitations

* `MidnightPool1` renders in the CLI but stays black through the libretro core:
  its per-paint frame limiter busy-waits 50 ms of `System.currentTimeMillis`
  inside `paint()`, which the synchronous libretro frame model cannot satisfy.
* `PAC-MAN (176x220)` is title-side/network-limited (empty Sprint `Muglet` URI
  in its constructor; the FreeJ2ME reference hangs on it too).
* Titles that spend more than the 4 s state-service timeout inside a single
  frame cannot be serialized by a frontend mid-frame (same slow-frame class as
  `MidnightPool1`/`Asphalt4EliteRacing`).
* `毁灭战士3_10598.jar` references unused JSR-226 `m2g.SVG*` symbols; the title
  runs and never resolves them.
