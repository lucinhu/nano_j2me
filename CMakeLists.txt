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
project(nano_j2me VERSION 0.1.0 LANGUAGES C)

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
option(NJ_BUILD_SDL2     "Build the SDL2 desktop port"      ON)
option(NJ_BUILD_EMBEDDED_PORTS
       "Also compile the Android and ESP32 port sources (they stub themselves \
outside their own platform, so this costs a compile and nothing else)" ON)
option(NJ_WERROR         "Treat warnings as errors"         OFF)
option(NJ_SANITIZE       "Build with ASan + UBSan"          OFF)

set(NJ_ENABLE_JPEG  1 CACHE STRING "Built-in baseline JPEG decoder")
set(NJ_ENABLE_TTF   1 CACHE STRING "TrueType text (0 = bitmap faces only)")
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
set(NJ_ENABLE_AMR   1 CACHE STRING "Built-in AMR narrowband speech decoder")
# The built-in MIDI renderer.  A port whose platform plays MIDI itself (Android
# hands the file to the system synthesizer) can drop it: what is lost is the
# fallback for a device without that synthesizer, and MIDIControl's single note
# events, which a platform player cannot be sent.
set(NJ_ENABLE_MIDI  1 CACHE STRING "Built-in MIDI renderer (0 = platform only)")
# Floating point.  This switch was documented in nj_config.h from the start and
# broken in TWO independent ways: nothing in src/ read it, and CMake never
# passed it to the compiler at all - so -DNJ_ENABLE_FLOAT=0 built a runtime that
# still linked every libm transcendental.  A configuration option that lies is
# worse than one that is missing.
set(NJ_ENABLE_M3G 1 CACHE STRING "Build JSR 184 (Mobile 3D Graphics)")
set(NJ_ENABLE_MASCOT 1 CACHE STRING "Build JSR 184 (Mobile 3D Graphics)")
set(NJ_ENABLE_FLOAT 1 CACHE STRING "libm transcendentals for java.lang.Math")
set(NJ_PIXFMT       "NJ_PIXFMT_RGB565" CACHE STRING
    "Framebuffer pixel format: NJ_PIXFMT_RGB565, NJ_PIXFMT_RGB888 or NJ_PIXFMT_ARGB8888")

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
  src/core/nj_app.c
  src/core/nj_builtins.c
  src/core/nj_device.c
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
  src/midp/nj_lib_lcdui.c
  src/midp/nj_lib_game.c
  src/midp/nj_lib_rms.c
  src/midp/nj_lib_midlet.c
  src/midp/nj_lib_media.c
  src/midp/nj_midi.c
  src/midp/nj_amr.c
  src/midp/nj_lib_content.c
  src/midp/nj_lib_nokia.c
  src/midp/nj_lib_fc.c
  src/midp/nj_lib_wireless.c
  src/midp/nj_lib_m3g.c
  src/midp/nj_lib_mascot.c
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
)

add_library(nj2me STATIC ${NJ_SOURCES})
add_library(nj2me::nj2me ALIAS nj2me)

target_include_directories(nj2me
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

# The headless port inflates .m3g sections through the nj_port.inflate hook,
# so the library needs zlib when it is there.  A build without zlib still
# compiles: the hook is then simply not filled in.
find_package(ZLIB)
if(ZLIB_FOUND)
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
  NJ_ENABLE_PNG=${NJ_ENABLE_PNG}
  NJ_ENABLE_GIF=${NJ_ENABLE_GIF}
  NJ_ENABLE_INFLATE=${NJ_ENABLE_INFLATE}
  NJ_ENABLE_WAV=${NJ_ENABLE_WAV}
  NJ_ENABLE_JPEG=${NJ_ENABLE_JPEG}
  NJ_ENABLE_RMS=${NJ_ENABLE_RMS}
  NJ_ENABLE_IO=${NJ_ENABLE_IO}
  NJ_ENABLE_AMR=${NJ_ENABLE_AMR}
  NJ_ENABLE_MIDI=${NJ_ENABLE_MIDI}
  NJ_ENABLE_FLOAT=${NJ_ENABLE_FLOAT}
  NJ_ENABLE_M3G=${NJ_ENABLE_M3G}
    NJ_ENABLE_MASCOT=${NJ_ENABLE_MASCOT}
  NJ_ENABLE_TTF=${NJ_ENABLE_TTF})

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
  set_target_properties(nj2me-cli PROPERTIES OUTPUT_NAME nj2me)
  install(TARGETS nj2me-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS nj2me ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/nj2me DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_flatdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_excdemo_jar.sh
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_paintthrow_jar.sh
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

  # ---- the memory contract and the class library ----
  nj_add_test(test_lifecycle tests/test_lifecycle.c)
  # Every method a C-implemented class declares must have a native behind it.
  # Three such bindings were missing and each one only surfaced when a real
  # game called it; this walks the whole class library instead of waiting.
  nj_add_test(test_natives   tests/test_natives.c)
  nj_add_test(test_m3g       tests/test_m3g.c)
  nj_add_test(test_mascot    tests/test_mascot.c)
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

  # What the PORT observed, not what the VM counted: threads, GC, audio and
  # file activity on one machine-readable line.  Skipped without a fixture.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_late_display.sh)
    add_test(NAME check_late_display
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_late_display.sh
                     $<TARGET_FILE:nj2me-cli> ${NJ_FIXTURES}/latedemo.jar)
    set_tests_properties(check_late_display PROPERTIES TIMEOUT 120)
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
  nj_add_test(test_vm        tests/test_vm.c
              ${NJ_FIXTURES}/hello.jar HelloWorld "wideargs ok")

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

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_consts.c)
    nj_add_test(test_consts tests/test_consts.c
                ${NJ_FIXTURES}/constdemo.expected)
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

  foreach(extra_test test_font_png test_jpeg test_lcdui test_game test_lib)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${extra_test}.c)
      nj_add_test(${extra_test} tests/${extra_test}.c)
    endif()
  endforeach()

  # The MIDI test drives the built-in renderer directly, so it needs that
  # renderer to be part of the library it links against.
  if(NJ_ENABLE_MIDI AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_midi.c)
    nj_add_test(test_midi tests/test_midi.c)
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
