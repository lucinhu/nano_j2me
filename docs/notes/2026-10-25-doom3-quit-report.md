# Doom 3: closing the window is an end, not a failed construction

*2026-10-25. Fixes the bogus `ERROR: cannot instantiate Doom.Doom:
java/lang/InternalError` printed when the SDL2 window is closed while
`tests/games/毁灭战士3_10598.jar` is running, and the exit status that came
with it.*

## What the user saw

```
$ ./build/nj2me 'tests/games/毁灭战士3_10598.jar'    # then close the window
...
INFO: running Doom.Doom (Doom3D 1.0)
...
ERROR: cannot instantiate Doom.Doom: java/lang/InternalError
INFO: teardown: calling destroyApp()
INFO: teardown: destroyApp() returned
INFO: released 1877 host blocks at teardown
$ echo $?
1
```

Two things are wrong with that line. The MIDlet *was* instantiated and the
whole game ran — `Doom.Doom.<init>` starts its two engine threads and joins
them, so by design a Doom 3 session lives entirely inside the constructor. And
"`java/lang/InternalError`" is not an exception the MIDlet raised: none was
raised at all.

## Where the report came from

`nj_vm_new_midlet()` calls `<init>`; when the frame hook reports that the VM
stopped, that call unwinds with `NJ_ERR_HALT` and `nj_vm_new_midlet()` returns
NULL with an exception set by `take_or_synthesise()`
(`src/core/nj_vm.c:1640`). The runner then checks `nj_vm_stop_why()` first
(`src/runner/nj_run.c:1045-1064`) and prints `run ended: <reason>` when a stop
was recorded; only when no reason exists does it fall into the
`cannot instantiate ...` branch (`src/runner/nj_run.c:1066-1071`). That branch
is correct for a genuine failure — a missing class, a throwing constructor —
but it must not be reached for a run that was asked to stop.

The port's quit request arrives as `NJ_EV_QUIT`
(`include/nj2me/nj_port.h:67`), is polled in the MIDlet event pump
(`src/midp/nj_lib_midlet.c:319-351`) and used to mean just this:

```c
case NJ_EV_QUIT:         vm->running = 0u; break;
```

`midlet_notifyDestroyed()` (`src/midp/nj_lib_midlet.c`), the other
MIDlet-initiated exit, already called `nj_vm_stop_reason()`; the port event path
did not. So the run really had ended, `vm->running` said so, but
`nj_vm_stop_why()` was still NULL and the runner could only guess.

The guess is then made worse by the VM's own state. The synthesised
`java/lang/InternalError(String)` message cannot be attached while
`state == NJ_VM_RUNNING && running == 0`, because building the exception
message is a Java call and `nj_call_special()` refuses calls in that state
(`src/core/nj_vm.c:1262`); `nj_exception_new()` falls back to the bare instance
(`src/core/nj_vm.c:1040-1048`). The reader therefore gets the class name with
no explanation at all — the least informative form of the most misleading
line.

## The fix

One statement, in the `NJ_EV_QUIT` case of the MIDlet event pump
(`src/midp/nj_lib_midlet.c:345-347`):

```c
case NJ_EV_QUIT:
    nj_vm_stop_reason(vm, 0, "the user asked the app to quit");
    break;
```

`nj_vm_stop_reason()` (`src/core/nj_thread.c:723`) is the existing API for
exactly this: it calls `nj_vm_stop()` (records the exit code, clears
`vm->running`, moves the VM to `NJ_VM_STOPPING`) and stores the reason string.
No separate `vm->running = 0u` is needed — and moving the state to
`NJ_VM_STOPPING` is required, not incidental: teardown then calls
`destroyApp()`, and the VM deliberately allows a stopping MIDlet to keep
running Java code (`src/core/nj_vm.c:1253-1263`), which is what lets that call
succeed. The wording is host-neutral ("user"), matching the style of
`the MIDlet called notifyDestroyed()` and `the frontend unloaded the core`
used by the other ports.

The runner was already prepared for this (`src/runner/nj_run.c:1047-1064`):
with a reason recorded, the constructor-time stop takes the
`run ended: <why>` path and exits with the VM's exit code, 0. The fix changes
nothing in `take_or_synthesise()` or in the `cannot instantiate` branch, so
genuine failures still report exactly as before.

## Why a genuine failure that races a quit is acceptable

With the fix the recorded reason is "last writer wins", so consider a MIDlet
whose constructor *would have* failed while the user closes the window at the
same moment. Two orders are possible:

* the constructor throws before any `NJ_EV_QUIT` is polled — `take_or_synthesise()`
  keeps the real exception, `nj_vm_stop_why()` is still NULL and the runner
  reports the genuine failure (`cannot instantiate ...: <real exception>`);
* a quit event is polled first — the run is then a *requested end*. The
  constructor is no longer executing from the VM's point of view; it unwinds
  because the frame hook stopped it, and the exception that "would have"
  occurred later never existed. A user-requested quit is by definition not a
  failure, so reporting `run ended: the user asked the app to quit` and
  exiting 0 is correct, not a lost diagnostic.

