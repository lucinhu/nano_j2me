cmake_minimum_required(VERSION 3.16)
project(nano_j2me C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(NJ_BUILD_TESTS   "Build the test suite"          ON)
option(NJ_BUILD_SDL2    "Build the SDL2 desktop port"   ON)
option(NJ_BUILD_HEADLESS "Build the headless port"      ON)
option(NJ_BUILD_TOOLS   "Build developer tools"         ON)
option(NJ_WERROR        "Treat warnings as errors"      OFF)

set(NJ_SOURCES
  src/core/nj_heap.c
  src/core/nj_string.c
  src/core/nj_class.c
  src/core/nj_native.c
  src/core/nj_vm.c
  src/core/nj_thread.c
  src/core/nj_interp.c
  src/core/nj_app.c
  src/core/nj_builtins.c
  src/core/nj_lib_object.c
  src/core/nj_lib_string.c
  src/core/nj_lib_system.c
  src/core/nj_lib_math.c
  src/core/nj_lib_thread.c
  src/core/nj_lib_boxing.c
  src/midp/nj_lib_util.c
  src/midp/nj_lib_io.c
  src/midp/nj_lib_lcdui.c
  src/midp/nj_lib_game.c
  src/midp/nj_lib_rms.c
  src/midp/nj_lib_midlet.c
  src/midp/nj_lib_media.c
  src/gfx/nj_gfx.c
  src/gfx/nj_font.c
  src/gfx/nj_png.c
  src/gfx/nj_codec.c
  src/zip/nj_inflate.c
  src/zip/nj_zip.c
  src/runner/nj_run.c
)

set(NJ_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)


add_library(nj2me STATIC ${NJ_SOURCES})
target_include_directories(nj2me PUBLIC ${NJ_INCLUDE_DIRS})
target_include_directories(nj2me PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_options(nj2me PRIVATE -Wall -Wextra)
if(NJ_WERROR)
  target_compile_options(nj2me PRIVATE -Werror)
endif()
if(UNIX)
  target_link_libraries(nj2me PUBLIC m)
endif()

# ---------------------------------------------------------------------
# Ports
# ---------------------------------------------------------------------
if(NJ_BUILD_HEADLESS)
  target_sources(nj2me PRIVATE src/port/posix/nj_port_headless.c)
endif()

# The embedded ports compile everywhere: outside their own platform they
# provide stubs that report NJ_ERR_UNSUPPORTED, so a single library can contain
# every port and the host build still links.
target_sources(nj2me PRIVATE
  src/port/android/nj_port_android.c
  src/port/esp32/nj_port_esp32.c)

if(NJ_BUILD_SDL2)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
  endif()
  if(SDL2_FOUND)
    target_sources(nj2me PRIVATE src/port/posix/nj_port_sdl2.c)
    target_include_directories(nj2me PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(nj2me PUBLIC ${SDL2_LIBRARIES})
    target_compile_definitions(nj2me PUBLIC NJ_HAVE_SDL2=1)
    message(STATUS "SDL2 port: enabled (${SDL2_VERSION})")
  else()
    message(STATUS "SDL2 port: disabled (libsdl2-dev not found)")
  endif()
endif()

# ---------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------
# NOTE: the executable must not be called "nj2me" - that name is already the
# library target, and CMake rejects the duplicate (CMP0002).
add_executable(nj2me-cli apps/nj2me_main.c)
target_link_libraries(nj2me-cli PRIVATE nj2me)
set_target_properties(nj2me-cli PROPERTIES OUTPUT_NAME nj2me)

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------
if(NJ_BUILD_TESTS)
  enable_testing()
  add_executable(test_vm tests/test_vm.c)
  target_link_libraries(test_vm PRIVATE nj2me)
  add_test(NAME vm_core COMMAND test_vm
           ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hello.jar
           HelloWorld "hello from j2me")

  add_executable(test_lifecycle tests/test_lifecycle.c)
  target_link_libraries(test_lifecycle PRIVATE nj2me)
  add_test(NAME lifecycle COMMAND test_lifecycle)

  add_executable(test_zip tests/test_zip.c)
  target_link_libraries(test_zip PRIVATE nj2me)
  target_include_directories(test_zip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
  add_test(NAME zip_inflate COMMAND test_zip
           ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lcdui.c)
    add_executable(test_lcdui tests/test_lcdui.c)
    target_link_libraries(test_lcdui PRIVATE nj2me)
    target_include_directories(test_lcdui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
    add_test(NAME lcdui COMMAND test_lcdui)
  endif()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lib.c)
    add_executable(test_lib tests/test_lib.c)
    target_link_libraries(test_lib PRIVATE nj2me)
    target_include_directories(test_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
    add_test(NAME classlib COMMAND test_lib)
  endif()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_codec.c)
    add_executable(test_codec tests/test_codec.c)
    target_link_libraries(test_codec PRIVATE nj2me)
    target_include_directories(test_codec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
    add_test(NAME codecs COMMAND test_codec)
  endif()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gfx.c)
    add_executable(test_gfx tests/test_gfx.c src/gfx/nj_gfx.c)
    target_link_libraries(test_gfx PRIVATE nj2me m)
    add_test(NAME rasterizer COMMAND test_gfx)
  endif()
endif()

# ---------------------------------------------------------------------
# Developer tooling
# ---------------------------------------------------------------------
if(NJ_BUILD_TOOLS)
  add_executable(nj_inspect tools/nj_inspect.c)
  target_link_libraries(nj_inspect PRIVATE nj2me)
  target_include_directories(nj_inspect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

  add_custom_target(no-statics
    COMMAND ${CMAKE_COMMAND} -E env
            OBJDIR=$<TARGET_OBJECTS:nj2me>
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_statics.sh
    COMMENT "Verify the runtime keeps no mutable global state")
endif()
