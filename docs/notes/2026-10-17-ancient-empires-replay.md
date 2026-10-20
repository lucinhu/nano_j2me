# Ancient Empires II replay and snapshot crash — 2026-10-17

Original JAR: `/home/lucinhu/Downloads/jar/远古帝国2汉化版.jar`, SHA-256 `778b6e881b556e224de19d898cb65bc38c8bb2c910ec0deb04bc2d7713d66393`. Baseline executable: local commit `10b9887`. Experiments use only `.local-tools/debug-runs/ancient-empires-round5/`, with isolated RMS and an 8 MiB Java heap at 30 fps.

## Actual interaction

The original six-second inventory showed only the introduction. Longer replay confirmed that fire advances several pages of Chinese story. The 1,500-frame `second/` run reached the start-game menu at frame 1200 and the mission briefing at frames 1350/1500; its contact sheet was visually checked. A 2,400-frame `campaign/` attempt still ended at the title. A separate 1,800-frame `numeric/` run with numeric `5` instead of `fire` also ended at the title. These runs do not establish battle or unit control, nor a definitive input-code cause. The contact sheets are `.local-tools/debug-runs/ancient-empires-round5/{second,campaign,numeric}/contact.png`. A caught `NullPointerException` in `h.<init>(Ljava/lang/String;I)V` at pc72 and missing-new-store RMS exceptions were observed; neither prevented the title from drawing. Input timing and native key-code handling need further distinction before treating the title as a game hang.

## Confirmed snapshot failure

The campaign run saved `campaign/resume.njs`. Loading it without input for 60 frames returned normally. Loading it and injecting `31:5` crashed as the game deallocated a MIDI player. GDB recorded:

```
nj_midi_stream_close (s=0x55ed9a933430) at src/midp/nj_midi.c:1795
media_stream_release at src/midp/nj_lib_media.c:573
player_deallocate at src/midp/nj_lib_media.c:1839
```

The software MIDI player's `pData` is a Java byte array containing a native stream pointer followed by a scratch buffer. The heap snapshot preserves those bytes but cannot restore that C allocation in another process. Consequently music replacement/cleanup dereferences an obsolete pointer. A platform MIDI player's `pMidi` and PCM `pOpen` likewise represent external resources and require restore sanitation. Snapshot support already excludes host handles; successful loading alone did not prove that the restored state could handle later input.

Reproduction (from the repository root, original JAR unchanged):

```sh
./build/nj2me --port headless --heap 8 --fps 30 --run-frames 600 \
  --load-state .local-tools/debug-runs/ancient-empires-round5/campaign/resume.njs \
  --keys '31:5,92:soft1,153:fire,214:fire,275:fire,336:soft1,397:fire,458:right,519:down,580:fire' \
  --rms-dir .local-tools/debug-runs/ancient-empires-round5/rms \
  '/home/lucinhu/Downloads/jar/远古帝国2汉化版.jar'
```

Before-fix logs: `resumed/runtime.log`, `resumed/gdb.log` (no-input control, normal exit), and `resumed/gdb-input.log` (input-triggered SIGSEGV). GDB exits with status zero after catching this signal; the stack and signal are the failure evidence. Conversely the no-input batch exits status one because `bt` has no stack after normal process exit. Neither GDB exit status alone describes game behavior.

## Fix and post-fix verification

`nj_vm_load()` now walks all rebased arena objects before enabling GC. `nj_media_restore_object()` clears external ownership and old MIDI pointer containers without closing those pointers. This covers Players that never entered the audio pump registry. Started Players become prefetched; native MIDI songs become silent, empty sampled Players so later start/stop/deallocate/close remain usable. Ordinary PCM/WAV bytes and tone sequences remain available. This does not reconstruct an old synthesizer or automatically resume music. Saving leaves the source VM's audio fields unchanged. No new persistent fields, buffers or dependencies are needed.

The exact 600-frame reproduction above was rerun under GDB with a breakpoint on `player_deallocate`: `PLAYER_DEALLOCATE_HIT` was observed, followed by 600 frames, 301 paints, two GCs and a normal inferior exit. This verifies the formerly crashing lifecycle path, not just successful loading. Screenshots show the title, mission briefing and battle map. Evidence: `fixed/gdb-input.log`, `fixed/contact.png`.

