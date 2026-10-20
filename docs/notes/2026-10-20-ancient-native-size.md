# Ancient Empires II: native-size cold-start validation

This follows the [earlier snapshot and rendering investigation](2026-10-17-ancient-empires-replay.md). The original `/home/lucinhu/Downloads/jar/远古帝国2汉化版.jar` remains unchanged, with SHA-256:

```text
778b6e881b556e224de19d898cb65bc38c8bb2c910ec0deb04bc2d7713d66393
```

The JAR's bundled `com.hellomoto.fullscreen.FullCn` overrides `getWidth()` to return 128 and `getHeight()` to return 116. A 240×320 physical surface therefore exposes pixels outside the game's logical drawing region. Cold-start runs at both sizes distinguish this layout effect from the remaining dialogue/input problem. No snapshot was loaded and no runtime source change was made for these comparisons.

## Actual runs

All runs used the original JAR, a new RMS directory for each run, and `--fps 30`. Evidence is under `.local-tools/debug-runs/ancient-layout-round14/validation/`; each run's `command.json` records its full arguments and any capture environment.

| Run | Observed result |
|---|---|
| `long-128`, headless, 128×116, 2100 frames | Prologue and parchment story advanced to a snowy map, then a portrait and blank dialogue panel. 1052 paints, 69.590 s, 30.2 fps. |
| `long-240`, headless, 240×320, same keys and frame budget | Reached the same map/dialogue stage; title/status fragments remained outside the upper-left logical area. 1052 paints, 69.601 s. |
| `long-sdl-128`, SDL2, 128×116, 2100 frames | Visually confirmed the same map and blank dialogue panel. 1052 paints, 71.278 s, 29.5 fps. |
| `input-128`, headless, 128×116, 2460 frames | Further numeric, directional, soft-key and fire input produced no visible change in the sampled map/dialogue frames. 1232 paints, 81.486 s. |

The initial replay presses fire every 30 frames from frame 210 through frame 1950. Frame 780 shows parchment story; frame 1620 shows the snowy map; frames 1800 through 2100 show the portrait/dialogue panel. At frames 780 and 1440, the native 128×116 image exactly matches the upper-left 128×116 crop of the 240×320 control. The map comparisons differ slightly with sprite animation timing. The smaller physical surface removes the exposed title fragments, but the blank dialogue remains.

The extended input run appends:

```text
2101:5 2131:right 2161:down 2191:left 2221:up 2251:soft1
2281:soft2 2311:fire 2341:5 2371:down 2401:right
```

All 13 sampled images from frames 2100 through 2460, at 30-frame intervals, are identical within this run. They do not show unit movement, dialogue dismissal or an operation menu. The sampled executable for `input-128` and `long-240` has SHA-256 `39ca1f56f8bb9fa53f35851f2c08b227041696798d29c359ad2683610bf82f9a`.

The SDL window manager enlarged the physical output despite `--scale 1`; its screenshot is useful as visual confirmation, not an exact framebuffer comparison. `NJ_SDL2_CAPTURE_AT` limits the number of captured images; it does not select a frame number. Selected PNG checkpoints were retained after the owned bulk captures were reduced.

## Evidence and limits

Useful retained images, relative to the validation directory:

- `long-128/frame-01440.png`, `frame-01620.png` and `frame-02100.png`.
- `long-240/frame-02100.png` for the exposed title fragments.
- `long-sdl-128/capture-02099.png` for SDL visual confirmation.
- `input-128/frame-02100.png` through `frame-02460.png` for the input comparison.

The logs contain no uncaught exception or OOM. They still report caught `java/lang/NullPointerException at h.<init>(Ljava/lang/String;I)V pc=72` and fresh-store `RecordStoreNotFoundException` throws. Each run ended because its frame budget expired; exit zero and continuing paints do not establish successful interaction.

The game also maps raw key codes -21/-22 to its soft-key actions, while the generic runner supplies -6/-7 for `soft1`/`soft2`. This is an unresolved input candidate, not a demonstrated cause: numeric 5, fire and directional probes also left the visible panel unchanged. A same-size reference replay and tracing the blank dialogue's text/input path remain useful next checks.

The established result is native-size layout mitigation and cold-start arrival at the map. Dialogue text, unit control, combat, mission completion and save/restart remain unverified. The original JAR and user RMS were preserved.
