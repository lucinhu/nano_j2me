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

`restored-headless` and `restored-sdl2` load that snapshot without selecting a device on the command line. Softkeys still advance the tutorial, and both runs reach the game-created save overwrite confirmation. They run 944/941 frames at 30.2/29.7 fps.

Follow-up runs under `.local-tools/debug-runs/ancient-rms-round17/save-headless` and `save-sdl2` actually confirm the overwrite, and both show `游戏已存档`. Their independent RMS copies contain the same 128-byte `save.rms`, SHA-256 `12e46dc7e3a6ae90d3453fec187c59a98ca49501c06041a7268990f2b0682b78`. The bytes match the copied initial record because the same turn state was saved again; matching hashes alone are not proof of a changed game state.

`cold-load-headless` and `cold-load-sdl2` then start fresh processes with their respective test RMS copies, explicitly select `motorola-e398`, and do not pass `--load-state`. Both navigate to `读取游戏`, show the populated `SLOT 1/3` with `殿之入侵者` and `current turn: 1`, load the map, move the cursor, and open the map command menu. The submenu uses left/right navigation: after skipping the intro with 64 right-softkey presses, press `5` once, `right` once, `5` to open the slots, then `5` to load the first slot. Down does not change the wheel selection. The headless run completes 1,508 frames in 49.953 s at 30.2 fps; SDL2 completes 1,511 frames in 50.344 s at 30.0 fps. Neither run reports an uncaught exception. This establishes lookup, reading and continued input after a cold RMS restart on both port backends.

The caught `ArrayIndexOutOfBoundsException` at `e.b(II)V pc=5` is an invalid game audio ID. An independent local exception interposer records the call as `e.b(11, 1)` from `i.c()V`, with caller resume PC 5194. Original bytecode at `i.c()` PCs 5188–5191 pushes the literal 11 and invokes `e.b(int,int)`. That method reads `b:[Z` with `baload` at PC 4; this boolean array is sized from the game's nine-entry sound-name table. Its own `java/lang/Exception` handler covers that read and returns normally at PCs 131–132. Subsequent save menus remain usable. Java array bounds should therefore remain enforced; suppressing the exception in the VM would conceal an invalid sound index in the original game. Enemy movement also calls `e.e(I)V` with the literal sound ID 10: `i.c(Lc;)V` pushes 10 at PC 0 and invokes it at PC 2. The callee accesses the same nine-entry `b:[Z` at PC 4, with local `java/lang/Exception` handlers `[0,8)` and `[9,44)` targeting PC 45 (`pop; return`). The observed thrown PC 5 is the position after that failing array read. The original game catches this invalid audio index as well; movement and subsequent turns continue. These audio-exception captures alone do not establish combat or mission completion.

### Movement and combat follow-up

Separate bounded runs in `ancient-rms-round17/combat-*` through `combat9-*` continue from the loaded first mission on both ports. They select the commander, move it from (3,6) to (4,6), end the turn, allow the enemy units to move and reach the next turn with 60 income. After the turn banner finishes fading, they move the commander from (4,6) to (4,4), select `攻击`, target the enemy at (5,4), and play the attack and counterattack animation through its return to the map.

The local interposer reads existing game objects at each framebuffer checkpoint; it does not write game fields or bypass input handling. `combat8-headless` records the commander health changing from 100 to 89 and the targeted enemy from 100 to 44. `combat8-sdl2` records 100 to 89 and 100 to 40 respectively. Those values remain in the `combat9-*` map checkpoints, which also display the next unit-control tutorial. Damage rolls differ between these independent game instances, so these runs are behavioral checks, not pixel-equality comparisons. The other enemy remains at 100 health. Both continuations exit normally, with no uncaught exception.

The final steps use the local checkpoint build linked with the production library and start from each port's own preceding snapshot, retaining the same isolated RMS copies. Controls use 50 ms key presses separated by waits at 30 fps. A first attempt sent direction input while the new-turn banner was fading, leaving the cursor on an empty tile and opening the map menu; the per-step captures exposed that replay error. Waiting for the banner before selecting the commander resolves it without changing emulator input behavior. The earlier fresh-process `cold-load-*` runs establish cold RMS loading separately from these snapshot-assisted combat continuations.

| Run | Frames | Paints | Elapsed | Observed rate |
|---|---:|---:|---:|---:|
| `combat8-headless` | 258 | 129 | 8.589 s | 30.0 fps |
| `combat8-sdl2` | 257 | 128 | 8.854 s | 29.0 fps |
| `combat9-headless` | 290 | 145 | 9.657 s | 30.0 fps |
| `combat9-sdl2` | 289 | 145 | 9.865 s | 29.3 fps |

The framebuffer evidence includes `combat8-headless/03-after-attack.png` for the battle scene and `combat9-headless/02-map-after-combat.png` for the returned map and tutorial, with corresponding SDL2 captures. This verifies one movement/turn/combat sequence and subsequent tutorial progression; mission victory and a complete campaign remain unverified.

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
