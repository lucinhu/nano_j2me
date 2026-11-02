# Android input and lifecycle polish (round 6)

Scope: the Android port's Java shell (`src/port/android/java/com/lucinhu/nj2me/`) and the
lifecycle/lifecycle-adjacent inputs the port controls. Device: emulator-5560 (AVD
`OpenHV_API26_googleapis`, x86_64, 320x640 baseline). App built from this branch
(`tools/build_android_apk.sh --abi x86_64`), installed over the round-5 APK.

## Issues found

### 1. Audio focus was never requested (fixed, medium)

The port never asked for audio focus and never listened for focus changes: a `grep` for
`requestAudioFocus` / `abandonAudioFocus` / `AudioFocusRequest` in the whole Android
source returned nothing, and the only `AudioManager` use was the volume-key handler
(`GameActivity.java:882-889`). Before the change, `dumpsys audio` showed an empty focus
stack:

    Audio Focus stack entries (last is top of stack):
    No external focus policy

i.e. another app starting playback (music, a call, navigation) mixes with the game and the
port has no way to pause/resume its own audio around it.

After the change the port takes `AUDIOFOCUS_GAIN` in `onResume()` and abandons it in
`onPause()`:

    Audio Focus stack entries (last is top of stack):
      source:android.os.BinderProxy@3f409e7 -- pack: com.lucinhu.nj2me
        -- client: android.media.AudioManager@f0291facom.lucinhu.nj2me.GameActivity$3@dee83ab
        -- gain: GAIN -- flags:  -- loss: none -- notified: true -- uid: 10094 -- sdk:34

Focus loss pauses the MIDI stream and focus gain resumes it. Verified end-to-end with a
simulated incoming call (`adb -s emulator-5560 emu gsm call 5551234`): the focus stack
then shows the port with `loss: LOSS_TRANSIENT -- notified: true` underneath telecom's
`AudioFocus_For_Phone_Ring_And_Calls -- gain: GAIN_TRANSIENT -- flags: LOCK`, and the
port logs `audio focus: paused (change=-2)`; cancelling the call logs
`audio focus: gained` and the stack returns to `gain: GAIN` (evidence
`focus-test.log`, three lines at 22:46:59 / 22:47:31 / 22:47:35).

Implementation notes: the legacy `requestAudioFocus(listener, stream, gain)` API is used
rather than `AudioFocusRequest`, because the port's `minSdk` is 21 and the legacy call
covers every API level with one path. `MidiPlayer.pauseAll()` / `resumeAll()` are the same
calls `onPause()` / `onResume()` already use. A transient loss (`AUDIOFOCUS_LOSS_TRANSIENT`,
`change == -2`) pauses audio without touching the VM; a gain while the activity is resumed
resumes it.

### 2. Pad touches and hardware keys were not released on pause / panel open (defensive fix, low)

`PadView.releaseAllKeys()` (`PadView.java:263` → `releaseAll()` → `PadPress.cancel()`)
carried the comment "Called when the game goes to the background", but nothing ever
called it, and hardware keys forwarded through `GameActivity.dispatchKeyEvent()` were not
tracked at all:

- `GameActivity.onPause()` (~line 563) tears down the surface and pauses the VM but never
  tells `PadView` to drop held touches/keys;
- `showPanel()` (~line 1118) opens the dialog while the game keeps running underneath it
  (deliberate live-preview design, see round-5 notes), again without releasing input;
- `PadPress.cancel()` releases the pad codepoints *and* the single game pointer, so a
  cancel is exactly the right thing to send.

The fix tracks every forwarded hardware code in a `heldKeys` set and, on `onPause()` and
at the top of `showPanel()`, releases the tracked codes through `nativeKey(handle, code,
false)` and calls `pad.releaseAllKeys()` (which also lifts the game pointer). Every
release is idempotent and cheap; nothing changes for the common case where the framework
already delivered `ACTION_CANCEL`.

Honest caveat: on this emulator we could not reproduce a stuck key on the old build — a
scripted hold of the on-screen forward arrow followed by opening and closing the panel
left the player stationary afterwards on both the old and the new build, because the
framework delivers `ACTION_CANCEL` to the pad view when the dialog takes input. The fix
covers the paths where no cancel arrives (physical keys pressed while a dialog changes
focus, vendor input stacks) and removes the dead-code comment lie; it is not presented as
a reproduced user-visible bug.

## Deliberate non-fixes (documented behaviour)

- **BACK is not a hard exit.** `dispatchKeyEvent()` still forwards `KEYCODE_BACK` to the
  MIDlet as its right soft key and the key-up opens the port panel. Observed on device:
  BACK in-world opens the game's own pause menu, and a second BACK opens the port panel
  (the panel can be on screen at the same time as the game's pause menu). This is the
  round-5 decision — a J2ME game must be able to use its soft keys, and leaving the app is
  the launcher's job.
- **The settings panel does not pause the game.** Measured in round 5: while the panel is
  open the VM keeps running at ~59.6 fps and `/proc/<pid>/stat` advances utime+stime
  8422→8553 ticks over 4 s (≈33% CPU). This is the intended live-preview design (the
  font/anti-alias/fps rows must apply visibly). `dumpsys cpuinfo` shows a lifetime average
  of 0% here and is misleading; use the `/proc` counters.
- **A MIDlet gets one pointer.** `PadPress` tracks up to 16 fingers for the pad's own
  keys, but the game sees a single pointer. Documented in `PadPress.java` ("A MIDlet sees
  a single pointer, and the pad must not hand it two").

## Audit items that were already correct

- Orientation is locked in `onCreate()`: `setRequestedOrientation(screenW > screenH ?
  SCREEN_ORIENTATION_LANDSCAPE : SCREEN_ORIENTATION_PORTRAIT)` (`GameActivity.java:319`).
- `FLAG_KEEP_SCREEN_ON` is set in `onCreate()` (`GameActivity.java:323`); `dumpsys window`
  during gameplay shows `mLastWakeLockHoldingWindow=Window{...GameActivity}` with
  `mLastWakeLockObscuringWindow=null`.
- Touch coordinates after a resize were verified in round 5 (same surface-resize code
  path): after `wm size 480x800` the game HUD pause icon could be tapped at 2x logical
  coordinates; this round re-checked that the picture still re-fits (window=480x800,
  picture fills the width with a 2x pad) but the tap position probe was inconclusive, so
  resize+touch stays credited to the round-5 measurement.

## Verification

- `ctest` in the worktree: 137/137 tests, "100% tests passed", 251.92 s.
- Desktop release build: clean, no new warnings. APK build: no new compiler warnings (only
  JDK `apksigner` native-access warnings).
- On-device: fixed APK installs over the round-5 APK, world loads and renders, panel rows,
  pause menu and quit-save path unchanged.
- Audio focus evidence: before/after `dumpsys audio` blocks and the
  `adb emu gsm call` loss/gain sequence in `focus-test.log`.

## Build note

`adb install -r` fails with `INSTALL_FAILED_UPDATE_INCOMPATIBLE` because the worktree
generates its own debug keystore. Copy the canonical
`build/android/debug.keystore` from the main checkout into the worktree's `build/android/`
before building, or rebuild after installing.
