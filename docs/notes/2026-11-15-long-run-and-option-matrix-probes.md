# Long-run and core-option matrix probes, 2026-11-15

The sweep (`tools/sweep_compat.py`) runs every title for a fixed frame budget,
and the libretro probes so far either ran the mock frontend for 120–240 frames or
validated the harness itself. This note records two wider probes done after the
r14 artifact refresh, and the two tools they produced.

Machine: same host as the other 2026-11-15 evidence, `build/libretro-dist`
r14 artifacts, headless CLI built from the same tree.

## Long-run corpus probe

`tools/check_corpus_deep.py` samples titles that the r9 sweep marked `ok` and
runs each one far beyond the sweep window, paced at a real-time cadence so
titles whose loop is paced by `Thread.sleep` actually execute:

```sh
NJ_BIN=/var/tmp/nj2me-goal13-build/nj2me \
  tools/check_corpus_deep.py .local-tools/debug-runs/compat-20261115-r9 \
  --count 24 --seed 31 --frames 1800 --fps 30 --jobs 8 \
  --out .local-tools/debug-runs/goal15-deep/deep-probe-24.json
```

1800 frames at 30 fps is 60 s of emulated time per title.

- 24-title sample (seed 31): **24 passed, 0 early exit, 0 failed**, frames
  1800/1800 each, no new uncaught exceptions, no timeouts.
- 48-title sample (seed 41): **46 passed, 2 early exit, 0 failed**. The two
  early exits are key-script/menu results, not crashes: `美男狂想曲` (manifest
  name `Hotguys Rhapsody`) ends at frame 58 and `哆啦A梦-3D卡丁车` ends at frame
  321 after calling `notifyDestroyed()` because the sweep key script presses a
  menu control that exits the MIDlet. Both are already handled by the sweep's
  game-side classification.
- Static screens are expected and not failures here: `STAR` and `恋爱物语` paint
  2 frames in the whole run because their screen does not change while the
  selection menu waits for input.

## Core-option matrix probe

`tools/check_libretro_option_matrix.py` runs real titles through the mock
libretro frontend under every non-default core option, with `--visual-required
--static-ok` so a blank screen shows up as a failure. Profiles that change the
reported geometry pass the matching harness expectation (`--expect-screen` for
`nano_j2me_screen`, `--rotation 90` for `nano_j2me_rotation=90`), because the
harness's default conformance check expects the default 240x320 base.

```sh
tools/check_libretro_option_matrix.py .local-tools/debug-runs/compat-20261115-r9 \
  --count 8 --seed 5 --frames 240 --jobs 6
```

Eight corpus titles (雷霆战机2012BT版, 灵异村庄, 浴火银河2完整版, 超级马里奥,
拳皇98终极之战, 仙剑奇侠传完美版, 美女滑雪胜地, 植物大战僵尸BT版) × eleven
profiles (default, screen176, screen320, rot90, rot180, heap32, font-builtin,
sound-off, pointer-off, device-nokia6230, locale-zh) = **88 runs, 88 passed, 0
failed**. Every option combination kept the picture non-black and animated, and
every run also executed the harness's serialize/unserialize round-trip checks.

## What this means

- No late crash, hang or new uncaught exception appeared in 72 title-runs of 1800
  frames (roughly 72 minutes of emulated time), and no core option broke
  rendering or save states on the sampled titles.
- These are sampling probes, not exhaustive guarantees: the sweep remains the
  broad check, and the option matrix uses eight titles because each profile adds
  a full harness run.
- The two tools are tracked so the same evidence can be regenerated with one
  command each.
