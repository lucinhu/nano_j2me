# The platform MIDI handle that deallocate() took away

2026-10-28.  Reported against 极品飞车14之热力追踪中文版_4160 on an Android
phone (Android 15, Sonivox's General MIDI synth): pausing the game with the
title's own pause screen and resuming brings the picture back but not the
music.  Thanksgiving earlier: after being caught by the police, "重新挑战"
was silent in the same way.

## What the log said

Two independent bugs were already fixed before this one: the looping streamed
MIDI Player that lost its source bytes across a collection (round 93) and the
single PCM device driven by many Players (round 94).  A diagnostic build
(`v0.3.4`, logs in `.local-tools/debug-runs/android-midi-reacquire-20261028/`)
showed a third path, on the platform synthesizer concretely:

    platform open id=3 duration 13814            (race music)
    platform start id=3 loop=true
    midi platform stop id=3                      (pause)
    midi platform close id=3                     (Player.deallocate())
    media: setLoopCount state=200 (plat=0)       (resume)
    media: volume ... plat=0

After the pause the title called `stop()` and `deallocate()` on its music
`Player` - legal MIDP, `deallocate()` is documented to release scarce
resources - and then, on resume, called `realize()` and `start()` on the same
Player object.  The log shows the resume calls with `plat=0`: the runtime had
already released the platform handle and cleared `F_P_PLAT`, and `realize()`
did not give it back.  `start()` then fell through to the sampled path with
`F_P_DATA` still holding the song bytes: a MIDI file played as if it were
PCM, which is silence (and, at the wrong rate, worse than silence).

## The fix

`player_realize()` now re-acquires the port's synthesizer for a Player the
platform itself took at creation:

    if (!F_P_PLAT && F_P_MIDIK && !F_P_STREAM && port->midi_open)
            midi_open(F_P_DATA) -> F_P_PLAT = 1, duration from the port

`F_P_STREAM` separates the two kinds of MIDI Player: a platform-taken one
keeps the song in `F_P_DATA` and no stream state; a rendered one (ports
without a synth, or files the synth refused) keeps a `nj_midi_stream` in
`F_P_DATA` plus the source bytes in `F_P_SRC`, and is already restored by its
own stream code.  The renderer path never has `F_P_STREAM` clear while
`F_P_MIDIK` is set, so it is untouched.

If the synth refuses the song again the Player stays silent rather than
falling into the PCM path; that is the same policy `createPlayer()` uses for
a MIDI file it cannot play.

## Test

`test_midi_reacquire_after_deallocate()` in `tests/test_media_replay.c`: a
mock port takes a one-note MIDI song, the test starts it, calls `stop()` and
`deallocate()`, then `realize()` and `start()` again, and asserts the port
was asked for the song twice, the duration survived, the second handle was
started, and no PCM device was claimed.  Before the fix the test fails on the
second open and `start()` reports "MIDI output unavailable" through the PCM
path; full ctest is 122/122 after it.

## Limits

The headless port has no `midi_open`, so it always renders MIDI itself and
cannot reproduce this: the symptom is only visible on a port with a platform
synthesizer (the Android APK; the emulator's API 21 image also ships one).
The phone is the reproduction: 极品飞车 is installed there and the pause /
resume sequence is user-driven - the runtime can drive the title to the race
headlessly but not through its police chase.
