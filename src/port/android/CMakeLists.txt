# nano_j2me - Android NDK build.
#
# In your app's build.gradle:
#
#   android {
#       externalNativeBuild { cmake { path "src/main/cpp/CMakeLists.txt" } }
#       defaultConfig { externalNativeBuild { cmake { cppFlags "" } } }
#   }
#
# and add the game to assets/ (it is copied to the app's files directory on
# first launch, because RecordStore needs a writable path next to it).

cmake_minimum_required(VERSION 3.16)
project(nj2me_android C)

set(CMAKE_C_STANDARD 99)

set(NJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

add_library(nj2me SHARED
    ${NJ_ROOT}/src/core/nj_heap.c
    ${NJ_ROOT}/src/core/nj_string.c
    ${NJ_ROOT}/src/core/nj_class.c
    ${NJ_ROOT}/src/core/nj_native.c
    ${NJ_ROOT}/src/core/nj_vm.c
    ${NJ_ROOT}/src/core/nj_thread.c
    ${NJ_ROOT}/src/core/nj_interp.c
    ${NJ_ROOT}/src/core/nj_engine.c
    ${NJ_ROOT}/src/core/nj_app.c
    ${NJ_ROOT}/src/core/nj_builtins.c
    ${NJ_ROOT}/src/core/nj_lib_object.c
    ${NJ_ROOT}/src/core/nj_lib_string.c
    ${NJ_ROOT}/src/core/nj_lib_system.c
    ${NJ_ROOT}/src/core/nj_lib_math.c
    ${NJ_ROOT}/src/core/nj_lib_thread.c
    ${NJ_ROOT}/src/core/nj_lib_boxing.c
    ${NJ_ROOT}/src/midp/nj_lib_util.c
    ${NJ_ROOT}/src/midp/nj_lib_io.c
    ${NJ_ROOT}/src/midp/nj_lib_lcdui.c
    ${NJ_ROOT}/src/midp/nj_lib_game.c
    ${NJ_ROOT}/src/midp/nj_lib_rms.c
    ${NJ_ROOT}/src/midp/nj_lib_midlet.c
    ${NJ_ROOT}/src/midp/nj_lib_media.c
    ${NJ_ROOT}/src/gfx/nj_gfx.c
    ${NJ_ROOT}/src/gfx/nj_font.c
    ${NJ_ROOT}/src/gfx/nj_png.c
    ${NJ_ROOT}/src/gfx/nj_codec.c
    ${NJ_ROOT}/src/zip/nj_inflate.c
    ${NJ_ROOT}/src/zip/nj_zip.c
    ${NJ_ROOT}/src/runner/nj_run.c
    ${NJ_ROOT}/src/port/android/nj_port_android.c
)

target_include_directories(nj2me PRIVATE
    ${NJ_ROOT}/include
    ${NJ_ROOT}/src/core
)
target_compile_options(nj2me PRIVATE -Wall -Wextra -O2)
target_link_libraries(nj2me PRIVATE android log)

# The Java heap goes in the normal native heap; 0 lets NJ_MAX_HEAP decide.
target_compile_definitions(nj2me PRIVATE NJ_MAX_HEAP=8388608)
