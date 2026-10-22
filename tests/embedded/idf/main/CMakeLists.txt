idf_component_register(SRCS "main.c" "jar.c"
                       INCLUDE_DIRS "."
                       REQUIRES nj2me spiffs esp_driver_uart)

# ---------------------------------------------------------------------------
# UI demo (-DNJ_UI_DEMO=1)
#
# esp-emu models no LCD, no SD card and no GPIO input, so the demo reproduces
# those three software paths instead of the hardware:
#   - display: the port's real 240x320 RGB565 framebuffer is dumped as a
#     run-length encoded stream on UART0 and decoded to PNG by the host;
#   - buttons: a UART line protocol ("K:fire:1") feeds input_poll(), i.e. the
#     same queue GPIO buttons fill on a real board;
#   - storage: the game JAR is baked into a SPIFFS partition mounted at
#     /sdcard.  A real board mounts SDMMC+FAT at the same VFS path, so the
#     firmware's fopen("/sdcard/game.jar") call is the hardware path too.
#
# The JAR is chosen with -DNJ_UI_JAR=/path/to/game.jar
# (default: the keysdemo fixture).
# ---------------------------------------------------------------------------
if(NJ_UI_DEMO)
    target_sources(${COMPONENT_LIB} PRIVATE "ui_demo.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NJ_UI_DEMO=1)

    if(NOT NJ_UI_JAR)
        set(NJ_UI_JAR "${CMAKE_CURRENT_LIST_DIR}/../../../fixtures/keysdemo.jar")
    endif()
    if(NOT EXISTS "${NJ_UI_JAR}")
        message(FATAL_ERROR
            "NJ_UI_JAR does not exist: ${NJ_UI_JAR} (run tools/make_keys_jar.sh)")
    endif()

    # spiffs_create_partition_image takes a directory, so stage the JAR under
    # the name the firmware opens.
    set(NJ_UI_STORAGE_DIR "${CMAKE_BINARY_DIR}/ui_storage")
    file(MAKE_DIRECTORY "${NJ_UI_STORAGE_DIR}")
    configure_file("${NJ_UI_JAR}" "${NJ_UI_STORAGE_DIR}/game.jar" COPYONLY)
    spiffs_create_partition_image(storage "${NJ_UI_STORAGE_DIR}" FLASH_IN_PROJECT)
endif()
