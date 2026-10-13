# Architecture

How the runtime is put together, and why each decision was made the way it was.
If you are about to change something in `src/core/`, read the relevant section
first — most of these choices are load-bearing.

## 1. Everything belongs to an `nj_vm`

There is no hidden singleton. `nj_vm_create()` builds a self-contained world;
`nj_vm_destroy()` tears it down and returns the number of host allocations it
released, which callers use as a leak assertion.

Two consequences shape the rest of the code:

- **Entry points take the VM explicitly.** Natives receive it as their first
  parameter, the interpreter and the class library thread it through. There is
  deliberately no `nj_vm_current()`.
- **Read-only tables are `const` and live in `.rodata`** (native registration
  tables, font bitmaps, opcode names), while every mutable byte is reachable
  from the VM struct or from a Java object.

`tools/check_no_statics.sh` enforces this by resolving each symbol's section
rather than trusting `nm` letters: a `const` array of structs containing
pointers cannot go in `.rodata` on a hosted ELF — it needs relocation and lands
in `.data.rel.ro`, which the loader makes read-only afterwards. Both are fine;
`.data`, `.bss`, `.sdata` and `.sbss` are not.

## 2. The heap: one arena, non-moving mark & sweep

Objects are allocated from a single contiguous arena and are **never moved**.
That is the decision everything else is built on: the interpreter and the class
library hold plain C pointers to Java objects, so no handle indirection is
needed anywhere and no read barrier is needed in the hot loop.

```
base ─────────────────────────────────────────────────────► base + bump
[blk][blk][blk][free][blk][blk][free free free][blk] ...
```

Every block begins with a header carrying its own total size, so the arena can
be walked linearly. Live blocks have a magic; free blocks reuse their payload as
a free-list link.

**Allocation**: segregated free lists (exact size class, then larger classes,
then a big-object list), then bump allocation, then a collection and retry.
Large blocks are split when the remainder is worth keeping.

**Sweep** walks the arena once and **coalesces** runs of adjacent free blocks
into a single larger block, rebuilding the whole free structure from scratch.
Without coalescing, a long game session fragments the bump pointer into
uselessness; with it, the arena behaves.

### Conservative frame scanning

The interpreter keeps its operand-stack pointer in a C local and only writes it
back to the frame on a call or an unwind. So a collection triggered in the
middle of an instruction — an allocation inside `newarray`, say — would see a
stale `fr->sp` and free live objects that had been pushed since the last save.

The fix is to scan the frame's **whole slot region** (`locals` through
`stack + max_stack`) instead of up to `sp`. That is sound because
`nj_gc_mark_object()` range-checks every candidate against the arena before
touching it, and objects are 16-byte aligned, so a stale `int` slot cannot be
mistaken for a reference. It costs a few hundred compares per frame per
collection.

The same property is what makes untyped operand slots safe at all: a Java `int`
may hold any 32-bit pattern, and without the range check, walking the stack
would be a wild read.

## 3. Classes: lazy resolution with inline caches

Class bytes are parsed once and **kept alive**, because the constant pool is
resolved lazily. A J2ME JAR may legitimately reference optional APIs this
runtime does not implement; failing only when that code path is actually reached
is far friendlier than refusing to load the class.

The parsed form is compact: the raw bytes plus a `nj_u4` per constant-pool entry
packing `(byte offset << 4) | tag`, and a table of NUL-terminated UTF-8
constants.

### Why there is no vtable

The obvious design — give every class a vtable and dispatch through a slot
index — costs megabytes on a microcontroller: slots are global, so the
last-linked class needs an array as long as the whole program's distinct method
signatures.

Instead, each resolved constant-pool entry *becomes* a monomorphic inline
cache:

```c
typedef struct nj_cpentry {
    nj_class *cls;       /* key: the class whose pool this is     */
    nj_u4     cp_index;  /* key: the pool index                   */
    void     *target;    /* the statically resolved member        */
    nj_class *recv_cls;  /* inline cache: last receiver class     */
    void     *ic_target; /* inline cache: the method it answered  */
    ...
} nj_cpentry;
```

Real MIDlets are overwhelmingly monomorphic — one concrete receiver type per
call site — so `recv->cls == recv_cls` hits almost every time and dispatch costs
one pointer compare instead of a hierarchy walk. A miss walks the class chain
and refills the cache. Measurements on the test suite show a 99 % hit rate, and
the whole cache is bounded and growable.

Field access uses the same cache, storing the byte offset (instance) or the
static slot index.

### Layout computation

`nj_class_link()` resolves the superclass, then lays instance fields out
superclass-first with per-type alignment, and assigns static fields indices into
a flat `vm->statics` array. Final instance sizes are rounded to 16 bytes so a
`long`/`double` field is always naturally aligned.

## 4. The interpreter

The full CLDC 1.1 instruction set, including the parts small JVMs often skip:
`long` and `double` throughout, `tableswitch`/`lookupswitch`, `multianewarray`,
`checkcast`/`instanceof`, `monitorenter`/`monitorexit`, and the legacy
`jsr`/`ret` that older J2ME toolchains still emit for `finally`.