A 1,200-frame follow-up used the same initial keys, followed by `660:fire,720:soft1,780:fire,840:right,900:down,960:fire,1020:soft1,1080:fire,1140:soft2`. It advanced to the battle map and a rescue dialogue, with 601 paints, two GCs, 1,338,320 used heap bytes and a normal exit in 40.097 seconds. Evidence: `fixed-map/runtime.log`, `fixed-map/contact.png`. Both runs use an 8 MiB Java heap, 30 fps and separate RMS. The existing caught NPE at `h.<init>` pc72 remains; no uncaught failure was observed. At 240x320 the game still draws in a smaller region with title remnants and misaligned areas. Unit movement, combat and completing a mission are unverified.

SDL2 dummy video/audio also ran the old snapshot for 600 frames and exited normally (`fixed-sdl/runtime.log`), but only 5,006 bytecodes and zero graphics calls were observed: this is insufficient evidence of restored interaction or audio cleanup. Cross-backend snapshot timing remains to investigate. Dummy output supplies no visual proof.

Validation:

- `test_save`: 141 checks, zero failures with built-in MIDI enabled and disabled. Poison native pointers exercise unregistered software/platform Players, preserved PCM/tone data, unchanged source VM fields, restored lifecycle calls and GC. Negative control replacing the sanitation helper with a no-op deterministically crashes in `nj_midi_stream_close → player_deallocate → nj_call`. Logs: `.local-tools/debug-runs/civ5-round5/snapshot-fix/{test.log,test-no-midi.log,test-negative-gdb.log}`.
- ASan/UBSan with leak detection: 141 checks, zero failures and no reported leaks after the headless fix below. Log: `asan-save.log` in this run directory.
- Full final CTest: 84/84 passed, 49.15 seconds; log: `ctest-all.log`.

## Repeated headless display initialization

Sanitizer validation exposed a separate headless resource leak: each VM calling display initialization replaced the port-owned framebuffer with a fresh allocation. Three initializations in the save/restore test leaked two 153,600-byte buffers (307,200 bytes total). The headless port now reuses its existing framebuffer; shutdown remains its owner. `test_headless_fs` checks repeated initialization preserves the pointer, dimensions, stride and live pixel contents. This avoids extra allocations during restore and the sanitizer test now exits successfully with leak detection enabled.

Original JARs and user saves are preserved.

## 2026-10-20 rendering follow-up: fixed logical canvas

A read-only inspection of the original JAR explains the small drawing region and much of the retained title. The bundled [FullCn disassembly](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/ancient-render-round15-20261020/com.hellomoto.fullscreen.FullCn.javap.txt>) has `getWidth()I` return **128** and `getHeight()I` return **116**, both as literal `sipush` instructions. This is not 128×160. `e.<init>(MIDlet)V` copies these values into `e.e:I` / `e.f:I` at PCs 59 / 66, and `a.<init>()V` copies them into scene fields `W` / `X` at PCs 11 / 21. The map later derives its viewport from these fields.

The same original JAR packs `splash.png` as a **132×176** PNG in `1.pak` (offset 69140, length 9480), and `glow.png` as **48×40**. In [i disassembly](</home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/ancient-render-round15-20261020/i.javap.txt>), `i.k()V` loads the splash and glow; the title path `i.e(Graphics)V` draws the splash at `(0,0)` at PC 272 and moving glow at `(4+M,6)` at PC 262 without first bounding the clip to `W×X`. Subsequent map drawing sets its smaller clip at PC 519 of `i.a(Graphics)V`; other scenes fill only `W×X`. The shake translation at PCs 586 / 599 is paired.

Comparing existing 240×320 fixed-map PPMs, the title rectangle `x=0..131,y=140..175` remains **4752/4752 pixels identical** to frame 00120 in frames 00240, 00600, 00720 and 01200, while the logical 128×116 area changes almost completely. This supports the specific mechanism: initial oversized title artwork touches the physical buffer outside the game's reported logical area, then later partial redraws leave those pixels intact. The moving glow also explains the diagonal strip beyond the logical width.

The native paint path creates a new Graphics with a fresh transform/clip on each paint and retains the physical-sized backing image; it does not clear all pixels on every repaint. Static inspection therefore provides no basis for changing global `translate`, `setClip` or partial-redraw behavior to hide this game's outlying title pixels. A fresh boot at physical 128×116 and a same-size reference comparison remain the next discriminating runtime checks; neither was performed in this follow-up. Do not reuse the old 240×320 heap snapshot for a resolution comparison, since it serializes image and display state. These findings explain the version's fixed logical size; they do not establish every rendering primitive as correct or verify campaign movement/combat.
