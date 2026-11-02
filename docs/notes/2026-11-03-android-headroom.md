# Android in-world headroom audit (round 7)

Goal: find out where the remaining in-world frame cost goes on the Android
target, using real on-device sampling rather than desktop numbers, and either
fix one clearly attributable hotspot or leave a ranked candidate list plus a
reusable profiling recipe. Result: the audit worked, but on this AVD there is
no user-visible frame cost left to attack — the emulator is vsync-bound at
60 Hz while the emulated CPU uses about a fifth of the frame budget. The
deliverable is therefore the attribution, the headroom measurement and the
recipe; no source change is justified by this data.

## Setup

- Emulator: AVD `NJ_API21` (x86_64, API 21, 320x640), `-gpu swiftshader_indirect
  -memory 2048 -port 5570`, `adb root`. Same AVD as earlier rounds.
- APK: built from main `3bf10cf` with
  `./tools/build_android_apk.sh --abi x86_64 --out ...` (default `-Os`, JIT off),
  919,705 bytes; installed with `adb install -r`; JAR pushed to
  `/data/local/tmp/MinecraftJ2ME_PE_0.6.1_V2.2.jar`.
- World: the 22 MB "Unnamed world" fixture (753K blocks) from
  `.local-tools/mc051/fs`, pushed into
  `/data/data/com.lucinhu.nj2me/files/fs` with `chown 10061:10061`.
- Frame cap: the port stores it in `shared_prefs/display.xml` as
  `<int name="frameCap" value="N"/>` (`-1` follows the build default, `0` is
  uncapped, `60` is the 60 fps cap); the native side logs
  `fps: cap N` / `fps: cap 0 = uncapped` when it applies the value.
- Navigation: the game is portrait-locked in a 240x320 area at window offset
  (40,31). Title `Start Game` at (160,160), world card at (160,220). After a
  fresh install/force-stop the touch injection sometimes lands before the title
  screen is interactive; `input keyevent 20` (DPAD_DOWN) and `23` (DPAD_CENTER)
  are reliable: title -> Select World -> load world.
- Scene: the landscape view (terrain + water + sky) of that world, steady state
  after the load settles.

## Headroom: the AVD is vsync-bound, the emulated CPU is not the limiter

Uncapped (`frameCap=0`) and capped (`frameCap=60`) both run at exactly
60.0 fps in the game's own `stats:` lines (300 frames / 5000 ms every window).
`dumpsys SurfaceFlinger --latency SurfaceView` shows the same: 127-frame
windows with p50 = p95 = p99 = max = 16.667 ms and zero intervals above 17 ms,
in both modes. The cap only changes the pacing sleep, not the presented rate.

Process CPU (utime+stime over 30 s windows, 1800 frames, steady state):

| mode | process CPU | per frame | share of one core |
| --- | --- | --- | --- |
| uncapped (`frameCap=0`) | 6260 ms | 3.48 ms | 20.9% |
| capped 60 (`frameCap=60`) | 6320 ms | 3.51 ms | 21.1% |

So the guest has ~79% of the 16.7 ms frame budget unused; the frame limiter is
the emulator's 60 Hz vsync plus host-side GPU emulation (swiftshader), not the
emulated interpreter, rasteriser or display path. Consequence: on this AVD no
guest-side optimisation can move fps or p99 — before/after fps deltas mean
nothing here (this is also why the round-2..6 absolute fps figures were flat at
56-60). Judge guest work by CPU ms/frame or on real hardware.

## On-device attribution (in-world, 633 cpu-clock samples, 10 s)

`/system/bin/perf` (perf.3.12_android) works under `adb root`; the NDK 30
`simpleperf` binary cannot run on API 21 (`CANNOT LINK EXECUTABLE: empty/missing
DT_HASH`, too old linker). Kernel symbols are unavailable (`kptr_restrict`),
but user symbols of the unstripped `libnj2me.so` resolve.

