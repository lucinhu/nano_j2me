# Execution engines and the JIT contract

The runtime has exactly one place where Java bytecode execution begins.
`nj_exec()` in `src/core/nj_engine.c` is that place: it either forwards to the
portable interpreter (`nj_interp_run()` at `src/core/nj_interp.c:466`) or to an
installed *execution engine*.  An engine is any object that can execute a
thread's Java frames and return the same observable effects as the interpreter.

The tree includes opt-in **RV32 and ARM64 baseline JITs**: portable machine-code
encoders sharing a bounded per-VM hot-block cache and interpreter dispatch
integration. x86_64 currently uses the interpreter. RV32 has bounded temporary
register reuse and integer constant propagation within each leaf block; there is
no method-level optimizer, general register allocator, or persistent code cache. The original engine
contract and future method-level designs below remain useful background; the
implemented block compilers are described first.

## Baseline JIT: configuration and scope

Enable `NJ_ENABLE_JIT=1` (default `0`). CMake also enables `NJ_ENABLE_ENGINE`;
direct compiler builds default the engine switch to the JIT switch and reject
an explicitly disabled engine with an enabled JIT. `nj_vm_create()` tries to
install the matching JIT automatically. Native execution requires little-endian
RV32 with the standard ILP32 register set (not RV32E), or little-endian AArch64
with LP64 flat pointers (not ILP32 or arm64e). All three executable-memory
callbacks must exist. Unsupported hosts and ports continue interpreted execution.
There is no runtime CPU probing; explicit extension settings must match the CPU.

### RV32

| Macro | Default from compiler ISA | Generated code when enabled |
|---|---|---|
| `NJ_JIT_RV32_M` | `__riscv_mul` and `__riscv_div` | `mul`, `div`, `rem` for Java int |
| `NJ_JIT_RV32_A` | `__riscv_atomic` | Reserved capability; no AMOs needed by the current cooperative VM |
| `NJ_JIT_RV32_F` | `__riscv_flen >= 32` | Float add/subtract/multiply/divide/negate |
| `NJ_JIT_RV32_D` | F enabled and `__riscv_flen >= 64` | Double add/subtract/multiply/divide/negate |
| `NJ_JIT_RV32_C` | `__riscv_compressed` | Compressed immediate loads/adds and return |

Macros accept `0` or `1`. CMake also accepts `AUTO` (default), meaning leave the
macro undefined so the header derives it from the compiler target. D requires F;
`D=1,F=0` is a compile error. With F disabled and D left AUTO, D defaults off.
The switches constrain JIT output only: use the toolchain's `-march`/`-mabi` to
constrain the VM, libc and other compiled C code. Java operations whose machine
extension is disabled fall back to the interpreter. Bitwise float/double slot
copies can still use RV32I loads/stores. Float arithmetic lowering additionally
requires `NJ_ENABLE_FLOAT`; double and long-slot lowering require `NJ_ENABLE_LONG`.

For example, using an RV32 toolchain file (set its compiler and deployment ISA):

```sh
cmake -S . -B build-rv32 -DCMAKE_TOOLCHAIN_FILE=/path/to/rv32-toolchain.cmake \
  -DNJ_ENABLE_JIT=1 -DNJ_JIT_RV32_M=1 -DNJ_JIT_RV32_A=0 \
  -DNJ_JIT_RV32_F=0 -DNJ_JIT_RV32_D=0 -DNJ_JIT_RV32_C=1 \
  -DNJ_BUILD_SDL2=OFF -DNJ_BUILD_POSIX_NETWORK=OFF
cmake --build build-rv32 -j
```

A direct C build can use `-DNJ_ENABLE_JIT=1` and the same feature defines. Include
`src/core/nj_jit.c`, `src/core/nj_jit_rv32.c` and `src/core/nj_jit_arm64.c` in the
source list. The Android, ESP-IDF and embedded-test component lists include them;
they compile empty when the JIT is disabled. Execution still requires a supported
CPU/ABI and executable-memory callbacks.

The RV32I path covers integer constants, local load/store (including references),
add/subtract/negate, bitwise operations, shifts, narrowing, `iinc`, supported
`wide` locals/`iinc`, `pop`/`pop2`, `dup`/`dup2`, and `swap`. Wide values and floating
constants/locals are copied using integer instructions. The current direct local
offsets support indices through 255 for single slots and 254 for wide slots.
Both backends support `ldc/ldc_w` for Integer and Float constants by reading
validated raw constant-pool bytes, without resolution or allocation. Float bits
can be loaded with hardware FP and `NJ_ENABLE_FLOAT` disabled.

Resolved instance fields compile to native `getfield`/`putfield` for every Java
primitive and reference type. The CP cache must already hold a linked instance
field; the probe reads it without resolving anything. `BOOLEAN`/`BYTE` widen
with sign extension, `CHAR` with zero extension, `SHORT` with sign extension,
`INT`/`FLOAT` move 32 bits (`FLOAT` as raw bits), references move one
pointer-sized word (32 bits on RV32, 64 bits on LP64), and `LONG`/`DOUBLE` move
64 bits with the padding slot's low 32 bits cleared.
Resolved static fields compile to native `getstatic`/`putstatic` for every Java
primitive and reference type, reading the linked field out of the CP cache like
instance fields and addressing the slot through `vm->statics`. When the declaring
class is not `NJ_CLASS_READY` at build time the block carries a runtime
class-state guard: a not-yet-initialized class deoptimizes at the instruction, so
the interpreter still runs `<clinit>` first. Once the class is ready the guard is
dropped, because `READY` is terminal.

Array element access compiles natively for `int` (`iaload`/`iastore`), `byte` and
`boolean` (`baload`/`bastore`), `char` (`caload`/`castore`), `short`
(`saload`/`sastore`) and references (`aaload`); native `arraylength` pushes the
array length. The generated guard checks null, array kind and length before the
element flags, in the same order as the interpreter. `baload`/`bastore` accept
both boolean and byte arrays, the other typed accesses require the matching
element flag, and `aaload` requires a reference array (a non-reference array is
malformed bytecode and only deoptimizes). A null receiver, a mismatched array, an
out-of-bounds index or a zero divisor makes the block return to the interpreter
*before* the operation's first side effect; the interpreter then re-executes that
exact bytecode, so the Java exception class, message and PC are unchanged.

Integer conditional branches (`if<cond>`, `if_icmp<cond>`, `if_acmpeq/ne`,
`ifnull`/`ifnonnull`) and `goto` compile natively. A forward target inside the
same window becomes an internal label; two paths may join only at the same
operand depth, otherwise the whole block is declined. A forward target beyond
the window ends the block through a dedicated exit that records the target PC
and stack depth. A backward branch is compiled only when it targets the block
entry with an empty operand stack (a loop). Loop blocks keep a bytecode counter
and check the remaining scheduling budget before every iteration, so native
execution always returns to the interpreter after at most `budget` bytecodes and
cannot starve other threads or defer the bridge's deadline check.

