# Porting nano_j2me

The runtime talks to the operating system through exactly one struct,
`nj_port`, declared in [`include/nj2me/nj_port.h`](../include/nj2me/nj_port.h).
Nothing else in `src/` opens a file, allocates from the OS, reads a clock or
touches a screen. Bringing the emulator up on a new board means writing one C
file that fills that struct in.

## The contract in full

Five callbacks are mandatory. Everything else is optional and degrades
gracefully when left `NULL`.

| Callback | Purpose | If omitted |
|---|---|---|
| `millis` | monotonic millisecond clock | `Thread.sleep()` and Alert timeouts cannot advance |
| `display_init` | hand the runtime a linear framebuffer | nothing can be drawn |
| `display_present` | push a changed rectangle to the panel | the panel never updates |
| `input_poll` | non-blocking event pump | the app receives no input |
| `log` | text output (`System.out`, errors) | output is discarded |

| Optional callback | Purpose | Fallback |
|---|---|---|
| `alloc` / `free` | host memory | `malloc`/`free` |
| `idle` | let the RTOS breathe | no-op |
| `sleep_ms` | block for a while | `idle` |
| `now` | wall-clock date and time; anchors Java milliseconds once at VM creation | unanchored monotonic time; no valid epoch guarantee |
| `file_*` | RecordStore persistence | C library I/O |
| `app_blob` | a JAR flashed into a partition | nothing |
| `tone_play` / `tone_stop` | `Player` tones | silence |
| `vibrate` | `Display.vibrate()` | no-op |
| `shutdown` | release port-owned state | nothing |

RecordStore writes an optional 16-byte `NJMT` timestamp trailer after its v1
records, and an optional append-only `NJBG` section before that trailer for
records longer than 64 KiB (files without long records stay byte-for-byte v1). New stores and successful mutations retain `getLastModified()` across
VM restarts without a filesystem metadata hook. A legacy file without that
trailer reports **0 (unknown)** through `file_*`, since this callback interface
has no stat/mtime operation; the direct libc fallback can use host mtime. Supply
`now` for epoch-based timestamps, and make `file_write` report the actual byte
count: short/failed writes are errors, not a successful saved mutation.

### Two pointers, two jobs

`nj_port.ud` is **yours**: the runtime hands it back to every callback except
`log`, so it normally points at a struct you allocated in your port
constructor. `nj_port.user` is the **embedder's**: it is passed to `log` only,
so an application can capture `System.out` without knowing anything about your
internals.

### The framebuffer contract

`display_init` returns a pointer to a linear buffer plus a stride **in bytes**.
The stride may exceed `width * bytes_per_pixel` — a panel with padding, or an
SDL texture with a different pitch — and the rasterizer honours it. The format
is chosen at compile time by `NJ_PIXFMT`:

```c
-DNJ_PIXFMT=NJ_PIXFMT_RGB565    /* default: panels, 16bpp */
-DNJ_PIXFMT=NJ_PIXFMT_RGB888
-DNJ_PIXFMT=NJ_PIXFMT_ARGB8888
```

`display_present(x, y, w, h)` receives the smallest rectangle that changed.
A panel driven over SPI should DMA just that rectangle; a port that would rather
not bother can ignore the arguments and push the whole buffer.

## Rule: a port owns no global state either

The runtime keeps no mutable global variables, and a port must not reintroduce
them. Allocate your state in the constructor, stash it in `nj_port.ud`, and
release it from `nj_port.shutdown`, which the runner calls before returning:

```c
static void my_shutdown(void *ud)
{
    my_state *s = ud;
    free(s->framebuffer);
    free(s);
}

nj_status nj_port_mine(nj_port *out)
{
    my_state *s = calloc(1, sizeof(*s));
    if (!s) return NJ_ERR_NOMEM;
    memset(out, 0, sizeof(*out));
    out->name            = "mine";
    out->ud              = s;
    out->millis          = my_millis;
    out->display_init    = my_display_init;
    out->display_present = my_present;
    out->input_poll      = my_input_poll;
    out->log             = my_log;
    out->shutdown        = my_shutdown;
    return NJ_OK;
}
```

Note the shape: **the port fills in a caller-owned struct**. There is
deliberately no `const nj_port *nj_port_mine(void)` accessor, because that would
require a shared static instance.

### The one thing a UI thread may not do: touch the VM

A port that draws its own controls over the game — the SDL2 reference port has
an on-screen keypad and a speed badge in its window — will want to change a
runtime setting from inside an event handler. It must not. `input_poll` is the
only hook that runs at the top of a frame, on the VM's own thread, so a handler
writes a number into the port's state and the next frame applies it:

```c
/* in the event handler, during present(): */
s->speed_percent = 200;                 /* nothing else */

/* top of input_poll(), once per frame: */
if (s->vm && s->speed_percent != s->speed_applied) {
    nj_vm_set_speed(s->vm, s->speed_percent);
    s->speed_applied = s->speed_percent;
}
```

The VM pointer itself comes from `install_codecs(ud, vm)`, the one hook called
right after the VM exists and before the built-in codecs are registered; a port
with no codecs of its own still implements it just to remember the VM. The same
rule covers snapshots — `nj_vm_save_file()` at the top of a frame, never from a
handler, because the object arena, every Java stack and the scheduler have to be
in a state the blob can record with no native call in flight. And note what a
port cannot do at all: loading a snapshot is only valid *before* the MIDlet is
constructed, which is the runner's business (`--load-state`), not a hotkey's.

A snapshot has to cover what a *live* frame can still read, which is more than
the allocation frontier: when a frame returns, `nj_frame_pop()` leaves
`thread->top` at that frame's locals, and the caller then pushes its own
operands above it. The image therefore reaches each frame's `sp`, and a
restored slot is legal anywhere inside `stack_mem..stack_end`. Saving only
`top` produced blobs that loaded into a program running on zeros — the
reason `NJ_SAVE_VERSION` is 2 and a version-1 blob is refused rather than
restored.

## Worked example: ESP32-S3

An ESP32-S3 with 8 MB of PSRAM and a 240×320 SPI panel is the target this
runtime was shaped for.

```c
#include "nj2me/nj.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "driver/gpio.h"

typedef struct {
    esp_lcd_panel_handle_t panel;
    uint16_t              *fb;      /* PSRAM */
    size_t                 fb_bytes;
    int                    w, h;
} s3_state;

static nj_u4 s3_millis(void *ud)
{
    (void)ud;
    return (nj_u4)(esp_timer_get_time() / 1000);
}

static int s3_display_init(void *ud, int *w, int *h, int *stride, void **px)
{
    s3_state *s = ud;
    s->fb = heap_caps_malloc(s->fb_bytes, MALLOC_CAP_SPIRAM);
    if (!s->fb) return -1;
    *w = s->w; *h = s->h;
    *stride = s->w * 2;
    *px = s->fb;
    return NJ_PIXFMT_RGB565;
}

static void s3_present(void *ud, int x, int y, int w, int h)
{
    s3_state *s = ud;
    esp_lcd_panel_draw_bitmap(s->panel, x, y, x + w, y + h,
                              s->fb + (size_t)y * s->w + x);
}

static int s3_input_poll(void *ud, nj_event *ev)
{
    /* Drain a FreeRTOS queue the GPIO ISR and the keypad task post into. */
    s3_state *s = ud;
    return xQueueReceive(s->keys, ev, 0) == pdTRUE;
}

static void s3_idle(void *ud)
{
    (void)ud;
    vTaskDelay(1);          /* let the WiFi stack and the IDLE task run */
}

nj_status nj_port_esp32(nj_port *out, const nj_port_esp32_config *cfg);
```

Three things matter on this target:

