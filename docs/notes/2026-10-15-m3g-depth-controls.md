# M3G compositing depth controls

`CompositingMode.setDepthTestEnable` and `setDepthWriteEnable` were stored by
native methods but ignored by the software rasterizer. Every fragment always
tested and updated the depth buffer. The renderer now reads both controls from
the appearance, preserves their enabled defaults, and applies each independently.

The pixel regression in `tests/test_m3g.c` draws a near red triangle followed by
a far green triangle over the same pixel. It exercises all four combinations of
the first draw's depth write and the second draw's depth test. Red must remain
only when both controls are enabled. The fixture uses an explicit material,
camera, valid indexed mesh, and background so unrelated default-color or null
background behavior cannot mask the result.

Validation on 2026-10-15:

- All 79 CTest tests passed (45.93 seconds).
- `test_m3g`: 188 checks passed.
- Stranded 2 completed a 1,500-frame replay at 30.2 fps, drawing 244,253 triangles
  across 27,269 M3G calls. Task dialogue remained responsive.
- Stranded's laboratory still has missing black floor/wall regions and incorrect
  texture placement. This depth correction does not resolve that issue.

A bounded diagnostic at frame 1101 observed 27 draw calls, all with textured
REPLACE mode, depth testing/writing disabled, no lighting, and a perspective
projection (fourth row `0, 0, -1, 0`). The floor buffers had 736 vertices. These
observations rule out orthographic reciprocal-depth ambiguity and material
lighting for this captured scene, but do not establish the remaining cause.
The diagnostic has been removed from the source.

Replay logs, pixel captures and the isolated patch are under
`.local-tools/debug-runs/stranded2-depth-001/`. Original game archives and saves
were preserved; the replay used an isolated RMS directory.
