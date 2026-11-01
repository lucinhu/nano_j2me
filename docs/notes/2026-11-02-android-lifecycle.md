# Android round 5: a resize that kept its surface, and what the port panel
# really does to the game (2026-11-02)

The hunt this round was lifecycle, input, upgrade and crash safety on the
Android port.  One real bug came out of it: when the SurfaceView keeps its
surface and only changes size (rotation, multi-window drag, a display-size
change in developer options), the port kept drawing for the old size and the
compositor stretched the old buffer into the new aspect ratio.  The settings
panel also turned out not to pause the game at all, which is worth writing
down even though it is the intended design.

Everything below is on `emulator-5560` (x86_64, Android 8.0/API 26,
`OpenHV_API26_googleapis`, 320x640, density 160) with the APK built from
`331686f`.

## The resize bug

### Symptom

In-world at 320x640, `adb shell wm size 480x800`:

* `dumpsys display` reports `cur=480x800`, and the SurfaceView layer in
  `dumpsys SurfaceFlinger` reports `size=( 480, 800)`, but the layer's buffers
  stay `[ 320x 640: 320, 1]` (`state=ACQUIRED` / `QUEUED`, 320x640).  The
  compositor therefore stretches a 320x640 picture into a 480x800 window.
* The port's own status line says `window=320x640 scale=1.00` while the real
  window is 480x800, so the picture was fitted for the wrong size: wrong
  integer scale, wrong letterbox, and the Java pad overlay (laid out for the
  real window) no longer lines up with the game picture.
* Resetting to 320x640 "fixes" it only because that is the size the port
  cached at creation.

Before-fix evidence: `r1-overlay.png` (status line `window=320x640` while the
display is 480x800), `r2-wm480.png`.

### Root cause

`GameActivity.surfaceChanged()` was an empty no-op:

```java
/* The port reads the window size on every present, so a resize needs no
 * announcement - the letterbox is recomputed the next frame anyway. */
```

That comment was wrong.  The native port caches the window size exactly once,
in `android_display_init()`:

```c
s->win_w = ANativeWindow_getWidth(s->window);
s->win_h = ANativeWindow_getHeight(s->window);
```

and `nj_port_android_set_window()` does the same for a new window.  The
integer-fit letterbox in `android_display_present()` divided by
`s->win_w/s->win_h`, i.e. by that creation-time cache.  A resize that keeps
the same surface and the same `ANativeWindow` never reaches either setter, so
the cache stays stale forever.

### Fix

`src/port/android/java/com/lucinhu/nj2me/GameActivity.java`,
`src/port/android/nj_port_android.c`, `include/nj2me/nj_port.h`,
`tools/build_android_apk.sh`:

1. New native entry `nj_port_android_set_window_size(port, width, height)`
   (JNI: `GameActivity.nativeSetWindowSize`).  It publishes the new size under
   `s->lock` and re-asks the buffer queue for buffers in that size with
   `ANativeWindow_setBuffersGeometry(window, width, height,
   WINDOW_FORMAT_RGBA_8888)` - the same call and format `android_display_init`
   uses.
2. `GameActivity.surfaceChanged()` calls it (under the handle lock, guarded by
   `handle != 0`), so rotation / multi-window / display-size changes now reach
   the port.
3. `android_display_present()` no longer trusts the cache for the fit: the
   locked buffer is this frame's real window, so when
   `buf.width != s->win_w || buf.height != s->win_h` it adopts the buffer's
   size (under the lock) and computes the integer fit from
   `buf.width/buf.height`.  A frame already in flight when the resize arrives
   is scaled for the window it actually lands in, and the status line stays
   honest.
4. A `!__ANDROID__` stub keeps the desktop build linkable, next to the
   existing `nj_port_android_set_window` stub.

### Verification

Same APK, same world, `wm size` round trips (screenshots in
`/home/lucinhu/workspace/wt/evidence-r5-android/`):

| step | status line | SurfaceFlinger buffer |
| --- | --- | --- |
| 320x640 baseline | `window=320x640 scale=1.00` | `[ 320x 640: 320, 1]` |
| `wm size 480x800` | `window=480x800 scale=2.00` | `[ 480x 800: 480, 1]` |
| `wm size reset` | `window=320x640 scale=1.00` | `[ 320x 640: 320, 1]` |
| `wm size 480x800` again | `window=480x800 scale=2.00` | `[ 480x 800: 480, 1]` |

The picture now fills the width at 2x, integer-scaled (game picture 480x640,
letterboxed to 480x800), and back to 1x after the reset - screenshots
`s2-wm480.png`, `s4-reset.png`, `u1-reset.png`.

Touch mapping after the resize also lands on the game's own UI: at 480x800 the
game's in-game HUD pause icon is at canvas (228,5) x 2 = logical (456,10), and
tapping there opens the game's "Game menu" (`t1-tap456x10.png`); tapping a menu
row acts on that row (`t3-swipe.png`, server-visibility row toggled).  So the
published view transform follows the new size too.

## The port panel does not pause the game (intended, documented)

The settings panel (the ≡ handle) is a live overlay: `GameActivity.showPanel()`
says so in a comment ("the game underneath keeps running") because the
anti-aliasing / font / frame-cap rows are previewed live.  Measured with the
panel open in-world:

