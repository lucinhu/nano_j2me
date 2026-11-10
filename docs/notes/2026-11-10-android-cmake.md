# Android CMake component: restore the complete JNI library

The standalone [Android component](../../src/port/android/CMakeLists.txt), used
by `externalNativeBuild` integrations, had an obsolete source list. Its default
configuration compiled but failed to link: the VM registered native libraries
whose tables were absent, and used helpers whose implementations were omitted.
The [APK shell builder](../../tools/build_android.sh) already collected the full
runtime source set and did not have this source-list defect.

## Reproduction and repair

On the original component, NDK `30.0.16248370`, Ninja, Release and
`ANDROID_PLATFORM=android-21` / `ANDROID_ABI=x86_64` configured successfully and
then failed at the shared-library link. Diagnostics included
`undefined symbol: nj_avcodec_reset`, `nj_lib_content_table`, `nj_lib_fc_table`,
`nj_lib_http_table`, `nj_lib_pim_table` and additional native tables. This was an
actual default-build failure, independently of enabling JIT. The old source
list also omitted the x86-64 JIT translation unit.

The component now collects the same 68 translation units as the shell builder:
all C files in `src/core`, `src/midp`, `src/gfx` and `src/zip`, plus the runner and
Android port. The previous list contained 34. `CONFIGURE_DEPENDS` asks CMake to
reconfigure when that set changes. The header feature flags still select which
optional implementations contribute code.

It also supplies the runtime include directories and explicitly links
`android`, `log`, `OpenSLES`, `z`, `m` and `dl`. Explicit `--no-undefined` keeps
missing dependencies visible at link time; the tested NDK already supplied that
check by default. AAudio remains dynamically loaded, so it does not become a
load-time requirement on API 21.

The component retains C99, `-O2`, `NJ_MAX_HEAP=8388608`, switch dispatch, and JIT
disabled by default. It does not adopt the APK scripts' compiler/feature profile.
The interpreter, runtime interfaces, snapshot layout and existing release assets
are unchanged. This repair establishes build completeness; it does not claim a
Minecraft GEN, LOAD or frame-rate improvement. The ESP-IDF component was not
changed or validated by these builds.

## Validation

All builds used the same NDK and API 21 target as the reproduction:

| ABI | Component configuration | Build |
| --- | --- | --- |
| arm64-v8a | default | pass |
| armeabi-v7a | default | pass |
| x86 | default | pass |
| x86_64 | default | pass |
| x86_64 | `CMAKE_C_FLAGS=-DNJ_ENABLE_JIT=1` | pass |

All five libraries export the 21 JNI entry points declared by the Android port.
Their dynamic dependencies contain no `libaaudio.so` requirement.

On existing x86_64 emulators running API 21 and API 26, a temporary native probe
loaded each of the x86, x86_64 and JIT-enabled x86_64 libraries with
`dlopen(RTLD_NOW)`: six successful runs. It checked JNI symbols, created and
destroyed a VM, resolved the RMS/FileConnection/M3G/PIM/Nokia builtin classes,
and loaded the existing [ArrayDims fixture](../../tests/arrays/ArrayDims.java).
`ArrayDims.check()` returned 1 in every run, covering multidimensional/ragged
array bytecode, stores, lengths and bounds exceptions. No APK was installed or
replaced, and no running app was stopped.

The probe used no executable-memory callbacks. Its JIT-enabled run verifies
library loading and the supported interpreter fallback, not native JIT execution
or Android JIT speed. ARM variants were cross-built and had their exports checked;
they were not run on ARM hardware. Audio playback, UI rendering and a complete
gameplay session are outside this library-loading check. The small probe was
compiled as C11 after its initial strict C99 compile rejected an existing repeated
`nj_vm` typedef in the public headers; the component libraries themselves retained
their C99 configuration.

The existing desktop library/CLI and original Minecraft JAR hashes remained
unchanged. No desktop runtime or test source changed, so the previous 158-test
result was not rerun as evidence for this Android build repair. Commands, return
codes, library/export hashes and emulator probe logs were retained under the
locally excluded `.local-tools/debug-runs/android-cmake-20261110/`. See
[porting instructions](../PORTING.md#worked-example-android) for the NDK build workflow.
