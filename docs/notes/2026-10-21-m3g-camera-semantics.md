# JSR 184 camera and node transforms: what The Run needed

`极品飞车16：亡命狂飙_1519.jar` (The Run, 240x320) booted, showed its menus and
its car-selection screen, and then raced on a flat olive field with a correct
HUD: no road, no tunnel, no car.  The race HUD, the minimap and the speedometer
were right, so the game was running; only the 3D world was missing.

The previous note, `2026-10-21-nfs-the-run-m3g.md`, had already retracted "the
textures are missing" and left the loss somewhere between the M3G render and
the screen.  It is neither: the world *was* being rendered, into the right
surface, with the wrong camera.  Three implementation defects, all in
`src/midp/nj_lib_m3g.c`, were feeding it a stale pose.

## What the game does

Bytecode (local names in `/tmp/nfsrun-cls`): each race frame
`an.a(Graphics,int,int,int)` does

1. `Graphics3D.bindTarget(screen Graphics)`, `setViewport(0, 0, 240, 320)`,
2. `Graphics3D.setCamera(H, I)` with the `Transform` `I` it set once when the
   menu was built,
3. `World.setActiveCamera(camNode)` where `camNode` is a `Camera` node in the
   World that the game re-places every frame,
4. `render(World)`.

It also calls `Node.getTransformTo()` between nodes to place the chase camera
and uses `setTransform(null)` to reset scratch nodes.

The title is not unusual: `render(World)` with a World-held camera is the
ordinary JSR 184 shape and the corpus uses it (`setCamera()` is the rarer
call).  What made this title special is that it sets *both* the World camera
and `Graphics3D.setCamera()`, and only ever updates the former.

## Defect 1: `render(World)` preferred `setCamera()`

`jr_setup()` picked the camera as

```c
cam = G3D._nj_camera;                 /* setCamera() */
if (!cam && world) cam = world._nj_cam;  /* active camera */
```

so with both present the World's own camera was ignored and the view came from
`setCamera()`'s stale `Transform` - the pose from the last car-select frame.
The race world was then projected from a camera that had not moved with the
car: 95 %+ of the incoming triangles fell outside the frustum, the few emitted
ones landed at x = 815..1560 in a 240 px viewport, and the screen kept just the
background clear.

JSR 184: "the active camera of the World is the camera used to render it";
`setCamera()` applies to the `render(Node, Transform)` entry point.  Fixed by
recording which entry point chose the camera and letting the World's active
camera win for `render(World)`; `render(Node, Transform)` keeps the old
behaviour, and the `setCamera()` matrix is no longer read for a World render.

## Defect 2: `Transform.setTransform(null)` was a no-op

The symmetric-difference node API needs `null` to mean "identity"; our
`m3g_xable_setTransform` simply returned.  A scratch node kept the previous
frame's translation, and The Run uses `null` exactly this way before placing
the nodes it renders.  Now `null` writes the identity matrix.

## Defect 3: `Node.getTransformTo()` handled only ancestors

The implementation composed the chain from `this` up to a common ancestor but
ignored the chain from `other`, which is only correct when `other` is an
ancestor of `this`.  The Run asks for transforms *between siblings*; the result
was the wrong matrix, and with it the chase-camera offsets were wrong too.

Replaced with the JSR 184 definition: with `Wa` and `Wb` the world transforms
of the two nodes, the answer is `inv(Wb) * Wa`, false when the nodes have
different roots.  World transforms are built by walking each node's parent
chain and composing parent-first.

## Tests

`tests/test_m3g.c` grew three regression blocks (321 checks, 0 failures):

- `getTransformTo` between siblings under one root: `a` is at (-1,0,0) in
  `b`'s space and `b` at (1,0,0) in `a`'s.  The old ancestor-only code cannot
  produce both.
- `Transform.setTransform(null)` resets a node to the identity.
- `render(World)` with a World camera *and* a stale `setCamera()`: the render
  must draw the World's own view.  The target image is blanked first, so the
  check cannot pass on pixels left by an earlier render - the first version of
  this test did, and stayed green with the fix reverted.  With the blanking in
  place, reverting defect 1 fails exactly this assertion
  (`a World's active camera draws it even after a stale setCamera()`), 1 of
  321.

## Real-game result

Production build (no diagnostics), same keys as the failing runs,
`.local-tools/debug-runs/nfs-prod-r10o/`:

```
frames=3300 paints=3300 gfx=843790 tris=588645 m3g=55991 time=110.883s
fps=29.8 bytecodes=253614945 heap=10435568/10437488
```

Frame 03150 (`frames/03150.png`, sha256 `01f5aa4b...`) is the race: yellow
Camaro in the tunnel, road markings, minimap, positions (P3 376M / P2 357M /
P1 317M), 65 MPH.  The frame has 1008 distinct colours where the old capture
had two (olive + HUD); the picture now changes every frame as the car moves.

## Cost and blast radius

No allocation and no measurable frame cost: the transforms were already being
composed, and the work is a handful of 4x4 multiplies per `getTransformTo`
(which no title in the measured corpus calls more than a few times per frame).

The other titles that call `getTransformTo` (`NFS 13/14`, `NFS 无间风云`,
`Doom 3`, `蓝牙真3D格斗`, `CS 反恐精英(蓝牙对战)`) were re-run as a regression
sweep: no uncaught exceptions, and the M3G render counters stay in their
historical range.  `tests/test_m3g_file`, `check_m3g_reals`, `check_m3g_api`
and `check_m3gdemo` pass; the full suite is 87/87.
