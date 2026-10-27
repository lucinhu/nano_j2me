# The music that never came back

The previous note (`2026-10-27-midi-loop-restart.md`) fixed a looping MIDI
song whose loop restart re-opened the song from a freed byte array.  A user
reported that background music *still* stops for good: in
`极品飞车：热血追踪` (`极品飞车14之热力追踪中文版_4160.jar`) there is no sound
after the race has been going for a while (on a handset: after being caught by
the police).  The game keeps running and drawing; only the audio never returns.

This note is about the second, independent cause: the runtime has **one PCM
device per VM** and every sampled `Player` used to drive it directly.

## One device, many Players

`nj_port` exposes a device, not streams:

```c
int (*pcm_open)(void *ud, int rate, int chans);
int (*pcm_write)(void *ud, const void *samples, int frames, int rate, int chans);
void (*pcm_close)(void *ud);
void (*pcm_free)(void *ud);
```

`pcm_open()` configures the device, `pcm_write()` pushes interleaved frames,
`pcm_close()` closes the device.  The ports honor that shape literally: the
SDL2 port's `sdl_audio_ensure()` closes and re-opens the device when a caller
asks for a different rate or channel count, and `sdl_pcm_close()` closes it
unconditionally.  The Android port has one PCM ring; the libretro port has one
frontend stream.

Every sampled `Player` used to do its own `pcm_open`/`pcm_write`/`pcm_close`
in `nj_lib_media.c`.  With one Player that is exactly right.  With two it is
not: a second Player's `pcm_open()` reconfigures the device under the first,
and its later `pcm_close()` closes the device on the first.  The first Player
keeps pumping, `pcm_write()` returns -1 and the return value is ignored, so the
music is gone for good while the game happily keeps drawing.  The platform
synth (tone) path had the same problem from the other side: the SDL2
`sdl_tone_play()` called `sdl_audio_ensure(22050, 1)` unconditionally, so a
sound effect made the port re-open the device out from under the music.

The synthetic check is in the evidence directory (see below).  A MIDlet starts
a looping 22,050 Hz stereo WAV Player, then starts and stops a short 8,000 Hz
mono WAV Player.  Before the fix the second Player leaves the device closed:
the log has two "audio: disk opened" lines and nothing more, and the raw PCM
capture holds 12,288 bytes - just the sound effect.  After the fix the log has
a third open, the arbitration lines below, and 745,472 bytes: the music keeps
streaming.

```
media: the PCM device moved to another Player, 8000 Hz, 1 channel(s)
media: the PCM device went back to a paused Player
```

## The fix

`nj_vm` grows one reference, `vm->pcm_owner`.  The rules live in
`nj_lib_media.c`:

- `media_dev_claim(vm, p)` closes the previous owner's device, configures it
  for `p`'s rate and channel count and records `p` as the owner.
- `media_dev_release(vm, p)` closes the device when `p` is the owner, then
  walks `vm->media_players` in insertion order and hands the device to the
  first Player that still wants it (`state == 400`, sampled or AMR audio).
  A Player that was paused resumes exactly where it stopped: only the device
  was closed, not the Player.
- `nj_media_frame()` pumps a sampled Player only while it owns the device.  A
  Player that wants the device but does not own it stays in `STARTED` with its
  position frozen, so it is resumed later rather than reporting
  `endOfMedia`.
- `player_start()` claims the device for sampled players and always registers
  the Player with the pump, so a waiting Player can be handed the device by
  the frame loop.  `player_stop()` releases it.
- `VideoControl`'s direct video audio path uses the same claim/release.
- The SDL2 `sdl_tone_play()` renders its tone in the **current** device format
  when a device is open (interleaving channels, using the device rate) and
  only opens its own 22,050 Hz mono device when none is; `sdl_tone_stop()`
  keeps clearing the queue and documents why SDL cannot clear part of it.
- Snapshot loading (`nj_vm_load`) clears `vm->pcm_owner`, like the other
  host-facing references: a restored Player must not reference the device of
  the VM that saved it.

## Verification

- `tests/test_media_replay.c` grows `test_device_handover()` on a mock port:
  A (8000 Hz) owns the device; B (16000 Hz) claims it and freezes A; stopping
  B hands the device back and A's frames advance again.  The first version
  used a 400-frame B, which finished inside the pumped frames and handed the
  device back in the same frame; the test now uses 2,000 frames so the
  ownership change is observable.
- Full `ctest` is 122/122; the media subset (`test_midi`,
  `test_media_backpressure`, `test_media_replay`, `test_audio`) all pass.
- Port-level A/B: `build/nj2me` versus a build of `952869f` (the commit before
  this fix) run the synthetic overlap fixture with the SDL2 port and
  `SDL_AUDIODRIVER=disk`.  Before: 2 device opens, 12,288 bytes captured.
  After: 3 device opens, 745,472 bytes, both players `state=400` throughout.
- The 极品飞车 title reaches the race headlessly (keys script: ANSWER the
  sound prompt, menu, car, tutorial) but the arrest happens inside gameplay,
  so the reported scenario cannot be replayed headlessly; the 8,000-frame run
  shows zero handovers before that point, which is consistent with the title
  being serialized up to the race and the overlap appearing later.

The runtime does not mix concurrent streams.  Overlapping Players time-share
the one device (claim/pause/resume); a port that wants to mix may do so below
`pcm_open`, but the runtime now guarantees it is never asked to keep two
owners at once.

## Evidence

`.local-tools/debug-runs/audio-device-ownership-20261028/`:

- `Overlap.java`, `overlap.jar` - the synthetic two-player MIDlet;
- `pre-fix-952869f-sdl2.log`, `post-fix-sdl2.log` - the A/B logs;
- `raw-sizes.txt`, `nfs-post-fix-headless.log`.

Mechanism note for the earlier, separate loop bug:
`docs/notes/2026-10-27-midi-loop-restart.md`.
