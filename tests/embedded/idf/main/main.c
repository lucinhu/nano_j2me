/*
 * nano_j2me on an ESP32, under ESP-IDF.
 *
 * This is the image that runs in QEMU's esp32s3 and esp32c3 machines.  It uses
 * the bare-metal port from tests/embedded/ rather than nj_port_esp32.c on
 * purpose:
 *
 *   - nj_port_esp32.c drives an SPI panel over GPIO.  QEMU models neither the
 *     panel nor the wiring, so booting it there would prove nothing about it;
 *     it IS compiled against the real IDF headers as part of this component,
 *     which is a stronger check than a stub-header compile.
 *   - the bare port needs nothing but a framebuffer in RAM, so what QEMU
 *     exercises is exactly the part worth exercising: the interpreter, the
 *     class library, the scheduler and the GC, on the real instruction set.
 *
 * The output goes to UART0, which is the console QEMU is attached to.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_app.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

typedef struct bare_state bare_state;
nj_status nj_port_bare(nj_port *out, bare_state *st, void *heap, nj_size heap_bytes);
int  nj_port_bare_frames(const void *ud);
void nj_port_bare_report(int rc, int frames);

extern const unsigned char nj_bare_jar[];
extern const unsigned int  nj_bare_jar_len;

static const char *TAG = "nj2me";

/*
 * Everything from internal SRAM, and small.
 *
 * On a real board the Java heap belongs in PSRAM and the framebuffer belongs in
 * a panel that DMAs from internal RAM - nj_port_esp32.c does both.  This image
 * is not that: it is here to prove the RUNTIME executes on the ESP32-S3's real
 * instruction set, so the framebuffer is 64x64 (16 KB) and both buffers are
 * sized to leave headroom in the 512 KB the part actually has.
 */
/*
 * The pool comes from IDF's allocator, in PSRAM, 16-byte aligned.
 *
 * three things at once, which is why it is not a static array:
 *   - PSRAM, because internal SRAM cannot hold it (nj_port_esp32.c does the
 *     same on real hardware, and this is the same fix);
 *   - 16-byte alignment, which the Java arena requires for long/double fields
 *     and which nj_heap_init() can only reject - heap_caps_aligned_alloc()
 *     guarantees it rather than hoping the linker agreed;
 *   - enough room for all three consumers, which a hand-sized array got wrong.
 */
#define NJ_QEMU_HEAP (340u * 1024u)

static unsigned char g_port_state[28u * 1024u];

/* The port's allocator, backed by IDF with PSRAM preferred and internal RAM as
 * the fallback for a board without it. */
static void *idf_alloc(void *ud, nj_size n)
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

static void idf_free(void *ud, void *p)
{
    (void)ud;
    heap_caps_free(p);
}

void app_main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_ref midlet = 0;
    int rc = 1;
    int started = 0;
