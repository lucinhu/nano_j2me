# Cross-compile a Linux libretro core with `zig cc`.
#
# Zig ships a self-contained C compiler and libc headers/stubs for several
# Linux triples, which is enough for this core: it is plain C99 and its only
# system dependencies are libc, libm and pthreads.  Use it like this:
#
#     cmake -S . -B build/libretro-linux-aarch64 -G Ninja \
#           -DCMAKE_TOOLCHAIN_FILE=tools/cmake/toolchain-zig-linux.cmake \
#           -DNJ_ZIG_TARGET=aarch64-linux-gnu.2.17 \
#           -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON \
#           -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_CLI=OFF -DNJ_BUILD_SDL2=OFF \
#           -DNJ_BUILD_EMBEDDED_PORTS=OFF -DNJ_BUILD_POSIX_NETWORK=OFF
#
# `tools/build_libretro_cores.sh` does this for you; the file is kept separate
# so the invocation is reproducible without the script.
#
# Zig's glibc targets link against the version stubs Zig bundles, so the
# resulting core runs on a normal glibc distro (Raspberry Pi OS, Armbian,
# Anbernic/Batocera-style handheld firmware).  Pin the baseline with a suffix
# such as aarch64-linux-gnu.2.17 - the default without it is much newer and
# the core then fails to load on older firmware with a GLIBC symbol error.
# Use *-musl triples only if the target really is musl; RetroArch builds in
# the handheld world are glibc.
set(CMAKE_SYSTEM_NAME Linux)

# CMake's compiler-ABI probe runs in a *nested* try_compile project that only
# inherits the toolchain file and the variables listed here - a plain -D cache
# entry does not cross that boundary.  Without this line configure dies with
# "CMAKE_C_COMPILER not set" / "needs -DNJ_ZIG_TARGET".
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES NJ_ZIG_TARGET)

if(NOT NJ_ZIG_TARGET)
  message(FATAL_ERROR "toolchain-zig-linux.cmake needs -DNJ_ZIG_TARGET=<triple> "
                      "(for example aarch64-linux-gnu or arm-linux-gnueabihf)")
endif()

if(NJ_ZIG_TARGET MATCHES "^aarch64")
  set(CMAKE_SYSTEM_PROCESSOR aarch64)
elseif(NJ_ZIG_TARGET MATCHES "^arm")
  set(CMAKE_SYSTEM_PROCESSOR arm)
elseif(NJ_ZIG_TARGET MATCHES "^riscv64")
  set(CMAKE_SYSTEM_PROCESSOR riscv64)
elseif(NJ_ZIG_TARGET MATCHES "^x86_64")
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
else()
  set(CMAKE_SYSTEM_PROCESSOR "${NJ_ZIG_TARGET}")
endif()

find_program(NJ_ZIG_EXECUTABLE NAMES zig)
if(NOT NJ_ZIG_EXECUTABLE)
  message(FATAL_ERROR "zig was not found in PATH (install it, or point "
                      "CMAKE_C_COMPILER at zig yourself)")
endif()

set(CMAKE_C_COMPILER "${NJ_ZIG_EXECUTABLE}")
set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_C_FLAGS_INIT "-target ${NJ_ZIG_TARGET}")

# CMake's compiler and feature probes would otherwise try to *link and run* a
# test program; a cross build cannot run one.  A static library proves the
# compiler works without needing the target's runtime here.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# There is no pkg-config for the target and no host libraries may leak in; the
# build script disables ZLIB explicitly as well.  These make an accidental
# find_package() fail instead of linking a host .so into a foreign core.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
