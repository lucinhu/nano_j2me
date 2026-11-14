# Real Android AMR service test

This test calls the production native provider from a normal app worker thread.
It synthesizes 12 packets for each of the eight speech modes and SID, requires
3840 bytes of 8 kHz mono S16 PCM per mode, and checks output guards, EOF, rewind,
and allocator cleanup. It does not contain an AMR decoder or game assets.

The separate shell invocation checks that a process without an existing Java VM
returns NULL before touching MediaCodec, allowing the registry's software retry.
API 21's codec service stalled in an uninitialized standalone shell process;
`JNI_GetCreatedJavaVMs` distinguishes the app runtime without creating a VM or
attaching a thread. Missing JNI symbols safely choose software fallback too.

The following recipe was exercised with an x86_64 API 21 emulator, NDK r30,
Android build tools 35.0.0, and installed platform android-34. Run from the
repository root. Set `ANDROID_SDK_ROOT`, `ANDROID_NDK_HOME` and `ANDROID_SERIAL`
to the installed SDK, NDK and a disposable test device/emulator. The app is
installed under the test-only package `org.nanoj2me.amrprobe`.

```sh
set -eu
: "${ANDROID_SDK_ROOT:?}" "${ANDROID_NDK_HOME:?}" "${ANDROID_SERIAL:?}"
probe_src=tests/private/system_amr_android_app
probe_out=build-system-amr/app
probe_bt="$ANDROID_SDK_ROOT/build-tools/35.0.0"
probe_jar="$ANDROID_SDK_ROOT/platforms/android-34/android.jar"
probe_cc="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android21-clang"
mkdir -p "$probe_out/classes" "$probe_out/dex" "$probe_out/lib/x86_64"
javac -source 8 -target 8 -classpath "$probe_jar" -d "$probe_out/classes" "$probe_src/Probe.java"
"$probe_bt/d8" --min-api 21 --output "$probe_out/dex" "$probe_out/classes/org/nanoj2me/amrprobe/Probe.class" "$probe_out/classes/org/nanoj2me/amrprobe/Probe\$1.class"
"$probe_cc" -std=c11 -Wall -Wextra -Werror -shared -fPIC -DNJ_ENABLE_SYSTEM_AMR=1 -Dmain=nj_amr_test_main -Iinclude tests/test_system_amr_android.c src/port/common/nj_system_amr.c "$probe_src/jni.c" -ldl -o "$probe_out/lib/x86_64/libamrprobe.so"
"$probe_cc" -std=c11 -Wall -Wextra -Werror -DNJ_ENABLE_SYSTEM_AMR=1 -Iinclude tests/test_system_amr_android.c src/port/common/nj_system_amr.c -ldl -o build-system-amr/test_system_amr_android
adb push build-system-amr/test_system_amr_android /data/local/tmp/nj-test-system-amr
adb shell /data/local/tmp/nj-test-system-amr --expect-no-app
"$probe_bt/aapt" package -f -M "$probe_src/AndroidManifest.xml" -I "$probe_jar" -F "$probe_out/probe-unsigned.apk"
python3 - <<'PY'
from zipfile import ZipFile
from pathlib import Path
p = Path('build-system-amr/app')
with ZipFile(p / 'probe-unsigned.apk', 'a') as z:
    z.write(p / 'dex/classes.dex', 'classes.dex')
    z.write(p / 'lib/x86_64/libamrprobe.so', 'lib/x86_64/libamrprobe.so')
PY
if [ ! -f "$probe_out/probe.keystore" ]; then
  keytool -genkeypair -keystore "$probe_out/probe.keystore" -storepass probe123 -keypass probe123 -alias probe -dname CN=AMRProbe -keyalg RSA -validity 30
fi
"$probe_bt/apksigner" sign --ks "$probe_out/probe.keystore" --ks-pass pass:probe123 --out "$probe_out/probe.apk" "$probe_out/probe-unsigned.apk"
adb install -r "$probe_out/probe.apk"
adb shell am force-stop org.nanoj2me.amrprobe
adb shell am start -n org.nanoj2me.amrprobe/.Probe
```

After the worker finishes (under 30 seconds), collect the result:

```sh
adb shell run-as org.nanoj2me.amrprobe cat files/result.log
adb logcat -d -s NJAMRProbe:I '*:S'
```

Require nine `PASS provider=system-amr-android` lines, the final
`real Android system AMR: 9 modes passed`, and `NJAMRProbe RESULT=0`. PCM hashes
are diagnostic: different platform decoders may produce different samples.
Some Android runtime messages can also reach the redirected process output.
The probe does not validate audio quality or every Android device/OS release.
An abort or the 30-second alarm is a failure, not a software-fallback pass.

Remove only this test package and executable after collecting results:

```sh
adb uninstall org.nanoj2me.amrprobe
adb shell rm /data/local/tmp/nj-test-system-amr
```

The Java/JNI host is test-only. Production system-AMR code does not include this
app, create/attach to Java, or bundle platform codec binaries.
