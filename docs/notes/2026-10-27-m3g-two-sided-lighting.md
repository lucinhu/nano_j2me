# Two-sided lighting: the file's flag now reaches the rasteriser

Date: 2026-10-27
Scope: `src/midp/nj_lib_m3g.c` (PolygonMode decode + link, software rasteriser),
`tests/test_m3g.c`, `tests/test_m3g_file.c`, `tests/probe_m3g.c`
Evidence: `docs/m3g-twosided-results.json`,
`.local-tools/debug-runs/twosided-unit-ab/`; unit A/B table below.

## Why this one, from the corpus

JSR 184's `PolygonMode.TWO_SIDED_LIGHTING` says the back side of a polygon is
lit with its vertices' normals reversed, so a one-sided paper-thin surface
turned away from a light is shaded like the front side would be.  The runtime
already answered `setTwoSidedLightingEnable()` / `isTwoSidedLightingEnabled()`
and already decoded the three enumerations at the start of a file PolygonMode,
but nothing consumed the flag in `jr_setup()`/`sw_tri()`: every visible back
face was lit with the front normal, which for a surface whose light is on the
other side means ambient-only or black.

This was not a guess about what titles might need.  A scan of the 975 `.m3g`
files the corpus ships (`tools/check_m3g_polygon_modes.py`, results in
`docs/m3g-twosided-results.json`) found:

* all **1513** PolygonMode objects are **eighteen** bytes: three enumerations
  at offset 12..14 and the three booleans at 15..17;
* bytes 15..17 are only ever 0 or 1 - **(0,0,1) on 1409**, **(0,0,0) on 61**,
  **(1,0,1) on 43**.  So two-sided lighting is on in **43** PolygonModes across
  exactly **six** titles:

  | two-sided PolygonModes | title |
  | --- | --- |
  | 15 | `生化危机3D(CAPCOM经典)_3671` |
  | 13 | `3D科林麦克雷拉力赛之尘埃_6600` |
  | 9 | `杀戮机器-纳粹僵尸_3610` |
  | 3 | `CS反恐精英(蓝牙对战)_6858` |
  | 2 | `3D拉力赛-进化汉化版_6366` |
  | 1 | `蓝牙真3D格斗_3424` |

The independent walk agrees with the in-tree probe: `tools/check_m3g_reals.sh`
probes 975 files with 0 rejected, 0 refused objects and **PolygonMode
1513/1513 in range**, so the eighteen-byte layout is measured, not assumed.
(That also settles the probe's old "403 files disagree" note: the disagreement
was object framing, fixed by the later framing commits, not a different
PolygonMode layout.  The comments were corrected with this change.)

## What changed

**Loader.**  The decode case already read `data[12..14]`; it now also reads the
three booleans into `f[0..2]` and sets `d` to say they were present.  The link
step writes all three to the Java object: `_nj_twosided`, `_nj_localcam`,
`_nj_persp`.  A payload that stops after the enumerations keeps the
specification defaults - a file object never runs the class constructor, so
that means writing perspective correction `true` explicitly rather than
leaving the allocation's zero.  No real file omits the flags; the short-payload
path only exists for the hand-built fixtures.

**Renderer.**  Two-sided lighting needs the *facing* of a triangle, and that is
only known after projection.  So the vertex path computes both colours up
front:

* `jr_fetch()` lights the vertex as before, and when the appearance's
  PolygonMode asked for two-sided lighting, lights it a second time with the
  negated eye-space normal (and fogs both), keeping `rgb` and `rgb2`;
* `jr_blend()` interpolates both colour sets when clipping invents a vertex;
* `jr_emit()` hands both sets to `sw_tri()`;
* `sw_tri()` already computes `back` from the signed screen area against
  `winding` for culling.  It now selects `rgb2` for a back-facing triangle
  **before** the Gouraud/flat and fast-path flags are derived, so every
  per-pixel loop, the flat-opaque and flat-textured fast paths, alpha test,
  blending and depth behaviour stay byte-for-byte the old code.  A
  one-sided appearance never sets `twosided`, never pays the second lighting
  pass, and never allocates anything.

Not attempted, and now stored for API correctness only: local camera lighting
(no implementation) and perspective correction (the rasteriser is always
perspective correct).  Both getters answer the file's values.

## Verification

**The tests fail on the old source and pass on the new one.**  A worktree at
HEAD `333a347` was given the new test files and built separately:

| build | `test_m3g` | `test_m3g_file` |
| --- | --- | --- |
| HEAD source + new tests | 746 checks, **5 failures** | 101 checks, **1 failure** |
| this source + new tests | 746 checks, 0 failures | 101 checks, 0 failures |

The five are exactly the vertices of the change: the back face stays black
with the flag off, does not turn white with it on, and the old code's
`isTwoSided...`/`isLocalCamera...`/`isPerspective...` on a file object are
wrong.  Logs: `.local-tools/debug-runs/twosided-unit-ab/head-*.log` and
`fixed-*.log`.

The renderer test is a scene where the check cannot pass by accident: one
triangle authored in the back-facing winding, normals pointing at the camera,
one directional light shining from the far side (its transform rotated 180
degrees about y), and a material with no ambient or emissive term.  Off, the
triangle draws the front-side colour, which is black; on, the negated normal
faces the light and the same pixels turn white.  The test asserts both the
lit-pixel count with the flag on and that switching the flag is what changed
it.

The file test builds an `.m3g` out of bytes (a stored container with a single
unreferenced root) whose PolygonMode is the real files' eighteen-byte shape
with flags `(1,1,0)`; `Loader.load()` has to return it as a root and
`isTwoSidedLightingEnabled()`/`isLocalCameraLightingEnabled()`/
`isPerspectiveCorrectionEnabled()` must answer 1/1/0.  A second build with the
fifteen-byte payload must keep the defaults: 0/0/1.  That pins decode, link and
the absence rule together.

**Corpus and suite.**

* `tools/check_m3g_reals.sh`: 975 `.m3g` files probed, **0 rejected**, 0
  objects refused, PolygonMode **1513/1513** in range.
* `python3 tools/check_m3g_polygon_modes.py --json docs/m3g-twosided-results.json tests/games`:
  the table above; 0 payloads outside the shape.
* Full `ctest` on this tree: **118/118 passed**.

**Real-game frames.**  The six titles are next: the same key script and frame
dump on a HEAD build and on this build, per title.  That evidence lands in a
follow-up commit and is added here when it does.
