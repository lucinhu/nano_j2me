# The audible position, across loops and seeks

The sampled-audio Player reports two positions: the frames it has pushed into
the device (`F_P_PUSHED`) and the time anyone has actually heard
(`getMediaTime()`).  With the device queue in the way they are not the same
number, and the accounting had three gaps that a looping effect makes visible.

## What was wrong

1. **The queue was subtracted from the wrong Player.**  `media_played()` read
   the device's queue for *any* Player that asked, so a Player paused because
   another one owned the device subtracted that Player's queue from its own
   pushed count.  It now only consults the queue when `vm->pcm_owner` is the
   asking Player.
2. **A looped effect lost its tail.**  When the queue still held frames from
   the previous loop (or several whole loops of a short effect), subtracting
   the queue from `F_P_PUSHED` went negative and the position was clamped to
   zero, losing the frames that had already been heard.  `media_played()` now
   walks back across the loop boundaries with `F_P_LOOPDONE` and the loop
   length instead of clamping.
3. **A seek replayed from the old queue.**  `setMediaTime()` moved the decoder
   and then kept the device queue, so the discarded suffix played over the new
   position.  A successful seek now discards the queue without syncing it back
   (`media_dev_close(vm, keep_position = 0)`) and re-opens the device at the
   new position; failed seeks keep the old behaviour.

Two smaller pieces follow from the same accounting: `getMediaTime()` refreshes
`F_P_TIME` from the played count when the Player owns the device, and the
end-of-media branch waits for the queue to drain (via `pcm_queued`) before it
closes the device and fires `endOfMedia`.

## Verification

`tests/test_media_replay.c` (new) replays a 32-frame WAV several times through
a loop Player and compares every sample against the expected waveform,
including after EOF, and checks the reported position.  `tests/test_lib.c`
gains the looped-effect and seek cases behind `test_player_*`, and
`tests/test_media_backpressure.c` covers the drain-before-end behaviour.

Full ctest 122/122, `tools/check_no_statics.sh` clean.  The change is behind no
compile-time switch: it is Player state accounting, not decoder code, so it
costs no RAM beyond the two existing fields it reads.

Two follow-ups in the next round:

* `tests/test_media_replay.c` also pins the "port reports a queue but no free
  space" branch - `pcm_free` NULL, `pcm_queued` answering - with
  `test_eof_drain_queued_only()`.  With the older pcm_free-only condition
  restored it reports 2 failures; the drain really does consult the queue.
* A second test that was committed in this round was never called from `main`
  and never exercised: its expectations assumed the mock device could hold more
  than its capacity.  It has been replaced by the test above rather than left
  as dead code.
