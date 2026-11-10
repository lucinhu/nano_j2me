# Integer radix formatting: defined minimum-value magnitude

`Integer.toString(int, int)` previously evaluated `-v` as a signed 32-bit
expression before casting it to `nj_u4`. For `Integer.MIN_VALUE`, the magnitude
2147483648 cannot be represented by that signed type, so the C expression had
undefined behavior. The affected overload is `(II)Ljava/lang/String;`.

The fix converts the input to `nj_u4` before subtracting it from unsigned zero.
Unsigned arithmetic is defined modulo 2^32, including the minimum integer. The
formatter still receives a separate minus-sign flag and the same magnitude for
all other inputs. Radices outside 2..36 still fall back to decimal.

The [CLDC 1.1 Integer contract](https://docs.oracle.com/javame/config/cldc/ref-impl/cldc1.1/jsr139/java/lang/Integer.html)
requires signed output for this overload across the entire `int` range. Its
binary, octal and hexadecimal convenience methods intentionally use an unsigned
representation, and remain unchanged. Decimal Integer/String/StringBuffer and
PrintStream paths already widen before negating. Long formatting already uses
a safe magnitude calculation. Neither requires this repair.

This change adds no classes, methods, fields, caches or persistent state and
changes no public API tables or save format. It does not expand CLDC Unicode
digit support or change parsing policy. No Minecraft speed gain is claimed.

## Validation

The regression in `tests/test_lib.c` adds 358 public API calls with expected
strings generated independently by OpenJDK 27.0.1.0.3:

- 35 valid radices × seven values: both integer extremes, their neighbours,
  -1, 0 and 1.
- Six invalid radices × the same seven values, checking decimal fallback.
- 36 positive/negative power-boundary cases, including lowercase letter digits.
- 21 unsigned binary/octal/hex controls and 14 existing decimal-path controls.

The saved Java output is embedded as literal expected data, rather than
reimplementing the C magnitude algorithm in the test. The matrix runs after
existing heap-pressure cases and compares each returned String before any
further allocation.

Both old and fixed ordinary Release builds passed **1790 class-library
assertions**, including the new matrix. This host therefore showed no ordinary
output mismatch. Separately, compiling the old boxing translation unit with
fatal UBSan produced:

```text
runtime error: negation of -2147483648 cannot be represented in type 'int'; cast to an unsigned type to negate this value to itself
```

That old sanitizer process stopped at the offending expression; it did not
complete the matrix. Ordinary output equality does not make the signed C
expression defined.

An independent full Debug library build and both test translation units use
`-Wall -Wextra -Werror`, ASan, UBSan, explicit `float-cast-overflow`,
`-fno-sanitize-recover=all` and frame pointers. Leak detection and sanitizer
halt-on-error are enabled. `test_save` ran first (**2521/0**), followed by
`test_lib` (**1790/0**). Both passed without sanitizer diagnostics. These are
same-host save regressions, rather than a new cross-version serialization test.

The original Minecraft JAR ran for **1600 paced SDL frames** with isolated
filesystem/RMS copies. Old and new snapshots then resumed for **300 frames
each**. The inspected terrain, held block and HUD were correct; the SDL sample
and selected restored-frame PNGs matched their baseline hashes. This checks
selected samples, not every frame or the whole game. There were no uncaught
exceptions or invalid-chunk messages. The pre-existing caught UDP receive loop
after snapshot restore remains outside this repair. The JAR and seed-file
hashes stayed unchanged. Paced runs do not establish a speed gain.

The complete isolated Release CTest suite passed **161/161**, serially in
**336.44 seconds**. Its source tree was the recorded HEAD plus only the boxing
fix and this regression test. Concurrent workspace changes for version 0.5.6,
release documentation and frontend metadata were preserved and excluded from
this validation and commit.

A concurrent drawRGB change (`00318b4`) was subsequently merged. A fresh build
of the combined tree passed `test_lib`, `test_gfx` and `test_lcdui` (**3/3**),
including **1790/0** class-library and **8511/0** LCDUI assertions. The original
Minecraft SDL scene and both snapshot resumes were repeated with this build;
the selected PNGs retained the baseline hashes. The full-suite and sanitizer
results above describe the initial isolated boxing fix; the combined tree
received these focused integration checks and the repeated scene checks.
