# =====================================================================
#  nano_j2me - build
#
#  Build:
#
#      cmake -S . -B build -G Ninja
#      cmake --build build                    # compiles; nothing else
#
#  Test (the fixture step is separate on purpose - see NJ_GENERATE_FIXTURES):
#
#      cmake --build build --target nj_fixtures   # once, needs a JDK + Pillow
#      ctest --test-dir build --output-on-failure
#
#  Everything below is either that default or a documented opt-out.  The
#  runtime has no dependencies; SDL2 is the only thing even looked for, and
#  its absence only disables the desktop window (the headless port is always
#  built and is what the tests use).
# =====================================================================
cmake_minimum_required(VERSION 3.16)
project(nano_j2me VERSION 0.5.3 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
               Debug Release RelWithDebInfo MinSizeRel)
endif()

include(GNUInstallDirs)

# ---------------------------------------------------------------------
# Options.  All default to "the useful thing".
# ---------------------------------------------------------------------
option(NJ_BUILD_TESTS    "Build the test suite"             ON)
option(NJ_GENERATE_FIXTURES
       "Generate test fixtures as part of the build.  OFF by default: they are
test DATA, they need a JDK and Pillow, and generating them on every build turns
'cmake --build' into a multi-second job that has nothing to do with compiling.
Run it once with: cmake --build build --target fixtures" OFF)
option(NJ_FIXTURES_FETCH_GAMES
       "Let the fixture step download third-party games from the network.  OFF
by default because a build should not depend on GitHub being reachable; the
tests that need those titles skip themselves when they are absent." OFF)
option(NJ_BUILD_CLI      "Build the command-line runner"    ON)
option(NJ_BUILD_LIBRETRO "Build the libretro core for RetroArch" ON)
option(NJ_BUILD_SDL2     "Build the SDL2 desktop port"      ON)
option(NJ_BUILD_EMBEDDED_PORTS
       "Also compile the Android and ESP32 port sources (they stub themselves \
outside their own platform, so this costs a compile and nothing else)" ON)
option(NJ_WERROR         "Treat warnings as errors"         OFF)
option(NJ_SANITIZE       "Build with ASan + UBSan"          OFF)

set(NJ_ENABLE_JPEG  1 CACHE STRING "Built-in baseline JPEG decoder")
set(NJ_ENABLE_TTF   1 CACHE STRING "TrueType text (0 = bitmap faces only)")
set(NJ_ENABLE_FONT_CJK 1 CACHE STRING "Built-in monochrome CJK faces (0 = MIDP cell font only)")
set(NJ_ENABLE_FONT_CJK_AA 1 CACHE STRING "Four-level built-in CJK and Latin coverage (0 = monochrome)")
set(NJ_ENABLE_PNG   1 CACHE STRING "Built-in PNG decoder")
# GIF is a MIDP 1.0 requirement and 34 corpus titles ship one, but a port whose
# platform decodes it (Android's BitmapFactory) can leave it out.
set(NJ_ENABLE_GIF   1 CACHE STRING "Built-in GIF decoder")
# DEFLATE: every compressed JAR entry and every PNG IDAT goes through it.  A
# port with zlib of its own implements nj_port.inflate_raw/nj_port.inflate and
# turns this off; a build with it off can still open a JAR of stored entries.
set(NJ_ENABLE_INFLATE 1 CACHE STRING "Built-in DEFLATE decompressor (0 = port hooks only)")
# The RIFF/WAVE reader.  With it off a WAV goes to nj_port.audio_decode (the
# same hook AMR and MP3 already use) and is refused by name otherwise.
set(NJ_ENABLE_WAV   1 CACHE STRING "Built-in PCM WAV reader (0 = platform codec)")
set(NJ_ENABLE_RMS   1 CACHE STRING "RecordStore persistence")
set(NJ_ENABLE_IO    1 CACHE STRING "java.io and javax.microedition.io")
set(NJ_ENABLE_STREAM_UTF8 1 CACHE STRING "UTF-8/ASCII character streams (one state int per reader/writer)")
set(NJ_ENABLE_SAX   1 CACHE STRING "Streaming non-validating SAX XML reader")
set(NJ_ENABLE_AMR   1 CACHE STRING "Built-in AMR narrowband speech decoder")
# The built-in MIDI renderer.  A port whose platform plays MIDI itself (Android
# hands the file to the system synthesizer) can drop it: what is lost is the
# fallback for a device without that synthesizer, and MIDIControl's single note
# events, which a platform player cannot be sent.
set(NJ_ENABLE_MIDI  1 CACHE STRING "Built-in MIDI renderer (0 = platform only)")
set(NJ_ENABLE_MP3  1 CACHE STRING "Built-in MP3 (MPEG Layer III) decoder")
set(NJ_ENABLE_VIDEO 1 CACHE STRING "Built-in video (H.263/MPEG-4) decoder")
# The two video codecs can also be cut one at a time; both follow NJ_ENABLE_VIDEO
# unless named here, so -DNJ_ENABLE_VIDEO=0 still means "no video at all".
set(NJ_ENABLE_H263 ${NJ_ENABLE_VIDEO} CACHE STRING "Built-in H.263 decoder")
set(NJ_ENABLE_M4V ${NJ_ENABLE_VIDEO} CACHE STRING "Built-in MPEG-4 Part 2 decoder")
# Floating point.  This switch was documented in nj_config.h from the start and
# broken in TWO independent ways: nothing in src/ read it, and CMake never
# passed it to the compiler at all - so -DNJ_ENABLE_FLOAT=0 built a runtime that
# still linked every libm transcendental.  A configuration option that lies is
# worse than one that is missing.
set(NJ_ENABLE_M3G 1 CACHE STRING "Build JSR 184 (Mobile 3D Graphics)")
set(NJ_M3G_FAST_PATHS 1 CACHE STRING "Specialize common M3G raster/clip paths (no persistent RAM)")
set(NJ_ENABLE_MASCOT 1 CACHE STRING "Build Mascot Capsule Micro3D v3")
set(NJ_ENABLE_FLOAT 1 CACHE STRING "libm transcendentals for java.lang.Math")
# The rest of the switches nj_config.h documents.  These used to be reachable
# only by putting -D on CMAKE_C_FLAGS, which then collides with the definitions
# below under -Werror ("macro redefined") - so a trimmed build was a patch
# rather than a configuration.  Defaults match the header.
set(NJ_ENABLE_LONG 1 CACHE STRING "java.lang.Long arithmetic (CLDC 1.1)")
set(NJ_ENABLE_EXCEPTION_MESSAGES 1 CACHE STRING "Exception messages and stack traces")
set(NJ_ENABLE_LC_DUPLI 1 CACHE STRING "The duplicated lcdui classes")
set(NJ_ENABLE_GAME_PACKAGE 1 CACHE STRING "javax.microedition.lcdui.game")
set(NJ_ENABLE_MEDIA 0 CACHE STRING "javax.microedition.media tone player")
set(NJ_PIXFMT       "NJ_PIXFMT_RGB565" CACHE STRING
    "Framebuffer pixel format: NJ_PIXFMT_RGB565, NJ_PIXFMT_RGB888 or NJ_PIXFMT_ARGB8888")
# Interpreter fast paths.  These trade code size for dispatch work, not RAM;
# see the long comment in include/nj2me/nj_config.h.  The threaded-dispatch
# switch selects the replicated (computed-goto) interpreter body, which is the
# same 256 opcodes implemented a second time and must match the switch
# interpreter bytecode for bytecode (tools/check_threaded_dispatch.sh).
set(NJ_THREADED_DISPATCH 0 CACHE STRING "Use the computed-goto interpreter body (0 = portable switch)")
set(NJ_ENABLE_SUPERINSTR 1 CACHE STRING "Run fused bytecode pairs in one dispatch (no RAM cost)")
set(NJ_ENABLE_JUMP_THREADING 1 CACHE STRING "Follow goto chains without re-dispatching")
set(NJ_ENABLE_ENGINE 0 CACHE STRING "Pluggable execution engine interface")
set(NJ_ENABLE_JIT 0 CACHE STRING "Enable the RV32/ARM64 baseline JIT (implies engine interface)")
if(NOT NJ_ENABLE_JIT MATCHES "^[01]$")
  message(FATAL_ERROR "NJ_ENABLE_JIT must be 0 or 1")
endif()
if(NJ_ENABLE_JIT)
  set(NJ_ENABLE_ENGINE 1)
endif()
foreach(ext M A F D C)
  set(NJ_JIT_RV32_${ext} "AUTO" CACHE STRING "Generated RV32 ${ext} instructions: AUTO, 0, or 1")
  set_property(CACHE NJ_JIT_RV32_${ext} PROPERTY STRINGS AUTO 0 1)
  if(NOT NJ_JIT_RV32_${ext} MATCHES "^(AUTO|0|1)$")
    message(FATAL_ERROR "NJ_JIT_RV32_${ext} must be AUTO, 0, or 1")
  endif()
endforeach()
foreach(ext FP SIMD LSE)
  set(NJ_JIT_ARM64_${ext} "AUTO" CACHE STRING "Generated ARM64 ${ext} instructions: AUTO, 0, or 1")
  set_property(CACHE NJ_JIT_ARM64_${ext} PROPERTY STRINGS AUTO 0 1)
  if(NOT NJ_JIT_ARM64_${ext} MATCHES "^(AUTO|0|1)$")
    message(FATAL_ERROR "NJ_JIT_ARM64_${ext} must be AUTO, 0, or 1")
  endif()
endforeach()
set(NJ_JIT_CACHE_ENTRIES 64 CACHE STRING "Per-VM JIT code cache slots")
set(NJ_JIT_CACHE_WAYS 2 CACHE STRING "JIT code cache associativity: 1, 2, or 4")
set(NJ_JIT_METHOD_ENTRIES 32 CACHE STRING "Per-VM JIT probe maps; LRU recycled")
set(NJ_JIT_METHOD_ARENA_BYTES 4096 CACHE STRING "Fixed JIT probe-map arena in bytes")
set(NJ_JIT_MAX_FAULTS 12 CACHE STRING "Guarded instructions recorded per native block")
set(NJ_JIT_MAX_EXITS 4 CACHE STRING "Native block exits (window end, branches, loop budget)")
set(NJ_JIT_MIN_BLOCK_INSNS 4 CACHE STRING "Minimum bytecodes worth compiling")
set(NJ_JIT_STATS 1 CACHE STRING "Collect batched JIT execution counters")
set(NJ_JIT_DETAILED_STATS 0 CACHE STRING "Collect probe reasons and execution-weighted block lengths")
set(NJ_JIT_RV32_REG_CACHE 1 CACHE STRING "Reuse RV32 temporary register values within leaf blocks")
set(NJ_JIT_HOT_THRESHOLD 8 CACHE STRING "Hits before compiling a bytecode block")
set(NJ_JIT_MAX_BLOCK_INSNS 64 CACHE STRING "Maximum bytecodes per native block")
set(NJ_JIT_MAX_CODE_BYTES 2048 CACHE STRING "Maximum native bytes per block")
set(NJ_ENABLE_HOT_FIELDS 1 CACHE STRING "Memoise hot Midp field lookups (+192 B per VM for 24 slots)")
set(NJ_MASCOT_RASTER_SPANS 1 CACHE STRING "Specialised Mascot rasteriser span loops (+10 KB code)")
set(NJ_MASCOT_FAST_FIELDS 1 CACHE STRING "Use Micro3D receiver classes directly for field access (no cache)")
set(NJ_MASCOT_MAX_QUEUED_TRIANGLES 16384 CACHE STRING "Maximum deferred Micro3D primitives per Graphics3D")
# The class/field lookup memo.  Entries are one pointer each, so the two
# tables cost (NJ_FIELD_CACHE_SLOTS + NJ_CLASS_CACHE_SLOTS) * sizeof(void *)
# per VM - 9,232 B at the defaults on a 64-bit build, which is real RAM on an
# ESP32.  Both must be powers of two.  See docs/notes/2026-10-31-lookup-memo.md
# for the hit rates behind the defaults.
set(NJ_FIELD_CACHE_SLOTS 1024 CACHE STRING "Entries in the nj_field_lookup() memo (power of two)")
set(NJ_CLASS_CACHE_SLOTS 128 CACHE STRING "Entries in the nj_class_lookup() memo (power of two)")

# ---------------------------------------------------------------------
# The runtime
# ---------------------------------------------------------------------
set(NJ_SOURCES
  # ---- virtual machine ----
  src/core/nj_heap.c
  src/core/nj_string.c
  src/core/nj_class.c
  src/core/nj_native.c
  src/core/nj_vm.c
  src/core/nj_thread.c
  src/core/nj_interp.c
  src/core/nj_engine.c
  src/core/nj_avcodec.c
  src/core/nj_jit.c
  src/core/nj_jit_rv32.c
  src/core/nj_jit_arm64.c
  src/core/nj_app.c
  src/core/nj_builtins.c
  src/core/nj_device.c
  src/core/nj_detect.c
  src/core/nj_save.c
  # ---- java.lang, in C ----
  src/core/nj_lib_object.c
  src/core/nj_lib_string.c
  src/core/nj_lib_system.c
  src/core/nj_lib_math.c
  src/core/nj_lib_thread.c
  src/core/nj_lib_boxing.c
  # ---- the MIDP class library, in C ----
  src/midp/nj_lib_util.c
  src/midp/nj_lib_io.c
  src/midp/nj_lib_http.c
  src/midp/nj_lib_pim.c
  src/midp/nj_lib_samsung.c
  src/midp/nj_lib_socket.c
  src/midp/nj_lib_datagram.c
  src/midp/nj_lib_sensor.c
  src/midp/nj_lib_bt.c
  src/midp/nj_lib_lcdui.c
  src/midp/nj_lib_game.c
  src/midp/nj_lib_rms.c
  src/midp/nj_lib_midlet.c
  src/midp/nj_lib_media.c
  src/midp/nj_midi.c
  src/midp/nj_mp3.c
  src/midp/nj_mp4.c
  src/midp/nj_h263.c
  src/midp/nj_m4v.c
  src/midp/nj_amr.c
  src/midp/nj_amr_mp4.c
  src/midp/nj_lib_content.c
  src/midp/nj_lib_sax.c
  src/midp/nj_lib_nokia.c
  src/midp/nj_lib_fc.c
  src/midp/nj_lib_wireless.c
  src/midp/nj_lib_m3g.c
  src/midp/nj_lib_mascot.c
  src/midp/nj_mascot_model.c
  src/midp/nj_mascot_anim.c
  src/midp/nj_mascot_raster.c
  # ---- graphics ----
  src/gfx/nj_gfx.c
  src/gfx/nj_text.c
  src/gfx/nj_font.c
  src/gfx/nj_font_ttf.c
  src/gfx/nj_ttf.c
  src/gfx/nj_png.c
  src/gfx/nj_jpeg.c
  src/gfx/nj_codec.c
  src/gfx/nj_gif.c
  # ---- compression ----
  src/zip/nj_inflate.c
  src/zip/nj_zip.c
  # ---- the runner ----
  src/runner/nj_run.c
  # ---- the desktop client's portable half ----
  # nj_library and nj_client_ui read a JAR and draw text; neither knows about a
  # window, so they are built into the library and tested headlessly.  The SDL
  # window half lives in src/port/posix/nj_sdl2_client.c (below, when SDL2 is
  # found).
  src/client/nj_library.c
  src/client/nj_client_config.c
  src/client/nj_client_ui.c
)

add_library(nj2me STATIC ${NJ_SOURCES})
add_library(nj2me::nj2me ALIAS nj2me)

target_include_directories(nj2me
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core
          # nj_posix_win32.h: the mingw-w64 shims shared by the POSIX-shaped
          # port sources and the hosted client library.
          ${CMAKE_CURRENT_SOURCE_DIR}/src/port/posix)

# The headless port inflates .m3g sections through the nj_port.inflate hook,
# so the library needs zlib when it is there.  A build without zlib still
# compiles: the hook is then simply not filled in.
find_package(ZLIB)

# The headless port also *includes* <zlib.h> for its inflate hooks, so a
# cross-build needs the header for the target, not the host's.  Ask the
# target compiler instead of guessing from ZLIB_FOUND: `zig cc -target ...`
# has no zlib headers in its sysroot and must simply build the hooks out.
include(CheckIncludeFile)
check_include_file(zlib.h NJ_HAVE_ZLIB_H)
if(ZLIB_FOUND AND NJ_HAVE_ZLIB_H)
  target_compile_definitions(nj2me PRIVATE NJ_HAVE_ZLIB_H=1)
  target_link_libraries(nj2me PUBLIC ZLIB::ZLIB)
endif()

target_compile_options(nj2me PRIVATE -Wall -Wextra)
# A printf whose conversions and arguments disagree is undefined behaviour, and
# it cost this project a day: an eleven-argument stats line printed the bit
# pattern of a double as `m3g=516708`, which read as a measurement and sent me
# looking for a regression that did not exist.  -Wall already warns about it;
# this makes the warning stop the build.
target_compile_options(nj2me PRIVATE -Werror=format)
if(NJ_WERROR)
  target_compile_options(nj2me PRIVATE -Werror)
endif()

target_compile_definitions(nj2me PUBLIC
  NJ_PIXFMT=${NJ_PIXFMT}
  NJ_THREADED_DISPATCH=${NJ_THREADED_DISPATCH}
  NJ_ENABLE_SUPERINSTR=${NJ_ENABLE_SUPERINSTR}
  NJ_ENABLE_JUMP_THREADING=${NJ_ENABLE_JUMP_THREADING}
  NJ_ENABLE_ENGINE=${NJ_ENABLE_ENGINE}
  NJ_ENABLE_JIT=${NJ_ENABLE_JIT}
  NJ_JIT_CACHE_ENTRIES=${NJ_JIT_CACHE_ENTRIES}
  NJ_JIT_CACHE_WAYS=${NJ_JIT_CACHE_WAYS}
  NJ_JIT_METHOD_ENTRIES=${NJ_JIT_METHOD_ENTRIES}
  NJ_JIT_METHOD_ARENA_BYTES=${NJ_JIT_METHOD_ARENA_BYTES}
  NJ_JIT_MAX_FAULTS=${NJ_JIT_MAX_FAULTS}
  NJ_JIT_MAX_EXITS=${NJ_JIT_MAX_EXITS}
  NJ_JIT_MIN_BLOCK_INSNS=${NJ_JIT_MIN_BLOCK_INSNS}
  NJ_JIT_STATS=${NJ_JIT_STATS}
  NJ_JIT_DETAILED_STATS=${NJ_JIT_DETAILED_STATS}
  NJ_JIT_RV32_REG_CACHE=${NJ_JIT_RV32_REG_CACHE}
  NJ_JIT_HOT_THRESHOLD=${NJ_JIT_HOT_THRESHOLD}
  NJ_JIT_MAX_BLOCK_INSNS=${NJ_JIT_MAX_BLOCK_INSNS}
  NJ_JIT_MAX_CODE_BYTES=${NJ_JIT_MAX_CODE_BYTES}
  NJ_M3G_FAST_PATHS=${NJ_M3G_FAST_PATHS}
  NJ_ENABLE_HOT_FIELDS=${NJ_ENABLE_HOT_FIELDS}
  NJ_FIELD_CACHE_SLOTS=${NJ_FIELD_CACHE_SLOTS}
  NJ_CLASS_CACHE_SLOTS=${NJ_CLASS_CACHE_SLOTS}
  NJ_MASCOT_RASTER_SPANS=${NJ_MASCOT_RASTER_SPANS}
    NJ_MASCOT_FAST_FIELDS=${NJ_MASCOT_FAST_FIELDS}
  NJ_MASCOT_MAX_QUEUED_TRIANGLES=${NJ_MASCOT_MAX_QUEUED_TRIANGLES}
  NJ_ENABLE_FONT_CJK=${NJ_ENABLE_FONT_CJK}
  NJ_ENABLE_FONT_CJK_AA=${NJ_ENABLE_FONT_CJK_AA}
  NJ_ENABLE_PNG=${NJ_ENABLE_PNG}
  NJ_ENABLE_GIF=${NJ_ENABLE_GIF}
  NJ_ENABLE_INFLATE=${NJ_ENABLE_INFLATE}
  NJ_ENABLE_WAV=${NJ_ENABLE_WAV}
  NJ_ENABLE_JPEG=${NJ_ENABLE_JPEG}
  NJ_ENABLE_RMS=${NJ_ENABLE_RMS}
  NJ_ENABLE_IO=${NJ_ENABLE_IO}
  NJ_ENABLE_STREAM_UTF8=${NJ_ENABLE_STREAM_UTF8}
  NJ_ENABLE_SAX=${NJ_ENABLE_SAX}
  NJ_ENABLE_AMR=${NJ_ENABLE_AMR}
  NJ_ENABLE_MIDI=${NJ_ENABLE_MIDI}
  NJ_ENABLE_MP3=${NJ_ENABLE_MP3}
  NJ_ENABLE_VIDEO=${NJ_ENABLE_VIDEO}
  NJ_ENABLE_H263=${NJ_ENABLE_H263}
  NJ_ENABLE_M4V=${NJ_ENABLE_M4V}
  NJ_MP4_SEPARATE
  NJ_ENABLE_FLOAT=${NJ_ENABLE_FLOAT}
  NJ_ENABLE_LONG=${NJ_ENABLE_LONG}
  NJ_ENABLE_EXCEPTION_MESSAGES=${NJ_ENABLE_EXCEPTION_MESSAGES}
  NJ_ENABLE_LC_DUPLI=${NJ_ENABLE_LC_DUPLI}
  NJ_ENABLE_GAME_PACKAGE=${NJ_ENABLE_GAME_PACKAGE}
  NJ_ENABLE_MEDIA=${NJ_ENABLE_MEDIA}
  NJ_ENABLE_M3G=${NJ_ENABLE_M3G}
  NJ_ENABLE_MASCOT=${NJ_ENABLE_MASCOT}
  NJ_ENABLE_TTF=${NJ_ENABLE_TTF})

foreach(ext M A F D C)
  if(NOT NJ_JIT_RV32_${ext} STREQUAL "AUTO")
    target_compile_definitions(nj2me PUBLIC NJ_JIT_RV32_${ext}=${NJ_JIT_RV32_${ext}})
  endif()
endforeach()

foreach(ext FP SIMD LSE)
  if(NOT NJ_JIT_ARM64_${ext} STREQUAL "AUTO")
    target_compile_definitions(nj2me PUBLIC NJ_JIT_ARM64_${ext}=${NJ_JIT_ARM64_${ext}})
  endif()
endforeach()

if(NJ_SANITIZE)
  target_compile_options(nj2me PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(nj2me PUBLIC -fsanitize=address,undefined)
endif()

# The interpreter is one function with a ~1200-case switch.  At -O3 GCC 16 has
# been observed to die with an internal compiler error on it under parallel
# build memory pressure, and the generated code is not measurably faster than
# -O2 anyway - the dispatch is a jump table either way.  Pin this one file so a
# release build is reproducible rather than occasionally flaky.
set_source_files_properties(src/core/nj_interp.c PROPERTIES
  COMPILE_OPTIONS "-O2")

if(UNIX)
  target_link_libraries(nj2me PUBLIC m)
endif()

# ---------------------------------------------------------------------
# Ports
#
# The headless port is always present: it is the reference implementation and
# the one the test suite drives.  Everything else is conditional on the SDK
# being available, and every port source compiles everywhere - outside its own
# platform it provides stubs that report NJ_ERR_UNSUPPORTED, so one library can
# contain all of them.
# ---------------------------------------------------------------------
target_sources(nj2me PRIVATE src/port/posix/nj_port_headless.c)

if(NJ_BUILD_EMBEDDED_PORTS)
  target_sources(nj2me PRIVATE
    src/port/android/nj_port_android.c
    src/port/esp32/nj_port_esp32.c)
endif()

set(NJ_POSIX_NETWORK_ENABLED OFF)
option(NJ_BUILD_POSIX_NETWORK "Enable desktop HTTP/HTTPS/TCP with libcurl" ON)
if(NJ_BUILD_POSIX_NETWORK AND UNIX AND NOT ANDROID)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(NJ_CURL QUIET IMPORTED_TARGET libcurl>=7.85)
  endif()
  if(NJ_CURL_FOUND)
    set(NJ_POSIX_NETWORK_ENABLED ON)
    target_sources(nj2me PRIVATE src/port/posix/nj_posix_network.c)
    target_link_libraries(nj2me PUBLIC PkgConfig::NJ_CURL)
    target_compile_definitions(nj2me PUBLIC NJ_HAVE_POSIX_NETWORK=1)
    message(STATUS "POSIX network: enabled (${NJ_CURL_VERSION})")
  else()
    message(STATUS "POSIX network: disabled - libcurl >= 7.85 development files unavailable")
  endif()
endif()

if(NJ_BUILD_SDL2)
  # Save values the caller may have set on the command line.  A cross build has
  # no pkg-config file for the *target* SDL2 (and the host's libsdl2-dev, when
  # installed, is the wrong architecture), so accept the include/lib paths
  # explicitly:
  #
  #   -DSDL2_INCLUDE_DIRS=<sdl2>/x86_64-w64-mingw32/include
  #   -DSDL2_LIBRARIES=<sdl2>/x86_64-w64-mingw32/lib/libSDL2.dll.a
  #
  # The pkg-config path stays first: a native Linux build finds SDL2 exactly
  # as before and the fallback never overrides it.
  set(NJ_SDL2_CACHE_INCLUDE_DIRS "${SDL2_INCLUDE_DIRS}")
  set(NJ_SDL2_CACHE_LIBRARIES "${SDL2_LIBRARIES}")
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
  endif()
  if(NOT SDL2_FOUND AND NJ_SDL2_CACHE_INCLUDE_DIRS AND NJ_SDL2_CACHE_LIBRARIES)
    set(SDL2_FOUND TRUE)
    set(SDL2_INCLUDE_DIRS "${NJ_SDL2_CACHE_INCLUDE_DIRS}")
    set(SDL2_LIBRARIES "${NJ_SDL2_CACHE_LIBRARIES}")
    set(SDL2_VERSION "cross cache")
  endif()
  if(SDL2_FOUND)
    target_sources(nj2me PRIVATE
      src/port/posix/nj_port_sdl2.c
      src/port/posix/nj_sdl2_client.c)
    target_include_directories(nj2me PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(nj2me PUBLIC ${SDL2_LIBRARIES})
    target_compile_definitions(nj2me PUBLIC NJ_HAVE_SDL2=1)
    message(STATUS "SDL2 port: enabled (${SDL2_VERSION})")
  else()
    message(STATUS "SDL2 port: disabled - install libsdl2-dev for a window; "
                   "the headless port still works")
  endif()
endif()

# ---------------------------------------------------------------------
# Command-line runner
#
# NOTE the target name: the library already owns "nj2me", and CMake rejects a
# duplicate target, so the executable is nj2me-cli with an output name of
# nj2me.  Same binary, different target.
# ---------------------------------------------------------------------
if(NJ_BUILD_CLI)
  add_executable(nj2me-cli apps/nj2me_main.c)
  target_link_libraries(nj2me-cli PRIVATE nj2me)
  # CommandLineToArgvW: a Windows argv built from the wide command line, so a
  # UTF-8 path argument survives the ANSI code page (see apps/nj2me_main.c).
  if(WIN32)
    target_link_libraries(nj2me-cli PRIVATE shell32)
  endif()
  set_target_properties(nj2me-cli PROPERTIES OUTPUT_NAME nj2me)
  install(TARGETS nj2me-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # A desktop entry, so the client can be started from a menu: bare `nj2me`
  # opens the games library.  The icon is the one Icon= names.
  install(FILES data/nj2me.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES data/nj2me.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()

install(TARGETS nj2me ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/nj2me DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ---------------------------------------------------------------------
# libretro core
#
# A shared object any libretro frontend can load: RetroArch, Lakka,
# RetroDECK, or the test harness in tests/test_libretro_frontend.c.  It links
# runtime, so the archive has to be position independent.  The libretro ABI
# reaches its entry points without a userdata pointer, which is why the single
# process-wide core instance lives in src/port/libretro/ - see the documented
# exemption in tools/check_no_statics.sh.
# ---------------------------------------------------------------------
if(NJ_BUILD_LIBRETRO)
  set_target_properties(nj2me PROPERTIES POSITION_INDEPENDENT_CODE ON)

  add_library(nano_j2me_libretro SHARED
    src/port/libretro/nj_port_libretro.c)
  target_include_directories(nano_j2me_libretro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/port/libretro)
  target_link_libraries(nano_j2me_libretro PRIVATE nj2me)
  target_compile_options(nano_j2me_libretro PRIVATE -Wall -Wextra -Werror=format)
  if(NOT MSVC)
    target_compile_options(nano_j2me_libretro PRIVATE -fvisibility=hidden)
  endif()
  # The archive carries the optional SDL2 and curl ports; a frontend must not
  # inherit their shared libraries, so let the linker drop what no symbol uses.
  if(UNIX AND NOT APPLE)
    target_link_options(nano_j2me_libretro PRIVATE -Wl,--as-needed)
  endif()

  # RetroArch loads cores by bare name (<core>_libretro.so), so no "lib"
  # prefix; the .info beside it is what makes the core appear in its menu.
  set_target_properties(nano_j2me_libretro PROPERTIES
    OUTPUT_NAME nano_j2me_libretro
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libretro
    # A Windows core is a .dll (RUNTIME), and a macOS core a .dylib
    # (LIBRARY); keep whichever artifact the host produces in one place.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libretro
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libretro)

  install(TARGETS nano_j2me_libretro
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/libretro
          RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/libretro)
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/port/libretro/nano_j2me_libretro.info
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/libretro)
endif()

# ---------------------------------------------------------------------
# Tests
#
# Each test is a self-contained program with its own assertion count; ctest
# just runs them.  They are added only when the file exists, so the tree stays
# buildable while a module is being written.
# ---------------------------------------------------------------------
if(NJ_BUILD_TESTS)
  enable_testing()

  # ---- fixtures the tests need, generated at build time ----
  #
  # PICK THE INTERPRETER THAT ACTUALLY HAS PILLOW, not merely the first one on
  # PATH.  A pyenv/user python3 without Pillow is extremely common, and
  # "pip install Pillow" is not something a build system gets to demand - while
  # the system python usually already has it.  gen_fixtures.py needs nothing but
  # the standard library, so it can always run; the PNG and JPEG fixture
  # generators do need Pillow.
  set(NJ_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

  set(NJ_PYTHON_CANDIDATES "")
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    list(APPEND NJ_PYTHON_CANDIDATES ${Python3_EXECUTABLE})
  endif()
  list(APPEND NJ_PYTHON_CANDIDATES
       /usr/bin/python3 /usr/local/bin/python3 python3)

  set(NJ_PYTHON "${Python3_EXECUTABLE}")
  set(NJ_PYTHON_PIL "")
  foreach(candidate IN LISTS NJ_PYTHON_CANDIDATES)
    if(NOT candidate)
      continue()
    endif()
    if(NOT NJ_PYTHON)
      set(NJ_PYTHON "${candidate}")
    endif()
    execute_process(
      COMMAND ${candidate} -c "import PIL"
      RESULT_VARIABLE _pil_rc OUTPUT_QUIET ERROR_QUIET)
    if(_pil_rc EQUAL 0)
      set(NJ_PYTHON_PIL "${candidate}")
      break()
    endif()
  endforeach()

  if(NJ_PYTHON_PIL)
    message(STATUS "fixture generator python: ${NJ_PYTHON_PIL} (Pillow found)")
  else()
    message(WARNING
      "no python3 with Pillow was found; the PNG and JPEG fixture generators "
      "will be skipped and their tests will fail.  Install Pillow, or run "
      "those generators by hand into tests/fixtures/.")
  endif()

  add_custom_target(nj_fixtures
    COMMAND ${CMAKE_COMMAND} -E make_directory ${NJ_FIXTURES}
    COMMENT "Generating test fixtures")
  if(NJ_PYTHON)
    add_custom_command(TARGET nj_fixtures POST_BUILD
      COMMAND ${NJ_PYTHON}
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_fixtures.py
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "zip/deflate fixtures")
  endif()
  # Every tests/gen_*.py is a fixture generator.  Globbing rather than listing
  # them means a new test module that needs assets only has to add its
  # generator, not edit the build.
  # The JAR fixtures need a JDK; the C tests that use them are skipped
  # without one, so this is only registered when javac is present.
  find_program(NJ_JAVAC javac)
  if(NJ_JAVAC)
    add_custom_command(TARGET nj_fixtures POST_BUILD
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_keys_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_blockdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_loopdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_latedemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fsorder_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_hangdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_sizedemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_utfdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_encdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_devicedemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_waitdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_lockdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_timerdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fielddemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_cvdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_flatdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_excdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_paintthrow_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_clipdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_m3gdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_boxdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_repaintdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_alphademo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_arrcls_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_keychardemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_savedgfx_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_flushdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_nokiademo_jar.sh
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "keysdemo.jar, blockdemo.jar (input-effect fixtures)")
  endif()
  file(GLOB NJ_FIXTURE_GENERATORS
       ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen_*.py)
  foreach(gen IN LISTS NJ_FIXTURE_GENERATORS)
    get_filename_component(gen_name ${gen} NAME_WE)
    if(gen_name STREQUAL "gen_fixtures")
      continue()   # already run above; needs no Pillow
    endif()
    if(NJ_PYTHON_PIL)
      add_custom_command(TARGET nj_fixtures POST_BUILD
        COMMAND ${NJ_PYTHON_PIL} ${gen}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "${gen_name}")
    endif()
  endforeach()

  # Generators that need no third-party module can always run.
  if(NJ_PYTHON)
    foreach(gen IN LISTS NJ_FIXTURE_GENERATORS)
      get_filename_component(gen_name ${gen} NAME_WE)
      if(gen_name STREQUAL "gen_lcdui_fixture" AND NOT NJ_PYTHON_PIL)
        add_custom_command(TARGET nj_fixtures POST_BUILD
          COMMAND ${NJ_PYTHON} ${gen}
          WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
          COMMENT "${gen_name}")
      endif()
    endforeach()
  endif()
  add_custom_command(TARGET nj_fixtures POST_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_hello_jar.sh
    COMMENT "VM-core MIDlet fixture")
  add_custom_command(TARGET nj_fixtures POST_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_game_jar.sh
    COMMENT "full-game MIDlet fixture")
  add_custom_command(TARGET nj_fixtures POST_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_audio_jar.sh
    COMMENT "audio MIDlet fixture")
  add_custom_command(TARGET nj_fixtures POST_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_fs_jar.sh
    COMMENT "JSR 75 MIDlet fixture")
  # Two real third-party titles, built into the FIXTURE directory as well as the
  # corpus one, because test_poke drives them and a test cannot depend on a
  # gitignored directory that may not exist.
  # These two FETCH FROM THE NETWORK, which is why they are behind their own
  # switch: a build that silently needs GitHub is a build that fails on a plane.
  if(NJ_FIXTURES_FETCH_GAMES)
    add_custom_command(TARGET nj_fixtures POST_BUILD
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_slimevolley_jar.sh
              ${NJ_FIXTURES}/slimevolley.jar
      COMMENT "Slime Volleyball fixture (downloaded)")
    add_custom_command(TARGET nj_fixtures POST_BUILD
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_game15_jar.sh
              ${NJ_FIXTURES}/game15.jar
      COMMENT "15-puzzle fixture (downloaded)")
  endif()
  add_custom_command(TARGET nj_fixtures POST_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_const_jar.sh
    COMMENT "stub constant fixture")
  add_custom_command(TARGET nj_fixtures POST_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_sensor_jar.sh
    COMMENT "JSR 256 sensor fixture")
  add_custom_command(TARGET nj_fixtures POST_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_socket_jar.sh
    COMMENT "SocketConnection fixture")
  add_custom_command(TARGET nj_fixtures POST_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_bt_jar.sh
    COMMENT "javax.bluetooth fixture")
  # The TrueType fixtures are hand-built by a script (there is no system font
  # we could legally check in): deterministic, so re-running is a no-op.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_ttf_fixture.py)
    add_custom_command(TARGET nj_fixtures POST_BUILD
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_ttf_fixture.py
      COMMENT "TrueType rasteriser fixtures")
  endif()

  # A test that fails with the command to fix it, rather than 25 tests failing
  # one at a time with "cannot open tests/fixtures/...".  The fixtures are
  # gitignored build inputs, so a fresh clone has none.
  add_test(NAME fixtures_present
           COMMAND ${CMAKE_COMMAND}
                   -DFIX=${NJ_FIXTURES}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_fixtures.cmake)
  set_tests_properties(fixtures_present PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  # The Android port's plain-Java decisions on a desktop JVM: the keypad's
  # press semantics (a keycap presses with the touch that lands on it, releases
  # when the finger slides off) and the name a game is shown under (the
  # MIDlet's own name, not the file's).  A JDK is the whole requirement - no
  # SDK, no device - so this runs wherever the tests do.
  if(NJ_JAVAC)
    add_test(NAME check_android_java
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_android_java.sh)
    set_tests_properties(check_android_java PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
  endif()

  if(NJ_POSIX_NETWORK_ENABLED AND NJ_BUILD_SDL2 AND SDL2_FOUND AND NJ_PYTHON)
    add_executable(test_posix_network tests/test_posix_network.c)
    target_link_libraries(test_posix_network PRIVATE nj2me)
    add_test(NAME test_posix_network
      COMMAND ${NJ_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_posix_network.py
              $<TARGET_FILE:test_posix_network>)
    set_tests_properties(test_posix_network PROPERTIES TIMEOUT 25 SKIP_RETURN_CODE 77)
  endif()

  # ---- helper: one test executable, linked against the runtime ----
  function(nj_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE nj2me)
    # Tests are deliberately white-box: several of them drive the builtin
    # class tables and the MIDP-internal contract directly, which is the only
    # way to assert on things the public API does not expose.
    target_include_directories(${name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core
      ${CMAKE_CURRENT_SOURCE_DIR}/src/midp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx)
    # NOT a dependency of the default build.  Fixtures are data: they are
    # generated once, explicitly, with `--target fixtures`, and a plain build
    # stays a compile.  Set NJ_GENERATE_FIXTURES=ON to go back to the old
    # behaviour where every build regenerated them.
    if(NJ_GENERATE_FIXTURES)
      add_dependencies(${name} nj_fixtures)
    endif()
    if(ARGC GREATER 2)
      add_test(NAME ${name} COMMAND ${name} ${ARGN})
    else()
      add_test(NAME ${name} COMMAND ${name})
    endif()
    # Tests reference tests/fixtures/... relatively, and ctest runs from the
    # build directory by default.
    set_tests_properties(${name} PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endfunction()

  # Encoders run on every host; native calls require a matching architecture.
  nj_add_test(test_jit tests/test_jit.c)
  if(NJ_ENABLE_JIT)
    # Respect the deployment ISA here. Separate matrix scripts enable other
    # extensions only inside a suitable native execution harness.
    nj_add_test(test_jit_rv32 tests/test_jit_rv32.c)
    target_compile_features(test_jit_rv32 PRIVATE c_std_11)
    nj_add_test(test_jit_arm64 tests/test_jit_arm64.c)
    target_compile_features(test_jit_arm64 PRIVATE c_std_11)
    add_executable(test_jit_perf tests/test_jit_perf.c)
    target_link_libraries(test_jit_perf PRIVATE nj2me)
    target_compile_features(test_jit_perf PRIVATE c_std_11)
    target_compile_definitions(test_jit_perf PRIVATE
      NJ_TEST_JIT_PERF_JAR="${NJ_FIXTURES}/jitperf.jar")
    # Generate explicitly with tools/check_jit_perf.sh --build-fixture-only.
    # A missing fixture must not turn an ordinary source build into a JDK job.
    if(EXISTS "${NJ_FIXTURES}/jitperf.jar")
      add_test(NAME test_jit_perf COMMAND test_jit_perf)
      set_tests_properties(test_jit_perf PROPERTIES SKIP_RETURN_CODE 77)
    endif()
  endif()

  # ---- the memory contract and the class library ----
  nj_add_test(test_lifecycle tests/test_lifecycle.c)
  nj_add_test(test_string_intern tests/test_string_intern.c)
  nj_add_test(test_text_input tests/test_text_input.c)
  if(SDL2_FOUND)
    target_include_directories(test_text_input PRIVATE ${SDL2_INCLUDE_DIRS})
  endif()
  # Every method a C-implemented class declares must have a native behind it.
  # Three such bindings were missing and each one only surfaced when a real
  # game called it; this walks the whole class library instead of waiting.
  nj_add_test(test_natives   tests/test_natives.c)
  nj_add_test(test_m3g       tests/test_m3g.c)
  nj_add_test(test_m3g_targets tests/test_m3g_targets.c)
  nj_add_test(test_m3g_references tests/test_m3g_references.c)
  nj_add_test(test_mascot    tests/test_mascot.c)
  nj_add_test(test_mascot_scene tests/test_mascot_scene.c)
  nj_add_test(test_mascot_model tests/test_mascot_model.c)
  nj_add_test(test_mascot_anim  tests/test_mascot_anim.c)
  nj_add_test(test_mascot_raster tests/test_mascot_raster.c)
  nj_add_test(test_png tests/test_png.c)
  nj_add_test(test_mascot_commands tests/test_mascot_commands.c)
  nj_add_test(test_string    tests/test_string.c)
  nj_add_test(test_m3g_file tests/test_m3g_file.c)

# The container probe as a command line tool, so tools/check_m3g_reals.sh can
# run it over the .m3g files the collected games ship.  A unit test encodes the
# format and therefore cannot catch the format being written down wrongly.
add_executable(nj_m3g_probe tests/probe_m3g.c)
target_link_libraries(nj_m3g_probe PRIVATE nj2me)
# The probe inflates real sections, so it needs zlib.  The runtime does not:
# decompression belongs behind a port hook there, and this is a host tool.
find_package(ZLIB)
if(ZLIB_FOUND)
  target_link_libraries(nj_m3g_probe PRIVATE ZLIB::ZLIB)
else()
  message(WARNING "no zlib: nj_m3g_probe cannot inflate compressed .m3g sections")
endif()
  nj_add_test(test_wireless tests/test_wireless.c)
  # JSR 75 security types: HttpsConnection / SecurityInfo / SecureConnection /
  # pki.Certificate, and the http-vs-https instanceof answer.
  nj_add_test(test_https tests/test_https.c)
  nj_add_test(test_socket_api tests/test_socket_api.c)

  # JSR 256 end to end: SensorManager -> SensorInfo -> SensorConnection ->
  # getData() and a DataListener.  The listener is the half no other test can
  # reach, because dataReceived() is driven by the frame pump.  The headless
  # port's synthetic accelerometer makes the numbers assertable.
  if(EXISTS ${NJ_FIXTURES}/sensordemo.jar)
    add_test(NAME check_sensor
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_sensor.sh
                     $<TARGET_FILE:nj2me-cli> ${NJ_FIXTURES}/sensordemo.jar)
    set_tests_properties(check_sensor PROPERTIES TIMEOUT 120)
  endif()

  # javax.bluetooth end to end, on the headless port's fake radio: the local
  # device, an inquiry whose devices arrive through the frame pump, a service
  # search, an RFCOMM server accepted and echoed, and an L2CAP server.  The
  # fake is opt-in (NJ_HEADLESS_BT=1 inside the script) so the radio-less
  # answer tests/test_wireless.c pins down stays the default.
  if(EXISTS ${NJ_FIXTURES}/btdemo.jar)
    add_test(NAME check_bt
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_bt.sh
                     $<TARGET_FILE:nj2me-cli> ${NJ_FIXTURES}/btdemo.jar)
    set_tests_properties(check_bt PROPERTIES TIMEOUT 180)
  endif()

  # SocketConnection's negative half: the headless port has no socket stack, so
  # every socket:// open must come back as a ConnectionNotFoundException that
  # names the build rather than the server.  The positive half needs a live
  # server and is run on a device (see tools/check_socket.sh).
  if(EXISTS ${NJ_FIXTURES}/socketdemo.jar)
    add_test(NAME check_socket
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_socket.sh
                     $<TARGET_FILE:nj2me-cli> ${NJ_FIXTURES}/socketdemo.jar)
    set_tests_properties(check_socket PROPERTIES TIMEOUT 120)
  endif()

  # What the PORT observed, not what the VM counted: threads, GC, audio and
  # file activity on one machine-readable line.  Skipped without a fixture.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_late_display.sh)
    add_test(NAME check_late_display
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_late_display.sh
                     $<TARGET_FILE:nj2me-cli> ${NJ_FIXTURES}/latedemo.jar)
    set_tests_properties(check_late_display PROPERTIES TIMEOUT 120)
  endif()

  # Same-scene pixel comparison against the reference emulator.  Only
  # registered when the reference is present (it is a local tool, not part of
  # the tree), and it skips when python3 has no PIL.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.local-tools/run-freej2me-headless.sh)
    add_test(NAME check_reference_frame
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/compare_reference_frame.sh
                     $<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/games/三国杀单机战功修改版_4143.jar
                     ${CMAKE_CURRENT_SOURCE_DIR}/.local-tools/run-freej2me-headless.sh
                     300 240 320 12)
    set_tests_properties(check_reference_frame PROPERTIES TIMEOUT 200
                         SKIP_RETURN_CODE 77)
    add_test(NAME check_reference_frame_mario
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/compare_reference_frame.sh
                     $<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/games/超级马里奥_10283.jar
                     ${CMAKE_CURRENT_SOURCE_DIR}/.local-tools/run-freej2me-headless.sh
                     300 240 320 12)
    set_tests_properties(check_reference_frame_mario PROPERTIES TIMEOUT 200
                         SKIP_RETURN_CODE 77)
    add_test(NAME check_reference_frame_kof
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/compare_reference_frame.sh
                     $<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/games/拳皇98终极之战_7534.jar
                     ${CMAKE_CURRENT_SOURCE_DIR}/.local-tools/run-freej2me-headless.sh
                     300 240 320 8)
    set_tests_properties(check_reference_frame_kof PROPERTIES TIMEOUT 200
                         SKIP_RETURN_CODE 77)
  endif()

  # The titles that found the round 33-37 compatibility fixes: a leaked
  # thread slot, Canvas methods declared instance instead of static, and the
  # M3G spot light that lost its cone angle.  Cheap replay, real games.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_compat_fixes.sh)
    add_test(NAME check_compat_fixes
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_compat_fixes.sh
                     $<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/games)
    set_tests_properties(check_compat_fixes PROPERTIES TIMEOUT 200
                         SKIP_RETURN_CODE 77)
  endif()

  # 街头霸王2冠军典藏版_1812: answering the sound prompt starts a loader whose
  # state machine lives in paint() and sleeps between asset reads.  The frame
  # pump used to run a second frame on top of that paint, the title's loader
  # advanced twice, skipped the step that fills its sample table and threw
  # NullPointerException, which the title swallows in an empty catch loop.
  # See docs/notes/2026-10-27-street-fighter-loader-hang.md.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_sf2_loader.sh)
    add_test(NAME check_sf2_loader
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_sf2_loader.sh
                     $<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/games)
    set_tests_properties(check_sf2_loader PROPERTIES TIMEOUT 200
                         SKIP_RETURN_CODE 77)
  endif()

  # 纽约人生_1339: a title that blocks inside paint() while its loader reads.
  # The pump's nested frame may run (input, scheduler, present), but it must
  # not paint again - and it must not be refused whole either, because
  # run_frame_hook() counts pumped frames, so refusals spent the run budget
  # and the title presented one picture.  See
  # docs/notes/2026-10-27-paint-pump.md and tools/check_sf2_loader.sh.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_paint_pump.sh)
    add_test(NAME check_paint_pump
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_paint_pump.sh
                     $<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/games)
    set_tests_properties(check_paint_pump PROPERTIES TIMEOUT 120
                         SKIP_RETURN_CODE 77)
  endif()

  # A title whose constructor outlasts the frame budget - both Need for Speed
  # titles sleep through a splash screen in <init>.  That is the run ending on
  # purpose and it has to say so: it used to report "cannot instantiate",
  # which reads as a missing class and sent a reader looking for one.
  if(EXISTS ${NJ_FIXTURES}/blockdemo.jar)
    if(NJ_JAVAC)
    add_test(NAME check_readutf
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_readutf.sh
                     ${NJ_FIXTURES}/utfdemo.jar)
    # Java encoding names are case-insensitive; the table in nj_lib_string.c was
    # matched with strcmp, which made getBytes("utf-8") an
    # UnsupportedEncodingException - 疯狂战机_2013 dies there on every run and 48
    # of the collected titles carry a non-canonical spelling of the name.
    add_test(NAME check_encoding_names
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_encoding_names.sh
                     ${NJ_FIXTURES}/encdemo.jar)
    # --device / --prop: the runtime can be told which handset a title is on,
    # because a title branches on microedition.platform and on the vendor's IMEI
    # spellings, and takes an unfinished path when it gets an answer it does not
    # know.  1945空战英雄_6105 will not start at all unless the platform begins
    # with "Nokia", and runs when told it is one.
    add_test(NAME check_device_props
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_device_props.sh
                     ${NJ_FIXTURES}/devicedemo.jar)
    # JSR 75's connection security types, checked through the interpreter:
    # `http://` must NOT be an HttpsConnection, `https://` must be (and an
    # HttpConnection through its super), and getSecurityInfo() answers
    # IOException because the port's HTTP hook does not expose a TLS session.
    # 急速公路狂飙－新年版_84 evaluates `c instanceof HttpsConnection` on every
    # request; without the class the interpreter raised NoClassDefFoundError
    # inside a working connection.  The script builds its own fixture, so it
    # needs only a JDK.
    add_test(NAME check_https_types
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_https_types.sh)
    set_tests_properties(check_https_types PROPERTIES TIMEOUT 180
                         SKIP_RETURN_CODE 77)
    # JSR 135 TempoControl / RateControl on a MIDI Player, through the
    # interpreter: the control exists, setTempo/setRate report what they set,
    # getControls() has no NULL, and the Player's duration follows the tempo.
    # 速度与激情6_9 gets this control, null-checks it and catches exceptions,
    # so an absent control only meant its music ran at the file's own tempo.
    add_test(NAME check_tempo_control
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_tempo_control.sh)
    set_tests_properties(check_tempo_control PROPERTIES TIMEOUT 180
                         SKIP_RETURN_CODE 77)
    # JSR 75's PIM half through the interpreter: the contact list opens empty,
    # no field is supported, items() is a real empty Enumeration, a list type
    # with no store raises PIMException and an unknown mode IllegalArgumentException.
    # 3D孤胆枪手2010_4634 walks the contact list and used to die on the class.
    add_test(NAME check_pim
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_pim.sh)
    set_tests_properties(check_pim PROPERTIES TIMEOUT 180
                         SKIP_RETURN_CODE 77)
    # Samsung's SM/SMS through the interpreter: the value object keeps what a
    # title sets, isSupported() is false and send() raises the IOException the
    # SDK declares.  Three corpus titles call exactly this subset.
    add_test(NAME check_samsung_sms
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_samsung_sms.sh)
    set_tests_properties(check_samsung_sms PROPERTIES TIMEOUT 180
                         SKIP_RETURN_CODE 77)
    # A MIDlet constructor is entered from C, and it may park on another thread:
    # 毁灭战士3_10598 starts a loader and waits for it while it builds the level,
    # and the C caller has to keep the VM running so the loader finishes rather
    # than report a timeout.  check_wait_pump.sh runs the fixture whose
    # constructor waits and asserts that it starts and paints.
    add_test(NAME check_wait_pump
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_wait_pump.sh
                     ${NJ_FIXTURES}/waitdemo.jar)
    # synchronized must mean mutual exclusion: a cooperative quantum boundary
    # inside a critical section used to let a second thread in (LockDemo
    # printed overlap=true).  check_lockdemo.sh asserts it never does.
    add_test(NAME check_lockdemo
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_lockdemo.sh
                     ${NJ_FIXTURES}/lockdemo.jar)
    set_tests_properties(check_lockdemo PROPERTIES TIMEOUT 180
                         SKIP_RETURN_CODE 77)
    # A java.util.Timer task runs on a thread that reaches it through a native,
    # so a task that parks leaves a native frame at the top of that thread's
    # stack.  Running it from the scheduler fed that frame to the interpreter
    # and five collected titles died with VerifyError "code runs past the end
    # of the method" at nj2me/util/TimerThread.pump pc=0.
    add_test(NAME check_timer_wait
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_timer_wait.sh
                     ${NJ_FIXTURES}/timerdemo.jar)
    # JVMS 5.4.3.2 resolves a Fieldref by name and descriptor through the class
    # and its superclasses; static-ness is a property of the field that wins.
    # Resolving the instance variant first linked 疯狂农场2_2375's static Vector
    # to its superclass's instance field of the same name, and the static access
    # then read and wrote nothing at all.  check_field_shadow.sh runs the
    # fixture that hides an instance field behind a static one.
    add_test(NAME check_field_shadow
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_field_shadow.sh
                     ${NJ_FIXTURES}/fielddemo.jar)
    # The SDL2 port grew an F5 snapshot key, but a headless test cannot press a
    # key: the runner's --save-state/--load-state flags reach the same
    # nj_vm_save_file()/nj_vm_load_file() calls.  tests/savestate/run.sh runs
    # the SDL2 port twice under the dummy video and audio drivers and asserts
    # the second process RESUMED the MIDlet - fielddemo prints its startApp
    # banner exactly once, so a restart would print a second one.  It also
    # checks that a file which is not a snapshot is refused without costing the
    # caller the run.
    if(SDL2_FOUND)
        add_test(NAME check_sdl2_savestate
                 COMMAND ${CMAKE_COMMAND} -E env
                         NJ_BIN=$<TARGET_FILE:nj2me-cli>
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/savestate/run.sh
                         ${NJ_FIXTURES}/fielddemo.jar)
        set_tests_properties(check_sdl2_savestate PROPERTIES TIMEOUT 120)
    endif()
    # The desktop client is a window, but everything it does before a game
    # starts is C: open a launcher, scan the games directory, paint a frame and
    # answer --print-library.  Under SDL's dummy driver that runs without a
    # display, and --client <jar> --run-frames N walks the launcher -> game ->
    # exit seam.  Real clicks and drops are covered by test_port_drop.c and
    # test_client_ui.c and were checked by hand with a display.
    if(SDL2_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_sdl2_client.sh)
        add_test(NAME check_sdl2_client
                 COMMAND ${CMAKE_COMMAND} -E env
                         NJ_BIN=$<TARGET_FILE:nj2me-cli>
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_sdl2_client.sh
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/games
                         ${NJ_FIXTURES}/alphademo.jar)
        set_tests_properties(check_sdl2_client PROPERTIES TIMEOUT 120
                             SKIP_RETURN_CODE 77)
    endif()
    # JVMS 5.5 step 4: a static field with a ConstantValue attribute takes its
    # value before <clinit> runs, and javac leaves such a field out of <clinit>
    # entirely.  20 static fields across 11 collected titles are read by a
    # getstatic and assigned nowhere else.  The fixture also pins the float and
    # double cases, where the field has to receive the constant's bytes rather
    # than a converted number.
    # MIDP 2.0, Graphics.translate(): "Operations on the coordinate system,
    # such as translate(), do not modify the clip."  Dragging the clip along
    # with the origin wiped out every tile of 帝国时代3-亚洲王朝_6288's campaign
    # map, which is drawn by translating the origin by the camera offset and
    # then blitting at map coordinates.
    add_test(NAME check_clip_translate
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_clip_translate.sh
                     ${NJ_FIXTURES}/clipdemo.jar)
    # CLDC declares toString()/equals()/hashCode() on every wrapper class.  A
    # missing override resolves up to java.lang.Object, so a title that builds
    # a path or a Hashtable key from a boxed value keeps running with the wrong
    # answer: 模拟人生2_10762 asked its JAR for
    # "global_data/java.lang.Integer@c1803e7" and went black.
    add_test(NAME check_boxing
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_boxing.sh
                     ${NJ_FIXTURES}/boxdemo.jar)
    # MIDP paints on request: repaint() with no arguments must request the
    # whole canvas, and the runner must not paint again in a frame the
    # application has already painted.  仙魔飞行棋_417 keeps its paint state in
    # mobile/k.f and clears it right after serviceRepaints(), so the second
    # paint found f == 0 and left a black map for the whole run.
    add_test(NAME check_repaint
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_repaint.sh
                     ${NJ_FIXTURES}/repaintdemo.jar)
    # A title may keep the Graphics paint() handed it and draw through it from
    # its own thread (out of spec, and 合金弹头2013BT版_678 does it with the
    # scenery behind the character).  Whatever is in the canvas buffer when the
    # frame ends has to be what the screen shows, so the buffer is copied at
    # the frame boundary, not at the end of paint().
    add_test(NAME check_savedgfx
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_savedgfx.sh
                     ${NJ_FIXTURES}/savedgfx.jar)
    # A GameCanvas that draws whole frames into its own buffer and calls
    # flushGraphics() has presented that frame: the runner must not paint the
    # same buffer again in the same frame.  The buffer is not double buffered,
    # so a second paint copies whatever is there at that moment, and a title
    # halfway through the next frame shows up torn - the block shaped flicker a
    # player reported for 鲁宾逊漂流记汉化版_702, whose game canvas flushes once
    # per pass of its own loop.  tests/flushdemo pins it down: the worker paints
    # the top quarter of the frame, sleeps across a frame boundary, finishes,
    # and flushes, so a runtime that paints by itself presents a frame holding
    # both colours.
    add_test(NAME check_flushdemo
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_flushdemo.sh
                     ${NJ_FIXTURES}/flushdemo.jar)
    add_test(NAME check_constant_value
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_constant_value.sh
                     ${NJ_FIXTURES}/cvdemo.jar)
    # The threaded (computed-goto) interpreter is a SECOND implementation of all
    # 256 opcodes, and nothing in a default build compiles it - so it rots
    # silently.  It did: a converted handler's DISPATCH() fetches the next
    # opcode, but the table sent unconverted opcodes to L_slow, which fetched a
    # second time, so every converted-to-unconverted transition lost one byte.
    # keysdemo (invokespecial, then aload_0) surfaced it as
    # "VerifyError: receiver is not an object reference".  The default build
    # never ran it because the dispatch default was nested inside
    # `#ifndef NJ_PIXFMT`: CMake defines a pixel format and got the switch,
    # while a hand build got the threaded interpreter.
    # tools/check_threaded_dispatch.sh builds BOTH interpreters from source,
    # requires equal bytecode counts on the fixture, and probes nj_config.h so
    # the default stays the portable switch with or without NJ_PIXFMT.
    add_test(NAME check_threaded_dispatch
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_threaded_dispatch.sh
                     ${NJ_FIXTURES}/keysdemo.jar)
    # Cutting one video codec must actually cut it: NJ_ENABLE_H263=0 and
    # NJ_ENABLE_M4V=0 used to leave ~23 KB of decoded tables in the object file
    # because only the provider table was guarded.  The check compiles each
    # unit with the switch on and off and reads the object size.
    add_test(NAME check_codec_trim
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_codec_trim.sh
                     ${CMAKE_C_COMPILER})
    set_tests_properties(check_threaded_dispatch PROPERTIES TIMEOUT 300)
    # The benchmark cannot see a wrong superinstruction: its branches are on
    # constants, so a fused branch that tested an uninitialized operand still
    # printed the expected `BENCH sum` (2026-10-22).  This compiles a digest
    # MIDlet whose methods exercise each fused family, hashes every result into
    # six per-category digests and compares them with the same code on a
    # desktop JVM - values against a JVM, not counts against a constant.
    add_test(NAME check_superinstr_digest
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_superinstr_digest.sh
                     $<TARGET_FILE:nj2me-cli>)
    set_tests_properties(check_superinstr_digest PROPERTIES TIMEOUT 300)
    # A mutable Image is white and fully opaque (MIDP 2.0).  A Java int[] starts
    # zeroed - transparent black - so a title that builds its scenery off-screen
    # blended its sprites against black and let the untouched pixels see
    # through: 合金弹头2013BT版_678's scenery read as flickering patches.
    add_test(NAME check_alphademo
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_alphademo.sh
                     ${NJ_FIXTURES}/alphademo.jar)
    # An array the runtime created has no class, and the empty string has no
    # characters.  String.getBytes() hands out a byte[] that was never given a
    # class, and a title calls methods on it: the UC browser title killed the
    # emulator with SIGSEGV while it was building the message "X does not
    # implement Y" from that missing class.  The empty string is stored as a
    # NULL payload, and the C standard does not allow a NULL pointer to reach
    # memcmp even for a length of zero.
    add_test(NAME check_arrcls
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_arrcls.sh
                     ${NJ_FIXTURES}/arrcls.jar)
    # Displayable.getKeyChar() - the character of the key that was pressed.
    # The runtime had no getKeyChar() at all, so a Canvas that draws its own
    # editor (and asks for the character in keyPressed()) got a
    # NoSuchMethodError on the first key press and lost the input.
    add_test(NAME check_keychar
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_keychar.sh
                     ${NJ_FIXTURES}/keychardemo.jar)
    # The Nokia UI API is the vendor surface the corpus leans on hardest:
    # 83 titles reference FullCanvas, 74 DirectGraphics, 73 DirectUtils, 14
    # Sound and 10 DeviceControl.  A vendor class that is missing is caught by
    # the title and worked around, but a vendor METHOD that is missing is a
    # NoSuchMethodError in the middle of a frame - which is what the audit of
    # the javadoc turned up (thirteen of them, one per family).
    add_test(NAME check_nokiademo
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_nokiademo.sh
                     ${NJ_FIXTURES}/nokiademo.jar)
    add_test(NAME check_corpus_size_retry
             COMMAND ${CMAKE_COMMAND} -E env
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_corpus_size_retry.sh
                     ${NJ_FIXTURES}/sizedemo.jar)
    set_tests_properties(check_corpus_size_retry PROPERTIES TIMEOUT 300)
    add_test(NAME check_flat_wording
             COMMAND ${CMAKE_COMMAND} -E env
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_flat_wording.sh
                     ${NJ_FIXTURES}/flatdemo.jar)
    set_tests_properties(check_flat_wording PROPERTIES TIMEOUT 300)
    # The container probe against the .m3g files real games ship.  Skips when
    # the corpus is absent (tests/games/ is gitignored), which is why it is
    # registered unconditionally: a clone without games still has to configure.
    add_test(NAME check_m3g_reals
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_M3G_PROBE=$<TARGET_FILE:nj_m3g_probe>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_m3g_reals.sh)
    set_tests_properties(check_m3g_reals PROPERTIES TIMEOUT 900)
    add_test(NAME check_hang_teardown
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_hang_teardown.sh
                     ${NJ_FIXTURES}/hangdemo.jar)
    set_tests_properties(check_hang_teardown PROPERTIES TIMEOUT 120)

    add_test(NAME check_stack_trace
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_stack_trace.sh
                     ${NJ_FIXTURES}/excdemo.jar)
    set_tests_properties(check_stack_trace PROPERTIES TIMEOUT 120)

    # A title that throws from paint() on every frame must still stop at the
    # frame budget; the budget used to be compared against frames painted.
    add_test(NAME check_paint_budget
             COMMAND ${CMAKE_COMMAND} -E env
                     NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_paint_budget.sh
                     ${NJ_FIXTURES}/paintthrow.jar)
    set_tests_properties(check_paint_budget PROPERTIES TIMEOUT 120)
endif()

add_test(NAME check_splash_budget
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_splash_budget.sh
                     $<TARGET_FILE:nj2me-cli> ${NJ_FIXTURES}/blockdemo.jar)
    set_tests_properties(check_splash_budget PROPERTIES TIMEOUT 120)
  endif()

  # Both of these drive generated JARs, and nj_add_test() deliberately keeps
  # nj_fixtures out of a plain build (see the comment there).  Register them
  # only when the fixture is actually on disk: on a fresh clone without
  # `--target fixtures` they would otherwise fail with "no fixture at ...",
  # which reads as a runtime defect when nothing was ever run.  A test that
  # did not run must not look like a test that failed.
  if(EXISTS ${NJ_FIXTURES}/gamedemo.jar)
    add_test(NAME test_observed_ports
             COMMAND ${CMAKE_COMMAND} -E env NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_observed.sh
                     ${NJ_FIXTURES}/gamedemo.jar)
    set_tests_properties(test_observed_ports PROPERTIES TIMEOUT 120)
  endif()
  if(EXISTS ${NJ_FIXTURES}/loopdemo.jar)
    # A MIDlet that runs its own frame loop inside startApp() must still
    # present frames; 仙剑奇侠传完美版 does exactly that.
    add_test(NAME test_loop_frames
             COMMAND ${CMAKE_COMMAND} -E env NJ_BIN=$<TARGET_FILE:nj2me-cli>
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_loopdemo.sh
                     ${NJ_FIXTURES}/loopdemo.jar)
    set_tests_properties(test_loop_frames PROPERTIES TIMEOUT 120)
  endif()
  nj_add_test(test_directgraphics tests/test_directgraphics.c)
nj_add_test(test_clip tests/test_clip.c)
  nj_add_test(test_blocked_init tests/test_blocked_init.c ${NJ_FIXTURES})
  # A quit request is an end, not a failed construction: closing the port's
  # window while a MIDlet is still in its constructor (Doom 3 runs the whole
  # game there) must report the stop, not a java/lang/InternalError.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_quit_stop.c")
    nj_add_test(test_quit_stop tests/test_quit_stop.c ${NJ_FIXTURES})
  else()
    message(WARNING "tests/test_quit_stop.c is absent; skipping test_quit_stop")
  endif()
  nj_add_test(test_vm        tests/test_vm.c
              ${NJ_FIXTURES}/hello.jar HelloWorld "wideargs ok")

  # A malformed class file must come back as a Java exception - never as a
  # crash and never as a hang.  The fixture is checked in (tests/fixtures/ is
  # generated, so it is force-added) and tools/make_fuzz_fixture.sh rebuilds
  # it byte-for-byte; see tests/test_class_hardening.c for the mechanisms.
  if(EXISTS ${NJ_FIXTURES}/hardening_malformed.jar)
    nj_add_test(test_class_hardening tests/test_class_hardening.c
                ${NJ_FIXTURES}/hardening_malformed.jar)
  endif()

  # The JIT engine seam: the null engine must be observationally identical to
  # the interpreter, and an installed engine must actually be entered.  The test
  # self-skips when NJ_ENABLE_ENGINE=0, so both configurations stay green.
  if(EXISTS ${NJ_FIXTURES}/bench.jar)
    nj_add_test(test_engine tests/test_engine.c ${NJ_FIXTURES}/bench.jar)
  endif()

  # ---- the formats ----
  nj_add_test(test_zip       tests/test_zip.c ${NJ_FIXTURES})
  nj_add_test(test_codec     tests/test_codec.c)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gif.c)
    nj_add_test(test_gif tests/test_gif.c)
  endif()

  # Malformed class files must produce a clean error, not a crash and not a
  # hang.  Small case count so it stays a smoke test; run the tool directly with
  # a larger number to fuzz properly.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/fuzz_classfile.py)
    add_test(NAME fuzz_classfile
             COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/fuzz_classfile.py
                     $<TARGET_FILE:nj2me-cli> 24)
    set_tests_properties(fuzz_classfile PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 300)
  endif()

  # Every nj_field_lookup() must name a field its class declares.  A lookup
  # that asks for the wrong descriptor answers NULL, and the native built on
  # it then silently does nothing - which is how a Light came to be
  # unconstructable and Node.setAlphaFactor() came to set nothing.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_field_descs.py)
    add_test(NAME check_field_descs
             COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_field_descs.py)
    set_tests_properties(check_field_descs PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 120)
  endif()

  # The JSR 184 API surface is enumerated in tools/data/m3g-api-1.1.txt and
  # compared with what src/midp/nj_lib_m3g.c declares; the audit exits non-zero
  # for the first member that is missing, so this test is what keeps "the whole
  # package is implemented" true.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/m3g_api_audit.py)
    add_test(NAME check_m3g_api
             COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/m3g_api_audit.py)
    set_tests_properties(check_m3g_api PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 120)
  endif()
  # The core API audit: every CLDC 1.1 / MIDP 2.0 member the reference declares
  # has to resolve in this runtime's own tables, or be listed as an accepted
  # gap in tools/data/api-allow.txt.  A missing member is not a link error at
  # run time - it is a NoSuchMethodError the title hits later.
  add_test(NAME check_api_audit
           COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/api_audit.py)
  # Every shared runtime source in the desktop build must also be in the
  # hand-written ESP-IDF source list.  That list went stale when the avcodec
  # split, the datagram connection and the PIM/Samsung modules landed: the
  # ESP32-S3 image stopped linking and nobody noticed until it was rebuilt.
  add_test(NAME check_port_sources
           COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_port_sources.py)
  # The audit is a text pass over every source unit and takes ~50 s on an idle
  # machine; under a loaded one it crossed the old 120 s ceiling and failed a
  # whole suite run as a timeout.  300 s leaves room to spare without hiding a
  # hang.
  set_tests_properties(check_api_audit PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 300)

  # ... and the surface has to behave, not merely exist.  M3GDemo calls every
  # API area and prints one summary line; check_m3gdemo.sh asserts it.
  add_test(NAME check_m3gdemo
           COMMAND ${CMAKE_COMMAND} -E env
                   NJ_BIN=$<TARGET_FILE:nj2me-cli>
                   ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_m3gdemo.sh
                   ${NJ_FIXTURES}/m3gdemo.jar)

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_consts.c)
    nj_add_test(test_consts tests/test_consts.c
                ${NJ_FIXTURES}/constdemo.expected)
  endif()

  # The class/field memo is an optimisation with a correctness shadow: a hit
  # is only taken when the names still match.  test_lookup_cache pins that,
  # the per-VM reset and the counters a benchmark reads.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lookup_cache.c)
    nj_add_test(test_lookup_cache tests/test_lookup_cache.c)
  endif()

  # Only when the two downloaded titles are actually there: NJ_FIXTURES_FETCH_GAMES
  # is off by default, and a test whose fixture is missing fails for a reason
  # that has nothing to do with the emulator.  A gate that is red on a plain
  # build is worse than no gate - it teaches people to ignore it.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_poke.c AND
     EXISTS ${NJ_FIXTURES}/slimevolley.jar AND EXISTS ${NJ_FIXTURES}/game15.jar)
    nj_add_test(test_poke tests/test_poke.c
                ${NJ_FIXTURES}/slimevolley.jar ${NJ_FIXTURES}/game15.jar)
  endif()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fs.c)
    nj_add_test(test_fs tests/test_fs.c ${NJ_FIXTURES}/fsdemo.jar)
  endif()

  # The other half of the filesystem story: test_fs brings its own port and
  # proves the runtime's path rules, this one uses the headless port and proves
  # that a MIDlet's saves are observable from outside it.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_headless_fs.c)
    nj_add_test(test_headless_fs tests/test_headless_fs.c
                ${NJ_FIXTURES}/gamedemo.jar)
  endif()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_port_code.c)
    nj_add_test(test_port_code tests/test_port_code.c)
  endif()

  # The rule that decides whether the box a port detected is a canvas at all.
  # It needs no fixture and no port: it is a table and a distance, which is why
  # it lives in core rather than inside the Android blit that uses it.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_detect.c)
    nj_add_test(test_detect tests/test_detect.c)
    nj_add_test(test_speed tests/test_speed.c)
    nj_add_test(test_timer tests/test_timer.c)
    nj_add_test(test_timer_fairness tests/test_timer_fairness.c)
    nj_add_test(test_thread_lifecycle tests/test_thread_lifecycle.c)
  endif()

  # Save state / restore state: runs the fielddemo fixture in one VM, saves it
  # mid-run and resumes it in a second VM in the same process.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_save.c)
    nj_add_test(test_save tests/test_save.c)
  endif()

  # The desktop client, headlessly: a JAR's name and icon, the settings file,
  # and the library model's keys and hit-testing are all plain C, so they are
  # tested without a window or a display.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_client_config.c)
    nj_add_test(test_client_config tests/test_client_config.c)
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_library.c)
    nj_add_test(test_library tests/test_library.c)
  endif()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_client_ui.c)
    nj_add_test(test_client_ui tests/test_client_ui.c)
  endif()
  # A drop is only accepted when the file is a JAR with a MIDlet in it; the
  # port validates it so a stray file can never end a running game.
  if(SDL2_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_port_drop.c)
    nj_add_test(test_port_drop tests/test_port_drop.c)
    # The port is SDL2's window: the drop test pushes an SDL_DROPFILE through
    # the real event queue, so it needs the same headers and library the port
    # itself was built against (nj2me's copy is PRIVATE).
    target_include_directories(test_port_drop PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(test_port_drop PRIVATE ${SDL2_LIBRARIES})
  endif()

  # Remappable game keys and the display knobs the desktop settings reach: the
  # lookup is exercised without video events, then once through a real
  # SDL_KEYDOWN so the event path is not a second, untested mapping.
  if(SDL2_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_port_binds.c)
    nj_add_test(test_port_binds tests/test_port_binds.c)
    target_include_directories(test_port_binds PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(test_port_binds PRIVATE ${SDL2_LIBRARIES})
  endif()

  if(SDL2_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_port_gamepad.c)
    nj_add_test(test_port_gamepad tests/test_port_gamepad.c)
    target_include_directories(test_port_gamepad PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(test_port_gamepad PRIVATE ${SDL2_LIBRARIES})
  endif()

  # Multidimensional arrays through real bytecode: tests/arrays/ArrayDims.java
  # builds int[3][7], int[2][4][5] and a ragged table, and the C side only reads
  # the static method's result.  The class bytes are checked in, so no Java
  # toolchain is needed here.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arrays.c)
    nj_add_test(test_arrays tests/test_arrays.c)
    target_include_directories(test_arrays PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  endif()

  foreach(extra_test test_font_png test_jpeg test_lcdui test_game test_lib
                      test_ioapi test_resolution test_sax test_rms test_run_archive
                      test_nokia_sound test_nokia_touch test_datagram)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${extra_test}.c)
      nj_add_test(${extra_test} tests/${extra_test}.c)
    endif()
  endforeach()

  if(NJ_ENABLE_M3G)
    nj_add_test(test_m3g_load_memory tests/test_m3g_load_memory.c)
  endif()

  # The MIDI test drives the built-in renderer directly, so it needs that
  # renderer to be part of the library it links against.
  if(NJ_ENABLE_MIDI AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_midi.c)
    nj_add_test(test_midi tests/test_midi.c)
    nj_add_test(test_media_backpressure tests/test_media_backpressure.c)
    nj_add_test(test_media_replay tests/test_media_replay.c)
    if(NJ_ENABLE_H263)
        nj_add_test(test_h263 tests/test_h263.c)
    endif()
    if(NJ_ENABLE_MP3)
        nj_add_test(test_mp3 tests/test_mp3.c)
    endif()
    nj_add_test(test_avcodec tests/test_avcodec.c)
    if(NJ_ENABLE_M4V)
        nj_add_test(test_m4v tests/test_m4v.c)
    endif()
  endif()

  # The rasterizer test is deliberately standalone: it compiles nj_gfx.c with
  # tests/stub_font.c rather than linking the library, so its text assertions
  # are measured against a fixed, deterministic glyph set instead of the real
  # bitmap font.  Linking the library instead would substitute the real
  # renderer and make those assertions meaningless - which is exactly what
  # happened before this rule existed.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gfx.c)
    add_executable(test_gfx tests/test_gfx.c
                   src/gfx/nj_gfx.c tests/stub_font.c)
    target_include_directories(test_gfx PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core
      ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx)
    target_link_libraries(test_gfx PRIVATE m)
    add_test(NAME test_gfx COMMAND test_gfx)
    set_tests_properties(test_gfx PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  # Antialiased text, with the rasteriser taken out of the picture.  test_gfx
  # above pins the renderer against a stub font; test_ttf later pins the
  # TrueType rasteriser.  This one is the third piece - the coverage path those
  # two meet in - and it hands the renderer coverage values it chose itself, so
  # a wrong pixel here means the renderer is wrong and not the rasteriser.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_font_coverage.c)
    nj_add_test(test_font_coverage tests/test_font_coverage.c)
  endif()

  # A port can hand the font set its own glyphs instead of shipping a table,
  # which is how the Android port drops the built-in CJK face.  This one is
  # standalone on purpose: the library is built with NJ_ENABLE_FONT_CJK=1, and
  # with the built-in table present a provider is never consulted for a wide
  # character - which is precisely the behaviour the test has to observe.  It
  # compiles nj_font.c by itself with the table off, so the assertion does not
  # depend on how the rest of the build was configured.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fallback_font.c)
    add_executable(test_fallback_font
                   tests/test_fallback_font.c src/gfx/nj_font.c)
    target_compile_definitions(test_fallback_font PRIVATE NJ_ENABLE_FONT_CJK=0)
    target_include_directories(test_fallback_font PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core
      ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx)
    add_test(NAME test_fallback_font COMMAND test_fallback_font)
    set_tests_properties(test_fallback_font PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  # The third piece, and the one that makes the other two mean something: the
  # TrueType rasteriser on its own, compared glyph by glyph against FreeType
  # through Pillow.  Three things have to hold, and each is a real skip rather
  # than a failure: the rasteriser must be compiled in (NJ_ENABLE_TTF), the
  # test must exist, and the hand-built fixture faces in tests/fixtures/ttf
  # must be on disk (make them with `--target nj_fixtures`, or
  # tools/gen_ttf_fixture.py).  The FreeType comparison reports SKIP by itself
  # when Pillow is missing; a missing fixture directory is a hard error in the
  # test, which would read as a rasteriser defect when nothing was run.
  if(NJ_ENABLE_TTF
     AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ttf.c
     AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ttf)
    nj_add_test(test_ttf tests/test_ttf.c)
  endif()

  # ---- audio: what the MIDlet asked for AND what the port received ----
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio.c)
    nj_add_test(test_audio tests/test_audio.c ${NJ_FIXTURES}/audiodemo.jar)
  endif()

  # The AMR decoder under test_audio's feet: frame walking and every speech
  # mode, against samples another implementation of the same standard produced.
  if(NJ_ENABLE_AMR AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_amr.c)
    nj_add_test(test_amr tests/test_amr.c)
  endif()

  # The same decoder reached through an MP4/3GP 'samr' track instead of the
  # "#!AMR\n" layout: synthetic containers only, including the refusals that
  # keep a hand-edited sample table from walking past its mdat.
  if(NJ_ENABLE_AMR AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_amr_mp4.c)
    nj_add_test(test_amr_mp4 tests/test_amr_mp4.c)
  endif()

  # ---- end to end: run a whole MIDlet and assert on what it rendered ----
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_play.c)
    nj_add_test(test_play tests/test_play.c ${NJ_FIXTURES}/gamedemo.jar)
nj_add_test(test_fullscreen tests/test_fullscreen.c ${NJ_FIXTURES}/fsorder.jar)

  # Does input actually reach the game?  This is the only test that can tell a
  # MIDlet which responds to the keypad from one that is merely alive, and it
  # needs the keysdemo fixture because a real game animates on its own and
  # would make the comparison meaningless.
  if(NJ_JAVAC)
    add_test(NAME test_input_effect
      COMMAND ${CMAKE_COMMAND} -E env NJ_FRAMES=400 NJ_INKEY=150
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_input_effect.sh
              ${NJ_FIXTURES}/keysdemo.jar)
    set_tests_properties(test_input_effect PROPERTIES TIMEOUT 180)
    # Pointer input, from the same fixture: the script drives a press, two
    # drags and a release, and the check asserts that a drag reaches
    # pointerDragged rather than arriving as another pointerPressed - which is
    # what it did until the event kind stopped being tested for truthiness.
    add_test(NAME test_pointer
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_pointer.sh
              ${NJ_FIXTURES}/keysdemo.jar)
    set_tests_properties(test_pointer PROPERTIES TIMEOUT 180)
    # ... and the third question, which neither of the two above can ask: not
    # "did a key arrive" but "did the RIGHT key arrive".  Every arrow and FIRE
    # press used to reach the MIDlet as the port's own enum number (UP = 13)
    # instead of the MIDP code (UP = -1), because nj_midp.h's MIDP macros
    # shared their names with the nj_port.h enum and shadowed it.  The fixture
    # prints the code it was handed, so this compares against Canvas's numbers.
    add_test(NAME test_key_codes
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_key_codes.sh
              ${NJ_FIXTURES}/keysdemo.jar)
    set_tests_properties(test_key_codes PROPERTIES TIMEOUT 180)
  endif()
    set_tests_properties(test_play PROPERTIES TIMEOUT 120)
  endif()
endif()

# ---------------------------------------------------------------------
# The no-mutable-state gate, as a build target and a test.
# ---------------------------------------------------------------------
find_program(NJ_BASH bash)
if(NJ_BASH)
  add_custom_target(no-statics
    COMMAND ${NJ_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_statics.sh
    COMMENT "Verifying the runtime keeps no mutable global state"
    VERBATIM)
  if(NJ_BUILD_TESTS)
    add_test(NAME no_statics
             COMMAND ${NJ_BASH}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_statics.sh)
  endif()
endif()

# ---------------------------------------------------------------------
# The libretro core is exercised through its real ABI: the mock frontend in
# tests/test_libretro_frontend.c dlopen()s the shared object, plays the part of
# RetroArch, and asserts on what the core reported, drew and played.
# tools/check_libretro_frontend.sh compiles it (with the same CFLAGS a frontend
# would use) and runs the matrix; the target below only exists so a compile
# error is a build error, not a mysterious test failure.
# ---------------------------------------------------------------------
if(NJ_BUILD_TESTS AND NJ_BUILD_LIBRETRO AND UNIX
   AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_libretro_frontend.c)
  add_executable(test_libretro_frontend tests/test_libretro_frontend.c)
  target_include_directories(test_libretro_frontend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/port/libretro)
  target_link_libraries(test_libretro_frontend PRIVATE ${CMAKE_DL_LIBS})

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_libretro_frontend.sh)
    # The harness executable dlopen()s the core.  When the core is built with
    # -DNJ_SANITIZE=ON the harness must be built with the same flags: an
    # ASan-instrumented .so can only be loaded by a process whose ASan runtime
    # came first, and a plain executable would fail with "ASan runtime does not
    # come first in initial library list".
    if(NJ_SANITIZE)
      set(NJ_HARNESS_CFLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
    else()
      set(NJ_HARNESS_CFLAGS "")
    endif()
    add_test(NAME test_libretro_frontend
             COMMAND ${CMAKE_COMMAND} -E env
                     CORE=$<TARGET_FILE:nano_j2me_libretro>
                     "HARNESS_CFLAGS=${NJ_HARNESS_CFLAGS}"
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_libretro_frontend.sh)
    set_tests_properties(test_libretro_frontend PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      TIMEOUT 300)
  endif()
endif()

message(STATUS "nano_j2me ${PROJECT_VERSION}: "
               "JPEG=${NJ_ENABLE_JPEG} PNG=${NJ_ENABLE_PNG} "
               "pixfmt=${NJ_PIXFMT} tests=${NJ_BUILD_TESTS}")

# ---------------------------------------------------------------------------
# An nj_value argument array must have room for every slot its descriptor names.
# vm_invoke copies m->arg_slots entries out of it and a long or double occupies
# two, so a one-entry array handed to a "(J)V" callee reads past the end.  That
# is what AddressSanitizer caught in Long.valueOf(String, int) - the read is
# usually harmless because the callee only looks at the first entry, so running
# the code does not find it.
# ---------------------------------------------------------------------------
add_test(NAME check_arg_slots
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_arg_slots.py)

# ---------------------------------------------------------------------------
# ... and the INDEX into those slots.
#
# check_arg_slots above verifies the array is big enough.  It says nothing about
# which entry a native reads: `args` points at raw operand slots, so in
# Math.max(long, long) the second argument is at index 2, not 1.  Reading it
# from 1 splices the first argument's high half onto the second's low half -
# Math.max(0L, 60L) returned 0, and Thread.sleep(Math.max(0 - elapsed, 60L))
# became a 4,400-year sleep that froze a whole game.  No C-level test can see
# it, because nj_call() packs one value per slot and therefore agrees with the
# wrong index; the check has to read the source.
# ---------------------------------------------------------------------------
add_test(NAME check_wide_args
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_wide_args.py)
