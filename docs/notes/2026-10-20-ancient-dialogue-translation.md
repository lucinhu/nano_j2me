# Ancient Empires II: translated antialiased text

The blank dialogue reported in [the native-size investigation](2026-10-20-ancient-native-size.md) came from the coverage pixel path omitting `Graphics.translate()`. The original game now displays its Chinese dialogue in both headless and SDL2 runs. Replays also verify scrolling and a transition to another speaker.

## Reproduction and cause

The original `/home/lucinhu/Downloads/jar/远古帝国2汉化版.jar` has SHA-256 `778b6e881b556e224de19d898cb65bc38c8bb2c910ec0deb04bc2d7713d66393`. Its bundled `FullCn` reports a logical size of 128×116. All new runs used that physical size, a 16 MiB heap, fresh isolated RMS directories and `--fps 30`.

A local FreeJ2ME-Plus comparison at commit `baca76a4dda6f30abac67fb4a3db009eed05f91c`, with `-Dfile.encoding=ISO_8859_1`, displayed dialogue where nano showed an empty panel. An independent executable interposed on `nj_vm_create` and wrapped the native `Graphics.drawString`; no game bytecode or production instrumentation was needed. At frame 1710 it recorded:

- Valid strings `快来救救我们！` and `我们正遭受攻击`.
- User positions `(76,6)` and `(76,20)`, with anchor 17 (`HCENTER|TOP`).
- Translation `(0,77)` and user clip `(25,0,103,36)`.
- A game call chain through `e.b` and `d.a(Graphics;IIZ)V` into `Graphics.drawString`.

`gfx_load` already converted the user clip into device coordinates correctly. The glyph blitter passed user coordinates to `nj_gfx_blend_coverage`, which tested those coordinates directly against the device clip. The text was therefore clipped out above the panel. Ordinary 1-bit glyphs went through `nj_gfx_fill_rect`, which already applied translation.

The coverage helper now applies `nj_gfx_dev_x` and `nj_gfx_dev_y` before clipping, following the same bounded coordinate conversion as the other primitives. Its public declaration now explicitly documents user coordinates. This fixes translated Latin and CJK coverage fonts without changing font metrics or bitmap data.

## Regression and resource cost

`test_translated_text` in `tests/test_font_coverage.c` compares translated drawing against drawing the same mixed Latin/CJK text at explicit device positions. It covers 1-bit, 8-bit coverage and packed 2-bit glyphs, three surface formats, positive/negative translation, the game's `(0,77)` offset, clipping, source alpha and XOR. The selected visible pixel assertion also catches an accidentally empty result on both sides.

The expanded test has 467 assertions: 108 failed before the production fix and all pass after it. The full CTest suite passes all 85 tests. Font coverage and fallback tests also pass in the monochrome configuration and with ASan/UBSan and leak detection enabled.

With identical compiler settings, the graphics object changes from 31,592 to 31,720 bytes of text (+128 bytes); data and BSS remain zero. The fix adds no allocation, lookup table or persistent state. Existing font feature switches are preserved.

## Original-game verification

Evidence is retained under `.local-tools/debug-runs/ancient-dialogue-round15/`. Commands and logs are stored inside each run directory. The relevant runs were:

| Run | Result |
|---|---|
| `trace-128-bound` / `trace-model`, before fix | Valid dialogue calls, but frame 1800 shows a portrait and empty panel. |
| `after-headless-numeric`, after fix | 2460 frames, 1232 paints, 81.488 s, 30.2 fps. Frame 1800 shows the Chinese call for help; frame 2460 shows a different speaker saying `啊啊……我们` / `完蛋了……`. |
| `after-sdl2`, after fix | 2460 frames, 1231 paints, 81.943 s, 30.0 fps. SDL captures show the call for help and later scrolled dialogue. The window manager enlarged the output despite `--scale 1`. |

The stable headless replay uses numeric `5` every 30 frames from 210 through 1950, then `2101:down 2161:down 2221:down 2281:down 2341:5 2401:down`. An earlier headless attempt with `fire` took a different menu path and eventually called `notifyDestroyed`; it was not used as evidence of dialogue success. The SDL replay used the recorded fire/directional sequence. These scripts demonstrate the visible dialogue states, not deterministic timing across ports.

The successful runs contain no uncaught exception or OOM. The previously observed caught `h.<init>(String,int)` NullPointerException and fresh-store exceptions remain in the logs. The scope verified here is dialogue rendering, scrolling and a speaker transition; unit control, combat, mission completion and save/restart still need separate checks. Original JAR bytes and user RMS were preserved.
