# M3G mesh alpha and Minecraft clouds

Source baseline: `c6561fd8779b7207fb57364a1f726974b7f896be`.

## Reproduction and intended behavior

The original `MinecraftJ2ME_PE_0.6.1_V2.2.jar` constructs a cloud mesh with no color array and sets its default color to `0xCCFFFFFF` in `Sky.render`. Its texture is RGBA, with BASE_LEVEL / NEAREST filtering, REPEAT wrapping, MODULATE texturing and ALPHA compositing. The mesh color alpha was discarded before rasterization, and MODULATE retained only texture alpha. Opaque cloud texels consequently rendered as opaque white.

A public-API probe reproduced this independently of game timing. Default-color and RGBA-vertex draws with alpha 0, 128 and 204 all rendered opaque red over blue (9 incorrect results); the three alpha-255 controls were correct. A separate texture update probe passed six bounded BASE_LEVEL / mip-level controls, so this patch does not attribute the cloud defect to stale texture caches.

The [Texture2D specification](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/Texture2D.html) defines MODULATE output alpha as incoming fragment alpha times texture alpha. RGB and LUMINANCE textures retain incoming alpha. RGBA REPLACE takes texture alpha; RGB REPLACE and DECAL retain incoming alpha. [Material](https://nikita36078.github.io/J2ME_Docs/docs/jsr184/javax/microedition/m3g/Material.html) takes alpha from diffuse color, or from vertex/default color when vertex color tracking is enabled.

## Scope

Preserve mesh alpha through color selection, camera-plane clipping, flat/Gouraud alpha interpolation, texture application and raster fast paths. RGB vertex arrays imply alpha 255. Preserve existing RGB arithmetic for opaque draws. Do not change builtin method/field/class table layout.

Independent limitations remain outside this change: Node alpha-factor inheritance, depth offsets and the equal-depth comparison, destination-alpha semantics, existing RGB FLAT shading behavior, and full RGB equations for ADD / BLEND / DECAL. This change concerns their alpha equations and does not claim those modes are otherwise complete. Correct cloud transparency alone does not establish that every water, leaf or cloud flicker is resolved.

## Validation

The independent public-API test exercises 4,032 assertions on an ARGB MIDP image and actual RGB565, RGB888 and ARGB8888 screen buffers. It covers default/RGB/RGBA colors; alpha 0, 1, 128, 204, 254 and 255; flat/Gouraud alpha; source-format-sensitive texture blending; diffuse and tracked alpha through lighting; clipping and alpha gradients; exact alpha thresholds and depth-write controls with strictly different depths. The baseline fails 1,200 assertions and the candidate passes all 4,032. Additional controls cover disabled alpha writes and alpha-zero/threshold-zero fragments that still write depth. Its RGB source-over oracle does not call the runtime's raster helpers.

The original-JAR cloud control invokes `Sky` unchanged to obtain its mesh, image and render attributes. It fixes time, brightness, projection and camera orientation, then renders the cloud mesh over `0x3060C0`. The baseline's original `0xCCFFFFFF` result is byte-identical to a forced-alpha-255 control: 15,025 of 76,800 pixels are clouds. An ordinary world replay also looks upward with a pointer drag `(200,180)` to `(200,40)`, using a copy of the existing test world's filesystem and RMS; it reaches visible clouds without editing the JAR or user worlds.

The final fixture passes all 4,032 assertions in Release with fast paths enabled and disabled, and with ASan+UBSan. Modified production/test objects and the M3G-disabled test compile with `-Wall -Wextra -Werror`. A separate observation-only run captures 3,984 RGB samples from this matrix: the fast and general variants are byte-identical (15,936 bytes, SHA256 `744c15634c8476eccbd88016ec335804eeb6eae53ab84d32acb9269d682889b9`). This comparison concerns sampled pixels, not every possible scene.

The fixed original-JAR cloud control changes exactly the 15,025 cloud pixels. All 76,800 output pixels match an independent alpha-204 source-over calculation within one channel unit. The forced-alpha-255 before/after frames remain byte-identical. A fresh SDL2 process also loads a copied world and looks upward using the same pointer drag: 1,200 paced frames at 240×320 with a 16 MiB heap, an actual 960×1280 X11 window capture, readable terrain/HUD and translucent clouds. LOAD/prepare were 2,704/2,645 ms and the disposable world saved in 249 ms. No uncaught exception or malformed-chunk error occurred; the game's existing optional Android/DatagramSocket probes still throw caught `ClassNotFoundException`s. The paced 29.8 fps is not a throughput result.

Seven opaque raster controls retain identical whole-buffer checksums. Measurements use GCC 16.2.1, the project's C99 `-O3 -DNDEBUG` flags, CPU affinity to core 0, process CPU time, warm buffers and 1,200 identical triangle draws per case (240×320 bounds, unaligned padded buffers, no fog/depth test, depth writes on). Nine alternating measurement pairs after discarding a warm-up pair show increased median time of 0.11–4.59%: flat ARGB +1.16%, flat RGB565 +1.09%, tinted clamp +1.24%, tinted RGB565 repeat +0.11%, Gouraud nearest +4.11%, linear sampling +1.25%, RGB REPLACE +4.59%. This is a correctness change with a measured cost; it does not claim a rendering speedup.

Core API audit: all 982 reference members resolve. All 59 builtin field/method/class declaration blocks in this source remain byte-identical to the baseline; no save-state table layout change.

The initial complete run passed 147/148 tests; `check_m3gdemo` exposed 19 obsolete opaque-pixel expectations because its Java inputs used zero-alpha diffuse colors (`0x00RRGGBB`). Corrected nine opaque DIFFUSE literals and the explicitly opaque `flat(rgb)` helper to alpha 255; texture alpha arguments and pixel expectations are unchanged. After regenerating the ignored fixture JAR, both the baseline and fixed binaries pass its 148 Java checks. All seven affected M3G/save-state regressions pass, and the final complete serial CTest run passes **148/148** in **300.08 seconds**.

```sh
cmake --build build -j4
# Needed when reusing an older generated fixture (NJ_GENERATE_FIXTURES=OFF).
bash tools/make_m3gdemo_jar.sh
ctest --test-dir build -R '^test_m3g_alpha$' --output-on-failure
ctest --test-dir build --output-on-failure -j1
```

Detailed local commands, before/after source snapshots and raw evidence are under `.local-tools/debug-runs/mc-20261109-alpha-{impl,tests,main}/`. Experiments use copied test-world filesystem/RMS data. Original game JARs and user worlds were not modified or committed.
