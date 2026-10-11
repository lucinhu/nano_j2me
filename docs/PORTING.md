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

The NDK port is a software framebuffer blitted into an `ANativeWindow`; a
thread-safe event queue carries input from the Java side.

```java
// MainActivity.java
public class MainActivity extends Activity {
    static { System.loadLibrary("nj2me"); }
    private native void nativeCreate(Surface surface, int w, int h);
    private native void nativeResize(int w, int h);
    private native void nativeDestroy();
    private native void nativeKey(int keyCode, boolean down);
    private native void nativeRun(String jarPath);

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        nativeKey(code, true);  return true;
    }
    @Override public boolean onKeyUp(int code, KeyEvent e) {
        nativeKey(code, false); return true;
    }
}
```

```c
/* nj2me_jni.c */
static nj_port g_port;            /* per-activity, owned by the native side */
static jobject g_window;

JNIEXPORT void JNICALL
Java_com_example_MainActivity_nativeCreate(JNIEnv *env, jobject thiz,
                                           jobject surface, jint w, jint h)
{
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    nj_port_android(&g_port);
    nj_port_android_set_window(&g_port, win);
}
```

Two Android-specific details:

- **`ANativeWindow_lock` gives you a stride, not a width.** Hand that stride
  straight to `display_init`; the rasterizer already respects it, so no copy is
  needed between the Java heap and the window buffer.
- **Do not map Android key codes to MIDP codes in Java.** Push the raw key code
  and let `nj_port_android_push_key` do the translation, so the mapping stays in
  one place next to the port.

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
