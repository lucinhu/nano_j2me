# Android Form/TextBox IME regression (2026-10-17)

## Diagnosis and changes

Reported case: tapping the registration input in 仙镜OL on Android does not open the keyboard. Base: `220f146`.

`GameActivity` already adds the native `ImeEditor extends EditText` to its Activity root window. It does not set `FLAG_NOT_FOCUSABLE` or `FLAG_ALT_FOCUSABLE_IM`. The native editor exists and can receive focus; constructing another editor is unnecessary.

The input chain needed the following corrections:

- MIDP `nj_midp_pointer()` previously treated non-Canvas pointer-down as activation of the old focus. A Form tap could execute its default registration command without selecting the tapped TextField. The correction uses the renderer's title/ticker/row measurements to select the touched item. Tapping a TextField or TextBox renews its IME focus/session and synchronizes text input. This also reopens an Android-dismissed keyboard on the same field.
- Android `android_text_input()` intentionally deduplicates identical snapshots (session/active/sequence/constraints/max length/multiline/cursor/text). Without session renewal, a repeated tap on the active field cannot produce another Java show request. Keep this deduplication; renew the session at the MIDP field hit.
- Java previously called `restartInput()` / `showSoftInput()` immediately when applying a new session, or during `onResume()`. View focus may exist before window input focus. `requestIme()` now posts one UI request, checks the window token and window/view focus, retains a pending request while unfocused, and retries from `onWindowFocusChanged(true)`. New-session and resume use the same path. Pause, inactive snapshots and destruction cancel pending work through `hideIme()`. No unconditional show was added to game touches; commands do not reopen the keyboard.

The Android C port remains unchanged. Touch coordinates still traverse `onGameTouch()` → JNI `nativeTouch()` → `nj_port_android_touch()` → scale/offset reversal → MIDP pointer events. Java and IME work stay on the UI thread; native callbacks enter through `runOnUiThread()`.

## Completed verification

- Full Android Java compile against `/home/lucinhu/Android/Sdk/platforms/android-35/android.jar`: passed.
- ARM64 NDK build and standard APK build: passed.
- Lead ran `build/test_text_input`: **197 checks / 0 failures**. The complete native suite (`ctest --test-dir build --output-on-failure -j4`) passed **84/84 in 43.56 seconds**. Build output contained no compiler warnings or errors. Logs: `.local-tools/debug-runs/xianjing-ime-20261017/build.log` and `.local-tools/debug-runs/xianjing-ime-20261017/ctest.log`.
- Device `425610ca` was online for one isolated instrumentation run. Only the distinct package `com.lucinhu.nj2me.imeprobe` was installed/launched; the user's original package and real registration flow were not operated.

The completed device run asserted actual `WindowInsets.Type.ime()` visibility, editor window token and window/view focus, hid the keyboard through IMM, tapped the same TextBox through `onGameTouch()`, and checked a changed session plus visible system IME. It also exercised Chinese/emoji UTF-16 replacement longer than 32 UTF-8 bytes, MIDlet `getString()` roundtrip through a second TextBox, and native editor closure after switching to Canvas.

Command and exact completed output:

```sh
adb -s 425610ca install -r .local-tools/debug-runs/ime-20261017/device/ime-probe.apk
adb -s 425610ca shell am instrument -w com.lucinhu.nj2me.imeprobe/com.lucinhu.nj2me.ImeProbe
```

```text
INSTRUMENTATION_RESULT: result=PASS native EditText activation, Chinese UTF16 replacement >32 UTF8 bytes, MIDlet getString roundtrip, focus session change, Canvas closes editor
INSTRUMENTATION_CODE: -1
```

That output predates the subsequently enhanced fixture; its printed description did not enumerate the already-present system-IME visibility and same-TextBox reopening assertions. No screenshot was captured during the successful run; the instrumentation finishes its Activity in `finally`.

## Prepared but not device-verified

The device disconnected before the enhanced probe could be installed. A subsequent install/run returned `adb: device '425610ca' not found`; both investigator and lead observed an empty `adb devices -l`. Do not report the enhanced checks as device passes.

