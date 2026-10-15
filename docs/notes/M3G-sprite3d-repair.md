# Sprite3D rendering repair

The software M3G renderer collected meshes but skipped Sprite3D nodes. In Stranded 2 this left the player invisible after the terrain and room texture fixes.

The renderer now collects standard and vendor Sprite3D nodes and draws camera-facing quads. It supports scaled and pixel-sized sprites, crop intersection and negative crop flips, RGB/RGBA images, inherited node alpha, compositing, fog and viewport clipping. Sprite vertices live on the C stack. The accompanying render(Node, Transform) fix replaces the root's local transform with the supplied transform while retaining descendant transforms.

Regression checks cover crop initialization/reset, flipped and empty crops, camera-facing orientation, explicit parent alpha, alpha threshold, unscaled dimensions and root transform replacement. The transform API fixture now constructs Sprite3D with a valid Image2D; a null image correctly raises NullPointerException.

Validation on 2026-10-15:

- All 79 CTests passed in 182.87 seconds. The final additional root-transform and parent-alpha assertions also passed the focused test_m3g check.
- Stranded 2: isolated 240x320, 16 MB heap, 30 FPS, 2,250-frame headless and SDL2 runs. SDL2 measured 30.1 FPS with no uncaught exception. The player is visible at the lockers; room floor and objects render. A subsequent 2,700-frame SDL2 run measured 30.1 FPS with no uncaught exception. Holding Left and Right for three seconds moved the player and panned the camera; Down was blocked by the crates in front of the player. Earlier one-frame direction taps established facing changes but were insufficient to establish movement. The reference emulator also moves under sustained input, although these runs do not establish identical movement speed or frame timing. Saving and restarting to continue remain unverified.
- Rally 3D: 4,300-frame headless and SDL2 replays at the same settings measured 30.2 and 30.1 FPS respectively. Captures show the pause menu, a subsequent race in cockpit view, and further third-person driving. Existing game Timer callbacks still raise NullPointerException, cancel the timer, and later trigger IllegalStateException on rescheduling; the replay recovers, so this is not reported as resolved.

Remaining renderer limits include ARGB8888 sprite targets, reciprocal clip-w depth (orthographic depth ties), the existing 0.05 near-eye clipping threshold, and the existing collection limits of 64 nodes / depth 32. Scope culling remains unimplemented.