String/Class and wide constants, long arithmetic, conversions/comparisons,
complex stack permutations, calls, allocation, monitors and exception handling
stay in the interpreter. A block ends immediately before an unsupported
operation; the next interpreter dispatch can enter another compiled block.

`NJ_JIT_RV32_REG_CACHE=1` (default) tracks T0/T1 values across bytecodes, reuses
redundant loads and folds supported integer operations with known operands.
Stores are still emitted: all slot bytes, including dead stack-slot contents,
retain interpreter semantics. Overlapping stores invalidate cached mappings;
failed emission rolls back the entire writer state. Set the macro to `0` for
an A/B comparison. ARM64 currently retains its original per-operation loads.

### ARM64 / AArch64

| Macro | Default from compiler ISA | Generated code when enabled |
|---|---|---|
| `NJ_JIT_ARM64_FP` | AArch64 with single/double support in `__ARM_FP` | Scalar float/double add/subtract/multiply/divide/negate |
| `NJ_JIT_ARM64_SIMD` | AArch64 with `__ARM_NEON` | Reserved capability; no vector lowering yet |
| `NJ_JIT_ARM64_LSE` | AArch64 with `__ARM_FEATURE_ATOMICS` | Reserved capability; no atomic lowering yet |

These macros accept `0` or `1`; CMake also accepts `AUTO`. FP is independent of
SIMD: setting SIMD to zero still permits scalar floating-point instructions.
With FP disabled the encoder emits no FP instructions or FPCR accesses; Java
float/double arithmetic runs in the interpreter. Slot copies and constants can
still use integer instructions. Java feature flags `NJ_ENABLE_FLOAT` and
`NJ_ENABLE_LONG` apply as for RV32. The macros constrain emitted code; the C
compiler's `-march`/`-mcpu` settings separately govern the rest of the program.

The ARM64 backend covers the same basic bytecodes as RV32 — including
conditional branches, `goto`, internal labels and bounded loops — and uses base
AArch64 integer multiply/divide/remainder. Branches follow the v3 block contract:
`w2` carries the budget, `w3` counts completed bytecodes, a taken branch leaves
through the recorded exit, and a loop re-checks `budget - completed` at its entry
label. It supports all valid 16-bit local indices
(up to 65534 for one slot or 65533 for a two-slot value, within `max_locals`).
References, including null, use full 64-bit slot accesses. Integer and float
stores preserve the upper half of a slot, matching the interpreter's 32-bit
`bits` member. Long/double payloads occupy 64 bits; only the low 32 bits of their
second, padding slot are cleared. The static-field and typed-array coverage
described above is RV32-only for now: the ARM64 encoder ends the block before
`getstatic`, `putstatic` and the non-`int` array accesses, which then run in the
interpreter. Unsupported operations use the interpreter.

Floating arithmetic temporarily saves FPCR, selects round-to-nearest-even with
subnormal values preserved and default-NaN mode disabled, then restores the
caller's FPCR. Negation flips the sign bit to preserve NaN payloads. Generated
blocks use caller-saved registers and never change the native stack pointer.
Every entry starts with `BTI C` (a harmless hint on CPUs without BTI), allowing
ports with guarded executable pages to use the same entry convention. Pointer
authentication for the arm64e ABI is not supported.

```sh
cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=/path/to/arm64-toolchain.cmake \
  -DNJ_ENABLE_JIT=1 -DNJ_JIT_ARM64_FP=1 \
  -DNJ_JIT_ARM64_SIMD=0 -DNJ_JIT_ARM64_LSE=0 \
  -DNJ_BUILD_SDL2=OFF -DNJ_BUILD_POSIX_NETWORK=OFF
cmake --build build-arm64 -j
```

### Shared runtime

Each generated function is a bounded leaf with ABI
`nj_u4 block(nj_value *incoming_sp, nj_value *locals, nj_u4 budget)`. It only
changes frame slots, uses caller-saved registers, and never calls, allocates,
yields, or changes Java control flow. A successful return is
`(completed << 4) | exit_index`, where `completed` counts the bytecodes finished
on this run (never more than `budget`) and `exit_index` selects `exits[]` in the
block metadata. Exit 0 is the end of the straight-line window; further exits are
forward branches that leave the window and the loop budget exit, each carrying
the target PC and operand stack depth. For a loop block `completed` can exceed
`block.insns` because it counts whole iterations.

If a runtime guard fails (null receiver, bad array, zero divisor), the block jumps
to a fault exit that returns `0x80000000 | (fault_index << 24) | completed`, where
`completed` counts the bytecodes finished before the faulting instruction and
`fault_index` selects `faults[]` in the block metadata. The C entry restores PC to
`entry_pc + faults[i].pc_off` and SP to `incoming_sp + faults[i].sp_delta`, then
returns `completed`; the interpreter executes that bytecode again, so the Java
exception, its PC and any class initialization behave exactly as in a purely
interpreted run. A block that would need more than `NJ_JIT_MAX_FAULTS` guarded
instructions ends before the next one. A failure with `completed == 0` also pins
the site as refused, which prevents compile/fault churn. A loop block checks
`budget - completed >= window size` at its entry label, so a back edge always
returns through the budget exit after at most `budget` bytecodes.

Both limits bound how much of a loop body can be compiled at once: a body with
more than `NJ_JIT_MAX_BLOCK_INSNS` bytecodes or more than `NJ_JIT_MAX_FAULTS`
guarded operations is split, and the part left outside still runs in the
interpreter. The defaults (64 bytecodes, 12 faults) admit the common
field/array-update loops in one block; lowering them saves metadata and code
bytes at the cost of a higher fallback ratio, which the statistics expose as
`fallback_ratio`.

The C entry checks stack consumption/growth and scheduling budget before calling.
Both backends match Java's `INT_MIN / -1` overflow behavior. RV32 encodes RNE
explicitly; ARM64 uses the temporary FPCR state described above (the fault exit
restores it too). Negation preserves NaN payload bits on both targets.

A conservative inline opcode filter skips known interpreter-only instructions.
The remaining probe only touches bounded JIT metadata: it never allocates, frees,
emits code, calls the port or enters Java. At the hot threshold the site becomes
ready: in execute mode the probe omits the separate decode-only scan whenever
the remaining scheduling budget is at least `NJ_JIT_MAX_BLOCK_INSNS`, because
compilation emits into a stack buffer and rejects unsupported/short prefixes
before `SAVE()` or any executable-memory callback anyway. A smaller remaining
budget (end of a quantum) still takes the decode-only path, so it can report
`budget_skips` instead of compiling code the guard would refuse to run;
`NJ_JIT_MODE_PROBE_ONLY` always analyses so it can report block lengths. The
interpreter still saves exact PC/SP and instruction counts before compilation,
executable-memory callbacks (including eviction/free), or native execution. A returned probe ticket
must be consumed immediately; port callbacks must not re-enter the VM. Native
bytecodes consume the scheduler quantum and are included in VM instruction
statistics. While this JIT is installed, interpreter superinstruction fusions
are bypassed from the first dispatch, including cold or refused blocks: warming
up must not change the quantum charged for a bytecode sequence. Goto-chain
threading retains the interpreter's existing counting policy. Therefore counts
can differ from a separate interpreter-only run with superinstructions enabled;
compare program results, or disable superinstructions for count comparisons.
Tracing bypasses native execution so per-bytecode trace output remains available.