1. **Put the Java heap in PSRAM.** `nj_vm_create(&port, 6u * 1024u * 1024u)`
   builds the arena with `port->alloc`, so route `alloc` to
   `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` and the internal SRAM stays free
   for the WiFi stack and DMA descriptors.
2. **`idle` must actually yield.** The scheduler calls it when no Java thread is
   runnable; returning immediately starves the RTOS task watchdog.
3. **The JAR lives in flash.** Either flash it into a partition and return it
   from `app_blob`, or embed it as a C array. Nothing is copied: `nj_zip` reads
   the archive in place, so a memory-mapped partition costs no RAM at all.

Flash the game and the runtime:

```sh
idf.py build flash monitor
# or, with an embedded game:
tools/make_hello_jar.sh
xxd -i hello.jar > main/game_jar.h      # then return it from app_blob
```

## Worked example: Android

The NDK port is a software framebuffer blitted into an `ANativeWindow`, with a
thread-safe event queue carrying input from the Java side. It is the largest
port in the tree and the one with the most to say, so read it as the reference
for what "a port" means here:

| piece | where |
| --- | --- |
| the port itself, plus all JNI entry points | `src/port/android/nj_port_android.c` |
| library UI, game activity, keypad editor, system-font rasteriser (framework only, no AndroidX) | `src/port/android/java/com/lucinhu/nj2me/` |
| the shared library, one `.so` per ABI | `tools/build_android.sh` |
| the APK, by hand: `aapt2` + `javac` + `d8` + `apksigner` | `tools/build_android_apk.sh` |
| the launcher icon set (PNG mipmaps + adaptive icon, no vector placeholder) | `tools/gen_android_icon.py` |

The floor is **API 21**, the oldest level the NDK still ships a compiler for, and
the audio is what makes it work: neither `libaaudio.so` nor `libOpenSLES.so` is
linked. AAudio is `dlopen()`ed on the first sound and used when the phone has it
(API 26+); when it is missing the same mixer is driven by OpenSL ES, which every
Android since 2.3 has. One APK, both devices: verified on an API 36 emulator
(`audio: AAudio started, 44100 Hz, 2 ch`) and on an API 21 emulator
(`audio: OpenSL ES started, 44100 Hz, 2 ch`). A hard link to `libaaudio.so` would
make the loader refuse `libnj2me.so` entirely on an older phone, which is why the
declarations are copied from `<aaudio/AAudio.h>` instead of included - the header
marks them "introduced in Android 26" and will not compile for a lower target.
`adb shell setprop debug.nj2me.audio opensl` pins the old backend, which is the
only way to exercise that path on a phone that has AAudio.

The port fills in `display_init`/`display_present`, `input_poll`, `input_push`,
`now`, `dir_open`/`dir_next`/`dir_close`, `tone_play`/`tone_stop`,
`pcm_open`/`pcm_write`/`pcm_close`/`pcm_free`/`pcm_queued`,
`device_property`, `vibrate` and `idle`. Four of those are worth copying:

- **Directories, but not files.** `dir_open`/`dir_next`/`dir_close` are real
  (`dirent` + `stat`), because the runtime has no fallback for them and
  `FileConnection.list()` would have to throw. The byte-level file hooks stay
  `NULL` on purpose: every one of them already falls back to stdio, and bionic
  supplies it, so a JAR in `filesDir` opens with the same `fopen` the desktop
  build uses.
- **`rms_dir` and `fs_dir` set from Java.** The runtime derives them from
  `XDG_DATA_HOME`/`HOME` when a port leaves them `NULL`, which on Android means
  the record store lands in `/`. They are pushed in as absolute paths from
  `Context.getFilesDir()` before the VM thread starts.
- **MIDI is taken over wholesale, not streamed.** The runtime can render a
  `.mid` itself, so the port's job is only to say whether it has something
  better: `midi_open` hands back a handle and a duration, or `NULL` to leave the
  file to the built-in synthesiser. Android answers with the platform's own
  synth (`MediaPlayer`, which is Sonivox EAS), which is why a phone gets the
  platform's timbres and a game's `getDuration()`/`getMediaTime()`/looping still
  behave - the remaining seven hooks back the same `Player` state machine.
  Two more hooks are optional and only matter to a synth that can take single
  events: `midi_short_event` and `midi_long_event`, which is what
  `MIDIControl.shortMidiEvent()`/`longMidiEvent()` become. A port that leaves
  them `NULL` is not broken - the runtime takes the song over from the platform
  player on the first such call, which is what Android does. A port that wants
  to drop the built-in renderer altogether builds with `-DNJ_ENABLE_MIDI=0`.
- **`device_property` is a real hook, and its platform answer is now reachable.**
  It is asked for every key the runtime does not answer itself, which is how a
  port publishes its own identity - `"nano_j2me/headless"`, `"nano_j2me/sdl2"`,
  an Android model number. The platform string is NOT among the runtime's own
  facts any more, so the port's answer is the one a MIDlet sees; the built-in
  fallback `"nano_j2me"` is used only when the hook is absent or returns `NULL`,
  and a device profile installed through `nj_run_opts.props` (`--device
  nokia6230`) is consulted *before* the hook. Answering `NULL` means "no
  opinion" and is always correct.

Five details are Android-specific and each one cost a debugging session:

- **`ANativeWindow_lock` gives you a stride, not a width, and a format you did
  not choose.** Hand the stride to `display_init` and scale from it; and pack
  the pixels for `buffer.format` as it comes back from every lock, never for the
  format you asked for. `WINDOW_FORMAT_RGBA_8888` is `R,G,B,A` in memory from
  the lowest address, so the 32-bit value is `0xAABBGGRR` - writing the
  natural-looking `0xffRRGGBB` swaps red and blue on the panel while every byte
  the runtime produced is correct. `tools/make_colordemo_jar.sh` paints the
  three primaries and `tools/check_color_order.sh` reads them back, on a
  screenshot or on a headless frame dump, which is how that one is settled.
- **Do not map Android key codes to MIDP codes in Java.** Push the raw key code
  and let `nj_port_android_push_key` translate through the one table in the port
  - and expose that same table back to Java (`nativeKeyTable()`) so the on-screen
  keypad editor can only offer keys the port actually understands.
- **Touch arrives in view pixels and is needed in device pixels.** The port owns
  the letterbox transform, so the transform is published under the same lock
  that the blit uses before the frame reaches the panel, and
  `nj_port_android_touch` inverts it. A tap that lands between the two would
  otherwise be mapped with the previous geometry.
- **Text can come from the platform instead of from a table.** Android already
  ships a text stack with every script in it, so this port does not carry one:
  `tools/build_android.sh` compiles the runtime with `NJ_ENABLE_FONT_CJK=0`
  (that one table is ~480 KB per ABI) and the port installs three `nj_font`
  providers through `nj_run_opts.fallback_fonts`, whose `glyph_lookup` asks
  `Paint` for a coverage bitmap (`GlyphSource.java`) and caches it. A fallback
  is only consulted for what the built-in faces cannot draw, so Latin text
  keeps the deterministic bitmap font and only the wide characters change.
  Two traps: install the providers **before** `font_path`, because loading a
  TTF resets every slot's fallback to the built-in box; and the lookup runs on
  the VM thread, so it attaches to the JVM itself rather than borrowing the
  UI thread's `JNIEnv`.
- **An API-gated call must live in its own class, not in a branch.** The
  immersive-fullscreen replacement (`WindowInsetsController`, API 30) cannot sit
  in a method that also runs on Android 5: a method whose code mentions a class
  the device does not have can fail verification when its class is loaded, before
  the `SDK_INT` test is ever reached. It lives in `Ui.java`, which only the API
  30+ branch touches, and the legacy `SYSTEM_UI_FLAG_*` flags stay there for
  everything older.

