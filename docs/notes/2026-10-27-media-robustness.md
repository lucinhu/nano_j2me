# Media robustness: sanitizers and stress over the MP4V Player

Task round: the MPEG-4 Part 2 video path, `VideoControl` and the movie's
AMR-NB sound track were written feature-first over several rounds.  This round
ran the whole movie-Player surface under AddressSanitizer/UBSan/LeakSanitizer
and under lifecycle churn, on a synthetic clip and on the three real titles,
and fixed what it found.  Three defects were real product bugs, two of them
reachable from a MIDlet.

Commits: `3cee8ff` (Player fixes + regressions in `tests/test_lib.c`),
the docs commit that adds this note.

## Defects and fixes

### F1 - `Player.setMediaTime()` on a movie crashed the runtime

`player_setMediaTime()` (`src/midp/nj_lib_media.c`) decided what to seek by
checking the Player's private flags: platform MIDI, streamed MP3, streaming
MIDI.  A movie Player sets `pStream = 1` - the flag means "pData is not PCM" -
so any movie fell into the streamed-MIDI branch, which `memcpy`s the first
bytes of `pData` into a `struct nj_midi_stream *` and calls
`nj_midi_stream_seek()`.  For a movie that pointer is the decoder holder, not
MIDI state:

    AddressSanitizer: SEGV on unknown address (READ, high address)
      #0 nj_midi_stream_open    src/midp/nj_midi.c:1970
      #1 nj_midi_stream_seek    src/midp/nj_midi.c:1932
      #2 player_setMediaTime    src/midp/nj_lib_media.c:3678

A MIDlet could reach it with one call.  Fixed with a video-kind branch that
runs before the streamed-MIDI branch.  Seeking a movie cannot skip samples: a
P-VOP references the previous picture and the corpus 3GPs carry no I-VOP
index, so `video_seek()` rebuilds the decoder, walks the samples before the
target and decodes each one to rebuild the reference chain, then leaves the
iterator on the target sample.  The position it reports is that sample's
timestamp (`video_seek`, `src/midp/nj_lib_media.c`), and a seek made while
STARTED keeps the Player STARTED.

### F2 - the video decoder was never closed: leaked until VM teardown

`deallocate()`/`close()` released the MIDI and PCM state but not the provider
decoder.  The only pointer to it is inside the Player's `byte[]` holder, and
`media_stream_release()` skipped that holder because its length does not match
`NJ_MEDIA_STREAM_BYTES`.  A title that opens and closes a movie Player per
menu visit grew the host heap by one decoder (and its frame buffers) per visit;
the host allocator only frees those at `nj_vm_destroy()`.  Fixed with
`video_decoder_release()` on `deallocate`/`close` and a matching
`video_decoder_reopen()` on `start()`, because MIDP allows a deallocated
Player to be realized and started again.  The holder, source and frame buffer
stay: only the decoder is released and rebuilt.

### F3 - providers borrowed an `nj_av_mem` that lived on `createPlayer`'s stack

Both video providers keep the `const nj_av_mem *` they are opened with
("borrowed from the caller", `src/midp/nj_h263.c:90`; `m4v_p_free()` in
`src/midp/nj_m4v.c`) and call its `free` in `close()`.  `createPlayer` passed a
stack `nj_av_mem memd`, so closing the decoder later was a use-after-return:

    AddressSanitizer: stack-use-after-return ... READ of size 8
      #0 m4v_p_free            src/midp/nj_m4v.c:3450
      #1 video_decoder_release src/midp/nj_lib_media.c:1622
    address located in stack frame media_createPlayer_stream, variable 'memd'

The latent bug existed for H.263 too; F2's new close path made it fire.  Fixed
on the caller side, because the provider contract is legitimate and
`src/midp/nj_h263.c` is another round's file: each video decoder gets one
host-allocated `nj_av_mem` owned by the Player holder (`media_video_state.mem`),
freed after the provider's `close()`, and a failed reopen gives its fresh block
back.

### Also hardened

- `video_decoder_release()` runs from `player_deallocate()` for every Player
  kind; it now returns unless `pKind == 2`, because the first version
  dereferenced a MIDI Player's holder (ASan SEGV through
  `test_midi_long_song_streams`, `tests/test_lib.c:3819`).
- `player_start()` reports `MediaException("movie decoder is not available")`
  when the decoder cannot be rebuilt, instead of starting a Player that can
  never show a frame.
- A seek made before `start()` no longer replays the sound track from sample 0:
  the PCM cursor is aligned with the new picture position.

## Verification

Sanitizer build `/tmp/nj-media-san` (RelWithDebInfo, `-DNJ_SANITIZE=ON`), normal
build `/tmp/nj-m4v-build` (Release).  Full results, commands and per-file logs:
`.local-tools/debug-runs/m4v-media-robustness-20261027/`.

| Suite | Result |
| --- | --- |
| normal ctest | 116/116 passed |
| sanitized ctest | 115/116 passed; only `test_string_intern` fails, a pre-existing LeakSanitizer report whose test and port files are outside this round's scope (reported to the Lead, not fixed here) |
| `test_lib` under ASan+UBSan+LSan | 954 checks, 0 failures |

`tools/media_stress.c` drives the Player surface the way a MIDlet reaches it
(createPlayer / lifecycle / `setMediaTime` / `VideoControl` / snapshots) and
checks the host block count returns to baseline; on the three corpus clips
under ASan+UBSan+LSan:

| Mode | 1942.3gp | doom.3gp | movie.3gp |
| --- | --- | --- | --- |
| `control` (VideoControl, snapshots, visible) | 22 checks / 0 | 22 / 0 | 22 / 0 |
| `seek` (setMediaTime in every state, rewind, end, loop) | 32 / 0 | 32 / 0 | 32 / 0 |
| `life` (60 rounds of create/start/stop/deallocate/start/close) | 61 / 0 | 61 / 0 | 61 / 0 |

`tools/snapshot_probe.c` saves a VM while a movie Player is playing and loads
it into a fresh VM: 23 checks per clip, no events.  The restored Player is a
plain arena object again (`pKind` 1, `pState` 300, `pData` cleared, frame buffer
kept), i.e. it survives save/load as a silent Player and can be realized,
started, stopped and closed; `getSnapshot()` still works on the kept frame.

The three real titles ran 300 headless frames each under the sanitizers with
no report (`/tmp/nj-media-san/nj2me --port headless --heap 32 --run-frames 300
--dump-frames ...`); dumped frames show their intro movies rendering, and the
game-side exceptions in the logs are the same handled ones as in a normal run.

## Permanent regressions

`tests/test_lib.c` gained two tests next to `test_video_playback_pacing`:

- `test_video_seek_mp4v()` - `setMediaTime()` on a movie returns instead of
  crashing, lands on the requested boundary, reports the same position from
  `getMediaTime()`, a rewind clears the pump counter, a seek while STARTED
  keeps the Player STARTED, and a deallocated Player can be realized and
  started again to decode a frame (F1, F2);
- `test_video_decoder_release()` - host block count via `nj_vm_get_stats()`
  is back to baseline after three create/start/pump/stop/deallocate rounds and
  after `close()`, and the rebuilt decoder still produces a frame (F2, F3).

The harnesses themselves are not in `ctest` (they need a sanitizer build and
real movie files); they live in `tools/` with their build lines in the header.