| Limit macro | Default | Meaning |
|---|---:|---|
| `NJ_JIT_CACHE_ENTRIES` | 64 | Total executable-code entries per VM |
| `NJ_JIT_CACHE_WAYS` | 2 | 1, 2 or 4 ways; entry count must be divisible by ways |
| `NJ_JIT_METHOD_ENTRIES` | 32 | Mapped methods; least-recently-used map is recycled |
| `NJ_JIT_METHOD_ARENA_BYTES` | 4096 | Fixed probe-map arena (one 16-bit cell per bytecode) |
| `NJ_JIT_MAX_FAULTS` | 12 | Guarded instructions per block before it must end |
| `NJ_JIT_MAX_EXITS` | 4 | Window end, leaving branches and the loop budget exit |
| `NJ_JIT_HOT_THRESHOLD` | 8 | Visits before decode-only eligibility analysis |
| `NJ_JIT_MIN_BLOCK_INSNS` | 4 | Minimum bytecodes to compile; experiment with 1/4/8 |
| `NJ_JIT_MAX_BLOCK_INSNS` | 64 | Maximum Java bytecodes in a block |
| `NJ_JIT_MAX_CODE_BYTES` | 2048 | Maximum bytes per block; also compiler stack scratch |

Heat, refusal and compiled state live in one per-method map: a 16-bit cell per
bytecode offset (`0..0x3FFF` heat, `0x4000` refused, `0x6000|insns` ready for
compilation — the analysed length, or zero when execute mode deferred the check
to the compilation pass — and `0x8000|slot` compiled). The interpreter's current method is
cached, so the steady-state probe is a pointer compare plus one array load: no
hashing and no scan per bytecode. Maps come from a fixed arena. When it is full,
the least recently used *method* map is destroyed and the arena is compacted;
that drops metadata only, never compiled code. A later recompile of the same
(method, pc) finds the surviving code-cache entry and reuses it instead of
emitting a duplicate. Refused sites are never analyzed again until their map is
recycled, and code eviction clears the site's cell so it starts cold again.
Emission can shorten an analyzed prefix to fit the code buffer; the minimum
length is checked again before allocation. Integer division/remainder compiles
inline; a zero divisor is one more guarded instruction, so the interpreter
raises the usual `ArithmeticException` at the original PC.
A 64-entry cache requests at most 128 KiB of code at default limits; OS page
rounding and port headers are additional (POSIX commonly consumes one page per
small block). At default limits the JIT allocates about 11 KiB of host metadata
per VM on RV32 and 12 KiB on LP64 (code entries with fault records, method table
and probe arena); `metadata_bytes` reports the exact size. Metadata belongs to the VM host allocator. Executable storage
belongs to the port and is freed on eviction, `nj_jit_disable()`, or VM
destruction, even when an external engine temporarily replaced the JIT. Cached
bytecode/method metadata must remain immutable, as in the current class loader.

The public `nj_jit_enable`, `nj_jit_disable`, `nj_jit_set_mode`, and
`nj_jit_get_stats` APIs are in `include/nj2me/nj_jit.h`, available with
`NJ_ENABLE_JIT=1`. `NJ_JIT_MODE_EXECUTE` is the default. In
`NJ_JIT_MODE_PROBE_ONLY`, the installed engine still probes, heats and analyzes
sites, but never emits code, calls code-memory hooks or executes native blocks.
Previously compiled code is retained. Enable never replaces an external engine.
Change mode or enable/disable between VM execution entries. The built-in engine
handle remains VM-owned after uninstall; external engines retain their original
borrowed ownership contract.

`NJ_JIT_STATS=1` (default) enables execution/native-bytecode counters, fault
follow-ups and method-map recycling counters. Hot counters use 32-bit pending batches, folded into 64-bit totals when
an engine execution returns, normally at a scheduling quantum. The public getter
includes pending values without forcing a flush, so a same-thread snapshot sees
all completed operations. It is not a cross-thread synchronization API.
`NJ_JIT_STATS=0` removes those per-probe/per-execution updates; compilation,
refusal reasons, code eviction/recompilation, and live/peak code and metadata
accounting remain available. Read `stats_enabled` before interpreting execution
counts. Stats reset on disable/re-enable.

`NJ_JIT_DETAILED_STATS=1` (default `0`, requires `STATS=1`) adds candidate queries,
code hits, cold/negative/probe-only/budget/stack skips and an
execution-weighted length histogram (1, 2–3, 4–7, 8–15, ≥16 bytecodes).
For loop blocks one native call can complete more bytecodes than the block
length; `bytecodes` counts the completion count the block reported, exactly as
the interpreter charges it against the quantum.
`queries` excludes opcodes rejected by the inline filter; `code_hits` includes
cached entries subsequently rejected by a guard. `recompiled_blocks` is a lower
bound because recycling a method map forgets that a site was compiled before;
the code-cache entry itself is reused rather than emitted twice.
Memory byte counts exclude allocator/page overhead. Portable cycle counters are
not provided; use target-specific profiling for compile/execute cycle attribution.
The fixed-workload harness, measurement results and board reproduction procedure
are in [JIT_PERFORMANCE.md](JIT_PERFORMANCE.md).

Ports must implement writable allocation, executable commit (including instruction
cache synchronization), and free with `port->ud`. Alignment is at least 4 bytes;
callbacks must not re-enter Java or mutate engines on the same VM. The existing
POSIX headless port provides W^X allocation. ESP32 currently leaves these hooks
NULL: an ESP32-C3/C6 deployment needs a board-specific executable-memory/cache
implementation before native execution can be enabled; it otherwise falls back.

### Validation

```sh
# Portable encoding checks plus actual generated machine-code execution.
# Requires Zig or the matching Linux cross-compiler and qemu-user (or native Linux).
tools/check_jit_rv32.sh --require-execution
tools/check_jit_arm64.sh --require-execution
# Explicitly allow encoding-only validation on hosts without those tools:
tools/check_jit_rv32.sh --encoding-only
tools/check_jit_arm64.sh --encoding-only

cmake -S . -B build-jit -DNJ_ENABLE_JIT=1
cmake --build build-jit -j
ctest --test-dir build-jit --output-on-failure -R 'test_jit|test_engine|test_lifecycle'
```