The JNI handle is a `jlong`, not a `static nj_port`, so two games can never share
state and the library keeps passing `tools/check_no_statics.sh`; the VM runs on
its own thread which is joined before `nj_port_shutdown()`, and backgrounding
parks that thread in `idle()` instead of letting it spin against a dead surface.

### The shell around the game

Thirteen things in that shell are worth copying, because each one is a rule a plain
"put a SurfaceView in an Activity" port gets wrong:

- **The name comes from the jar, not from the file.** `JarName.of(File)` reads
  `MIDlet-1` (first field), then `MIDlet-Name`, then `MIDlet-2..9` out of the
  manifest, skipping records that are blank or a bare `-`; `JarStore.read()`
  makes that the title of the row and of the game that launches from it, and
  falls back to the file name only when the jar declares nothing. A rename made
  in the library is stored in the sidecar as `title=` + `titleset=1`, so it wins
  over the manifest and stays won; a legacy sidecar whose title is just the file
  name is re-derived instead of being mistaken for a rename. The file name keeps
  its own line in the list, so nothing becomes un-findable.
- **A keycap presses when the finger arrives, not when it leaves.** `PadPress`
  (pure Java, no Android imports) owns that state machine: a finger that lands on
  a cap presses it immediately, sliding off releases it, sliding onto another cap
  presses that one, a finger the game has already been told about is handed back
  to the game when it reaches a cap, and `cancel()` releases every held key
  exactly once when the panel opens, the activity pauses or the framework sends
  `ACTION_CANCEL`. The class is unit-tested off-device, which is the only
  reason the rules survived the move out of `PadView`.
- **Layouts are data.** `PadConfig` stores keycaps as fractions of the view
  (`0..1`, top-left corner) and ships eight presets (`PRESET_IDS`, `byId()`,
  `presetName()`, `presetHint()`): 方向盘, 经典手机, 全按键, 横屏双手, 街机双键,
  手机键盘, 单手右下 and an empty one to build on. The presets are *written* in
  short-side units, not in window fractions: a keycap is `0.185` units on its
  short side and the factory converts with the window it is given, so a pad that
  is square on a 1080x1920 phone is still square on a 1920x1080 one and on a
  320x640 emulator - raw fractions is what made the first eight presets look
  stretched. `Grid` holds that arithmetic (the design box, the bottom anchor, the
  mirroring) and `tools/check_android_java.sh`'s `PadConfigTest` asserts every
  preset on four window shapes: on screen, at least 16 px, aspect between 0.35
  and 3.6.
  A player can also keep an arrangement under a name (`layoutNames()`,
  `saveLayout()`, `loadLayout()`), pick it from the same chooser, and make one the
  default (`setDefaultLayout()`), and every game can have its own pad
  (`forGame(ctx, jar)` / `saveForGame()` - the editor writes the per-jar key when
  it was opened from a game and the default when it was opened from the library). 经典手机 is the handset the games were
  written for - twelve digits on the left, the eight-way cluster on the right,
  where the diagonals carry the digit codes 1/3/7/9 a second time because that is
  what they are on a phone; the preset list stayed at what a player can tell
  apart instead of one entry per key combination. It is also the default
  (`PadConfig.defaults()`): a fresh game starts from the handset keypad, while a
  saved arrangement - per game or the shared one - still wins over it. Editor and game load the same
  blob, so a layout tweaked in game is the one the editor opens. A cap carries an
  optional second code (`Key.code2`): 经典手机's diagonals press two directions
  at once, because a MIDlet steering with `Canvas.getKeyStates()` needs both bits
  held to walk diagonally - a single digit code would never move it. The digits
  those keys carry on a handset are the block to the left, which is what makes
  the trade affordable, and a pad saved before the change is rewritten on load
  (`upgradeClassicDiagonals`: the four corner codes on the right half only, so
  the digit block keeps its 1/3/7/9). The caps' transparency is the editor's
  slider - default `PadConfig.opacity = 0.50`, a percentage beside the thumb, and
  the caps preview the game's alpha while the thumb is held, since the editor
  otherwise paints them solid in order to be placeable.
- **The game's clock is a setting.** `nj_vm_set_speed(vm, percent)` scales the
  single clock a MIDlet can read (`nj_now_ms`: `Thread.sleep`, `Object.wait`,
  `java.util.Timer`, the frame pacer), so the panel's 游戏速度 row makes a slow
  game faster instead of skipping its frames; the pacer divides its wait by the
  same factor, which is what keeps the emulator from pinning a core at 300%. The
  Android side never touches the VM from the UI thread: `nativeSetSpeed` leaves a
  number behind and the port applies it at the top of its next frame.
  `tests/test_speed.c` drives a port whose clock the test owns and pins the
  arithmetic, including the two mistakes that look harmless and are not: the
  clock must not jump when the multiplier changes (every parked deadline would
  fire at once) and the 64-bit fold must survive the port's 32-bit clock wrapping
  (the field was 32-bit, so the fold was dead code and the MIDlet's clock jumped
  backwards every 49.7 days).
- **A canvas is only a canvas when it is one.** A port measures the box the game
  actually painted and hands it to the runtime, which snaps it to one of nineteen
  standard MIDP sizes with a tolerance of `size/16 + 3` px and accepts it only if
  it is smaller than nine tenths of the window in area
  (`src/core/nj_detect.c`, `nj_detect_snap`). A 240x305 measurement in a 240x320
  canvas is a game that did not paint its last fifteen rows, not a 240x305
  device: without the snap, the picture gets cropped and scaled to a size the
  title never asked for. `src/port/android/nj_port_android.c` keeps the raw box,
  refuses to publish anything until the same box has been seen for 300 frames
  (and gives up after 3000), and lets `nj_detect_snap` decide.
- **One adjustment page, not two.** `PadEditActivity` is where both the keycaps
  and the picture are placed: an empty-space drag moves the picture, two fingers
  scale it, and 画面 chooses the fit - plus a first-run card that says so, because
  none of those gestures are discoverable on their own. The page loads the game's
  sidecar (`JarStore.byName`) and writes it back with 保存, and
  `GameActivity.reloadView()` re-reads those fields in `onResume()`, so a game
  that is already running picks up the new position without a restart.
- **Tools are a panel, and the panel can get out of the way.** Nothing is drawn
  across the top of a game: the in-game controls sit behind the `≡` handle as a
  panel (调整画面和按键 / 状态信息 / 退出游戏), so the MIDlet's own title row stays
  visible and no strip is silently swallowing pad touches until a timer hides it.
  The editor's strip collapses with `▴` and comes back from a `▾ 工具条` pill, so a
  key can be placed along the very top edge - the one place a fixed strip makes
  unreachable.
- **The shell owns what outlives the game.** A MIDP `Player` is an
  `android.media.MediaPlayer`, and that player outlives the VM, the port and the
  Activity: a game closed with its music running used to keep playing over the
  library (`piid` still `state:started` after 退出). `MidiPlayer.pauseAll()` /
  `resumeAll()` follow the activity lifecycle - a player the MIDlet itself stopped
  stays stopped - and `closeAll()` runs when the VM thread ends and again in
  `onDestroy`. Volume keys are answered by the activity
  (`AudioManager.STREAM_MUSIC`, returning `false` only when there is no audio
  service) instead of being forwarded to the MIDlet, which has no volume key in
  its key set and would leave the player with no way to turn the sound down.
- **The library is searchable and keeps the order the player chose.** The field
  under the app bar filters in memory over the list already read - title or file
  name, case-insensitive - and the count line says `找到 N / M 个游戏`; 排序方式
  (最近运行 / 名称 / 文件大小 / 最近导入) is stored in `lib/sort`, so the library opens
  the way it was left. Sorting and filtering both happen on a copy, which is why
  neither touches the disk.
