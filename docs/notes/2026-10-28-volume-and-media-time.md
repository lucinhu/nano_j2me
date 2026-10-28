# The volume that never reached the samples, and the position that ran ahead

Two more audio defects were found by driving the runtime directly on the
Android emulator (the 0.3.x audio rig described in
`docs/notes/2026-10-28-tone-and-drain.md`).  Both are in the runtime's own
media layer; no title is involved.

## Volume: `VolumeControl` was a no-op for sampled players

`vol_setLevel()` only stored the level in `F_P_VOLUME` and forwarded it to the
platform MIDI synthesiser.  The comment said the rendered paths scaled their
own samples, but only the movie-audio pump (`video_audio_pump()`) did: the
sampled WAV/AMR path and the streamed MIDI renderer wrote decoded samples to
`pcm_write()` at full scale.  `setMute(true)` had the same hole - it set
`F_P_MUTED`, which nothing on those paths read.

The probe made it visible as an absent gap: a WAV player was started, the
level set to 0 for 400 ms, then restored, and the host capture showed
continuous energy.  The capture's inter-step silence made the same window
obvious by contrast: every step boundary was quiet, the painted mute window
was not.

Fix: a `media_pcm_write()` helper stages samples through a small `nj_s2`
buffer and scales them by `volume/100` (or zero when muted) before handing
them to the port.  All three `pcm_push()` write sites (streamed MPEG audio,
streamed MIDI, plain sampled) now go through it.  Port selection and buffering
are unchanged; a port that already mixes tone and PCM still receives scaled
samples.

## Media time: `getMediaTime()` reported the write frontier

`pcm_push()` advanced `F_P_TIME` from `F_P_PUSHED`, the number of frames
handed to the port.  The port's queue is not the speaker: the Android ring is
16384 frames, which at 22050 Hz is ~0.74 s inside the device before anything
is audible.  Probing a 2 s WAV, `getMediaTime()` after 700 ms of wall clock
read 1,439,002 us - it reported the front of the queue, not the sound.

The user-visible consequence was worse than a bad number: `stop()` closes the
PCM device (dropping the queued tail) and `start()` resumed from the stale
frontier, so the tail between the audible position and the write frontier was
never heard.  A 2 s sweep stopped at 0.7 s and restarted skipped almost 0.4 s
of the 880 Hz half.

Fix: a new optional port callback

```c
int (*pcm_queued)(void *ud);   /* source frames written but not yet audible */
```

`media_played()` = `F_P_PUSHED - pcm_queued()`, and that is what
`getMediaTime()` reports.  When a device is closed for a stop, a hand-over or
`deallocate()`, `media_sync_played()` first rewinds `F_P_PUSHED` to the
audible position, so a following `start()` continues from where the user
actually was.  End of media keeps setting the position to the file's duration,
which is where the sound really ends.  The Android port implements it from its
ring indices (`used * src_rate / out_rate`); SDL2 uses
`SDL_GetQueuedAudioSize() / (channels * 2)`.  A port that leaves the callback
`NULL` keeps the old write-frontier behaviour, so no other port had to change.

`tests/test_media_replay.c` gained `test_volume_and_position()`: a mock port
with `pcm_queued`/`pcm_free`, asserting that level 50 halves the samples the
port sees, that mute turns them into zeros, that `getMediaTime()` after 64 of
256 frames played reads 8000 us, that `stop()` keeps that position, and that
`start()` resumes writing from frame 64.  The mock queue has no `pcm_queued`
in the EOF-drain test, which is deliberate: the two tests pin the two
different contracts.

## Verification

The emulator audio rig (`QEMU_AUDIO_DRV=wav`, a scripted MIDLet with distinct
frequency and position steps) on API 26 shows, after the fixes:

* `wave_stop_position`: 696,009 us after 700 ms (was 1,439,002), frozen at
  720,000 us while stopped, then the remaining tail plays out from there;
* `volume`: the 400 ms muted window is silent in the host capture;
* the earlier scenarios (one-shot length, loops, seek, tone, platform MIDI
  play/pause/deallocate/restore) still pass.

`tools/midp_stubs.py` grew `Player.setMediaTime(long)` so probe MIDlets that
use it compile against the stubs; the runtime already implemented the method.

Full ctest 122/122.  Evidence:
`.local-tools/debug-runs/android-audio-probe-20261028/` (probe sources and
captures); this note is the mechanism record.
