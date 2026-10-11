# nano_j2me as an ESP-IDF component.
#
# Usage, from an idf project:
#   idf.py add-dependency "nano_j2me"      # or drop this tree into components/
#   idf.py menuconfig                      # "nano_j2me" menu: heap size, screen
#   idf.py build flash monitor
#
# The runtime is plain C with no external requirements; the only thing the
# component needs from IDF is the driver stack the port uses.

idf_component_register(
    SRCS
        "../../core/nj_heap.c"
        "../../core/nj_string.c"
        "../../core/nj_class.c"
        "../../core/nj_native.c"
        "../../core/nj_vm.c"
        "../../core/nj_thread.c"
        "../../core/nj_interp.c"
        "../../core/nj_app.c"
        "../../core/nj_builtins.c"
        "../../core/nj_lib_object.c"
        "../../core/nj_lib_string.c"
        "../../core/nj_lib_system.c"
        "../../core/nj_lib_math.c"
        "../../core/nj_lib_thread.c"
        "../../core/nj_lib_boxing.c"
        "../../midp/nj_lib_util.c"
        "../../midp/nj_lib_io.c"
        "../../midp/nj_lib_lcdui.c"
        "../../midp/nj_lib_game.c"
        "../../midp/nj_lib_rms.c"
        "../../midp/nj_lib_midlet.c"
        "../../midp/nj_lib_media.c"
        "../../gfx/nj_gfx.c"
        "../../gfx/nj_font.c"
        "../../gfx/nj_png.c"
        "../../gfx/nj_jpeg.c"
        "../../gfx/nj_codec.c"
        "../../zip/nj_inflate.c"
        "../../zip/nj_zip.c"
        "../../runner/nj_run.c"
        "nj_port_esp32.c"
    INCLUDE_DIRS
        "../../../include"
        "../../core"
    REQUIRES
        driver esp_timer esp_partition spi_flash
    PRIV_REQUIRES
        freertos
)

# The runtime is C99 and warning-clean; keep it that way on the target too.
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c99 -Wall -Wextra)

# Heap profile from Kconfig.  The runtime always allocates the arena through
# the port's alloc hook (which prefers PSRAM); this only fixes the default used
# when the embedder passes heap_bytes == 0.
if(CONFIG_NJ2ME_HEAP_TINY)
    set(NJ2ME_DEFAULT_HEAP 262144)
elseif(CONFIG_NJ2ME_HEAP_SMALL)
    set(NJ2ME_DEFAULT_HEAP 524288)
else()
    set(NJ2ME_DEFAULT_HEAP 6291456)
endif()

if(CONFIG_NJ2ME_HEAP_BYTES GREATER 0)
    set(NJ2ME_DEFAULT_HEAP ${CONFIG_NJ2ME_HEAP_BYTES})
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    NJ_MAX_HEAP=${NJ2ME_DEFAULT_HEAP}
    NJ_SCREEN_WIDTH=${CONFIG_NJ2ME_SCREEN_WIDTH}
    NJ_SCREEN_HEIGHT=${CONFIG_NJ2ME_SCREEN_HEIGHT}
)

if(CONFIG_NJ2ME_TRACE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC NJ_ENABLE_DEBUG=1)
endif()
