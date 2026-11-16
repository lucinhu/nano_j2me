# Multi-touch input (runtime, Android frontends and the libretro core)

Status: implemented 2026-11-16. Requested by the user to run J2ME Minecraft
(J2ME PE), which drives both the MIDP Canvas pointer API and the Nokia
`com.nokia.mid.ui.multipointtouch` listener.

## What was wrong

The runtime already had the Nokia MultipointTouch API
(`src/midp/nj_lib_nokia.c`: one instance, four pointers, `getState`/`getX`/
`getY`, listener registration), but `nj_nokia_mpt_pointer()` only ever filled
pointer slot 0, fed from the single MIDP pointer path. Every port also
collapsed input to one finger:

* libretro read `RETRO_DEVICE_ID_POINTER_X/Y/PRESSED` at index 0 only and never
  asked `RETRO_DEVICE_ID_POINTER_COUNT`;
* Android/PadPress forwarded only the first `MotionEvent` pointer, and the
  runtime's older contract ("first finger owns the MIDP pointer") made the
  other fingers disappear.

So a title that listens for `pointersChanged(int[])` always saw a single id, and
a title that tracks two fingers through the Canvas API could never see the
second one start.

## Design

* `nj_event` grew an `id` field (pointer events only). `0` stays the mouse /
  first finger, so every existing single-pointer frontend keeps its old
  behaviour.
* The runtime keeps the MIDP contract: one owner finger drives
  `Canvas.pointerPressed/Dragged/Released`; additional fingers are still
  reported to the Nokia listener through `nj_midp_pointer_multi()`. The first
  finger down owns the Canvas pointer until it lifts; extra fingers never
  steal it (this matches the Asha/S40 behaviour Minecraft expects).
* `nj_nokia_mpt_pointer()` now takes the finger id, clamps it to the four
  hardware slots, stores per-id state/coordinates and notifies
  `pointersChanged` with the changed ids.
* libretro: `lr_read_pointer()` queries `RETRO_DEVICE_ID_POINTER_COUNT`,
  loops up to eight indices and emits per-id down/move/up events. A frontend
  that reports count 0/1 (mouse) behaves exactly as before.
* Android: every `MotionEvent` pointer is forwarded with its pointer id by
  `PadPress`, so the game's keypad covers the whole hand. `cancel()` releases
  every forwarded finger and `forgetGame()` drops pan/zoom fingers so they do
  not become phantom taps.
* CLI: `--keys` learned `pressed#<id>@X,Y`, `dragged#<id>@X,Y` and
  `released#<id>@X,Y` (ids 0..9); the plain `pressed@X,Y` forms remain.

## Verification

* `tests/multitouchdemo/MultitouchDemo.java` + `tools/check_multitouch.sh`
  (CTest `check_multitouch`): a two-finger script must produce the listener
  callbacks with the right ids/coordinates, the Canvas callbacks for the owner
  finger, and no Canvas callback for the second finger. 13/13 checks pass.
* `test_nokia_touch` now covers two independent fingers, the per-id
  coordinates, and id clamping; `test_vm`, `test_save`,
  `test_libretro_frontend` and `check_pointer` stay green.
* `tools/check_libretro_frontend.sh` matrix and a real RetroArch 1.22.2
  loopdemo run pass with the rebuilt core (single-pointer legacy path
  unchanged); the option matrix is 88/88.
* Android Java tests (including the new two-finger `PadPressTest` case) pass
  through `tools/check_android_java.sh`.

## Limits

* The libretro API cannot deliver more than one pointer through
  `RETRO_DEVICE_POINTER` on every frontend; cores that need more than the
  frontend reports will see only that many fingers. RetroArch on Android does
  report several.
* The runtime supports the four Nokia hardware pointer slots; libretro reads up
  to eight and Android forwards whichever ids the platform sends, but only the
  lower four have listener slots.
* Only the Nokia MultipointTouch API is implemented; the newer
  `com.nokia.mid.ui.multipointtouch` Asha convenience methods beyond the
  listener are not (the stub covers what titles compile against).
