# Android in-game 保存状态: the frame-hook boundary save

The Android launcher has an in-game panel with a 保存状态 row.  On 0.5.20,
tapping it while a `serviceRepaints()`-driven title ran logged
`state: save to …/loopdemo.jar.state -> -8` and left a 0-byte file.  Round 140
root-caused it; this note records the cause and the fix that landed afterwards.

## Root cause

The Android port serves save requests from `android_input_poll()`
(`src/port/android/nj_port_android.c`).  That callback runs from
`nj_midp_frame()`, which for a MIDlet that drives its own loop is pumped by the
runner's frame hook (`run_frame_hook()` in `src/runner/nj_run.c`).  Two guards
in `nj_vm_can_save()` (`src/core/nj_save.c`) refused that snapshot:

1. `vm->in_frame_hook` - the save was requested from inside the frame hook.
2. the parked MIDlet thread's `in_c_call` - it sits inside the
   `serviceRepaints()` native, and a C stack cannot be put in a blob.

(The libretro port does not hit this: `lr_run_frames()` serves state requests
*between* frames, outside the hook.)

## Fix: boundary saves

New `int nj_vm_save_file_boundary(nj_vm *vm, const char *path)`
(`include/nj2me/nj_vm.h`, `src/core/nj_save.c`).  It sets `vm->boundary_save`
around the normal save, and the save path then:

* lets the frame-hook guard pass (the port asked from that context; every
  other guard - C roots, `native_depth`, gc_disabled - is unchanged);
* for a thread parked in a frame-wait native, drops the native dispatch frame
  from the blob and stores the thread as `RUNNING` with the wait/wake fields
  cleared, i.e. "resume the Java caller after the parked call returns".  For
  `serviceRepaints()`/`paint` waits that is exactly what the game does next:
  the next frame paints as usual.
* every other thread and every VM field is stored exactly as before.

`nj_vm_save_file()` is untouched; on an idle VM the two entry points produce the
same loadable blob (covered by a new check in `tests/test_save.c`).

## Evidence (0.5.21-era debug APK, API 36 emulator, LoopDemo)

```
I nj2me : running LoopDemo (LoopDemo ?)
I nj2me : state: save to /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/loopdemo.jar.state -> 0
-rw-rw---- 1 u0_a219 ext_data_rw 330016 2026-11-21 03:06 loopdemo.jar.state
```

force-stop, relaunch, long-press the card, 恢复进度:

```
I nj2me : running LoopDemo (LoopDemo ?)
I nj2me : resumed LoopDemo from /storage/emulated/0/Android/data/com.lucinhu.nj2me/files/jars/loopdemo.jar.state
```

The restored run paints the normal game screen (screenshot checked), so the
parked-thread resume works.

## What did NOT work (kept as a lesson)

Retrying the save on later frame boundaries (`save_attempts` up to 200, JNI
wait 8000 ms): every one of the 200 boundaries was busy, because the title
re-enters `serviceRepaints()` immediately after each paint.  A global
diagnostics pointer in `src/core/nj_save.c` also made the Android native build
fail its "no writable static state" link check; port diagnostics must be pure
functions.

## Limits and follow-ups

* The boundary form is only valid from the port's frame-block callback (the
  Android input poll today).  Calling it from a Java callback is not supported.
* The Android legacy APK does not expose the in-game panel; nothing changes
  there.
* `docs/notes/2026-11-21-fetched-black-screen-titles.md` titles are unrelated.

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
# force-stop, relaunch, long-press the card, tap 恢复进度, then:
adb logcat -d -s nj2me:* | grep resumed
```