**Value representation.** One operand slot is one `nj_value` (8 bytes). `long`
and `double` consume *two* slots as the JVM requires, but the 8-byte payload
lives in the lower slot and the upper slot is always written as zero. Two
reasons: `dup2`/`dup2_x1`/`dup2_x2`/`pop2` then work unchanged on raw slots, and
the collector's conservative scan cannot mistake a "don't care" upper half for a
pointer.

**Exceptions.** Every instruction that can throw jumps to a `do_unwind` label,
which saves the frame, searches the handler tables of the frames above the call
barrier, and either resumes at a handler with the exception pushed onto a
cleared operand stack, or reports that the thread died. A native signals an
exception by returning non-`NJ_OK` with `vm->exception` set, and signals
"blocked" by returning `NJ_ERR_TIMEOUT`.

**No bytecode verification.** An interpreter cannot execute malformed code into
memory unsafety the way a JIT can, so there is no verifier — but every array
access, cast and stack depth assumption is still checked, and malformed input
raises a Java error rather than corrupting C state.

## 5. Threads: green, cooperative, deterministic

A Java `Thread` maps onto an `nj_thread` with its own Java stack, but **not**
onto an OS thread. The interpreter switches between them every
`NJ_SCHED_QUANTUM` bytecodes.

Mapping onto OS threads would force every thread to share the GC root set, need
real mutexes around every object, and make teardown non-deterministic — none of
which is acceptable on a part with no MMU. Cooperative scheduling gives:

- a single-threaded collector with a trivially enumerable root set,
- byte-identical behaviour on Linux, Android and an ESP32-S3,
- deterministic, complete teardown.

**Blocking.** A native that must block (`Thread.sleep`, `Object.wait`) sets the
thread state and returns `NJ_ERR_TIMEOUT`; the interpreter pops the native
frame, delivers the return value and suspends the thread. No C stack is ever
suspended, so this works on any port. `monitorenter`/`monitorexit` are recorded
but do not lock, because a cooperatively scheduled thread is never preempted
inside a critical section.

**The caller may be C.** A MIDlet constructor is entered from C
(`nj_vm_new_midlet`), so there is no interpreter frame to pop when that
constructor parks. `vm_invoke` therefore pumps the scheduler while the callee
waits - for a timed park until its deadline, and for an untimed one
(`Object.wait()`, `Thread.join()`) until the thread wakes or the call budget
runs out - and the pump ticks the frame hook and honours a stop request like
any other loop. A re-entrancy guard keeps it from being entered twice, because
the scheduler runs Java code that can re-enter C. Without the pump,
毁灭战士3_10598 - whose constructor builds its scene on a loader thread and
waits for it - was reported as `construction ... blocked` and never started.

## 6. The class library is C, so classes are data

`java.lang.Object` and friends do not exist as `.class` files. Each module
declares them as a `const` table:

```c
static const nj_builtin_method string_methods[] = {
    M("<init>", "()V", 0),
    M("length", "()I", 0),
    ...
};
const nj_builtin_class nj_lib_string_classes[] = { { "java/lang/String", ... } };
```

and `nj_class_create_builtin()` materialises the `nj_class` the VM works with;
field offsets and static slots are then assigned by the ordinary linker, exactly
as for a class parsed out of a JAR. Methods become `ACC_NATIVE` with no bytecode
and are bound to their C function on first call.

A subtlety worth knowing: **exception subclasses declare no methods of their
own.** They inherit `Throwable`'s fields and constructors. Declaring `<init>` on
each of the 36 exception classes without a matching binding makes constructing
the exception fail, and constructing *that* failure recurses forever.

## 7. Strings

Java strings are UTF-16 code units with a trailing NUL, so `charAt`/`length` are
O(1) and surrogate pairs behave exactly as Java requires.

There are two representations, distinguished by a flag:

- **embedded** — the payload sits directly after the object header, so a literal
  or a `substring` costs one allocation. This is the common case.
- **wrapped** — `u.raw` points at a Java `char[]`. This exists so that
  `new String(char[])` can install characters into an object the interpreter
  already allocated at a fixed size; the characters are copied, matching Java's
  value semantics.

The collector follows the wrapped form, so the backing array stays alive.

## 8. Where the ports fit

`nj_port` is the only OS-facing surface. The framebuffer contract is
deliberately dumb — a linear buffer plus a stride in bytes plus a `present`
rectangle — because that is all an SPI panel, an `ANativeWindow` or an SDL
texture needs.

## Lessons the test fixture caught

Recorded here because they are the kind of bug that looks like something else
entirely, and a future change could reintroduce any of them.

