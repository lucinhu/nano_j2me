# nano_j2me 0.5.31 — libretro r50

Release date: 2026-11-22. Tag `v0.5.31`.

This release ships the member-level API audit and the runtime gaps it closed, plus the
remaining JSR-82/JSR-75 surface holes found by the same audit. It also adds a
developer-facing API dump so the audit can run against the real builtin tables instead
of a hand-parsed copy.

## Highlights

* **Builtin-API dump.** New public helper `nj_dump_builtin_api()` and CLI flag
  `nj2me --dump-builtin-api <path>` write every builtin class, its superclass, its
  members and descriptors. `tools/check_vendor_members.py` uses that dump for a static
  member-level audit of the corpus (it found 296 missing signatures over 54 classes on
  the first honest run).
* **Wrapper/linkage members completed.** `Byte.parseByte(String,int)`,
  `Short.parseShort(String,int)`, `Double.parseDouble(String)`,
  `Float.valueOf(String)`, `Float.intValue()`, `RateControl.getMinRate()/getMaxRate()`,
  the `PIMItem` query declarations, and a real superclass bug —
  `InputConnection`/`OutputConnection` were linked to `java/lang/Object` instead of
  `Connection`, so `close()` did not resolve through the chain.
* **JSR-82 Bluetooth surface completed.** `LocalDevice.getDiscoverable()`,
  `LocalDevice.getRecord(Connection)`, `RemoteDevice(String)`,
  `RemoteDevice.getRemoteDevice(Connection)`, `RemoteDevice.isTrustedDevice()`,
  `ServiceRecord.setAttributeValue(int,DataElement)Z`. Radio-less semantics are honest:
  `BluetoothStateException` where the device has no radio, documented no-op answers
  where the specification allows one.
* **JSR-75 `FileConnection.directorySize(Z)J`.** Walks the port's directory hooks and
  sums regular-file sizes, honours the hidden-file flag, throws `IOException` for
  files/missing paths.
* After the fixes the static audit reports only two deliberately title-side classes:
  a non-MIDP `PrintStream` extension and two JARs that were built against a patched
  LCDUI (`DirectGraphics` bundled inside them). No runtime-owned member gap remains.

## Verification

* Full serial CTest: **207/207 passed in 360.12 s**.
* Mock frontend matrix: **20/20**; real RetroArch 1.22.2 LoopDemo 120 frames **PASS**.
* Windows x86_64 under Wine: **4/4** at 300 frames.
* Submission bundle self-check: **32/32**.
* Static member audit (`tools/check_vendor_members.py`): only the two title-side
  classes above.
* API audit: 1048 corpus JARs, 642 builtin classes implemented; only
  `毁灭战士3_10598.jar` references a missing symbol (unused JSR-226 SVG).

* Post-release corpus check on the released 0.5.31 CLI: the full 1050-row
  sweep reproduces the 0.5.30 verdicts exactly (**0 changes**, 1036 `ok`) and
  the r50-core visual probe over the 1036 rendered titles passes **1032** with
  the 2 documented known limitations and the 2 documented failures
  (`MidnightPool1`, `PAC-MAN 176x220`).
* After the round-187 audit gate was added, the suite is **208/208**
  (362.65 s); `check_vendor_members` fails on any new unresolved runtime
  member outside the tracked title-side allowlist.

* Memory safety (round 188): the whole 1050-title corpus was executed once
  under an ASan+UBSan build (`tools/check_corpus_asan.py`, 120 frames per
  title, per-row screen/heap/device) with **0 sanitizer findings**.

## Artifacts

Cores (`nj2me-0.5.31-libretro-cores.zip`, SHA-256
`0e388de05ad12c58a4cec7174cd205377a59e9f52a696639a68a96ea981479ae`):

