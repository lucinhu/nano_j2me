# libretro core rebuild after the phone-discovered fixes (2026-11-14)

The eight installable cores in `build/libretro-dist/` were rebuilt from the
current tree. The previous package (2026-11-13) was made before the Android
load-lifecycle bug, the Java monitor fix, the serviceRepaints/paint deadlock fix
and the new frontend features, so those binaries could boot-loop on Android even
though they built cleanly.

What changed in the sources since that package is recorded in
`build/libretro-final-report-20261114.md` and in
`docs/notes/2026-11-14-cooperative-scheduling-startup-hangs.md`. The short
version:

- `retro_load_game()` no longer calls `SET_SYSTEM_AV_INFO`; the frontend asks
  `retro_get_system_av_info()` after loading, and on Android the old callback
  initialised EGL a second time and aborted the video driver.
- A `synchronized` `run()` now owns the Thread monitor from its first bytecode,
  so `wait()`/`notify()` inside it no longer throw
  `IllegalMonitorStateException` (三国塔防－曹操传, 空战1944).
- `serviceRepaints()` called off the UI thread parks until the in-flight paint
  finishes, and the painter releases it at the frame boundary; this ends the
  `明星制造梦工厂` paint spin without a busy loop (tonnes of bytecode saved).
- `MIDlet.getAppProperty` trims the manifest value, which fixes the 3D kart
  title's `Integer.parseInt("0 ")` crash.
- New options and hooks: `SET_INPUT_BITMASKS`, rumble (Nokia DeviceControl and
  Display.vibrate → RETRO_RUMBLE_WEAK), `nano_j2me_screen`,
  `nano_j2me_locale`.

Verification of the rebuilt artifacts (logs under
`.local-tools/debug-runs/libretro-rebuild-20261114/`):

| target | check | result |
| --- | --- | --- |
| linux-x86_64 | `tools/check_libretro_frontend.sh` mock matrix | 20 passed, 0 failed, 0 skipped |
| linux-x86_64 | real RetroArch 1.22.2, loopdemo 300 frames | PASS, running + run-ended markers |
| linux-x86_64 | real RetroArch, textdemo 180 frames, screen/locale/heap options | PASS; kept log `Geometry: 176x208` |
| windows-x86_64 | `tools/check_libretro_windows.sh --frames 120` under Wine | 4/4 fixtures PASS; audio 88,200 frames, 41,354 nonzero, peak 19,999 |
| linux-x86_64 | real RetroArch, 浴火银河2完整版_7689.jar (M3G 3D) 300 frames | PASS; rotation 0 → `Geometry: 240x320`, rotation 90 → `Geometry: 320x240` |
| current tree | `ctest --test-dir build-engine-mem -j1` | 188/188 PASS, 357.57 s |
| android-arm64/armv7/x86_64, linux-aarch64/armv7 | `file`, `readelf`, `objdump` | correct ABI; Android needs only libm/libdl/libc; Windows imports KERNEL32 + UCRT |

The Android cores were not executed on a device in this rebuild: the phone was
handed back to its owner after the earlier test, and the run that proved the
load fix on hardware used the then-current android-arm64 build. That limitation
is stated in the report rather than glossed over.

Packaging: each target directory has the same 16 files (core + `.info` +
`LICENSE` + `BUILD_STATUS.txt` + notice bundle); the 120 non-binary files are
byte-identical across targets. The old distribution was archived outside the
tree (`/var/tmp/libretro-dist-20261113`, tarball
`/var/tmp/libretro-dist-20261113-backup.tgz`) and the 2026-11-13 report is
marked superseded.
