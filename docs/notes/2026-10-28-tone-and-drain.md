# The tone that had no length, and the tail the close threw away

Two audio defects were found while testing the 0.3.x runtime on the Android
emulator, with the phone reports as the starting point:

* a `ToneControl` sequence was silent on every port that has a tone
  synthesizer, and
* a one-shot sampled sound was cut short - a one second wave played for about
  a third of a second.

Both are in the runtime, not in a title, and both are now fixed and covered by
tests that fail against the old behaviour.

## Tone: an `[I` pair array read as a `nj_audio_tone` array

`player_start()` kept a tone sequence as a Java `int[]` of `{frequency,
duration}` pairs (`F_P_TONES`) and handed the array's bytes to the port's
`tone_play()` with a cast:

```c
port->tone_play(port->ud,
                (const nj_audio_tone *)(const void *)nj_array_data(tones), n);
```

`nj_audio_tone` is two 16-bit fields:

```c
typedef struct nj_audio_tone { nj_u2 freq_hz; nj_u2 duration_ms; } ...
```

so on little-endian hardware the low half of each 32-bit frequency word became
the frequency and the high half - always zero for the frequencies a game uses -
became the duration.  The Android/synth path therefore received
`duration_ms = 0` for every note: the port log read

```
trace: tone_play count=2 f0=440 d0=0
```

and the synthesizer had nothing to play.  Ports that render tones themselves
(the SDL2/desktop path) read the same array field-by-field, which is why the
bug only showed on the platform synthesizer.

Fix: copy the pairs into a local `nj_audio_tone` array (64 notes is the MIDP
limit) and pass that.  Regression test: `tests/test_audio.c` asserts the last
sequence the headless port received is A4 440 Hz/100 ms, C5 523 Hz/100 ms and
a rest 0 Hz/50 ms, through a new `nj_port_headless_tone_last()` helper.  Before
the fix the test reports three duration failures; after it, 52 checks pass.

## Sampled one-shots: closing the device before the queue drained

The runtime's sampled path pushes PCM into the port until the last frame is
accepted and then, on the same frame, calls `media_dev_release()` ->
`pcm_close()`.  A port is a queue: the Android port keeps 16384 frames in its
ring and the audio device plays them asynchronously.  The trace showed

```
pcm_write took 16383 of 22050 frames (ring full)
pcm_close port ring=15906
```

i.e. a third of a second had been played and the remaining ~0.72 s was thrown
away by the close.  Every one-shot sampled sound shorter than the queue plus
the frames already played had the same tail cut; looping sounds were fine
because they never reached EOF until the title stopped them.

Fix: a Player records how much its device can hold when the device is handed
to it (`F_P_DEVROOM`, measured through `pcm_free()` right after `pcm_open()`,
where the queue is empty).  At EOF the pump keeps the Player open - and
`END_OF_MEDIA` unfired - until `pcm_free()` reports the queue is empty again,
then releases the device and fires the event.  Ports without `pcm_free()`
(the headless test port) keep the old behaviour.

Regression test: `test_eof_drain()` in `tests/test_media_replay.c` drives a
mock queue whose `pcm_free()` shrinks as the test hands frames to it and
asserts the Player stays in state 400 with the device open while the tail is
queued, then closes and reaches state 300 once the queue has drained.
Without the runtime fix the test reports two failures.

## What the emulator showed

The verification method is a QEMU `wav` audio backend on the emulator: the
`pa` backend cannot initialise in this environment, but
`QEMU_AUDIO_DRV=wav QEMU_WAV_PATH=/tmp/qemu-audio.wav` makes the host write
the guest's audio to a file (44100 Hz stereo s16le; the RIFF size fields stay
zero and the PCM starts at offset 44).  `/tmp/audio-probes3` is a scripted
MIDLet that plays nine scenarios with distinct frequency signatures (440/523
tone, 330 wave, 660 loop, 550 stop/resume, MIDI play/loop/pause/resume/recreate
and tones over a song); the capture is sliced from a recorded file offset and
analysed by energy and by a narrow-band detector.

API 26 (Android 8.0, x86_64): every scenario behaves.  Tone 0.82 s,
one-shot wave 1.02 s, wave loop 2.2 s and the title-driven stop, wave
stop/resume with the silent gap, MIDI play/loop, a 4 s pause with
`getCurrentPosition()` frozen and playback resumed from there, and
`deallocate()` + `realize()` + `start()` re-acquiring the synth.  The probe
finished with `PROBE done` and no error marker.

API 21 (Android 5.0, x86_64): the PCM paths behave identically, and the same
fixes apply, but this emulator image's `MediaPlayer.pause()` does not stop a
platform MIDI player: `isPlaying()` stays true and `getCurrentPosition()`
keeps advancing after the call (an AOSP 5.0 / Sonivox image quirk, not a
runtime path - the runtime only calls `pause()`).  On API 26 the same probe
pauses correctly.  Recorded here so the observation is not re-investigated as
a runtime bug; the real title evidence for `pause()` semantics remains the
phone.

## Verification

* `tests/test_audio.c`: 52 checks, includes the tone-duration regression.
* `tests/test_media_replay.c`: new `test_eof_drain()`; drain guard removed ->
  2 failures; restored -> 0.
* Full ctest: 122/122 (the fixes add no test binary).
* Emulator capture rig and raw captures:
  `.local-tools/debug-runs/android-audio-probe-20261028/` (`audioprobe.jar`,
  `captures/probe8.raw` = pre-drain-fix API 21 run, `captures/probe10.raw` =
  API 26 full run, the probe sources and the analysis scripts).
