# Pixel-art scaling (hd_art_span) profile and SIMD attempt (2026-11-28)

## Where the time goes

`--asset-filter pixel-art --resident` on Doom3 at 8x, 240 frames, gprof
(`-pg`) build: `hd_art_span` holds 54.4% of the whole run (1.23 s of 2.26 s
samples, 115.5 M calls).  Next entries are far behind: `nj_region_draw`
4.9%, `nj_run` 4.4%, `sw_span` 4.4%, `sw_tri_band` 3.5%, `jr_hd_target`
3.5%, `m3g_background_image` 2.7%, `nj_surface_format_color` 2.2%.

`hd_art_span(dst, count, color)` writes one flat run of physical pixels:
opaque colours copy 4 bytes per pixel, translucent colours call
`hd_asset_over` per pixel.  Flat opaque cells only write their first row;
the caller (`hd_pixel_art_bitmap`) repeats it with `hd_repeat_row`, so the
interior of a bitmap is mostly this function.

## What was tried

1. SSE4.1 translucent composite (4 pixels per group, exact `/255` via
   `(y*0x8081)>>23`, scalar fallback for transparent destinations).  The
   per-group result matched `hd_asset_over` for every group of a 200-frame
   run, and the division identity holds for the whole attainable range
   (0..65152), yet the dumped frames changed: 6 pixels `[0,0,0]` became
   `[1,1,0]` at frame 120 with every other pixel identical.  The cause was
   not found, so the change was not kept.
2. SSE2 / SSE4.1 wide store for the opaque fill (16 bytes per store).
   Gates stayed exact but the 200-frame wall time went from 4.1-4.5 s to
   5.9-6.1 s (+45%).  Disabling the branch at zero cost (dead-code control)
   restored the original time, and a 64-bit pair store (2 pixels) was
   neutral.  The regression therefore follows the wide store itself, most
   likely its interaction with the `hd_repeat_row` copy that immediately
   reads back the row just written.
3. Dedicated ARGB8888 column decode (skipping the `nj_src_pixel` format
   switch) in the same way the FSR1 path was made faster: the profile puts
   the whole `nj_src_pixel` entry at 1.8% here, so the expected gain does
   not cover another rewrite of the inner loop.

## Conclusion

No change was merged: every attempt was either slower or changed output.
The per-span loop is already close to what the scalar code can do, and the
remaining cost is structural rather than a missing instruction: the CPU
scales the same assets every frame for 2D draws.  The next lever is the
one already identified for the composition path: keep the scaling on the
GPU (2D pipeline over the resident mirror) instead of making this loop
faster on the CPU.
