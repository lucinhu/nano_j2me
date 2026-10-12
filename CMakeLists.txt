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
set(NJ_ENABLE_RMS   1 CACHE STRING "RecordStore persistence")
set(NJ_ENABLE_IO    1 CACHE STRING "java.io and javax.microedition.io")
# Floating point.  This switch was documented in nj_config.h from the start and
# broken in TWO independent ways: nothing in src/ read it, and CMake never
# passed it to the compiler at all - so -DNJ_ENABLE_FLOAT=0 built a runtime that
# still linked every libm transcendental.  A configuration option that lies is
# worse than one that is missing.
set(NJ_ENABLE_M3G 1 CACHE STRING "Build JSR 184 (Mobile 3D Graphics)")
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
  src/midp/nj_lib_content.c
  src/midp/nj_lib_nokia.c
  src/midp/nj_lib_fc.c
  src/midp/nj_lib_wireless.c
  src/midp/nj_lib_m3g.c
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

target_compile_options(nj2me PRIVATE -Wall -Wextra)
if(NJ_WERROR)
  target_compile_options(nj2me PRIVATE -Werror)
endif()

target_compile_definitions(nj2me PUBLIC
  NJ_PIXFMT=${NJ_PIXFMT}
  NJ_ENABLE_PNG=${NJ_ENABLE_PNG}
  NJ_ENABLE_JPEG=${NJ_ENABLE_JPEG}
  NJ_ENABLE_RMS=${NJ_ENABLE_RMS}
  NJ_ENABLE_IO=${NJ_ENABLE_IO}
  NJ_ENABLE_FLOAT=${NJ_ENABLE_FLOAT}
  NJ_ENABLE_M3G=${NJ_ENABLE_M3G}
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
  nj_add_test(test_wireless tests/test_wireless.c)

  # What the PORT observed, not what the VM counted: threads, GC, audio and
  # file activity on one machine-readable line.  Skipped without a fixture.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_late_display.sh)
    add_test(NAME check_late_display
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_late_display.sh
                     $<TARGET_FILE:nj2me-cli> ${NJ_FIXTURES}/latedemo.jar)
    set_tests_properties(check_late_display PROPERTIES TIMEOUT 120)
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
  nj_add_test(test_blocked_init tests/test_blocked_init.c ${NJ_FIXTURES})
  nj_add_test(test_vm        tests/test_vm.c
              ${NJ_FIXTURES}/hello.jar HelloWorld "hello from j2me")

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
