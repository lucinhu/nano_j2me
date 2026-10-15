# Sprite3D rendering repair

The software M3G renderer collected meshes but skipped Sprite3D nodes. In Stranded 2 this left the player invisible after the terrain and room texture fixes.

The renderer now collects standard and vendor Sprite3D nodes and draws camera-facing quads. It supports scaled and pixel-sized sprites, crop intersection and negative crop flips, RGB/RGBA images, inherited node alpha, compositing, fog and viewport clipping. Sprite vertices live on the C stack. The accompanying render(Node, Transform) fix replaces the root's local transform with the supplied transform while retaining descendant transforms.

Regression checks cover crop initialization/reset, flipped and empty crops, camera-facing orientation, explicit parent alpha, alpha threshold, unscaled dimensions and root transform replacement. The transform API fixture now constructs Sprite3D with a valid Image2D; a null image correctly raises NullPointerException.

Validation on 2026-10-15:

- All 79 CTests passed in 182.87 seconds. The final additional root-transform and parent-alpha assertions also passed the focused test_m3g check.
- Stranded 2: isolated 240x320, 16 MB heap, 30 FPS, 2,250-frame headless and SDL2 runs. SDL2 measured 30.1 FPS with no uncaught exception. The player is visible at the lockers; room floor and objects render. A subsequent 2,700-frame SDL2 run measured 30.1 FPS with no uncaught exception. Holding Left and Right for three seconds moved the player and panned the camera; Down was blocked by the crates in front of the player. Earlier one-frame direction taps established facing changes but were insufficient to establish movement. The reference emulator also moves under sustained input, although these runs do not establish identical movement speed or frame timing. Saving and restarting to continue remain unverified.
- Rally 3D: 4,300-frame headless and SDL2 replays at the same settings measured 30.2 and 30.1 FPS respectively. Captures show the pause menu, a subsequent race in cockpit view, and further third-person driving. Existing game Timer callbacks still raise NullPointerException, cancel the timer, and later trigger IllegalStateException on rescheduling; the replay recovers, so this is not reported as resolved.

Additional menu and RMS validation:

- A 2,000-frame headless replay opened the Stranded 2 pause menu with `soft2`. A subsequent 2,350-frame SDL2 replay opened the same menu, selected Main Menu, and explicitly selected Yes in the confirmation dialog. SDL2 measured 30.1 FPS, returned to the title menu, and reported no uncaught exception. FreeJ2ME-Plus at `baca76a4dda6f30abac67fb4a3db009eed05f91c` followed the same menu flow. The earlier physical-key experiment that failed to open the menu therefore does not establish a MIDP softkey-routing defect.
- Returning to the main menu left the isolated RMS files unchanged: store 0 was 528 bytes and stores 1 and 2 were 18 bytes each. The game's save-slot writer serializes a two-byte zero length when no saved payload exists; merely having these files is insufficient evidence of saved progress. A 900-frame SDL2 restart reused the exact RMS directory, reached the startup/title prompt, measured 29.9 FPS, and reported no exception. Loading a checkpoint and continuing gameplay remain unverified.

External-container follow-up:

- External M3G references can name another M3G container as well as an image. The loader now recognizes the container signature, loads its root through Loader.load(String), and preserves errors or rejects an empty result. An in-memory application-resource regression verifies the returned Image2D identity and dimensions. Removing only this branch caused two loader assertions to fail; restoring it passed. The final test_m3g_file run passed 83 checks, and all five M3G CTests passed in 29.14 seconds.
- Fresh isolated SDL2 runs used 240x320, 16 MB heap and 30 FPS. Stranded 2 completed 2,250 frames at 30.0 FPS with the player, floor and room objects visible and no uncaught exception. Rally completed 4,300 frames at 30.1 FPS, but its cockpit captures at 110 and 140 seconds were identical, with speed zero and an unchanged race timer. Its log again contains TimerTask NullPointerException and subsequent Timer cancelled scheduling errors. This replay does not establish sustained driving after restart; frame-loop FPS is insufficient proof of game progress. Earlier successful driving evidence remains separate.

Remaining renderer limits include ARGB8888 sprite targets, reciprocal clip-w depth (orthographic depth ties), the existing 0.05 near-eye clipping threshold, and the existing collection limits of 64 nodes / depth 32. Scope culling remains unimplemented.
