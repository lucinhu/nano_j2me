# AnimationTrack.COLOR: the one colour track in the corpus now animates

Date: 2026-10-27
Scope: `src/midp/nj_lib_m3g.c` (`m3g_animate_object`, `m3g_anim_apply`),
`tests/test_m3g.c`
Evidence: `.local-tools/debug-runs/anim-colour-20261027/`; ctest 118/118.

## What the corpus has

The feature audit (`docs/notes/2026-10-26-m3g-corpus-features.md`) counted the
2,092 AnimationTrack objects in the corpus by target property:

| property | tracks | implemented before |
| --- | ---: | --- |
| TRANSLATION 275 | 539 | yes |
| ORIENTATION 268 | 1,089 | yes |
| SCALE 270 | 429 | yes |
| ALPHA 256 | 17 | yes (孤岛余生2) |
| VISIBILITY 276 | 17 | yes (孤岛余生2) |
| **COLOR 258** | **1** | **no** |

The single COLOR track is in 蓝牙真3D格斗's `sadan.m3g` and it is attached to a
**Background** (file object 58, track object 57).  Its KeyframeSequence is
three-component, three-key, linear, looping, and its keys say what the game
does: colour `(0.624, 0.624, 0.624)` at t=0 -> `(1,1,1)` at t=55 ms ->
`(1,1,1)` at t=14933 ms.  Scaled to eight bits that is the file's grey
`0x9E9E9E` fading to white in the first two frames of the sequence.  The
Background carries no image (backgroundImage ref 0) and colour clear is on, so
the animation's only possible effect is the colour `clear()` paints.

## What changed

`m3g_animate_object()` whitelisted TRANSLATION, ORIENTATION, SCALE, ALPHA and
VISIBILITY; COLOR (258) is now blended like the others.  `m3g_anim_apply()`
writes it to the only classes JSR 184 lets a COLOR track target - Background,
Fog and Light - by scaling the three 0..1 components to eight bits and keeping
the existing alpha byte (JSR 184's COLOR is RGB only; a Background's alpha is
a separate ALPHA track).  The fields written are the same ones the
`setColor()` natives write, so `getColor()` and the renderer see it:
Background's `_nj_color` is what `clear()` fills with and Light's is what
`jr_lit()` reads.

One defect surfaced while adding it and is fixed here: `composed` used to be
set for every applied property, so every animation called
`xable_recompose()`.  That is fine for a Node, but ALPHA/COLOR's targets
include Background/Fog/Light, which are not Transformables, and recomposing
one dereferences a transform that does not exist - the first COLOR test
segfaulted in `xable_recompose()`.  Recompose is now limited to the three
component-transform properties, which is also where it is needed.

## Verification

**The new tests fail on the old source and pass on the new one.**  A worktree
at HEAD `656f7b6` was given the new `tests/test_m3g.c` and built separately:
it reports **6 failures** in the three new blocks (background colour,
Light/Fog colours, animated clear).  With this change `test_m3g` is **757
checks, 0 failures**; the full suite is **118/118**.  Logs:
`.local-tools/debug-runs/anim-colour-20261027/head-source-test_m3g.log` and
`fixed-test_m3g.log`.

The tests pin, in order:

* a Background with an ALPHA-bearing colour (`0xFF204080`) and a COLOR track
  whose keys are `(0,0,0)` at 0 ms and `(1, 0.5, 0.25)` at 1000 ms: at t=0 the
  colour is black, at t=500 it is `(128,64,32)` (the linear blend rounded to
  eight bits), at t=1000 it is `(255,128,64)`, and the alpha byte stays `0xFF`;
* the same track applied to a Light and to a Fog writes each object's
  `_nj_color` (the classes a COLOR track may target);
* renderer-level: an `.m3g`-style Graphics3D canvas whose Background is
  animated to red clears its target to `0xFF0000` at two corners, and
  animating the same Background back to t=0 clears it black again.  So the
  value the track writes is the value `clear()` paints - not just a field
  that reads back.

**In-game frame evidence was attempted and is not claimed.**  A before/after
A/B of 蓝牙真3D格斗 (1,500 frames, identical key script, the reproduction from
`docs/notes/2026-10-26-m3g-render-defects.md`) first showed two traps:

1. the title is **nondeterministic run to run**: the same binary run twice
   differs by 1,101 pixels at frame 00500 (a random hint line, y=250..260) and
   by 28,626 pixels over 150 dumped frames, so a whole-frame diff cannot be
   attributed to a change;
2. the reached 3D arena (frame 00900, both fighters and the sky visible) is
   pixel-identical between the two builds - its sky comes from another file's
   geometry/image, not from `sadan.m3g`'s Background, so the animated clear
   colour is not on screen in the scene that can be reached with a key script.

The renderer-level test above is therefore the frame proof for this property;
the corpus's one track is recorded here with its exact key values.  The
nondeterminism is worth remembering for any future before/after evidence on
these titles: establish a same-binary baseline first.

## Limits

* Only the properties listed in the table are animated.  CROP (259), DENSITY
  (260), FAR_DISTANCE (263), FIELD_OF_VIEW (264), INTENSITY (265),
  MORPH_WEIGHTS (266), NEAR_DISTANCE (267), PICKABILITY (269), SHININESS
  (271), SPECULAR_COLOR (272), SPOT_ANGLE (273), SPOT_EXPONENT (274) are still
  left alone, as are the material colours; the corpus counts for those were
  zero or unreachable, except MORPH_WEIGHTS (MorphingMesh is zero objects).
* `addAnimationTrack()` does not enforce the JSR's target-property
  compatibility (a COLOR track added to a Group is silently ignored by
  `animate()` rather than thrown at add time).