- **The picture can be moved in every mode.** The port applies zoom and pan on
  top of whichever fit was chosen - `NJ_VIEW_INT` keeps its integer scale - and
  publishes how far the pan may go once per frame
  (`nj_port_android_view_limits`, surfaced as `nativeViewLimits`) because only the
  blit knows the drawn size. `PictureFit` (pure Java, no Android imports) computes
  that same rectangle off-device from the window, the canvas size and those
  numbers, so the editor can draw where the picture will really land and show the
  limits it clamps to (`可挪 ±60 / ±320`) before the game is even started; the game
  clamps a restored pan to the port's own limits instead of guessing.
- **Immersive and the cutout are two different insets.** `Ui.apply()` hides the
  bars (API 30+ through `WindowInsetsController`, older through
  `SYSTEM_UI_FLAG_IMMERSIVE_STICKY`) and `Ui.edgeToEdge()` asks for
  `LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES` on API 28+; `Ui.insets()` then
  returns the display-cutout safe insets always, but the system-bar insets only
  while immersive - counting both when the window is already shrunk by the
  framework reserves the same space twice. `PadConfig.setSafeArea()` +
  `PadView.setSafeAreaPx()` move a keycap out of a notch band **without touching
  its stored position**: drawing and hit-testing both go through `rectOf()`, so a
  key under a camera hole is still where it is drawn, and the editor paints the
  amber border so the player can see why a key refuses to move further. The
  library's ⋮ menu owns the switch (`ui`/`immersive`, on by default).
- **The Material 3 look is hand-rolled, and the bars are not black.** No AndroidX
  or Material Components: `Md3` is ~500 lines of colour roles, shapes, ripple and
  type over the framework alone, which costs ~4 KB of dex instead of the 3.3 MB a
  shrunk `com.google.android.material` would add (the measured comparison is in
  the commit that added it). The scheme follows the system's night mode, and on
  API 31+ it is read from the wallpaper palette through the framework's own
  `android.R.color.system_*` resources - public, so no `R.java` is involved.
  `Ui.drawBehindBars()` makes the library edge-to-edge: the status and navigation
  bars take the app's surface colour with icons flipped to match
  (`APPEARANCE_LIGHT_*` on API 30+, `SYSTEM_UI_FLAG_LIGHT_*` below), and the
  library then pads itself with `Ui.insets(win, true)` because the bars are real
  space again. A `Theme.Material` dialog is dark-on-dark under a light palette,
  so `Md3.show()` recolours the message and each list row - colours only, never
  padding or text size, because restyling metrics after the dialog has been
  measured clips its last row. The chrome that floats **on the picture** - the
  control bar, the status card, the ≡ handle - keeps the dark end of the palette
  in both schemes (`Md3.OVERLAY`, `overlayButton()`, `overlayHandle()`): it sits
  on a black game, and a bright bar there is worse than the black system bars
  this shell used to have. The picture itself is never restyled.
- **A dialog is built, not configured.** The framework's `AlertDialog` cannot be
  made to look like Material 3 from a theme, so `Md3` supplies the card's
  anatomy and every screen assembles a body out of it: `body()` + `title()` or
  `titleWith(icon)` (24sp), `supporting()` (14sp) for the line that says what the
  choice costs, `section(label, text)` for an explained paragraph, `command()` for
  a chip that copies a shell command when tapped, `divider()`, and `row()` - a
  56dp (64dp with a hint) list item with a 24dp leading icon, a state layer, and a
  trailing check when it is the current choice. `chooser()` turns the six
  single-choice dialogs of this app into one call, and `dangerRow()` /
  `errorButton()` keep the destructive action in the error role instead of
  letting every action shout in the accent colour. The fourteen icons those rows
  lead with are drawn in a 24x24 grid (`Icon`, a `View` that scales its canvas)
  because the platform's own set is Material 2 and this build links no resources
  at all - and the stroke and glyph inside that scaled canvas must be given in
  grid units too, or the scale multiplies them and a 1.7-unit ring fills its own
  hole.

- **The first run has to look like a phone the player knows.** The pad the app
  opens with used to be its own design: a 0.30 x 0.20 fire key - 324 x 216 px on
  a 1080 x 1920 screen, the widest thing on the glass - over a 0.115 cross, which
  is not what any handset this runtime emulates ever had. `PadConfig.defaults()`
  now borrows `classic()`'s numbers (a 0.135 cap on a 0.143 step), so the cross,
  the fire key and the two soft keys are ~146 px squares at 1080 x 1920, and the
  picture is moved out of their way instead of the keys being shrunk around it:
  `AutoLayout.fit()` cuts the inset-safe area into vertical slabs at every key
  edge, keeps the tallest free band, and turns the largest whole-number scale
  that fits that band into a view request (`NJ_VIEW_CUSTOM` with
  `zoom = scale / base` and `dx, dy` = band centre minus window centre) - the
  presenter multiplies *every* mode's scale by the zoom and centres the result,
  so an integer scale anywhere in the window is expressible in a mode that
  already exists, with no new `nj_port` call. On 1080 x 1920 with a 240 x 320
  MIDlet that is 3x at `(0, -376)`, ending 79 px above the top of the cross.
  Whether the app may place the picture at all is remembered separately: the
  sidecar's `viewset` flag is set the moment the player saves, pans or zooms a
  picture of their own, because "these numbers are still the defaults" and "the
  player never chose" are different questions - someone who picks 整数倍 and
  leaves the pan alone writes exactly what a fresh game writes.

- **The ≡ handle is config, not chrome.** Its place lives in `PadConfig`
  (`menuX`, `menuY`, `menuVisible`, in the same blob as the keys, with an old
  blob simply having no `menu=` record), `positionHandle()` clamps it into the
  system insets so it can never hide under a notch, and hiding it leaves the
  panel reachable through Back. In the editor it is drawn and dragged like a key
  (same grid snap); the editor's own toolbar covers the top strip, which is
  exactly where the default corner place sits, so dragging it starts with ▴
  (the tip says so). The tests that keep this honest are pure Java and run
  without a device: `AutoLayoutTest` mirrors the presenter's arithmetic and
  checks every preset on four screen shapes, and `PadConfigTest` checks that the
  first run is handset-sized and that the menu place survives the blob.

- **Text comes from the platform, and the port is the last one to decide that.**
  The rasteriser already consults the glyph provider first, but *measurement* did
  not: `nj_font.c` returns the built-in cell's width for anything inside
  `first_char..last_char`, which is exactly the ASCII range - so Latin text was
  measured as if it came from the bundled bitmap and drawn with the phone's
  glyphs, and a proportional face came out with every advance wrong. Emptying
  that cell in `android_glyph_init()` (`first_char = 1, last_char = 0`) is only
  half of it, and the half that does nothing on its own: `nj_fontset_set_
  fallback_slots()` puts the port's faces in `.fallback`, so the primary slot -
  and its cell - is still the built-in copy, and the empty cell is never asked.
  The other half is a new additive hook, `nj_vm_set_primary_fonts()` (reached
  through `nj_run_opts.primary_fonts`), which installs the port's faces *as* the
  slots and leaves the built-in face as their `fallback` - so the platform
  answers for ordinary text and the bundled table still catches what it cannot
  draw. Bold is plumbed through the same path (`android_glyph_face.bold` -> the
  Java `render()` call), and both bold and smoothing are cache-key bits; the
  port's own 96-slot copy is keyed without either, so `nativeGlyphReset()` is
  what makes a settings change visible - the UI sets a word, the frame clears.

