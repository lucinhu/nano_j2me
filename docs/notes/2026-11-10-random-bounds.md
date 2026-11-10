# Random.nextInt(bound): defined rejection arithmetic

The bounded random-number implementation translated Java's wrapping `int`
addition directly into signed C arithmetic. This change makes that arithmetic
well-defined while preserving Java's seeded sequence and rejection decisions.

## Defect and minimal fix

The [CLDC 1.1 Random contract](https://docs.oracle.com/javame/config/cldc/ref-impl/cldc1.1/jsr139/java/util/Random.html)
specifies the 48-bit LCG, the power-of-two mapping and the rejection algorithm.
In [rnd_nextIntN](../../src/midp/nj_lib_util.c#L1665), the original condition was:

```c
bits - val + (n - 1) < 0
```

For positive `n`, both `bits - val` and `n - 1` are individually safe, but their
sum can exceed `INT32_MAX`. Java wraps that sum; signed C overflow is undefined.
With seed `0` and bound `1073741825`, the old sanitizer build reports:

```text
util-before.c:1682:25: runtime error: signed integer overflow: 1073741825 + 1073741824 cannot be represented in type 'int'
```

The replacement converts operands before the arithmetic and tests the sign bit:

```c
(nj_u4)bits - (nj_u4)val + (nj_u4)(n - 1) >= 0x80000000u
```

After rejecting nonpositive bounds, `0 <= val <= bits <= INT32_MAX` and
`0 <= n - 1 <= INT32_MAX - 1`. The mathematical sum lies in `[0, 2^32 - 3]`.
Unsigned arithmetic followed by the comparison therefore reproduces Java's
negative-result test without signed overflow or an unsigned-to-signed cast.
The power-of-two branch, LCG, seed storage, public signatures, builtin tables
and snapshot layout remain identical.

The old ordinary GCC 16.2.1 `-O3` build already passes all 24,051 value checks.
The reproduced failure is undefined C arithmetic under sanitizer; these results
do not demonstrate an incorrect seeded value in that ordinary build.

## Independent regression oracle

[test_random_bounds.c](../../tests/test_random_bounds.c) calls the public native
method interface and never reads Random's seed fields. Its 64-bit reference LCG
uses a separate acceptance criterion:

```text
M = 2^31
limit = floor(M / n) * n
accept bits if bits < limit
```

Writing `bits = q*n + r`, the original mathematical sum is `(q+1)*n - 1`.
Its comparison with `M` yields exactly the acceptance interval above. The
power-of-two case retains the specified high-product mapping; replacing it
with modulo would change the sequence.

The fixture covers eight seeds and 22 bounds, including `2^30 - 1`, `2^30`,
`2^30 + 1`, `2^30 + 2`, `INT32_MAX - 1` and `INT32_MAX`. Each seed/bound restarts
the generator, takes 64 bounded values and interleaves an unbounded call every
eighth value. Both the independent model and runtime digests match eight
constants produced separately by OpenJDK 27.0.1.0.3.

Directed controls check rejection and subsequent state consumption:

| Seed | Bound | Rejections | Bounded result | Following nextInt() |
|---:|---:|---:|---:|---:|
| 0 | 1073741825 | 2 | 516548029 | -1690734402 |
| 186643064605892 | 2147483647 | 1 | 554899859 | -1753186469 |

The second seed forces the first 31-bit candidate to equal `INT32_MAX`.
Additional checks cover `setSeed`, full 64-bit seed inputs, and bounds `0`, `-1`
and `INT32_MIN` throwing `IllegalArgumentException` without advancing the state.

## Validation

| Build or check | Result |
|---|---|
| Original ordinary `-O3` | 24,051 passed; 0 failed |
| Original sanitizer, directed rejection | Signed-overflow diagnostic above; program exit 1 |
| Fixed ordinary `-O3` | 24,051 passed; 0 failed |
| Fixed focused ASan + UBSan + float-cast-overflow | 24,051 passed; 0 failed; no sanitizer diagnostics |
| Independently rebuilt full Debug library, `-Werror` and sanitizers | 6/6 neighboring tests passed; no sanitizer or leak diagnostics |
| Main Release suite | 156/156 passed in 300.36 seconds |
| Original Minecraft SDL replay | 1,600 paced frames; verified terrain, held block and HUD |
| Prior-version and newly saved Minecraft snapshots | Both resumed for 300 frames; sampled RGB output identical |

The independent build recompiles all 76 runtime translation units and six test
units with `-Wall -Wextra -Werror`, AddressSanitizer, UndefinedBehaviorSanitizer
and explicit `float-cast-overflow`. It enables leak detection and halts on
sanitizer errors. Alongside the new fixture it runs `test_lib` (1,432 checks),
`test_util`, `test_save` (2,521), `test_save_m3g_depth` (95) and
`test_save_rms_enumeration` (70). Source hashes match before and after the run.

The original Minecraft JAR and source worlds remained unchanged. The SDL run
used isolated filesystem/RMS copies, heap 16 MiB, size 240x320 and a 30 FPS cap;
its 960x1280 window capture matched the prior baseline byte for byte. Both
snapshot runs sampled the same 240x320 RGB scene after 200 frames. These are
bounded scene and snapshot checks. The pre-existing caught UDP receive loop
after restoring host network handles remains; this change does not repair
network-state restoration. The paced replay is not a throughput benchmark.

The public regression can be run from the repository root:

```sh
cmake --build build -j4
ctest --test-dir build --output-on-failure -R '^test_random_bounds$'
```

This batch establishes arithmetic and seeded-sequence compatibility. The
Minecraft generation hotspot uses the game's own `Gen$Rnd`, so the Random fix
has no measured Minecraft GEN, LOAD or FPS benefit. A separate existing gap is
that Random's native methods call their LCG helper directly and bypass a
subclass override of `protected next(int)`; this batch leaves that dispatch
behavior for a dedicated repair.
