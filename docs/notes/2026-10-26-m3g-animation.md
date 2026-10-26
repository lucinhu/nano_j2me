# M3G: keyframe animation (`Object3D.animate`) is now implemented

`极品飞车14之热力追踪中文版_4160.jar` (NFS Hot Pursuit) drives its car rigs
entirely through JSR-184 animation: every car's suspension, steering and wheel
nodes are `AnimationTrack`s that the game samples with `Object3D.animate(int)`
once per frame. Our `animate()` was a native stub returning 0 and the loader
never decoded the animation objects, so the cars stayed in their file bind
pose. That bind pose puts the wheels 0.085 world units **below** the road
plane (y = 0), which is the "wheels sunk into the road" the user reported.

This note records the file-format details, the implementation, and the
before/after measurement. The original JAR and the user's RecordStores are
untouched; the replays ran headless with an isolated RMS directory at 30 FPS.

## Symptom and measurement

A temporary `NJ_M3G_YMIN` hook in `m3g_world_draw()` (removed before finishing;
patch kept in `.local-tools/wip/nfs-anim-fix.patch`) prints each collected
mesh's world-space y range. Before the fix, in the tutorial race with the car
standing still at the start line:

```
YMESH i=24 cn=javax/microedition/m3g/Mesh lo=-0.026 hi=0.174 n=684  t=6.80,0.00,6.10   (body)
YMESH i=25 cn=javax/microedition/m3g/Mesh lo=-0.085 hi=0.037 n=90   t=6.58,-0.02,5.98  (front wheel)
YMESH i=26 cn=javax/microedition/m3g/Mesh lo=-0.085 hi=0.037 n=90   t=7.01,-0.02,5.98
YMESH i=27 cn=javax/microedition/m3g/Mesh lo=-0.085 hi=0.037 n=90   t=6.58,-0.02,6.22  (rear wheel)
YMESH i=28 cn=javax/microedition/m3g/Mesh lo=-0.085 hi=0.037 n=90   t=7.01,-0.02,6.22
```

The road tiles in the same render are flat at `lo=hi=0.000` (480-vertex
strips), so the wheels were 0.085 units inside the road: about 55 % of the
wheel radius (the wheel diameter is 0.122 units).

## Root cause

Two gaps, one in the loader and one in the runtime:

1. `nj_m3g_decode_object()` (`src/midp/nj_lib_m3g.c`) had no cases for object
   types 1 (`AnimationController`) and 19 (`KeyframeSequence`), and its
   `AnimationTrack` case (type 2) consumed only the two object references, not
   the target property.
2. `NJFN(m3g_animate)` was a stub (`ret->i = 0`). The Java
   `Object3D._nj_tracks` vector existed but file-provided tracks were never
   attached to the nodes that own them, and no sampler drove
   `_nj_t / _nj_r / _nj_s`.

The bind pose therefore rendered with the car body slightly sunk as well
(body `lo=-0.045`), because the rig groups that position the car sit at their
authored neutral translation until an animation track moves them.

## File format (verified against FreeJ2ME-Plus' loader and our own dumps)

FreeJ2ME-Plus `.local-tools/freej2me-plus/src/javax/microedition/m3g/Loader.java`
was used as the format oracle, then the layouts were confirmed byte-for-byte
on `animrig_car.m3g` from the NFS jar (42 objects: 11 `KeyframeSequence`, 11
`AnimationController`, 11 `AnimationTrack`, 9 `Group`):

```
AnimationController (type 1), after the Object3D header:
    f32 speed; f32 weight; s32 activationTime; s32 deactivationTime;
    f32 referenceSequenceTime; s32 referenceWorldTime

AnimationTrack (type 2), after the Object3D header:
    s32 keyframeSequenceRef; s32 animationControllerRef; s32 property

KeyframeSequence (type 19), after the Object3D header:
    u8 interpolation; u8 repeatMode; u8 encoding;
    s32 duration; s32 validRangeFirst; s32 validRangeLast;
    s32 components; s32 keyframeCount;
    encoding 0: keyframeCount x ( s32 time; f32[components] value )
    encoding 1: f32[components] bias; f32[components] scale;
                then keyframeCount x ( s32 time; u8[components] q ),
                value = bias + scale * q / 255
    encoding 2: same, but q is u16 and divided by 65535
```

`Object3D`'s file header carries the userID, the animation-track object list
and the user parameters; the refs use the same `object number + 2` numbering
as every other M3G reference (`m3g_ref_mode_of()` / `m3g_resolve_ref()`
already handle it). The NFS rigs only use `encoding == 0` with `LINEAR`,
`SLERP` and `SPLINE` interpolation, but the quantised encodings are decoded as
well because they are cheap and other titles use them.

The property numbers are the ones already in `atrack_fields[]`
(`ALPHA=256 … TRANSLATION=275, VISIBILITY=276`); the rig files use
`TRANSLATION=275`, `ORIENTATION=268` (4-component quaternion) and
`SCALE=270`.

## What changed

`src/midp/nj_lib_m3g.c`:

* new type constants `NJ_M3G_T_ANIMATION_CONTROLLER` (1) and
  `NJ_M3G_T_KEYFRAME_SEQUENCE` (19), plus decode cases that validate the
  payload (`m3g_object_header_size()` normalisation, bounds-checked key bytes,
  `interp` 176..180, `encoding` 0..2, 4-component requirement for SLERP/SQUAD);
