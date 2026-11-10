# Random subclass dispatch

The native `java.util.Random` implementation used its private LCG directly from
its public generation methods. A subclass overriding `protected next(int)` was
therefore ignored by inherited `nextInt`, bounded `nextInt`, `nextLong`,
`nextFloat` and `nextDouble`. The constructors also wrote the LCG state directly
instead of invoking the subclass's `setSeed(long)`.

[CLDC 1.1 Random](https://docs.oracle.com/javame/config/cldc/ref-impl/cldc1.1/jsr139/java/util/Random.html)
specifies these extension points explicitly, including `Random()` delegating to
`Random(System.currentTimeMillis())` and `Random(long)` calling `setSeed(seed)`.
Subclasses may replace the generator while retaining the public algorithms.

The existing `nextBoolean` and `nextGaussian` methods are extensions beyond that
CLDC API. Their dispatch follows the
[Java SE 8 Random contracts](https://docs.oracle.com/javase/8/docs/api/java/util/Random.html):
`nextBoolean` calls `next(1)`, while `nextGaussian` calls the virtual `nextDouble`
and caches the second accepted result. In particular, a subclass overriding
`nextDouble` must affect inherited `nextGaussian` even when it leaves `next`
unchanged.

## Implementation

[nj_lib_util.c](../../src/midp/nj_lib_util.c) keeps a direct LCG path for an exact
`Random` receiver. For a subclass, it resolves the requested method: an inherited
base implementation also uses the direct path, while an override is called
through `nj_call`. The protected base `next` remains the primitive operation;
`super.next` therefore does not redispatch into the same override.

Status-returning helpers sequence callbacks explicitly. A two-draw operation
returns immediately when its first callback fails, and bounded generation keeps
the same rejection algorithm, including the defined unsigned overflow test.
Constructors invoke virtual `setSeed` once and do not overwrite the override's
state afterward. Gaussian generation invokes virtual `nextDouble` and publishes
the cached second result only after an accepted, successful pair.

The `nextDouble` combination uses signed 64-bit multiplication instead of
left-shifting a potentially negative overridden `next` result. This preserves
the Java expression without masking the subclass's return value or introducing
C undefined behavior. The existing signed low-half combination in `nextLong`
also remains intact.

Receiver references stay live through the native frame's argument/local root
range while an override allocates, triggers GC or reenters native code. No new
C global state, Java fields, static slots or builtin member layouts are added.
The existing seed and Gaussian cache remain in the same Java fields, so this
change does not require a snapshot format migration.

## Regression oracle

[RandomDispatch.java](../../tests/java/RandomDispatch.java) compiles to real Java
subclasses. [check_random_dispatch.py](../../tools/check_random_dispatch.py)
runs the same classes first on the desktop JVM, then as a headless MIDlet in the
emulator, and compares ten labeled digests. It checks:

- The bit widths and call order used by all existing generation methods.
- Inherited overrides and a subclass delegating through `super.next`.
- Bounded rejection, power-of-two bounds and invalid-bound validation before a
  callback; signed low-half behavior in `nextLong`.
- Both constructors calling the virtual `setSeed`, including an override that
  throws before construction completes.
- First/second callback exceptions interrupting multi-draw operations.
- Gaussian rejection, virtual `nextDouble`, cached results, reseeding and
  exceptions that must not leave a successful cached result.
- A `nextDouble` override delegating through `super.nextDouble` while the
  protected `next` override remains observable.

For the no-argument constructor, the JVM comparison checks callback count and
exceptions, not the seed value: modern Java SE selects its default seed
differently from CLDC. Explicitly supplied seeds are compared. Gaussian digests
quantize results to one part
in a million because the test targets dispatch and cache behavior rather than
last-bit agreement of host transcendental functions.

On the unmodified `176de3a` runtime, all ten categories disagreed with desktop
Java. The fixture completed with `RANDOM-DISPATCH DONE` and emulator exit 0;
these are behavioral differences, not fixture startup failures.

## Verification

The complete default Release suite passed **158/158 tests** in 298.80 seconds,
including the new native and real-Java dispatch checks. No tests were skipped.

The native [test_random_dispatch.c](../../tests/test_random_dispatch.c) fixture
reports 111 failed checks on the old implementation (340 checks, 229 passed) and
439 passed checks on the fix. The totals differ because successful callback
entry executes additional assertions. This covers native-frame-only GC roots,
reentrant callbacks, inherited defaults, `super` delegation and exception
short-circuiting. The existing bounded-generator oracle still passes all
24,051 checks.

An independent full Debug library and CLI build used `-Wall -Wextra -Werror`,
AddressSanitizer, UndefinedBehaviorSanitizer, explicit float-cast-overflow checks
and leak detection. Seven selected CTests passed, including both Random tests,
general library/util tests, and the general/M3G/RMS snapshot neighbors. The same
sanitized CLI passed all ten Java subclass categories.

A separate snapshot driver created three real base `Random` states: a Gaussian
result waiting in cache, an already consumed pair, and a reseeded generator with
its cache cleared. Old-to-new, new-to-new and both reverse/control reads passed
44 checks each. Sixty rounds of mixed generation preserved the seed, cache flag,
raw cached double bits and result bits in all 303 continuation output lines.
The full registered class/member ABI inventory also remained identical.

The original Minecraft JAR completed a paced 1600-frame SDL replay with
`--fps 30 --heap 16 --size 240x320`, including world loading, selection and saving
at frame 1450. The terrain/held-block/HUD capture matched the previous version.
Both a prior-version snapshot and the new snapshot resumed for 300 headless
frames, with identical sampled RGB and no uncaught exception or chunk error.
The previously documented caught UDP receive loop after restoring network
handles remains outside this fix. These runs validate the sampled scenes and
state continuation; they are not a throughput measurement or whole-game proof.

## Scope

Minecraft's profiled generator uses its own `Gen$Rnd` implementation. This fix
addresses the standard-library extension contract and makes no Minecraft GEN,
LOAD or Android frame-rate improvement claim. Existing base-generator seeded
sequences and the bounded-rejection overflow fix remain requirements.
