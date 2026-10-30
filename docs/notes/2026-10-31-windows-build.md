# A Windows release build: mingw-w64, SDL2 and UTF-8 paths (2026-10-31)

0.5.2 ships a Windows x86_64 build for the first time.  The desktop client and
the command-line runner now cross-compile from Linux with mingw-w64 and run
under wine, and the release is one zip: `nj2me.exe`, the `SDL2.dll` it was
linked against, a bilingual `README.txt` and the five sample suites.

## What changed

- `tools/toolchain-mingw-w64.cmake` is the cross toolchain: Windows system
  name, `x86_64-w64-mingw32-gcc`/`windres`, `CMAKE_FIND_ROOT_PATH` over
  `/usr/x86_64-w64-mingw32` and the unpacked SDL2 mingw package, `wine` as
  `CMAKE_CROSSCOMPILING_EMULATOR`, and a static libgcc/libwinpthread link so
  the only DLL a user needs is SDL2.
- `CMakeLists.txt` gained two small things: SDL2 discovery accepts
  `-DSDL2_INCLUDE_DIRS`/`-DSDL2_LIBRARIES` as a fallback when pkg-config finds
  nothing (pkg-config stays first, the native Linux path is unchanged), and the
  CLI links `shell32` for `CommandLineToArgvW`.  The `if(UNIX)` guards stay.
- `src/port/posix/nj_posix_win32.h` collects the Win32 shims in one header:
  UTF-8 <-> UTF-16, wide `fopen`/`stat`/`remove`/`rename`/replace, `mkdir_p`,
  `getcwd`, a `FindFirstFileW` directory iterator returning UTF-8 names,
  `Sleep`, and wide `GetEnvironmentVariable`.  It is also used from
  `src/client/`, which is why `src/port/posix` joined the library's include
  paths.
- `nj_posix_clock.h` uses `localtime_s`/`gmtime_s` on Windows;
  `nj_posix_dir.h` backs the same `nj_posix_dir` struct with `FindFirstFileW`,
  so the headless and SDL2 ports share one directory API on both platforms.
- `nj_port_headless.c` makes JIT code pages with
  `VirtualAlloc(MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE)` ->
  `VirtualProtect(PAGE_EXECUTE_READ)` -> `VirtualFree(MEM_RELEASE)`, sleeps with
  `Sleep`, and opens/stat/removes files through the wide helpers.
- `nj_library.c` scans with `FindFirstFileW` and returns UTF-8 names in the
  same `nj_lib_dir_entry`/scan candidates the Linux build fills, so the
  launcher and `--print-library` needed no new formatting code.
  `nj_client_config.c` uses `%APPDATA%\nano_j2me\client.conf` and
  `%APPDATA%\nano_j2me\games`, and saves with `MoveFileExW(..., REPLACE_EXISTING)`
  because the CRT's `rename` refuses an existing target (the second save would
  otherwise never persist).
- `nj_lib_fc.c` (FileConnection) and `nj_run.c` (direct-run jar read) use the
  wide APIs too; `_chsize_s` truncates on Windows.  `apps/nj2me_main.c` rebuilds
  `argv` with `CommandLineToArgvW`: the mingw CRT converts the command line to
  narrow `argv` through the ANSI code page, which turns `1942空战_10403.jar`
  into GBK bytes (or `?` on an English system, irreversibly).
- The datagram native table compiles the "unavailable" stub on Windows instead
  of the POSIX socket half, and `tools/build_windows.sh` plus
  `tools/windows/README.txt` reproduce the build and the zip.

## Why one console-subsystem exe and wide paths

There is no MSVC toolchain in this environment, so mingw-w64 is the only way to
produce a Windows binary at all, and it keeps the project's single C99 codebase
and CMake setup.  The exe stays console-subsystem on purpose: the emulator's
log (`INFO:`/`WARN:` lines and `--stats`) goes to stdout/stderr exactly as on
Linux, which is what makes a report from a user actionable.  SDL2 is bundled
because Windows has no system copy of it; everything else (libgcc,
libwinpthread) is linked statically, and the mingw sysroot has no zlib, so the
built-in DEFLATE handles the `java.util.zip` paths unchanged.

