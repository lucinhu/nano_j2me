# Uncapped by default, because the display is the pacer

## What changed

The desktop SDL2 port and the Android port no longer cap the frame rate unless
asked to. `out->fps_cap` is 0 (uncapped) in both ports, the command-line runner
only writes a cap when `--fps` is given (it used to force its own 30 over
whatever the port wanted), and the Android player setting changed with it:
不限制（默认）/ 60 / 30, with `-1` still meaning "the port's own default" and
still applied to a running VM through `nj_vm_set_frame_cap()`.

libretro keeps 60 and the ESP32 keeps 30. In libretro the frontend owns the
present cadence - a core is called once per frame and does not get to decide
how often that is - and on the ESP32 the saving is real and the panel is the
bottleneck.

## Why uncapped is the right default

A port's frame cap sleeps between frames. That is useful when a title renders
far faster than the display can show, but it is the wrong thing to do when the
presentation path already blocks: the SDL2 renderer is created with
`SDL_RENDERER_PRESENTVSYNC`, and an Android `SurfaceView` hands a frame to the
display only when SurfaceFlinger takes one. In both cases the next frame cannot
start before the previous one has been shown, so the cap only adds a second
wait on top of the one already there - and it adds it to every frame, capping
what the machine can do rather than smoothing it.

The measurement that made this concrete: a saved Minecraft PE world, 600
frames, same build, on a 240 Hz laptop.

| Run | Time | Rate |
|---|---|---|
| 60 cap (0.5.1 default) | 10.45 s | 57.4 fps |
| uncapped `--fps 0` | 4.90 s | 122.5 fps |
| headless, uncapped | 3.82 s (1200 frames) | 314.4 fps |

The desktop can present about twice per refresh here (the swap returns without
blocking on every other frame), so the uncapped run is not "as fast as
possible" - it is what the machine's own present path allows. Both numbers are
real frames; the title's own threads keep their wall-clock time either way,
because the frame pacer waits in the scheduler rather than sleeping the whole
VM (see `src/midp/nj_lib_midlet.c` and the 2026-10-30 frame-pacer note).

On a laptop this costs battery, which is what the `--fps` option and the
Android player setting are for. On a phone the panel paces it to 60 or 120 Hz
anyway; a player who wants the battery back picks 60 or 30 in 显示设置.

## Wider hands

The old default also hid what the port could do: `check_sensor` and
`check_late_display` launch threads of their own and need wall-clock time, and
they had been relying on the runner's unconditional 30. They now pass
`--fps 30` themselves (`tools/check_sensor.sh`, `tools/check_late_display.sh`),
which is what a fixture that depends on the clock should do. The headless port
stays uncapped - it is the benchmark and test port, and its numbers are
supposed to be the ceiling.

## Verification

* `ctest --test-dir build -j4`: 132/132.
* Desktop SDL2 default (no `--fps`): 600 frames, `stats: ... fps=122.5` on the
  laptop; `sdl-uncapped.log` in `.local-tools/mc051/`.
* Headless in-world: `stats: frames=1200 ... time=3.817s fps=314.4`.
* Android: the port's own `stats:` line follows the Surface rate; the default
  run logs no `fps: cap` line because no property is in play.
