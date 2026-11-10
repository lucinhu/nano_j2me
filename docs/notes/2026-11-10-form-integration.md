# Upstream Form integration and verification boundary

The validated callback, Bluetooth and skin-record fixes are committed as
`1f1a6da99e4b8be558ee0b494b98c3f6eb3be055`. Merge
`b184dba01e0ed67180bff6ee631c6c305430690f` also includes upstream `04ed9aa`
and `8689b5b`: Form content scrolling, wrapped text measurement, focus and
pointer coordinates, and optional accelerated fills for built-in screens.
The CMake conflict is resolved by registering both `test_form_scroll` and
`test_display_serially`; Bluetooth and skin-record tests remain registered.

The merged runtime is built independently under
`.local-tools/debug-runs/form-upstream-20261110-main/`. Its CLI SHA-256 is
`5bb6b36f6356c6340aff14bf2905cab8d6b8245455af838be83cb8a80f511893`.
The earlier four JBenchmark attempts use CLI `1733fc81…a985d1` from the
preceding `6a13581` cohort. Their scores and limitations remain tied to that
version in [the benchmark report](2026-11-10-jbenchmark-baseline.md). The new
uninstrumented 3D/HD observations and the separate synchronous 3D diagnosis
are recorded there under their own runtime and protocol. The older native GEN
profile is another explicitly dated cohort.

## Snapshot compatibility

Upstream adds the private `Screen.fmScroll` field between `fmIndex` and the
listener/buffer references. The field count and reference offsets change.
Existing strict class-layout validation must reject older whole-VM snapshots;
relaxing its hash or field-count checks would interpret incompatible objects.
This is a whole-VM snapshot boundary, separate from the game's own RMS and
world files. Original game JARs, RMS and world inputs remain unchanged.
The earlier Bluetooth report's successful original-snapshot resumes describe
the preceding runtime, before this upstream layout change.

The dedicated fixture passes a nonzero-scroll roundtrip between two VMs with
the same Java listener class bytes. It checks the Form title, item array and
owners, listener and its last-item reference, Image pixels and the cached Form
buffer after releasing host roots and collecting garbage. Painting, pointer
selection and Up-key navigation also pass after load. The 2,119,650-byte state
contains arena bytes and metadata; the format has no one-MiB file limit.
Both VMs attach a caller-owned class source, as required by the existing loader.
A separate 585-check invocation confirms that the original older snapshot is
rejected with `NJ_ERR_FORMAT` before arena installation, preserves the current
VM graph, and allows a fresh MIDlet startup. The original JAR and state remain
byte-for-byte unchanged.

## Merged-runtime validation

The combined normal result is **168 registered tests passing**. The first 167
are verified as follows: Save runs first, the other 165 run together, and the
parallel equivalence test runs once
with the already-reviewed anonymous-file adapter. That adapter redirects only
`tmpfile`/`tmpfile64` to a private filesystem to avoid the established `/tmp`
quota; renderer, writes and comparisons remain unchanged. The new parallel
ELF passes 234 forks and compares 117,504,000 color/depth bytes.

The original Minecraft JAR completes the same 1600-frame paced SDL replay and
restores its newly written snapshot for 300 headless frames. The actual SDL
image matches the previous terrain/HUD/held-block reference byte-for-byte.
Neither run has an uncaught exception or resume fallback. The restored network
connection still produces caught UDP receive exceptions, a pre-existing native
handle limitation. Paced 30-FPS runs establish scene behavior, not a speed gain.

Eight affected tests pass the strict AddressSanitizer, UndefinedBehaviorSanitizer,
float-cast-overflow and LeakSanitizer checks: Save, Form scrolling, LCDUI,
Display callback delivery, Game API, Bluetooth property dispatch, VM lifecycle
and application lifecycle. They use warning-as-error builds and preserve the
original Java pixel thresholds. This is targeted sanitizer coverage, not a
claim that the full registered suite ran under sanitizers. The additional
snapshot fixture also passes these strict checks separately: 752 roundtrip
checks and 585 old-state refusal/fresh-start checks. This adds one registered
test to the previously completed 167; the unchanged 167 were not rerun. The
original failed fixture variants and their logs remain separate evidence.

The snapshot fixture initially exposed an existing ImageItem compatibility
defect: both constructors reject a null `altText`, although the
[MIDP specification](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/lcdui/ImageItem.html)
explicitly permits it. Its null check in `setAltText` has the same issue. The
fixed benchmark cohort retains that implementation; no interface repair is
claimed here. The focused snapshot fixture supplies explicit alternate text
and invokes its real Java listener synchronously to test the saved reference
graph without assuming immediate `Item.notifyStateChanged` event delivery.
The original failing source and log remain available locally for the separate
interface repair.

Source, logs, manifests and screenshots remain under the private experiment
directories; no commercial JAR or user state is included in the commit.