The port's central problem is that Windows' narrow C runtime file APIs speak
the active ANSI code page while the runtime's internal paths are UTF-8 (jar
names in the corpus are Chinese).  Converting at the boundaries - wide
`CreateFile`-family calls and `FindFirstFileW` for enumeration, UTF-8 back out
for the library rows - keeps every existing byte-string contract intact.
`argv` is the same boundary: the CRT has already damaged it before `main`, so
it is read again from `GetCommandLineW`.

pkg-config cannot be trusted on a cross build: it happily reports the host's
SDL2, of the wrong architecture.  The toolchain points `PKG_CONFIG_LIBDIR` at
the mingw sysroot and the CMake fallback takes the explicit cache paths.  The
official mingw `sdl2.pc` is not usable directly (stale `/tmp` prefix,
`-mwindows`, `-Dmain=SDL_main`), so the zip script passes the include and
import-library paths and leaves the `.pc` alone.

## Verification

Built with `tools/build_windows.sh` (wine 11.20, mingw-w64 16.2.0, SDL2
2.32.10); the packaged `nj2me.exe` was then run from the staged directory:

    wine nj2me.exe --version                     -> nano_j2me 0.5.2, exit 0
    wine nj2me.exe --help                        -> usage text, exit 0
    wine nj2me.exe --print-library --games tests/games
        -> 402 lines, "400 games, 399 runnable", exit 0; rows such as
           "1942AirCombat  1942空战_10403.jar" print as UTF-8
    wine nj2me.exe --port headless --run-frames 60 --stats tests/fixtures/alphademo.jar
        -> exit 0, "released 1078 host blocks at teardown"
    wine nj2me.exe --port headless --run-frames 120 --stats "tests/games/1942空战_10403.jar"
        -> exit 0; throws the same
           java/lang/ArrayIndexOutOfBoundsException at j.<init> pc=382 as the
           Linux build, then "run ended: the frame budget ran out"

The client runs headless with SDL's dummy drivers:

    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    NJ2ME_CLIENT_FRAMES=10 NJ2ME_CLIENT_SCREENSHOT=launcher.ppm \
    wine nj2me.exe --client --games tests/games    -> exit 0

`launcher.ppm` is a 720x540 P6 frame with 1417 distinct colours; it shows the
library header (`nano_j2me v0.5.2`, `tests/games`), rows with each MIDlet's icon
and Chinese name, and the footer key legend - so icon and name probing really
opens Chinese-named jars through the wide path.

A small probe calling `nj_client_settings_path`/`load`/`save` against the built
library reports `%APPDATA%\nano_j2me\client.conf` (wine:
`C:\users\<user>\AppData\Roaming\nano_j2me\client.conf`), the games directory
`%APPDATA%\nano_j2me\games`, and a save/load round-trip (`save=1 load=1
fps=30`).  XDG variables are ignored on Windows, as intended.

The native Linux build still configures from scratch and builds warning-free
(`cmake -S . -B /tmp/build-linux-check && cmake --build ...`), `ctest --test-dir
build -j6` is 138/138, and `tools/check_no_statics.sh` reports no writable
statics.

The release zip is `build/nj2me-0.5.2-windows-x86_64.zip`: 2,139,213 bytes,
SHA-256 `f9d7871d87faac67d9f94fe08ed12d1ce0a52def71daabf4329ec2cb3cc8ca3e`,
with `nj2me.exe` (3,124,875 bytes stripped), `SDL2.dll`, `README.txt` and
`samples/{keysdemo,colordemo,textdemo,audiodemo,mididemo}.jar`.

## Limits

- No libretro core for Windows: the libretro target is still UNIX-only, and
  `-DNJ_BUILD_LIBRETRO=OFF` is how the Windows build configures.
- No network: the POSIX libcurl port is not built on Windows, so HTTP/HTTPS/TCP
  are unavailable and `datagram://` uses the runtime's connection-unavailable
  path.  A WinSock/libcurl port would be a separate piece of work.
- SDL2 is pinned: the zip carries the 2.32.10 `SDL2.dll` from the mingw
  development package; mixing in another SDL2 build is not tested.
- x86_64 only, built by mingw-w64; there is no MSVC build and no arm64 build.
  The exe is unsigned, so SmartScreen will warn on first run.
- Verified under wine only, not on a physical Windows machine.  The Windows
  build also runs without JIT (the interpreter is what the mingw build uses),
  so it is much slower than the Linux binary but behaviourally the same: the
  1942 title throws the same exception at the same pc on both.
- The console window is intentional (log visibility), not a debug leftover.
