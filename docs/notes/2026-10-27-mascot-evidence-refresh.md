# Mascot Capsule rasteriser: the two checked-in titles that actually reach it

2026-10-27. Task-7: `docs/COMPATIBILITY.md` claimed Devil May Cry 3D reaches
mission-1 combat and Galaxy on Fire reaches its flight scene, while
`docs/VERIFICATION.md` still said "*no collected title reaches the rasteriser*".
This note is the re-measurement that settles it, and the text the corrected
ledger row points at. No runtime code changed.

## The short version

- At `9d32766`, **two** checked-in titles drive the Mascot software rasteriser
  with JSR 184 disabled (`m3g=0`, so every triangle in `--stats` comes from
  `src/midp/nj_mascot_scene.inc:948`):
  **鬼泣3D中文版_3376** (mission select -> briefing -> mission-1 corridor, enemies,
    sword combat, movement, HUD, death screen and retry) and
  **浴火银河_10189 / _3621** (space flight: ship, station, planet, asteroids, HUD,
    waypoint distances, blaster fire, banking turns).
- **生化危机3D(CAPCOM经典)_3671** is not a rasteriser title: it renders through
  JSR 184 M3G and only uses the Capcom `com.capcom.mascotcapsule` helper classes
  for vector math; it also never leaves its startup data-folder gate
  (`tris=0` in every run).
- Measured triangle totals: DMC 600-frame menu 20; 1,800-frame dense navigation
  377,387; 4,500-frame long play 1,233,810; a 4,500-frame stand-still run that
  dies at the first enemy 2,649,104. GOF 600-frame menu 72,117 and 250,758
  (same byte-identical JAR, different wall-clock phase); 4,500-frame flight
  811,839.
- No uncaught exception, no `NoSuchMethodError` and no `Error` in any run. The
  only throws are the games' own caught ones: DMC's startup `EOFException`
  (`DataInputStream.readUTF`), GOF's fresh-RMS `RecordStoreNotFoundException`,
  and RE3D's `NullPointerException` in `BioData.Dl_CheckDataFolder`.
- Exact triangle totals are **not reproducible run to run**: the guest clock is
  wall time, so frame-numbered key scripts land on slightly different timed
  screens. The old `COMPATIBILITY.md` counts are the right order of magnitude
  and the qualitative claims are confirmed; treat the numbers as scale, not as
  a hash.
- `docs/VERIFICATION.md` open-gap row corrected; the `COMPATIBILITY.md` claims
  were **not** disproved and that file was left to its owner.

## Environment

Clean detached worktree of `9d32766` (`/tmp/nj-head-mascot`), Release, target
`nj2me`, binary SHA-256
`c0fe4836d22e66e1e06308b37147b3dc92bf58e4b295fda412b8e59f98e3930c`. Runs were
serial, fresh RMS each, 240x320, 30 fps, heap 16 MiB, headless:

```
$BIN --port headless --heap 16 --size 240x320 --fps 30 \
  --rms-dir <run>/rms --run-frames N --dump-frames <run>/frames/ \
  --dump-every E --stats [--keys '<frame:key[:down|up],...>'] tests/games/<jar>
```

`--keys` uses `frame:key` for a tap and `frame:key:down` / `:up` for a hold
(`fire` = FIRE, `soft1`/`soft2` = softkeys). Both games poll
`GameCanvas.getKeyStates()`, so a one-frame tap can be missed and holds are
required for movement/turning.

`--stats` counts `tris` in both rasterisers (`src/midp/nj_mascot_scene.inc:948`,
`src/midp/nj_lib_m3g.c:4560` and `:12868`) and `m3g` is `m3g_renders`
(`src/midp/nj_lib_m3g.c:4602`, `:12869`). **`tris > 0` with `m3g = 0` is the
proof that the Mascot rasteriser drew the frame.**

## 600-frame baseline for the four titles that reference the package

