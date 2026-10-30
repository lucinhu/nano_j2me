nano_j2me 0.5.2 - Windows x86_64
================================

nano_j2me 是用 C99 写成的 J2ME（CLDC 1.1 / MIDP 2.0）运行时。
本压缩包是 Windows x86_64 桌面版：命令行运行器 + 图形客户端（游戏库窗口）。
nano_j2me is a J2ME (CLDC 1.1 / MIDP 2.0) runtime in C99. This is the Windows
x86_64 desktop build: the command-line runner and the games-library client.

快速开始 / Quick start
----------------------
双击 nj2me.exe 打开游戏库：Enter 运行，O 打开 jar，R 重新扫描，S 设置，
Esc/Q 退出；游戏里按 F10 回到游戏库，F11 切换全屏；把 .jar 拖到窗口上直接
运行，把文件夹拖上去就把它设为游戏库。
Double-click nj2me.exe for the library: Enter runs, O opens a jar, R rescans,
S opens settings, Esc/Q quits; F10 returns to the library, F11 toggles
fullscreen; drag a .jar (or a folder) onto the window to run it (make it the
library).

命令行示例 / Command line
-------------------------
  nj2me.exe game.jar
  nj2me.exe --port headless --dump-frames shots/ --run-frames 60 game.jar
  nj2me.exe --print-library --games "D:\games"
  nj2me.exe --client --games "D:\games"
  nj2me.exe --help

设置 / Settings
---------------
设置文件：%APPDATA%\nano_j2me\client.conf
默认游戏库：%APPDATA%\nano_j2me\games（首次运行自动创建）
The settings file is %APPDATA%\nano_j2me\client.conf and the default games
directory is %APPDATA%\nano_j2me\games (created on first use).

依赖 / Dependencies
-------------------
只需要本目录下的 SDL2.dll（已附带）；C 运行库（libgcc、winpthread）已静态
链接进 nj2me.exe。没有 zlib（用内置 DEFLATE 解压），没有 libcurl（POSIX
网络端口不为 Windows 构建，因此 HTTP / datagram 等联网功能不可用）。
仅支持 x86_64。
Only the bundled SDL2.dll is needed next to nj2me.exe; the C runtime is
statically linked. No zlib (built-in DEFLATE) and no libcurl (the POSIX network
port is not built for Windows, so HTTP/datagram networking is unavailable).
x86_64 only.

示例 / Samples
--------------
samples\keysdemo.jar, colordemo.jar, textdemo.jar, audiodemo.jar, mididemo.jar

从源码构建 / Building from source
---------------------------------
需要 mingw-w64 和 SDL2 的 mingw 开发包（release-2.32.10，解压到
.local-tools/sdl2-mingw/）：
  cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=tools/toolchain-mingw-w64.cmake ^
        -DSDL2_INCLUDE_DIRS="<sdl>\include;<sdl>\include\SDL2" ^
        -DSDL2_LIBRARIES="<sdl>\lib\libSDL2.dll.a" ^
        -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF
  cmake --build build-windows --target nj2me-cli
或运行 tools/build_windows.sh：它会在需要时下载 SDL2，构建并生成 zip。
Or run tools/build_windows.sh, which downloads SDL2 if needed, builds and zips.

许可 / Licence: MIT - see LICENSE.