The final probe sources under `.local-tools/debug-runs/ime-20261017/device/` compile and package successfully. They add:

- Pause/resume restoration via a temporary isolated LibraryActivity.
- A show request while a Dialog owns window focus, retention of `imeShowPending`, and actual keyboard visibility after Dialog dismissal.
- A two-field registration Form: account and password. Pointer taps scan vertically until the password editor is selected, checking its text and password input type and asserting that no Form command ran. Hiding and tapping that same password field must renew the session and reopen the IME.

The hide operation uses IMM rather than a physical Back key; a real Back dismissal remains a manual follow-up. The Form pointer scan exercises the GameActivity/native/MIDP chain, but does not synthesize MotionEvents through PadView. Real touch routing should also be manually verified. The fixture contains no network registration operation.

When the device returns, install the final isolated probe and rerun the command above. If evidence screenshots are needed, capture them while a manually launched probe Activity is still open (instrumentation otherwise closes it).

Rebuild the probe after a standard APK build using these commands; `package_probe.py` uses the current standard Java classes, ARM64 library and resource archive:

```sh
PROBE=.local-tools/debug-runs/ime-20261017/device
SDK=/home/lucinhu/Android/Sdk/platforms/android-35/android.jar
STUBS=$(python3 tools/midp_stubs.py --quiet)
javac --release 8 -nowarn -classpath "$STUBS" -d "$PROBE/fixture" "$PROBE/fixture/ImeFixture.java"
javac -source 8 -target 8 -nowarn -classpath "$SDK:build-ime-android/apk/classes" -d "$PROBE/classes" "$PROBE/src/com/lucinhu/nj2me/ImeProbe.java"
python3 "$PROBE/package_probe.py"
```

## Real JAR in the isolated package

The probe manifest exposes `com.lucinhu.nj2me.GameActivity` under the distinct package. Lead can copy a chosen real JAR into that package's private files and launch it directly. This prepares the real game's input-screen check without updating the user's installed app. Do not submit a registration command.

```sh
JAR=/absolute/path/to/selected-game.jar
adb -s 425610ca shell run-as com.lucinhu.nj2me.imeprobe mkdir -p files
adb -s 425610ca shell "run-as com.lucinhu.nj2me.imeprobe sh -c 'cat > files/real.jar'" < "$JAR"
adb -s 425610ca shell am start -n com.lucinhu.nj2me.imeprobe/com.lucinhu.nj2me.GameActivity --es jar real.jar --es jarpath /data/user/0/com.lucinhu.nj2me.imeprobe/files/real.jar --es title 'Isolated real JAR input check'
```

Check first/second registration field selection, password keyboard type, physical Back then same-field reopening, switching away to Canvas, and background/foreground restoration. Capture a screenshot and `dumpsys input_method` while the system keyboard is visible. Real 仙镜OL device behavior remains unverified due to the disconnect.

## APK artifacts

Standard update artifact (not installed): `build-ime-android/nj2me.apk`, package `com.lucinhu.nj2me`, versionCode `10`, versionName `0.1.0`, ARM64 only, minSdk 21 / targetSdk 34.

```text
SHA256 d5b1fb2a8077dfa143922d2697dcbee7e14bb95b998a4f5f485feb9394b979e2
```

Final enhanced isolated artifact: `.local-tools/debug-runs/ime-20261017/device/ime-probe.apk`.

```text
SHA256 371b06cc0df17148688f559ddf1c6f82942da585489146c4e4b26be8505fc6cd
```

Build commands/logs:

```sh
tools/build_android.sh --abi arm64-v8a --out build-ime-android
tools/build_android_apk.sh --abi arm64-v8a --out build-ime-android
```

Logs: `.local-tools/debug-runs/ime-20261017/device/native-focus-build.log` and `.local-tools/debug-runs/ime-20261017/device/standard-apk-build.log`. Local probe sources/artifacts are ignored work products; the tracked regression documentation is this note plus lead's native unit tests. No commit was created by the Android investigator.