| title | frames | paints | gfx | tris | m3g | what frame 600 shows |
| --- | --- | --- | --- | --- | --- | --- |
| 鬼泣3D中文版_3376.jar | 600 | 503 | 32,324 | 20 | 0 | title splash + 新游戏 menu (2D) |
| 浴火银河_10189.jar | 600 | 0 | 54,161 | 72,117 | 0 | logo over a 3D ship/planet menu |
| 浴火银河_3621.jar | 600 | 0 | 55,716 | 250,758 | 0 | same JAR, menu at another animation phase |
| 生化危机3D(CAPCOM经典)_3671.jar | 600 | 301 | 5,049 | 0 | 0 | "A/Loading" then title, data gate |

The two GOF rows are the same file (`浴火银河_10189.jar` = `浴火银河_3621.jar`,
SHA-256 `513d3558…f5618`), so the 3.5x difference is replay nondeterminism, not
content.

## 鬼泣 3D: what the replay reaches

Dense navigation script (taps; the menu sequence is splash -> 新游戏 -> save
prompt -> 任务选择 -> 任务说明 -> gameplay):

```
'300:fire,400:soft1,500:fire,600:soft1,700:fire,800:soft1,900:fire,1000:soft1,
 1100:fire,1200:soft1,1300:fire,1400:soft1,1500:fire,1600:soft1,1700:fire,
 1800:soft1,1900:fire'
```

| run | frames | stats line |
| --- | --- | --- |
| dense | 1,800 | `frames=1800 paints=1325 gfx=331604 tris=377387 m3g=0 time=60.049s fps=30.0` |
| dense | 2,000 | `frames=2000 paints=1466 gfx=352163 tris=333557 m3g=0 time=66.636s fps=30.0` |
| long play | 4,500 | `frames=4500 paints=3337 gfx=757852 tris=1233810 m3g=0 time=149.467s fps=30.1` |
| stand still | 4,500 | `frames=4500 paints=3136 gfx=342603 tris=2649104 m3g=0 time=149.394s fps=30.1` |

The long play walks the scripted route: mission select at 02100, briefing at
02400, gameplay from 02445, enemies and sword swings 02600-02900, left/right
holds move through the dungeon 03000-03700, the **fail screen 任务失败 /
继续? / 是 / 否 at 03800**, respawn into the corridor at 03900 and gameplay again
to 04500. The stand-still run (no input after entering the level) shows the
health bar draining under enemy attacks (01400-01500) and the same fail prompt
at 01600, which then waits forever without input.

Frame deltas over 240x320 = 76,800 px (changed px, mean luminance delta on the
changed pixels, 0-255): enemy attack 4,426 px / 20.75; health drop 39,179 /
20.56; death screen 45,364 / 19.23; sword swings 40,964-47,349 px / 16.6-22.7;
left/right movement holds 2,661-42,694 px / 24.1-24.4; death transition 47,406 /
21.57; respawn 48,256 / 21.32.

## 浴火银河: what the replay reaches

`soft2` is "Weiter" and advances the intro dialogue; the flight script taps it
from 1000 to 3520, then holds left (3600-3900) and right (4000-4300):

```
'700:fire,1000:soft2,1060:soft2,...,3520:soft2,3600:left:down,3900:left:up,
 4000:right:down,4300:right:up'
```

4,500-frame result: `frames=4500 paints=0 gfx=705497 tris=811839 m3g=0
time=149.179s fps=30.2`. Scene order: menu 0500, intro cutscene 0750, hangar
with the player's fighter 1000-2000, mission briefing 2250, flight from ~2300
to 4500 - ship, stations, planet, asteroids, bottom shield/ammo HUD, waypoint
distances ("29.7 km", "13.9 km"), target reticles, German radio/tutorial text,
a blaster shot at 03000, and visible banking under the left/right holds.
Frame deltas: left-bank start 21,748 px / 18.55; left hold 10,080 / 19.32;
right-bank start 11,382 / 17.37; right hold 7,675 / 17.97.

## 生化危机3D: not reached, and not a rasteriser title

