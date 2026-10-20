# Paint interrupted by run stop is not a fault (NJ_ERR_HALT)

## Symptom

`火影忍者汉化版_10735.jar` (corpus game-263) intermittently printed:

```
exception escaped into the frame loop: java.lang.RuntimeException
```

with **no message and no method** — on a run that otherwise succeeded
(the title renders, exit code 0).  The line appeared immediately before
`run ended: the frame budget ran out`.

## Diagnosis

`nj_midp_paint()` (src/midp/nj_lib_lcdui.c) reports a paint that failed
*without* raising an exception as a `RuntimeException`, because the old
code used to fabricate `AbstractMethodError: paint` for every failure
and sent a whole debugging session after a bug that did not exist.
The message said what was known — class + "failed" — but not *which*
`nj_status` came back, so the real cause was invisible.

Threading the status out of `canvas_call_paint()` /
`canvas_call_paint_rooted()` as an `int *why` out-parameter showed it:

```
paint() on k failed without raising an exception (status -3)
```

`-3` is `NJ_ERR_HALT` (include/nj2me/nj_types.h: "thread finished").

That status is not a paint failure.  When the frame budget runs out,
`run_frame_hook()` (src/runner/nj_run.c:652) returns stop; if a paint is
in flight at that moment, `vm_invoke()` (src/core/nj_vm.c) abandons the
call with `NJ_ERR_HALT`.  The run is *over* — there is nothing left to
report the fabricated fault to, and no game code will ever see it.
`gdb` backtrace at the throw: `nj_throw ← nj_midp_paint ←
c_serviceRepaints ← nj_invoke_native ← nj_exec ← nj_scheduler_step`.

The escape is timing-dependent (whether a paint is in flight when the
budget expires): it reproduced on 100f and 450f runs of the same build
and then vanished for several runs — which is exactly why it read as a
flaky game bug instead of a teardown artifact.

## Fix

In `nj_midp_paint()`, when the paint call failed with `NJ_ERR_HALT`,
treat it as the benign stop it is: clear any pending exception and
return `NJ_OK`.  No `RuntimeException` is invented.

Any *other* no-exception failure still reports — now with the status
number appended (`... (status <n>)`) so the next one is diagnosable
from the message alone, without a debugger.

GameCanvas fallback paint (nj_lib_lcdui.c ~5164) passes `why = NULL`;
it clears exceptions by design.

## Verification

- `火影忍者汉化版_10735`: 0 escapes over 100f/450f/900f plus 6×450f
  repeats (previously intermittent).
- `斩妖伏魔录_6231`: its **genuine** escape is untouched —
  `NullPointerException @ c.showNotify+6` still reported.
- `扫雷行动_10904` and `僵尸危机2_4349`: clean runs (exit 0).
- Full suite: 84/84 `ctest` pass (216 s).

No behaviour change for real exceptions; only the fabricated
teardown-time fault disappears.
