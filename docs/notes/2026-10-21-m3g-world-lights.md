# M3G: each World is lit by its own Light nodes

`毁灭战士3_10598.jar` reached New Game and drew the HUD, but the 3D world
behind it was pure black. This note records the root cause, the fix, and the
evidence. The original JAR and the user's RecordStores are untouched; the
replay ran headless with an isolated RMS directory at 30 FPS.

## Symptom and first measurement

Menu, crosshair and ammo counter rendered; the world did not. A temporary
`#ifdef NJ_M3G_TRACE` build (removed before the commit) counted what the
rasteriser wrote for the gameplay `World`:

```
r245 World meshes=173 drew=124 px=56786 pxbox=(0,0)-(238,319) mean=(0,0,0) black=56786 nlights=1
```

124 triangles covered 56,786 pixels and every one was black. The setup state
was healthy: material diffuse `0xCCCCCC`, ambient `0x333333`, normals present,
a texture bound, one white light of intensity 1.0. Adding counters at the
lighting rejection points showed why nothing contributed:

```
lit=2304 cone=2304 ndl=0 llz=0 maxl=0
```

Every vertex was rejected by the spot-cone test. The single light in the list
was a `SPOT` (mode 131) that did not belong to the level at all.

## Root cause

`m3g_render_scene()` collected the scene's `Light` nodes only while
`Graphics3D._nj_nlights == 0`, then stored them in the Graphics3D's
`_nj_lights` / `_nj_lmats` fields. Doom renders its menu `World`
(`/mars/mar.m3g`, which contains its own spot light 14) before the level, so
after the first frame the list was non-empty forever. The level `World`
(`/doors/mars.m3g`) never had its own lights collected.

The level's lights come from the game: `Doom/m.wrd()` calls `ll.l(world)`,
which creates a `Light`, sets mode 128 (AMBIENT), intensity 3.0, and passes it
to `World.addChild()` (`javap -p -c Doom/ll.class`). With the level's ambient
light never collected, the only light in the list was the menu's spot; its cone
rejected all level geometry, and the material's ambient term summed over zero
ambient lights, leaving black.

JSR 184's split is the one the renderer had missed: `Graphics3D.addLight()` is
for immediate-mode rendering, while `render(World)` is lit by the `Light`
nodes that are part of that World.

## Fix

`src/midp/nj_lib_m3g.c`:

- `m3g_render_scene()` collects the rendered `World`'s `Light` nodes on every
  World render into stack arrays (`nj_ref slights[16]`,
  `float slmats[16 * 16]`), and passes them through
  `m3g_world_draw(..., slights, slmats, nlights)` to
  `jr_setup(..., const nj_ref *slights, const float *slmats, nj_s4 snlights, ...)`.
- `m3g_jr` carries `const nj_ref *slights` / `const float *slmats`; `jr_lit()`
  reads lights from that set when present and falls back to the Graphics3D
  arrays for immediate mode.
- The old collect-once block now runs only for non-World node rendering, and
  the Graphics3D `addLight` list is left alone by World rendering.
- No heap allocation was added (stack arrays only, 16 lights as before).

Immediate-mode callers (`m3g_render_jr`, `jr_sprite`) pass `NULL, NULL, 0` and
keep the previous behaviour.

## Results

With the fix the same trace reports `cone=0 maxl=153 mean=(25,25,26)
black=0`, and the production build shows the Doom corridor: textured floor and
ceiling, door frame, crosshair, ammo HUD (image
`.local-tools/debug-runs/doom-lights-fix/game1.ppm`, 240x320).

- Menu regression: `doom-lights-fix/menu.ppm` is byte-identical to the
  pre-fix `doom-round17-play/menu1/01-menu.ppm` (pixel SHA-256
  `617e60d8753d85518d8d106d...`), so the menu World path is unchanged.
- Cost A/B, same bridge script (`wait 16000; down 200; wait 350; fire 100;
  wait 1500; quit`) with `--stats`: before `frames=523 paints=360 gfx=1591
  tris=91921 m3g=5432 time=18.424s fps=28.4`; after `frames=522 paints=359
  gfx=1587 tris=92172 m3g=5306 time=18.436s fps=28.3`. The per-render light
  walk is below measurement noise.
- `ctest` 86/86 passed, including `test_m3g`, `test_m3g_file`,
  `check_m3g_reals`, `check_m3g_api` and `check_m3gdemo`.
- A second real title, `3D拉力赛-进化汉化版_6366.jar`, replayed with its
  416-event script (3650 frames, isolated RMS): 30.2 FPS, tris=141,002,
  m3g=16,368, 29 distinct hashes among 36 sampled frames, and the race scene
  still shows the textured car, snowy road, fence, sky and HUD
  (`.local-tools/debug-runs/rally-lights-fix/rally-3300.png`), so the
  per-World light set did not regress a single-World title.

The level is also interactive now: after New Game, holding UP for 800 ms
changed the view (`g0.ppm` vs `g1.ppm`, distinct hashes) and then LEFT changed
it again (`g2.ppm`); a later FIRE press left the sampled frame unchanged after
release, so firing visuals were not confirmed. Evidence frames are in the same
directory (`doom-lights-fix/g0..g3.ppm`).

## Regression test

`tests/test_m3g.c` now builds two Worlds, each with its own mesh and one
ambient `Light` (red in the first, green in the second), renders A then B into
a 16x16 target, and checks the pixels: A red only, B green only, no red leak.
The pre-fix renderer fails exactly the two "second World" checks (the first
World's light stayed selected); the fix passes them.

## Remaining limitations

- The file loader still reads only a Light's mode, colour and intensity, so a
  spot light authored in an m3g file uses the constructor defaults
  (spot angle 45 degrees, exponent 0, constant attenuation 1). That was not
  the cause here - the mis-selected light was the bug - but a title that
  depends on the authored spot parameters would still look different.
- Doom's world stays very dark because the game lights it with one ambient
  light of intensity 3.0 and relies on its textures; this note only claims the
  world is drawn, not that its look matches a handset.