600-frame run: `frames=600 paints=301 gfx=5049 tris=0 m3g=0`. With keys the
MIDlet calls `notifyDestroyed()` at frame 501 (`frames=501 ... tris=0`). Log:
14 caught `NullPointerException`s in `BioData.Dl_CheckDataFolder` pc=36 and
`BioData.Get_FolderFile` pc=57 plus one caught `RecordStoreNotFoundException`;
no uncaught throw. The title checks its downloaded data folder with
`getClass().getResourceAsStream("/"+name)` and parks on the install/loading
gate when the folder is absent.

Its renderer is `javax.microedition.m3g`, not Mascot: the class files reference
`Graphics3D.render`, `Mesh`, `SkinnedMesh`, `bindTarget`, `setCamera`,
`Loader.load`, `Image2D`/`Texture2D`/`Appearance` (198 refs / 64 members) and
reference from `com.capcom.mascotcapsule` only `Vector3D`, `AffineTrans`,
`Util3D.sqrt`, `ActionTable` (34 members) — never
`Graphics3D.renderPrimitives`/`renderFigure`. Even if the data gate opened, it
would exercise M3G, not the Mascot rasteriser. (The 13-class/41-member
inventory in `COMPATIBILITY.md` is a package-level count; the split above is
what matters for rasteriser coverage.)

## Replay determinism: why the numbers move

The guest clock is wall time: `src/core/nj_lib_system.c:21-23` answers
`sys_currentTimeMillis`/`nanoTime` from `nj_now_ms`, and
`src/port/posix/nj_port_headless.c:138-156` backs that with
`clock_gettime(CLOCK_MONOTONIC)`. Frames are paced at 30 fps, but loading and
menu screens advance by elapsed time, so a fixed-frame key script lands a few
frames off and the scene phase shifts. Same dense DMC script: 377,387 triangles
at 1,800 frames vs 333,557 at 2,000 frames. Same GOF JAR at 600 frames:
72,117 vs 250,758 triangles on two fresh RMS directories. This is also why the
two old DMC 1,800-frame counts (381,180 and 410,366) differ from each other.

Consequence for the ledger/compatibility text: quote scene/behaviour as the
verified fact and treat triangle totals as order-of-magnitude evidence. If
tighter numbers are ever needed, raise the frame budget and compare scene
reached, not the total.

## Spot-check at 6be1d0a

The task named `9d32766` as HEAD, but four commits landed during the pass
(`ea02f46`, `3cfe46d`, `0f49cb1`, `6be1d0a`: core malformed-class hardening,
tests, docs - no Mascot file touched). The clean worktree was re-pointed at
`6be1d0a` and rebuilt
(SHA-256 `d07cf6d484e12333b3436704ebd7bf5df6f39096bb105fa8880eb7eb3cae99c3`),
then the same scripts were replayed serially: DMC 1,800 frames 330,498
triangles (vs 377,387), GOF 4,500 frames 910,645 triangles (vs 811,839, flight
re-confirmed at frames 02500/03000/03750/04250), 600-frame baselines unchanged
in behaviour (DMC 20, GOF 187,966 / 108,728, RE3D 0). The spread is the same
wall-clock phase effect, not a change between commits.

## Docs outcome

- `docs/VERIFICATION.md` open gap: the "no collected title reaches the
  rasteriser" bullet was stale and is replaced with the verified state plus the
  remaining unobserved DMC paths (pause menu, arena door, bosses, mission
  completion, save/continue) and RE3D's data gate.
- `docs/COMPATIBILITY.md`: both gameplay claims stand on this replay. Its
  evidence pointers to `.local-tools/debug-runs/micro3d-round7-20261022/...`
  name directories that are absent from this checkout; this refresh replaces
  them with `.local-tools/debug-runs/micro3d-head-20261027/` in this note. The
  file itself is Lead-owned and was not edited.

## Evidence

`.local-tools/debug-runs/micro3d-head-20261027/` (not committed, like all
`.local-tools` evidence): `report.md`, `head-9d32766/<run>/` with rms, PPM
frames and `runtime.log`, `head-9d32766/api-usage.txt`, the
`images/*.png` contact sheets and screenshots, `frames` diff tables generated
with `frame_diff.py`.
