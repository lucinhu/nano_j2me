/*
 * nano_j2me - the ESP-emulator UI demo.
 *
 * What this image demonstrates, and what it cannot
 * -----------------------------------------------
 * Espressif's esp-emu (0.50.0) models the ESP32-S3 CPU, PSRAM, flash, UART,
 * timers, SPI, I2C, LEDC/RMT/PCNT and the GPIO pads, but it models **no LCD
 * controller, no SD/MMC host and no way to drive a GPIO input from the
 * command line**.  So this image does not pretend a panel or a card is there.
 * It reproduces the three software paths a real handheld uses, with the
 * emulator's own facilities standing in for the hardware at the edges:
 *
 *   display  the port renders into the same 240x320 RGB565 framebuffer that
 *            nj_port_esp32.c DMAs to an ST77xx panel.  Instead of an SPI
 *            panel, display_present() is followed by a run-length-encoded
 *            frame dump on UART0; tools/run_esp_emulator_ui.sh decodes it to
 *            PNG.  The pixels are the runtime's real output.
 *
 *   buttons  a UART line protocol ("K:fire:1") feeds the port's input_poll(),
 *            so the events take the exact path GPIO buttons take on hardware
 *            (a queue drained by nj_midp_frame).  esp-emu's --inject/--inject-on
 *            flags generate them deterministically.
 *
 *   storage  esp-emu has no SD card, so game.jar is carried in a SPIFFS
 *            partition and mounted at /sdcard.  On a real board SDMMC + FAT
 *            mounts at the same VFS path and the same fopen()/fread() code
 *            reads it; only the mount call differs.
 *
 * The default fixture is keysdemo.jar: it repaints a different flat colour and
 * grows a bar on every key event, so "the frame after the injected keys differs
 * from the frame before them" is a real proof that the input path worked, and
 * the colour itself proves the rendering path.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
/* ESP-IDF v6 renamed the SPIFFS VFS header from esp_vfs_spiffs.h to
 * esp_spiffs.h; the API (esp_vfs_spiffs_register, esp_spiffs_info) is the
 * same.  The include is done through __has_include so the file also builds on
 * IDF v5 trees used by other projects. */
#if defined(__has_include)
#  if __has_include("esp_spiffs.h")
#    include "esp_spiffs.h"
#  else
#    include "esp_vfs_spiffs.h"
#  endif
#else
#  include "esp_spiffs.h"
#endif
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAG "nj2me-ui"

typedef struct ui_state {
    nj_u1  *fb;
    int     width, height, stride;

    QueueHandle_t keys;        /* nj_event, written by the UART key task */
    unsigned keys_delivered;
    unsigned presents;

    int     stop;
} ui_state;

