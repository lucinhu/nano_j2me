# Android in-game 保存状态: why it fails for frame-driving titles

The Android launcher has an in-game panel with a 保存状态 row.  On the 0.5.20
release APK, tapping it while a `serviceRepaints()`-driven title runs logs

```
state: save to /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/loopdemo.jar.state -> -8
state: save to /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/loopdemo.jar.state -> -8
```

and leaves a 0-byte file.  `-8` is `NJ_ERR_STATE`: `nj_vm_save_file()` refused
the snapshot.

## Root cause

The Android port serves save requests from `android_input_poll()`
(`src/port/android/nj_port_android.c`).  That callback runs from
`nj_midp_frame()`, which for a MIDlet that drives its own loop is pumped by the
runner's frame hook (`run_frame_hook()` in `src/runner/nj_run.c`).  At that
moment two guards in `nj_vm_can_save()` (`src/core/nj_save.c`) are tripped:

1. `vm->in_frame_hook` — the snapshot is being requested from inside the frame
   hook that the VM installed to keep a parked Java thread's loop going.
2. the parked MIDlet thread's `in_c_call` — the thread sits inside the
   `serviceRepaints()` native call, i.e. inside a C stack that a blob cannot
   represent.

A temporary pure diagnostics function (`nj_u4 nj_vm_save_refusal_mask(const
nj_vm *)`, added and reverted in this round) confirmed the exact mask on the
emulator: **0x0820 = bit 5 `in_frame_hook` + bit 11 thread `in_c_call`**.

The libretro port does not have this problem because it drives frames itself
and serves state requests *between* frames, outside the frame hook
(`lr_run_frames()` in `src/port/libretro/nj_port_libretro.c`; its frame hook
documents that a snapshot there is unsafe).  Android only has the frame-boundary
input poll, which is by construction inside the frame.

CLI contrast: `build/nj2me --save-state` with `--save-state-frame 300` saves
`loopdemo.jar` fine (330,410 B) because the CLI's frame loop reaches boundaries
where the MIDlet thread is in Java (sleeping / waiting for the next frame) and
the frame hook is not active.

## What was tried and rejected

* Retry the save on later frame boundaries: `save_attempts` up to 200 (JNI wait
  raised to 8000 ms) — every one of the 200 boundaries was busy
  (`state: save deferred (N) after a busy frame`).  For a title that re-enters
  `serviceRepaints()` immediately after each paint there is no clean boundary,
  so retrying cannot fix it.  Code reverted.
* A global diagnostics pointer in `src/core/nj_save.c` made the Android native
  build fail its "no writable static state" check (`WRITABLE STATIC STATE: 8
  bytes`); port diagnostics must be pure functions, not globals.

## User-visible behaviour today

The launcher now says what happened instead of a bare error code:
`这次没能保存：游戏线程正忙，这类游戏暂不支持中途存档` (`GameActivity.saveState()`,
`rc == -8`).  Titles whose loop sleeps in Java (fixtures, many menus, the
multitouch demo) still save normally — e.g. the 0.5.18 smoke test wrote
680,456 B for `multitouchdemo.jar`.

## Fix directions (not implemented)

1. **Snapshot threads parked in the frame-wait native call.**  Drop the native
   dispatch frame from the blob, resume the thread at the caller's invoke pc on
   restore (the `serviceRepaints()` call is idempotent and will wait for the
   next frame again).  Needs a blob-format change, loader acceptance rules and
   tests for nested paint/native waits.
2. **Give the Android port its own frame pump** like the libretro port so save
   requests are handled between frames.  Cleaner but a larger refactor of
   `nj_run`/frame-hook ownership.
3. Keep the limitation (this round) — the UI is honest and the hazard is
   documented.

## Reproducing

```
emulator -avd Medium_Phone_API_36.1 -no-window -no-audio -no-boot-anim \
         -gpu swiftshader_indirect -no-snapshot &
adb install -r build/android/nj2me.apk
adb push tests/fixtures/loopdemo.jar \
    /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/
# launch LibraryActivity, tap the LoopDemo card, tap ≡, swipe the panel up
# twice, tap 保存状态, then:
adb logcat -d -s nj2me:* | grep 'state: save'
adb shell ls -l /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/
```
