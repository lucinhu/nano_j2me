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
* CTest `check_libretro_multitouch`: the harness (`--multitouch-check`) reports
  `RETRO_DEVICE_ID_POINTER_COUNT=2` with independent per-index coordinates
  through the real libretro ABI; the fixture prints `MPT id=0` and `MPT id=1`
  lines for both fingers while `CANVAS down` appears exactly once (the first
  finger owns MIDP). 47 checks, 0 failed.
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

## Round-106 verification: real J2ME Minecraft PE and the APK path

The user reported (m19726) testing multi-touch with the J2ME Minecraft PE
build, so this round checked that exact title and then the Android APK path.

### Real Minecraft PE (`tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar`)

The JAR's manifest carries `Nokia-UI-Enhancement:
EnableMultiPointTouchEvents` and its classes reference
`com/nokia/mid/ui/multipointtouch/MultipointTouch` and
`MultipointTouchListener` - i.e. it is exactly the Nokia hardware-slot API
this runtime implements.

Run (two fingers down, dragged, released; paced 30 fps, heap 64 MB):

    build/nj2me --port headless --size 240x320 --heap 64 --fps 30 \
        --run-frames 1800 --keys @/tmp/r106-mt3.keys --verbose 3 --stats \
        --rms-dir /tmp/r106-rms3 tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar

The runtime log shows both fingers delivered independently to the game's
listener, and the game itself logs that it found the API:

    MCJ: multitouch: Nokia Multipoint Touch API
    nokia mpt: pointer 0 state 0 at 100,160
    nokia mpt: pointer 1 state 0 at 180,160
    nokia mpt: pointer 0 state 1 at 120,180
    nokia mpt: pointer 1 state 1 at 160,140
    nokia mpt: pointer 0 state 2 at 120,180
    nokia mpt: pointer 1 state 2 at 160,140

The title never leaves its own loading loop in headless mode (`paints=1`,
81,275,659 `drawImage` calls, 8.38 billion bytecodes in 60 s), so this proves
the pointer delivery to the game's `MultipointTouchListener`, not gameplay.

### Fixture now draws the hand

`tests/multitouchdemo/MultitouchDemo.java` paints one coloured dot per active
pointer (id 0 red, 1 green, 2 blue, 3 yellow) with its coordinates, the
`MPT events=N` counter, and a white crosshair for the last MIDP Canvas
pointer. Console lines are unchanged, so `tools/check_multitouch.sh` still
passes 13/13 and CTest `check_multitouch` / `test_nokia_touch` stay green.
The dots let a headless screenshot (or a real handset) show that each finger
arrives with its own coordinates.

### Android APK on the emulator

`build/android/nj2me.apk` (0.5.17) installs and runs `multitouchdemo.jar` on
AVD `Medium_Phone_API_36.1`; the fixture registers its listener and the
`MPT events=N` counter advances when a tap is delivered from `adb shell input
tap` (two `pointersChanged` calls, one down and one up, plus the Canvas
crosshair), which proves the Android View -> MotionEvent -> runtime ->
MultipointTouch plumbing end to end on device.

Two-finger host-side injection could not be completed on this image: the AVD
is a production build (`adb root` -> "adbd cannot run as root in production
builds"), `sendevent /dev/input/event2` is denied to the `shell` user, and
`adb emu event send` accepts MT protocol-B events (`OK`) but they do not
surface through InputReader (the fixture stays at `MPT events=0`). A real
handset - or an AVD with root/writable system - is needed for the two-finger
screenshot; interactive two-finger play on the user's phone with this APK is
the natural acceptance test.
