# nano_j2me 0.5.5

This release collects the performance and compatibility work since 0.5.4.
It includes Linux DPI scaling, CLDC/MIDP and Nokia API fixes, and the Minecraft
rendering corrections described below.

## Performance

- Release builds no longer force the interpreter to `-O2`. A fixed-work run of
  the original Minecraft generator on three seeds used 19.43–20.12% less CPU
  time, with identical chunks, hashes and spawn results. This is a controlled
  desktop comparison, not a promise about every device or game.
- Transformed 2D copies and alpha drawing use incremental source coordinates.
  Fixed-work microbenchmarks show gains on the affected paths; ordinary drawing
  controls retain identical pixels.
- String.getChars copies into the destination without a temporary array.

See [generation evidence](notes/2026-11-09-minecraft-generation-depth.md),
[2D results](OPTIMIZATION_2026-11-09.md),
[String results](OPTIMIZATION_2026-11-09_ROUND2.md), and
[transformed alpha results](OPTIMIZATION_2026-11-09_ROUND3.md).

## Compatibility

- M3G depth survives host frames until the rendering target is rebound or cleared.
  World rendering clears its default background, including a null Background;
  rendering a World through the Node overload retains the Node contract.
- Mesh alpha survives color extraction, clipping, FLAT/Gouraud shading and texture
  composition. The original Minecraft cloud mesh now uses its `0xCC` alpha.
- Depth testing accepts equal depths. Projected window depth, legal reversed and
  constant depth ranges, and polygon factor/units offset are supported across the
  specialized and general raster paths. bindTarget's depthBuffer argument disables
  both depth tests and writes when false.
- Linux desktop UI scaling follows the display/environment settings and supports
  a saved override. Input coordinates follow the scaled UI.
- Java stream boundary, forwarding and modified-UTF behavior, Hashtable GC roots,
  RMS allocation lifetimes, StringBuffer capacity/growth, Sprite sequence bounds,
  image decoder exceptions, Nokia pixel formats/shapes and SoundListener arguments
  have regression fixes.

Detailed M3G evidence: [depth lifetime](notes/2026-11-09-minecraft-generation-depth.md),
[backgrounds](notes/2026-11-09-m3g-background-clear.md),
[mesh alpha](notes/2026-11-09-m3g-mesh-alpha.md),
[LEQUAL](notes/2026-11-10-m3g-depth-compare.md), and
[projected depth](notes/2026-11-10-m3g-projected-depth.md).

## Snapshot compatibility

Game JARs, world files and RMS saves are preserved. Fast VM snapshots are separate
from these game saves. Old snapshots containing a Graphics3D instance are refused
because reciprocal-W depth and an uninitialized old far endpoint cannot be
converted reliably. Restart the MIDlet and load its own save instead. Old
snapshots without a Graphics3D instance remain supported. New snapshots require
the new runtime; older runtimes reject their depth-semantics flag explicitly.

Native network handles are still outside the snapshot restore contract. In the
Minecraft save/resume control, both old and new runtimes can emit caught UDP
receive exceptions after restoring a snapshot. This release does not claim to
fix that networking limitation or all transparency/sorting defects in every game.

## Validation and packages

- **151/151 CTest tests pass** in the Release build; all 982 audited reference
  API members resolve.
- The independent projected-depth fixture passes **13,740 checks** in FAST1,
  FAST0 and ASan/UBSan with explicit float-cast-overflow checking. The same
  fixture exposes 4,904 failures in the pre-fix runtime.
- New snapshot tests pass 95 sanitizer checks and 50 M3G-disabled checks;
  existing snapshot tests pass 2,521 sanitizer checks.
- The original Minecraft JAR runs in SDL2 for 1,600 paced frames using isolated
  copied world/RMS storage. A new snapshot resumes for another 300 frames;
  an incompatible old state refuses safely and starts the MIDlet fresh.
- Linux and Windows packaged binaries run the project's color MIDlet for
  90 frames with identical pixels and correct RGB bar order. Windows is checked
  under Wine, not on a physical Windows machine.
- The packaged Linux x86_64 libretro core passes 36 frontend checks, including
  repeated load/run/unload, real MIDlet frames, input and animated content.
- The final universal APK is installed in an isolated Android API21 x86_64
  emulator. Launcher/import, 240x320 drawing, hardware/overlay press and release,
  and the actual M3G MIDlet fixture (**148 checks, zero failures**) pass. The
  installed APK matches the staged file's SHA-256. The arm64-only APK has
  signature/content/ELF verification; no physical ARM runtime is claimed.

The projected-depth correction has a measured cost on some raster paths:
constant-depth controls range from 14.80% less to 6.72% more time; sloped flat
ARGB costs 17.66% more (0.156 ns/pixel), and sloped textured drawing costs 4.19%
more. See the [18-case table and reproduction command](notes/2026-11-10-m3g-projected-depth.md).
These costs are disclosed rather than presented as a general 3D speedup.

| Download | Contents |
|---|---|
| Linux x86_64 | SDL2 desktop launcher/CLI and five project-authored sample MIDlets |
| Windows x86_64 | Desktop launcher/CLI, SDL2 DLL and the same samples |
| Android universal | arm64-v8a, armeabi-v7a, x86 and x86_64 APK |
| Android arm64-v8a | Smaller arm64-only APK |
| libretro cores | Linux x86_64/aarch64/armv7 and Android arm64/armv7/x86_64 |

Android packages have versionName 0.5.5, versionCode 55, minSdk 21 and targetSdk
34, and retain the preceding release's signing certificate for upgrades.
Linux ELF binaries target glibc 2.17; desktop Linux also needs the distribution's
SDL2, libcurl and zlib runtime libraries. The Linux CLI and native libretro core
reuse the tested GCC objects with Zig's 2.17 link stubs; 12 affected native
regressions and the original Minecraft SDL2 scene also pass after relinking.
The host checks do not certify every older distribution or physical ARM device.

The attached build manifest binds packages to the release commit and records
platform checks; SHA256SUMS covers the downloads. Android APKs and libretro
archives contain no game JARs, RMS or worlds. Desktop samples are authored
fixtures, not redistributed commercial games. GitHub also provides source
archives for the release tag.
