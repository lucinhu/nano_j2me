# RV32 control-flow correctness and smaller native loops

This follow-up is based on `bdee0bf`, on branch `perf/jit-cache-admission`.
The fixes are `b849ef0` and `9f815c2`. Platform-specific integration and unrelated
media changes are excluded. No target firmware is flashed in this follow-up.

The register cache kept known constants across control-flow joins while only
invalidating remembered memory locations. A taken edge could skip the
instruction establishing that constant on the linear fall-through path. A
small regression reproduces an incorrect zero instead of one in generated
native code. Joins and loop headers now invalidate constants as well. Dynamic
memory invalidation keeps its original, narrower meaning.

For performance, consecutive bytecodes share one completed-count update.
Branches include their own bytecode before the update; guards flush the
preceding run before the potentially faulting instruction. Fall-through updates
sit before target labels so taken edges do not count the skipped path. Loop
budget checks and fault exits therefore retain exact completed counts. Signed
comparisons use BLT/BGE directly, avoiding a temporary SLT result and a second
instruction. These are baseline integer instructions; no new ISA extension,
public ABI, cache capacity or per-VM allocation is introduced.

An eight-bytecode sum loop changes from eight completed-count updates per
iteration to one. Its emitted function, including checks and exits, changes
from 33 to 25 machine instructions and from 106 to 88 bytes with compressed
instructions enabled. This is an instruction/code-size result, not a device
cycle or FPS measurement.

Eight independent 50,000-iteration Java kernels retain their checksums, Java
and native bytecode counts, metadata sizes and allocation/free balance in
interpreter, probe-only and execute modes. Their execute-mode code sizes are:

| Kernel | Before bytes | After bytes | Reduction |
| --- | ---: | ---: | ---: |
| Locals | 238 | 196 | 17.65% |
| Constants | 150 | 128 | 14.67% |
| Fields | 324 | 294 | 9.26% |
| Arrays | 582 | 532 | 8.59% |
| Mixed fields/arrays | 914 | 836 | 8.53% |
| Branches | 414 | 340 | 17.87% |
| Byte table | 616 | 538 | 12.66% |
| Matrix | 662 | 578 | 12.69% |
| Sum of eight separate runs | 3900 | 3442 | 11.74% |

The sum is not the live cache occupancy of one VM. Smaller blocks reduce code
space pressure; these measurements do not establish a reduction in game cache
evictions. Single diagnostic QEMU wall times vary in both directions and are
archived without treating them as device performance.

The full emitter matrix passes fourteen ISA/feature/capacity/statistics
configurations: 22,292,532 host encoding assertions, 22,674,598 RV32 assertions
and 42,128 native executions. Separate runtime/emitter checks pass on the host,
under RV32 emulation, and with address/undefined-behaviour sanitizers (three
tests each). Added coverage checks all 25 pairs of five signed boundary values
for each comparison operator, every loop budget from zero through forty, and
fault counts after both outcomes of a forward branch. The join regression
fails before its fix and passes afterwards.

No new gameplay FPS result is available. The original serial node is absent
and another bridge is held exclusively by a separate test session. Existing
240x320 results in [JIT_CACHE_LOOKUP.md](JIT_CACHE_LOOKUP.md) describe the earlier
source; they must not be relabelled as measurements of this follow-up. The
pre-existing slow native branch-kernel result remains unresolved on the target.
The runtime default remains JIT off, with per-application opt-in.

The anonymous numerical archive is [jit-loop-accounting-results.json](jit-loop-accounting-results.json).
Reproduction uses the existing scripts and a separately configured build:

```sh
QEMU_RISCV32=qemu-riscv32-static tools/check_jit_rv32.sh --require-execution
tools/check_jit_perf.sh --build-fixture-only
tools/check_jit_perf.sh --build-dir build-rv32 --runner qemu-riscv32-static \
    --iterations 50000 --require-execution
```

The matrix needs a matching compiler; the performance build must explicitly
enable JIT and disable superinstructions for a comparable run. Emulation proves
execution and checksum agreement, not target throughput.

Old mobile 3D and the next rendering priorities

Low clock speed alone does not determine game performance. Sun's historical
[CLDC HotSpot white paper](https://www.oracle.com/technetwork/java/cldc-hi-whitepaper-150012.pdf)
reports early 12-32 MHz designs and a surveyed next-generation range of
30-400 MHz. It describes adaptive native compilation and keeping the working
set in processor cache. These are historical survey figures, not a claim that
every 3D-capable phone had the same clock or VM.

[JSR 184's proposal](https://jcp.org/en/jsr/detail?id=184) explicitly targets
limited devices without 3D hardware or floating-point support, while allowing
hardware acceleration on stronger devices. It expects rendering in native
code and encourages high-level operations that move work out of Java. Having
Java game logic therefore does not imply a Java pixel loop, and a GPU is not a
prerequisite for every old mobile 3D title.

For this runtime, code inspection identifies a concrete path limitation, not
an estimated timing percentage. `jr_setup` enables M3G's full software rasterizer
only for ARGB8888 targets. A direct RGB565 target falls back through `jr_emit`
and `emit_to_java` to per-triangle `Graphics.setColor`/`fillTriangle` calls,
without the full texture/depth path. An offscreen ARGB8888 Image can use the full
rasterizer, so this limitation must be checked per game. Mascot/Micro3D has a
separate native fixed-point rasterizer, and games rendering 3D in Java differ
again. No renderer behaviour is changed by this follow-up.

The next priorities are to measure the selected render path and frame-time
components, extend native rendering to the required target formats, and reduce
VM/native transitions while preserving clipping, transforms, textures, depth
and blending. After that, profile texture sampling, vertex reuse and memory
traffic before choosing scanline specializations or cache layouts. Existing
M3G edge/interpolant stepping, opaque-flat specialization, triangle-strip reuse
and Mascot span loops already implement several basic optimizations; they
should not be proposed as missing features.

Keep 240x320 as the comparison resolution. An optional lower internal render
resolution can trade quality for speed, but is a separate measurement: a
128x160 image has only 1/3.75 as many pixels. Nearest filtering or less frequent
perspective correction similarly changes quality/rounding and needs explicit
options and image checks, rather than a silent global fast-math change.
