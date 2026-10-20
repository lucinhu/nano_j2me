# Reuse the private input buffer for 16-bit WAV

## Reproduction

The original `tests/games/愤怒的小鸟-高清版_2353.jar` has SHA-256
`8b9925e3127d0975979aae7474e917098f652bebc4007a2cafa69d6d4cee4aad`.
This is a different game build from the landscape SAX/XML title.

At 240×320 with a 32 MiB heap, a fresh RMS directory and fire presses at frames
240, 360 and 480, the game reached its first-level loading screen, then failed:

```text
thrown: java/lang/OutOfMemoryError at javax/microedition/media/Manager.createPlayer(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player; pc=0: (no message)
uncaught exception in thread 2: java.lang.OutOfMemoryError @ luma.k.c+149
```

The 600-frame baseline still exited with status zero because the frame budget
expired. Its visible level-loading screen and thread exception are the relevant
failure evidence; the process exit code alone did not detect this failure.

## Cause and change

The game contains a 13,059,116-byte `music.wav`, with 13,059,072 bytes of 16-bit
stereo PCM. Its input stream retains the original resource array.
`read_stream()` makes a private Java byte array, and the WAV path then allocated
a third full-size array just to remove the 44-byte RIFF header. These three live
copies, plus the game state, cannot fit in 32 MiB.

[The WAV player path](../../src/midp/nj_lib_media.c) now reuses that private read
buffer for 16-bit PCM. It moves the sample data to offset zero with `memmove()`
and changes the logical array length to the complete sample-frame length. This
also excludes trailing RIFF chunks. The input stream's backing array is not
modified or shared with the player.

Eight-bit WAV still allocates a wider buffer because the output port accepts
signed 16-bit PCM. Existing sample conversion, playback, channel count and
duration calculation are unchanged. There is no new persistent allocation,
configuration flag or dependency. The optimization eliminates one full-size
allocation and the temporary coexistence of two runtime-owned PCM buffers;
it does not turn WAV playback into streaming or free the unused physical tail
of the retained allocation.

## Regression and runtime validation

[The library test](../../tests/test_lib.c) adds a 6 MiB 16-bit stereo WAV with a
trailing JUNK chunk on a 16 MiB Java heap. It checks successful loading, every
sample byte, removal of container/trailing bytes, duration and independence
from the caller's input array. This test fails on the previous implementation
at `createPlayer()` and passes after buffer reuse. Existing 8-bit widening,
large WAV, MIDI and player GC tests also pass.

Validation results:

- Library suite: 713 assertions passed, zero failures.
- Full CTest suite: 85/85 passed.
- AddressSanitizer/UndefinedBehaviorSanitizer with leak detection: library suite
  passed, with no reported sanitizer error.
- Release CLI with `NJ_ENABLE_WAV=0`, `NJ_ENABLE_MIDI=0` and `NJ_ENABLE_AMR=0`
  built successfully. Strict C99 freestanding syntax check with `NJ_USE_LIBC=0`
  and `-Wall -Wextra -Werror` passed.
- Current release executable: text 2,721,534 → 2,721,470 bytes (−64), data
  159,752 bytes and BSS 6,688 bytes unchanged. This is a measurement of this
  build, not a guarantee across compilers.

The unchanged original JAR was replayed at 240×320, 32 MiB, paced at 30 fps:

```sh
./build/nj2me --port headless --size 240x320 --heap 32M --fps 30 \
  --run-frames 1200 \
  --keys '240:fire 360:fire 480:fire 900:soft2 1020:fire' \
  --rms-dir .local-tools/debug-runs/angry-hd-round14/interaction/rms \
  --dump-frames .local-tools/debug-runs/angry-hd-round14/interaction/frames/ \
  --dump-every 60 --stats 'tests/games/愤怒的小鸟-高清版_2353.jar'
```

The headless run reached the level, opened the pause menu at frame 960 and
returned to the slingshot at frame 1200. It completed all 1,200 frames at
28.1 fps including loading, without an uncaught exception or OOM. The observed
port accepted 3,264,768 PCM frames. The paired SDL2 run completed 1,200 frames
at 28.6 fps and opened PipeWire audio at 44,100 Hz, two channels, without an
uncaught exception or OOM. Screenshots come from the headless run.

All runs use isolated RMS directories under
`.local-tools/debug-runs/angry-hd-round14/`; the original JAR and user RMS were
not changed. This verifies first-level loading and pause/continue, not a
complete level or game playthrough.
