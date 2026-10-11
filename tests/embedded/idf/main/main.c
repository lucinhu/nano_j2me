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
#define NJ_QEMU_HEAP (224u * 1024u)

static unsigned char g_heap[NJ_QEMU_HEAP];
static unsigned char g_port_state[28u * 1024u];

void app_main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_ref midlet = 0;
    int rc = 1;

    ESP_LOGI(TAG, "nano_j2me on %s, heap %u KiB, free internal %u KiB",
             CONFIG_IDF_TARGET, (unsigned)(NJ_QEMU_HEAP / 1024u),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u));

    ESP_LOGI(TAG, "java heap %u KiB, port state %u KiB, free internal %u KiB",
             (unsigned)(NJ_QEMU_HEAP / 1024u),
             (unsigned)(sizeof(g_port_state) / 1024u),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u));

    if (nj_port_bare(&port, (bare_state *)g_port_state,
                     g_heap, NJ_QEMU_HEAP) != NJ_OK) {
        ESP_LOGE(TAG, "port init failed");
        return;
    }

    /* The port needs slack beyond the arena it hands out. */
    /* The arena covers the VM instance AND the Java heap, which is why
     * nj_vm_sizeof() is public: an embedder sizing a static arena needs both
     * numbers. */
    ESP_LOGI(TAG, "sizeof(nj_vm)=%u, arena %u KiB", (unsigned)nj_vm_sizeof(),
             (unsigned)((NJ_QEMU_HEAP - nj_vm_sizeof() - 8192u) / 1024u));
    vm = nj_vm_create(&port, NJ_QEMU_HEAP - nj_vm_sizeof() - 8192u);
    if (!vm) {
        ESP_LOGE(TAG, "nj_vm_create failed");
        return;
    }

    app = nj_app_open(vm, nj_bare_jar, nj_bare_jar_len);
    if (app && nj_vm_new_midlet(vm, 0, &midlet) == NJ_OK &&
        nj_vm_start_app(vm, midlet) == NJ_OK) {
        nj_vm_stats st0, st1;
        while (nj_vm_running(vm) && nj_port_bare_frames(port.ud) < 600) {
            if (nj_midp_frame(vm) != NJ_OK) {
                break;
            }
        }
        nj_vm_get_stats(vm, &st0);
        rc = (int)nj_vm_exit_code(vm);

        /* The numbers that make this a measurement rather than a smoke test. */
        ESP_LOGI(TAG, "frames=%lu bytecodes=%lu natives=%lu heap=%lu/%lu",
                 (unsigned long)st0.frames, (unsigned long)st0.insns,
                 (unsigned long)st0.native_calls,
                 (unsigned long)st0.heap_used, (unsigned long)st0.heap_peak);
        (void)st1;
    } else {
        ESP_LOGE(TAG, "could not start the MIDlet");
    }

    nj_app_close(vm, app);
    (void)nj_vm_destroy(vm);
    nj_port_bare_report(rc, nj_port_bare_frames(port.ud));
    ESP_LOGI(TAG, "done rc=%d", rc);
}
