# Java Math edge cases

The native `java.lang.Math` implementation mishandled several defined numeric
edges. This change fixes existing methods; it adds no Java API declarations,
fields, persistent state or interpreter special cases.

## Contracts and changes

The [CLDC 1.1 Math contract](https://docs.oracle.com/javame/config/cldc/ref-impl/cldc1.1/jsr139/java/lang/Math.html)
requires integer `abs(MIN_VALUE)` to return the same negative value, floating
`abs(-0)` to return positive zero, and floating `min/max` to propagate either NaN
and order negative zero below positive zero.

- Integer and long `abs` now exclude `MIN_VALUE` before signed negation. The old
  implementation relied on C signed overflow even when its ordinary output
  happened to match Java.
- Floating `abs` clears the IEEE sign bit using `memcpy`. This handles both
  zeros and infinities, keeps NaNs in the NaN category, and adds no `fabs`/`fabsf`
  requirement to the freestanding port's minimal libm subset.
- Floating `min/max` explicitly handle NaNs and both zero signs before ordinary
  comparisons. A first-argument NaN was previously discarded, and the selected
  zero sign depended on argument order. C `fmin/fmax` are unsuitable here because
  they may return the non-NaN argument.

`round` is an existing Java SE compatibility extension in this emulator; it is
not part of the linked CLDC 1.1 Math API. Its
[Java SE contract](https://docs.oracle.com/en/java/javase/17/docs/api/java.base/java/lang/Math.html#round(double))
requires nearest integer, ties toward positive infinity, zero for NaN and
saturation at the integer limits.

The old double implementation used `floor(x + 0.5)`. The addition itself can
round a value just below a half-integer up to the tie, or change an already
integral odd double near `2^52`. It also compared against a double literal for
`LONG_MAX`, which rounds to `2^63`; equality then reached an out-of-range C cast.
The replacement checks the exact `+/-2^63` thresholds before any cast, then uses
`floor(x)` and its fractional remainder to decide whether to add one.

Examples on the tested host:

| Call | Old result | Required result |
| --- | --- | --- |
| `abs(-0.0)` | negative zero | positive zero |
| `min(+0.0, -0.0)` | positive zero | negative zero |
| `max(NaN, 1.5)` | `1.5` | NaN |
| `round(nextDown(0.5))` | `1` | `0` |
| `round(4503599627370497.0)` | `4503599627370498` | `4503599627370497` |
| `round(9223372036854775808.0)` | out-of-range cast; host returned `LONG_MIN` | `LONG_MAX` |

The float `round` path already promotes its input to double before adding the
half; it is unchanged and covered by the new regression. All tested methods
remain outside the optional transcendental guard when `NJ_ENABLE_FLOAT=0`.

## Verification

[test_math_edges.c](../../tests/test_math_edges.c) uses public `nj_call_static`
calls and fixed expected tables generated with OpenJDK 27. It covers every `abs/min/max` overload and both
`round` overloads, both binary argument orders, signed zeros, NaNs, infinities,
subnormals, integer limits and half-integer neighbors. Wide binary arguments use
slots 0 and 2 with sentinels in the unused slots. NaNs are compared by category;
other floating results are compared by bits. No C rounding formula supplies the
expected values.

The original Release library fails 30 of 363 checks. Instrumenting the original
Math translation unit with UBSan and explicit `float-cast-overflow` also reports
both minimum-value negations and the double-to-long boundary cast. These are
three concrete C undefined-behavior sites; the minimum-value `abs` outputs alone
were correct on the ordinary tested build.

The final Release build passes all 363 Math checks and the complete 159-test
CTest suite (serial run, 316.46 s). The fixed tables were also reviewed
independently; all 82 round expectations match a second exact-integer-ratio
oracle decoded from the original IEEE input bits.

An unchanged Minecraft JAR ran for 1600 paced SDL frames using copied test-world
storage. Terrain, held block and HUD were visually inspected; the captured
window matches the prior validated baseline by SHA-256. Both an actual earlier
snapshot and a newly written snapshot resumed for 300 headless frames; their
frame-200 images match each other and the previous validated scene. There are no
uncaught exceptions or invalid-chunk messages in these runs. The previously
recorded caught UDP receive loop after snapshot restore remains; this patch
does not repair native network handles.

An independent full Debug library and five tests pass with `-Wall`, `-Wextra`,
`-Werror`, ASan, UBSan, explicit `float-cast-overflow` and recovery disabled:
Math (363/363), plus the library, arrays, Random bounds and Random dispatch
regressions. The same instrumented Math regression passes 363/363 with
`NJ_ENABLE_FLOAT=0` and `NJ_ENABLE_M3G=0`; the existing M3G configuration guard
requires that combination.

A separate Math translation-unit compile with `NJ_USE_LIBC=0`, `-ffreestanding`
and `-fno-builtin` introduces no new libm imports. Apart from sanitizer helpers,
its added undefined symbols are required libc `memcpy` and this host compiler's
stack-protector helper; there are no `fabs`/`fabsf` references. This is a host toolchain symbol check,
not a full bare-device numerical test. A bare port must still supply a correct
`floor` implementation for the Java rounding contract.

The complete builtin metadata dump from the actual pre-fix library and the new
normal library is byte-identical (4022 lines). A paired comparison using the old
Math implementation under the same noFP/M3G-off configuration is also identical
(3387 lines). Field/method order, static slots and save layout do not change.

This is a numeric compatibility repair, with no Minecraft GEN, LOAD or FPS
speedup claim.
