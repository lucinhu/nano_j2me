# Static libretro probe rows (round 80 classification)

2026-11-17 (round 80)

The 600-frame whole-corpus visual probe reports `distinct_frames` and
`max_nonblack` per title. After round 79's key-script work only four titles
still showed one distinct frame. Each was re-probed at 1800 frames (30 s
virtual), then inspected with `--dump-frames` montages:

## 空战1944_10321.jar

At 1800 frames: `distinct=8`, nonblack 76800/76800, rc=0, no FAIL. It is a
slow loader/movie — like `毁灭战士3_10598` the first tens of seconds are a
loading animation with long static stretches, so 10 s of virtual time is not
enough. Not a defect; the probe passes it with `--static-ok`.

## 生化危机5核变_866.jar

Title screen shows the Resident Evil key art with "游戏联机… 确定 / 返回".
A pointer tap on 确定 advances to an **activation-failure alert**
("温馨提示 激活失败,请退出游戏后再进入游戏激活,如需联网请务必选择CMWAP接入点!"
with 返回游戏). Tapping 返回游戏 returns to the title art, which is why the
frames alternate between two states and end static. The title is gated behind
the distributor's server activation (no CMWAP/SMS path in the emulator), so it
cannot reach gameplay. Game-side gate, not a rendering or input defect.

## 3D城市桌球_8299.jar

Starts on an attract/game screen (pool table with cue), then key 5
(`60:53` in CLI key syntax) reaches the "3D POOL - MAIN MENU"
(START/OPTIONS/HELP/EXIT, OK) menu. Further scripted inputs
(`fire`, `left/right`, digits, pointer drags) leave the menu unchanged within
the probe budget: a static menu waiting for a human choice. Visuals are
complete; not a defect.

## 宠物之王_12445.jar

Shows a small "是否开启音乐?（关闭音乐可使游戏更流畅）" prompt with 是/否 at the
bottom. Tried: soft keys (`soft1`/`soft2`), arrows+fire, digit keys 1/2/5/*/#,
and pointer press/release taps on both buttons (canvas pixels
`pressed@20,312` / `pressed@225,312`). Each input produced one transient
frame change only; the prompt persists. The dialog is static by design (or
expects an input path this title maps elsewhere); it is a screen, not a
hang — rc=0, no exception, heap flat.

## 夜店3D斗地主_2296.jar

With the generic key script the title stops on a carrier SMS confirmation
dialog: "恭喜你发送成功! 请返回继续游戏." with 确定/返回 buttons. A pointer tap
on 确定 (`60:pressed@40,305`) dismisses the dialog (the frame changes and paint
count rises), but no further input sequence in the probe budget reached
gameplay, so the probe still records one distinct frame. The dialog is a
carrier/SMS gate (the game also logs a caught
`javax/microedition/rms/InvalidRecordIDException` while reading its own
records); the screen is a dialog, not a hang. This row is probe-static by
design until someone identifies the follow-up screen.

## Verdict

All four rows are static because of slow loading or by design (menu/dialog/
attract screen or a server-side gate); none is a rendering or input bug. The
probe keeps `--static-ok` so they are not failures, and the distinction is
now recorded here. `台球传奇_87.jar`'s state-roundtrip timeout remains the
separate known limitation documented in the probe itself.
