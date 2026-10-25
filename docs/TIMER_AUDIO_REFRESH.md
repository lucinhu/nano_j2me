# Timer scheduling and streamed audio regression checks

The refresh starts at `d48046c` and also incorporates upstream `57bd259`.
Numerical samples are in [the results archive](timer-audio-refresh-results.json).
Portable fixes are `1020740` (CustomItem argument storage), `1ddbdae`
(streamed MIDI backpressure), `4f621e8` (timer fairness), and `afb9005`
(integration with upstream loop-count handling).

## Timer callbacks must return control to the frame loop

The new synchronous repaint path exposed a scheduling problem in NFS The Run.
A periodic TimerTask updates the game and calls repaint. When the callback
takes longer than its period, its next invocation is immediately due. Many
native paint calls can run within one bytecode quantum, delaying input and
the present at the end of the frame. On the device, the refreshed baseline
painted 4,024 times but presented only 25 frames in roughly 74 seconds. The
startup navigation timed out. The host runner also exceeded a 30-second
limit before reaching 120 frames.

The helper now yields after each completed callback using the existing
zero-duration thread sleep. It becomes runnable at the next scheduling pass,
without adding a timed delay. Synchronous repaint remains available, including
multiple paint requests within one callback; suppressing those requests would
break callbacks that wait for paint to publish a result.

`test_timer_fairness` uses a fake clock and a one-millisecond periodic task
that performs two five-millisecond paints. Twenty scheduler passes must call
the task twenty times and complete all forty paints. The old implementation
fails 22 assertions; the fix passes under ASan/UBSan. Existing timer lifecycle,
thread lifecycle and LCDUI tests also pass. The host NFS run now completes its
120-frame budget normally.

## Keep unaccepted MIDI samples

The internal renderer previously advanced when it produced a PCM block even
if the port accepted only part of it. The suffix was discarded and media time
advanced beyond accepted audio. The stream holder now retains its pending
frame count and offset. Zero, short, negative and invalid oversized write
results preserve the unaccepted samples for a later attempt.

The renderer stays alive through end-of-media and is released on close or
deallocate. Seeking clears pending samples. Looping rewinds the retained
renderer rather than allocating another one or retaining a second copy of
the input file. This also reconciles the upstream loop-count change with
the retained stream state.

`test_media_backpressure` compares every output frame against a complete
reference render. It covers mono/stereo, irregular acceptance, zero/error
writes, stop/start, seek with pending output, seek after end-of-media, and
three-pass playback followed by replay of the same Player. The partial-write
test fails on the original implementation and passes under ASan/UBSan after
the fix. The test has no JDK or commercial-game dependency.

## CustomItem memory safety

CustomItem's native draw bridge passed Graphics, width and height through a
two-element argument array. ASan reproduced a stack overflow in the existing
LCDUI test. The array now has three elements; the test also checks the paint
dimensions against the sizeChanged callback.

## Device measurements

Measurements use a 240x320 game surface, a 10 MiB Java heap, fresh temporary
RMS, and changed-image FPS. They do not count duplicate frame-loop presents
as new game pictures. Screenshots are outside sampling intervals. Normal
games and saved data are retained.
The device integration retains its existing drawRGB low-memory fallback,
short-PCM completion fix, and internal raster placement. Those are held
constant through the comparisons and are not new portable changes here.

The port's audio queue watermark was increased from 80 to 160 milliseconds,
with the completion grace adjusted to include the buffered tail. The physical
queue allocation is unchanged. This is a port change, separate from the
portable MIDI renderer fix. The music in Bounce Tales uses the port renderer;
its improvement must not be attributed to the internal-renderer patch.

Two Bounce Tales runs per configuration use the same navigation and repeated
left/right input, five-second warm-up, then twenty one-second FPS polls:

| Configuration | Mean FPS, run 1 / run 2 | Underruns, run 1 / run 2 | Inserted silent output frames |
| --- | ---: | ---: | ---: |
| Refreshed baseline, 80 ms watermark | 11.105 / 11.390 | 8 / 10 | 4600 / 6429 |
| 160 ms watermark plus timer fix | 10.980 / 10.485 | 0 / 0 | 0 / 0 |

These short samples establish an audio-buffering improvement, not a 2D FPS
improvement. The two-run FPS mean is about 4.6% lower, and the buffering adds
about 80 milliseconds of queued latency. All offered PCM frames were accepted
and output errors stayed at zero during sampling. Startup and shutdown are
outside this interval and can still contain silence or cancelled output.
A two-millisecond consumer wait was also tried; it left nine underruns in
each run and was discarded.

After the timer fix, NFS The Run reaches the race at 5.320 FPS with JIT off
in the near-start brake-held scene. This is close to the previous integration's
5.355 FPS, and is recovery from the refresh regression rather than a new
rendering speedup. The changes do not alter generated JIT code.

Hot Pursuit reached active driving at 8.020 FPS in one ten-poll run. Replaying
the same navigation against the previous firmware measured 4.160 FPS, but the
post-sample pictures showed different modes and tracks. Those numbers are
functional spot checks, not a valid speedup comparison. A previous 3.930 FPS
run also used another scene. No Hot Pursuit improvement percentage is claimed.

After integrating `57bd259`, thirteen selected host suites pass under
ASan/UBSan: lifecycle, M3G, M3G targets, Mascot, wireless, timer, timer fairness,
thread lifecycle, LCDUI, library, Nokia Sound, MIDI, and MIDI backpressure.
The port media suite also passes, including native MIDI partial writes,
completion, stop/seek/loop and shutdown. Its component substitutes do not
replace physical audio verification.

The final `afb9005` integration reaches NFS The Run at **4.990 FPS with JIT
on** in the same brake-held setup, and Hot Pursuit at **7.070 FPS with JIT
off** in active driving. Both exit normally with full external-memory recovery
and no live hot or executable allocations. The earlier 5.320 result used the
pre-merge image and JIT off, so it is not a strict same-image JIT comparison.

The final Bounce Tales run, with JIT on, measures **11.630 FPS**, with all
478,794 offered PCM frames accepted and zero underruns, inserted silent frames
or output errors during sampling. This remains a short gameplay sample;
the results do not establish a repeatable 2D FPS gain.

## Final fixed-work checks

All checks below use the same final firmware, 240x320 and the FPS overlay off.
Elapsed times include the runner, initialization, rendering and teardown;
they are not pure generated-code timings. Each case exits normally with
complete external-memory recovery.

| Check | Result |
| --- | --- |
| CPU, JIT off | 120 frames, work=4000, checksum=-1082513742; 9.533 s |
| CPU, JIT on | Same work and checksum; 26.367 s |
| Drawing, JIT on | 240 of 240 redraws completed; 7.388 s |
| WAV | Media time reaches 3,000,000 us; final state=300 |
| Native MIDI | Media time reaches 3,000,000 us; final state=300 |
| Short PCM | Final state=300; exactly one end-of-media event |

The CPU workload is slower with JIT enabled. This refresh does not fix that
cost, and no JIT speedup is claimed. That run compiled eight blocks, executed
24,817 blocks covering 11,264,550 bytecodes, and recorded zero evictions,
recompilations or allocation failures. Cache churn does not explain this
particular result; generated-code and helper costs need separate profiling.
The device is restored to the normal
menu with JIT off, a 240x320 game surface, FPS overlay enabled and volume 70.
All 25 existing game entries remain available.