- **A font is a file, and a cache is a promise about memory.** `GlyphSource`
  caches rasterised coverage in an `LruCache` sized in *entries*, which says
  nothing about bytes - so it also reports `{entries, bytes}` and
  `trimToUsed()` drops every glyph the current run never asked for, which is what
  makes "keep only the characters this game uses" a real operation instead of a
  slogan. Replacement is a copy into `filesDir` (`FontKit.storeName()` names it,
  never the picker), a pref, and a `Typeface.createFromFile()` that throws its way
  back to the system face; the library's 显示设置 dialog and the game's panel show
  the same rows because they read the same prefs. Picture smoothing is the other
  half and it has to live in the presenter: whole-number scales are a block copy
  and stay exact, and only the fractional path samples - nearest, or a bilinear
  tap when `android_state.view_filter` says so. Both switches reach the port the
  way the speed does (the UI writes an int in `nativeSetFilter()`, the run thread
  reads it), because a view setting is not worth a data race.

- **A pad key can carry a code MIDP never had.** The on-screen pad can hold
  音量+ / 音量- (`PadConfig.full()`), and those two are forwarded as the
  platform's own key codes 24/25, labelled in `KeyCatalog`. The hardware rocker
  is *not*: it stays the system's, which is a decision written down in
  `GameActivity.dispatchKeyEvent` - every other app and the player's muscle
  memory agree on what that key means, and a J2ME title is not going to take it
  away. Two files outside the port directory had to agree on the code
  (`nj_midp.h` and `nj_key_to_midp()`), which is the real lesson: a new key is a
  runtime-wide constant, not a port detail.

- **A switch has to be enforced where the MIDlet's connection is made.** The
  library's per-game card owns two settings, and neither is a permission: the
  network switch is enforced inside `PlatformHttp.request` and
  `PlatformSocket.connect` (an immediate `0`, plus a line in the log naming the
  setting), and the sensor switch is enforced in `PlatformSensor`, where
  "simulate" hands the MIDlet a synthetic `sensor:` URL instead of opening a
  real one and "off" makes `open()` fail and `count()` answer zero. They are
  applied by `GameActivity.onCreate` before the VM exists, so no MIDlet can
  reach a device the settings deny - and the sidecar value is read with
  `parseFlag()`, not `parseInt()`, because `parseInt()` treats anything that is
  not positive as "missing", which is exactly wrong for `net=0`.

- **A game can be put away and picked up.** `nj_vm_save()` writes the whole
  machine - the arena image, the statics, the interned strings, every thread's
  Java stack and the frame it stopped in - and `nj_vm_load()` reads it back into
  a fresh VM with the same JAR attached. It is small here for one reason worth
  stating: this runtime has no mutable global state and exactly one OS thread
  executes Java, and Java threads are cooperative fibres that park by returning
  from the interpreter, so no C stack is ever suspended and a snapshot is a data
  structure rather than a process. Two things make it work in practice: the
  loader re-derives every host cache (class and field memos, `cp_cache`,
  resolved and linked classes) instead of imaging it, and the class list is
  matched by name *and* by ABI - instance size, field and method counts, statics
  order - so a rebuilt JAR is refused instead of half-applied. What a snapshot
  cannot carry is a live native handle: a MIDlet sitting inside `socket`,
  `sensor`, `bluetooth`, a `Player` or the framebuffer has no C state to image,
  so `nj_vm_can_save()` refuses while any native frame is on the stack, and the
  port drops the rest on load and lets the next call fail the way a closed
  connection does. One layout detail is easy to miss: an array's payload and an
  embedded string live at `object + sizeof(nj_object)`, so those two pointers are
  baked into the image and the loader recomputes them rather than copying them.
  The interned-string table is host-side, not an arena image: the blob stores
  `ninterned` plus one arena offset per entry, the loader grows the table to fit
  (so a snapshot may carry more literals than the runtime's initial 512 slots)
  and re-fills it, and a table that cannot be allocated is a load failure rather
  than a silently shorter `==` history. That growth makes compatibility one-way
  for snapshots that carry more than 512 literal entries: a newer binary loads
  an older snapshot, but an older binary refuses a newer one (it sees
  `ninterned` above its fixed bound, reports a format error and, at the CLI,
  starts fresh). Snapshots within the old limit remain interchangeable.

## Worked example: a bare-metal board

For a board with a small TFT and no OS:

```c
static nj_u4 bare_millis(void *ud) { return systick_ms(); }
static void bare_sleep(nj_u4 ms)   { delay_ms(ms); }
static int  bare_init(void *ud, int *w, int *h, int *stride, void **px)
{
    lcd_init();
    *w = 240; *h = 320; *stride = 240 * 2;
    *px = lcd_framebuffer();      /* a static uint16_t array is fine HERE: the
                                     linker places it in RAM, and the runtime
                                     does not care who owns it */
    return NJ_PIXFMT_RGB565;
}
```

Point the Java heap at a static array too — `nj_vm_create(&port, 0)` uses
`NJ_MAX_HEAP`, but you can hand it an exact region instead:

```c
static uint8_t java_heap[512 * 1024];
/* pass a port whose alloc() returns slices of java_heap; the arena is then
   static, the runtime still frees it correctly on destroy */
```

Sizing notes for a 512 KB part:

| Setting | Suggestion |
|---|---|
| `NJ_MAX_HEAP` | 320–448 KB |
| `NJ_MAX_CLASSES` | 128 |
| `NJ_MAX_THREADS` | 4 |
| `NJ_MAX_FRAMES` | 64 |
| `NJ_MAX_STACK_SLOTS` | 2048 |
| `NJ_ENABLE_FLOAT` | 1 (CLDC 1.1 needs it; the S3 has an FPU) |
| `NJ_ENABLE_DEBUG` | 0 |

The class-file parser keeps each class's bytes alive so the constant pool can be
resolved lazily. If a title's JAR is large, `NJ_MAX_CLASS_BYTES` and the number
of simultaneously resident classes are the two knobs that matter.


## Image codecs: plugging in the platform's decoder

Portable C PNG and JPEG are the right default, but they are rarely the right
answer on a specific board. An ESP32-S3 has a hardware JPEG block; Android has
`BitmapFactory`, which usually reaches hardware; a product with a fixed asset
pipeline may not want a JPEG decoder linked in at all.

Decoding therefore goes through a registry of providers
([`include/nj2me/nj_codec.h`](../include/nj2me/nj_codec.h)). The runtime ships
software PNG (and optionally software JPEG) as ordinary providers; a port
installs its own ahead of them and wins.

### Installing one

Fill in a provider and hand it to the VM. `nj_port.install_codecs` is called
once, right after the VM exists and *before* the built-ins are registered, so
anything installed there takes precedence:

```c
static nj_image *hw_jpeg_decode(const nj_codec_req *req)
{
    nj_image *img = nj_codec_image_new(req, w, h, NJ_CODEC_OUT_RGB565, 0);
    if (!img) return NULL;
    if (!esp_jpeg_run(req->data, req->size, img->pixels, &w, &h)) {
        req->dealloc(req->ud, img->pixels);
        req->dealloc(req->ud, img);
        if (req->err) *req->err = "the hardware JPEG block rejected the image";
        return NULL;
    }
    img->width = w; img->height = h;
    return img;
}

static const nj_codec hw_jpeg = {
    "jpeg-hw",
    { NJ_CODEC_FMT_JPEG, NJ_CODEC_OUT_RGB565, NJ_CODEC_SCALE_1 | NJ_CODEC_SCALE_2,
      4096u * 4096u, 0, 1 },
    jpeg_accepts, jpeg_info, hw_jpeg_decode,
    NULL, NULL, NULL
};

static void my_install_codecs(void *ud, struct nj_vm *vm)
{
    (void)ud;
    nj_codec_register(vm, &hw_jpeg, NJ_CODEC_PRIO_PLATFORM);
}
```

Then `port.install_codecs = my_install_codecs;`.

### The rules a provider must keep

| Rule | Why |
|---|---|
| `accepts()` only looks at a magic number | it runs for every provider on every decode |
| all memory comes from `req->alloc` / `req->dealloc` | teardown stays complete and the buffer can live in PSRAM |
| `info()` answers without decoding | `Image.createImage` bounds-checks with it; without it, probing a 2000×2000 image decodes it |
| bounds-check every read | a game JAR is untrusted input; a decoder that can be made to write outside its output is remote code execution |
| no mutable globals | same reason as everywhere else: games must start and stop cleanly |
| `attach` / `detach` for per-run state | that is the only sanctioned place for a hardware handle |

### Priorities

```
NJ_CODEC_PRIO_PLATFORM    200   what the port installs
NJ_CODEC_PRIO_PREFERRED   100
NJ_CODEC_PRIO_BUILTIN       0   software PNG / JPEG
NJ_CODEC_PRIO_FALLBACK   -100   provider that reports "no decoder for this"
```

Registering the same descriptor twice replaces the entry rather than
accumulating, so a port can be re-initialised safely.

### Why there is a "unavailable" provider

On a build with `NJ_ENABLE_JPEG=0`, a JPEG is still *recognised* — by a
fallback provider whose `decode` always fails with

> this build has no JPEG decoder; install one with `nj_codec_register()`

so a game fails with an actionable message instead of "unrecognised image
format". That distinction is worth a few hundred bytes of descriptor.

### Output formats

A provider may return `NJ_CODEC_OUT_ARGB8888` or `NJ_CODEC_OUT_RGB565`. The
latter halves the memory for a full-screen background — 150 KB instead of
300 KB at 240×320, which is the difference between running and not running on a
part without PSRAM. Consumers check `nj_image.format`; the blitter handles both.

### Verifying an adapter

`tests/test_codec.c` is the contract test: it installs a fake platform provider
that deliberately also claims PNG and proves that

- the platform provider wins, and unregistering restores the built-in;
- the registry is **per VM** — installing into one leaves another untouched,
  which is what lets a device run two games without interference;
- `attach`/`detach` run exactly once per VM and in the right order;
- teardown releases the registry and is identical on every create/destroy round.

Copy its shape when you add a codec: a provider that is only tested through the
built-in path has not been tested at all.

### The other two codec hooks

Images are not the only thing a platform decodes better than portable C, so
there are two plain `nj_port` fields beside `install_codecs`:

| Hook | What it decodes | Called for |
|---|---|---|
| `inflate` | one zlib stream (RFC 1950) | `.m3g` sections |
| `inflate_raw` | one raw DEFLATE stream (RFC 1951) | **every compressed zip entry**, which on a JAR means every class and every resource |
| `audio_decode` | a sampled-audio container, into signed 16-bit LE PCM | anything the runtime has no decoder for, and — since it is offered first — AMR and WAV as well |

`inflate_raw` is the one with the widest reach: a JAR is read through it on
every class load, so a platform with zlib (Android has had it in the NDK
sysroot since API 21) should implement it and can then build with
`-DNJ_ENABLE_INFLATE=0`, dropping about 3 KB of code *and* PNG's IDAT path
still works because PNG calls the same hook.

`audio_decode` is the two-call shape the header documents: call it with
`out == NULL` to be told how many PCM bytes the file holds and at what rate and
channel count, allocate exactly that, call it again to fill the buffer. A port
that answers `NJ_ERR_UNSUPPORTED` leaves the runtime to its own decoders and
then to its own refusal, which names the format.

**What a platform provider should NOT do** is hold a decoder open across calls
or render audio itself: `pcm_write()` is the runtime's only door to the audio
device, so a decoder's job is just to hand back PCM.  Note that `pcm_*` is one
device, not a stream - the runtime arbitrates which `Player` owns it and pauses
the others (see `docs/notes/2026-10-28-audio-device-ownership.md`).
`tone_play()` may be called while a PCM device is open; it must render into the
device's current rate and channel count instead of re-opening it (the SDL2 and
Android ports both do, so one device serves tones and sampled audio).
`pcm_queued()` is optional: it reports how many source frames are inside the
device but not yet audible.  The runtime uses it to report
`Player.getMediaTime()` at the audible position and to keep a `stop()`/`start()`
pair from skipping the queued tail; a port that leaves it `NULL` keeps the
older write-frontier behaviour.  A port
that wants true mixing can mix below `pcm_open`, but it must never close or
reconfigure the device behind the runtime's back: that is how a finished sound
effect used to take the background music with it.

