# nano_j2me 0.5.33 — libretro r52

Release date: 2026-11-22. Tag `v0.5.33`.

This release adds a **deterministic timing mode** to the libretro core: the
baseline needed to decide whether replay/netplay can be supported honestly.
It also documents the measured determinism limits and keeps the upstream
submission bundle in sync.

## Highlights

* **`nano_j2me_deterministic` core option** (`System`, `disabled`/`enabled`,
  default `disabled`). When enabled, `retro_run()` advances the MIDlet virtual
  clock by exactly one 60 Hz tick (16667 µs) instead of the wall-clock delta,
  so the number of virtual frames per frontend call is fixed. The MIDlet
  `frame rate limit` is switched to unlimited internally because the fixed
  tick itself is the pacing; the A/V geometry still reports 60 fps.
* **Deterministic `java.util.Random` seeding.** The default `Random()`
  constructor is seeded from a VM-level 64-bit LCG counter instead of the
  wall clock when deterministic mode is on (`nj_vm_set_deterministic`), so
  games that create `Random` instances without an explicit seed replay
  identically. Explicit seeds and `Math.random()` are unchanged.
* **`check_libretro_determinism` regression gate**: two runs of
  `alphademo.jar` through the real core with the option enabled must present
  byte-identical frame dumps; the gate also verifies the core advertises the
  option. The upstream `libretro/docs` page documents the option (20 options
  now on the page) and the tracked docs patch was regenerated.
* **Measured status (honest)**: frame-driven titles are byte-identical
  (alphademo 61/61 frames, 家园5 77/77, 雷霆战机2012BT 107/107; a 137-frame
  run had one trailing teardown difference). Titles that sample
  `System.currentTimeMillis()` inside paint (loopdemo) or teardown timing
  still vary by a few frames, because the VM frame and the frontend tick are
  not frame-locked. **Netplay is therefore not advertised** — this release
  provides a reproducible baseline and a documented roadmap item, not a
  playable netplay implementation.

## Artifacts

### libretro cores (`nj2me-0.5.33-libretro-cores.zip`, 12,164,891 bytes)

SHA-256: `73bd4fd9e051c76195a3d60af5a117b4198aa53a6d5c1024c0a50f9fe1ab88fe`

| target | size | SHA-256 |
|---|---|---|
| linux-x86_64 | 4,044,728 | `ded8842337844a1b32269c3c6c3145a1a999826f837f47f6004731dd39e213e5` |
| linux-aarch64 | 3,517,768 | `41cc08991fd4accb15b56fac8c1de6bb2c80f0e47b50677d8e4b21d4d8394cd0` |
| linux-armv7 | 3,172,036 | `95244a865417e24c0bb8e6e2c77d5aea184358a50b031ad6ac2b79d4675d638d` |
| android-arm64 | 3,763,336 | `289b3ddca1ae9c884e31e04b5333beb11c4671be7b129804d459464629bada8d` |
| android-armv7 | 2,861,736 | `f940404ca1623b5a43e93a503c04ccacee971c951110bf33248107784cbb2276` |
| android-x86_64 | 3,789,448 | `0574c318c3cef1606447b454109ee6f2ea321853906a62b449646406cf19f1e1` |
| windows-i686 | 3,596,288 | `b85c723a9993fddaf49aa3c644785939e15e04bca4011ba5b83bf4733929a4da` |
| windows-x86_64 | 3,366,912 | `030dd5467da7c3270ad9570d5c6e9e31cae3a9c5a3578482d002d622284827fa` |

The tracked `libretro-super` patch adds `dist/info/nano_j2me_libretro.info`
for 0.5.33 (`.info` SHA-256
`b2a787ef50284497e768a6061a0d198c4c26949eb18b5768aa619d44ca9e49d8`) and is
applied on top of `libretro-super` master `55e50a8`.

### Android APKs (versionCode 533, per ABI)

