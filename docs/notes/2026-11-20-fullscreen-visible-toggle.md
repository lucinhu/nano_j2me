# Full-screen mode changes on a visible Canvas (round 129)

The runtime used to throw `IllegalStateException` when a *visible* Canvas
changed its full-screen mode:

```
setFullScreenMode() on a visible Displayable
```

That matches the letter of the MIDP 2.0 note ("the implementation may throw
when the Displayable is visible") but not the phones the games were written
for.  `The Sims 3 Winter Edition` (`the_sims_3_winter_ed_259820.jar`, EA
Mobile) calls `setFullScreenMode(true)` from its paint loop: every paint threw
once, the title swallowed the exception and painted pure black for the whole
run, and the libretro corpus probe flagged it as the only failure of the
510-title batch (`visual_nonblack: max non-black pixels=0`).

Fix: when the Canvas that owns the current Display changes its full-screen
mode while visible, apply the flag, propagate it to `GameCanvas`, schedule a
repaint and return normally instead of throwing.  The ordering guarantee that
`开心农场` needs is unaffected: the mode change from a constructor still
happens before the first presentation, when `F_C_VISIBLE` is still 0 (covered
by `test_fullscreen`).

Evidence (same 240x320 headless recipe, `--fps 30`, 600 frames, sweep keys):

| build | frames | paints | gfx calls | last frames | notes |
| --- | --- | --- | --- | --- | --- |
| before | 602 | 242 | 0 | all black | 722 × `IllegalStateException` |
| after | 600 | 817 | 83 259 | 60 k non-black pixels | main menu renders |

The menu shows the expected `The Sims 3 Winter Edition` screen ("Start Game /
Goals Checklist / Options / Help / Exit"), matching the FreeJ2ME-Plus
reference (which shows the earlier sound-prompt dialog at that time point).

Systematic scan of the 108 fetched titles with the pre-fix binary found the
visible-mode exception in exactly this title (`98` throws in a 240-frame run,
`gfx=0`); all other titles show only real-time scheduling noise between runs
(frames identical, paints ±2, gfx within a few percent).  A new fixture phase
(`tools/make_fsorder_jar.sh`) changes the mode from `paint()` and
`tests/test_fullscreen.c` asserts the new rule, so CTest covers it.

Verification: full serial CTest **199/199 passed in 352.77 s**.
