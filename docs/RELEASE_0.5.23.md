# nano_j2me 0.5.23

Released 2026-11-21.

## Highlights

* **JSR-179 Location API**: `javax.microedition.location` now exists with the
  value classes (`Coordinates`, `QualifiedCoordinates`, `AddressInfo`,
  `Orientation`, `Criteria`, `Location`, the listener interfaces) and a
  functional emulated provider fixed at Helsinki (60.1699 N, 24.9384 E) with
  one immediate listener callback.  `Landmark` and `LandmarkStore` add an
  in-memory landmark registry (stores, categories, rectangle queries) that the
  GC and save states see; it is not persisted to RMS yet.  A title that only
  needs a provider and coordinates now starts and gets a plausible fix.
* **Nokia UI extras**: `DirectGraphicsImplemented` (marker), `Clipboard`,
  `GlobalIndicators` (+`GlobalIndicatorsException`), `TactileFeedback`,
  `SoftNotification`/`SoftNotificationImpl` (+`SoftNotificationException`,
  listener), `JoystickEventListener` and the `com.nokia.mid.ui.gestures`
  family.  The classes behave per spec where the emulated device can
  (`Clipboard` is an in-VM string, indicators keep per-VM state and return the
  previous value, soft notifications store their content); where there is no
  hardware they are honest no-ops (`isTouchFeedbackSupported()` is `false`,
  gesture registration returns `false` and no gesture/joystick events are
  synthesised).
* The web demo workflow now retries the Playwright boot when GitHub Pages
  briefly serves the previous deployment after `deploy-pages` reports
  success, instead of failing a green build (`b4f2e25`).
* Internal: the field-descriptor audit also checks whether each lookup uses
  the static or instance accessor, so a `NJ_FS` on an instance field is a hard
  failure instead of silently writing zero.

Everything else is unchanged from 0.5.22; no behaviour change for existing
titles, no save-format change.

## libretro core artifacts (r42)

| target | size | sha256 |
|---|---:|---|
| linux-x86_64 | 3,942,296 | `1316a3c7a1737121d1cf087eefcbb59a463ca0b7eee081dfc548af2114fba361` |
| linux-aarch64 | 3,418,472 | `d06e186269d0fbc679fdbbeba1c13ae64c8437f514f9d8f30752ba461f9e28e3` |
| linux-armv7 | 3,101,404 | `48f413d41d4226530dae424d72e1341e6f1c0383db14f106cab00c09165eaf71` |
| android-arm64 | 3,657,104 | `dbd4d9cfd51e844eab70a382d6788dae77f3681ef6ca4518727d7d8bb5dfc583` |
| android-armv7 | 2,803,300 | `99f5f85a96a6ebf0601467d60142fbc2e231358112503fe1abbef9fa6787ba8f` |
| android-x86_64 | 3,692,176 | `2805957eef45d295125b88c145cdc780aca873e02a5bc3883665ababd98330a6` |
| windows-i686 | 3,525,632 | `47d0ebe7073dc4361e825f955e99a7ef408dbff9703e9a62a0ad5ff4e0747b08` |
| windows-x86_64 | 3,298,304 | `1e74752a1be7cd8d3dd51814a13bc81ff0d8a80ac2f5ca0ca5f8bb7221671d3e` |

`.info` SHA-256 `cff3cdd3459e83c544d18e07b0ac8c52a53b0a69c61629197cb27dc2c43c9ca1`
(`display_version = "0.5.23"`).

## Android APKs

| file | versionCode | size | sha256 |
|---|---:|---:|---|
| `nj2me.apk` | 523 | 3,381,610 | `90e27ffbfb8db6c7a08c94829bf6b90e9261a200c15690ed1c8c353a9a47cacd` |
| `nj2me-legacy.apk` | 523 (0.5.23-legacy) | 720,763 | `a123e7e678272c966ed0db1433fc8f3c0f9f306ba0fb5a3dee04bfe231370118` |

Release zip `nj2me-0.5.23-libretro-cores.zip` 11,959,521 B SHA-256
`50331d2c56bbe7a3f5515637f7e9037d82ae3443ae0166db689b70d51261cd5e` (128
entries, eight targets x 16 files).

## Verification

* full serial CTest: **201/201 passed** (includes the new `test_location` and
  `test_nokiaext` bytecode gates);
* libretro mock matrix **20/20**; option matrix **88/88**; real RetroArch
  1.22.2 LoopDemo 120 frames **PASS**; Windows x86_64 Wine **4/4 at 300
  frames**; submission bundle **32/32**;
* the new API classes are driven through real bytecode by
  `tests/locationdemo/LocationDemo.java` and
  `tests/nokiaextdemo/NokiaExtDemo.java`; the Nokia fixture also provokes and
  catches the two documented exceptions (`GlobalIndicatorsException`,
  `IllegalArgumentException`), so the validation paths are exercised, not
  just the happy paths.

## Upgrade notes

* Save states from 0.5.8 through 0.5.22 are refused by the version CRC; start
  a new state after upgrading.  The save format itself did not change.
* The new classes only add API surface: a title that does not reference them
  is unaffected.

## Upstream submission

* `libretro/libretro-super` PR #2204 (`.info` for 0.5.23)
* `libretro/docs` PR #1239 (library page)