| APK | size | SHA-256 |
|---|---|---|
| `nj2me-0.5.33-arm64-v8a.apk` | 1,054,876 | `15c14f29e784fe912b9a86515817c2afe2e97f902553fca4261e2b20483e3ba1` |
| `nj2me-0.5.33-armeabi-v7a.apk` | 1,063,070 | `4b8d5d63cacf599b08ac5b7f78205c29c14e430d811636a63074a414a6369b02` |
| `nj2me-0.5.33-x86.apk` | 1,042,582 | `241acb864867394c589c350271c97f01bf45f478cd3841b5a91165dea2b66d20` |
| `nj2me-0.5.33-x86_64.apk` | 1,058,969 | `c859a1fdf8f98dfac7ec0eb9a0e57e6cf1ee740986254d8afb16b83389d9c44f` |
| `nj2me-0.5.33-legacy.apk` | 740,040 | `4e61e587193d212c9d9e4a806d3827f00f37fc3369ae18d31bddbc9a0c9114f8` |

The four phone APKs are GLES2-backed (hardware acceleration on by default;
the in-game panel can force software rendering). The legacy ARMv5/API 5 APK
stays software-rendered. All are debug-signed and ship without games.

## Verification

* Full serial CTest on the release tree: **210/210 passed** (includes
  `check_libretro_determinism`, the member audit gate and the option-docs
  gate; see the attached log).
* libretro mock frontend matrix: **37/37 checks passed**.
* Real RetroArch 1.22.2, `loopdemo.jar`, 120 frames: **PASS**.
* Windows x86-64 PE under Wine, 300 frames: **4/4 cases passed**
  (`loopdemo.jar`, `audiodemo.jar`, PE format and exports).
* Submission bundle validator: **32/32 checks passed**.
* Deterministic mode measurements (two runs per title, per-frame sha256):
  alphademo 61/61 identical, 家园5汉化版 77/77, 雷霆战机2012BT 107/107
  (137-frame run: one trailing difference); loopdemo remains nondeterministic
  because it reads the wall clock inside paint.
* The rest of the engine is unchanged since 0.5.32; the corpus verdict set is
  expected to be identical (re-measured separately in the compatibility
  matrix workflow).

## Upgrade notes

* Save states written by 0.5.8–0.5.32 are refused by the version CRC check;
  the format version is unchanged in 0.5.33 and states are not compatible
  across versions.
* The deterministic option requires a restart of the frontend/core session
  after changing it (libretro core options are read at load).
* Deterministic mode is opt-in; the default runtime behaviour (wall-clock
  virtual time, wall-clock RNG seeding) is unchanged.

## Known limitations

* Netplay is **not** advertised. Frame-locked single-step execution (one
  MIDlet frame boundary per `retro_run`) is the missing piece; an attempted
  handshake is documented in
  `docs/notes/2026-11-22-deterministic-timing.md`.
* `MidnightPool1`'s paint busy-wait on `System.currentTimeMillis()` still does
  not complete under the virtual clock (documented paint-clock limitation).
* Long-frame titles (`Asphalt4EliteRacing`, `毁灭战士3_10598`) refuse
  save-state requests while a frame runs longer than the 4 s service timeout.

## Upstream submission

* `libretro-super`: branch `add-nano_j2me` in `lucinhu/libretro-super`,
  commit `b32e30c` (adds `dist/info/nano_j2me_libretro.info`).
* `libretro/docs`: branch `add-nano_j2me` in `lucinhu/docs`, commit
  `95a1d0a` (page, core list, license row, see-also entry, mkdocs nav).
* Pull requests: `libretro/libretro-super#2204` and `libretro/docs#1239`;
  both are open and marked mergeable, and nothing is claimed to be merged.

## Links

* Release: <https://github.com/lucinhu/nano_j2me/releases/tag/v0.5.33>
* Web demo: <https://lucinhu.github.io/nano_j2me/>
* ESP32-S3 port: <https://github.com/lucinhu/esp32s31-korvo-nano-j2me>