`tests/test_jit_rv32.c` compares complete stack/local bytes against an independent
reference, scans emitted instructions for disabled extensions, and checks
capacity guards, inaccessible-page truncation, stack/local bounds, integer edge
cases and floating rounding. Numeric constant-pool cases cover raw int/float
bits, invalid/truncated offsets and unsupported object constants. Analysis and
emission metadata are compared, including capacity rollback and local aliases.
The matrix covers RV32I, all extensions, no M, no A, no F/D, no D, no C, no LONG,
and register caching disabled, plus rejected invalid configurations. It executes
the actual generated buffer under QEMU, rather than simulating encoder output.

`tests/test_jit_arm64.c` checks the same block contract, plus full 64-bit reference
copies, large local offsets, exact slot-byte preservation, BTI entry encoding,
callee-saved registers/native SP and caller FPCR preservation. Floating cases
include nondefault rounding/flush modes, subnormal input/output, NaN payloads and
negative zero. Its seven configurations are base, all, no FP, no SIMD, no LSE,
no LONG and no FLOAT. Invalid nonboolean macros are rejected. The test calls each
accepted capacity-limited prefix through executable memory and compares its
result against an independent reference. Set `ARM64_CC`, `ARM64_CFLAGS` and
`QEMU_AARCH64` to override compiler/runner discovery.

`tests/test_jit.c` checks VM dispatch, stack/divisor guards, quantum accounting,
code-memory failures and teardown on both native targets; `test_lifecycle` checks
two-thread fairness. For full cross-built CTest runs set
`CMAKE_CROSSCOMPILING_EMULATOR` in the toolchain file. QEMU validates instruction
semantics, not physical-board cache coherence or performance. The BTI entry is
executed in the tests, but kernel-enforced guarded pages are not part of that
check. No hardware speedup claim is made by these backends.