#define SNAP(what)                                                        \
    do {                                                                  \
        nj_vm_stats ss;                                                   \
        if (vm) {                                                         \
            nj_vm_get_stats(vm, &ss);                                     \
            ESP_LOGI(TAG, "  after %-16s classes=%u heap=%lu/%lu", what,  \
                     (unsigned)ss.classes, (unsigned long)ss.heap_used,   \
                     (unsigned long)ss.heap_peak);                        \
        }                                                                 \
    } while (0)

    ESP_LOGI(TAG, "nano_j2me on %s, heap %u KiB, free internal %u KiB",
             CONFIG_IDF_TARGET, (unsigned)(NJ_QEMU_HEAP / 1024u),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u));

    ESP_LOGI(TAG, "pool %u KiB; free: psram %u KiB, internal %u KiB",
             (unsigned)(NJ_QEMU_HEAP / 1024u),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024u),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u));

    if (nj_port_bare(&port, (bare_state *)g_port_state,
                     NULL, 0u) != NJ_OK) {
        ESP_LOGE(TAG, "port init failed");
        return;
    }
    /* Hand the runtime IDF's allocator instead of the port's static bump
     * allocator: this build has a real heap manager, and nj_port_bare.c stays
     * free of IDF dependencies for the bare-metal image. */
    port.alloc = idf_alloc;
    port.free = idf_free;

    /* The port needs slack beyond the arena it hands out. */

    ESP_LOGI(TAG, "port.alloc=%p port.ud=%p display_init=%p",
             (void *)(size_t)port.alloc, port.ud,
             (void *)(size_t)port.display_init);
    /*
     * The pool handed to nj_vm_create has to cover THREE things, not one:
     * the VM instance (nj_vm_sizeof), the Java arena (this argument), and
     * vm->statics - the interned-string and constant tables, which want about
     * 32 KiB.  Sizing the pool as "sizeof(nj_vm) + java heap" leaves nothing
     * for the third and fails with "statics failed", which is a puzzle the
     * first time and obvious afterwards.
     */
    ESP_LOGI(TAG, "sizeof(nj_vm)=%u, arena %u KiB", (unsigned)nj_vm_sizeof(),
             (unsigned)((NJ_QEMU_HEAP - nj_vm_sizeof() - 200u * 1024u) / 1024u));
    vm = nj_vm_create(&port, NJ_QEMU_HEAP - nj_vm_sizeof() - 200u * 1024u);
    if (!vm) {
        ESP_LOGE(TAG, "nj_vm_create failed");
        return;
    }

    SNAP("nj_vm_create");
    app = nj_app_open(vm, nj_bare_jar, nj_bare_jar_len);
    SNAP("nj_app_open");
    if (!app) {
        ESP_LOGE(TAG, "nj_app_open failed: %s",
                 nj_exception_describe(vm) ? nj_exception_describe(vm) : "?");
    } else if (!(midlet = nj_vm_new_midlet(vm, nj_app_midlet_class(app, 0),
                                           NULL))) {
        /* Two mistakes cancelled each other here for several rounds:
         * nj_vm_new_midlet() takes a CLASS NAME and returns a REF, and the
         * name has to come from the manifest - there is no "pass NULL and it
         * picks one for you".  Passing NULL returns NULL, and comparing that
         * pointer against NJ_OK (which is 0) reads as success.  So the MIDlet
         * was NULL, nj_vm_start_app() returned NJ_ERR on its first line, and
         * the log said "startApp failed" about a MIDlet that had never been
         * created. */
        ESP_LOGE(TAG, "nj_vm_new_midlet(%s) failed: %s",
                 nj_app_midlet_class(app, 0) ? nj_app_midlet_class(app, 0) : "?",
                 nj_exception_describe(vm) ? nj_exception_describe(vm) : "?");
    } else {
        nj_status st;
        SNAP("nj_vm_new_midlet");
        st = nj_vm_start_app(vm, midlet);
        if (st != NJ_OK) {
            /* The STATUS, not just "failed": a call that returns an error
             * without an exception is a different bug from one that throws, and
             * the number is the only thing that distinguishes them. */
            ESP_LOGE(TAG, "nj_vm_start_app failed: status %d, exception %s",
                     (int)st,
                     nj_exception_describe(vm) ? nj_exception_describe(vm)
                                               : "(none)");
        } else {
            started = 1;
        }
    }
    if (0) {
    } else {
        started = 1;
    }
    if (started) {
        nj_vm_stats st0;
        while (nj_vm_running(vm) && nj_port_bare_frames(port.ud) < 600) {
            if (nj_midp_frame(vm) != NJ_OK) {
                break;
            }
        }
        nj_vm_get_stats(vm, &st0);
        rc = (int)nj_vm_exit_code(vm);
        ESP_LOGI(TAG, "nb %lu", (unsigned long)st0.native_calls);

        /* The numbers that make this a measurement rather than a smoke test. */
        ESP_LOGI(TAG, "frames=%lu bytecodes=%lu natives=%lu heap=%lu/%lu",
                 (unsigned long)st0.frames, (unsigned long)st0.insns,
                 (unsigned long)st0.native_calls,
                 (unsigned long)st0.heap_used, (unsigned long)st0.heap_peak);
    }

    nj_app_close(vm, app);
    (void)nj_vm_destroy(vm);
    nj_port_bare_report(rc, nj_port_bare_frames(port.ud));
    ESP_LOGI(TAG, "done rc=%d", rc);
}
