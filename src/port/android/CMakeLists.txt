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

# Keep the same runtime source set as tools/build_android.sh.  All native
# library tables and optional backends must be present; nj_config.h controls
# which features contribute code.  Reconfigure when a runtime source is added.
file(GLOB NJ_ANDROID_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${NJ_ROOT}/src/core/*.c"
    "${NJ_ROOT}/src/midp/*.c"
    "${NJ_ROOT}/src/gfx/*.c"
    "${NJ_ROOT}/src/zip/*.c"
)
add_library(nj2me SHARED
    ${NJ_ANDROID_RUNTIME_SOURCES}
    ${NJ_ROOT}/src/runner/nj_run.c
    ${NJ_ROOT}/src/port/android/nj_port_android.c
)

target_include_directories(nj2me PRIVATE
    ${NJ_ROOT}/include
    ${NJ_ROOT}/src/core
    ${NJ_ROOT}/src/midp
    ${NJ_ROOT}/src/gfx
    ${NJ_ROOT}/src/zip
)
target_compile_options(nj2me PRIVATE -Wall -Wextra -O2)
# Fail here if a runtime module is missing, rather than at System.loadLibrary.
target_link_options(nj2me PRIVATE -Wl,--no-undefined)
target_link_libraries(nj2me PRIVATE android log OpenSLES z m dl)

# The Java heap goes in the normal native heap; 0 lets NJ_MAX_HEAP decide.
target_compile_definitions(nj2me PRIVATE NJ_MAX_HEAP=8388608)
