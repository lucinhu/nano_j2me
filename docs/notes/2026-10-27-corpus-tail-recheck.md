# 2026-10-27 — corpus-tail recheck: 乱世Q三国, 有女倾城, 3D孤胆枪手2010

Re-check of the three corpus titles that were still oddities in
`docs/VERIFICATION.md` / `docs/COMPATIBILITY_MATRIX.md`. All three are
**title-side**; no runtime defect was found and no runtime code was changed.

Evidence (not committed, per the usual policy):
`.local-tools/debug-runs/corpus-tail-20261027/` — see its `report.md` for the
full command/log inventory.

## What was run

- nj2me: clean out-of-tree Release build of `6be1d0a`
  (`/tmp/nj-head-mascot/build/nj2me`, sha256 `d07cf6d4…cae99c3`), which is
  code-identical to `8099d56` (the only commits between them touch the two
  task-7 docs files). `--port headless`, 240x320 and 176x208, heaps 2/4/8/16 MB,
  300-600 frames, fresh RMS per run, `--stats`, frame dumps.
- reference: local FreeJ2ME-Plus at `baca76a4dda6f30abac67fb4a3db009eed05f91c`
  via `.local-tools/run-freej2me-headless.sh` (wait/shot/press/quit).

## 1. `乱世Q三国_1649` — title-side licence check, confirmed

- nj2me, heaps 16 and 8, 400 frames: `rms.RecordStoreNotFoundException` is
  thrown and caught at `RecordStore.openRecordStore(...) pc=0`, the title then
  prints `授权过期`, calls `notifyDestroyed()`; `frames=0 paints=0 gfx=0
  tris=0`, 128 bytecodes, exit 0. Identical at both heaps.
- reference: `ref-c1/ref.log` contains the same `授权过期` line; the adapter
  then shows its *APP TERMINATED* panel (`ref-c1/01.png`, `02.png`) because
  no canvas was ever painted. The reference also exits without a frame.
- Verdict: the `docs/VERIFICATION.md:46`/`:136` row is **correct**; only its
  evidence pointer was stale (the old `corpus-r3/q3/ref/ref.log` no longer
  exists in this checkout). The row now cites
  `corpus-tail-20261027/ref-c1/ref.log` with today's date.

## 2. `有女倾城破解版_11525` — the title's own 6 MB heap gate, confirmed

- nj2me, 240x320 and 176x208, 600 frames: heaps 2 MB and 4 MB run and paint —
  `frames=600 paints=600 gfx=6323 tris=0`, stable title art (the
  `有女倾城` / “按任意键开始” screen). Heap 8 MB: `notifyDestroyed()` at frame
  0 with 53,901 bytecodes and `frames=0`.
- reference at the desktop JVM heap (> 6 MB): *APP TERMINATED* and no canvas —
  i.e. the reference also self-destroys before painting. So this is the
  title's `Runtime.totalMemory() >> 20 > 6` gate, not a runtime difference.
- Bytecode site (nuance, not a docs error): the gate is not literally inside
  `startApp()`. `startApp()` calls `Display.setCurrent(new b())`; `b.<init>`
  creates class `d` (`new d; invokespecial d.<init>(MIDlet, String)`) and
  `d.<init>` compares `Runtime.totalMemory() >> 20` with `6L` and calls
  `notifyDestroyed()`. `b.run()` bytecode offset 278 is the other
  self-destroy path already documented. Both are title-side.
- Current default retry path (`tools/run_game_corpus.sh`, `HEAP_RETRY=4`)
  now **passes** this title: `176x208, 200 frames, 115 colours, gfx 71733`,
  note “the picture appeared only with a 4 MB heap”. The matrix row #258
  (`frames 0 … 未呈现画面`) comes from the `2606d63` matrix, before that retry
  was added; it is stale and should be fixed by **regenerating the matrix**,
  not by hand-editing one row.

## 3. `3D孤胆枪手2010_4634` — obfuscated-PNG probe plus a PIM decoy, title-side

- nj2me, 240x320, 300 and 600 frames: `frames=300/600, paints=181/291,
  gfx=6380/12020, tris=0 m3g=0`, exit 0, **no uncaught exception**. The 51 and
  135 `thrown:` lines are all caught by the title: `IOException cannot decode
  image: 30119 bytes starting ed 35 28 20` (the `load.png` raw probe) and
  similar raw probes, `NullPointerException`s inside its own `c.a/c.b` decode
  helpers, and one `ArrayIndexOutOfBoundsException at g.c() pc=421`.
- The `IOException` is **expected title behaviour**, not a runtime image-format
  gap: the JAR ships XOR-obfuscated PNGs whose key bytes start at 100 and
  increment mod 256 (`data[i] ^= key; key++`). Decoding `load.png` that way
  gives a valid PNG (`89 50 4e 47 0d 0a 1a 0a 00 00 00 0d 49 48 44 52 …`,
  30,119 bytes). `c.b(String)` tries the raw `createImage`, catches the
  `IOException`, decodes and retries, then falls back to `/il0.png`.
- `javax.microedition.pim` is referenced only by class `f`, instantiated only
  from the NET_Lizard capability probe `d.q()`, inside
  `try/catch` around `Class.forName("javax.microedition.pim.ContactList")`
  (and `f.a()`'s own catch table). The runtime has no PIM implementation
  (`src/` has no `microedition.pim`), so the probe fails **caught** and the
  title continues — decoy, not a gap.
- Visual parity with the reference: both show publisher splash → *NET Lizard*
  loading logo → *SIGMA TEAM* → the `DANGER` corridor with monsters and
  “LOADING 72%” → main menu (SOUND ON/OFF, NEW GAME/OTHER GAMES/OPTIONS/HELP);
  only the wall-clock frame at which each state appears differs. `tris=0`
  confirms the title does not use the Mascot/M3G rasterisers at all (plain
  `Graphics` 2D), consistent with the matrix verdict `运行正常`.

## Outcome

- No runtime code change; no defect to report.
- `docs/VERIFICATION.md` evidence pointer for `乱世Q三国_1649` refreshed
  (claim re-verified on 2026-10-27).
- Open doc follow-up for the Lead: regenerate
  `docs/COMPATIBILITY_MATRIX.md` when convenient so the `有女倾城` row reflects
  the shipped 4 MB heap retry; and optionally tighten the
  `COMPATIBILITY.md:408-415` “from its own startApp()” wording to name
  `d.<init>` (that file is otherwise Lead-owned).
