# nano_j2me 0.5.8 — libretro core refresh, crash hardening, synchronized callbacks

Released 2026-11-16. This is the first release that ships the accumulated
round-12…round-54 libretro work together with two real defects found by the
fuzzing campaigns and a JNI-bridge callback fix.

## Highlights

### libretro core

* **Manual save states work while the frontend is paused** — save/load is served
  at frame boundaries with a bounded virtual-clock assist, including titles
  whose whole loop runs inside `startApp()` (async start) and a 60 Hz paced
  state-probe tool that models a real frontend.
* **SYSTEM_RAM exposed for frontend cheat search** — the JVM heap is a single
  non-moving arena; it is advertised through `retro_get_memory_data` and a
  `RETRO_ENVIRONMENT_SET_MEMORY_MAPS` descriptor (`memory_descriptors = "true"`),
  re-published after a state load. Code-based cheats are not claimed.
* **Rewind sizing guidance** — frontend rewind is usable when the state fits the
  rewind buffer (`nano_j2me_heap=16` typically does); auto-heap states exceed
  the default buffer. A smoke check scripts the measurement.
* **Device profiles** — `com.nokia.*` capability answers plus an automatic
  nokia6230 retry for titles that fail to load on the generic handset.
* **Robust loading** — the MIDlet is constructed on the VM thread, so a
  loading-screen constructor cannot block `retro_load_game`; a constructor that
  runs the game loop is waited out with the documented Doom3 state limitation.
* **Reproducible Windows builds** — PE artifacts are byte-identical between
  clean builds (timestamps suppressed at link and strip stages).
* **Exact upstream submission bundle** — `.info`, CI templates, libretro-super
  patch and docs-library page validated against the upstream indexes; the two
  pull requests were opened after the author's explicit authorization.

### Emulator core

* **M3G software raster fast paths** — Gouraud textured span fast path
  (-4.6 % CPU on the Doom3 loader benchmark) and direct opaque clear
  (-1.3 %), both validated on the deterministic frame prefix.
* **Array/object operand hardening** — type-invalid bytecode produced by a
  fuzzer (no Java bytecode verifier here) is rejected with `VerifyError`
  instead of dereferencing a garbage pointer; `arraylength`, `athrow`,
  `checkcast`, `instanceof`, `monitorenter`/`monitorexit` covered too.
* **GIF canvas budget** — malformed GIF headers can no longer demand a
  multi-GB canvas before allocation (4096×4096 pixel cap, same as PNG).
* **Synchronized callbacks** — MIDlet callbacks invoked through the C bridge
  now acquire the monitor their Java declaration requires, so a
  `synchronized paint()` calling `notify()` works; a bounded 100 ms wait with
  unlocked fallback keeps titles whose display and game threads wait on each
  other from deadlocking the frame painter.

### Test and validation infrastructure

* Fuzzers for malformed classes (single/multi-locus, any class in the JAR),
  JAR containers and media decoders (AMR/WAV/ADPCM/MIDI/PNG/JPEG/GIF), all
  registered in CTest and exercised with 5000-case campaigns (clean).
* Corpus probes: 404-title sweeps, motion/audio/deep/long-run probes,
  libretro visual/state/option-matrix/rewind/save-state checks, CI template
  simulations for Linux/Windows/Android.
* Final round-54 sweep: **372 ok / 27 title-exit / 2 title-exception /
  2 no-frame / 0 timeout / 1 bad-jar** (all non-ok rows adjudicated
  game-side or by design).

## Artifacts

`nj2me-0.5.8-libretro-cores.zip` contains the eight libretro cores with
metadata and licenses:

| Target | Core | Size | SHA-256 |
| --- | --- | --- | --- |
| linux-x86_64 | `nano_j2me_libretro.so` | 3,843,912 | `2960da47…` |
| linux-aarch64 | `nano_j2me_libretro.so` | 3,324,488 | `19221681…` |
| linux-armv7 | `nano_j2me_libretro.so` | 3,032,420 | `6d17fad8…` |
| android-arm64 | `nano_j2me_libretro.so` | 3,557,272 | `1db91922…` |
| android-armv7 | `nano_j2me_libretro.so` | 2,745,044 | `29919a1c…` |
| android-x86_64 | `nano_j2me_libretro.so` | 3,597,520 | `b837ae50…` |
| windows-x86_64 | `nano_j2me_libretro.dll` | 3,227,136 | `6efd4ca6…` |
| windows-i686 | `nano_j2me_libretro.dll` | 3,451,904 | `5f0c8f66…` |

`.info` SHA-256 `48ceccf6fc7110a605809c90877a4e16d57ff5e0275503c88ac1f6385790b791`,
`display_version = "0.5.8"`. The zip also contains `SHA256SUMS`.

## Verification

* libretro mock frontend conformance matrix: 20/20 checks pass.
* Real RetroArch 1.22.2: alphademo 300 frames PASS (load/run/exit markers).
* Windows x86_64 core under Wine: 4/4 fixtures pass.
* Full 404-title corpus sweep: distribution above; no timeouts.
* Full serial CTest: 189/189 (see the release evidence in
  `docs/LIBRETRO_SUBMISSION.md` and the round notes).
* libretro-submission bundle validator: patches apply to the upstream
  baselines and the docs site builds; the upstream PRs are
  `libretro/libretro-super#2203` and `libretro/docs#1239`.

## Known limitations

* `毁灭战士3_10598.jar` spends its MIDlet constructor joining loading threads
  that render a long software-M3G animation; loading can take far longer than
  the frontend's default timeout, and manual state save during that load is
  refused (documented).
* Save/load inside a nested paint (e.g. a paint that opens a dialog) remains a
  documented limitation.
* A fast-forward/unpaced frontend can snapshot a VM whose MIDlet construction
  has not completed; such early states may fail to restore. Real 60 Hz
  frontends are unaffected (diagnosed in
  `docs/notes/2026-11-15-libretro-async-load-early-state.md`).
* No netplay, runahead, code-based cheats/achievements/Disc support.
* Rewind works only when the state fits the frontend's rewind buffer.

## Upgrade notes

* Save states written by earlier versions contain a version CRC and are
  refused by 0.5.8 (expected for a new engine version); they are not
  migrated.
* The libretro core options are unchanged (`nano_j2me_heap`, `screen`, `locale`,
  `rotation`, `device`, `fps`, `sound`, `pointer`, `renderer`, …).
