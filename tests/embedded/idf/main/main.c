/*
 * nano_j2me on an ESP32, under ESP-IDF.
 *
 * This is the image that runs on the ESP32-S3 under Espressif's emulator
 * (esp-emu), and - with PSRAM switched off - under QEMU's esp32s3 machine.  It
 * uses the bare-metal port from tests/embedded/ rather than nj_port_esp32.c on
 * purpose:
 *
 *   - nj_port_esp32.c drives an SPI panel over GPIO.  Neither emulator models
 *     the panel nor the wiring, so booting it there would prove nothing about
 *     it; it IS compiled against the real IDF headers as part of this
 *     component, which is a stronger check than a stub-header compile.
 *   - the bare port needs nothing but a framebuffer in RAM, so what the
 *     emulator exercises is exactly the part worth exercising: the
 *     interpreter, the class library, the scheduler and the GC, on the real
 *     instruction set.
 *
 * The board profile is the one in sdkconfig.defaults: 16 MB flash, 8 MB PSRAM.
 * The output goes to UART0, which is the emulator's console.
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
 * The framebuffer stays in internal SRAM and small.
 *
 * nj_port_esp32.c DMAs a panel out of internal RAM; this image only needs a
 * scratch surface, so 64x64 (16 KB) is enough to prove the runtime renders.
 */
/*
 * The pool comes from IDF's allocator, in PSRAM, 16-byte aligned.
 *
 * It is not a static array because it must be three things at once:
 *   - PSRAM, because internal SRAM cannot hold the Java heap;
 *   - 16-byte aligned, which the Java arena requires for long/double fields
 *     and which nj_heap_init() can only reject - heap_caps_aligned_alloc()
 *     guarantees it rather than hoping the linker agreed;
 *   - enough room for all three consumers (VM instance, Java arena, statics),
 *     which a hand-sized array got wrong.
 */
/*
 * The pool is sized from what is actually free, and it prefers PSRAM.
 *
 * This board has 8 MB of PSRAM; the desktop hands nj_vm_create a 16 MB arena,
 * so 6 MB of PSRAM is the equivalent here and leaves 2 MB to IDF.  Sizing the
 * pool from internal RAM instead fails on the full class library: the 308 KiB
 * it produced could not create ~120 built-in classes ("cannot create builtin
 * class javax/microedition/media/Player" ...) before the main thread gave up,
 * which reads as a class-library bug and is really an out-of-memory one.
 *
 * Without PSRAM (an ESP32-C3, or QEMU's esp32s3, which cannot probe PSRAM) fall
 * back to internal RAM: 340 KiB fits an S3's 356 KiB free, less a margin for
 * IDF's own allocations.  The two parts have different SRAM, so the constant is
 * a cap rather than the size.
 */
#define NJ_EMU_PSRAM_RESERVE (1u * 1024u * 1024u)
#define NJ_EMU_PSRAM_POOL_MAX (6u * 1024u * 1024u)
#define NJ_EMU_POOL_MARGIN   (48u * 1024u)
#define NJ_EMU_HEAP_MAX      (340u * 1024u)

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
#if defined(NJ_UI_DEMO)
    /* The emulator UI demo (display capture, injected keys, JAR from a
     * simulated memory card): all of it lives in ui_demo.c, and it needs its
     * own frame loop, so it takes over app_main completely. */
    extern void ui_demo_main(void);
    ui_demo_main();
    return;
#endif
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_ref midlet = 0;
    int rc = 1;
    int started = 0;
    size_t pool = 0u;
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

    ESP_LOGI(TAG, "nano_j2me on %s, free: psram %u KiB, internal %u KiB",
             CONFIG_IDF_TARGET,
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

    /*
     * The pool handed to nj_vm_create has to cover THREE things, not one:
     * the VM instance (nj_vm_sizeof), the Java arena (this argument), and
     * vm->statics - the interned-string and constant tables.  Sizing the pool
     * as "sizeof(nj_vm) + java heap" leaves nothing for the third and fails
     * with "statics failed", which is a puzzle the first time and obvious
     * afterwards.
     */
    {
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const char *where;

        if (free_psram > NJ_EMU_PSRAM_RESERVE) {
            pool = (size_t)(free_psram - NJ_EMU_PSRAM_RESERVE);
            if (pool > NJ_EMU_PSRAM_POOL_MAX) { pool = NJ_EMU_PSRAM_POOL_MAX; }
            where = "psram";
        } else {
            pool = (free_internal > NJ_EMU_POOL_MARGIN)
                       ? (size_t)(free_internal - NJ_EMU_POOL_MARGIN) : 0u;
            if (pool > NJ_EMU_HEAP_MAX) { pool = NJ_EMU_HEAP_MAX; }
            where = "internal";
        }
        pool &= ~(size_t)4095u;
        ESP_LOGI(TAG, "pool %u KiB (%s)", (unsigned)(pool / 1024u), where);
    }

    /* Half the pool for the statics, the native registry and the interned
     * strings (a few hundred KiB with the full class library), half for the
     * Java arena.  With PSRAM the arena is megabytes, which is what the full
     * class library needs to materialise; the internal-RAM fallback keeps the
     * old split. */
    {
        nj_size arena = pool / 2u;
        ESP_LOGI(TAG, "sizeof(nj_vm)=%u, arena %u KiB, reserved %u KiB",
                 (unsigned)nj_vm_sizeof(), (unsigned)(arena / 1024u),
                 (unsigned)((pool - arena) / 1024u));
        vm = nj_vm_create(&port, arena);
    }
    if (!vm) {
        ESP_LOGE(TAG, "nj_vm_create failed");
        nj_port_bare_report(1, 0);
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

    if (app) {
        nj_app_close(vm, app);
    }
    (void)nj_vm_destroy(vm);
    nj_port_bare_report(rc, nj_port_bare_frames(port.ud));
    ESP_LOGI(TAG, "done rc=%d", rc);
}
