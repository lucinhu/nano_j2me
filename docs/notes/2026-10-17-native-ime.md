# Native text input and Chinese editing — 2026-10-17

The desktop SDL2 and Android ports now connect native input methods to MIDP TextBox and focused TextField editors. This round also removes invalid Item Command type restrictions that prevented the downloaded NetEase Music application from opening its search Form.

## Editing contract

`nj_port.text_input` receives an editable text snapshot, constraints, maximum length, cursor, focus session and acknowledged replacement sequence. UTF-8 commit events use a 32-byte character-aligned buffer; UTF-16 full replacement events support long commits, Android selection and deletion. Replacement payloads remain valid through dispatch until the next poll. Android owns queued copies and releases them at the next poll or shutdown.

Focus, constraints and maximum-length changes rotate the session. MIDP rejects missing or stale sessions, malformed Unicode, invalid constraints and duplicate replacement sequences. Rejected replacements are acknowledged so the host editor can restore accepted text. Cursor movement, backspace and delete preserve supplementary Unicode pairs. Maximum length follows Java UTF-16 units. Password and uneditable constraints are respected.

SDL starts and stops native input, treats TEXTEDITING as composition, commits TEXTINPUT, and suppresses duplicate printable key events and associated releases. Composition confirmation/cancellation does not dispatch game commands. Android uses a native EditText with InputConnection/TextWatcher, composing-span protection, cursor synchronization, soft keyboard lifecycle and a JNI full-text event queue. Its queue limits one snapshot to 1,048,576 UTF-16 units. Pause/destroy hide the editor and discard stale callbacks.

UP/DOWN traversal can leave a Form TextField for an adjacent item; LEFT/RIGHT edit its cursor. Item.addCommand, StringItem.addCommand and Item.setDefaultCommand accept SCREEN/BACK/ITEM commands; null addCommand still throws and null default clears the default.

## Verification

- `test_text_input`: **167 checks, zero failures**, including Chinese and supplementary characters, malformed encodings, >32-byte full replacement, cursor/delete, password, constraints, UTF-16 limits, rejected/repeated acknowledgement, stale focus and Form traversal.
- Independent Debug focused CTest: **4/4 passed** (`test_text_input`, `test_lcdui`, `test_lifecycle`, `test_natives`). Final integrated Release CTest, including the new thread lifecycle regression: **84/84 passed in 47.08 seconds**.
- Downloaded NetEase Music JAR: search Form opens at frame 300; soft2 returns to the main list by frame 600; no thrown/escaped/uncaught exceptions. Remote search results and playback remain unverified.
- Android NDK 28.2.13676358, ARM64 API21 C compilation/linking, Java compilation, dex, resources and signing passed. `tools/build_android_apk.sh --abi arm64-v8a --out build-ime-android` produced `build-ime-android/nj2me.apk`, 731,292 bytes, rebuilt after the final core thread and M3G changes. The APK was not installed over the user's application.

## Coverage limits

This host uses SDL2-compat 2.32.74 over SDL3. Injecting synthetic SDL text events crashes its SDL3 translator, so the regression explicitly skips that portion on SDL2-compat >=2.32.50 and reports the skip. SDL activation/deactivation passed; native SDL2 retains the full synthetic composition/commit/duplicate/session replay. A real desktop Pinyin composition and candidate-selection session has not been verified.

The Android device used earlier for independent network tests was disconnected before IME validation. Native Android EditText, keyboard and Chinese input therefore remain **unverified on a device**. An isolated instrumentation probe was prepared under `.local-tools/debug-runs/ime-20261017/device/`, but was not executed. It must be repackaged against the final core before running. Build success and the prepared probe are not device pass evidence.

Detailed local commands and logs are in `.local-tools/debug-runs/ime-20261017/REPORT.md`. Original JARs and user RMS/save data were preserved.