## Networking: the HTTP hook
`javax.microedition.io.HttpConnection` needs a socket stack, which portable C
cannot have, so it is one optional field — `http_request` — that does a whole
request and buffers the whole response:

```c
nj_status (*http_request)(void *ud, const char *url, const char *method,
                          const char *headers,
                          const nj_u1 *body, nj_u4 body_len,
                          int *status, char *rheaders, nj_size rheaders_cap,
                          nj_u1 **out_body, nj_u4 *out_len,
                          void *(*alloc)(void *aud, nj_u4 n), void *aud);
```

The shape is deliberately not streaming. A MIDP `HttpConnection` is asked for
its response code and then read as a stream, and every title in
`tests/games` that touches the network does exactly that with a small
document; buffering removes a state machine from both sides of the HAL.
`headers` and `rheaders` are text blocks of `Name: value` lines, so a port
that has a header map does not have to know MIDP's accessor order. The body
comes back through `alloc` — the runtime's own tracked allocator — so the
runtime frees it and a leak shows up in the same count as everything else.

Return `NJ_ERR_UNSUPPORTED` (or leave the hook NULL) and `http://` is refused
by name: `IOException: this build has no HTTP support; the port must supply
it`. That is the correct answer for a bare-metal board, and it is what the
desktop ports do — try `--port headless` with `tests/fixtures/httpdemo.jar` to
see it.

On Android the hook is `PlatformHttp.java` over `HttpURLConnection`, and the
app needs two manifest facts: `android.permission.INTERNET` and a network
security config with `cleartextTrafficPermitted="true"`. Without the second
one every `http://` request fails with
`java.io.IOException: Cleartext HTTP traffic to <host> not permitted`, which
is what the first emulator run of the fixture printed — MIDlet URLs predate
TLS, and no per-host allowlist can be written for software that hard-codes
hosts from 2005.

## Sockets: the four stream hooks

`javax.microedition.io.SocketConnection` is the other half of "the network",
and it is four optional fields rather than one, because a socket is a
connection a MIDlet holds open and reads in pieces:

```c
void *(*socket_connect)(void *ud, const char *url, nj_u4 timeout_ms,
                        nj_socket_info *info, char *err, nj_size err_cap);
int   (*socket_read)(void *ud, void *handle, nj_u1 *buf, int len,
                     nj_u4 timeout_ms);
int   (*socket_write)(void *ud, void *handle, const nj_u1 *buf, int len);
void  (*socket_close)(void *ud, void *handle);
```

`url` is what the MIDlet passed to `Connector.open()` — `socket://host:port` —
and `handle` is whatever the port wants it to be: the runtime stores it as an
opaque value and never dereferences it. `socket_connect` fills in the peer and
local address/port a MIDlet reads back through `getAddress()`/`getPort()`,
where a name that resolved to an address should answer with the address; a port
with nothing to report leaves the fields empty and the runtime answers with the
host and port from the URL instead.

`socket_read` has three answers that a game can tell apart, and that is the
point of the split: **> 0** bytes read, **0** nothing arrived within
`timeout_ms`, **< 0** the peer is gone (the runtime turns `-1` into `read()`
returning `-1`, and any other negative into an `IOException`). A port whose
socket API only has "read or fail" must still produce that middle case rather
than blocking forever — a game loop that polls its lobby every frame is the
normal shape here. `socket_write` sends all of it or returns negative;
`socket_close` hangs up and releases the handle, and the runtime calls it
exactly once per connection.

