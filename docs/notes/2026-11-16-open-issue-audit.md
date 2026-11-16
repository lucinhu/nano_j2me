# OPEN-issue audit, 2026-11-16 (round 68)

The `docs/notes/OPEN-issue-*.md` files are append-only incident logs: most of
them open with "Status: unfixed" and later sections record the fix. That is
exactly what happened to five of the six files, which makes them misleading
when read from the top. This audit re-checks every headline item against the
current tree (`dc6e745` + this note), records the evidence, and adds a pointer
line at the top of the files whose headers are stale.

## Summary

| File | Headline item | Status now | Evidence |
| --- | --- | --- | --- |
| `OPEN-issue-stack-slot.md` | one extra operand slot after `invokespecial <init>` | **no longer reproduces** | `gamedemo.jar` run: 29 frames / 2 paints / 540 gfx, `notifyDestroyed`, exit 0, no NPE |
| `OPEN-issue-stack-slot.md` | "the game's frame never reaches the framebuffer" | resolved by the round-15 black-frame fixes | later sections of the same file |
| `OPEN-issue-stack-slot.md` | `maze.jar` AbstractMethodError | **no longer reproduces** | 120 frames / 120 paints / 6168 gfx, no exception |
| `OPEN-issue-stack-slot.md` | MIDP stub generator drops `throws` | stale — generator preserves them | generated stubs contain `addRecord(...) throws RecordStoreException, RecordStoreFullException, SecurityException` |
| `OPEN-issue-stack-slot.md` | malformed-class hangs | **not reproducible with current tooling** | class fuzz 5000x2 all-class clean; JAR fuzz 5000x2 clean |
| `OPEN-issue-flat-frame.md` | titles reported "not drawing" | resolved case by case (menu prompt, harness pacing, field-resolution and paint-target fixes) | file tail: `pet2` needs one soft key; corpus tool now retries another size |
| `OPEN-issue-m3g-render-unreached.md` | Rally M3G scene never renders | resolved for the reported title; remaining item is a game-side cancelled-timer defect | file tail: race, wheel textures and timer provenance |
| `OPEN-issue-interface-object-methods.md` | inherited interface method resolution | resolved (2026-10-16) | header + `tests/test_dispatch` coverage |
| `OPEN-issue-threaded-dispatch.md` | threaded-dispatch VerifyError | resolved (2026-10-21) | header + `test_threaded_dispatch` |
| `OPEN-issue-finished-thread.md` | a `run()` that returned keeps `THREAD_RUNNING`; DEAD reclamation and finished-program detection | **still open** | header, plus the preserved scheduling workaround for `毁灭战士3_10598` |

So there is **one genuinely open item**: finished-program detection / DEAD-state
reclamation in `OPEN-issue-finished-thread.md`. Everything else has either been
fixed or no longer reproduces. The item is deliberately left open: the earlier
attempt to mark returned threads DEAD regressed 毁灭战士3_10598, and a safe fix
needs a scheduler-level distinction between "thread finished" and "scheduler
should stop pumping" (see that file).

## Evidence runs

```sh
# constructor-slot symptom (was: NPE at DemoGame.startApp+41)
build/nj2me --port headless --size 240x320 --heap 16 --fps 30 \
    --run-frames 120 --stats --rms-dir /tmp/audit-gd tests/fixtures/gamedemo.jar
# -> running DemoGame, exit 0, frames=29 paints=2 gfx=540, no exception

# maze.jar AbstractMethodError
build/nj2me --port headless --size 240x320 --heap 16 --fps 30 \
    --run-frames 120 --stats --rms-dir /tmp/audit-mz tests/games/maze.jar
# -> frames=120 paints=120 gfx=6168, no exception

# malformed classes / JARs
python3 tools/fuzz_classfile.py build/nj2me 5000 2 201 all   # clean 5000
python3 tools/fuzz_jar.py       build/nj2me 5000 2 202       # clean 5000
```

The old 18/36 hanging cases from the stack-slot investigation were produced by
the pre-containment fuzzer against an older interpreter; their mutated JARs
were not kept under `.local-tools/`, so they cannot be replayed. The portable
fuzzers that replace them (single- and multi-locus class mutations, any class
in the JAR, whole-JAR container mutations, media decoders) now run thousands of
cases without a crash or a hang. If a hang resurfaces, the recipe in the
stack-slot file (`reproduce under a debug build and tell a loop from a fault`)
is still the right next step.

## Actions

* Added a `Status audit` pointer line under the title of the three files whose
  header is stale (`OPEN-issue-stack-slot.md`, `OPEN-issue-flat-frame.md`,
  `OPEN-issue-m3g-render-unreached.md`); their historical bodies are untouched.
* `OPEN-issue-finished-thread.md` keeps its header and remains the one open
  item; it already states the constraint that the fix must not change scheduler
  idle timing.
