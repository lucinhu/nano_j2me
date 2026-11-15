# Libretro async load: states saved during the first seconds may not restore

Status: **open** — affects the core built from `a2abdee` and later, including
HEAD `68bffb9`. The r24 package (built from `770d7b0`) and the
`a296ce8` hardening candidate are not affected.

## Summary

`a2abdee` ("construct the MIDlet on the VM thread so a loading-screen
constructor cannot block retro_load_game") moved MIDlet construction out of
`retro_load_game()` into `lr_vm_thread`. After that change, a libretro state
saved during the first seconds of `快打旋风_10825.jar` (FinalFightMidlet) can no
longer be restored, and the failed restore leaves the session shut down and
unsaveable.

The mock frontend's serialize/restore conformance test shows it directly: the
same title passes all 45 checks with the pre-`a2abdee` core and fails three
checks with the current core when the snapshot is taken early.

## Reproduction

```
H=/var/tmp/nj2me-lrstate-build/test_libretro_frontend
CORE=<core>/linux-x86_64/nano_j2me_libretro.so

for frames in 60 120 300 600; do
  $H --core "$CORE" --jar tests/games/快打旋风_10825.jar \
     --frames $frames --save-dir /tmp/ff-$frames
done
```

Results:

| core | frames=60 | frames=120 | frames=300 | frames=600 |
|------|-----------|------------|------------|------------|
| r24 (`770d7b0`) | PASS 45/45 | PASS | PASS | PASS |
| r25 candidate (`a296ce8`, pre-`a2abdee`) | PASS 45/45 | PASS | PASS | PASS |
| r25 from HEAD `43a38b3` (`a2abdee` + tool fix) | FAIL 3 checks | FAIL 3 checks | PASS | PASS |
| linux core rebuilt from HEAD `68bffb9` | FAIL 3 checks | FAIL 3 checks | PASS | PASS |

## Failing checks and log

With the current core at `--frames 60`/`--frames 120`:

```
PASS unserialize_rejects_other_font: valid-checksum state with different font identity must be refused
FAIL unserialize_accepts_resigned_state: restoring original font identity and checksum must succeed
[info] core requested SHUTDOWN
FAIL unserialize_refusal_keeps_running: the session must remain active after refused states
FAIL serialize_after_refused_restore: the live VM must remain saveable after refused states
```

The core log around the failure also contains:

```
[core][info] [nano_j2me] font: loaded /usr/share/fonts/TTF/DejaVuSans.ttf (crc 4c61f78b)
[core][warn] [nano_j2me] save-state request refused (-7); the game continues
[core][warn] [nano_j2me] save-state request refused (-7); the game continues
[core][info] [nano_j2me] resumed from a save state
```

At `--frames 300` and `--frames 600` the same save/restore round trip passes
with no refusal.

## Working hypothesis

With synchronous construction the MIDlet's constructor (and its class/resource
loading, including the built-in font resolution logged by the port) completed
before `retro_load_game()` returned, so every state ever saved by a frontend
already carried the resolved font identity. With `a2abdee` the VM thread starts
`retro_run()`-driven frames while/after constructing on that thread; a state
captured in the first couple of seconds can therefore have been produced before
the active font identity settled (or with a VM that is still inside
`nj_vm_start_app_async`'s startup), and restoring it later fails the font
identity check (`-7`). The refusal path then ends in a requested shutdown,
which is why the session is no longer usable afterwards.

This is the same class of issue as the Doom3 limitation recorded in
`68bffb9`: the constructor/loading phase is now interleaved with frontend
frames. Ownership of the load/state path is with the async-load work
(`a2abdee`, `68bffb9`), so this note records the observation instead of
patching that code mid-flight.

## Impact and decision

* Only very early saves of titles with deferred constructor/font work are
  affected; saves taken after a few seconds behave normally.
* The r25 release package built from HEAD is **held**: the canonical
  `build/libretro-dist` stays r24 until the regression is resolved or
  explicitly documented as a limitation. The validated candidates remain at
  `/var/tmp/nj2me-submission-dist-r25-a296ce8` (pre-`a2abdee`, all probes
  clean) and `/var/tmp/nj2me-submission-dist-r25` (current HEAD, visual/mock/
  Wine/RetroArch clean, state probe 49/50 due to this issue).
* Follow-up for the async-load owner: either finish construction/font
  resolution before frames are published, or make the post-refusal path keep
  the live VM intact so a refused restore can never shut a session down.

## Refined diagnosis (same day, after deeper probing)

The failure is **not font-specific** and is not about a stale font identity:

* It reproduces with `--set nano_j2me_font=builtin` (no host font data pinned at all).
* It is a **frontend cadence** artifact of the async load: at
  `--frames 60 --pace-us 16666` (a real 60 Hz cadence) the whole 45-check run
  passes; at `--frames 60` with the harness default `--pace-us 0` it fails.
  Same for `--pace-us 33333`. `--frames 300`/`600` pass even unpaced because by
  then construction has certainly finished.
* Instrumented `lr_apply_state()` (temporary, private build) shows the second
  restore never reaches the envelope checks; instead, right after the first
  successful restore the VM log shows
  `thrown: java/lang/OutOfMemoryError at java/lang/Object.getClass()...`
  followed by `core requested SHUTDOWN`. The state that was saved a moment
  earlier came from a VM whose MIDlet construction/`startApp()` had not
  completed (the `a2abdee` load path returns after a fixed 250 ms construction
  grace, and an unpaced frontend delivers 60 frames in milliseconds), so
  restoring it later yields a VM that immediately fails.

## Mitigation and recommendation

* `tools/check_libretro_states.py` now paces frames at 60 Hz by default
  (`--pace-us 16666`, `0` disables) for exactly this reason: a real frontend
  calls `retro_run()` in real time, and the probe should model that. The
  early-save scenario is then covered by the paced run instead of an artificial
  unpaced burst.
* Recommended port fix for the async-load owner: gate state requests on
  `s->load_ready` (refuse both save and load while construction/`startApp()`
  has not completed) in `lr_request_state()`/`lr_serve_state_request()`.
  Then no frontend can persist a half-loaded VM state; a refused request is
  already handled gracefully, while a bogus saved state silently corrupts the
  next restore.