Leave all four NULL and `socket://` is refused by name —
`ConnectionNotFoundException: this build has no socket support; the port must
supply it` — which is what `--port headless` prints for
`tests/fixtures/socketdemo.jar`, and what `tools/check_socket.sh` asserts in the
test suite. On Android the four hooks are `PlatformSocket.java` over
`java.net.Socket`; the positive half of the fixture is run against a host echo
server (`python3 tools/socket_echo_server.py 8080`, reached from the emulator as
`10.0.2.2:8080`).

### Worked example: Android

The Android port fills all of them, and each one is optional — the port still
builds and runs against an app that has none of the Java classes:

| Hook | Implementation |
|---|---|
| `install_codecs` | `PlatformImage.java` wraps `BitmapFactory`, so BMP and WebP (which this runtime cannot read at all) decode on Android. It is registered at priority `-1`, *below* the built-in providers, because BitmapFactory mishandles palette PNGs with `tRNS` and the built-in PNG decoder does not |
| `inflate` / `inflate_raw` | the platform's `zlib`, driven with `Z_FINISH` into a buffer of the declared size |
| `audio_decode` | `PlatformAudio.java` drives `MediaCodec` directly — **not** `MediaExtractor`, which on the API 36 emulator fails with `Service media.extractor didn't start` in an app process |
| `midi_open` … | `MidiPlayer.java` hands the file to the platform's own General MIDI synthesizer (Sonivox, behind `MediaPlayer`) |
| `http_request` | `PlatformHttp.java` wraps `HttpURLConnection`; the app declares `INTERNET` and permits cleartext, because a MIDlet's URL is `http://` |
| `sensor_list` … `sensor_close` | `PlatformSensor.java` over `android.hardware.Sensor`: eight Android sensor types are mapped onto the quantities a MIDlet asks for, and every open handle is one registered listener |
| `socket_connect` … `socket_close` | `PlatformSocket.java` over `java.net.Socket` (TCP_NODELAY on, because a J2ME game sends small messages and waits), with the Java-side id kept in a map and the peer address read back rather than copied from the URL |

`tools/make_platformimg_jar.sh` builds the fixture that holds those hooks to
account: one MIDlet that feeds the runtime five assets — `pic.bmp`, `pic.webp`,
`pic.png`, `tone.mp3` and `tone.amr` — through `Image.createImage(byte[])` and
`Manager.createPlayer()`, and prints one `PLATFORM <name> OK/FAILED` line each.
The same JAR is a positive test on Android and a negative one on the desktop,
and both answers are the expected ones:

```
(desktop, headless)
PLATFORM pic.bmp   FAILED java.io.IOException: cannot decode image: 3126 bytes starting 42 4d 36 0c
PLATFORM pic.png   OK     32x32 ffff0000 ff0000ff
PLATFORM tone.amr  OK     duration=520000 played=520000
(Android, API 36 and API 21)
PLATFORM pic.bmp OK / pic.webp OK / pic.png OK / tone.mp3 OK / tone.amr OK
```

The Android line was measured three times: once with everything built in, once
with `tools/build_android_apk.sh --no-amr --no-inflate --no-wav --no-gif`, and
once with `--no-png --no-jpeg --no-gif`. Every one of the five answers still
comes from the platform in all three — the JAR itself is inflated by the
platform's `zlib` through `inflate_raw`, AMR is decoded by `MediaCodec`, and the
pictures go through `BitmapFactory` even when all three built-in image decoders
are compiled out. That last run is the one that matters for `install_codecs`:
priority `-1` only keeps the built-in in front while it exists.

A decoder that is compiled out says so **by name**: the MIDlet's `IOException`
now carries the provider's reason, so

```
PLATFORM pic.png FAILED java.io.IOException: cannot decode image: 102 bytes
  starting 89 50 4e 47: this build has no PNG decoder; install one with
  nj_codec_register() (see include/nj2me/nj_codec.h)
```

is what a `-DNJ_ENABLE_PNG=0` build answers, instead of the same "cannot decode
image" a corrupt file gets.

## Bluetooth: the ten hooks

`javax.bluetooth` (JSR 82) is split the same way the package is: asking what is
out there, connecting to it, and serving it. The first half is four hooks, the
connection half is five, and every one of them may be NULL.

| Hook | Answers |
| --- | --- |
| `bt_local` | the local adapter: address, name, class-of-device, the discoverable mode, and how long an inquiry takes on this radio (`inquiry_ms`, 0 = the runtime's twelve seconds) |
| `bt_discoverable` | asks the radio to become (or stop being) discoverable; returns the mode achieved |
| `bt_inquiry` | fills in the devices found so far, at most `max`, waiting no longer than `duration_ms`. Called once per pump slice while the inquiry runs |
| `bt_services` | fills in the services known for one address, optionally filtered by a UUID |
| `bt_connect` | opens a CLIENT connection to a `btspp://<address>:<channel>` or `btl2cap://<address>:<psm>` URL and returns an opaque handle |
| `bt_listen` | opens a server on a `btspp://localhost:<uuid>` or `btl2cap://localhost:<uuid>` URL and returns an opaque handle |
| `bt_accept` | waits for a peer, at most `timeout_ms`, and returns a connection handle |
| `bt_read` / `bt_write` | the byte stream on that connection |
| `bt_close` | hangs up one connection, or one server |

`bt_connect` is deliberately NOT `socket_connect`, although both open a TCP-ish
stream: a Bluetooth connect has to name the RFCOMM channel or the L2CAP PSM, and
a port that has a socket layer but no radio must still refuse `btspp://` by
name. A port with both writes two small functions.

A port with no radio leaves all ten NULL. `SensorManager`-style degradation is
the rule here too: the classes are present, `LocalDevice.getLocalDevice()`
raises `BluetoothStateException`, and `Connector.open("btspp://...")` refuses by
name — with one sentence for a server URL and another for a client one.

An inquiry is a poll, not a callback: `bt_inquiry` is called once per frame-pump
slice, it must return devices in a STABLE order (discovery order) because the
runtime fires `deviceDiscovered()` only for the ones it has not reported yet,
and `inquiryCompleted()` arrives when `duration_ms` is up. A simulated radio
that knows its answer immediately sets `inquiry_ms` small; a real one leaves it
0 and gets the twelve seconds a real inquiry needs. `bt_accept` is called in
250 ms slices and the runtime stops after thirty seconds of REAL time, so a hook
that blocks for its whole slice is still bounded — the budget is measured with
the VM's clock, not by adding up the pauses.

On Android the bridge is `PlatformBluetooth.java`: discovery over
`BluetoothAdapter` and a `BroadcastReceiver`, servers over
`listenUsingInsecureRfcommWithServiceRecord()` (the SPP UUID) and
`listenUsingInsecureL2capChannel()` (API 29+), and clients over
`createInsecureRfcommSocket()`/`createInsecureL2capChannel()` driven from a
worker thread with a join timeout, because the platform's own connect has none.
`accept()` gets one parked waiter thread per server: Android's
`BluetoothServerSocket.accept(int timeout)` does not honour its timeout (the
call ends up in `waitSocketSignal`), and one thread per server is cheaper than
one per poll. The manifest declares `BLUETOOTH`/`BLUETOOTH_ADMIN` (up to API 30)
plus `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT`/`BLUETOOTH_ADVERTISE`. The three
modern ones are runtime permissions: this app never raises the prompt itself, so
on a device running Android 12 or newer grant them once with
`adb shell pm grant com.lucinhu.nj2me android.permission.BLUETOOTH_CONNECT`
(and `...BLUETOOTH_SCAN`, and `...BLUETOOTH_ADVERTISE` if the MIDlet calls
`setDiscoverable()`). Until then `LocalDevice.getLocalDevice()` raises
`BluetoothStateException`, which is the correct answer from a program's point of
view: no radio is available, keep playing.

An Android emulator HAS a virtual adapter, so there the walk goes further: the
address and name come back, `setDiscoverable()` works once `BLUETOOTH_ADVERTISE`
is granted (before that the platform refuses and the bridge says so once), an
inquiry runs its twelve seconds and finds nothing, and a connection to a device
that does not exist comes back as `ConnectionNotFoundException` rather than a
hang. `BluetoothAdapter.getBluetoothClass()` is a hidden API on modern releases,
so the class-of-device reads 0 — the same answer as a radio that will not say.

A real handset is the useful test. On a Redmi Note 12 Turbo (Android 15) the
same fixture reported `BT name Redmi Note 12 Turbo`, `BT addr 020000000000`,
`discoverable true`, and an inquiry that found three actual neighbours — a
`BT5.1 Mouse`, a `GamepadSpace-Q37` and `sirius` — before `inquiryCompleted()`
twelve seconds later; `searchServices` for 0x1101 came back empty for a device
that offers no SPP record, the client to a made-up address was refused with a
sentence, a real RFCOMM server with its SDP record opened, and both notifiers
gave up after exactly thirty seconds. Two things a port user should expect on
modern Android: the address is the privacy placeholder `02:00:00:00:00:00`
(third-party apps are no longer told the hardware address, and the runtime
treats it like any other address a radio will not name), and
`getBluetoothClass()` is hidden, so the class reads 0 unless the port is rooted.
Discovery also needs `BLUETOOTH_SCAN`, and with `neverForLocation` declared it
does not need the location permission.

One platform behaviour is worth knowing before a MIDlet calls
`LocalDevice.setDiscoverable()`: from Android 12 the system shows its own
consent dialog for that, and the dialog takes the focus, so the MIDlet's frame
pump stops until somebody answers it. That is the platform's rule, not this
runtime's, but it looks like a freeze from the inside — a game that makes itself
discoverable on the first frame will appear hung until the dialog is dismissed.
Measured on API 36: the walk stood still for three and a half minutes until
`adb shell input keyevent 4`, and then every remaining step ran in the expected
order, with each notifier's `acceptAndOpen()` giving up after exactly the
runtime's thirty seconds (`19:55:03.138` server opened, `19:55:33.336` no peer,
`19:56:03.544` the L2CAP notifier, `done`).

