# System.arraycopy overlap, failure prefixes and bounds

The reference-array implementation previously copied overlapping ranges in the
wrong direction and checked element types only after modifying the destination.
This change follows the existing CLDC 1.1 interface without adding builtin
members or changing the snapshot layout.

## Required behavior

The [CLDC 1.1 System specification](https://docs.oracle.com/javame/config/cldc/ref-impl/cldc1.1/jsr139/java/lang/System.html)
requires the following order:

1. A null source or destination throws `NullPointerException`.
2. A non-array operand, or incompatible primitive array types, throws
   `ArrayStoreException`, including when the requested length is zero.
3. Invalid positions or length throw an `IndexOutOfBoundsException` subtype and
   leave the destination unchanged.
4. Reference elements are checked in source order. If element `k` is the first
   incompatible value, exactly the preceding `k` elements are copied; the
   failing slot and all subsequent destination slots retain their original values.

A copy within one array behaves as though the source range were first saved to
a temporary array. A zero-length reference copy does not inspect element values
or reject different reference component classes.

## Implementation

The old overlap decision compared the base addresses of the two arrays. Those
addresses are equal for a copy within one array, so a rightward overlapping copy
used the forward loop and overwrote unread source elements. Ordering pointers
from separate allocations was also unnecessary.

Primitive arrays continue to use `memmove`. Reference copies now use `memmove`
when the arrays are identical, the destination is an untyped native reference
array, or declared component covariance proves every element assignable. Other
reference copies check each source element before publishing that element to the
destination. No allocation or callback occurs while holding array data pointers;
an exception returns immediately after the failing check.

Bounds use checked nonnegative positions and unsigned subtraction instead of
signed `position + length`. This avoids signed overflow at extreme Java integer
arguments and preserves the required exception order.

Native-created arrays may lack a class mirror. Their element kind still proves
primitive-array identity. The checked path recognizes the exact primitive array
component, `Object`, `Cloneable` and `Serializable` where applicable. A native
reference array without component metadata is conservatively treated as
`Object[]`; its current contents do not establish that it is a `String[]`.
This boundary is covered separately from behavior constructible in Java.

## Regression evidence

[The C regression](../../tests/test_arraycopy.c) calls the public native bridge
and checks fixed destination snapshots, rather than reproducing the copy loop.
It covers all eight primitive kinds, raw floating-point payloads, overlap in both
directions, identical and zero-length ranges, all directed primitive-type
mismatches, null and structural-type priority, extreme bounds, reference
covariance, partially failing reference copies and typed/native nested arrays.

An independent standalone Java oracle exercises the Java-constructible cases
and records the four failure-prefix snapshots. The native-only missing-metadata
cases use explicit C expectations. Both old and new implementations are checked
with the same frozen test source.

The new fixture makes 233 public calls and passes all 1817 assertions. Linking
that same test object against the preserved old library fails 37 assertions.
The old System translation unit also reports two signed `INT_MAX + 1` overflows
under UBSan. The standalone OpenJDK 27.0.1.0.3 oracle passes 329 checks, including
the four failure prefixes. These results distinguish Java-constructible behavior
from the native-only class-metadata policy.

The isolated Release suite has passing records for all 160 registered tests.
Its first run passed 152 of 157 tests; five script checks could not find the
executable at their assumed source-tree build path. Supplying the private binary
path and alias fixed that environment. A private reference-tool wrapper enabled
three additional reference checks. Only the five failed and three newly
registered tests were then run, and all eight passed. Previously passing tests
were not repeated.

A full Debug library and the four tests `test_arraycopy`, `test_arrays`,
`test_lib` and `test_save` also pass with warnings treated as errors, ASan,
UBSan, explicit float-cast-overflow instrumentation, leak detection and fatal
sanitizer errors. Their assertion counts are 1817, 3, 1432 and 2521. The first
`test_lib` invocation lacked its private output parent; creating that directory
was the only setup change before its successful rerun. The old and new builtin
ABI dumps are byte-identical across 4022 lines: 293 classes, 294 registrations
and 425 static slots. The method and field tables are unchanged.

Both builds use private copies of the committed source plus only this System
change, its CMake registration and new test. Concurrent uncommitted renderer and
renderer-test changes are excluded. Original game JARs and user saves are
preserved. Minecraft runs for 1600 paced SDL frames; both a pre-change and a new
snapshot resume for 300 headless frames each. The checked terrain, held block and
HUD match the prior baseline, as do the selected resume frames. The pre-existing
caught UDP receive loop after snapshot restore remains outside this repair.
These bounded scenes do not establish whole-game compatibility.

This is a correctness repair; no Minecraft GEN, LOAD or FPS gain is claimed.
