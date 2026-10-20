# Motorola softkeys and Ancient Empires II

The explicit `motorola-e398` device profile now delivers Motorola left/right softkey codes (`-21` and `-22`) to raw Canvas callbacks. This lets the original Chinese Ancient Empires II advance dialogue using the intended softkeys. MIDP commands and widgets continue to receive the logical input keys.

## Cause and implementation

The game's bundled `com.nokia.mid.ui.FullCn` adapts input according to handset properties. Selecting Motorola properties while continuing to deliver generic `-6`/`-7` softkeys leaves that adaptation inconsistent. The profile now selects properties and the Canvas key convention together. Setting an arbitrary `microedition.platform` property does not silently opt into different input codes.

`nj_vm_set_device` selects the convention only for `motorola-e398`; another profile or NULL resets it. Later `nj_vm_set_system_properties` calls preserve the selected convention. The CLI passes the selected device through the trailing `nj_run_opts.device` field, and the runner applies it before opening the application and before applying the merged property overrides. This also corrects the earlier CLI path that copied device properties without selecting the input convention.

Canvas translation happens after widget and logical Command dispatch, and after GameCanvas held/latch state updates. Press, release and repeat callbacks use the same convention. Directional and numeric codes are unchanged. `getKeyName` recognizes the Motorola softkeys in that profile; the generic profile retains `SOFTKEY3` for `-21` and rejects `-22`.

## Snapshot behavior

Bit 0 of the existing version-2 header flags at offset 20 stores the softkey convention. Old zero-flag snapshots restore generic input, and unknown flag bits are rejected. The loader applies the restored convention only at the successful end of loading; a later geometry or GC failure must not change the current selection.

Tests restore Motorola snapshots into fresh generic VMs and generic snapshots into Motorola VMs, check actual callback codes, reject unsupported flags, and cover late geometry failure without changing the current convention.

## Validation

- The complete CTest suite passes all 85 tests after the final runtime changes.
- Input tests pass 1,000 checks; snapshot tests pass 683 checks.
- The input and snapshot tests pass with ASan/UBSan and leak detection enabled.
- The existing trimmed configuration builds successfully.
- The real CLI device fixture checks properties plus raw press/release callbacks for the generic convention, Motorola selection, property overrides, and a Motorola platform string without explicit device selection. Its ignored fixture JAR was regenerated for these runs.
- A separate read-only review found no blocking issue in the scoped changes.

## Original-game evidence

The unmodified `/home/lucinhu/Downloads/jar/远古帝国2汉化版.jar` has SHA-256 `778b6e881b556e224de19d898cb65bc38c8bb2c910ec0deb04bc2d7713d66393`. Tests use its bundled FullCn size, 128×116, a 16 MiB heap, `--fps 30`, and isolated copies of test RMS. User saves and original JAR bytes are preserved.

Evidence is retained under `.local-tools/debug-runs/ancient-softkeys-round16/`. `replay-headless` and `replay-sdl2` use the numeric replay from the dialogue translation investigation, then exercise right and left softkeys. Both reach the map dialogue, respond to the right softkey, and display subsequent dialogue. Their framebuffer captures come from an independent local port interposer linked with the production library.

| Run | Frames | Paints | Elapsed | Observed rate |
|---|---:|---:|---:|---:|
| `replay-headless` | 2,604 | 1,304 | 86.235 s | 30.2 fps |
| `replay-sdl2` | 2,604 | 1,304 | 86.595 s | 30.1 fps |

`menu-headless` also saves a VM snapshot at frame 2,460, advances the remaining story with softkeys, and reaches the mission objective and unit-control tutorial. Its initial caught `h.<init>(String,int)` NullPointerException and absent-store exceptions match earlier runs; they do not terminate the game thread.

`restored-headless` and `restored-sdl2` load that snapshot without selecting a device on the command line. Softkeys still advance the tutorial, and both runs reach the game-created save overwrite confirmation. They run 944/941 frames at 30.2/29.7 fps. Repeated downward input during the tutorial encounters a caught `ArrayIndexOutOfBoundsException` at `e.b(II)V pc=5` in both ports; subsequent menus still respond. That game-side boundary case remains to be investigated. The captures do not establish combat, mission completion, or a cold restart from a game-created RMS slot.

## Resource cost

The change introduces no allocation or lookup table. On this host, `sizeof(nj_vm)` remains 10,808 bytes because the one-byte flag fits existing padding. Four affected runtime objects built with identical compiler settings change as follows:

| Object | Before text bytes | After text bytes |
|---|---:|---:|
| `nj_device` | 1,714 | 1,810 |
| `nj_save` | 25,432 | 25,179 |
| `nj_lib_lcdui` | 191,845 | 191,973 |
| `nj_run` | 9,734 | 9,798 |
| Total | 228,725 | 228,760 |

The aggregate text delta is +35 bytes; data and BSS are unchanged. This host/compiler measurement excludes CLI help strings and is not a prediction of another platform's code size. The appended device pointer belongs to the caller's run options and is borrowed for the duration of `nj_run`.
