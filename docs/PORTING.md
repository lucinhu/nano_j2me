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
| `now` | wall-clock date and time | Java epoch |
| `file_*` | RecordStore persistence | C library I/O |
| `app_blob` | a JAR flashed into a partition | nothing |
| `tone_play` / `tone_stop` | `Player` tones | silence |
| `vibrate` | `Display.vibrate()` | no-op |
| `shutdown` | release port-owned state | nothing |

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
`pcm_open`/`pcm_write`/`pcm_close`/`pcm_free`, `device_property`, `vibrate` and
`idle`. Four of those are worth copying:

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

Seven things in that shell are worth copying, because each one is a rule a plain
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
  exactly once when the control bar opens, the activity pauses or the framework
  sends `ACTION_CANCEL`. The class is unit-tested off-device, which is the only
  reason the rules survived the move out of `PadView`.
- **Layouts are data.** `PadConfig` stores keycaps as fractions of the view
  (`0..1`, top-left corner) and ships eight presets (`PRESET_IDS`, `byId()`,
  `presetName()`, `presetHint()`): 方向盘, 数字键盘, 全按键, 横屏双手, 街机双键,
  极简方向, 手机键盘 and an empty one to build on. Editor and game load the same
  blob, so a layout tweaked in game is the one the editor opens.
- **One adjustment page, not two.** `PadEditActivity` is where both the keycaps
  and the picture are placed: an empty-space drag moves the picture, two fingers
  scale it, and 画面 chooses the fit. The in-game bar is down to 调整 / 状态 / 退出 -
  调整 opens that page over the running game - because setting the keys and the
  screen up in the same place beats hunting for two separate screens. The page
  loads the game's sidecar (`JarStore.byName`) and writes it back with 保存, and
  `GameActivity.reloadView()` re-reads those fields in `onResume()`, so a game
  that is already running picks up the new position without a restart.
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
or render audio itself: `pcm_write()` already mixes tone and sampled audio at
whatever rate it is given (8 kHz AMR and 44.1 kHz MP3 both go through the same
resampler), so a decoder's only job is to hand back PCM.

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
