# Deterministic timing: replay/netplay feasibility baseline (round 193)

Netplay and input replay need two runs of the same content with the same input
to produce the same game.  Up to round 192 the libretro core deliberately
followed real time: `retro_run()` advanced the MIDlet's virtual clock by the
wall-clock delta, so frame timing (and therefore any title that reads
`System.currentTimeMillis()` or constructs `java.util.Random()` with the
default seed) varied between runs.

## What the option does

`nano_j2me_deterministic` (Core Options → System, default **disabled**):

* advances the virtual clock by exactly **16667 µs per `retro_run()`** instead
  of the measured wall delta (fast-forward and pause behaviour unchanged for
  normal play, since this only applies when the option is on),
* forces the MIDlet frame cap to **60 fps** so every tick can present a frame,
* enables a VM-level deterministic seed source: `new java.util.Random()` takes
  its seed from a 64-bit LCG counter (`nj_vm_set_deterministic()`) in
  construction order instead of the port clock.  Explicit `new Random(seed)`
  and `Math.random()` are unaffected (`Math.random()` already uses a fixed
  internal LCG).

Implementation: `src/core/nj_internal.h` (`deterministic`,
`deterministic_seed`), `src/core/nj_thread.c`
(`nj_vm_set_deterministic`), `src/midp/nj_lib_util.c` (`rnd_init` seed
selection) and `src/port/libretro/nj_port_libretro.c` (option, fixed delta,
`nj_vm_set_deterministic` after VM creation).

## Measured behaviour (mock frontend, frames dumped every frame)

Two runs with `--set nano_j2me_deterministic=enabled`, comparing the
sha256 of every PPM.  "load frames" are frames the core presents while
`retro_load_game()` is still pumping the MIDlet constructor/startApp.

| title | retro_run calls | frames presented | differing frames |
|---|---|---|---|
| `tests/fixtures/alphademo.jar` | 60 | 61 | **0** |
| `家园5汉化版_10376.jar` | 60 | 77 | **0** |
| `雷霆战机2012BT版_3101.jar` | 90 | 107 | **0** |
| `雷霆战机2012BT版_3101.jar` | 120 | 137 | 1 (frame 130) |
| `loopdemo.jar` (clock-sampling animation) | 40 | 57 | 10 |

The remaining differences are a *frame-boundary race*, not RNG: the VM runs on
its own thread and the frontend advances the clock one tick per `retro_run()`,
so a title that samples the clock inside a frame can observe a different
virtual millisecond depending on where the frame happened to be when the tick
landed.  Titles whose logic is frame-driven (alphademo, 家园5, 雷霆战机) are
bit-identical; a title that paces itself by reading the clock (loopdemo)
currently is not.  Final teardown frames can also differ because
`retro_unload_game()` races the VM thread.

## Netplay status

Netplay stays **not advertised** in the core.  Deterministic timing removes
the wall-clock and default-RNG variability and gives a testable baseline
(`tools/check_libretro_determinism.sh`, CTest `check_libretro_determinism`,
two alphademo runs must present byte-identical frames), but bit-exact netplay
also needs frame-locked execution: each `retro_run()` must run exactly one
MIDlet frame up to its boundary before returning, so clock samples cannot
straddle ticks.  That is the next step if netplay is pursued; it touches the
async VM thread model and needs save-state rollback tests for every title
class.

## Regression gate

`tools/check_libretro_determinism.sh` runs the fixture twice through the real
core ABI with the option enabled and fails on any frame difference.  It also
checks that the core advertises `nano_j2me_deterministic`.