| Target | Size | SHA-256 |
| --- | ---: | --- |
| linux-x86_64 | 4,011,952 | `cb668957ac9546f5efad8273c6f23345be2f6fbb6a391f7d768364eb95d9c653` |
| linux-aarch64 | 3,489,472 | `d4a18c762ae92e7d94216189fe43ecaf355fc08ea0b3e4c24103d6748162b881` |
| linux-armv7 | 3,151,144 | `cc2c00178d43e702cc1f735f1cb6619d1fa7ee93090797639da6902104d24080` |
| android-arm64 | 3,732,224 | `119eca23f1eacf184b4414242ba72ba48d85cf3566905c30390a54b3189b01f2` |
| android-armv7 | 2,843,708 | `2728f31794ff4a6e28c2a3357f1921b9b3152bcab0937343ef83008fdf2063f7` |
| android-x86_64 | 3,760,576 | `96eade3217b87867ed92cce89b82ac9636e11c2dc31e704d4ddf0a934356688b` |
| windows-i686 | 3,575,296 | `f6e24df89356625fb3e5f37791e0cdfea1bc43e9a3798b224e98f5c79c938e47` |
| windows-x86_64 | 3,346,432 | `b239b4cf54aeba12aa337dbe9528e01585943f9e0c2f309734027abde41f3de0` |

Metadata: `.info` SHA-256
`4f7a9f1921fe1a09890484c2d8852559b9aa8676650c870604d74a59ac3ffd8a`
(`display_version = "0.5.31"`, `cheats = "true"`, `memory_descriptors = "true"`).

Android APKs (versionCode 531; per-ABI, GLES2 hardware backend on the four phone ABIs,
legacy ARMv5 build stays software):

| APK | Size | SHA-256 |
| --- | ---: | --- |
| `nj2me-0.5.31-arm64-v8a.apk` | 1,046,684 | `ca0374a57cd44cd8908da4065adc94fc6407a0f3658dcfbfde3ee47cd128406c` |
| `nj2me-0.5.31-armeabi-v7a.apk` | 1,054,878 | `7a411f3fe1b4b3187f1ca0ebe717ece7a7728e8c9049ac449d84c19fe101fc27` |
| `nj2me-0.5.31-x86.apk` | 1,038,486 | `e8a96471c934ffed52c02db47eb59edeb2a1e052c2553b22530094aaba83f85b` |
| `nj2me-0.5.31-x86_64.apk` | 1,054,873 | `8652423c603ae68d3dae0fd4defed089a48c0718e2457fb8e55ed2b08f6495da` |
| `nj2me-0.5.31-legacy.apk` | 734,873 | `39b196b092aecec403c2787fe28e9ed45cc204aa0f5c0404bf308ab6f1dbddec` |

## Upgrade notes

* Save states written by 0.5.8–0.5.30 are refused by the version check (as always
  across releases); state files from 0.5.31 load in 0.5.31 only.
* The CLI gained a developer-only flag `--dump-builtin-api <path>`; it writes the
  builtin API and exits without running a game.
* No core option names or defaults changed.

## Known limitations

* `MidnightPool1` is black through the libretro core (its own paint loop busy-waits on
  the virtual clock; documented in `docs/LIBRETRO.md`), while the CLI renders it.
* `PAC-MAN (176x220)` is title-side (empty Sprint Muglet URI) and also hangs in the
  FreeJ2ME reference.
* Frontend `retro_serialize()` can still time out on titles whose single frame runs
  longer than the state-service budget.
* The libretro frontends are expected to serialize only after at least one
  `retro_run()`; rewind at auto heap sizes remains impractical.
* 128×128 Tomb Raider build (device dump) is not in the corpus cache.

## Upstream submission

* libretro-super: branch `add-nano_j2me` at commit `54f978a` (baseline
  `55e50a81195eaf8dd1b7720a601282459832ced1`), PR
  [#2204](https://github.com/libretro/libretro-super/pull/2204), open/mergeable.
* libretro/docs: branch `add-nano_j2me` at `95a1d0a83df4ce749ba7c8d4ea3784ab0cbf9924`
  (baseline `96ee874e010a33b3fb1a79cf3cf833fc17189cfc`), PR
  [#1239](https://github.com/libretro/docs/pull/1239), open/mergeable.
