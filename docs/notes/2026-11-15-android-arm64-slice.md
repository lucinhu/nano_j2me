# 2026-11-15 Android 现代 APK 的 arm64-v8a 切片

## 结论

现代 APK 必须同时包含 `arm64-v8a`。此前一直用
`tools/build_android_apk.sh --abi "armeabi-v7a x86_64"` 构建，64 位手机
（Xiaomi 23049RAD8C，arm64-v8a/armeabi-v7a/armeabi）因此只能安装并运行
**32 位** 的 `armeabi-v7a` 切片，即 32 位解释器。真机无头基准显示 32 位
解释器比 64 位慢 2.3 倍，这是手机端 Minecraft 生成/读档明显慢于 x86_64
模拟器的主要原因之一。

## 真机无头基准（不启动 UI、不占用前台）

用 `tools/bench_minecraft_gen.c` + `src/port/posix/nj_port_headless.c`
交叉编译成 arm64/armv7 两个独立可执行文件，push 到
`/data/local/tmp/nj-bench/` 后用 `adb shell` 直接运行；同一 JAR、同一种子
`12345`、固定 256 chunk 生成工作，两种 ABI 交替各跑 3 轮：

| ABI | GEN cpu_ms | 备注 |
| --- | --- | --- |
| arm64-v8a | 31246 / 32853 / 36013（均值 33400） | 2.29× 快 |
| armeabi-v7a | 71179 / 77981 / 80341（均值 76500） | 基线 |

两种 ABI 输出完全一致：`WORLD chunks=256 blocks=a893a062 data=c77a1b93`、
`dispatch=5175713674`、`natives=1616679`，说明差异只来自解释器代码生成，
不是行为差异。

## 修复与产物

- 构建命令改为包含三个 ABI：
  `tools/build_android_apk.sh --out build/android --abi "arm64-v8a armeabi-v7a x86_64" --gles2`
- 本次 APK：`.local-tools/debug-runs/gfx-full-20261113/android/arm64-build/nj2me.apk`
  2,595,113 字节，SHA256
  `c508dc4ef79b91c5e112efbb531181383b5dd15db1c3a6ad5dc95751c2413349`；
  内含 `lib/arm64-v8a/libnj2me.so`（ELF AArch64, API21, NDK r30, 2,298,976 B）、
  `lib/armeabi-v7a`、`lib/x86_64`。
- 安装到真机后 `dumpsys package com.lucinhu.nj2me` 显示
  `primaryCpuAbi=arm64-v8a`，数据保留（`adb install -r`）。
- 应用内实测（同一手机、同一存档，用户手动打开游戏）：
  `MCJ: LOAD read 14147 ms, prepare 11998 ms`（合计 26.1 s），对比 32 位
  安装包此前的 `read 26527 ms, prepare 25056 ms`（合计 51.6 s）——
  **读档快 1.98×**（read 1.87×、prepare 2.09×）。游戏内 `m3g: accel
  calibration gpu=1017us software=757us -> disabled` 与此前一致，自动
  选择仍保持软件渲染。
- 新建世界应用内实测：`MCJ: GEN 43885 ms, save 10170 ms`（含 Player 创建、
  UI 与存档写入，非纯 generate()）；同一工作量的无头基准是 33.4 s，说明
  应用内多出的时间来自 UI/存档等非解释器部分。未测 32 位应用内 GEN 基线
  （旧安装包已不在手机上），按无头 2.29× 比例推断约为 90–100 s。

## 复现

```sh
NDK=$HOME/Android/Sdk/ndk/30.0.16248370
BIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
DEFS="-DNJ_PIXFMT=NJ_PIXFMT_RGB565 -DNJ_ENABLE_PNG=1 -DNJ_ENABLE_JPEG=1 \
-DNJ_ENABLE_GIF=1 -DNJ_ENABLE_INFLATE=1 -DNJ_ENABLE_WAV=1 -DNJ_ENABLE_MEDIA=1 \
-DNJ_ENABLE_AMR=1 -DNJ_ENABLE_RMS=1 -DNJ_ENABLE_IO=1 -DNJ_ENABLE_FLOAT=1 \
-DNJ_ENABLE_M3G=1 -DNJ_ENABLE_MASCOT=1 -DNJ_ENABLE_TTF=1 \
-DNJ_ENABLE_FONT_CJK=0 -DNJ_THREADED_DISPATCH=1"
SRC="$(ls src/core/*.c src/midp/*.c src/gfx/*.c src/zip/*.c)"
for abi in arm64 armv7; do
  case $abi in
    arm64) CC=$BIN/aarch64-linux-android21-clang ;;
    armv7) CC=$BIN/armv7a-linux-androideabi21-clang ;;
  esac
  $CC -std=c99 -Os -Wall $DEFS -Iinclude -Isrc/core -Isrc/midp -Isrc/gfx -Isrc/zip \
      $SRC src/port/posix/nj_port_headless.c src/port/common/nj_system_amr.c \
      tools/bench_minecraft_gen.c -lz -lm -ldl -o bench-$abi
done
adb push bench-arm64 bench-armv7 MinecraftJ2ME_PE_0.6.1_V2.2.jar /data/local/tmp/nj-bench/
adb shell "chmod 755 /data/local/tmp/nj-bench/bench-*; \
  /data/local/tmp/nj-bench/bench-arm64 /data/local/tmp/nj-bench/MinecraftJ2ME_PE_0.6.1_V2.2.jar 12345"
```