* `debug.nj2me.stats` keeps printing `stats: 59.6 fps, 298 frames in 5000 ms`,
  and screenshots show the world still animating.
* `/proc/<pid>/stat` utime+stime went 8422 -> 8553 ticks over 4 s, i.e. ~33%
  of one core, while the game's own pause menu holds the same process at 0%.
* `dumpsys cpuinfo` shows 0% because it reports a lifetime average; use
  `/proc/<pid>/stat` for this kind of check.

The consequence for players: the phone keeps rendering (and burning battery)
while the panel is up, and the game's own pause menu is only reachable through
the HUD pause icon.  This matches the code comment, so it is recorded as
design behaviour / product decision rather than silently changed.

## BACK, HOME, load-time HOME, kill -9, quit

* In-world BACK -> port panel.  On the world list BACK -> port panel as well
  (`b1-back-worldlist.png`): `GameActivity.onBackPressed()` opens the panel and
  never exits the app, so BACK cannot lose a world through an unsaved exit.
  BACK again (or a tap outside) dismisses the panel; the panel does not leak
  touches into the game.
* Quit path saves correctly: `MCJ: offset write ok`, `MCJ: chunks in place 0`,
  `MCJ: saved world ...`, `MCJ: SAVE 0 ms`, then the run ends with
  `notifyDestroyed()` and `released N host blocks at teardown`.
* HOME in-world: process alive, 0.0% CPU (the VM parks in `android_idle` while
  `s->paused`), resume via the same task (`am start
  --activity-reorder-to-front`) shows the identical picture and CPU returns to
  ~32%.
* HOME during world load: the load continues after resume and finishes
  normally (`MCJ: LOAD read 21096 ms, prepare 5300 ms` vs 7310/5201 warm; the
  read number is wall time and so includes the paused period).  Side note: the
  `stats:` line also counts paused wall time (`stats: 8.3 fps, 135 frames in
  16222 ms`), a logcat-only artifact.
* `kill -9` in-world: process dies, relaunch starts cleanly, the world list is
  intact and the world loads; the world directory holds only
  `level.dat`/`entities.dat`/`chunks.dat` (no stale temp files).  App data
  backup for the hunt: `appdata-backup1.tar` (22,032,896 B).

## Build script fix

On a clean tree `tools/build_android_apk.sh` could not create its debug
keystore: `keytool -genkeypair` does not create parent directories, its stderr
is dropped, and the build only failed later in `apksigner` with
`Failed to load signer ... FileNotFoundException:
.../build/android/debug.keystore`.  Added `mkdir -p "$ROOT/build/android"`
before the keystore check.  (Worktrees need the canonical
`build/android/debug.keystore` copied in, otherwise `adb install -r` fails with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`; that is a dev-workflow note, not code.)

## Reproduce

```sh
# boot
$ANDROID_SDK/emulator/emulator -avd OpenHV_API26_googleapis -no-window \
    -no-audio -no-boot-anim -no-snapshot-load -gpu swiftshader_indirect -port 5560
adb -s emulator-5560 root
adb -s emulator-5560 install -r evidence-r5-android/apk-fix/nj2me.apk
adb -s emulator-5560 shell am start -n com.lucinhu.nj2me/.GameActivity \
    --es jar MinecraftJ2ME_PE_0.6.1_V2.2.jar \
    --es jarpath /data/local/tmp/mc.jar --es title "Minecraft PE"
# then: Start Game, tap the world, wait for "MCJ: HUD3D auto 2d"
adb -s emulator-5560 shell wm size 480x800   # picture must be re-fitted
adb -s emulator-5560 shell wm size reset
```

Evidence: `/home/lucinhu/workspace/wt/evidence-r5-android/` keeps the
before/after screenshots, the `wm size` status-line crops, the appdata backup
and the logcat captures.

## Tests

Desktop: `cmake --build build -j8` clean, 241/241 targets, no warnings.
Full `ctest --test-dir build --output-on-failure`: 137/137 passed (275.8 s)
after the local JAR fixtures (`tests/games/`, `tests/fixtures/`) were copied
into the worktree (they are `gitignore`d and not part of this patch; without
them the suite registers 96 tests and several fixture-driven tests fail to
find their jars).  The main tree has three more `test_port_*` cases that need
port libraries not built here, hence 137 instead of 140.

## Risks and limits

* Verified on the x86_64 emulator only (320x640 and 480x800); rotation was
  emulated via `wm size`, not by a physical accelerometer event, and a
  multi-window drag was not exercised (the path is the same `surfaceChanged`
  call the display-size change uses).
* `ANativeWindow_setBuffersGeometry` requests are asynchronous from the
  caller's point of view; the fix also takes the size from the locked buffer
  every frame, which is what makes a frame that was in flight during the
  resize land correctly, but a device that ignores the geometry request would
  still show compositor scaling for one frame.
* The panel's non-pausing behaviour is deliberately documented only; changing
  it (e.g. pausing the VM while the panel is open) is a product decision.
* adb-synthesised touch cannot dig blocks in this game; digging behaviour was
  not part of this round (see the round-4 desktop notes).
