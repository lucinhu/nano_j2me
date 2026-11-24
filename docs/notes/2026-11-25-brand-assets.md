# Brand assets: one icon design for Android, desktop and web (2026-11-25)

The launcher icon used to exist in three unrelated versions: the Android mipmap
set (dark navy plate, white D-pad, two blue buttons), the README logo
(`data/nj2me.png`) and the web/PWA icons (an older green screen motif). This
round makes the Android design the single source of truth.

## Generator

`tools/gen_android_icon.py` is the existing Android icon designer:
`render_square(size)`, `render_round(size)` and `render_foreground(size)` draw
the plate and the motif from the constants at the top of the file.

`tools/gen_brand_assets.py` imports that module and writes every other brand
surface deterministically:

| Output | Content |
|---|---|
| `data/nj2me.png` | `render_square(256)`, used by both READMEs (`README.md`, `README.zh-CN.md`) |
| `docs/images/nano_j2me-icon.png` | `render_square(512)`, documentation copy |
| `web/assets/icon-512.png` | `render_square(512)` |
| `web/assets/icon-192.png` | `render_square(192)` |
| `web/assets/icon-maskable-512.png` | `render_foreground(512)` composited on the plate gradient (maskable safe zone) |
| `web/assets/icon.svg` | hand-written SVG twin of the same geometry (`viewBox="0 0 64 64"`) |
| `src/port/posix/nj_icon.h` | 64x64 RGBA8888 array used by `SDL_SetWindowIcon` |

Regenerate with:

```sh
python3 tools/gen_brand_assets.py
```

The script is deterministic: running it again leaves `git status` clean
(SHA-256 checked during the round).

## Consumers

- **Android** — unchanged: the APK uses the mipmap set produced by
  `tools/gen_android_icon.py generate`; the new outputs are derived from the
  same functions so the three platforms cannot drift again.
- **README / docs** — `data/nj2me.png` is replaced in place; no markup changed.
  Both READMEs pick it up.
- **SDL2 desktop** — `src/port/posix/nj_port_sdl2.c` includes the generated
  `nj_icon.h` and calls `SDL_SetWindowIcon` right after creating the real
  window, so the taskbar/dock icon matches the APK. The bytes are embedded in
  the binary; no side-car file is needed.
- **Web / PWA** — `web/index.html` (favicon + header logo), `web/manifest.json`
  (192/512/maskable) and `web/sw.js`'s precache list keep their existing paths,
  so the shell does not change shape; only the images are new.

## Verification in this round

- `cmake --build /tmp/r5-gles2 -j10` (SDL2 + GLES2 + CLI) rc=0 and the default
  CLI build `/var/tmp/nj2me-goal13-build` rc=0 with the new `nj_icon.h`.
- Generated PNGs inspected: 512 px plate/D-pad/buttons match the Android
  launcher rendering (same generator functions).
- `xprop _NET_WM_ICON` could not be read back on the Xvfb display used for
  GLES2 tests; the icon path is exercised by the build and will be visible on a
  normal desktop. This is noted rather than claimed as a pixel check.
