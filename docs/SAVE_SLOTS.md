# Save-state slots (CLI + SDL2), 2026-11-25

A game can keep several snapshots instead of one file.  Slots reuse the existing
versioned blob (`nj_vm_save_file()` / `nj_vm_load_file()`), so every slot has the
same format, integrity CRC and version-refusal rules as `--save-state`; nothing
in the runtime format changed.

## Naming and location

```
<dir>/<jar stem>-slot<N>.state          N = 0..999
```

`<dir>` is `--state-dir`, or by default `<jar directory>/states` (created on
first save).  Examples for `loopdemo.jar`:

```
states/loopdemo-slot0.state
states/loopdemo-slot1.state
```

## Command line

```sh
nj2me --port headless --save-slot 0 --save-state-frame 30 game.jar   # write
nj2me --port sdl2     --load-slot 0                                  # resume
nj2me --load-slot 2 --state-dir /path/to/states game.jar             # elsewhere
```

- `--save-slot N` is `--save-state` plus the slot path; `--save-state-frame`
  chooses the presented frame (default: the first boundary).
- `--load-slot N` is `--load-state` plus the slot path.
- `--save-slot` and `--save-state` are mutually exclusive, and so are
  `--load-slot` and `--load-state`: a mistake is a usage error, not a guess.
- Overwrite: save to the same slot again.  Delete: remove the file, or press F9
  in the SDL2 port.  Switching slots: pass a different `N`.

## SDL2 desktop

The port's own keys, served at the top of a frame on the VM thread:

| Key | Action |
|---|---|
| F5 | save the current slot |
| F6 / F7 | previous / next slot (the log names the path) |
| F8 | load the current slot |
| F9 | delete the current slot file |

The runner tells the port where the slots live: a direct
`nj2me --port sdl2 game.jar` run exports `NJ_SDL2_STATE_DIR=<jar dir>/states`,
`NJ_SDL2_STATE_STEM=<jar stem>` and an optional `NJ_SDL2_SLOT` start slot before
the port starts.  `NJ_SDL2_STATE=<file>` still overrides everything with a
single fixed file, so existing scripts keep working.

`F5`/`F8` run at the same frame boundary as the old single-file snapshot; the
load restores the arena and Java stacks in place, which is what the runner's
`--load-state` does before the MIDlet starts.

## Android

The APK keeps its snapshots in the app's private files directory, one file per
slot:

```
<filesDir>/<jar name>.state              slot 0, the original quick save
<filesDir>/<jar name>-slot1.state        slot 1
...                                     up to slot 4
```

- In game, the panel row **保存状态** opens a slot chooser: quick save or slots
  1-4, each showing 空 / 已保存 <time>; saving over a slot asks 覆盖<slot>? first.
  The snapshot is taken by the same `nativeSaveState(handle, path)` call at the
  frame boundary the single-file version used, so no native change was needed.
- The library card's **读取存档** row lists the slots that hold a snapshot for
  that game (with the save time) and launches with the chosen one; the old
  single "恢复进度" row is now this chooser, and legacy `.state` files appear as
  快速存档, so old snapshots still resume.
- **删除存档** clears every slot for the game.
- Emulator verification checklist: save to 快速存档 and to 存档位 1, overwrite a
  slot (confirm dialog), relaunch the game from both slots, delete the archive
  and confirm the slots disappear.

## libretro

Libretro frontends own save-state slots (`retro_serialize`/`retro_unserialize`
plus the frontend's slot directory).  The core deliberately exposes no slot
management of its own; see `docs/LIBRETRO.md` and `docs/LIMITATIONS.md`.
