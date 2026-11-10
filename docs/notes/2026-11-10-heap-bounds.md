# Checked Java heap and UTF-16 allocation sizes

`nj_size` is a 32-bit unsigned value on both 32-bit and 64-bit hosts. The
allocator previously added its block header and alignment padding without
checking for wraparound. Near-4-GiB requests could become minimum-size blocks.
A separate `bump + total <= size` test could wrap even when `total` itself was
representable, admitting an enormous zero-fill on a small real heap.

The fix checks the request before either addition and uses
`bump <= size && total <= size - bump` for arena capacity. With the default
16-byte header and alignment, the largest representable raw payload is
`0xffffffe0`; representability does not mean that it fits the configured heap.
A NULL VM now returns NULL before accessing its allocation counter. A real VM
still counts failed allocation attempts, and ordinary out-of-space requests
retain the existing one-collection-and-retry policy.

The UTF-16 constructor also needs its own check: `(len + 1) * 2` and the object
header could already have wrapped before reaching the allocator. In particular,
`len = INT_MAX` with a NULL source allocated a tiny object and then attempted to
write the terminator roughly 4 GiB away. The constructor now checks its object,
characters and terminator before multiplication. Negative lengths still become
empty strings, and a NULL source with an ordinary positive length still creates
zero-filled characters.

The conservative internal array extent guard and the existing public/bytecode
array length cap are unchanged. This does not establish a new Java array-size
bug. Internal allocation routines return NULL; their Java callers translate
allocation failure into the appropriate exception. No object/block fields,
class registrations, saved layouts, alignment, default heap size or interpreter
settings change.

The regression fixture uses real small heaps and fixed arithmetic boundaries.
Raw blocks are tested separately from GC-visible objects. Destructive old
NULL-VM, bump-wrap and UTF-16 cases run in separate processes; ordinary array
and string tests use checked roots, verify zero-fill and sentinel contents,
exercise collection, and confirm subsequent allocations still work.

Validation used an isolated copy of commit `c841753` with only the heap,
string, CMake registration and [new fixture](../../tests/test_heap_bounds.c)
overlaid. Its M3G code includes that commit's VertexArray repair. Games and
fixture data were copied privately; the main build and user storage were not
used as writable test output.

The Release fixture passed **664 checks**. The measured layout was 4-byte
`nj_size`, 32-byte object headers, and 16-byte allocator headers/alignment.
The same fixture linked with the two original heap/string translation units
reproduced these outcomes:

| Old implementation mode | Outcome |
| --- | --- |
| `--raw` | 62 failed assertions out of 308; wrapped requests returned small blocks and advanced the arena |
| `--null` | SIGSEGV in a separate process |
| `--bump` | SIGSEGV in a separate process on the real small heap |
| `--strings` | SIGSEGV in a separate process |
| `--arrays` | 273 checks passed, confirming the existing array guard |

No old combined run or core dump was used. The revised fixture verifies each
failed attempt's counter increment, registers roots with a checked count
increase, and never lets accidentally allocated raw blocks enter a collection.

All **161 CTest checks passed in one serial run** (351.35 seconds). A separate
full Debug build instrumented all 74 library and six test translation units
with `-Werror`, AddressSanitizer, UndefinedBehaviorSanitizer and explicit
`float-cast-overflow`, with recovery disabled. Heap bounds (664), lifecycle
(2301), arrays (3), arraycopy (1817), save (2521), and string-intern growth/GC
checks passed without sanitizer or leak diagnostics. The save test ran first,
before the main suite, to avoid its fixed temporary filenames overlapping.

The original Minecraft JAR ran for 1600 paced frames in SDL. Terrain, held
block and HUD were inspected. Old and newly written snapshots each resumed
for 300 frames, with identical sampled terrain images and preserved source
JAR/seed hashes. The previously known caught UDP receive loop after snapshot
restore remains. These checks cover that scene and save path, not a complete
playthrough or network-handle restoration.

The native measurements above are Linux x86-64 results. The fixture computes
its boundaries from actual layouts; its ILP32 expected count is static analysis,
not a 32-bit device result. This is a memory-safety and compatibility repair;
no Minecraft speed increase is claimed.
