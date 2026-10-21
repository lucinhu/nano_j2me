# M3G lighting cost: read the lights once per draw, not once per vertex

A profile of Doom 3 (900 frames, headless 240x320) showed 31.8 million
`nj_field_lookup()` calls, 29.6 million of them attributed to `jr_fetch()`:
one lighting evaluation per vertex, and inside it six field lookups and a
handful of field reads for every light.  The next largest source was 1.8 M from
`bais_read()`.  On that run the emulator spent about 3.8 s of CPU inside the
renderer, and roughly a second of it was field resolution that produced the
same answer for every vertex.

## What changed

`jr_lit()` used to read each `Light` object per vertex:

```c
nj_field *fm = nj_field_lookup(r->vm, NJ_C_LIGHT, "_nj_mode", "I", 0);
nj_field *fi = nj_field_lookup(r->vm, NJ_C_LIGHT, "_nj_intensity", "F", 0);
... 6 lookups, then nj_iget/nj_rget for mode, colour, intensity,
    attenuation, spot angle and spot exponent ...
```

`src/midp/nj_lib_m3g.c` now resolves those once per draw, in `jr_setup()`,
into a small table on the stack state (`m3g_jr`):

```c
nj_u4 lmode[NJ_M3G_LIGHTS];
nj_u4 lcol[NJ_M3G_LIGHTS];
float lint[NJ_M3G_LIGHTS];
float latt[NJ_M3G_LIGHTS][3];
float lspot_angle[NJ_M3G_LIGHTS];
float lspot_exp[NJ_M3G_LIGHTS];
```

`jr_lit()` then runs over the table with no Java field access at all.  The
defaults the JSR defines are filled in when a field or object is absent (mode 0
means "no light at this index"; attenuation defaults to (1,0,0); spot angle to
45 degrees).  No Java code can run between the fill and the last triangle, so
the table cannot go stale within a draw; a title that changes a light between
two renders is read again on the next draw, which the existing per-World light
regression test already covers.

The JSR caps a `Graphics3D` at eight lights.  `NJ_M3G_LIGHTS` is 16, and any
slot beyond it is left unlit rather than growing a variable-size table.

## Measured effect

Same source, Release, `-DNJ_BUILD_TESTS=OFF`, headless 240x320, wall-clock
paced at 30 fps, so the honest metric is CPU:

| title | frames | user CPU before | user CPU after | change |
| --- | --- | --- | --- | --- |
| Doom 3 (`毁灭战士3_10598.jar`) | 900 | 4.769 s | 3.249 s | -31.9% |
| NFS The Run (`极品飞车16：亡命狂飙_1519.jar`) | 3,300 | 11.560 s | 11.113 s | -3.9% |

Doom 3 is the lighting-heavy case (per-vertex spot light over 288,735
triangles); The Run's race is mostly texture work, which is why its saving is
smaller.  Doom's reported fps rose from 27.9 to 28.6 as its per-frame cost moved
below the pacing budget.

## Correctness

* Doom 3 frames 150..900 (every 150, full RGB) are **byte identical** before
  and after the change.
* NFS The Run frames 150..1800 are byte identical.  From 1950 on they differ,
  and that is the title, not the renderer: its race simulation is driven by
  wall-clock time, so changing how much CPU the emulator uses changes the game
  state (the triangle and render counters differ too: 603,595 vs 597,856
  triangles).  Before the race, where the same menu animation is replayed, the
  pictures match.
* `tests/test_m3g.c` 321 checks / 0 failures (including the two-World light
  regression, which renders the same scene twice with different lights),
  `test_m3g_file` 97 / 0, full `ctest` 87/87, trimmed `-DNJ_WERROR=ON` build
  clean.

Evidence: `.local-tools/debug-runs/m3g-light-table-r11/` (`timings.txt`,
`doom-base-frames/` vs `doom-new-frames/`, the two run logs).

## Not changed

The field lookups are still there for the other per-draw state (materials,
vertex buffers, textures); they were already once per draw and are below the
noise.  The remaining 1.8 M lookups in `bais_read()` are the resource loader
reading bytes out of a `ByteArrayInputStream`, a different path.
