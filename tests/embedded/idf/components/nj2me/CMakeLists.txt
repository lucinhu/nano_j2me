# The runtime, as an ESP-IDF component.
#
# The source list is written out rather than globbed so that adding a module to
# the runtime without adding it here is a build error rather than a silent
# omission - which is how a port ends up missing a class library and failing at
# run time instead of at compile time.
set(NJ_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

idf_component_register(
    SRCS
        "${NJ_ROOT}/src/core/nj_heap.c"
        "${NJ_ROOT}/src/core/nj_string.c"
        "${NJ_ROOT}/src/core/nj_class.c"
        "${NJ_ROOT}/src/core/nj_native.c"
        "${NJ_ROOT}/src/core/nj_vm.c"
        "${NJ_ROOT}/src/core/nj_thread.c"
        "${NJ_ROOT}/src/core/nj_interp.c"
        "${NJ_ROOT}/src/core/nj_app.c"
        "${NJ_ROOT}/src/core/nj_builtins.c"
        "${NJ_ROOT}/src/core/nj_lib_object.c"
        "${NJ_ROOT}/src/core/nj_lib_string.c"
        "${NJ_ROOT}/src/core/nj_lib_system.c"
        "${NJ_ROOT}/src/core/nj_lib_math.c"
        "${NJ_ROOT}/src/core/nj_lib_thread.c"
        "${NJ_ROOT}/src/core/nj_lib_boxing.c"
        "${NJ_ROOT}/src/midp/nj_lib_util.c"
        "${NJ_ROOT}/src/midp/nj_lib_io.c"
        "${NJ_ROOT}/src/midp/nj_lib_lcdui.c"
        "${NJ_ROOT}/src/midp/nj_lib_game.c"
        "${NJ_ROOT}/src/midp/nj_lib_rms.c"
        "${NJ_ROOT}/src/midp/nj_lib_midlet.c"
        "${NJ_ROOT}/src/midp/nj_lib_media.c"
        "${NJ_ROOT}/src/gfx/nj_gfx.c"
        "${NJ_ROOT}/src/gfx/nj_text.c"
        "${NJ_ROOT}/src/gfx/nj_font.c"
        "${NJ_ROOT}/src/gfx/nj_png.c"
        "${NJ_ROOT}/src/gfx/nj_jpeg.c"
        "${NJ_ROOT}/src/gfx/nj_codec.c"
        "${NJ_ROOT}/src/zip/nj_inflate.c"
        "${NJ_ROOT}/src/zip/nj_zip.c"
        "${NJ_ROOT}/src/port/esp32/nj_port_esp32.c"
        # The bare port, for the QEMU image.  nj_port_esp32.c above is compiled
        # too, so it is checked against the real IDF headers either way.
        "${NJ_ROOT}/tests/embedded/nj_port_bare.c"
    INCLUDE_DIRS
        "${NJ_ROOT}/include"
        "${NJ_ROOT}/src/core"
        "${NJ_ROOT}/src/midp"
        "${NJ_ROOT}/src/gfx"
        "${NJ_ROOT}/src/zip"
    PRIV_INCLUDE_DIRS
        "${NJ_ROOT}/src/core"
    REQUIRES
        esp_driver_gpio esp_driver_spi esp_driver_i2s esp_timer esp_partition
        driver esp_common
)