**A note on the record.**  `docs/ARCHITECTURE.md:344-414` ("Is a JIT possible?
What was left open, and what was not") deliberately *refused* to add an engine
wrapper back when it was written, on the grounds that a wrapper alone speeds
nothing up and a JIT without a verifier is a footgun.  That refusal was about
shipping speed claims, not about the shape of the code.  The seam was added
later for a smaller reason: the execution entry point should have a name that
describes its role, and an engine interface makes the interpreter's assumptions
explicit and testable.  The costs were kept at zero for default builds (see
[the switch](#2-the-switch-and-what-it-costs)), which is what made the reversal
acceptable.  `ARCHITECTURE.md` still needs a cross-reference to this document;
that file is owned by the architecture notes, not by the engine seam.

---

## 1. Where the seam is

Everything that runs Java enters through `nj_exec(vm, th, stop_depth)`:

```
   embedder / MIDP / native class library
                  │
                  ▼
        nj_vm_call / nj_vm_call_static / nj_vm_start_app
                  │
                  ▼
   vm_invoke()  ── bridge loop ──────────────┐   src/core/nj_vm.c:1198-1505
   nj_scheduler_step() ── scheduler ─────────┤   src/core/nj_thread.c:450-612
                  │                          │
                  ▼                          │
        nj_exec(vm, th, stop_depth)  ◄───────┘   src/core/nj_engine.c:93-103
                  │
        ┌─────────┴──────────┐
        ▼                    ▼
   vm->engine->ops->run()   nj_interp_run()
   (if installed)           (always the fallback)
```

There are exactly two callers of `nj_exec()`:

* `src/core/nj_vm.c:1343` — the C-to-Java bridge loop in `vm_invoke()`.  It
  calls `nj_exec(vm, th, saved_frames)`, where `saved_frames` is `th->nframes`
  at the moment the bridge started.  When the frame count drops back to that
  depth, `nj_exec()` returns `NJ_OK` and the value is in `th->ret_value`.
* `src/core/nj_thread.c:525` — the cooperative scheduler, which runs a ready
  thread with `nj_exec(vm, t, 0)`.

There is also a *nested* path: a native called from Java can call back into Java
through the C bridge, which calls `nj_exec()` again while the first `nj_exec()`
is still on the C stack.  Engines therefore have to tolerate re-entrant `run()`
calls for the same VM and thread (see [§4.4](#44-what-run-must-tolerate)).

The name `nj_exec()` describes the contract, not the implementation: it is
"run this thread until it stops being runnable", and it is the only execution
entry point that other runtime code should use.  The interpreter loop itself is
an implementation detail of the portable path.

---

## 2. The switch and what it costs

`NJ_ENABLE_ENGINE` is defined in `include/nj2me/nj_config.h:450-464` and wired
into the build at `CMakeLists.txt:107` (cache option), `:121` (source list) and
`:207-213` (compile definition list).  The default is `0`.

| | `NJ_ENABLE_ENGINE=0` (default) | `NJ_ENABLE_ENGINE=1` |
|---|---|---|
| `struct nj_vm` | no engine field | one pointer (`src/core/nj_internal.h:599-605`) |
| `sizeof(nj_vm)` (x86-64, Release) | 10,808 bytes | 10,816 bytes |
| `nj_engine.o` (Release, compiled alone) | 101 bytes `.text` | 466 bytes `.text`, 40 bytes `.data.rel.ro`, 0 `.bss` |
| `nj_exec()` body | one tail call to `nj_interp_run()` | read `vm->engine`, call `ops->run`, fall back |
| runtime RAM at rest | 0 bytes | 8 bytes (x86-64) / 4 bytes (32-bit) in the VM |

The `.data.rel.ro` payload in the enabled build is the null engine's ops table
and instances of it; it is read-only after relocation and is explicitly allowed
by `tools/check_no_statics.sh` (which rejects writable `.data`/`.bss` statics).
The enabled build passes that gate: `OK: no writable statics across 57 object
files (2 const-pointer tables in .data.rel.ro, read-only at runtime)`.

*Zero behavior change when off.*  The default build's benchmark output is
identical to the enabled build with no engine installed.  Whatever else an
engine adds (code pages, maps, spill areas) is allocated by the engine itself
and shows up nowhere when no engine is installed.

---

## 3. Execution model facts an engine inherits

An engine is an alternative *implementation* of bytecode execution, not an
alternative VM.  The heap, class metadata, thread objects, frame stack and all
C APIs stay exactly as they are.  The following invariants are the load-bearing
ones; each is enforced today by the location cited.

### 3.1 Value representation

`nj_value` is 8 bytes (`typedef` in `include/nj2me/nj_types.h`).  It holds an
`int`, `float` or reference in its low 32 bits (`.i`/`.f`/`.r`, union members of
the same 4 bytes) and a `long`/`double` in two consecutive 8-byte slots: payload
in the **lower** slot, with the upper slot **always zero** (`PUSH_W` macro,
`src/core/nj_interp.c:605`; `wide` opcode forms zero the high half the same
way).  The GC scans operand stacks untyped and conservatively, so a non-zero
high half would be a false reference candidate; never use it as scratch space.

### 3.2 Frames

`struct nj_frame` (`src/core/nj_internal.h:171-191`) is the interpreter's
visible state for one Java activation:

```
m            const nj_method *   method being executed
pc           nj_u4               bytecode index of the next instruction
locals       nj_value *          argument slots, followed by locals
stack        nj_value *          bottom of the operand stack
sp           nj_value *          current top of the operand stack
stack_limit  nj_value *          stack + m->max_stack (first slot past it)
argbase      nj_value *          == locals; where the arguments start
total_slots  nj_u2               arguments passed (incl. receiver)
monitor      nj_ref              object held by this frame's monitorenter
is_native    nj_u1               1 for a native frame (m->code == NULL)
```

Frames are pushed by `nj_frame_push()` (`src/core/nj_thread.c:282-321`) and
popped by `nj_frame_pop()`/`nj_frame_pop_fast()`
(`src/core/nj_thread.c:323-330`, `src/core/nj_internal.h:781`).  A push from a
caller with `total` argument slots at `sp` establishes:

* `locals = argbase = sp - total`;
* `stack = argbase + max_locals`;
* `sp = stack`;
* `stack_limit = stack + max_stack`;
* locals past `total_slots` are zeroed;
* `th->top = stack + max_stack`.

`nj_frame_push()` is also the overflow gate: it throws `StackOverflowError`
when `NJ_MAX_FRAMES` (`include/nj2me/nj_config.h:291-293`) would be exceeded or
when the frame would not fit below `th->stack_end`.  An engine must use it (or
reproduce it exactly) for every Java callee it pushes; scheduler and bridge
code depend on `th->top` and `th->nframes` being truthful.

### 3.3 `pc`/`sp` must be saved before anything can observe them

The interpreter keeps `pc` and `sp` in C locals for speed and writes them back
with `SAVE()` (`src/core/nj_interp.c:579`); `RELOAD()`
(`src/core/nj_interp.c:580-590`) picks up a callee's state after a call returns.
Every call that can run Java, allocate (and therefore GC), block, or be seen
from C does `SAVE()` first.  `SAVE()` also folds the batched instruction
counter back into `vm->insns` (`INSN_FLUSH()`, `src/core/nj_interp.c:578`):
the count is accumulated in a local (`insns_`, `src/core/nj_interp.c:478`)
and written back on every save/return, so a caller always observes an exact
count between `nj_exec()` calls, but it must never be read from inside the
dispatch loop.  The GC root scan
(`src/core/nj_heap.c:639-694`) is the strict reason:

```
end = fr->stack_limit ? fr->stack_limit : fr->stack + (fr->m ? fr->m->max_stack : 0u);
if (end < fr->sp) end = fr->sp;
for (p = fr->locals; p < end; p++) nj_gc_mark_object(vm, p->r);
nj_gc_mark_object(vm, fr->monitor);
```

It scans the whole slot region from the current `sp` to the frame limit, not the
live values only; conservative marking plus the arena range check makes stale
slots harmless.  For a C-bridge frame (`m == NULL`) `stack_limit` carries the
extent.  **An engine's compiled frames are invisible to that scan.**  Any
reference the engine wants to keep across a safepoint must be written into its
`nj_frame` slot region, or rooted explicitly (`§3.9`).

### 3.4 Invocation and return

`interp_invoke()` (`src/core/nj_interp.c:194-263`) is the call ABI:

* Java callee (`m->code != NULL`): push the frame with `nj_frame_push()` and
  return; the caller reloads and execution continues in the callee at `pc = 0`.
* Native callee (`m->code == NULL`): `nj_invoke_native()`
  (`src/core/nj_interp.c:135-193`) binds the C function once
  (`nj_native_find`, cached in `target->native`), pushes a native frame,
  increments `vm->native_depth` and calls it with
  `args = argbase + (static ? 0 : 1)` — the slot after `this` for instance
  methods.  The native frame is popped before returning.  A native that blocks
  returns `NJ_ERR_TIMEOUT` after setting the thread's wait state; the
  interpreter turns that into `NJ_ERR_YIELD` (`src/core/nj_interp.c:2029-2032`)
  so `run()` never reports `NJ_ERR_TIMEOUT`.

The return path is `DO_RETURN` (`src/core/nj_interp.c:1574-1592`): remember
`fr->locals` (the caller's stack rewind point), save, pop, and
— if `nframes <= stop_depth` — store the value in `th->ret_value` and return
`NJ_OK`; otherwise reload the caller, set `sp = rsp` and push the result (wide
values occupy two slots with a zeroed upper half).  The void-return case
(`src/core/nj_interp.c:1609-1633`) is separate and pushes **nothing**; using
`DO_RETURN` with a zero value there corrupts the caller's stack by one slot per
constructor call.  An engine that returns from a compiled frame into an
interpreted caller has to reproduce this exact caller-side push.

### 3.5 Quantum and yield

`NJ_SCHED_QUANTUM` is 20,000 (`include/nj2me/nj_config.h:364-366`).
`CHECK_QUANTUM` (`src/core/nj_interp.c:607-612`) decrements once per dispatched
instruction; at zero it saves and returns `NJ_ERR_YIELD`.  `stop_depth` is the
frame depth at which `run()` must stop and report `NJ_OK`, even if the quantum
has not expired.

### 3.6 Exceptions

`THROW()` (`src/core/nj_interp.c:599-603`) stores the throwing `pc` in the
frame, then jumps to `do_unwind` (`src/core/nj_interp.c:2226-2243`).  Unwinding
calls `interp_unwind()` (`src/core/nj_interp.c:65-134`), which searches handler
tables from the innermost frame outward, matching `start_pc <= throw_pc <
end_pc`, with `catch_cp == 0` meaning catch-all/finally, otherwise resolving the
catch type and testing `nj_class_is_assignable()`.  On a match it stores the
exception in `f->stack[0].r`, sets `f->pc`, `f->sp = f->stack + 1`,
`f->is_native = 0`, resets `nframes`/`top`, clears
`vm->exception`/`exception_method`/`exception_pc` and resumes.  If nothing
matches above `stop_depth`, `run()` returns `NJ_ERR_HALT` with the exception
still in `vm->exception`, its method and pc in `vm->exception_method`/`pc`.

`struct nj_handler` (`src/core/nj_internal.h:65-70`) is the on-disk exception
table shape (`start_pc`, `end_pc`, `handler_pc`, `catch_cp`); an engine should
reuse it rather than inventing a parallel table.

### 3.7 Inline caches and resolution

Constant-pool entries are `nj_cpentry` (`src/core/nj_internal.h:108-117`):
`cls`, `cp_index`, `target` (resolved field/method/class/string), `recv_cls` and
`ic_target` (the inline cache), plus kind/slot bookkeeping.  The interpreter's
virtual/interface dispatch does one pointer comparison: if
`e->recv_cls != recv->cls || e->ic_target == NULL`, it resolves
(`nj_method_lookup`, with `SAVE()` before `nj_class_init_lazy` because class
initialisation runs Java through the bridge), then writes
`e->recv_cls`/`e->ic_target` (`src/core/nj_interp.c:1908-1971`).  When a class is
(re)resolved, `src/core/nj_class.c:1433-1434` and `:1486-1487` clear those two fields.
An engine that writes the same cache must be prepared for it to be cleared and
must not resurrect a stale target without re-checking `recv_cls == recv->cls`.

Receiver validation is part of the dispatch: `nj_heap_owns_object_fast(vm, recv)`
must hold before dereferencing `recv->cls` (`src/core/nj_interp.c:1904`),
otherwise the interpreter throws `VerifyError`.  There is **no bytecode
verifier** in this VM; the interpreter is the de-facto verifier, checking
operand-stack bounds and `pc` bounds at fetch (`NJ_FETCH_OP`,
`src/core/nj_interp.c:936-966`) and per-opcode type assumptions.  Compiled code
inherits that duty for the instructions it handles.

### 3.8 Monitors

`monitorenter`/`monitorexit` (`src/core/nj_interp.c:2142-2151`) call
`nj_vm_monitor_enter/exit()` (`src/core/nj_thread.c:385-406`), which record the
object in the **current top frame's** `monitor` field.  Scheduling is
cooperative, so there is no lock word and no preemption inside a critical
section; `wait()`/`notify()` set a thread's wait state and are found by scanning
the thread table (`src/core/nj_thread.c:409-445`).  Note that
`NJ_ACC_SYNCHRONIZED` is defined in the class-file format but is referenced
nowhere in `src/`: synchronized *methods* are not implemented, only the bytecode
pair.  An engine must not advertise support for them.

Monitor enter/exit use `vm->cur`.  The scheduler sets `vm->cur` before calling
`run()` (`src/core/nj_thread.c:524`) and `vm_invoke()` assumes it is set for an
existing thread (`src/core/nj_vm.c:1219-1224`); an engine may assume
`vm->cur == th` on entry and must not change it.

### 3.9 The collector, handles, and roots

The heap is a non-moving mark-and-sweep arena (see
`docs/ARCHITECTURE.md:344-414`).  Consequences for an engine:

* Raw references are stable; there is no relocation, no handle indirection and
  **no write barrier**.  A plain object pointer is valid until the object is
  unreachable.
* References are kept alive by being reachable from roots.  The frame-slot scan
  in `§3.3` covers every slot from `locals` to `max(sp, stack_limit)` plus the
  frame's `monitor`.  Anything an engine holds elsewhere (its own spill array,
  a code-site constant table, a CPU register) is **not** a root.
* To hold a temporary object across a call that can allocate, root it:
  `nj_u4 mark = nj_root_mark(vm); nj_root(vm, obj); ...; nj_root_release(vm, mark);`
  (`src/core/nj_internal.h:806-815`).  The cheaper option is to keep it in a
  scan-covered frame slot — which is what the interpreter does.

### 3.10 Threads are green

There is no preemption and no OS thread inside the VM.  The scheduler runs one
thread at a time (`src/core/nj_thread.c:450-612`) and switches only when the
current one yields (`run()` returns `NJ_ERR_YIELD`/`NJ_ERR_TIMEOUT`) or blocks in
a native.  A compiled loop now carries its own bytecode counter and returns to
the interpreter through a budget exit when the quantum is nearly exhausted, so
it cannot starve other threads or defer the bridge's `halt_at_ms` deadline check
(`src/core/nj_vm.c:1345-1356`, which is only re-checked when `nj_exec()`
returns).  The interpreter charges the returned completion count against the
quantum, exactly like interpreted bytecodes.

---

## 4. The engine API contract

### 4.1 Types

```c
/* src/core/nj_engine.h:76-81 */
typedef struct nj_engine_ops {
    const char *name;
    nj_status (*run)(struct nj_engine *engine, struct nj_vm *vm,
                     struct nj_thread *th, int stop_depth);
    void (*destroy)(struct nj_engine *engine);
} nj_engine_ops;

/* src/core/nj_engine.h:89-91 */
struct nj_engine {
    const struct nj_engine_ops *ops;
};
```

An implementation embeds `struct nj_engine` as its first member and recovers
its state with `NJ_ENGINE_FROM` (`src/core/nj_engine.h:98-99`).  The runtime
only ever sees the base pointer.

### 4.2 Install and lifetime

* `nj_engine_install(vm, engine)` — `NULL vm` returns `NJ_ERR`.  The instance is
  **borrowed**, not copied.  Installing affects the next `nj_exec()` call; a
  native that calls back into Java sees the installed engine immediately,
  because dispatch is re-read on every entry.  Installing `NULL` clears.
* `nj_engine_uninstall(vm)` — removes and returns the previous engine.
* `nj_engine_current(vm)` — the installed engine or `NULL`.
* `nj_engine_destroy(engine)` — calls `ops->destroy`; a `NULL` engine or an
  engine without a destroy hook is a no-op.
* External engines remain caller-owned. If an external engine keeps pointers
  into a VM, uninstall and destroy it before destroying the VM. The null engine
  (`nj_engine_null()`) is a shared read-only singleton. The baseline JIT is a
  separate VM-owned allocation: VM destruction frees it even after uninstall
  or replacement, and any previously returned handle then becomes invalid.
* There is one engine slot per VM.  There is no stacking, no fallback chain and
  no notification when an engine is replaced mid-run.

### 4.3 `run()` semantics

`run(engine, vm, th, stop_depth)` executes the top thread until one of these
conditions holds.  It is called with `th->nframes > stop_depth` (the interpreter
returns `NJ_OK` immediately otherwise, and the engine must do the same) and with
`vm->cur == th`.

| return | meaning | state on return |
|---|---|---|
| `NJ_OK` | the frame at depth `stop_depth` has just returned (or `stop_depth` was already reached) | `th->ret_value` holds the return value (`bits = 0` for void); frames above `stop_depth` are gone |
| `NJ_ERR_YIELD` | quantum expired **or** a native blocked | all live frames have current `pc`/`sp` saved; thread state is `RUNNING` for a quantum yield, or the blocked state (`SLEEPING`/`WAITING`) if a native blocked |
| `NJ_ERR_HALT` | an exception escaped every frame above `stop_depth` | `vm->exception` holds it; `vm->exception_method`/`exception_pc` are set |
| `NJ_ERR` | invalid arguments | nothing was executed; no exception is required |

`run()` must never return `NJ_ERR_TIMEOUT` or `NJ_ERR_EXIT`; the interpreter
does not, and the bridge loop maps statuses to its own policy
(`src/core/nj_vm.c:1343-1505`).

If `run()` is installed but `ops` or `ops->run` is `NULL`, `nj_exec()` silently
falls back to the interpreter (`src/core/nj_engine.c:93-103`).  That is a
robustness choice, not a supported way to "half install" an engine.

### 4.4 What `run()` must tolerate

* **Re-entrancy on the same VM and thread.**  A native called from compiled code
  can call back into Java through `vm_invoke()`, which calls `nj_exec()` again.
  The nested `run()` sees the same `vm`/`th` and a larger `nframes`.  Any
  per-thread engine state must be a stack, not a global.  (The interpreter has
  the same recursion via `nj_invoke_native()`; `NJ_MAX_NATIVE_DEPTH` bounds it.)
* **Several VMs in one process.**  The engine instance is installed per VM, so
  sharing one stateless instance is fine; a stateful instance must key state by
  `vm` (and by `th` for thread-local state).
* **Installation changes while running.**  Dispatch is re-read at every
  `nj_exec()` entry.  Do not cache `vm->engine` across a call that can run Java.
* **A frame stack that another engine/interpreter left behind.**  If a hybrid
  engine delegates to `nj_interp_run()`, the interpreter's frames are ordinary
  `nj_frame`s; transfer back through `stop_depth` as described in §5.

### 4.5 Observable equivalence

Whatever an engine does internally, these counters and outputs must not drift:

* the printed program output — the test uses the benchmark fixture's
  `BENCH sum=-136042548 s=5`;
* `nj_vm_stats.insns` (`vm->insns`) — the same total as the interpreter in the
  same build.  The unit is "dispatched instruction", and the superinstruction
  feature is allowed to fuse two opcodes into one dispatch
  (`src/core/nj_interp.c:614-649`), so the number is a
  configured-build property, not a property of the class file: it moves
  whenever a fusion is added or removed (the const+int-arith fusion alone
  removes ~11.2M dispatches from `bench.jar`; the change that introduced this
seam measured 89,716,784 on `bench.jar`, which is a fact about that tree and
not a contract value).  An engine must charge the same
  count as the interpreter for the same source, so the test compares
  engine-vs-interpreter within one build instead of pinning a constant.  An
  engine that deliberately fuses more than the interpreter changes the number
  by design, and that must be documented with the fusion.  An engine that
charges instructions must fold its accumulator into `vm->insns` before every
return to `nj_exec()` (see §3.3); reading `vm->insns` from inside `run()` is
not meaningful;
* `native_calls` and the host allocation accounting around
  `nj_vm_create`/`nj_vm_destroy` (the test compares the released block count,
  990, across runs).

---

## 5. Mixed interpreter/engine execution

The seam dispatches per *thread*, not per method: `nj_exec()` asks the installed
engine to run until the stop condition, and the interpreter loop is not consulted
for individual methods.  An engine may therefore do one of two things:

1. **All-or-nothing per quantum.**  `run()` implements everything it needs and
   falls back to `nj_interp_run()` for whole quanta it cannot handle, for
   example after hitting an unsupported opcode.  The fallback runs until its own
   return condition, which may be deeper than a single method: the interpreter
   will run any Java callees itself, so control returns to the engine only at
   the next `nj_exec()` entry.  This is simple and always correct, but a partial
   engine gets no speedup from methods the interpreter calls.
2. **Per-method transfer with a hook.**  For a real baseline JIT the useful
   design is per-method: compile a method on first execution, run compiled calls
   from compiled code directly, and hand a compiled method back to the
   interpreter when an interpreted caller calls it.  That needs one interpreter
   change: after `nj_frame_push()` of a Java callee in `interp_invoke()`
   (`src/core/nj_interp.c:2028`; the caller's state is already published by the
   `SAVE()` at `src/core/nj_interp.c:2027`), ask the engine whether the callee has compiled code and, if so,
   enter it.  The engine's normal `run()` handles the return.  That hook has not
   been added; it is the first thing a real JIT adds, and it should be a
   compile-time no-op (`#if NJ_ENABLE_ENGINE`) so the default build is untouched.

Transfer rules in either direction:

* **Engine → interpreter.**  Call `nj_interp_run(vm, th, nframes - 1)` to run
  the current top frame and its callees to completion.  When it returns
  `NJ_OK`, the frame returned through `stop_depth`; the engine must perform the
  caller-side push itself (rewind to `fr->locals`, store the return value,
  `sp` advance) exactly like `DO_RETURN`.  If it returns `NJ_ERR_YIELD` or
  `NJ_ERR_HALT`, propagate it unchanged.
* **Interpreter → engine** (future hook).  Before transferring, the calling
  frame must be saved (`SAVE()`); the callee frame was just pushed by
  `nj_frame_push()`, so its `pc`/`sp` are already valid.  The engine executes
  from `fr->pc` and returns through the same paths as any other entry.
* **Deoptimization.**  `nj_method` carries no stack maps and no compiled-code
  pointer.  A compiled frame can always be dropped at a method boundary by
  running the rest of that method in the interpreter.  Mid-method deopt requires
  materialising every live value into the frame slots the interpreter expects;
  the recommended baseline design avoids that entirely by keeping all Java
  values in `nj_frame` slots and using registers only for transient,
  non-reference values (§7).

---

## 6. Safepoints

The interpreter polls once per dispatched instruction (quantum plus the fetch
bounds checks).  An engine may poll far less often, but it must honour these
points:

1. **Return `NJ_ERR_YIELD` at least every `NJ_SCHED_QUANTUM` charged
   instructions.**  Thread scheduling, the `halt_at_ms` deadline and MIDP's
   cooperative event pumping all depend on `nj_exec()` returning.
2. **Before anything that can allocate, GC, link/resolve a class, initialise a
   class, bind a native, take a monitor, or block.**  Mark the frame state with
   `SAVE()`-equivalent semantics first: the collector scans frame slots, and the
   bridge needs a truthful `sp` to place a C-bridge frame above the live stack
   (see the comments at `src/core/nj_interp.c:2004-2009` and `:2021-2027`).
   A native that blocks must set the thread's wait state and return
   `NJ_ERR_TIMEOUT` to its caller, which becomes `NJ_ERR_YIELD` at the
   `run()` boundary.
3. **Before letting a compiled method call a native or return into the
   interpreter**, materialise the state those paths read (`fr->pc`, `fr->sp`,
   and any frame slots the interpreter or GC will scan).

There is no signal-based safepoint mechanism and no stop-the-world handshake; a
safepoint is just "the engine chose to make `run()` return or call back into the
runtime with consistent frame state".

---

## 7. Design notes for a first engine

These are recommendations, not requirements of the current API.  They follow
from the runtime's constraints (512 KB-class MCUs, no verifier, non-moving GC).

* **Code storage.**  `struct nj_port` has `code_alloc`/`code_commit`/`code_free`
  (`include/nj2me/nj_port.h:457-488`) for executable memory with W^X and
  instruction-cache semantics.  They are currently unused by the interpreter.
  Engines should allocate code pages through the port, treat `code_commit`
  failure as "fall back to the interpreter", and free on engine destroy.
  Code memory is **not** part of the VM's host allocation accounting.
* **Method → code mapping.**  Do not add a pointer to `struct nj_method` unless
  the RAM is justified: `nj_method` instances are numerous and live for the
  whole VM.  A side table keyed by the stable `nj_method *` (methods are not
  unloaded) costs nothing in default builds and can grow lazily.
* **Per-thread engine state.**  `th->port_ud` belongs to the port; do not reuse
  it.  A `void *engine_th` in `struct nj_thread` is an option under
  `#if NJ_ENABLE_ENGINE` (32 threads × pointer), or key state in the engine's
  own side table.
* **Baseline, not optimising.**  The recommended first design compiles one
  method at a time into a straight-line sequence of helper calls with all Java
  values in `nj_frame` slots.  That makes deopt, exception unwinding and GC
  interaction trivial, and it needs no stack maps, no register allocator and no
  verifier.
* **Reuse the existing tables.**  Handler tables (`nj_handler`), exception
  class resolution (`nj_cp_class_name`/`nj_class_resolve`/`nj_class_is_assignable`)
  and inline-cache entries are already exposed inside `src/core/`; copying them
  into engine-private formats duplicates policy that is easy to get wrong.
* **Unsupported opcodes.**  Refuse the whole method and keep it interpreted;
  half-compiled methods are a deopt bug farm.  There is a dispatch for every
  opcode today; the risk is malformed class files, which the interpreter
  tolerates by throwing `VerifyError`, not by assuming well-formedness.

---

## 8. Tests and how to run them

The interface test is `tests/test_engine.c`.  It is registered with
`nj_add_test()` next to the existing VM tests:

```cmake
  # JIT engine seam: the null engine must be observationally identical to the
  # interpreter, and an installed engine must actually be entered.
  if(EXISTS ${NJ_FIXTURES}/bench.jar)
    nj_add_test(test_engine tests/test_engine.c ${NJ_FIXTURES}/bench.jar)
  endif()
```

(`CMakeLists.txt`, after the `test_vm` registration at `:940-941`; the engine
test block itself is `:946-948`.  Fixtures are generated, hence the guard,
matching the style used for the other generated fixtures, e.g. `:921`.)

Plain interpreter build (the test compiles and self-skips):

```sh
cmake --build build -j8
./build/test_engine tests/fixtures/bench.jar
# NJ_ENABLE_ENGINE=0: engine interface not compiled in, skipping
# SKIP (0 checks; rebuild with -DNJ_ENABLE_ENGINE=1 to run)
```

Engine build:

```sh
cmake -S . -B build-engine -G Ninja -DCMAKE_BUILD_TYPE=Release -DNJ_ENABLE_ENGINE=1
cmake --build build-engine -j8
./build-engine/test_engine tests/fixtures/bench.jar
```

What the test does, and what it pins:

1. API surface: null engine is one shared singleton with a `name`/`run` and no
   destroy hook; `nj_engine_install(NULL, ...)`, `uninstall(NULL)`,
   `current(NULL)` and `destroy(NULL)` are safe.
2. Runs `bench.jar` four times in fresh 16 MB VMs: no engine (interpreter),
   null engine, a probe engine that wraps `nj_interp_run()` and counts entries
   and yields, and a deliberately malformed engine (`NULL` ops, then ops with a
   `NULL` run) that must fall back silently.
3. Compares, pairwise: the complete captured log string, `startApp` status,
   `stats.insns` (must equal the interpreter's count for this build),
   `stats.native_calls` (`1170`), and the number of host blocks returned by
   `nj_vm_destroy()` (`990`).
4. For the probe engine, asserts it was actually entered (thousands of times,
   with yields) — a dispatcher that silently bypassed the engine would fail.

Expected output ends with (the `insns=` value is whatever this build's
superinstruction set produces; it is the same on every line):

```
baseline    : startApp=0 insns=<N> natives=1170 released=990
null engine : startApp=0 insns=<N> natives=1170 released=990
probe engine: runs=<thousands> yields=<thousands-1> insns=<N> released=990
broken eng. : fell back to the interpreter, insns=<N>
54 checks passed, 0 failed
PASS: null and probe engines are byte-for-byte semantically identical to the interpreter (<N> bytecodes, BENCH sum unchanged)
```

The entry and instruction counts are not contract values — they depend on the
fixture, the quantum and the enabled fusions.  The pinned facts are: the printed
benchmark line is intact, and `insns`, `native_calls`/`released` and the full
program output are equal across the four runs.

Benchmark with the engine-off and engine-on builds (no engine installed; both
must print the same line):

```sh
./build/nj2me        --port headless --heap 16 --fps 0 --stats tests/fixtures/bench.jar
./build-engine/nj2me --port headless --heap 16 --fps 0 --stats tests/fixtures/bench.jar
# BENCH sum=-136042548 s=5
# bytecodes=<same value from both builds, and from every engine in the test>
```

The benchmark fixture exits with status 1 on both builds (pre-existing CLI
behaviour for a MIDlet that stops itself; it is not related to the engine).  The
printed benchmark line is the behaviour comparand; the bytecode total is a
property of the build's fusion set.

Memory gates that the enabled build must keep passing:

```sh
bash tools/check_no_statics.sh
# OK: no writable statics across 57 object files (2 const-pointer tables in
# .data.rel.ro, read-only at runtime)
```

---

## 9. Roadmap, in the order the code can absorb it

1. **Completed: engine seam.** `nj_exec()`, null engine, install/uninstall,
   equivalence test; default builds do not carry engine or JIT state.
2. **Completed: RV32 baseline blocks.** The bounded `(method, pc)` cache and
   dispatch hook described above keep every value in frame slots. Unsupported
   bytecodes use the interpreter. This replaces the original proposal to start
   with per-method helper-call compilation.
3. **Completed: ARM64 baseline blocks.** AArch64 LP64 uses the same cache,
   dispatch, exception and lifecycle contract, with full-width references and
   independently configurable scalar FP. SIMD/LSE lowering remains future work.
4. **Next: x86_64 and more bytecodes.** Add an x86_64 emitter behind the same
   block contract, then extend lowering with result equivalence and
   scheduler/exception tests. Per-method compiled entry and `interp_invoke`
   hooks remain optional later work; they require the transfer contract in §5.
5. **Superinstruction/fusion and IC fast paths.** Preserve the counter
   semantics or update the reference.
6. **Register allocation with stack maps.** Required before mid-method deopt is
   safe; only then do speculative optimisations make sense.
7. **Not now / not promised:** speculative inlining without a verifier, a
   moving collector (would invalidate raw references), preemptive scheduling,
   and a persistent code cache across VM instances.

The gate for every step is the same: `tests/test_engine.c` (and the existing 90
ctest tests) must keep passing, and the default build must keep its zero-cost
property.  A JIT that makes the interpreter build bigger or slower has broken
the contract of this seam.
