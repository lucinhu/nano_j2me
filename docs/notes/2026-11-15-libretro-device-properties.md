# Libretro port: device properties and the handset retry

Date: 2026-11-15
Component: `src/port/libretro/nj_port_libretro.c`

## Symptom

Two titles that work on the CLI frontend failed to load through the libretro
core with the default core options:

- `bubblet.jar`: `thrown: java/lang/NullPointerException at Bubblet.<init>()V
  pc=16: (no message)`, then `cannot instantiate Bubblet: (no exception)`.
- `1945空战英雄_6105.jar`: `cannot instantiate ExtendTorpedoMidlet:
  java/lang/IllegalAccessException`, a load failure that left the content
  unplayable.

Both appeared in the whole-corpus libretro rendering probe
(`tools/check_libretro_corpus_visual.py`) as `FAIL load_game`.

## Root cause 1: the port never answered device properties

`javap -p -c Bubblet.class` shows the constructor's first statement:

```
ldc           #7    // String com.nokia.keyboard.type
invokestatic  #9    // java/lang/System.getProperty(String)
astore_1
aload_1
ldc           #15   // String OnekeyBack
invokevirtual #17   // String.equals(Object)
```

The game probes `com.nokia.keyboard.type` and calls `.equals()` on the result
without a `NULL` check. On a handset the property always exists, so the game
never met the bug. The headless, SDL2, Android and web ports answer these
machine properties in their `port.device_property` callback (headless answers
`None` for the keypad, `1.1` for `com.nokia.mid.ui.version`, and a port-specific
`microedition.platform`). The libretro port did not install that callback at
all, so `System.getProperty` returned `NULL` and the constructor threw.

## Root cause 2: a title that rejects the generic handset

`1945空战英雄_6105.jar` branches on `microedition.platform`. It loads with
`--device nokia6230` (or with only
`--prop microedition.platform=Nokia6230`), and refuses the generic handset on
purpose; the CLR-style access check in `src/core/nj_lib_object.c`
(`Class.newInstance` naming a package-private class) is spec-correct and is not
the bug. The corpus sweep already handles this class of title by retrying
failure/timeout rows once with `--device nokia6230` and marking them `ok` with
a note, but a libretro user has no such retry.

## Changes

1. `lr_port_device_property()` is now installed as `port.device_property`: it
   answers `com.nokia.keyboard.type` → `None`, `com.nokia.mid.ui.version` →
   `1.1`, `microedition.platform` → `nano_j2me/libretro`, and `NULL` for
   anything else. Device profiles still take precedence because the runtime
   consults the selected profile's properties before the port callback.
2. `retro_load_game()` retries a failed load once with the `nokia6230` profile
   when `nano_j2me_device` is still `generic`, logging both the failure and the
   retry. The retry flag is kept for the session so `retro_reset` uses the same
   profile; selecting a device explicitly disables the retry. The retry only
   runs after `lr_load()` has already cleaned up, so no partial state is reused.

## Verification

Mock frontend, `--frames 300 --pace-us 16667 --visual-required --static-ok
--min-nonblack 8`:

- `bubblet.jar`: PASS 35/0 checks (loads with the generic profile now).
- `1945空战英雄_6105.jar`: PASS 35/0 checks; core log shows
  `load failed with the generic handset; retrying with the nokia6230 device
  profile`, then `running ExtendTorpedoMidlet (heap 128 MB, device nokia6230)`.
- Whole-corpus visual probe (`tools/check_libretro_corpus_visual.py`, 373
  titles): **372 passed, 1 failed**. The only remaining failure is
  `毁灭战士3_10598.jar`, whose own loader renders 190 software-M3G scenes before
  its MIDlet constructor returns and cannot finish inside the probe watchdog
  (documented separately in `docs/LIBRETRO.md`).
- Previously the same probe reported `370/373` with the two device titles plus
  the slow loader failing, so this change fixed two real load failures without
  regressions in the other 370 titles.
