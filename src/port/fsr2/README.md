# AMD FidelityFX Super Resolution 2 for Vulkan

This module upsamples an opaque, perspective-rendered 3D frame with AMD's FSR2 2.2.1 context and Vulkan compute pipelines. It keeps a history per context. Composite HUD and other 2D overlays at output resolution after dispatch.

## Build and verify on Linux

```sh
python3 tools/build_fsr2_sdk.py
cmake -S . -B build-fsr2 -DNJ_ENABLE_FSR2=ON -DNJ_BUILD_TESTS=ON
cmake --build build-fsr2 --target test_fsr2
ctest --test-dir build-fsr2 -R '^test_fsr2$' --output-on-failure
```

The SDK builder checks archive SHA-256 values, compiles the official GLSL passes locally and installs the headers and a PIC static library under `.local-tools/debug-runs/fsr2-sdk/install`. Point `NJ_FSR2_SDK_ROOT` to a different prefix if necessary. The enabled build requires a Vulkan loader, C++17, and a Vulkan 1.2 compute device with the shader and image features used by FSR2. The default `OFF` build provides the same CPU-facing C ABI and reports unavailability at context creation.

[AMD FSR2 2.2.1](https://github.com/GPUOpen-Effects/FidelityFX-FSR2/tree/1680d1edd5c034f88ebbbb793d8b88f8842cf804) is MIT licensed. [Khronos Vulkan-Headers 1.3.250](https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.3.250) supplies the Apache-2.0 headers. The local builder generates SPIR-V permutation headers, adapts Windows-specific C++ helpers, permits device-local unified memory, and matches two GLSL storage-image qualifiers to their SDK image formats. Distributors of builds containing the SDK should include its notices installed under `share/licenses` in the SDK prefix.

## Build for Android arm64

Set `ANDROID_NDK_HOME` to the installed NDK. Use the same NDK and API level for the SDK and the app. With NDK 28 on a Linux host:

```sh
bin="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
work="$PWD/.local-tools/debug-runs/fsr2-arm64"
CXX="$bin/aarch64-linux-android28-clang++" AR="$bin/llvm-ar" \
  python3 tools/build_fsr2_sdk.py --work "$work"
tools/build_android_apk.sh --fsr2-sdk "$work/install" --out build/android-fsr2
```

The signed debug APK is `build/android-fsr2/nj2me.apk`. This variant targets arm64-v8a with minimum API 28 and includes the native libraries and both SDK notices under `assets/licenses/`. For just the native libraries run `tools/build_android.sh --abi arm64-v8a --api 28 --fsr2-sdk "$work/install"`. For Android NDK CMake builds use `ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-28`, `ANDROID_STL=c++_shared`, `NJ_ENABLE_FSR2=ON`, and `NJ_FSR2_SDK_ROOT` pointing at that Android SDK prefix. The device must expose the required Vulkan capabilities for a context to be created.

## Frame contract

* Create one context per size and view history. Keep it on one thread. The CPU bridge accepts 32–4096 texels per input axis, with at least one axis of 64 or more for the luminance pyramid. The output may be up to four times the input in each axis, within device limits.
* Read the phase jitter with `nj_fsr2_jitter` and apply it to the projection. Pass that same offset to dispatch. Depth is perspective device Z, with zero at the near plane. Motion vectors map each current unjittered sample to its previous position in input pixels, positive Y down. Provide the frame interval in milliseconds and the vertical FOV in radians. Set `reset` for invalid history, scene cuts or discontinuities.
* The CPU call consumes tightly packed top-down ARGB8888, float depth, and interleaved XY float motion. On success it fills a caller-owned output buffer of `ow * oh` opaque ARGB8888 pixels. On failure use the caller's spatial rendering fallback. Recreate the context after a GPU error.
* For direct GPU operation, use the device and queue returned by `nj_fsr2_vk_get_device`. Supply image/view pairs on that device in the layouts specified in `nj_fsr2_vk.h`, with appropriate producer barriers. `nj_fsr2_vk_dispatch` waits for completion and leaves the result on the GPU. Insert a compute-write to consumer-read barrier before reading it in another GPU pass.

The hardware test dispatches a moving triangle across four frames. It checks foreground/background colors and verifies that preserved versus reset history produces different pixels for otherwise identical inputs. A compatible device is required to run it.
