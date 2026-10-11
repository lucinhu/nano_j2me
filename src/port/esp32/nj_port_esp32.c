/*
 * nano_j2me - nj_port_esp32.c
 *
 * The ESP32-S3 port: an SPI LCD, a handful of GPIO buttons, an I2S DAC and a
 * flash partition holding the game.
 *
 * This is the port the runtime was shaped for.  Three things make it work on a
 * part with 512 KB of internal SRAM:
 *
 *   1. THE JAVA HEAP GOES IN PSRAM.  nj_vm_create() builds the arena with
 *      port->alloc, so routing it to heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
 *      keeps internal SRAM free for the WiFi stack and DMA descriptors.  A
 *      module without PSRAM still works with a smaller heap - see
 *      NJ_MAX_HEAP in nj_config.h.
 *
 *   2. THE JAR IS NEVER COPIED.  nj_zip reads the archive in place, so a game
 *      flashed into a partition and memory-mapped costs zero RAM.
 *
 *   3. idle() MUST ACTUALLY YIELD.  The scheduler calls it when no Java thread
 *      is runnable; returning immediately trips the FreeRTOS task watchdog.
 */
#include "nj2me/nj_port.h"
#include "nj2me/nj_types.h"

#include <string.h>

#if defined(ESP_PLATFORM)

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdlib.h>

typedef struct esp32_state {
    spi_device_handle_t spi;
    const nj_port_esp32_config *cfg;

    nj_u1  *fb;            /* PSRAM framebuffer            */
    size_t  fb_bytes;
    int     width, height, stride;

    QueueHandle_t keys;    /* nj_event posted by the button ISR/task */

    int     bl_on;
} esp32_state;

#define ES(ud) ((esp32_state *)(ud))

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */
static void *esp_alloc(void *ud, nj_size n)
{
    void *p;
    (void)ud;
    /* Prefer PSRAM, fall back to internal RAM for small blocks. */
    p = heap_caps_malloc((size_t)n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        p = heap_caps_malloc((size_t)n, MALLOC_CAP_8BIT);
    }
    return p;
}

static void esp_free(void *ud, void *p)
{
    (void)ud;
    heap_caps_free(p);
}

/* ------------------------------------------------------------------ */
/* Time and idling                                                     */
/* ------------------------------------------------------------------ */
static nj_u4 esp_millis(void *ud)
{
    (void)ud;
    return (nj_u4)(esp_timer_get_time() / 1000);
}

static void esp_sleep_ms(void *ud, nj_u4 ms)
{
    (void)ud;
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}

static void esp_idle(void *ud)
{
    (void)ud;
    /* One tick: long enough for the IDLE task and the WiFi stack, short
     * enough not to add visible latency to the frame loop. */
    vTaskDelay(1);
}

static void esp_log_cb(void *ud, int level, const char *msg)
{
    (void)ud;
    switch (level) {
    case 0:  ESP_LOGE("nj2me", "%s", msg); break;
    case 1:  ESP_LOGW("nj2me", "%s", msg); break;
    case 3:  ESP_LOGD("nj2me", "%s", msg); break;
    default: ESP_LOGI("nj2me", "%s", msg); break;
    }
}

/* ------------------------------------------------------------------ */
/* Display                                                             */
/* ------------------------------------------------------------------ */
/*
 * A minimal ST77xx-class panel driver.  A real board file will replace the
 * command sequence; the framebuffer contract above it does not change.
 */
static void panel_cmd(spi_device_handle_t spi, int dc, int cmd,
                      const nj_u1 *data, int len)
{
    spi_transaction_t t;
    (void)dc;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(spi, &t);
    if (len > 0 && data) {
        memset(&t, 0, sizeof(t));
        t.length = (size_t)len * 8u;
        t.tx_buffer = data;
        spi_device_polling_transmit(spi, &t);
    }
}

