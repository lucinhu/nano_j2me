# Display callbacks and repaint scheduling

The original JBenchmark2 completed its five worker stages on commit
`2a633ba9439efcd429b658d0485fbc9268372305`, but never displayed its final scores.
A separate diagnostic runner observed both worker and results threads finished,
the results Form current, and that same Form's Runnable still queued in
`Display.callSerially`. This was a UI delivery failure, rather than a worker
join deadlock. Diagnostic counters are not benchmark scores.

The queue used to drain only at the beginning of automatic paint. GameCanvas
flushes and the adaptive paint gate could prevent that paint, leaving callbacks
waiting indefinitely. Independently, the adaptive idle counter saturates at 255,
while its old gate required idle strictly greater than the stored paint period;
a stored period of 255 therefore had no recovery. The diagnostic did not sample
the period, so it does not establish that the real benchmark reached that value.

## Event ordering

The [MIDP Display contract](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/lcdui/Display.html)
requires serialized, asynchronous callback delivery. Each enqueue executes once,
in FIFO order, after any Canvas repaint already pending at the call has completed
and its buffers have switched.

Each frame now snapshots up to 64 eligible callbacks after input, scheduling and
background services, before painting. It dispatches that prefix after presenting
the frame, including frames with no paint. Entries created by paint, present or
a callback remain for the next frame. Snapshotting before paint matters: a paint
method can request another repaint and enqueue a new callback, which must wait
for that next repaint. Conversely, a self-repainting Canvas must not indefinitely
block an older, already eligible callback.

Dispatch holds the existing paint guard across the batch, preventing nested
frames from recursively delivering callbacks or painting over the active event.
The queue and removed Runnable have checked GC roots; root registration failure
leaves the unremoved queue prefix intact. Root depth and the guard are restored
on all exits. Callback exceptions retain the previous isolation policy: stop the
batch, clear the callback exception, and leave later entries for a future frame.
Newly allocated callback queues are rooted across their constructor as well.

## Repaint requests and GameCanvas flushes

A GameCanvas flush already copies pixels and the frame presents them
unconditionally. It no longer manufactures a Canvas repaint request, which could
otherwise overwrite that buffer. It preserves an existing explicit request.
Fresh repaint requests take precedence over adaptive cadence and an earlier
paint or flush in the same frame. Paint consumes the request before entering
Java, so a new request from paint or a nested frame survives for the next cycle.

The existing serviceRepaints paint quota also needs to remain effective. A worker
can request a second synchronous repaint in the same host frame, have that
request coalesced by the quota, then clear its transient paint state or draw using
a cached Graphics. Treating this deferred request as fresh painted over those
pixels at the end of the frame. The pending byte now distinguishes no request,
a fresh request, and a request coalesced by serviceRepaints. A coalesced request
waits if the frame already painted, unless an eligible callSerially callback
requires its repaint to finish before callback delivery. A later fresh repaint
always restores fresh-request priority. Requests made inside paint are not
mistakenly downgraded when serviceRepaints returns.

This retains the existing service quota; it does not claim a complete redesign
of blocking serviceRepaints behavior. Both original Java pixel regressions keep
their original thresholds. The save codec already writes and restores the entire
pending byte, so the third value needs no new field or format revision.

The idle-at-255 recovery also handles states saved with the old maximum period.
Changing to a different Displayable resets the inherited adaptive paint history;
setting the same Displayable does not. Deferred Canvas showNotify remains in
place. No VM fields, builtin member layouts or queue representation were added.

## Validation

The independent frame-level fixture covers duplicate FIFO entries, callbacks
that requeue themselves, the 64-entry limit, nested frames, callback exceptions,
GC during a callback, pending-paint/present/run ordering, and a callback that
switches a GameCanvas to a Form after naturally reaching idle period 255. It also
checks repaint requests created inside paint, and a second explicit request in
the same frame after a synchronous serviceRepaints. The flush-only case consumes
both initial display and sizeChanged repaint requests through real frames before
asserting that flushes alone cause no additional paint.

The original nine-group fixture reports 961 checks and 19 failures against the
old `2a633ba` library with its original headers, and 973 checks with zero failures
against the initial correction. Independent strict sanitizer builds on the
`27432df` and `6a13581` baselines pass that initial fixture too. The final fixture
preserves those nine groups and adds three phases for coalesced service requests:
retaining cached pixels, completing the deferred paint on a quiet frame, and
honoring both eligible callbacks and fresh requests. It passes 1050 checks in
the normal build. The original Java regressions retain 33600 red pixels in
RepaintDemo and 1600 red plus 64 blue pixels in SavedGfx, with unchanged pass
thresholds. These pixel counts are regression evidence, not speed measurements.

The GC case also has the normal native-call receiver frame as a root; it is not
independent proof that removing the new explicit receiver root alone causes an
old use-after-free.

The final runtime includes the upstream `27432df` parallel hook and `6a13581`
optional opaque-fill/C-port extension. Of 166 tests, 165 pass in the first full
run. The remaining parallel-render test encounters `EDQUOT` while writing its
anonymous capture files on `/tmp`. The same unchanged test ELF passes with
`tmpfile` redirected to a private filesystem: 234 forks and 117504000 color/depth
bytes retain the original comparisons. This is combined verification, rather
than a clean single-run 166/166 result; both failures and the controlled recheck
are retained.

The final CLI also passes a 1600-frame original Minecraft SDL scene and genuinely
resumes old and new states for 300 frames each. No uncaught exception or cold-start
fallback occurs, and the sampled images match their prior references. Caught UDP
receive errors after snapshot restoration are a preexisting native-handle limit
and remain unresolved. Paced scene FPS is not evidence of a speed improvement.
The [benchmark report](2026-11-10-jbenchmark-baseline.md) distinguishes versions,
settings, final scores and unsupported subtests.