This is the same convention as `notifyDestroyed()` and `System.exit()`: once
the MIDlet or the user asks to stop, the run ended on purpose. Do not "repair"
this by preferring the synthesised exception when both exist — that would
resurrect the original bug (a stopped VM in `RUNNING` state fabricates an
`InternalError`) and contradict the other two exit paths.

## Ports and behavior notes

* `NJ_EV_QUIT` is produced by the SDL2 port when the window closes
  (`src/port/posix/nj_port_sdl2.c:1242`) and by the Android port's quit
  (`src/port/android/nj_port_android.c:5563`). Both now record the reason and
  exit 0 — the intended behavior on Android as well (a quit is not a crash).
  The libretro port never sends `NJ_EV_QUIT`; it sets its own reason
  (`the frontend unloaded the core`) before tearing down, so nothing is
  recorded twice.
* Behavior change beyond the log line: the quit event now sets the exit code
  to 0 explicitly. Previously it only cleared `vm->running`, so an earlier
  stop code (e.g. `System.exit(3)`) could survive into the exit status if a
  quit event were polled in the same event drain. That combination now reports
  0, which matches "the user asked to quit". `nj_vm_stop_reason()`'s
  last-wins semantics are unchanged.
* The headless paths are covered too: a `NJ_EV_QUIT` delivered by a test or a
  host frontend (anything calling `nj_port.input_poll`) takes the same code
  path, no window needed.

## Evidence

Same build tree (`build-lead-quit`, Release), same command and script; the
only difference between the two runs is the one statement above. Evidence in
`.local-tools/debug-runs/quit-sdl2-{before,after}/` (`stdout.log`,
`stderr.log`, `terminal.log`, `rms/`; the driver script is
`.local-tools/debug-runs/run-quit-sdl2.sh`, and `quit-sdl2-after/diff.txt` is
the log diff):

| | before | after |
|---|---|---|
| stderr, ctor-time quit | `ERROR: cannot instantiate Doom.Doom: java/lang/InternalError` | `INFO: run ended: the user asked the app to quit` |
| exit status | `1` | `0` |
| teardown | `destroyApp()`, `released 1877 host blocks` | identical |
| stdout | identical | identical |

The run is a real window close: the script starts
`./build-lead-quit/nj2me --port sdl2 --size 240x320 --fps 30 ... Doom3D.jar`
with `DISPLAY=:1 SDL_VIDEODRIVER=x11`, waits until the window exists,
`xdotool windowquit`s it (that is `WM_DELETE_WINDOW`, which SDL2 turns into
`SDL_QUIT`, which the port maps to `NJ_EV_QUIT`), and records the process's
exit status.

## Regression test

`tests/test_quit_stop.c`, registered as `test_quit_stop` next to
`test_blocked_init` (`CMakeLists.txt:1127`) with the shared fixtures
(`--target fixtures`; jars are gitignored). It runs the real runner
(`nj_run`) against fixtures, with a custom `port.log` that captures the
report lines — the same shape as `tests/test_play.c` — and injects
`NJ_EV_QUIT` from a test-owned `input_poll`:

1. **quit during construction** — `blockdemo.jar` sleeps 300 ms in its
   constructor, so the injected quit lands while `<init>` is on the stack.
   Asserts exit 0, `run ended: the user asked the app to quit`, no
   `cannot instantiate`, no `java/lang/InternalError`, teardown follows.
2. **quit after startup** — `flatdemo.jar` is started normally, then the quit
   is injected. Asserts exit 0, the normal line
   `run ended: the user asked the app to quit (exit code 0)`, teardown after
   the run-end line and no `destroyApp threw`.
3. **control, genuine failure** — `--midlet No.Such.Class` must still exit
   non-zero and print
   `cannot instantiate No.Such.Class: java/lang/ClassNotFoundException`, with
   no `run ended:` line.

The test is a genuine regression guard: linked against the pre-fix code it
fails 6 of 14 checks, including the injected `ERROR: cannot instantiate
BlockDemo: java/lang/InternalError` and `runner returned 1`; the identical
binary against the fixed code passes 14 of 14.

```
$ ctest --test-dir build-lead-quit --output-on-failure -R 'quit|blocked_init'
    Start 61: test_blocked_init ...   Passed
    Start 62: test_quit_stop .......   Passed
100% tests passed out of 2
```

`test_text_input` (which also injects `NJ_EV_QUIT` and then forces
`vm->running = 1` to keep exercising key handling) and the runner/play suites
were re-run green; the fix does not disturb a VM that a test un-stops
manually, because the stop reason is only read when the MIDlet is gone.

## Known limitation, out of scope

A quit delivered during the runner's "MIDlet installed no Displayable" grace
window still ends with
`the MIDlet installed no Displayable; nothing to show` and exit status 1:
that branch is taken before the frame loop that pumps `NJ_EV_QUIT`'s effects
into the runner, and it is not the path Doom 3 takes (Doom has a Canvas). It is
a separately misleading report for MIDlets that never show UI; leave it to a
follow-up if it matters.