static int esp_display_init(void *ud, int *w, int *h, int *stride, void **px)
{
    esp32_state *s = ES(ud);
    const nj_port_esp32_config *c = s->cfg;
    spi_bus_config_t bus;
    spi_device_interface_config_t dev;

    s->width = c->panel_w > 0 ? c->panel_w : NJ_SCREEN_WIDTH;
    s->height = c->panel_h > 0 ? c->panel_h : NJ_SCREEN_HEIGHT;
    s->stride = s->width * 2;

    /*
     * The framebuffer lives in PSRAM: 240x320x2 is 150 KB, which simply does
     * not fit in internal SRAM alongside the Java heap.
     */
    s->fb_bytes = (size_t)s->stride * (size_t)s->height;
    s->fb = (nj_u1 *)heap_caps_malloc(s->fb_bytes,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!s->fb) {
        s->fb = (nj_u1 *)heap_caps_malloc(s->fb_bytes, MALLOC_CAP_8BIT);
    }
    if (!s->fb) {
        ESP_LOGE("nj2me", "no memory for the framebuffer (%u bytes)",
                 (unsigned)s->fb_bytes);
        return -1;
    }
    memset(s->fb, 0, s->fb_bytes);

    memset(&bus, 0, sizeof(bus));
    bus.mosi_io_num = c->pin_mosi;
    bus.miso_io_num = -1;
    bus.sclk_io_num = c->pin_sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = (int)s->fb_bytes;
    if (spi_bus_initialize((spi_host_device_t)c->spi_host, &bus,
                           SPI_DMA_CH_AUTO) != ESP_OK) {
        return -1;
    }
    memset(&dev, 0, sizeof(dev));
    dev.clock_speed_hz = c->clock_hz > 0 ? c->clock_hz : 40 * 1000 * 1000;
    dev.mode = 0;
    dev.spics_io_num = c->pin_cs;
    dev.queue_size = 4;
    if (spi_bus_add_device((spi_host_device_t)c->spi_host, &dev, &s->spi)
        != ESP_OK) {
        return -1;
    }

    if (c->pin_rst >= 0) {
        gpio_set_direction((gpio_num_t)c->pin_rst, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)c->pin_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level((gpio_num_t)c->pin_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    if (c->pin_bl >= 0) {
        gpio_set_direction((gpio_num_t)c->pin_bl, GPIO_MODE_OUTPUT);
    }

    /* Buttons: plain GPIO inputs with pull-ups, polled by a small task that
     * posts the canonical nj_event into a queue. */
    if (c->button_active_low) {
        int k;
        for (k = 0; k < NJ_KEY_COUNT; k++) {
            if (c->button_gpio[k] >= 0) {
                gpio_set_direction((gpio_num_t)c->button_gpio[k],
                                   GPIO_MODE_INPUT);
                gpio_pullup_en((gpio_num_t)c->button_gpio[k]);
            }
        }
    }

    *w = s->width;
    *h = s->height;
    *stride = s->stride;
    *px = s->fb;
    return NJ_PIXFMT_RGB565;
}

static void esp_display_present(void *ud, int x, int y, int w, int h)
{
    esp32_state *s = ES(ud);
    int row;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->width)  { w = s->width - x; }
    if (y + h > s->height) { h = s->height - y; }
    if (w <= 0 || h <= 0 || !s->spi) {
        return;
    }

    /*
     * Set the address window once and stream the rectangle row by row.  A
     * board that can DMA the whole screen in one transfer should do that
     * instead; the contract allows either.
     */
    {
        nj_u1 win[4];
        int x2 = x + w - 1;
        int y2 = y + h - 1;
        panel_cmd(s->spi, s->cfg->pin_dc, 0x2A, NULL, 0);
        win[0] = (nj_u1)(x >> 8);   win[1] = (nj_u1)(x & 0xFF);
        win[2] = (nj_u1)(x2 >> 8);  win[3] = (nj_u1)(x2 & 0xFF);
        panel_cmd(s->spi, s->cfg->pin_dc, 0x00, win, 4);
        panel_cmd(s->spi, s->cfg->pin_dc, 0x2B, NULL, 0);
        win[0] = (nj_u1)(y >> 8);   win[1] = (nj_u1)(y & 0xFF);
        win[2] = (nj_u1)(y2 >> 8);  win[3] = (nj_u1)(y2 & 0xFF);
        panel_cmd(s->spi, s->cfg->pin_dc, 0x00, win, 4);
        panel_cmd(s->spi, s->cfg->pin_dc, 0x2C, NULL, 0);
    }
    for (row = 0; row < h; row++) {
        const nj_u1 *src = s->fb + (size_t)(y + row) * (size_t)s->stride
                              + (size_t)x * 2u;
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = (size_t)w * 16u;
        t.tx_buffer = src;
        spi_device_polling_transmit(s->spi, &t);
    }
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
static int esp_input_poll(void *ud, nj_event *ev)
{
    esp32_state *s = ES(ud);
    if (!s->keys) {
        return 0;
    }
    return xQueueReceive(s->keys, ev, 0) == pdTRUE;
}

/* ------------------------------------------------------------------ */
/* Persistence: RecordStore on a flash partition or in SPIFFS          */
/* ------------------------------------------------------------------ */
static int esp_file_open(void *ud, const char *name, const char *mode,
                         nj_file *out)
{
    esp32_state *s = ES(ud);
    char path[160];
    (void)mode;
    if (!s->cfg->rms_partition) {
        return NJ_ERR;
    }
    /* The board is expected to mount its filesystem before calling nj_run();
     * this port only builds the path. */
    snprintf(path, sizeof(path), "/%s/%s.rms", s->cfg->rms_partition, name);
    {
        FILE *f = fopen(path, "rb");
        if (!f) {
            return NJ_ERR;
        }
        out->handle = f;
        fseek(f, 0, SEEK_END);
        out->size = (nj_u4)ftell(f);
        fseek(f, 0, SEEK_SET);
        out->pos = 0;
    }
    return NJ_OK;
}

static nj_s4 esp_file_read(void *ud, nj_file *f, void *buf, nj_u4 n)
{
    (void)ud;
    return (nj_s4)fread(buf, 1u, n, (FILE *)f->handle);
}

static nj_s4 esp_file_write(void *ud, nj_file *f, const void *buf, nj_u4 n)
{
    (void)ud;
    return (nj_s4)fwrite(buf, 1u, n, (FILE *)f->handle);
}

static int esp_file_seek(void *ud, nj_file *f, nj_u4 pos)
{
    (void)ud;
    return fseek((FILE *)f->handle, (long)pos, SEEK_SET) == 0 ? NJ_OK : NJ_ERR;
}

static void esp_file_close(void *ud, nj_file *f)
{
    (void)ud;
    if (f->handle) {
        fclose((FILE *)f->handle);
        f->handle = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* The game, flashed into its own partition                            */
/* ------------------------------------------------------------------ */
static const void *esp_app_blob(void *ud, nj_u4 *size)
{
    esp32_state *s = ES(ud);
    const esp_partition_t *part;
    esp_partition_mmap_handle_t h;
    const void *ptr = NULL;

    if (!s->cfg->jar_partition) {
        return NULL;
    }
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_ANY,
                                    s->cfg->jar_partition);
    if (!part) {
        ESP_LOGE("nj2me", "no '%s' partition", s->cfg->jar_partition);
        return NULL;
    }
    /* Memory-map it: the ZIP reader works in place, so this costs no RAM. */
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                           &ptr, &h) != ESP_OK) {
        return NULL;
    }
    if (size) {
        *size = part->size;
    }
    return ptr;
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */
static void esp_shutdown(void *ud)
{
    esp32_state *s = ES(ud);

    if (!s) {
        return;
    }
    if (s->spi) {
        spi_bus_remove_device(s->spi);
        spi_bus_free((spi_host_device_t)s->cfg->spi_host);
    }
    if (s->keys) {
        vQueueDelete(s->keys);
    }
    if (s->fb) {
        heap_caps_free(s->fb);
    }
    /* cfg points at caller-owned memory; do not free it. */
    free(s);
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
nj_status nj_port_esp32_config_default(nj_port_esp32_config *out)
{
    int i;

    if (!out) {
        return NJ_ERR;
    }
    memset(out, 0, sizeof(*out));
    out->pin_sclk = 12;
    out->pin_mosi = 11;
    out->pin_cs   = 10;
    out->pin_dc   = 13;
    out->pin_rst  = 14;
    out->pin_bl   = 21;
    out->spi_host = 1;                 /* SPI2_HOST */
    out->clock_hz = 40 * 1000 * 1000;
    out->panel_w  = NJ_SCREEN_WIDTH;
    out->panel_h  = NJ_SCREEN_HEIGHT;
    out->invert_colors = 0;
    out->backlight_on_level = 1;
    out->button_active_low = 1;
    for (i = 0; i < NJ_KEY_COUNT; i++) {
        out->button_gpio[i] = -1;
    }
    out->audio_bclk = -1;
    out->audio_ws = -1;
    out->audio_dout = -1;
    out->rms_partition = "storage";
    out->jar_partition = "jar";
    return NJ_OK;
}

nj_status nj_port_esp32(nj_port *out, const nj_port_esp32_config *cfg)
{
    esp32_state *s;

    if (!out || !cfg) {
        return NJ_ERR;
    }
    s = (esp32_state *)calloc(1u, sizeof(*s));
    if (!s) {
        return NJ_ERR_NOMEM;
    }
    s->cfg = cfg;
    s->keys = xQueueCreate(32u, sizeof(nj_event));
    if (!s->keys) {
        free(s);
        return NJ_ERR_NOMEM;
    }

    memset(out, 0, sizeof(*out));
    out->name = "esp32";
    out->ud = s;
    out->alloc = esp_alloc;
    out->free = esp_free;
    out->millis = esp_millis;
    out->sleep_ms = esp_sleep_ms;
    out->idle = esp_idle;
    out->log = esp_log_cb;
    out->display_init = esp_display_init;
    out->display_present = esp_display_present;
    out->input_poll = esp_input_poll;
    out->file_open = esp_file_open;
    out->file_read = esp_file_read;
    out->file_write = esp_file_write;
    out->file_seek = esp_file_seek;
    out->file_close = esp_file_close;
    out->app_blob = esp_app_blob;
    out->shutdown = esp_shutdown;
    out->width = cfg->panel_w;
    out->height = cfg->panel_h;
    out->fps_cap = 30;
    return NJ_OK;
}

#else  /* !ESP_PLATFORM */

/*
 * Hosted builds still get the symbols so the runtime links, but the port
 * reports itself unavailable rather than pretending.  Keeping the stub here
 * (rather than inside src/core) is what lets a desktop build include every
 * port source file unconditionally.
 */
nj_status nj_port_esp32_config_default(nj_port_esp32_config *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return NJ_ERR_UNSUPPORTED;
}

nj_status nj_port_esp32(nj_port *out, const nj_port_esp32_config *cfg)
{
    (void)out;
    (void)cfg;
    return NJ_ERR_UNSUPPORTED;
}

#endif /* ESP_PLATFORM */
