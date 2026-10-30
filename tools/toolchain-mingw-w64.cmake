# =====================================================================
#  Cross-compile the nano_j2me desktop client for 64-bit Windows with
#  mingw-w64.
#
#      cmake -S . -B build-windows \
#            -DCMAKE_TOOLCHAIN_FILE=tools/toolchain-mingw-w64.cmake \
#            -DSDL2_INCLUDE_DIRS=<sdl2>/x86_64-w64-mingw32/include \
#            -DSDL2_LIBRARIES=<sdl2>/x86_64-w64-mingw32/lib/libSDL2.dll.a
#      cmake --build build-windows --target nj2me-cli
#
#  The SDL2 development package is the mingw one published at
#  https://github.com/libsdl-org/SDL/releases (SDL2-devel-<version>-mingw.tar.gz,
#  unpacked into .local-tools/sdl2-mingw/).  tools/build_windows.sh fetches it
#  when it is missing and passes both variables above; the GLOB below is only
#  a convenience so a plain `cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=...`
#  also finds the tree once it has been unpacked.
#
#  Why a cross build and not MSVC: there is no MSVC installation here, and
#  mingw-w64 is the same GCC the project is developed against, so the C99 code
#  and the -Wall -Wextra -Werror=format gate behave identically.
# =====================================================================
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)
set(CMAKE_AR           x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB       x86_64-w64-mingw32-ranlib)
set(CMAKE_STRIP        x86_64-w64-mingw32-strip)

# Do not make the host's Linux libraries visible to find_package()/find_library():
# a host zlib or SDL2 would link against the wrong architecture.  Exception: the
# SDL2 tree unpacked under .local-tools/ is a *target* package, so it is added
# to the root path below.
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
file(GLOB NJ_SDL2_MINGW_DIRS
     "${CMAKE_CURRENT_LIST_DIR}/../.local-tools/sdl2-mingw/SDL2-*/x86_64-w64-mingw32")
if(NJ_SDL2_MINGW_DIRS)
  list(LENGTH NJ_SDL2_MINGW_DIRS _nj_sdl2_n)
  math(EXPR _nj_sdl2_last "${_nj_sdl2_n} - 1")
  list(GET NJ_SDL2_MINGW_DIRS ${_nj_sdl2_last} _nj_sdl2_dir)
  list(APPEND CMAKE_FIND_ROOT_PATH "${_nj_sdl2_dir}")
  set(NJ_SDL2_MINGW_DIR "${_nj_sdl2_dir}" CACHE PATH
      "The x86_64-w64-mingw32/ directory of the unpacked SDL2 mingw package")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config is a host tool and does not know about the cross prefix: on a
# normal Linux box it finds /usr/lib/libSDL2 and hands back -I/usr/include and
# -lSDL2, i.e. the wrong architecture.  Point it only at the target prefix so a
# cross configure either finds a real target sdl2.pc or falls through to the
# -DSDL2_INCLUDE_DIRS/-DSDL2_LIBRARIES cache values in CMakeLists.txt.
#
# The SDL2 mingw tarball ships its own sdl2.pc, but that one has a stale
# /tmp/tardir prefix baked in and hardcodes "-mwindows -lSDL2main -Dmain=SDL_main"
# (a GUI-subsystem exe); the Windows release is a console-subsystem exe with a
# normal main(), so that file is deliberately not used.
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-w64-mingw32/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

# Convenience for a future `-DNJ_BUILD_TESTS=ON` cross configure: the built
# test binaries are Windows PE files and need wine to run.  The release build
# turns tests off, so this is not required.
find_program(NJ_WINE_EXECUTABLE NAMES wine wine64)
if(NJ_WINE_EXECUTABLE)
  set(CMAKE_CROSSCOMPILING_EMULATOR "${NJ_WINE_EXECUTABLE}" CACHE FILEPATH
      "Emulator used to run cross-built test binaries")
endif()

# The executable is a normal console program (main(), not WinMain), so every
# printf reaches a console window.  -static keeps the compiler runtime
# (libgcc, libwinpthread) inside the exe, so the only DLL the release needs is
# SDL2.dll; without it mingw links libwinpthread-1.dll and the exe fails to
# start on a clean machine.
set(CMAKE_C_FLAGS_INIT "-DWIN32_LEAN_AND_MEAN -DNOMINMAX")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -Wl,--enable-auto-import")

# No CMake host tools are executed during a cross build; tests need wine, so
# tools/build_windows.sh passes -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF.
