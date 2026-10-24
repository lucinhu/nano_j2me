# JIT probing under executable-memory pressure

The JIT now backs off new compilation after executable allocation fails, while
continuing to execute retained blocks. On two tested 240×320 game scenes this
substantially improves the previous JIT's effective image update rate. It does
not establish a general advantage over the interpreter, or a 3D speedup.

The implementation is in `8965245`. The isolated comparison starts at
`0ea577a`; both images share graphics, audio, frontend objects, private integration
fixes, flags and memory budgets. The published branch also incorporates the
subsequent graphics/M3G changes through `11c2fc4`. Those changes are excluded
from the isolated JIT numbers below. Final merged firmware is checked separately.

After an allocation failure, 1,024 subsequent probe/frame-hint opportunities
use the bounded code cache directly instead of recycling method maps and
repeatedly emitting code that cannot be allocated. Retained entries still pass
stack and scheduling-budget guards. Frame refreshes advance the backoff even
when a method has no retained code, so discovery can resume after memory recovers.
The site that actually failed retains its existing refusal behaviour; other
sites deferred during backoff are not permanently refused.

A conservative 32-bit PC mask in each of 256 method-hash buckets filters
interpreter probes during backoff. Collisions may admit unnecessary probes, but
cannot discard retained native entries. Replacing or refusing an entry rebuilds
its bucket so another entry sharing the same bit remains discoverable. The
interpreter refreshes hints at frame changes, quantum resume and unsuccessful
execution; native exits still probe continuation sites directly. Methods too
large for the existing arena skip futile probing. A compact opcode flag table
preserves the old candidate/boundary predicates for all 256 values.

Recycled maps reconnect existing code on their first probe. Zero-completed
native guard faults remain refused even when the executing ticket had no map
cell. Probe/hint operations remain callback-free; the interpreter publishes
its exact frame before compilation, code-memory callbacks or native execution.
No emitter, native ABI, stack capacity, code budget or default JIT setting changes.
The measured fixed per-VM metadata grows from 11,480 to 12,568 bytes.

Measurements use actual changed game images divided by the monotonic elapsed
sampling time. Image comparison occurs before the FPS overlay; repeated
identical presentations and VM loop iterations are not counted as frames.
Each run starts with fresh temporary RMS, enabled audio at the same volume and
an identical navigation/input script. Raw game images before and after the
sampling window establish gameplay. There are no screenshots during sampling;
runs requiring a screenshot-transfer retry are rejected because the extra pause
changes scene timing. Results are in [jit-game-cache-results.json](jit-game-cache-results.json).

| Game | Previous JIT FPS (range) | Patched JIT FPS (range) | Gain | Interpreter controls, old/new |
| --- | ---: | ---: | ---: | ---: |
| Bounce Tales 2.0.3 | 11.81 (11.60–12.15) | 16.31 (16.15–16.42) | 38.1% | 17.33 / 17.46 |
| ACH1945 Lite 1.0.0 | 10.24 (9.96–10.67) | 12.86 (12.79–12.98) | 25.6% | 13.42 / 13.87 |

During the sampling windows, failed executable allocations fall from about
1,841 to 47 per second in Bounce and 900 to 10 in 1945. Method-map evictions
fall from about 1,549 to 215 and 1,781 to 97 per second respectively. Both
games still run somewhat faster with JIT disabled in these controls.

Each JIT result pools three independent windows; ranges expose run-to-run
variation. Each image also has one interpreter control. The ordering is two
baseline runs and one control per game, then the same for the candidate,
followed by one more baseline and candidate run per game. Games advance with
wall time, so these are matched interactive scenes rather than identical frame
replays. Checksums and exact-work fixtures, not game scene CRCs, validate execution
correctness. Final scene CRCs may differ with timing.

Bounce Tales 2.0.3 uses the first level, with 5 seconds of warmup and 30 one-second
samples. ACH1945 Lite 1.0.0 uses level one with automatic firing, with 3 seconds
of warmup and 20 samples. Both repeatedly alternate numeric 6 and 4, with a
nominal 0.5-second pulse period. Navigation is:

- Bounce: wait 4 s; fire, 5, fire, left softkey, fire; wait 30 s; 5, fire,
  fire, fire; settle 5 s.
- 1945: wait 30 s; fire; wait 5 s; fire, left softkey, fire; settle 8 s.

Each navigation press lasts 0.4 s followed by a 0.6 s release. Actual sampling
duration and delivered input pulse counts are recorded. JAR digests identify
the tested releases; no game binaries are distributed. Ordinary JIT statistics
are enabled, detailed statistics disabled, and executable memory is capped at
32 KiB. Interpreter and JIT runtime placement is checked in both images; builds
with inconsistent linker placement are excluded.

Validation covers RV32 and ARM64 runtime execution, eight Java kernels in
interpreter/probe-only/execute modes (24 checksum comparisons on each
architecture), and eleven RV32 configurations: defaults (switch dispatch), threaded dispatch,
superinstructions disabled, JIT disabled, tiny cache, partial arena, large cache,
statistics disabled, detailed statistics, and minimum block lengths 1 and 16.
Tests cover backoff recovery, callback-free probes, unchanged frames, retained
code guards, persistent fault refusal, colliding mask bits, cache replacement,
map recycling and opcode-filter compatibility. Tiny configurations explicitly
skip fixtures requiring multiple retained entries. Enabled JIT/interpreter
objects have no writable static state.

ASan/UBSan checks for the JIT, both emitters, LCDUI, M3G and graphics pass, including
the merged upstream changes. A broader 70-test run has two unresolved baseline
issues after fixing test-environment paths: `test_string_intern` leaks its
11,088-byte headless port allocation, and `check_api_audit` exceeds its 120-second
timeout. Both reproduce on a separate unmodified baseline. The other 68 tests
pass, including three input tests rerun with the expected CLI path. This is not
a claim that the complete sanitizer suite is clean.

Some 1945 sampling windows execute no native bytecodes despite JIT being enabled
and used during startup. Their gain is reduced JIT overhead, not faster native
game logic. The per-window native-bytecode deltas make this distinction visible.

The fixed-work CPU/drawing fixture retains checksum `-1082513742` and identical
final pixels. Its single before/after JIT runs take 25.880/25.867 seconds, versus
9.016/9.176 seconds with JIT disabled. This patch does not improve that workload;
the cost of native execution and fallback outside allocation pressure still
needs separate investigation. The rendering fixture completes all 240 redraws.
The audio fixture completes both 3-second streams with media time 3,000,000 and
state 300; streamed MIDI initially reports an unknown duration (-1).

All recorded gameplay runs exit normally, reclaim VM/port/executable memory,
and report no JIT faults or scaler mismatches. Audio output errors and queue
underruns are recorded separately; no audio-quality or audio-performance gain is
claimed. These scenes still contain substantial interpreted work, and the
interpreter controls remain important when choosing JIT for a title.

After merging the newer upstream graphics/M3G changes, one additional JIT-on
window per game measures 17.34 FPS in Bounce Tales 2.0.3, 13.00 FPS in ACH1945 Lite 1.0.0.
These are final-integration checks, not extra samples for the isolated JIT
comparison. Both exit cleanly with all memory reclaimed and no JIT faults.

The normal production build also passes the exact-work CPU, full-redraw and
audio fixtures, plus a final Bounce gameplay check with JIT enabled. Its image
digest is recorded separately. This shorter functional check uses no diagnostic
frame-counter wrapper and is excluded from the pooled FPS results.
