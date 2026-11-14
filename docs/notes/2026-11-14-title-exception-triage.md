# Triage of the three title-exception rows, 2026-11-14

The fixed-binary sweep (`2026-11-14-corpus-regression-sweep.md`) leaves exactly
three `title-exception` rows. All three were adjudicated; one was a real
emulator bug and is fixed.

## 哆啦A梦－3D卡丁车增值版_1560.jar — EMULATOR BUG, FIXED

Symptom: uncaught `java/lang/NumberFormatException` in the MIDlet thread
(frames kept being produced because the paint thread survived), reproduced
identically at `--heap 16` and `--heap 32`.

The MIDlet does `Integer.parseInt(midlet.getAppProperty("FeeMode"))` without a
try/catch. Its manifest line is literally `FeeMode: 0 ` followed by CRLF (one
trailing blank), and `getAppProperty()` handed the blank through.

Fix: `nj_app_get_property()` (src/core/nj_app.c:376) now trims the value after
`nj_manifest_get()` succeeds, matching the AMS behaviour titles are written
against (FreeJ2ME-Plus trims every manifest value). The low-level manifest
reader stays byte-faithful — `test_zip`'s "spaced value" check still passes,
only the MIDlet-facing lookup is cleaned.

New regression test `tests/test_manifest_props.c` (registered as
`test_manifest_props`): trailing blank/tab trim, leading/interior blanks kept,
blank-only value becomes empty, continuation lines, following attribute intact,
case-insensitive name, truncation and NULL/absent arguments — 17 checks.

Verification on the headless build: fresh RMS, 600 frames with the
"download storefront" prompt declined (`.local-tools/debug-runs/subagent-triage-20261114/keys-g2-noexit.txt`)
→ `frames=600 paints=643`, no exceptions, clean `exit status 0`. With the
default sweep key script the title itself chooses the "download" button and
calls `notifyDestroyed()` at frame 300 — that is game-side, not a defect.

## 三国赤壁小乔秘史_7976.jar — GAME/JAR-SIDE

Uncaught `NullPointerException @ j.d+356`: the title selects a resource pack
by screen width (`/oe_176.pak` first) and the repacked JAR ships a stale
`oe_176.pak` that lacks `/value.dat` (only the 240/touch packs have it), so
`m.d()` returns NULL and the title dereferences it. Proof it is not the VM: a
copy of the JAR with `oe_176.pak` removed runs 600 frames with no uncaught
exception and reaches real gameplay (music prompt, then an RPG scene). The
caught pointer NPEs are sweep noise: the title only builds pointer fields for
its touch pack, while the headless port advertises pointer support and the key
script injects pointer presses.

## 愤怒的小鸟-高清版_2353.jar — HEAP/ENVIRONMENT

At the sweep's `--heap 16` it dies with an uncaught
`java.lang.OutOfMemoryError` inside `Manager.createPlayer()`/`Class.getResourceAsStream`
(heap_used ~9.6 MB). At `--heap 32` it is clean: 600 frames, paints 544,
heap_used ~28.9 MB, 2D title (no M3G). This is a heap-size property of the
title, not a VM defect. Note the libretro core's `nano_j2me_heap = auto`
already resolves to `total/16` clamped to **[32 MB, 128 MB]**
(src/port/libretro/nj_port_libretro.c:969), so frontend users do not hit this;
only the sweep's fixed `--heap 16` recipe does.

## Evidence

`.local-tools/debug-runs/subagent-triage-20261114/` — `REPORT.md`, per-game
`logs/*-h16.err` / `*-h32.err`, frame dumps (`frames-g1-no176/`,
`frames-g2/`, `frames-g3-h32/`), raw-property capture `g2-raw-property.txt`.