| Symptom | Cause |
|---|---|
| Loop never exits, no error | Branch offsets are relative to the **branch opcode**, not to the pc after its operands. |
| Caller's stack grows one slot per call | A return must rewind the caller's `sp` to `callee->locals` (the argument base), not to whatever was saved after the arguments were pushed. |
| Callee returns into the wrong instruction | The caller's `pc` must be published into its frame **before** a Java callee is pushed; the interpreter's pc lives in a C local. |
| Live objects freed at random | The collector must scan a frame's whole slot region, because `fr->sp` is stale mid-instruction. |
| Reference truncated to half a pointer | Copying an argument with `.bits` (32 bits) instead of the whole `nj_value`. |
| `UnsatisfiedLinkError` on `<init>` | A native frame pushed twice (once by the caller, once by the native invoker), shifting every later frame slot. |
| `VerifyError` from a `ldc2_w` | `long`/`double` constant-pool entries occupy two slots; the index must be recorded **before** being advanced. |
| NoSuchField on a valid field | The host allocator's user pointer overlapped its own header, so every allocation silently corrupted its own size and magic. |

## Repository map

| Path | Contents |
|---|---|
| `src/core/nj_heap.c` | arena, GC, host-allocation registry |
| `src/core/nj_class.c` | class parser, linker, constant-pool resolver |
| `src/core/nj_interp.c` | the bytecode interpreter |
| `src/core/nj_vm.c` | lifecycle, objects, exceptions, C-to-Java bridge |
| `src/core/nj_thread.c` | green threads, frames, scheduler |
| `src/core/nj_string.c` | string representation, UTF conversion |
| `src/core/nj_native.c` | per-VM native registry |
| `src/core/nj_builtins.c` | materialises the C class library |
| `src/core/nj_lib_*.c` | `java.lang` implementations |
| `src/midp/nj_lib_*.c` | MIDP API implementations |
| `src/midp/nj_midp.h` | the contract between MIDP modules |
| `src/gfx/` | rasterizer, fonts, PNG |
| `src/zip/` | DEFLATE and ZIP |

---

## Is a JIT possible? What was left open, and what was not

Asked directly, and the answer is yes - so this section records which doors are
open, which are shut, and what was deliberately NOT built.

### Already in this design's favour

* **The collector does not move objects.**  `src/core/nj_heap.c` is a non-moving
  mark & sweep over a contiguous arena.  This removes the hardest problem in
  JIT-ing a Java VM: a compiled method can hold raw object pointers in registers
  and on the stack, because nothing will ever relocate what they point at.  A
  moving collector would force either handles, or read/write barriers, or a
  pinning protocol on every compiled frame.

* **There is exactly one execution seam.**  `nj_exec()` has two callers,
  `nj_thread_start()` in `nj_thread.c` and the C bridge in `nj_vm.c`.  A JIT is
  an alternative to that function, chosen at those two sites - not a change
  spread through the runtime.

* **Methods are the unit of dispatch.**  A call resolves once to an `nj_method *`
  and every call site goes through it, so "this method has compiled code" is one
  pointer to add to a structure that is already mutable runtime state.

* **All state belongs to `nj_vm`.**  The project's rule already says where a JIT
  context, a code cache and a compilation queue would live.  Nothing needs to be
  invented; the rule covers it.

### The real blocker, stated plainly

**There is no verifier.**  The operand stack is untyped at run time, which is why
every reference read from it is range-checked before use and why the interpreter
carries a per-instruction bounds check.  A JIT that wants to emit good code needs
to know the types on the stack - and this runtime has never computed them.

That does not make a JIT impossible; it makes *a good one* impossible without
more work.  A **template or baseline JIT** - compile each method's basic blocks
to a fixed sequence of calls into the existing helpers, with no type
specialisation and no register allocation across operations - needs no verifier
at all, because every operation stays dynamic.  It would be a real speedup on
call-heavy code and it is the honest first step.  Anything cleverer needs the
verifier first, and jelatine's is 13,194 lines.

### What was added, and what was refused

**Added: executable memory as a port capability.**  `nj_port.code_alloc` /
`code_commit` / `code_free`.  This is not the same request as the existing
`alloc()`: that returns memory the CPU may read and write, and on every platform
this project targets such memory may not be EXECUTED.  A port that cannot supply
it leaves all three NULL and the runtime falls back to the interpreter.

`code_commit` is where the two details that break naive JITs live: the
instruction cache must be synchronised after writing (ARM, Xtensa and RISC-V all
require it; x86 does it in hardware) and a W^X platform must flip the pages from
write to execute there.

It was added NOW rather than when needed because a port is a published contract -
a capability added later means revisiting every port and breaking every
out-of-tree one.  It cost one line per port and one NULL check in a caller that
does not exist yet.  `tests/test_port_code.c` is that caller: it allocates, writes
machine code, commits, **jumps into it**, and checks the answer, because
compiling an API proves nothing about whether the pages are executable.

**Refused: a speculative `jit` field on `nj_vm`, and an engine wrapper around
`nj_exec`.**  Both would be dead code today.  The first is already implied by the
rule that all state lives on `nj_vm`; the second would add an indirection to the
hottest path in the program to serve a caller that does not exist.  The seam is
the two call sites, and a comment there names them as such - which is worth more
than a function that forwards its arguments.

**Also refused: writing any part of a JIT.**  Not one line.  A half-built JIT is
a second execution engine to keep correct, and this project's value is that its
one engine is right.
