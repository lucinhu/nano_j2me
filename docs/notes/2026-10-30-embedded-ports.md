# The embedded ports after a month of desktop work

The ESP32-S3 and Android images are built by hand-written source lists and
build scripts, and neither is in `ctest`.  Rebuilding both after the last
months of runtime work found two real defects and one stale image, which is
exactly what a port that is only "verified at release time" turns into.

## The ESP32-S3 image did not link at all

`idf.py build` for `tests/embedded/idf` stopped at link time with undefined
references to `nj_avcodec_*`, `nj_mp4_*`, and the `nj_lib_pim`,
`nj_lib_samsung` and `nj_lib_datagram` tables.  The component's source list
is written out by hand on purpose - a glob would hide a module that fails to
compile - but nothing compared it with `CMakeLists.txt`, so five rounds of new
modules simply stopped being part of the device image:

* `nj_avcodec.c` and the separated `nj_mp4.c` (the MP4/3GP demuxer moved out of
  `nj_h263.c` under `NJ_MP4_SEPARATE`);
* `nj_mp3.c`, `nj_h263.c`, `nj_m4v.c` (the codecs are on in a default build);
* `nj_lib_datagram.c`, `nj_lib_pim.c`, `nj_lib_samsung.c`.

The list now carries them and sets `NJ_MP4_SEPARATE` (as the desktop build
does) and `NJ_ENABLE_TTF=0` explicitly: a handheld image draws with the bitmap
faces, and stating it here means a future reference to the TrueType rasteriser
is a compile-time absence rather than an undefined symbol.

**The gate that keeps it honest**: `tools/check_port_sources.py` (ctest
`check_port_sources`) compares the desktop `NJ_SOURCES` with the ESP component
list in both directions.  A shared runtime source missing from the device image
fails the suite; a unit that is genuinely desktop-only goes in the script's
`DESKTOP_ONLY` table with a reason (the runner, snapshots, handset detection and
the two TrueType units).  `src/port/*` is exempt in the reverse direction
because each platform names its own port file.

## A portability bug the ESP build found

The new datagram connection formatted a packet's source address with
`getnameinfo(..., NI_NUMERICHOST | NI_NUMERICSERV)`.  On ESP-IDF that is a
declaration without an implementation - lwIP defines no `NI_*` flags and the
component does not link a resolver - so the ESP image failed to compile with
`'NI_NUMERICHOST' undeclared`.  It would also have pulled a name-service
dependency into the per-packet path on every port.

`nj_lib_datagram.c` now writes the numeric form itself from the `sockaddr`
bytes (IPv4 dotted quad, IPv6 eight hex groups, port from `ntohs`), which has
no resolver, no `NI_*` macros and no allocation.  That is the right answer
everywhere: the address a datagram came from is already an address.

## Results

**ESP32-S3** (`tools/run_esp_emulator.sh`, IDF v6.0.1, 16 MB flash / 8 MB
PSRAM): builds, boots under esp-emu and reports

    I nj2me:   after nj_vm_new_midlet classes=286 heap=4128/4128
    I nj2me: frames=0 bytecodes=70514376 natives=1170 heap=89536/89536
    NJ2ME DONE rc=0 frames=0

i.e. the image creates the class library from PSRAM, runs `bench.jar` on the
real instruction set and ends with the runtime's own result line.  Evidence:
`.local-tools/debug-runs/embedded-android-20261030/merged.bin` (1.6 MB image).

**Android** (`tools/build_android_apk.sh`, NDK 30, 4 ABIs): builds
`build/android/nj2me.apk` (3,000,682 B; sha256 in the evidence directory) and
runs on the running API 26 x86_64 emulator:

1. `am start -a VIEW -d file:///data/local/tmp/1942.jar` imports the title
   through `LibraryActivity` (the import dialog appears);
2. "用 240x320 开始" starts `GameActivity`; logcat shows
   `running com.lemonquest.aircombat.GameMIDlet`, `display_init: logical
   240x320, window 320x640`, and the title's own caught
   `ArrayIndexOutOfBoundsException at j.<init>` exactly as on the headless port;
3. the ¿SONIDO? prompt is drawn, tapping the on-screen soft-left answers it,
   the MPEG-4 intro plays and the 1942 AIR COMBAT logo follows;
4. `dumpsys media.audio_flinger` reports an active AudioTrack session;
5. no `FATAL`, no uncaught exception and no escaped exception in logcat.

Evidence: `.local-tools/debug-runs/embedded-android-20261030/`
(`01-library.png`, `02-game.png`, `04-menu.png`, `05-main-menu.png`,
`logcat-filtered.txt`, `audio-flinger.txt`, `apk-badging.txt`,
`apk-sha256.txt`, `device.txt`, `merged.bin`).

Neither port's traffic in `ctest` changes: the ESP gate is the source-list
comparison, and the emulator run stays a release-time check (it needs KVM and a
booted device, not a unit test).

## Limits

The Android smoke test proves startup, one prompt, the intro and the logo on an
x86_64 emulator - not gameplay, not a physical arm64 phone, and the emulator
has no host audio output, so the active AudioTrack proves playback is
*submitted*.  The ESP run is `bench.jar` (the CPU benchmark) under esp-emu, not
a title with a display; the UI demo remains `tools/run_esp_emulator_ui.sh`.
