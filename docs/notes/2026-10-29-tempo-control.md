# TempoControl and RateControl: re-timing a MIDI song without restarting it

JSR 135 declares two controls for playback speed and this runtime had neither:

```java
public interface RateControl extends Control {
    int setRate(int millirate);   // 1000 = normal
    int getRate();
}
public interface TempoControl extends RateControl {
    int setTempo(int milliTempo); // milli-beats per minute, 120000 = 120 bpm
    int getTempo();
}
```

速度与激情6_9 asks its music Player for `"TempoControl"`, null-checks the
result and catches `Exception`, so the missing class did not crash the title -
it degraded silently: the music ran at the file's own tempo and `setTempo()`
changed nothing.  But the linkage error is real even with the null check, and a
probe fixture shows it:

    pre   TEMPO ERROR java.lang.NoClassDefFoundError:
            javax/microedition/media/control/TempoControl

Our `checkcast`/`instanceof` resolve the class while looking at the control, so
a title that names the interface can lose the read even when it would have
handled a null.

## What the controls move

The renderer schedules every event at a sample position derived from its tick
through `mid_sample_at()` (which integrates the file's own Set Tempo events).
The controls scale that map:

    scaled_sample(tick) = mid_sample_at(tick) * rate_num / rate_den

with `rate_num/rate_den` a *duration* factor (`1/1` is the file's own tempo).
`base_milli` is what the file's first Set Tempo means in JSR 135's unit (500000
us per quarter note is 120000), so:

* `setTempo(m)`: factor = `base_milli / m` - half the tempo is twice the samples
* `setRate(r)`: factor = `1000 / r` - twice the rate is half the samples
* `getTempo`/`getRate` derive the current value from the stored factor

The release, reverb and chorus tails are in seconds of audio and are **not**
scaled, so half tempo makes the song a little less than twice as long while a
still-decaying note keeps its real release.  `getDuration()` follows: the
control natives write the new frame count and duration back to the Player.

## Position, seek, restart

Changing tempo while the song plays must not jump.  The control finds the tick
the next pull would have started at from the **old** map (binary search over
the tick range with `mid_sample_at`), installs the new factor, and re-maps that
tick through it.  The two memoised (tick, sample) pairs the pull loop keeps are
refreshed too, because only their tick half stays valid.

`nj_midi_stream_seek()` rebuilds the renderer from the file, so it copies the
factor into the fresh stream before rendering to the target frame - otherwise a
`setMediaTime()` on a slowed-down song would silently return to normal speed.
`nj_midi_stream_restart()` does the same for loops.

## Platform synthesizers

A Player the port's own synthesizer is playing cannot be told a tempo (Android's
`MediaPlayer` has no such API).  The first call takes the song over into this
library's renderer at the position the synth had reached - the same hand-over
`MIDIControl` already performs for note events - and the log says so.  After
that the control is real, at the price of the port's instruments being replaced
by the runtime's.

## Controls and getControls()

`getControl("TempoControl")` and `getControl("RateControl")` both return the one
`TempoImpl` object, whose super chain is `TempoControl -> RateControl -> Control`,
so a reference typed either way resolves and casts.  `getControls()` gained the
entry: a MIDI Player now has three (Volume, Tempo, MIDIControl), and the fixture
asserts the array holds no NULL - a game walking it would dereference one.

## Verification

`tests/test_midi.c` (renderer level): default tempo 120000; `setTempo(60000)`
doubles the length and reports rate 500; the position is kept within two frames
across the change (compare `nj_midi_stream_position()` before and after, which
is why that getter exists); restoring the tempo restores the exact length;
`setRate(2000)` halves it; a restart keeps both the tempo and the length.

`tools/check_tempo_control.sh` (ctest `check_tempo_control`) builds
`tests/tempodemo/TempoDemo.java` against the stub API with a generated 120 bpm
MIDI file and asserts the interpreter's line:

    TEMPO control=1 isTempo=true isRate=true t0=120000 set=240000 rate=2000
          rate_set=500 tempo_back=60000 dur_fast=true dur_slow=true
          nulls=0 tempos=1

A/B, same fixture and command: the build before this change prints
`TEMPO ERROR java.lang.NoClassDefFoundError: javax/microedition/media/control/TempoControl`;
this build prints the line above.  Full ctest 128/128.

## Limits

The hand-over from a platform synthesizer changes the instrument set to the
runtime's own (documented, and the same trade `MIDIControl` makes).  SMPTE-timed
files keep their existing (unconverted) timing and only scale with the factor.
No real-game A/B is claimed: in a headless run of 速度与激情6_9 its music Player
slots are null, so the tempo call site throws on the null Player before it can
reach the control - the fixture models that call sequence exactly.
