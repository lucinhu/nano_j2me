/*
 * nano_j2me - example ESP32-S3 application.
 *
 * This is a complete main() for a board with an ST7789-class SPI panel, a
 * D-pad on GPIOs, and the game flashed into a "jar" partition.  Copy it into
 * your project's main/ and adjust the pins.
 */
#include "nj2me/nj.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"

static const char *TAG = "nj2me";

void app_main(void)
{
    nj_port_esp32_config cfg;
    nj_port port;

    ESP_LOGI(TAG, "nano_j2me %s", NJ_VERSION_STRING);

    /* RecordStore needs somewhere to live; a SPIFFS partition is enough. */
    {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/storage",
            .partition_label = "storage",
            .max_files = 4,
            .format_if_mount_failed = true
        };
        esp_err_t err = esp_vfs_spiffs_register(&conf);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "no storage partition (%s); RecordStore will not "
                          "persist", esp_err_to_name(err));
        }
    }

    nj_port_esp32_config_default(&cfg);
    cfg.panel_w = NJ_SCREEN_WIDTH;
    cfg.panel_h = NJ_SCREEN_HEIGHT;
    cfg.pin_sclk = 12;
    cfg.pin_mosi = 11;
    cfg.pin_cs   = 10;
    cfg.pin_dc   = 13;
    cfg.pin_rst  = 14;
    cfg.pin_bl   = 21;
    cfg.button_gpio[NJ_KEY_UP]    = 4;
    cfg.button_gpio[NJ_KEY_DOWN]  = 5;
    cfg.button_gpio[NJ_KEY_LEFT]  = 6;
    cfg.button_gpio[NJ_KEY_RIGHT] = 7;
    cfg.button_gpio[NJ_KEY_FIRE]  = 15;
    cfg.button_gpio[NJ_KEY_SOFT_LEFT]  = 16;
    cfg.button_gpio[NJ_KEY_SOFT_RIGHT] = 17;
    cfg.rms_partition = "storage";
    cfg.jar_partition = "jar";

    if (nj_port_esp32(&port, &cfg) != NJ_OK) {
        ESP_LOGE(TAG, "cannot create the port");
        return;
    }
    port.fps_cap = CONFIG_NJ2ME_FPS;

    {
        /*
         * The JAR comes from the "jar" partition via the port's app_blob hook,
         * so no path is needed.  On a board without such a partition, drop the
         * archive into SPIFFS and pass "/storage/game.jar" instead.
         */
        nj_run_opts opts = { 0 };
        opts.heap_bytes = CONFIG_NJ2ME_HEAP_BYTES;
        opts.midlet_class = NULL;      /* take MIDlet-1 from the manifest */

        int rc = nj_run(&port, &opts);
        ESP_LOGI(TAG, "the MIDlet exited with %d", rc);
    }

    /*
     * nj_run released the VM, the port and the archive.  On a device that
     * never reboots this is the point at which a menu would let the user pick
     * another game - the runtime is designed so that this is exactly as clean
     * as starting the first one.
     */
}
