# =====================================================================
#  Cross-compile nano_j2me for 32-bit Windows with mingw-w64.
#
#      cmake -S . -B build-win32-libretro -G Ninja \
#            -DCMAKE_TOOLCHAIN_FILE=tools/toolchain-mingw-w64-i686.cmake \
#            -DCMAKE_BUILD_TYPE=Release \
#            -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_CLI=OFF -DNJ_BUILD_SDL2=OFF \
#            -DNJ_BUILD_EMBEDDED_PORTS=OFF -DNJ_BUILD_POSIX_NETWORK=OFF \
#            -DNJ_BUILD_LIBRETRO=ON
#      cmake --build build-win32-libretro --target nano_j2me_libretro -j
#
#  This is the 32-bit sibling of tools/toolchain-mingw-w64.cmake, used by
#  `tools/build_libretro_cores.sh windows-i686` for frontends that are still
#  32-bit processes (a 32-bit RetroArch cannot load a PE32+ DLL).
#
#  Why a cross build and not MSVC: there is no MSVC installation here, and
#  mingw-w64 is the same GCC the project is developed against, so the C99 code
#  and the -Wall -Wextra -Werror=format gate behave identically.
# =====================================================================
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_COMPILER   i686-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER  i686-w64-mingw32-windres)
set(CMAKE_AR           i686-w64-mingw32-ar)
set(CMAKE_RANLIB       i686-w64-mingw32-ranlib)
set(CMAKE_STRIP        i686-w64-mingw32-strip)

# Do not make the host's Linux libraries visible to find_package()/find_library():
# a host zlib or SDL2 would link against the wrong architecture.  Exception: the
# SDL2 tree unpacked under .local-tools/ is a *target* package, so it is added
# to the root path below.
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
file(GLOB NJ_SDL2_MINGW_DIRS
     "${CMAKE_CURRENT_LIST_DIR}/../.local-tools/sdl2-mingw/SDL2-*/i686-w64-mingw32")
if(NJ_SDL2_MINGW_DIRS)
  list(LENGTH NJ_SDL2_MINGW_DIRS _nj_sdl2_n)
  math(EXPR _nj_sdl2_last "${_nj_sdl2_n} - 1")
  list(GET NJ_SDL2_MINGW_DIRS ${_nj_sdl2_last} _nj_sdl2_dir)
  list(APPEND CMAKE_FIND_ROOT_PATH "${_nj_sdl2_dir}")
  set(NJ_SDL2_MINGW_DIR "${_nj_sdl2_dir}" CACHE PATH
      "The i686-w64-mingw32/ directory of the unpacked SDL2 mingw package")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config is a host tool and does not know about the cross prefix: point it
# only at the target prefix so a cross configure either finds a real target
# package or falls through to explicit cache values in CMakeLists.txt.
set(ENV{PKG_CONFIG_LIBDIR} "/usr/i686-w64-mingw32/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

# Convenience for a future `-DNJ_BUILD_TESTS=ON` cross configure: the built
# test binaries are Windows PE files and need wine to run.
find_program(NJ_WINE_EXECUTABLE NAMES wine wine64)
if(NJ_WINE_EXECUTABLE)
  set(CMAKE_CROSSCOMPILING_EMULATOR "${NJ_WINE_EXECUTABLE}" CACHE FILEPATH
      "Emulator used to run cross-built test binaries")
endif()

# The executable is a normal console program (main(), not WinMain), so every
# printf reaches a console window.  -static keeps the compiler runtime
# (libgcc, libwinpthread) inside the exe.
set(CMAKE_C_FLAGS_INIT "-DWIN32_LEAN_AND_MEAN -DNOMINMAX")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -Wl,--enable-auto-import")

# 32-bit code can need the GCC 64-bit arithmetic helpers, and mingw's i686 gcc
# defaults to importing them from libgcc_s_dw2-1.dll.  A libretro core is
# dlopen()ed from the frontend's own process, so that DLL would have to be
# installed next to RetroArch; link the GCC runtime statically instead.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -Wl,--enable-auto-import")

# No CMake host tools are executed during a cross build; tests need wine, so
# tools/build_windows.sh passes -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF.