* `nj_m3g_link_misc()` links the new objects: the sequence's keys are expanded
  into the existing Java `_nj_times` / `_nj_keys` fields (the same arrays
  `KeyframeSequence.getSample` already samples), the track's sequence,
  controller and property go into `_nj_kfs / _nj_ctl / _nj_prop`, the
  controller's fields into `_nj_ai0/_nj_ai1/_nj_speed/_nj_pos/_nj_weight`, and
  every object's file animation-track refs are appended to its `_nj_tracks`
  vector;
* `AnimationController` learned the JSR timing semantics it was missing: a
  `_nj_refw` reference-world-time field, `setPosition(seqTime, worldTime)`,
  `setSpeed(speed, worldTime)` (which re-bases the reference position exactly
  like `AnimationController.setSpeed`), `getPosition(worldTime)` and
  `getRefWorldTime()`; the old versions ignored the time argument;
* `NJFN(m3g_animate)` now walks the object and its `Group` children, samples
  each track through the Java `KeyframeSequence.getSample(F[F)I` (so the
  interpolation math lives in one place), gates sampling on the controller's
  active interval and weight, blends tracks that target the same consecutive
  property with a weighted average, applies `TRANSLATION` / `ORIENTATION` /
  `SCALE` to `_nj_t / _nj_r / _nj_s`, recomposes once, and returns the
  smallest "time to next event" hint as the JSR requires. Negative time is
  clamped to 0, and a failed sample clears the pending Java exception instead
  of leaving it for the game.

`tests/test_m3g.c` gained two fixtures:

* `test_animation()` builds a `Group` and a `KeyframeSequence` in Java, drives
  it with `animate()` and checks the applied translation, the scale matrix,
  the quaternion-to-matrix orientation, the returned validity hint, and the
  controller time arithmetic (`setPosition`/`setSpeed`/`getPosition`/
  `getRefWorldTime`);
* `test_animation_file()` crafts a 232-byte M3G file with one of each object
  (the exact NFS layout), loads it through `Loader.load`, and checks the
  decoded object graph and that `animate()` drives the loaded node.

## Verification

```
cmake --build build-lead-render --target test_m3g nj2me-cli -j24
./build-lead-render/test_m3g                      # 693 checks, 0 failures, PASS
cd build-lead-render && ctest -R 'm3g|quit'       # 9/9 passed
cd build-lead-render && ctest                     # 118/118 passed (311 s)
tools/check_m3g_titles.sh "$PWD/build-lead-render/nj2me"   # 0 titles not clean,
                                                  # deep pass reaches rasteriser
```

A/B measurement — the same replay with an animation-off switch (temporary
`NJ_M3G_ANIM_OFF`, also removed) vs the fix. The switch only short-circuits
`m3g_animate`, so game state follows the same script:

```
                         wheel mesh min y       body mesh min y
animation off (bind)     -0.086 .. -0.081       -0.049 .. -0.044
animation on (fixed)     +0.019 .. +0.024       +0.056 .. +0.061
```

Across 104 wheel samples and 26 body samples of the tutorial race, the wheels
move from 0.085 units below the road plane to just above it; road tiles stay
at y = 0. Visually the car body/wheels ride at the authored suspension height
instead of being buried in the tarmac (`nfs-anim-D` frames 1000-1060 even show
the car-selection wheel close-up with the tyre resting on the platform).

Repro (headless, 240x320, 30 FPS), fresh RMS directory:

```
RUN=.local-tools/debug-runs/nfs-anim-D; mkdir -p "$RUN/rms" "$RUN/frames"
./build-lead-render/nj2me --port headless --size 240x320 --fps 30 \
  --rms-dir "$RUN/rms" --run-frames 2600 \
  --keys '120:soft1:down,124:soft1:up,620:down:down,626:down:up,800:fire:down,806:fire:up,900:fire:down,906:fire:up,1100:up:down,1300:fire,1400:fire,1500:up,1600:down,1700:fire,1800:fire,1900:fire,2000:fire,2100:fire,2200:fire,2300:fire,2400:fire,2500:fire,2600:fire' \
  --dump-frames "$RUN/frames/" --dump-every 20 --stats \
  'tests/games/极品飞车14之热力追踪中文版_4160.jar'
```

Stats: `frames=2600 paints=1213 gfx=772099 tris=263693 m3g=24866 fps=47.3`
(the animation itself costs nothing measurable here; the replay is dominated
by the game's own rasterisation).

Evidence:

* `.local-tools/debug-runs/nfs-anim-D/` — fixed build, frames + runtime log,
  and `before-after.png`;
* `.local-tools/debug-runs/nfs-anim-C/` — same replay with the animation-off
  switch, for the before numbers;
* `.local-tools/debug-runs/nfs-anim-B/` — instrumented run with the YMESH
  measurement log.

## Limitations / follow-ups

* Only `TRANSLATION`, `ORIENTATION` and `SCALE` are applied. `ALPHA`,
  `VISIBILITY`, colour, morph-weight and camera/fog/light tracks decode and
  link fine (they are the same objects) but `animate()` ignores them, so a
  title that animates colour or visibility will not see those changes yet.
* Recursion follows `Group._nj_kids` children, which is what the JSR's
  "animate the object and the objects it references" means for a scene tree;
  a node reached only through a non-child reference (e.g. a track whose
  target is not a descendant) is not animated.
* The returned hint is the sequence's own hint; the JSR's
  `AnimationTrack`-level scaling (`validity / |speed|`) is applied when the
  controller speed is non-zero. `STEP` and `SQUAD` interpolation are handled
  by the existing sampler.
* After the fix the wheel bottoms rest 0.019-0.024 units above the road plane
  rather than exactly on it; the offset is identical for the body, so the
  animation is applying the authored pose (rigid lift), not a per-wheel
  correction. This is plausibly the authored ride height/suspension
  extension, but it has not been cross-checked against hardware footage.
