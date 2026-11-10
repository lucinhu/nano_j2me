# Minecraft GEN: current native CPU sampling

The fixed original Minecraft world generator still spends most sampled CPU in
`nj_interp_run`: 8,193 of 9,582 samples (85.50%). This includes inlined work and
shared dispatch paths. It does not mean that dispatch alone costs 85.50%, or
that removing a particular macro would recover its source-attributed share.
This batch records a diagnostic; it introduces no runtime optimization.

## Measured version and workload

The measured source is `a1543020135830d41db017e08c74894479748149`, before the
subsequent remote Gauge, Item layout and M3G loading fixes were merged. The
private source, archive and measured executable retain that identity. Later
benchmark results on a merged version are a separate experiment.

- Intel Core Ultra 9 275HX, Linux x86-64, GCC 16.2.1; process pinned to CPU0.
- Fresh Release `-O3 -DNDEBUG`, JIT and engine disabled; threaded dispatch,
  superinstructions, jump threading, hot fields and floating point enabled.
- Original `MinecraftJ2ME_PE_0.6.1_V2.2.jar`, SHA-256
  `fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`.
- Unmodified [public GEN benchmark](../../tools/bench_minecraft_gen.c), SHA-256
  `fcc8af1078665e7c329df1542a967c202f27a4eafeb4545e205bf60634743c69`;
  seed 12345, type 0, 64 MiB VM. Construction and world hashing remain outside
  its GEN clocks.
- One bounded process and one generation, with no retry or candidate variant.

The diagnostic reported CPU 9,579.973 ms and wall 9,628.255 ms. Those clocks
include sampler setup and teardown inside the wrapped GEN call. They are not an
unsampled throughput baseline or a comparison with earlier timing runs.
All 256 chunk block/data hashes and WORLD hashes match the original oracle:
`a893a062` / `c77a1b93`. Spawn independently matches `161 / 64 / 111`; the old
world oracle has no spawn field. Charged dispatch is 5,175,713,674, native calls
1,616,679, GC count 0 and heap/peak 6,510,256 bytes. Charged dispatch includes
fusions and is not the original JVM bytecode count.

## Sampling and code identity

An isolated wrapper includes the unchanged public benchmark and intercepts only
the existing `Gen.generate` bridge. ITIMER_PROF requests a 1,000 microsecond
interval. The signal handler copies native RIP into a preallocated bounded
array; it does not read VM frames, allocate, print or symbolize. Stop blocks and
disarms the timer, drains pending sampler signals, restores the handler/mask,
and precedes VM destruction on either returned status.

There are 9,582 accepted samples at 1,583 unique PCs, capacity overflow 0 and
pending/drained signals 0 at stop. Native thread counts are 1 at start and stop;
these are boundary observations. Ordinary signals may coalesce or be delayed,
so accepted samples are neither exact millisecond buckets nor instruction counts.

The extra-`-g3` interpreter object's `.text` (82,669 bytes) and `.rodata`
(1,657 bytes) are byte-identical to the actual fresh archive member before
linking. Read-only ELF extraction preserves the inputs. The measured executable
is ET_EXEC; symbol ranges and DWARF inline chains are resolved against that ELF,
without assigning external PCs to a nearest local symbol.

One wrapper compilation initially failed because current glibc selected
`_POSIX_C_SOURCE=202405L` and the included benchmark defines 200809L. The local
wrapper was corrected with an undef immediately before the unchanged benchmark
include, after its system headers. Only the failed wrapper compile, first link
and first prescreen were resumed; successful library/CLI/debug-object builds
were reused byte-for-byte. Initial failure records remain preserved. The
existing [VM lifecycle test](../../tests/test_vm.c) passed all three HelloWorld
create/run/destroy rounds before the single generation. This was not a full
suite, sanitizer, gameplay or Android run.

## Native symbol attribution

Each accepted sample contributes once to this table; percentages use all 9,582,
including 10 external/unknown samples.

| Native symbol | Samples | Share |
|---|---:|---:|
| `nj_interp_run` | 8,193 | 85.50% |
| `nj_interp_const_arith` | 615 | 6.42% |
| `nj_cp_const` | 261 | 2.72% |
| `nj_frame_push` | 170 | 1.77% |
| `nj_array_get_int` | 153 | 1.60% |
| `nj_field_read_ref_fast` | 109 | 1.14% |
| `nj_field_read_int_fast` | 39 | 0.41% |
| `nj_array_set_int` | 15 | 0.16% |

9,572 samples fall inside this ELF's executable sections. Partial DWARF
information exists for 8,972 samples: 8,971 have a numeric source line, one has
only a source filename, and 610 have no source attribution. Full inline chains
are retained as descriptions of the same samples, not additional observations.

Using the outer interpreter frame's compiler source location gives these
largest disjoint regions. Region names include fusion checks and dispatch;
optimized instructions may serve more than one source operation.

| Compiler source region or separate helper | Samples | Share |
|---|---:|---:|
| Local-load sites, fused tails and dispatch | 3,205 | 33.45% |
| Invocation sites and dispatch | 915 | 9.55% |
| Constant sites, fused tails and dispatch | 894 | 9.33% |
| Arithmetic sites and dispatch | 836 | 8.72% |
| Separate `nj_interp_const_arith` helper | 615 | 6.42% |
| Branch/switch sites and dispatch | 498 | 5.20% |
| Local-store sites, fusion and dispatch | 473 | 4.94% |
| Return/frame reload sites and dispatch | 439 | 4.58% |

An alternative innermost-inline view assigns 549 samples to `nj_cp_hit`, 111 to
`nj_heap_owns_object_fast`, 109 to `nj_interp_farith` and 87 to
`nj_class_init_lazy`. These already belong to the interpreter total and must not
be added to the tables above. Source discriminators and macro expansion also
make a single displayed C line an unsuitable exact Java-opcode cost measure.

## Verification and interpretation

Independent review reparsed every raw PC, symbol extent, inline chain and
frequency, checked the unchanged code sections and complete world/counter
identity. Lead independently checked raw frequencies, tables, archive identity
and inputs. The full raw profile, command logs, failure/recovery history and
frozen manifests are retained locally. Profile log SHA-256:
`9e8b5a93e5450019c5781293cd3d320c301cc07f3fb55ef1fcbaa6b24c000f5c`;
raw PC TSV: `f26de1792a497e2da532f0b03437930c6c518d7423936433b7ec618c8ff61e1e`;
measured ELF: `97dcd7e1c4bf120491c0837da3bf2e609142f10dbc70383be055a7627cfe34c7`.

These findings prioritize investigation of interpreter data movement,
call/return handling and existing constant/arithmetic paths. They do not prove
a new candidate's gain, Java opcode heat, cache misses, branch mispredictions,
Android speed or game FPS. Earlier rejected fusion, ldc, READY and JIT candidates
remain rejected. Frame/GC rooting, exception locations and scheduling boundaries
remain required when evaluating a future change.

The [J2ME-Loader comparison](2026-11-10-j2me-loader-comparison.md) and
[separate LOAD profile](2026-11-10-minecraft-load-profile.md) retain their own
versions and scopes. This pure GEN diagnostic does not measure rendering or
justify treating GLES as its acceleration mechanism.