| self | symbol | note |
| --- | --- | --- |
| 29.4% | `[kernel.kallsyms]` (unresolved) | GPU driver ioctls, read/write, scheduler |
| 15.0% | `android_pixel` | per-pixel 565<->RGBA8888 convert for the window |
| 10.3% | `nj_interp_run` | interpreter loop |
| 6.8% | `android_display_present` | software present/blit of the shadow frame |
| 5.7% | `nj_region_draw` | M3G software rasteriser |
| 4.3% | `memcpy` | buffer copies |
| 4.3% | `strcmp` | class/field name compares during resolution |
| 2.8% | `nj_field_lookup` | field resolution |
| 1.9% | `nj_565_row_from_argb` | row colour conversion |
| 1.7% | `memset` | buffer fills |

Everything else is below 1.3% self time.

## Ranked candidates (not implemented: unverifiable on this AVD)

1. Port software present path (`android_pixel` + `android_display_present` +
   `nj_565_row_from_argb`, ~23.7% of user samples, ~15% of the total process
   CPU). A 1:1 fast row path (skip the per-pixel sampler when destination and
   source scale are equal) and/or a precomputed 565->RGBA8888 table would cut
   most of it. Expected win is guest CPU/power only; it cannot be shown in fps
   or p99 on this emulator and would need a real device (or a longer
   battery-equivalent measurement) to justify shipping.
2. Field/class resolution: `nj_field_lookup` 2.8% + `strcmp` 4.3% +
   `nj_class_lookup` 0.6%. The candidates are a resolution cache keyed by
   constant-pool index per class (not by name) and comparing lengths before
   `strcmp`. Still small.
3. M3G rasteriser `nj_region_draw` 5.7%: the next real CPU block if a target
   with headroom ever appears.

`nj_interp_run` at 10.3% is consistent with the round-3 result: after the
superinstruction work the interpreter is no longer the in-world frame
bottleneck.

## Reusable recipe

```sh
ADB=$HOME/Android/Sdk/platform-tools/adb; S="$ADB -s emulator-5570"
# 1. Uncapped mode (and back to the 60 cap)
printf '<?xml version="1.0" encoding="utf-8" standalone="yes" ?>\n<map>\n    <int name="frameCap" value="0" />\n</map>\n' > /tmp/display.xml
$S push /tmp/display.xml /data/local/tmp/display.xml
$S shell "cp /data/local/tmp/display.xml /data/data/com.lucinhu.nj2me/shared_prefs/display.xml; chown 10061:10061 /data/data/com.lucinhu.nj2me/shared_prefs/display.xml"
# 2. Launch and get in-world (title -> Select World -> world) with key events
$S shell 'am start -n com.lucinhu.nj2me/.GameActivity --es jar "MinecraftJ2ME_PE_0.6.1_V2.2.jar" --es jarpath "/data/local/tmp/MinecraftJ2ME_PE_0.6.1_V2.2.jar" --es title "Minecraft"'
# wait for the title, then: input keyevent 20; input keyevent 23; wait; input keyevent 23
# 3. Frame timings: one dump = the last 127 frames, 16.667 ms refresh line first
$S shell "dumpsys SurfaceFlinger --latency 'SurfaceView'" > sf.txt
# column 2 of each row = presented ns; deltas give p50/p95/p99
# 4. CPU per frame (10 ms ticks, field 14+15 of /proc/<pid>/stat)
# 5. Profiling (rooted emulator; NDK simpleperf is too new for API 21)
$S shell 'perf record -p <pid> -g -F 1000 -o /data/local/tmp/perf.data & P=$!; sleep 10; kill -INT $P'
$S shell 'perf report --stdio -i /data/local/tmp/perf.data --sort dso,symbol' > report.txt
```

Caveats: `adb shell` on this image has no `which`/`head`/`tail`/`pidof`;
parse `ps` output on the host. `logcat` is a small ring buffer — the game's
`stats:` lines push older `MCJ:` lines out, so read `MCJ:` lines right after the
load, not minutes later.

## Evidence

`.local-tools/debug-runs/r7-headroom/`:
`setup-world.sh`, `launch-world.sh`, `set-framecap.sh`,
`apk-build.log`, `emulator.log`,
perf reports (`perf-inworld.txt` = report3; report2 is the earlier run),
`perf-report*.txt`, `sf-cap0-inworld.txt`, `sf-cap60-inworld2.txt`,
`sf-latency-*.txt`, screenshots `s1..s15`, `step-ingame*.png`,
`c60-*.png`, `cap60-logcat.txt`.
