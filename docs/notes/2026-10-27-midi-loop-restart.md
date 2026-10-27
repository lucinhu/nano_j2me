# The loop that could not find its song

A looping MIDI player runs out of song bytes.  `极品飞车：热血追踪`
(`极品飞车14之热力追踪中文版_4160.jar`) reported it as background music that
stops for good after the race has been running a while - on a handset the
player hears it after being caught by the police, when the game has been
playing the race track music long enough for it to loop several times.  The
game itself keeps running and drawing; only the music never comes back.

## What was happening

The streamed MIDI path renders the song in chunks instead of holding the whole
file's PCM.  Like the other streamed codecs it keeps the renderer in a small
byte array on the Player (`pData`) and then the loop restart calls
`nj_midi_stream_seek(stream, 0)` - which re-opens the song from
`stream->data`, a raw pointer into the Java byte array that `createPlayer()`
read the file into.

That array was not a GC root.  `createPlayer()` receives it from the
InputStream, opens the renderer over its bytes, stores the renderer in
`pData`, and the byte array stays referenced by nothing else once the call
returns: the renderer is a C struct and the collector cannot see its pointer.
On the first collections the array is reclaimed; when a loop then restarts,
`nj_midi_stream_open()` is handed whatever memory now occupies that address.
Parsing it yields no usable song, the open returns NULL, `seek()` reports
`0`, and `media_restart()` - which only saw a seek call - reported the loop as
successful.

The next pump pulled from the renderer that was still parked at the end of the
song.  `pcm_push()` read `done = 0` frames and wrote that as the Player's frame
total.  From then on `media_restart()` refused (`pFrames <= 0`), the next frame
closed the PCM device and fired `endOfMedia`, and the race ran on in silence.
The platform-synth takeover path had the same shape: the platform-backed MIDI
Player stored the file bytes in `pData`, and the takeover replaced `pData`
with the renderer handle without leaving the bytes rooted anywhere.

The instrumented trace shows the moment, for `two_fingers.mid` (13,814 bytes,
22,050 Hz stereo, 1,363,678 frames ≈ 61.9 s, `setLoopCount(-1)`; the loop
restarts every ~333 emulated frames because the headless device accepts
everything):

```
f=1740 LOOP_RESTART want=-1 done=1 rr=1
...
f=3405 LOOP_RESTART want=-1 done=6 rr=1
f=3738 [midi] seek FAIL open fresh NULL
f=3738 LOOP_RESTART want=-1 done=7 rr=1      <- falsely reported success
f=3739 STREAM_EOF done=0 frames_field=1363678
f=3739 RESTART_FAIL frames<=0
f=3739 PCM_CLOSE(end)
```

Full trace: `.local-tools/debug-runs/nfs-midi-loop-20261027/pre-fix-instrumented-trace.log`.

## The fix

- The Player keeps the song bytes alive: `pSrc` is set to the source array in
  `createPlayer()`'s renderer branch and in `midi_take_over()`.  `pSrc` is the
  same field the MP3 and video players already use to root the file bytes their
  decoders re-read.
- `nj_midi_stream_restart()` is new: same rebuild as a seek to zero, but it
  returns whether the song could be re-opened instead of a frame number, so a
  restart that cannot happen is no longer reported as success.
  `media_restart()` uses it and fails honestly.
- `pcm_push()` only corrects `pFrames` when a pass actually rendered
  something; a reset that pulls nothing no longer turns one failed restart
  into a Player that can never play again.

## Tests

- `tests/test_midi.c`: a restarted stream replays the whole song byte for
  byte, and a stream whose bytes are no longer a song reports `restart() == 0`
  instead of pretending to rewind.
- `tests/test_media_replay.c` `test_midi_stream_loop`: a looping MIDI Player
  runs three loops, drops every local reference, collects the heap with
  `nj_gc_run()`, and keeps playing past six loops with `pSrc` still rooted and
  the state still STARTED.  On the old code the very first assertion fails
  (`pSrc` is NULL).
- Full `ctest`: 122/122.

## A/B on the title

Same command, 8,000 frames at 176x208, `--fps 30`, the same key script
(`150:soft1,320:down,380:soft1,700:soft1,1000:soft1,1300:soft1` plus the
tutorial taps):

| binary | audio after the race music starts | pcm_frames |
| --- | --- | --- |
| pre-fix `78c4de1` | `[midi] seek FAIL` after 7 loops; silent from ~frame 4,300 on | 11,890,477 |
| fixed | loops for the whole run (43 collections happened) | 30,385,250 |

The intact run pushes roughly one 1.36 M-frame song per 333 frames; 11.9 M is
the eight loops before the broken restart plus nothing after it, 30.4 M is the
full 8,000-frame race.  Logs:
`.local-tools/debug-runs/nfs-midi-loop-20261027/pre-fix-78c4de1-8000.log`,
`post-fix-8000.log`.