/* ------------------------------------------------------------------ */
/* Port callbacks                                                      */
/* ------------------------------------------------------------------ */
static void *ui_alloc(void *ud, nj_size n)
{
    void *p = heap_caps_aligned_alloc(NJ_OBJ_ALIGN, (size_t)n,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        p = heap_caps_aligned_alloc(NJ_OBJ_ALIGN, (size_t)n,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    (void)ud;
    return p;
}

static void ui_free(void *ud, void *p)
{
    (void)ud;
    heap_caps_free(p);
}

static nj_u4 ui_millis(void *ud)
{
    (void)ud;
    return (nj_u4)(esp_timer_get_time() / 1000);
}

static void ui_sleep_ms(void *ud, nj_u4 ms)
{
    (void)ud;
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}

static void ui_idle(void *ud)
{
    (void)ud;
    vTaskDelay(1);
}

static void ui_log(void *user, int level, const char *msg)
{
    (void)user;
    /* Java System.out arrives here at level 2, so "KEY down -5" from the
     * fixture is visible in the UART log the host script checks. */
    switch (level) {
    case 0:  ESP_LOGE("nj2me", "%s", msg); break;
    case 1:  ESP_LOGW("nj2me", "%s", msg); break;
    case 3:  ESP_LOGD("nj2me", "%s", msg); break;
    default: ESP_LOGI("nj2me", "%s", msg); break;
    }
}

static int ui_display_init(void *ud, int *w, int *h, int *stride, void **px)
{
    ui_state *s = (ui_state *)ud;

    s->width  = NJ_SCREEN_WIDTH;
    s->height = NJ_SCREEN_HEIGHT;
    s->stride = s->width * 2;

    /* Same place nj_port_esp32.c puts it: PSRAM, because internal SRAM cannot
     * hold 150 KB next to the Java heap. */
    s->fb = (nj_u1 *)heap_caps_aligned_alloc(
        NJ_OBJ_ALIGN, (size_t)s->stride * (size_t)s->height,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s->fb) {
        s->fb = (nj_u1 *)heap_caps_aligned_alloc(
            NJ_OBJ_ALIGN, (size_t)s->stride * (size_t)s->height,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s->fb) {
        ESP_LOGE(TAG, "ui-demo: no memory for the %dx%d framebuffer",
                 s->width, s->height);
        return -1;
    }
    memset(s->fb, 0, (size_t)s->stride * (size_t)s->height);

    *w = s->width;
    *h = s->height;
    *stride = s->stride;
    *px = s->fb;
    return NJ_PIXFMT_RGB565;
}

static void ui_display_present(void *ud, int x, int y, int w, int h)
{
    ui_state *s = (ui_state *)ud;
    (void)x; (void)y; (void)w; (void)h;
    s->presents++;
}

static int ui_input_poll(void *ud, nj_event *ev)
{
    ui_state *s = (ui_state *)ud;
    nj_event got;

    if (!s->keys) {
        return 0;
    }
    if (xQueueReceive(s->keys, &got, 0) != pdTRUE) {
        return 0;
    }
    *ev = got;
    s->keys_delivered++;
    ESP_LOGI(TAG, "ui-demo: delivered key=%u type=%u",
             (unsigned)got.key, (unsigned)got.type);
    return 1;
}

/* ------------------------------------------------------------------ */
/* UART key injection: "K:<name>:<1|0>\n"                              */
/* ------------------------------------------------------------------ */
static int key_from_name(const char *name)
{
    if (!strcmp(name, "up"))     return NJ_KEY_UP;
    if (!strcmp(name, "down"))   return NJ_KEY_DOWN;
    if (!strcmp(name, "left"))   return NJ_KEY_LEFT;
    if (!strcmp(name, "right"))  return NJ_KEY_RIGHT;
    if (!strcmp(name, "fire"))   return NJ_KEY_FIRE;
    if (!strcmp(name, "soft1"))  return NJ_KEY_SOFT_LEFT;
    if (!strcmp(name, "soft2"))  return NJ_KEY_SOFT_RIGHT;
    if (!strcmp(name, "clear"))  return NJ_KEY_CLEAR;
    if (!strcmp(name, "star"))   return NJ_KEY_STAR;
    if (!strcmp(name, "pound"))  return NJ_KEY_POUND;
    if (!strcmp(name, "a"))      return NJ_KEY_GAME_A;
    if (!strcmp(name, "b"))      return NJ_KEY_GAME_B;
    if (!strcmp(name, "c"))      return NJ_KEY_GAME_C;
    if (!strcmp(name, "d"))      return NJ_KEY_GAME_D;
    if (name[0] >= '0' && name[0] <= '9' && name[1] == '\0') {
        return NJ_KEY_0 + (name[0] - '0');
    }
    return -1;
}

static void handle_key_line(ui_state *s, const char *line)
{
    char name[16];
    int down = 0;
    int key;

    if (strncmp(line, "K:", 2) != 0) {
        return;
    }
    if (sscanf(line + 2, "%15[^:]:%d", name, &down) != 2) {
        return;
    }
    key = key_from_name(name);
    if (key < 0) {
        ESP_LOGW(TAG, "ui-demo: unknown key '%s'", name);
        return;
    }
    {
        nj_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = down ? NJ_EV_KEY_DOWN : NJ_EV_KEY_UP;
        ev.key = (nj_u1)key;
        if (xQueueSend(s->keys, &ev, 0) == pdTRUE) {
            ESP_LOGI(TAG, "ui-demo: queued key %s %s",
                     name, down ? "down" : "up");
        }
    }
}

static void key_task(void *arg)
{
    ui_state *s = (ui_state *)arg;
    char line[64];
    size_t len = 0;

    for (;;) {
        nj_u1 buf[32];
        int n = uart_read_bytes(UART_NUM_0, buf, sizeof(buf),
                                pdMS_TO_TICKS(20));
        int i;
        for (i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (len > 0) {
                    line[len] = '\0';
                    handle_key_line(s, line);
                    len = 0;
                }
            } else if (len + 1 < sizeof(line)) {
                line[len++] = c;
            }
        }
        if (s->stop) {
            break;
        }
    }
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* RLE frame dump                                                      */
/* ------------------------------------------------------------------ */
/*
 * One "R:<run>:<rgb565>" line per run, bounded by FRAME:START/FRAME:END.
 * The host filters on the line prefix, so IDF log lines may interleave.
 */
static void dump_frame(ui_state *s)
{
    const nj_u2 *px = (const nj_u2 *)s->fb;
    long total = (long)s->width * (long)s->height;
    long i = 0;

    printf("FRAME:START:%d:%d\n", s->width, s->height);
    while (i < total) {
        nj_u2 v = px[i];
        long run = 1;
        while (i + run < total && px[i + run] == v) {
            run++;
        }
        i += run;
        while (run > 0) {
            long chunk = run > 0xFFFF ? 0xFFFF : run;
            printf("R:%lx:%04x\n", (unsigned long)chunk, (unsigned)v);
            run -= chunk;
        }
    }
    printf("FRAME:END\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Demo                                                                */
/* ------------------------------------------------------------------ */
static nj_u1 *load_jar(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    nj_u1 *buf;

    if (!f) {
        ESP_LOGE(TAG, "ui-demo: cannot open '%s' on the mounted filesystem",
                 path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        ESP_LOGE(TAG, "ui-demo: '%s' is empty", path);
        return NULL;
    }
    buf = (nj_u1 *)heap_caps_aligned_alloc(NJ_OBJ_ALIGN, (size_t)size,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (nj_u1 *)heap_caps_aligned_alloc(
            NJ_OBJ_ALIGN, (size_t)size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG, "ui-demo: no memory for the %ld-byte JAR", size);
        return NULL;
    }
    if (fread(buf, 1u, (size_t)size, f) != (size_t)size) {
        fclose(f);
        heap_caps_free(buf);
        ESP_LOGE(TAG, "ui-demo: short read from '%s'", path);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)size;
    return buf;
}

/*
 * Java heap sizing, the same shape tests/embedded/idf/main/main.c uses on the
 * board: keep the class library in PSRAM and leave internal SRAM for the WiFi
 * and driver stacks (this image has none, but the rule is the port's rule).
 */
static nj_size ui_vm_heap_bytes(void)
{
    const nj_size mib = 1024u * 1024u;
    nj_size psram = (nj_size)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    nj_size heap = 0;

    if (psram > mib) {
        heap = psram - mib;               /* 1 MiB stays free for the rest */
        if (heap > 6u * mib) {
            heap = 6u * mib;
        }
    }
    if (heap < 2u * mib) {
        nj_size internal = (nj_size)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        heap = internal > (64u * 1024u) ? internal - (48u * 1024u) : 0;
        if (heap > (256u * 1024u)) {
            heap = 256u * 1024u;
        }
        if (heap) {
            ESP_LOGW(TAG, "ui-demo: no PSRAM pool, using %u KiB of internal RAM",
                     (unsigned)(heap / 1024u));
        }
    }
    return heap;
}

void ui_demo_main(void)
{
    static ui_state s;
    nj_port port;
    nj_vm *vm = NULL;
    nj_app *app = NULL;
    nj_ref midlet = 0;
    nj_ref exc = 0;
    nj_u1 *jar;
    size_t jar_size = 0;
    size_t total = 0, used = 0;
    long frame;

    memset(&s, 0, sizeof(s));

    ESP_LOGI(TAG, "ui-demo: esp-emu models no LCD, no SD card and no GPIO "
                  "input, so this image reproduces the three software paths "
                  "instead (framebuffer capture, UART key injection, SPIFFS "
                  "mounted as /sdcard)");
    ESP_LOGI(TAG, "ui-demo: framebuffer %dx%d RGB565 from the same port "
                  "contract nj_port_esp32.c drives over SPI",
             NJ_SCREEN_WIDTH, NJ_SCREEN_HEIGHT);

    /* ---- the "SD card" ---- */
    {
        esp_vfs_spiffs_conf_t conf;
        esp_err_t err;

        memset(&conf, 0, sizeof(conf));
        conf.base_path = "/sdcard";
        conf.partition_label = "storage";
        conf.max_files = 5;
        conf.format_if_mount_failed = true;
        err = esp_vfs_spiffs_register(&conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ui-demo: SPIFFS mount failed: %s",
                     esp_err_to_name(err));
            printf("UI:DONE\n");
            fflush(stdout);
            return;
        }
        if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "ui-demo: mounted simulated SD card at /sdcard "
                          "(%u/%u bytes)", (unsigned)used, (unsigned)total);
        }
    }

    jar = load_jar("/sdcard/game.jar", &jar_size);
    if (!jar) {
        printf("UI:DONE\n");
        fflush(stdout);
        return;
    }
    ESP_LOGI(TAG, "ui-demo: loaded /sdcard/game.jar (%u bytes) from SPIFFS",
             (unsigned)jar_size);

    /* ---- the port ---- */
    s.keys = xQueueCreate(16u, sizeof(nj_event));
    if (!s.keys) {
        ESP_LOGE(TAG, "ui-demo: cannot create the key queue");
        printf("UI:DONE\n");
        fflush(stdout);
        return;
    }
    memset(&port, 0, sizeof(port));
    port.name = "esp-emu-ui";
    port.ud = &s;
    port.alloc = ui_alloc;
    port.free = ui_free;
    port.millis = ui_millis;
    port.sleep_ms = ui_sleep_ms;
    port.idle = ui_idle;
    port.log = ui_log;
    port.display_init = ui_display_init;
    port.display_present = ui_display_present;
    port.input_poll = ui_input_poll;
    port.width = NJ_SCREEN_WIDTH;
    port.height = NJ_SCREEN_HEIGHT;
    port.fps_cap = 60;   /* pace the demo so injected keys arrive between frames */
    port.rms_dir = "/sdcard/nano_j2me";

    /* UART RX is the virtual keypad.  The console uses UART0 for output; the
     * driver is only needed for reading, and a pre-installed driver (reporting
     * ESP_ERR_INVALID_STATE) is fine too. */
    {
        esp_err_t err = uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "ui-demo: UART driver install returned %s; key "
                          "injection may not work",
                     esp_err_to_name(err));
        }
        xTaskCreate(key_task, "njkeys", 4096, &s, 5, NULL);
    }

    /* ---- the MIDlet ---- */
    {
        nj_size heap_bytes = ui_vm_heap_bytes();
        ESP_LOGI(TAG, "ui-demo: Java heap %u KiB", (unsigned)(heap_bytes / 1024u));
        vm = nj_vm_create(&port, heap_bytes);
    }
    if (!vm) {
        ESP_LOGE(TAG, "ui-demo: nj_vm_create failed");
        goto done;
    }
    nj_vm_set_device(vm, nj_device_default());
    app = nj_app_open(vm, jar, (nj_u4)jar_size);
    if (!app) {
        ESP_LOGE(TAG, "ui-demo: not a usable MIDlet suite");
        goto done;
    }
    {
        const char *cls = nj_app_midlet_class(app, 0);
        if (!cls) {
            ESP_LOGE(TAG, "ui-demo: the manifest names no MIDlet class");
            goto done;
        }
        ESP_LOGI(TAG, "ui-demo: running %s", cls);
        midlet = nj_vm_new_midlet(vm, cls, &exc);
        if (!midlet) {
            ESP_LOGE(TAG, "ui-demo: cannot instantiate %s: %s", cls,
                     exc ? nj_exception_describe(vm) : "no exception");
            goto done;
        }
        if (nj_vm_start_app(vm, midlet) != NJ_OK) {
            ESP_LOGE(TAG, "ui-demo: startApp() failed: %s",
                     nj_exception_describe(vm));
            goto done;
        }
    }

    /* ---- the frame loop, with two captures and a keyboard injection ---- */
    for (frame = 0; frame < 4000; frame++) {
        if (nj_midp_frame(vm) != NJ_OK) {
            ESP_LOGE(TAG, "ui-demo: the frame loop stopped at frame %ld",
                     frame);
            break;
        }
        if (frame == 10) {
            ESP_LOGI(TAG, "ui-demo: capture 1 at frame %ld (before keys)",
                     frame);
            dump_frame(&s);
            /* The host injects the key lines when it sees this marker. */
            printf("UI:READY\n");
            fflush(stdout);
        }
        if (frame == 140) {
            ESP_LOGI(TAG, "ui-demo: capture 2 at frame %ld (%u key event(s) "
                          "delivered)", frame, s.keys_delivered);
            dump_frame(&s);
            break;
        }
        if (!nj_vm_running(vm)) {
            ESP_LOGI(TAG, "ui-demo: the MIDlet ended at frame %ld", frame);
            break;
        }
    }

    {
        nj_vm_stats st;
        nj_vm_get_stats(vm, &st);
        ESP_LOGI(TAG, "ui-demo: classes=%u heap=%lu/%lu",
                 (unsigned)st.classes, (unsigned long)st.heap_used,
                 (unsigned long)st.heap_peak);
    }
    ESP_LOGI(TAG, "ui-demo: frames=%ld presents=%u keys_delivered=%u",
             frame, s.presents, s.keys_delivered);
    ESP_LOGI(TAG, "ui-demo: done");

done:
    /* Let the UART drain, then give the emulator its exit marker. */
    vTaskDelay(pdMS_TO_TICKS(300));
    printf("NJ2ME DONE rc=%d\n", (vm != NULL && app != NULL) ? 0 : 1);
    printf("UI:DONE\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));

    s.stop = 1;
    if (vm) {
        nj_midp_destroy_app(vm, 0);
        nj_app_close(vm, app);
        nj_vm_destroy(vm);
    }
    if (jar) {
        heap_caps_free(jar);
    }
}

/* The port is only used by this file; keep the include path honest. */