`tools/check_bt.sh` runs the whole walk against the headless port's fake radio
(`NJ_HEADLESS_BT=1`), which is opt-in precisely because the radio-less answer is
the default a test can assert.

## Sensors: the JSR 256 hooks

`javax.microedition.sensor` is a standard package with no standard source of
data: JSR 256 says a sensor is identified by a URL the platform invents. Four
optional fields carry the whole package, and each answers a question rather
than describing a mechanism:

| Hook | The question it answers |
|---|---|
| `sensor_list(ud, out, max)` | which sensors exist, and what each one measures |
| `sensor_open(ud, url, period_ms)` | a handle for one of them, sampled at about that rate |
| `sensor_read(ud, handle, values, max)` | the latest sample, one double per channel — 0 if there is none yet |
| `sensor_close(ud, handle)` | stop sampling and release the handle |

`nj_sensor_desc` is what a port fills in for one sensor: url, quantity,
context type, model, a channel count, and per channel a name, a unit
(`m/s^2`, `lx`, `hPa`), a measurement range and a resolution. The strings are
borrowed for the duration of the call only; the runtime copies what it keeps.

`sensor_read` is called from two places — `SensorConnection.getData()` and the
frame pump, once per emulated frame — so it must be cheap and must never
block: it returns what the sensor last reported, and 0 when nothing has been
reported yet. The one call allowed to wait is the long form of `getData()`,
which carries a timeout; the runtime sleeps in small steps until a sample
arrives or the timeout runs out. That matters on a platform like Android,
where a sensor only ever hands a value to a listener: the first synchronous
read after `open()` legitimately has nothing to return, and a MIDlet that
wants a reading asks for the long form.

Leave the hooks NULL and `SensorManager.findSensors()` finds nothing: the
package is present and answers "no sensors on this device", which is an answer
a MIDlet written for a handset is prepared to hear. Conditions are the one
part of JSR 256 this runtime does not evaluate — `Channel.addCondition()`
raises `UnsupportedOperationException` instead of silently never firing — and
availability push is not supported, so `isAvailabilityPushSupported()` answers
`false` and the listener interfaces exist to be compiled against.

The fixture works on a machine with no sensors at all, because the headless
port has a synthetic accelerometer:

    build/nj2me --port headless --run-frames 200 tests/fixtures/sensordemo.jar

`tools/check_sensor.sh` is that run with every line checked; the fixture is
this module's test, so there is no separate unit test for it. On Android,
where the data really comes from hardware:

    adb emu sensor set acceleration 1.5:-2.25:9.75

is what makes the emulator's accelerometer report the values the fixture then
prints.

## Text: built-in bitmaps or a TrueType file

By default every glyph comes from six generated bitmap faces checked into
`src/gfx/nj_font_data.h` (three sizes, plain and bold). Nothing has to be
provided by the port and nothing is loaded at runtime.

`nj_vm_use_font_file(vm, path)` replaces all six with faces rasterised from a
TrueType file instead. The file is read through the port's **file hooks**, so a
port that already implements `file_open`/`file_read`/`file_close` needs no new
callbacks. `alloc`/`free` are *not* required — the scratch buffer comes from the
VM's own tracked allocator, and `nj_port.h` documents those two as optional.

```c
if (!nj_vm_use_font_file(vm, "/system/fonts/DroidSansMono.ttf")) {
    /* The built-in faces are still installed and still correct.  This is a
     * normal outcome, not a failure: a missing, unreadable or malformed file
     * leaves the VM exactly as it was. */
}
```

The em size of each face is chosen so its `ascent + descent` lands on the same
box the bitmap face occupied. **That is the point of the design**: a MIDlet has
already committed to a layout using `Font.getHeight()` before you get a say, so
changing the font must not move a single baseline. What changes is the glyph
quality inside an unchanged box.

To compile the rasteriser out entirely — it is the largest optional component —
set `-DNJ_ENABLE_TTF=0`. `nj_vm_use_font_file()` then always returns 0 and the
bitmap faces are the only ones that exist. The bitmap faces are never trimmed
away: they are the fallback that makes a bad font file a non-event.

Verification is in three pieces, and they are deliberately separate:

- `tests/test_ttf.c` — the rasteriser alone, against FreeType through Pillow;
- `tests/test_font_coverage.c` — the renderer alone, against coverage values it
  chose itself, so a wrong pixel there cannot be the rasteriser's fault;
- `tools/check_ttf_text.sh` — end to end: a real game, with and without
  `--font`, asserting the frames differ and that an unusable file falls back
  with a warning instead of silently doing nothing.

## Bring-up checklist

1. Implement `millis`, `display_init`, `display_present`, `input_poll`, `log`.
2. Verify the framebuffer by running with `--port headless --dump-frames out/`
   first: the dumps prove the rasterizer works before any panel driver is
   involved.
3. Run `tools/check_no_statics.sh` with your port's objects added. A port with a
   mutable global will pass its own tests and then fail on the second MIDlet
   launch — catch it here instead.
4. Check `nj_vm_destroy()`'s return value is stable across three
   create/run/destroy cycles. On a device that never reboots, a drift is a leak.
5. Only then wire up audio, vibration and RecordStore persistence.
