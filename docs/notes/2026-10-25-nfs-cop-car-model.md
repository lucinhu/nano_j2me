# NFS Hot Pursuit: no opponent/cop car model on the start grid (unresolved)

`极品飞车14之热力追踪中文版_4160.jar` (EA `com.ea.nfs10.MonkeyApp`): the user
reports that during a race some cars have no model, the police car in
particular. This note records what the emulator-side evidence shows and what
is still missing; **it does not claim a fix**, because no emulator defect was
proven.

Menu, event selection, car selection and the race itself all render: the
car-select screen draws the game's own `car_*.m3g` preview model with body,
wheels and shadow, which says the loader and the renderer handle those assets.
Main menu and HUD are 2D overlays, so the report has to be about in-game cars.

## Repro

Headless replay, 240x320 at 30 FPS, fresh RMS directory, the keys the Lead
used for the cop-race repro:

```
./build-lead-render/nj2me --port headless --size 240x320 --fps 30 \
  --rms-dir "$RUN/rms" --run-frames 3200 \
  --keys '120:soft1:down,124:soft1:up,600:down:down,606:down:up,700:fire:down,704:fire:up,
          760:soft2:down,766:soft2:up,900:fire:down,904:fire:up,1000:fire:down,1004:fire:up,
          1100:fire:down,1104:fire:up,1200:fire:down,1204:fire:up,1300:fire:down,1304:fire:up,
          1400:fire:down,1404:fire:up,1600:fire:down,1604:fire:up,1800:fire:down,1804:fire:up,
          2000:fire:down,2004:fire:up,2200:fire:down,2204:fire:up' \
  --dump-frames "$RUN/frames/" --dump-every 20 --stats \
  tests/games/极品飞车14之热力追踪中文版_4160.jar
```

Race appears around host frame 1800 on the forest track; HUD shows
`排名第4/4`, i.e. four racers exist. Every dumped frame after that shows only
the player's car. The same is true of the F2P reference at race start, so that
alone does not prove the other three should be on screen.

## What the scene graph shows

Temporary `M3GMESH` dump (per collected mesh: userID, class, world-space
translation from the collected matrix, triangles rasterised) around host
frames 1800-2300, run `.local-tools/debug-runs/nfs-ai-05`:

* The race `World` collects 56-75 `Mesh` nodes per frame (one render per host
  frame, 618 race renders logged). The walker is not truncating: the 64-Mesh
  cap was fixed earlier today (see `2026-10-25-doom3-render.md`).
* Car-sized assemblies sit close to the player's car, e.g. at
  `(1.6, 0, 6.4)` (8 meshes, 272 triangles rasterised), `(-6.4, 0, 6.4)`
  (3 meshes, 48 triangles), `(-13.6, 0, 8.8)` (5 meshes, 145 triangles).
* One assembly at `(14.4, 0, 6.4)` (7 meshes) logs `tris=0` in every frame --
  collected, but nothing rasterised.
* The player car body is near `(6.8, 0, 6.1)` (98 triangles); its x follows the
  track while driving, so world x is the track axis and z is the lane.

## The one suspicious node

One mesh (a 137-triangle body, i.e. car-body sized) is at
`x = -49.6228, z = 6.1000` with an animated y:

| host frame | world translation | triangles |
| --- | --- | --- |
| 1800 | (-49.6228, 30.5100, 6.1000) | 0 |
| 2000 | (-49.6228, 16.4100, 6.1000) | 155 |
| 2100 | (-49.6228, 11.8300, 6.1000) | 137 |
| 2200 | (gone from the list) | - |

The values are finite (no NaN/Inf), x and z are exactly constant, y descends
smoothly, and the node *does* rasterise once low enough; while y=30 it is
simply above the viewport. z = 6.10 is the player's lane plane and x = -49.62
is ~47 world units ahead of the player along the road, so this reads as an
object one grid slot ahead whose vertical offset is animated, not as a mangled
matrix. It does not look like the three missing opponents, and no second copy
of that node was seen.

## Verdict and missing evidence

Emulator defect: **not established**. Nothing observed drops AI car meshes:
they are collected, three of the four car-sized assemblies rasterise, and the
suspicious node rasterises when in view. What could still be ours:

1. The `(14.4, 0, 6.4)` assembly never rasterises. It may be authored that way
   (submeshes with a null `Appearance`/`IndexBuffer` are skipped by
   `m3g_world_draw`) or hidden with `setRenderingEnable(false)`. Per-mesh skip
   reasons (hidden / null appearance / null index buffer / `jr_setup` failure)
   were not captured in a valid run: the logging binary was built with the
   wrong CMake target (`nj2me` is the static library; the executable is
   `nj2me-cli`), so the last two instrumented runs silently used the previous
   binary.
2. The setTransform `float[16]` values the game passes for the animated node
   were not logged, so we cannot say whether the game intended that position.
3. No F2P or real-device race-start frame was found that clearly shows whether
   opponents should be visible at that moment.

If someone continues: re-add the `M3GMESH` dump (saved as
`.local-tools/wip/nfs-cop-instrumentation.patch`) and log, per mesh,
`_nj_hidden`, submesh count, null-appearance/null-index-buffer skips and
`jr_setup` failures for the `(14.4, 0, 6.4)` assembly, plus the node's
`setTransform` inputs keyed by node ref. Do not special-case the JAR.

## Follow-up: the cause was the Transformable model, not the walker

The verdict above ("no emulator defect proven") was written before the
`Transformable` component model was fixed later the same day.  It is now
superseded: the missing/tiny cars were not a mesh-collection problem at all.

`Transformable` stored a single composite matrix, so `setTransform()`
clobbered the scale/rotation components a node had been given, and
`getScale()`/`getOrientation()` read values out of a matrix that still had the
translation baked in.  NFS HP clones every car node (its `ah.b(Node)` helper
round-trips `getTranslation`/`getOrientation`/`getScale`) and then places it
each tick with `setTransform()`, while `ad.class` itself does
`setScale(0.0035f, ...)` followed by `setTransform(...)`.  Copies therefore
came back mis-scaled and landed as invisible or tiny models.

After the fix (`src/midp/nj_lib_m3g.c`, five pieces `T * R * S * M`, see
`docs/notes/2026-10-25-m3g-transform-components.md`), the same 3200-frame race
replay `.local-tools/debug-runs/nfs-fix-cop-01/frames/01800.ppm` shows three
rival cars on the grid where `.local-tools/debug-runs/nfs-lead-cop-04/frames/01800.ppm`
had an empty road; `stats tris` 271584 -> 355838.  The `(14.4, 0, 6.4)`
assembly and the animated grid-slot node mentioned above were red herrings;
nothing in `m3g_world_draw()` needed to change for this title.
