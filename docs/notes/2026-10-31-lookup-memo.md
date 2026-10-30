# The class and field lookup memos

*2026-10-31*

Minecraft PE's 3D scene walk resolves the same fields once per node per draw:
`m3g_fof()` (in `src/midp/nj_lib_m3g.c`) calls `nj_field_lookup()` with string
literals, and `nj_field_lookup()` calls `nj_class_lookup()` first.  Both walked
the class table and the inherited field chain on every call.  This note is the
measurement that quantified the cost, the design that removed it without
trading correctness for speed, and what was rejected.

## What the time actually went into

`perf` and `valgrind` are not installed here and `gdb -p` is refused
(`kernel.yama.ptrace_scope` is 1), so the cost was counted, not sampled:
temporary counters in `nj_class.c` and `nj_vm.c` (instrumentation not
committed).  1200 in-world headless frames of a saved world at 240x320, pinned
with `taskset -c 5`:

| counter | 1200 frames | per frame |
| --- | --- | --- |
| `nj_class_resolve()` calls | 26,982,794 | 22,400 |
| ... of those, ones that walked the class-table hash | 988,510 | 824 |
| `nj_field_lookup()` calls | 16.8 M | 14,000 |
| ... from `m3g_fof()` | 8.9 M | 7,400 |

The callers of `nj_class_resolve()` in a 200-frame window were almost all
`nj_class_lookup` (4,492,422 of 4,498,000 calls); the interpreter's own
resolve sites (`checkcast`, `instanceof`, `multianewarray`, `catch` handling)
accounted for 487.  So the cost is the renderer's: the per-call overhead of two
lookups × 30-40 cycles × 22,400 a frame, which the round-2 profile put at 8.7%
(`class_resolve_from_source`) plus 5.7% (`field_lookup_walk`), about 14%.

## The change

Both lookups now keep a memo in the VM (per-instance, so no writable globals
and no `.bss`):

* `nj_field *fld_memo[NJ_FIELD_CACHE_SLOTS]` (1024 entries, 8 KB) and
  `nj_class *cls_memo[NJ_CLASS_CACHE_SLOTS]` (128, 1 KB);
* the slot is chosen by hashing the pointer arguments - the names the caller
  passed - because every hot call site spells them as literals;
* **a hit is only taken when the answer still carries the names asked for**:
  `strcmp(f->owner->name, cls)`, `strcmp(f->name, name)`, `strcmp(f->desc,
  desc)` and the static flag must all match, and a class hit compares
  `strcmp(c->name, name)`.  A caller that reuses one buffer for different names
  falls through to a real lookup instead of getting the previous name's answer;
* only successful lookups are stored, and `desc == NULL` (name-only match)
  bypasses the memo rather than inventing an empty descriptor;
* a snapshot restore clears both tables, and `nj_vm_create()` starts from
  zeroed memory, so a VM built over a destroyed VM's memory cannot answer with
  a stale class pointer.

`--stats` now reports `findcache=hits/misses` from `nj_vm_stats.find_hits` /
`find_misses`; `nj_field_lookup()`'s public comment in
`include/nj2me/nj_vm.h` describes the pointer fast path and the text check.

Both table sizes are CMake options (`NJ_FIELD_CACHE_SLOTS`,
`NJ_CLASS_CACHE_SLOTS`, powers of two), traded against RAM: at the defaults the
two tables add 9,232 B to `sizeof(nj_vm)` on a 64-bit build (measured
7,040 -> 16,272 B in this tree's Release build, JIT off; a JIT build adds its
own tables on top), or 4,624 B on a 32-bit build (ESP32: 5,368 -> 9,992 B
estimated the same way).  `tools/make_bare_firmware.sh` subtracts
`nj_vm_sizeof()` from the Java arena, so a trimmed build can hand the bytes
back with `-DNJ_FIELD_CACHE_SLOTS=512 -DNJ_CLASS_CACHE_SLOTS=64` (4,624 B on
x86-64 instead of 9,232; measured 1-2% slower on the world benchmark, and the
option exists for targets where the RAM matters more).

## Measurements

Same saved world and command line, 1200 headless frames, `taskset -c 5`, user
CPU from bash `time`, alternating runs against the previous commit's binary:

| run | before (fog fast paths) | after | delta |
| --- | --- | --- | --- |
| 1 | 2.705 s | 2.433 s | -10.1% |
| 2 | 2.713 s | 2.447 s | -9.8% |
| 3 | 2.725 s | 2.474 s | -9.2% |

Miss rate: 1.63 M misses over 1200 frames at 1024/128 slots (a run of this
world touches more distinct name triples than the 516 seen in a first
200-frame window); raising the field table to 4096 slots dropped misses to
239 K with no measurable CPU difference, so the misses are not the cost - the
per-call walk they replace is.  `--stats` on the same run:
`findcache=26719294/1627233`.

The rendered output does not move.  A frame dump of the same state differs from
the previous binary only in the sky/cloud band (Minecraft animates clouds from
the wall clock; the same unmodified binary differs from itself by the same
amount there), and the near terrain, ground and hotbar are pixel-identical.
`check_m3gdemo`'s pixel checks and the m3g suite stay exact.

## Rejected

* A module-local cache inside `m3g_fof()`: 256 direct-mapped entries, 77
  distinct pointer triples, hit/miss 1.88 M/350 K over 300 frames, best case
  2% and inside noise.  The cost was two lookups per call, not just the field
  walk, so the class memo had to move too.
* Enlarging `NJ_CLASS_FIND_SLOTS` (4 -> 32): no measurable difference.
* Caching on the pointer alone without the text check: that is the bug
  `tests/test_consts.c` documents, where a reused line buffer read back the
  first field for every later name.

## Tests

`tests/test_lookup_cache.c` (15 checks, `--target test_lookup_cache` or the
`test_lookup_cache` ctest entry) pins the correctness side: a repeated literal
hits the memo and returns the right class/field; a reused buffer whose text
changes misses; a static request does not match an instance field with the
same name; a NULL descriptor matches by name (the sensor suite caught the
regression when this path went through the empty-descriptor branch); the
counters move; a fresh VM resolves classes without the old VM's answers.

Gates: full ctest 139/139 with fixtures, corpus gate `fatal 0 / 400, known 4`.
