# LocalDevice.getProperty static dispatch

JBenchmark HD invokes `javax.bluetooth.LocalDevice.getProperty(String)` with
`invokestatic` when collecting system information. The existing builtin table
incorrectly marked this method as an instance method, so that bytecode raised
`IncompatibleClassChangeError` before the native implementation could answer.

The [JSR 82 1.1.1 documentation](https://nikita36078.github.io/J2ME_Docs/docs/jsr82_1.1.1_javadoc/javax/bluetooth/LocalDevice.html)
declares a public static method with no declared exceptions and specifies `null`
for an unknown property. The fix adds the static flag to the existing method
slot. It also checks supported property names before probing the radio, compares
the entire Java string, and returns `null` if no working radio hook is available.
Previously, even an unknown name first tried to initialize a local device and
could throw `BluetoothStateException` on a port without Bluetooth support.

The three previously implemented values remain available when a radio hook
succeeds: `bluetooth.connected.devices.max` is `7`,
`bluetooth.sd.trans.max` is `1`, and `bluetooth.sd.attr.retrievable.max` is `64`.
Their exact names and values are covered by the native regression; this change
does not add a Bluetooth backend. Unknown, empty and embedded-NUL-suffixed names
return `null`. The runtime's existing permissive `null`-argument result is
preserved; the cited documentation does not prescribe a null-argument exception.

The regression loads a Java 45.3 class with the actual five-byte method body
`aload_0; invokestatic LocalDevice.getProperty; areturn`. It checks static
metadata, exact returned strings, full UTF-16 name matching and radio-hook call
counts across four port configurations. This exercises interpreter dispatch,
rather than invoking the C handler directly.

The old-code sensitivity harness supplies a zero-member
`IncompatibleClassChangeError` class because the bare CLDC library otherwise
represents this missing exception class as a generic `Throwable`. Only that
old-code mode registers the class; the old interpreter must still take its real
staticness error path and produce the exact named type. The ordinary regression
uses the runtime's unchanged exception library.

The old `2a633ba` library takes that real `invokestatic` error path. The initial
static-dispatch correction passed all 64 Java queries and 284 assertions,
including the independent strict sanitizer builds on both merged `27432df` and
`6a13581` baselines.

Correcting the static flag also changes the automatically assigned native local
count from two to one. Snapshot ABI hashes include that count, so the original
Minecraft snapshot was correctly rejected despite unchanged object layouts.
The builtin loader now retains two locals for this exact native signature:
local 0 receives the sole static String argument and local 1 is unused padding.
The method stays static, its argument count stays one, and snapshot validation
remains strict. Unused frame locals are cleared across the entire 64-bit value
slot, so conservative GC cannot see stale upper bits in that padding.

The final fixture retains all 64 Java queries and adds checks for the legacy
native layout and poisoned unused frame locals: 296 assertions pass. Linked
against the earlier static-but-one-local implementation, the same fixture
reports exactly six failures: four layout checks and two whole-slot checks.
The original pre-staticness sensitivity mode remains unchanged.

The final strict sanitizer run passes Save (2521 checks), this fixture (296),
Lifecycle (2301) and three VM create/run/destroy cycles, with ASan, UBSan,
explicit float-cast-overflow and leak checking enabled. The final CLI genuinely
resumes both the original `2a633ba` Minecraft state and a newly saved state;
neither run falls back to a fresh start. The intermediate unpublished
one-local snapshots remain unsupported.

The generated SDK declaration is also static. An independent Java source calling
`LocalDevice.getProperty` fails to compile against the old instance declaration,
compiles against the corrected stubs, and emits the expected `invokestatic` with
the String-to-String descriptor. This changes the stub generator only; original
game JARs and the already validated runtime binary are unchanged. The combined
suite and original benchmark results are documented in the
[benchmark report](2026-11-10-jbenchmark-baseline.md).
