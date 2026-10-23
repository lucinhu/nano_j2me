# Resolved-field cache lookup and JIT eviction accounting

This branch is based on `36fefd2`. It changes cache correctness and accounting;
the generated-code ABI, executable-memory callbacks and cache capacities stay
compatible with that revision.

The constant-pool resolver uses open addressing, but the JIT previously checked
only the first bucket. An already resolved field displaced by a collision was
therefore treated as unresolved. A block could stop before that field, or be
refused and remain interpreted until its method map was recycled. The read-only
lookup now follows the resolver's linear probe chain, including wraparound. It
stops at the first unused bucket or after one complete traversal of a full
table. It does not resolve a constant, initialise a class, allocate, call a port
callback or change cache-hit counters. Both instance and static field lowering
use the corrected lookup.

The method-map eviction counter also missed replacements caused by a full
method table when the cell arena still had space. Counting each metadata
release in one place covers both kinds of pressure without double-counting
arena releases. `recompiled_blocks` is documented as reuse of surviving native
code after map recycling, rather than a count of newly emitted replacements.
No public field or function was renamed.

The regression tests cover every start bucket, displaced instance and static
fields, chains wrapping around the table, first-unused-bucket termination,
full-table misses, and unchanged cache contents and hit counters. A separate
probe-only test exhausts method slots while leaving room in the arena. Each
regression failed before its corresponding fix and passes afterwards.

Validation includes the host JIT tests with warnings treated as errors, the
same tests with address and undefined-behaviour sanitizers, and native execution
under both RV32 and AArch64 Linux user-mode emulation. Eight fixed Java kernels
match independent unsigned C oracles in interpreter, probe-only and execute
modes; executable allocations are released. The sanitized build does not treat
an existing, unrelated socket warning as an error. Emulation timings are not
used to claim device speedups.

Target measurements use an anonymous embedded target, 240×320, volume 40,
superinstructions and threaded dispatch disabled, ordinary JIT statistics on
and detailed statistics off. Each run uses fresh temporary RMS. Only the
application is flashed. The adapter is built from the same baseline snapshot
for the comparison, keeping unrelated concurrent frontend changes out of it.

The two fresh-VM CPU rounds average **13.275625 s before / 2.915046 s after**
with JIT execution enabled: 78.0% less end-to-end time (4.55× throughput for
this fixed workload). The patched interpreter averages 8.675190 s, so execute
mode takes about one third of that duration. Native bytecode coverage in the
first CPU round rises from 2.12% to 98.92%. Both versions report the independent
checksum `-1082513742` and identical final native/LCD pixel CRCs. This is an
end-to-end fixture, including display and lifecycle costs, not pure CPU time.

| Fixed work, JIT enabled | Base seconds | Patched seconds | Scope |
| --- | ---: | ---: | --- |
| CPU, 120 × 4000 | 13.2756 | 2.9150 | Two fresh-VM rounds averaged |
| 240 complete redraws | 5.4551 | 5.4561 | Two rounds averaged |
| WAV + MIDI | 10.5129 | 10.8853 | One round per version |
| Racing title 14, 300 VM startup frames | 7.4724 | 7.4582 | One round per version |
| Racing title 16, 300 VM startup frames | 13.1294 | 13.6601 | One round per version |

All seven fixed-work runs retain their final native/LCD CRCs across the patch.
The audio fixture completes the same WAV and MIDI durations in both versions
with no driver errors or underruns. There is no listening evaluation. The small
single-run timing changes above do not establish stable regressions or gains.
Startup VM frames are not gameplay FPS.

The original five-kernel target JAR is deliberately unchanged; its SHA-256 is
in the result archive. All five checksums match. Locals/constants/fields/arrays
remain approximately 12/6/12/21 ms in execute mode; the branch-heavy kernel is
still slow on this target (about 1681–1682 ms, versus approximately 206.0 ms
interpreted). The patch does not resolve that pre-existing native-loop timing.
The separate mixed target fixture remains about 33 ms. Emulator timing does not
predict this target behaviour.

Gameplay was also sampled after navigation and five seconds of warmup, with
no screenshots during the sampling window. Before/after screenshots confirm a
running level or race. The bouncing-ball scene uses alternating 6/4 pulses and
40 FPS readouts, about 25–26 seconds; the motorcycle scene holds UP and uses
20 one-second readouts. These are means of the existing half-second FPS window
readouts, not a cumulative frame-counter measurement.

| Scene | Base JIT mean | Patched JIT mean | Patched interpreter mean |
| --- | ---: | ---: | ---: |
| Bouncing-ball first area | 12.95 | 11.33 | 17.95 |
| Motorcycle city race | 3.74 | 3.75 | Not sampled in this round |

Input rules match, but positions and camera progression are not synchronized;
the bouncing-ball end screenshots show different progress. These samples do
**not** establish a stable causal gameplay FPS speedup or regression. An early
bouncing-ball attempt still at loading/dialogue is excluded. Every retained
sample exits normally and restores host and executable memory; scaler checks
pass. A completed startup or short playable sample does not prove full-game
compatibility.

An additional recency-based code-cache replacement experiment reduced code
evictions but did not establish a gameplay benefit: bouncing-ball mean 11.17
and motorcycle mean 3.69 FPS. It was discarded. Cache capacities and the original
replacement policy are retained. Baseline method-map eviction totals omit table
pressure, so they must not be used as a directly comparable before/after metric.

A final rebuild restores the selected source after the discarded experiment;
its image hash is identical to the measured patched image. The application
write passes its hash check, and all twelve JIT test invocations pass again.
A further post-write runtime check was not run because a separate test session
held the target exclusively. Current device settings are not asserted here.

The intended runtime setting remains JIT off by default, with per-application
opt-in. The field-cache fix is useful for field-heavy loops; it is not evidence
that enabling JIT accelerates every game. Raw numbers, checksums and the rejected
experiment are archived in [jit-cache-lookup-results.json](jit-cache-lookup-results.json).

To reproduce the portable checks, enable JIT and disable superinstructions in a
separate build, generate the Java fixture, and use the existing native runner:

```sh
cmake -S . -B build-jit -DNJ_ENABLE_JIT=1 -DNJ_ENABLE_SUPERINSTR=0
cmake --build build-jit --target test_jit test_jit_rv32 test_jit_arm64 test_jit_perf
ctest --test-dir build-jit -R '^test_jit(_rv32|_arm64)?$' --output-on-failure
tools/check_jit_perf.sh --build-fixture-only
tools/check_jit_perf.sh --build-dir build-jit --require-execution
```

Native execution requires the corresponding CPU or a configured cross compiler
and emulator. Unsupported hosts cover encoding/fallback checks only; the
`--require-execution` option prevents treating such a skip as a native pass.
