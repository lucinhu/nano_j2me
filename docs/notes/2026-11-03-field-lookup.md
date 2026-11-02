# Field / class resolution fast path: measurement and negative result

Status: **not committed as a code change.** The pointer-identity fast path was
implemented, tested and measured; it wins on a gcc -O3 desktop build but does
not reproduce on the target codegen (clang -Os, both desktop and the x86_64
Android APK). Per the task rule ("commit only on a real win, >=1.5% on bench or
>=2% in-world guest CPU") it stays out.

## What was asked

Cut `nj_field_lookup` (2.8% self on the Android in-world profile) and the
`strcmp` validation around it (4.3%) with a bit-exact internal cache keyed by
the constant-pool slot / class pointer, instead of re-walking names per call.

## Where the lookups actually come from

Desktop `-pg` build, 1200 in-world frames loaded from
`.local-tools/mc051/world.state` (the heavy 753K-block world):

* `nj_field_lookup` is called **18,704,901 times** (15.6K/frame).
* Call graph top callers: `vec_elementAt` 8.0M, `m3g_fof` 5.74M,
  `collect_walk` 1.89M, `collect_lights_walk` 991K, `jr_setup` 731K,
  `nj_m3g_collect_meshes` 547K, `m3g_target_bounds` 81K, `ht_prepare` 59.5K,
  `nj_midp_surface` 58K, `nj_midp_image_pixels` 50K, `cam_proj_fill` 41K,
  `img_view` 40K. **Essentially all of them are the M3G/lcdui library code
  walking scene-graph fields per node per frame**, not the interpreter and not
  the game bytecode.
* `nj_class_lookup` is called 3.73M times; 2.74M of those come from
  `vm_invoke()` resolving the owner class of a native call (string literals
  passed by the library code).
* Interpreter-side class resolution is negligible by comparison: `new` /
  `checkcast` / `instanceof` together account for well under 1% of lookups,
  because the constant pool already caches field/method refs; only
  CONSTANT_Class refs are re-resolved, and they are rare in this workload.

The memoised `nj_field_lookup` hit path does three `strcmp()` calls per hit
(owner name / field name / descriptor) as validation. Everything else
(pointer hash, slot) happens regardless.

## Change that was tried (not committed)

`src/core/nj_internal.h` + `src/core/nj_vm.c`: next to
`nj_field   *fld_memo[NJ_FIELD_CACHE_SLOTS];` remember the exact argument
pointer triple (class/name/desc + static flag) that filled each slot. When the
same pointers come back, skip the three `strcmp()`s and return the memoised
field; if any pointer differs, fall through to the existing content validation
and refresh the stored pointer triple on success. A missed memo still resolves
through `nj_class_lookup()` + `nj_field_lookup_in()` as before.

Safety: the fast path assumes the string content behind a pointer never
changes once used as a field/class name. Every call site in this tree passes
either C string literals, class-owned names (`c->name`) or constant-pool
memory (interpreter) — all immutable. The one mutable-buffer caller that
exists (`Class.forName` in `src/core/nj_lib_object.c:526`) goes through
`nj_class_lookup()`, which keeps its full `strcmp` validation and is untouched.
The assumption is documented at the field declaration; a future caller that
builds a field name in a stack buffer and reuses that buffer would have to use
the slow path.

Builds and correctness checks run with the change: both interpreter bodies
still pass `tools/check_superinstr_digest.sh` 27/27 against the desktop JVM
(the fast path is data caching, no opcode semantics changed), and the fixed
benchmark workload consumes exactly the same 62,513,694 bytecodes before and
after.

## Measurements

All desktop numbers: taskset -c 21, 1200 frames, same world state, same JAR.
Baseline binary: clean worktree `/home/lucinhu/workspace/wt/r7-base`
(detached a0fcfc6). Candidate: `/home/lucinhu/workspace/wt/r7-lookup`.

| build | baseline in-world wall | candidate in-world wall | delta |
| --- | --- | --- | --- |
| gcc -O3 (Release) | 3.154 / 3.122 / 3.115 s (mean 3.130) | 3.048 / 3.057 / 3.034 s (mean 3.046) | **-2.7%** |
| clang -Os | 3.312 / 3.287 s (mean 3.300) | 3.284 / 3.330 s (mean 3.307) | +0.2% (noise) |

Fixed-workload bench (`tools/bench.sh 5`, bench.jar, CPU min of 5):

| build | baseline | candidate | delta |
| --- | --- | --- | --- |
| gcc -O3 | 0.168 / 0.170 / 0.166 s min (mean 0.168) | 0.171 / 0.170 / 0.169 s min (mean 0.170) | +1.2% CPU (slightly worse) |

Android x86_64 -Os APKs, in-world guest CPU ms/frame on emulator-5570
(NJ_API21, 320x640, 60 Hz vsync; 20 s `/proc/PID/stat` utime+stime windows,
frameCap at build default, world 753K blocks, load ~14.8 s). Baseline APK
919,705 B (headroom build at main a0fcfc6's code); candidate APK 919,705 B.

| run order | baseline ms/frame | candidate ms/frame |
| --- | --- | --- |
| first after launch (discarded, cold) | 7.025 | - |
| block 1 | 6.308 | 6.425 |
| block 1 | 6.442 | 6.408 |
| block 2 | 6.567 | 6.275 |
| block 2 | - | 6.175 |
| mean | 6.439 | 6.321 (-1.8%) |

The -1.8% is inside the per-run spread (±2%) and per-pair signs are mixed, so
it does not meet the >=2% bar; the clang -Os desktop A/B (the compiler family
the APK uses) shows no effect, so the Android wobble is not attributable to the
change.

Android probe attribution (reused from the round-7 headroom note, same world):
field lookup 2.8% self + strcmp 4.3% + class lookup 0.6% of guest CPU — but as
the call graph above shows, the callers are M3G scene-graph accessors, so any
fix has to make *those* accesses cheaper, not just the resolver's validation.

## Verdict

Not committed: a 2.7% win on gcc -O3 with a 1.2% bench regression on the same
compiler does not transfer to clang -Os (0%) or to the APK on the emulator
(within noise). This is the same pattern as the round-4 load-path result
(gcc win, clang -Os no win) and fits the working rule: only target-codegen
wins count.

## Next candidates (ranked, for whoever picks this up)

1. **Cache the `nj_field*` / resolved offsets inside the M3G accessors**
   (`src/midp/nj_lib_m3g.c`: `m3g_fof`, `vec_elementAt`, `jr_setup`,
   `nj_m3g_collect_meshes`): 15.6K name lookups per frame come from there, so a
   one-time resolution per node/attribute (or a small per-class field slot
   table) removes the whole cost. Out of this task's `src/core` write scope and
   normally landed by whoever touches the M3G library.
2. **Pointer-identity fast path for `vm_invoke`'s owner-class lookup** (2.74M
   lookups per 1200 frames, all `cls` arguments are library string literals):
   same pointer-triple trick inside `vm_invoke`, potentially ~0.5-1% if the
   measurement is repeated properly on the target.
3. **Interpreter CONSTANT_Class cache** (`new` / `checkcast` / `instanceof` /
   `multianewarray`): implementable in `src/core`, but the call graph shows a
   ceiling of well under 1% for this workload.

## Evidence

* `.local-tools/debug-runs/r7-lookup/inworld.sh` - desktop in-world runner
  (pinned CPU, `--load-state`, greps the `stats:` line).
* `.local-tools/debug-runs/r7-lookup/android-inworld.sh` - APK in-world guest
  CPU runner (install -r, launch with jar extras, taps Start Game at
  (160,250) and world card at (160,345), 20 s `/proc/PID/stat` window).
  Note: on this AVD `/sdcard` is read-only (screencap must use
  `adb exec-out screencap -p`) and API21's shell has no `printf`.
* `.local-tools/debug-runs/r7-lookup/android-abase*/`, `android-acand*/` -
  per-run logcat (stats lines, MCJ HUD3D).
* Worktrees: `/home/lucinhu/workspace/wt/r7-lookup` (branch r7-lookup, base
  a0fcfc6) and `/home/lucinhu/workspace/wt/r7-base` (detached a0fcfc6,
  baseline). The candidate source diff was reverted after measurement; the
  patch is not in the tree.
* APKs: `/home/lucinhu/workspace/wt/apk-r7-headroom/base/nj2me.apk`
  (baseline) and `/home/lucinhu/workspace/wt/apk-r7-lookup/cand/nj2me.apk`
  (candidate with the fast path).
