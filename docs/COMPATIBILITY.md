# Compatibility

What a J2ME title can rely on, what it cannot, and why. This file is meant to be
read *before* debugging a game: most "the emulator is broken" reports turn out
to be one of the gaps listed here, and knowing which one saves an afternoon.

## Profiles and configurations

| | Status |
|---|---|
| CLDC 1.1 (JSR 139) | implemented |
| CLDC 1.0 | implemented — the runtime is a superset; class files with major version 45–48 load unchanged |
| MIDP 2.0 (JSR 118) | implemented, see the API table below |
| MIDP 1.0 | implemented — every 1.0 call exists in 2.0 |
| JSR 75 (FileConnection/PIM) | `FileConnection`, sandboxed to `--fs-dir`, and `RecordStore`; the PIM half (`ContactList`) is not implemented |
| JSR 82 (Bluetooth) | implemented, on ten port hooks (`nj_port.bt_*`): `LocalDevice`, `DiscoveryAgent.startInquiry`/`searchServices`, `UUID`, `DeviceClass`, `DataElement`, `ServiceRecord`, `RemoteDevice`, and RFCOMM `btspp://` / L2CAP `btl2cap://` connections both ways — a client through `bt_connect`, a server through `bt_listen/accept/read/write`. Any of the hooks may be NULL, and then that URL is refused by name. With no hooks at all (or a radio the port cannot reach) every operation raises `BluetoothStateException`, because a MIDlet that is told "no radio" keeps working and one that is told "no such class" does not. `DataElement` covers the int/boolean/String/UUID/sequence types; `L2CAPConnection` is a byte stream with the default 672-byte MTU |
| JSR 256 (Sensor) | implemented: `findSensors`, `getSensorConnection`, channels, measurement ranges and data listeners, with the sensors themselves supplied by the port (`nj_port.sensor_list/open/read/close`). A port that has none reports none. Conditions are not evaluated — `addCondition` raises `UnsupportedOperationException` — and availability push answers `false` |
| JSR 120/205 (messaging) | classes present; connections refuse the same way |
| JSR 135 (Mobile Media) | tone and sampled PCM playback, WAV from a stream or a JAR resource, MIDI rendered by the runtime itself (or by the platform's synthesizer through the port's MIDI hooks), and AMR narrowband decoded by the runtime itself; no video. MP3, AAC and anything else the platform can decode arrive through `nj_port.audio_decode`, which is what the Android port does with MediaCodec |
| JSR 184 (Mobile 3D Graphics) | the classes, the scene graph, the transform maths, a `.m3g` container parser, the member surface real titles use, and a software rasteriser with a depth buffer, perspective-correct texturing (nearest, bilinear, and a level chain built from the image for minified surfaces), back-face culling and per-vertex lighting — all behind `NJ_ENABLE_M3G`. `浴火银河2完整版_7689` renders its 3D scenes through it (thousands of triangles, `tris=` in `--stats`), and `3D拉力赛-进化汉化版_6366` now draws its race: the car with its livery, the road under it and the HUD on top, 62,083 triangles in 1,500 paced frames (`docs/images/rally3d-race.png`); the rest of the collected titles keep their 3D behind menus, so each of them is judged by its 2D interface (see below) |
| Mascot Capsule Micro3D v3 | classes, fixed-point transform maths and a real triangle rasterizer, behind `NJ_ENABLE_MASCOT`. **No collected title reaches the rasterizer**, so it is exercised only by its own unit test (see below) |

Anything not implemented raises. That is deliberate: a
`java.lang.NoSuchMethodError` naming the method is a five-minute fix, whereas a
silently-ignored call is a two-day one. The one exception is a radio that does
not exist on the host: those classes are present and refuse at run time,
because "this device has no Bluetooth" is a normal answer a MIDlet is written
to handle, while "this build has no Bluetooth class" is not. A sensor is the
same story one step further: the package works, and what a port cannot see is
simply not in `findSensors()`.

Modules a platform may want to leave out entirely are switched off with a
compile-time macro rather than by editing the tables, so the same source builds
for a microcontroller and for a desktop. `NJ_ENABLE_M3G` is the first of these;
see `include/nj2me/nj_config.h` for the list and what each one costs.

## Class file support

The loader accepts major versions 45 through 49 and does not verify. No
verifier is needed because an interpreter cannot be talked into memory
unsafety the way a JIT can — but every array access, cast and stack-depth
assumption is still checked at run time, so malformed input raises a Java error
rather than corrupting C state.

## Verified titles

`tools/fetch_test_games.sh` pulls the published binaries for *J2ME Games with
MIDP 2* (Apress, 2004), built by javac 1.4 targeting CLDC 1.0 - class file
version 46, with the legacy jsr/ret finally blocks a 2004 toolchain emitted.

Every title is run by `tools/run_game_corpus.sh`, which tries a list of screen
sizes per title and records which one worked - the gates differ, and a title
that rejects one size is not a broken title. Tumbleweed is the strict one:

    if (DISP_WIDTH < 150 || DISP_HEIGHT < 170) throw new Exception("Screen too small");
    if (DISP_WIDTH > 250 || DISP_HEIGHT > 250) throw new Exception("Screen too large");

so it wants 176x208. It renders its own error Alert, correctly, at anything
outside that - which is how the gate was found.

| Title | Class | Size | Result |
|---|---|---|---|
| Tumbleweed (`jump.jar`) | `net.frog_parrot.jump.Jump` | 176x208 | **plays**: scrolling ground, an animated sprite, a live score and a countdown - [screenshot](images/tumbleweed.png) |
| Maze (`maze.jar`) | `net.frog_parrot.maze.Maze` | 176x208 | **runs**: generates and draws a maze with the player marker - [screenshot](images/maze.png) |
| Hello (`hello.jar`) | `net.frog_parrot.hello.Hello` | 176x208 | **runs**: draws "Hello World!" (a legitimately static screen) |
| **Bubblet** (`bubblet.jar`) | `Bubblet` | 176x208 | **plays**: a full bubble-popper grid, and the only title here that is not from the Apress book - see below |
| Dungeon (`book{5,6,7}.jar`) | `net.frog_parrot.dungeon.Dungeon` | 176x208 | **plays**: a platformer with tiled walls, keys, a door and a timer - [screenshot](images/dungeon.png) |
| Platformer (`book4.jar`) | `net.frog_parrot.jump.Jump` | 176x208 | **plays**: the same Tumbleweed game at a later stage of the book |

**Bubblet is built from source, not fetched.** `tools/make_bubblet_jar.sh`
clones [Summeli/Bubblet-Asha](https://github.com/Summeli/Bubblet-Asha) (GPL v2,
~900 lines, a Nokia SDK 2.0 project) and compiles it against this project's MIDP
stubs. That proves something a prebuilt JAR cannot: that the API surface this
runtime implements is the one javac resolves against, so a missing or
differently-described method is a compile error rather than a NoSuchMethodError
found later by a player. It found five things, all fixed, and two more that are
recorded in docs/notes/OPEN-issue-stack-slot.md.

`tools/run_game_corpus.sh` is the gate over all of them: it fails on "not a
MIDlet suite", "could not start", an uncaught exception, "nothing ever
presented" or a single flat colour, and reports - without failing - whether the
picture changes, because a title is allowed to be static. Current result: 7
passed, 0 failed.  The later chapters all build to `book.jar`, so they are
stored by chapter.

A sweep runs the titles **uncapped** (`--fps 0`), so 600 frames can cost a tenth
of a second of wall clock - and a title whose own loop is paced by
`Thread.sleep` has barely executed when its pixels are read. That is a claim
about the harness, not about the title: `开心农场_240` was written down as a
blank screen (0 colours, 4 Graphics calls, 1750 bytecodes over 600 frames) and
draws a 61-colour screen once its frames are 33 ms apart. `纯爱物语3之梦想之翼_5112`,
`新三国志_10225` and `PC正版-使命召唤5_7199` are the same story. So a verdict
that still has no colour on screen after the longer re-run is checked once more,
paced at `NJ_PACED_FPS` (default 30 - the frame rate of the phones being
imitated) for `NJ_PACED_FRAMES` (default 300, ten seconds of the game's own
time), and the row records which pace its verdict came from: "the picture
appeared only when the run was paced at 30 fps" or "still flat when paced at
30 fps". `--where` is the diagnostic that explains such a row, since it names
the frame the game's own loop is parked in (`af.a()V pc=30 / ab.b()V pc=60 /
ab.run()V pc=11` for 开心农场_240, a loop that sleeps between frames).

Frames are not time, and a frame budget is spent as fast as the runner can
present frames: a title that spends its first seconds in a sleep driven loading
animation presents very few of them, so thousands of frames go by while the
game's own clock has barely moved.  `反恐精英2正式版_5836` is the case that made
this concrete - its opening is a progress bar that advances on `Thread.sleep`,
and after the uncapped run, the long re-run and the paced re-run it was still
flat.  A title that still has no picture after all of those now gets
`NJ_SLOW_SECS` (default 90) seconds of wall clock in one more run and is judged
on what it drew; the row says so ("the picture appeared only after 40s of its
own clock").  The notes the shorter passes wrote are dropped in that case,
because "still flat when paced at 30 fps" next to a `PASS` contradicts the row's
own verdict.  Titles that could not start, or are not MIDlet suites, skip the
pass: more time cannot help them.

### Where the collected 395 stand

`NJ_FRAMES=600 NJ_TIMEOUT=25 tools/run_game_corpus.sh` over `tests/games`, with
the long re-run, the paced re-run, the wall-clock re-run, the device re-run and
the two heap re-runs below: **393 passed, 2 failed**.  The two are
`鲁宾逊漂流记汉化BT版_4677` (a truncated JAR: 286,534 bytes with no
end-of-central-directory record, which python's `zipfile` refuses as well) and
`实况足球经理2009_10609`, which is flaky rather than broken.  The sweeps before
it were 374/21 (before the pacing retry), 376/19, 381/14, 384/11, 385/10,
386/9, 387/8, 389/6, 390/5 and 393/2, and every drop came from a bug the corpus
itself pointed at rather than from a title being excused.  The last drop was
three passes at once: the pacing re-run (six titles draw only at 30 fps), the
wall-clock re-run (a loading bar paced by the title's own clock) and the device
re-run (`1945空战英雄_6105` runs as a Nokia 6230).

### The six that still fail, and what each one turned out to be

Five titles do not paint a frame.  They have been looked at one by one, because
the last three bugs came out of exactly this list, and the honest answer differs
per title:

| title | what happens | what it is |
| --- | --- | --- |
| `1945空战英雄_6105` | `IllegalAccessException` out of its own MIDlet constructor | the title's own platform check: it demands `microedition.platform` start with `Nokia`, and runs (`frames=60 paints=60 gfx=180`) with `--device nokia6230` |
| `鲁宾逊漂流记汉化BT版_4677` | `not a MIDlet suite` | the JAR is truncated: 286,534 bytes with no end-of-central-directory record at all (python's `zipfile` refuses it too) |
| `灌篮高手-爆发!樱木_7947` | `ArithmeticException: / by zero` in `w.paint()` | the class file really does `getWidth() / 0` (`iconst_0; idiv`) once its image field is loaded - no runtime can finish that paint |
| `战争艺术2之全球联盟中文版_1918` | NPEs in `n.a(String,int)`; nothing drawn | same shape: `d.f()`, which builds the config array `b.bb`, is never called |
| `午夜台球圣诞中文版_8677` | `NegativeArraySizeException` in its own decoder (`c.a(I)`), then a flat frame | its resource reader computes a negative length; the title is the only thing that can fix that |

Three titles left this table, and all three were the runtime:

* `模拟人生2_10762` was the row nobody could explain: its `paint()` made no
  Graphics call at all and its game thread returned before the first frame.  A
  wrapper class without `toString()` sent the game looking for a JAR entry named
  `global_data/java.lang.Integer@c1803e7` (see *A missing override is not a link
  error*).
* `实况足球经理2009_10609` was catalogued here as "the method that builds its
  string table has no caller" - a dead initialiser, we thought.  With the same
  wrapper overrides in place it loads its strings and paints, so the caller was
  there and the missing `Integer.toString()` was what stopped it.
* `仙魔飞行棋_417` was the one row with no exception at all; it was
  `Canvas.repaint()` dropping the request (see *MIDP paints on request*).  It now
  paints 79 colours and 16,206 Graphics calls per run where it painted black.

A fourth title left this table with the wall-clock pass described above:
`反恐精英2正式版_5836` was never stuck, it was slow.  Its opening is a sleep driven
progress bar, so a frame budget ran out while the bar was still moving, and with
`NJ_SLOW_SECS=40` the same binary paints its menu at 84 colours.  Its assets are
**not** the blocker either: every `l*.png` in the JAR is XOR-obfuscated (rolling
key from 100), the title tries the raw bytes first (one `unrecognised image
format` per attempt, all for the same resource), then retries with its own
`h.b(byte[],100)`, and that decrypted buffer is a valid PNG this runtime decodes
without a further error.

Every row in that table has since been re-run with the passes above, and four of
the five pass: `1945空战英雄_6105` as a Nokia handset, `灌篮高手-爆发!樱木_7947`
and `战争艺术2之全球联盟中文版_1918` when paced, `午夜台球圣诞中文版_8677` the
same way (the negative length is in a later menu, not in the first screen).  What
is left is `鲁宾逊漂流记汉化BT版_4677`, whose central directory was never written,
and the flaky football manager below.

`实况足球经理2009_10609` deserves a note of its own, because its row changes
between sweeps: it draws a nine colour screen at 176x208 and a flat one at
320x240, and its own initialisation races - the `NullPointerException` in
`d.T(int)` (its string cache) appears only in the runs where its loader thread
did not finish first.  Two runs of the SAME binary on the same size can execute
208 million or 15 million bytecodes, which is the title's timing, not the
runtime's.  The tool's fallback name for it, "flat under every pace and every
heap", is accurate per run and misleading across runs; the honest statement is
that it draws when its own race goes its way.
None of them is known to be the runtime's - which is a different statement from
"the runtime is perfect", and it is the reason the sweeps keep being re-run:
every one of these numbers has moved because of a bug the corpus itself pointed
at.



### MIDP paints on request

The runtime used to paint the current displayable on every frame, whether the
application had asked for it or not.  That is not what MIDP specifies, and the
difference shows in a title that publishes the state its `paint()` reads in a
field and clears it again right after forcing the paint:

```java
state = 1;
canvas.repaint();
canvas.serviceRepaints();   // paint() runs here, with state == 1
state = 0;
```

`paint()` clears the screen and then draws whatever `state` describes, so any
later paint in the same frame finds `state == 0` and leaves the screen blank.
`仙魔飞行棋_417`'s `mobile/k.e(I)V` is exactly this (`f = state; repaint();
serviceRepaints(); f = 0;`), and its board stayed black for a whole run while the
runtime reported a paint for every frame.  With both halves fixed the same title
paints 79 colours and 16,206 Graphics calls where it painted one colour and 370.

Two things were wrong:

* `Canvas.repaint()` with no arguments was routed through the four-argument
  form, which reads `x/y/w/h` out of argument slots `repaint()` does not have.
  It read a width of zero, took that as an empty region and requested nothing,
  so the matching `serviceRepaints()` had nothing to service.  It now requests
  the whole canvas.
* the runner painted again in a frame the application had already painted.  It
  now skips its own paint for that frame (`vm->painted_in_frame`).  That was not
  enough, because the two painters did not take turns evenly: `合金弹头2013BT版_678`
  paints itself every second frame, and on the frames in between the runner
  painted the same canvas with the title's own `paint()` - at a different camera
  position, because the title's state had moved on.  The screen alternated
  between two scenes every frame: exactly the "scenery behind the character
  flickers" a player reported.  Measured over 900 dumped frames, 362 of 898
  frame pairs showed the two-frame cycle before and **0** after.
* so the runner now stops painting altogether once the APPLICATION has painted
  by itself (`vm->app_painted`, set when a paint runs with `vm->in_paint` already
  set, i.e. from `serviceRepaints()`).  A title that paints itself is left to
  paint itself, which is what a device does; the runner only paints before the
  first application paint, which is what draws a title that never asks for one
  (`炸弹人_10311` never calls `repaint()` from its loop, and relies on exactly
  that).

The frame boundary also moved.  `present()` used to happen at the end of the
paint path, which is not the end of the drawing: a title that keeps the
`Graphics` from `paint()` and draws with it afterwards - out of spec, but
universal - had its scenery presented only by luck.  The canvas mirror now lives
in `nj_midp_present()`, called once per frame by the hook in
`nj_lib_midlet.c` after everything that frame drew, and it mirrors the current
plain `Canvas`'s buffer (a `GameCanvas` and a `Screen` keep their own, earlier
path).  `tests/savedgfx/SavedGfx.java` and `tools/check_savedgfx.sh` (ctest
`check_savedgfx`) pin it: the fixture draws a red block through the saved
`Graphics` after `serviceRepaints()` returns, and the check requires the last
presented frame to hold it.  Against the pre-fix runtime the same check reports
0 red pixels.

`tests/repaintdemo/RepaintDemo.java` and `tools/check_repaint.sh` (ctest
`check_repaint`) hold the idiom: a worker thread runs it for the whole length of
the run and the check requires the last presented frame to still be the red
block it drew inside the window.  Reverting either half makes the check fail.

A `GameCanvas` that flushes is the third case, and the one a player reported
twice.  Its buffer is not double buffered: the title draws a whole frame into
it, calls `flushGraphics()`, and starts drawing the next frame into the same
pixels.  The runner painting that canvas once per frame therefore copies
whatever is in the buffer at that moment - and if the title is halfway through
the next frame, the screen gets a frame that is part filled with the new frame
and part left over from the previous one.  Terrain drawn in blocks shows up as
blocks appearing and disappearing behind the character, which is
`鲁宾逊漂流记汉化版_702`'s flicker (its `data.k` game canvas flushes once per pass
of its own loop).  `flushGraphics()` now marks the frame as the application's
own (`vm->painted_in_frame`, `vm->app_painted`, `vm->app_painted_frame`, the
same bookkeeping `serviceRepaints()` uses), so the runner does not paint the
same buffer again in that frame; the adaptive rule still takes the frames back
if the title stops flushing.

`tests/flushdemo/FlushDemo.java` and `tools/check_flushdemo.sh` (ctest
`check_flushdemo`) pin it: the worker paints the top quarter of its frame one
pixel at a time, sleeps 45 ms across a frame boundary, finishes the frame and
flushes, alternating the whole screen between blue and red.  Every presented
frame must be one colour.  Against the pre-fix runtime the check reports 38 torn
frames out of 60, the worst holding 9152 pixels of the other colour; with the
fix, zero.

The runtime-side way to look for this class of defect is
`tools/scan_frame_health.sh`: it runs a title with `--dump-every 1` and counts
the pixels that flip back and forth - equal two frames apart, different from
the frame between - which the corpus sweep cannot see at its fortieth-present
sampling rate.  `鲁宾逊漂流记汉化版_702` with the pointer script that reaches its
map is `two=7164px worst=4632 verdict=flicker` against the runtime before
`08c7c26` and `two=0px verdict=static` against the fixed one.  Size alone is
not enough - an animated logo flips thousands of pixels too, in thin streaks
(`波斯王子之武者之心_8424` is `two=2123px` with a largest connected piece of
243), while a tear is one piece of thousands - so the verdict uses the largest
connected `blob`.  `NJ_KEYS` drives
a title past its menu, and `tools/frame_health.py` does the counting.  A run over all 395 titles with no input flags fifteen; eight of
them re-checked with the blob rule are four tears and three suspects, and
reading the frames shows what they are: an animated logo, a screen blanked
for exactly one frame between two identical ones (`男摄影师_11003`), a
loading spinner that blinks.  None of them is the runtime's - the two that
were are the ones 08c7c26 fixed.

### A mutable image is white, and always opaque

MIDP 2.0 says of `Image.createImage(int, int)`: "Every pixel within the newly
created image is white", and "Every pixel within a mutable image is always fully
opaque."  Neither held: the pixel array was allocated and never filled, so a
fresh mutable image was transparent black (`0x00000000`), and a translucent
source drawn into it kept its alpha.  A title that builds a scene off-screen and
blits it back therefore saw through the parts it had not painted.  The array is
now filled with white when a mutable image is created, and the blend keeps an
opaque destination opaque (which is what the alpha composition already did once
the destination was not transparent to begin with).

`tests/alphademo/AlphaDemo.java` and `tools/check_alphademo.sh` (ctest
`check_alphademo`) hold the rule: `fresh`, `drawn`, `blend`, `rgbkeep` and `copy`
are read back through `Image.getRGB()` and compared field by field - before the
fix, `fresh=0` and `drawn=0x80800000`; after, `fresh=0xffffffff` and
`drawn=0xffff7f7f`.

### The MIDlet clock is not the scheduler's tick count

`nj_now_ms()` folds the port's millisecond clock into `vm->scheduler_ticks`, and
`nj_yield()` and the end of `scheduler_step()` each incremented that same
counter - so every scheduler pass added a millisecond of MIDlet time on top of
real time.  A busy title gained about 30 ms of clock per 5 ms frame, the
runner's frame pacer measured every frame as over budget and never slept, and
`--fps 30` ran `合金弹头2013BT版_678` at 154 fps (2,479 fps uncapped): every
countdown, animation and `Thread.sleep()` in every title was scaled by the same
factor.  Both increments are gone; `tests/test_speed.c` pins it with "a
scheduler pass does not move the clock" (5,000 `nj_yield()` calls leave
`nj_now_ms()` unchanged) and the same title now runs at 30.0 fps.

### The passes reach further than the table did

The table above is a snapshot of one sweep, and three of its rows were written
before the tool learned to pace a title, to give it real seconds, and to hand it
the phone it was written for.  Run with those passes:

* `1945空战英雄_6105` passes - `PASS (static picture; only ran as a nokia6230
  handset)`.  The runtime has had device profiles all along; the sweep now uses
  one for a title that did not start.
* `灌篮高手-爆发!樱木_7947` passes - `PASS (the picture appeared only when the
  run was paced at 30 fps)`.  Its class file really does divide by zero inside
  `paint()`, which is why the row said so, but the frames drawn before the
  division stay on the screen and the tool accepts a static picture.
* `战争艺术2之全球联盟中文版_1918` passes the same way, with 405 colours: the
  initialiser with no caller is on a later menu path, not on the first screen.

What has NOT moved is the truncated JAR (`鲁宾逊漂流记汉化BT版_4677`): no pass
helps a suite whose central directory was never written.  The honest reading of
a row is therefore "this is what one pass of the tool saw", which is why the
tool prints the note that made a title pass.

### A title is allowed to ask how much memory it has

`Runtime.totalMemory()` is a real question with a real answer, and one title
uses the answer to decide whether to run at all: 有女倾城破解版_11525 calls
`notifyDestroyed()` out of its own `startApp()` when the total is above 6 MB
(the bytecode is `Runtime.getRuntime().totalMemory()` `>> 20`, compared with
`6L`). At `--heap 16` the sweep sees a MIDlet that presented nothing and exited
in 7 ms; at `--heap 4` it runs and draws 627 Graphics calls a second. A Java
heap of 1-4 MB is what the phones being imitated actually had, so the smaller
heap is the faithful run, and the sweep now asks once more with
`NJ_HEAP_RETRY` (default 4 MB, `0` switches it off) when a title paints
nothing. The row says which run the verdict came from: "the picture appeared
only with a 4 MB heap".

The same question has an opposite answer, and the sweep asks that one too:
愤怒的小鸟-高清版_2353 dies of `OutOfMemoryError` inside
`Class.getResourceAsStream` at the default 16 MB — it peaks at 16.5 MB — and runs
when it is given 32 (`NJ_HEAP_BIG`). That is not a faithful handset either; it is
the other end of the same range, and the row says which one it took: "the title
needed a 32 MB heap". The order matters: a title that ran out of heap is asked
for more room *before* it is asked to manage with less.

This is the same class of finding as the platform string: the runtime is not
wrong, the title made a decision about its world, and the honest way to run it
is to give it the world it was written for.

The eight that remain are each classified, not just counted:

| kind | titles |
|---|---|
| a broken JAR (truncated, no end-of-central-directory) | 鲁宾逊漂流记汉化BT版_4677 |
| a title that refuses a platform string this runtime does not imitate (`--device nokia6230` runs it) | 1945空战英雄_6105 |
| flat under every pace and every heap tried at the time, cause not yet found | 灌篮高手-爆发!樱木_7947, 午夜台球圣诞中文版_8677, 战争艺术2之全球联盟中文版_1918 (反恐精英2正式版_5836, 实况足球经理2009_10609 and 仙魔飞行棋_417 were in this row then; all three run now) |

The most recent fix to come out of this list is a good example of why it is
worth keeping: 疯狂农场2_2375 showed a black screen with 0 Graphics calls, and
the reason was that its class `h` declares a static `Vector b` while its
superclass declares an instance field of the same name and descriptor, so the
resolver linked `getstatic`/`putstatic` to the instance offset and both accesses
silently did nothing. Fixing field resolution the way JVMS 5.4.3.2 specifies
took that title, 疯狂企鹅中文版_10228 and 模拟人生3中文版_6162 off the flat list
in the same sweep. Two of those had been written down as "the game is stuck in
its own flow", which is exactly the conclusion this list exists to avoid.

All three parse and link with zero failures. Tumbleweed is the demanding one: it
uses `GameCanvas`, `LayerManager`, `Sprite` with frame animation, `TiledLayer`,
`Font` metrics, `Image` loading from the JAR, a background game-loop thread, and
`System.getProperty` - and it is the title that found most of the bugs listed at
the bottom of [ARCHITECTURE.md](ARCHITECTURE.md).

It also gates itself on the device, which is worth knowing when trying it:

```java
DISP_WIDTH  = getWidth();
DISP_HEIGHT = getHeight();
if (DISP_WIDTH < 150 || DISP_HEIGHT < 170) throw new Exception("Screen too small");
```

so run it with `--size 176x208` or larger; at 240x320 it refuses with its own
Alert (which the runtime renders correctly, title bar and all).

Specifically supported:

- `long` and `double` throughout, including the two-slot stack discipline
- legacy `jsr` / `ret` (old toolchains emit these for `finally`)
- `tableswitch`, `lookupswitch`, `multianewarray`
- `checkcast`, `instanceof`, `athrow`, `monitorenter` / `monitorexit`
- modified UTF-8 (CESU-8) constants, including embedded NULs
- class file attributes that matter (`Code`, `ConstantValue`); the rest skipped

## API coverage

### java.lang

| Class | Status |
|---|---|
| `Object` | complete (`hashCode` is a stable identity hash) |
| `String` | complete; both the embedded and the wrapped representation, including the `byte[]` constructors and `getBytes`, named or not |
| `StringBuffer` | complete |
| `StringBuilder` | present as a `StringBuffer` subclass — not in CLDC, but every modern javac lowers `a + b` to it |
| `Math` | complete, plus `cbrt`/`rint` beyond CLDC |
| `System` | `currentTimeMillis`, `nanoTime`, `arraycopy`, `identityHashCode`, `gc`, `exit`, `getProperty`, `out`, `err` |
| `Thread`, `Runnable` | green threads, `sleep`, `join`, `yield`, `wait`/`notify` on `Object` |
| `Throwable` and the full exception hierarchy | complete, with `toString`/`getMessage`/`printStackTrace` |
| `Class` | `getName`, `isArray`, `isInterface`, `isInstance`, `getSuperclass`, `forName` |
| `Integer`/`Long`/`Short`/`Byte`/`Character`/`Boolean`/`Float`/`Double` | constructors, `*Value`, `parseInt`, `toString(radix)`, `toHexString`, `valueOf`, and the value semantics of `toString()`/`equals(Object)`/`hashCode()` |
| `Runtime` | `gc`, `freeMemory`, `totalMemory`, `exit` |

`System.getProperty` answers for `microedition.platform`, `.configuration`,
`.profiles`, `.locale`, `.encoding`, `line.separator`, `java.version` and
`os.name`. A key the runtime does not know, the installed device profile does
not carry and the port does not answer returns `null`, which is what a real
device does for unknown keys. The platform string is itself overridable — see
[the device a title believes it is on](#the-device-a-title-believes-it-is-on).

**Encoding names are case-insensitive**, as the spec says, and the table is
deliberately closed. `String.getBytes`/`new String(byte[], String)` decode the
platform encoding this runtime advertises (ISO-8859-1, with every alias and
spelling seen in a jar), the ASCII family, and UTF-8. Anything else —
`Shift_JIS`, `GB2312`, `Big5`, `UTF-16` — refuses **by name** with
`UnsupportedEncodingException` rather than decoding as Latin-1: a title that
asked for one of those and got mojibake has no way to find out where it went
wrong. The corpus is the reason the table exists at all — 48 collected titles
spell UTF-8 some other way than `"UTF-8"` (`"utf-8"`, `"Utf-8"`, `"utf8"`), and
`疯狂战机_2013` died on every run at `getBytes("utf-8")` until the comparison
stopped being case-sensitive. `tools/check_encoding_names.sh` is the regression
test.

### A missing override is not a link error

CLDC declares `toString()`, `equals(Object)` and `hashCode()` on every wrapper
class. This runtime declared none of the first and, outside `Integer`, none of
the others - and a missing override here is invisible: the call resolves up to
`java.lang.Object`, so the method exists, runs, and answers the identity form.

`模拟人生2_10762` is what that costs. Its content loader
(`com/ideaworks3d/sims/util/a.a`) names a file by concatenating
`"/global_data/" + <content id>`; `String.valueOf(Object)` reached
`Integer.toString()`, the runtime answered `java.lang.Integer@c1803e7`, the
lookup for the JAR entry `global_data/java.lang.Integer@c1803e7` found nothing,
the game threw `Failed to locate content: 34795` into its own catch-all handler,
and its game thread returned before the first frame - a MIDlet that looks alive
and paints a black screen for as long as you watch it. With the value semantics
in place the same run reaches `loadAllSounds` and `start menu`, and paints
(401 paints / 8,635 Graphics calls in 400 frames, 2.4 M bytecodes/s).

The same gap made every `Hashtable` keyed on a `Long`, `Short`, `Character`,
`Boolean`, `Float` or `Double` compare identities, and `Boolean.hashCode()`
answered the identity hash instead of 1231/1237.

`Float` and `Double` follow Java's own details rather than a `printf`: a whole
value prints with its `.0` (`2.0f` is `"2.0"`, not `"2"`), `equals()` compares
the bit patterns - so `NaN` equals `NaN` and `+0.0f` does **not** equal `-0.0f` -
and `hashCode()` is `floatToIntBits()`, or the xor of the two halves of
`doubleToLongBits()`. `NaN`, `Infinity` and `-Infinity` are spelled the Java
way, not `printf`'s.

`tests/boxdemo` and `tools/check_boxing.sh` pin all of it, including the
cross-class rule (`new Integer(5).equals(new Long(5L))` is false) and the
float/double cases above; the negative check is the same fixture against the
pre-fix runtime, which prints `java.lang.Integer@e85f125` for `new Integer(34795)`.

### Every member the reference declares, or a line saying why not

A missing builtin member is not a link error: the class loads, the call sites
in other classes resolve nothing, and the title dies later with a
`NoSuchMethodError` that names a method the javadoc says exists.  That is how
`java.lang.Integer.toString()` went missing for ten rounds while 65 titles
kept working (the identity string was a plausible-looking resource name), and
it is why this round stopped reading the corpus and started reading the
specification.

`tools/build_api_ref.py` downloads the CLDC 1.1 and MIDP 2.0 javadoc pages,
plus the **Nokia UI API**, and writes `tools/data/api-core.txt`: 982 members
over 83 classes, one `class name (args)` line each.  The Nokia pages are there
because that vendor surface is the one the corpus leans on hardest - 83 titles
reference `FullCanvas`, 74 `DirectGraphics`, 73 `DirectUtils`, 14 `Sound` and
10 `DeviceControl` - and the first audit of those seven classes found thirteen
missing members, one per family: `drawTriangle`, both `drawPixels` overloads,
the `byte[]`/`byte[][]` `getPixels` and `getAlphaComponent` on
`DirectGraphics`; `createImage(byte[], int, int)` on `DirectUtils`;
`flashLights(long)`, `startVibra(int, long)` and `stopVibra()` on
`DeviceControl`; and `resume`, `getGain`, `getConcurrentSoundCount(int)` and
`getSupportedFormats()` on `Sound`.  A title that hits one of those loses the
frame it was drawing, and `DirectUtils.createImage(byte[], int, int)` is the
one a PNG-based title cannot work around at all.

The scan that says whether any of it matters to a title is
`tools/scan_api_gaps.sh`: it runs every collected title for 400 uncapped frames
and greps for `NoSuchMethodError`, `NoSuchFieldError`, `ClassNotFoundException`,
`UnsatisfiedLinkError` and `AbstractMethodError`.  Over all 395 titles it finds
ten titles with a missing member and **not one `NoSuchMethodError` or
`NoSuchFieldError`** - every line is a title asking `Class.forName` for a vendor
class inside its own `try`, which is a title looking for an emulator it is not
running on, not a gap in this runtime.

`tools/api_audit.py` then parses this runtime's
own builtin tables the way the compiler does - macro expansion, the class
table, every `M(...)` row, the registration table, the superclass walk - and
reports what cannot resolve.  It runs as the `check_api_audit` ctest, so it now
fails the build the moment a member disappears again, and
`tools/data/api-allow.txt` names the gaps that are accepted on purpose (two
`HttpConnection` members that belong to the HTTP lane) and is checked in both
directions: a line that starts resolving is a failure too.

Two things about that list are worth keeping.  The flags are not part of it:
`Sound.getSupportedFormats()` and `Sound.getConcurrentSoundCount(int)` are
static in the API and this runtime first declared them as instance methods,
which the audit cannot see (it compares names and argument lists) and which a
title would meet as an `IncompatibleClassChangeError` on the
`invokestatic`.  And a member that exists but does nothing useful is not a gap
either, which is why the vendor calls have a fixture: `tests/nokiademo` drives
`DirectUtils.createImage(16, 16, ...)` and `createImage(byte[], int, int)` (the
jar carries a real two-by-two PNG), checks that the triangle's top edge came out
red where it was drawn and white where it was not, that a `byte[]` pixel reads
back as its own grey and an ARGB `int` survives the round trip, and that
`Sound` plays, keeps gain, resumes, lists two formats and one channel.
`tools/check_nokiademo.sh` is that fixture's assertions and runs as the
`check_nokiademo` ctest.

The first honest run of the CLDC/MIDP audit said 81 members.  They were filled in two batches
(java.io: Reader/Writer/InputStreamReader/OutputStreamWriter/PrintStream;
core: String.valueOf/intern, Thread(String), Class.isAssignableFrom,
Throwable.printStackTrace(PrintStream), Calendar.before/after, Display colours,
TextBox/TextField.getCaretPosition, the five-argument Command and ImageItem
constructors, RecordStore.setMode, Player.getContentType, StringBuffer's
missing insert overloads and Object.wait(long,int)).  java.io mattered most:
`new PrintStream(new ByteArrayOutputStream())` used to write to the port log
and nowhere else, because PrintStream's own class row lives in
`src/core/nj_lib_system.c` while the natives lived in the io module - the
registry is keyed by (class, name, descriptor), so the bodies bind across
modules but the *declaration* has to be in the file that owns the class.

The audit's own blind spots are worth knowing: 35 of the 50 java.io rows were
false positives, because `reader_methods[]` and friends were written with a
private `RM(...)` macro that only knew `M(...)`; and a table that is empty
because nobody can parse it looks exactly like a table with nothing in it.
`-v` prints the other direction (members this runtime declares that the
reference does not list: the Mascot Capsule, m3g and JSR 75 extensions), which
is how a typo in a descriptor shows up.

### java.util

`Vector`, `Hashtable`, `Random` (the 48-bit LCG, so sequences match a real
device), `Date`, `Calendar`, `TimeZone` (UTC), `Stack`, `Enumeration`,
`Timer`/`TimerTask` (driven by the VM scheduler, so background timers work),
`NoSuchElementException`, `EmptyStackException`.

`Calendar` does real calendar arithmetic; `TimeZone` is UTC-only, so a game that
displays local time will show UTC.

### java.io

`InputStream`, `OutputStream`, their `Data*` subclasses (big-endian, modified
UTF-8 via `readUTF`/`writeUTF`), `ByteArrayInputStream`,
`ByteArrayOutputStream`, `PrintStream`, and the `IOException` hierarchy.

`Reader`/`Writer` are **not** implemented. A title that uses
`InputStreamReader` will fail; this is rare in practice because CLDC has no
`java.io.Reader` at all, so a well-behaved MIDlet never refers to one.

### javax.microedition.io

`Connector.open` for `file://`, `http://`/`https://` and `socket://`.

`HttpConnection` is complete for what a MIDlet uses: the URL accessors,
`setRequestMethod`/`getRequestMethod`, `setRequestProperty`/`getRequestProperty`,
`getResponseCode`/`getResponseMessage`, `getHeaderField` (by name and by
index) with `getHeaderFieldKey`, `getType`/`getEncoding`/`getLength`, and
`openInputStream`/`openOutputStream`. The request is made when the MIDlet
first asks for a response, and the response body is buffered whole - which is
what the API's shape implies and what these titles do with it (99 of the 436
titles in `tests/games` name `HttpConnection`).

The socket work belongs to the port: `nj_port.http_request` is one synchronous
call that takes the URL, method, request headers and body and returns the
status, response headers and body. With no hook installed - a bare-metal
target, or an Android build whose `PlatformHttp` did not resolve - `http://` is
still refused for real, but by name: `IOException: this build has no HTTP
support; the port must supply it`. On Android the hook is `HttpURLConnection`
and the app declares `INTERNET` plus a cleartext-permitted network security
config (MIDlet URLs predate TLS, and a per-host allowlist cannot be written
for software that hard-codes hosts from 2005).

`SocketConnection` is implemented for TCP, on four more port hooks
(`socket_connect/read/write/close`): `getAddress`, `getPort`,
`getLocalAddress`, `getLocalPort`, `openInputStream`/`openOutputStream` (and the
`Data*` wrappers), `getSocketOption`/`setSocketOption` for the five option ids
(this runtime remembers what was set and answers `-1` for an option no MIDlet
has asked for, rather than inventing a socket-level default), and `close`.
`setSocketOption` is registered under both `(BI)V` - the descriptor JSR 118
declares - and `(II)V`, because a title compiled against a stub that typed the
option id as an `int` exists in the corpus. Four of the 436 titles in
`tests/games` open a socket; an end of stream reaches `read()` as `-1`, and a
read that times out is not an end. With no hooks the URL is refused by name
(`this build has no socket support; the port must supply it`), which is what
the desktop build does and what `tools/check_socket.sh` asserts.

Datagrams, server sockets, OBEX and the push registry are **not**
implemented: `Connector.open` raises `ConnectionNotFoundException` naming the
scheme.  JSR 82 Bluetooth is implemented both ways, as described above, on a
port that writes the hooks; a port that writes none refuses the URL by name
rather than failing halfway, and the Android port writes them all — servers and
clients alike — subject to the platform's own permissions.  L2CAP here is a byte
stream rather than a datagram service (`L2CAPConnection.receive()` reads, it
does not wait for a packet boundary, and `ready()` answers `false`).

Bluetooth Low Energy has **no** Java ME standard API at all: JSR 82 predates it
by a decade, and the vendor extensions that exist (`com.nokia.mid.bt.*` and
friends) are not in any collected title.  A port that wants BLE would add it as
its own extension beside `javax.bluetooth`, the same way the camera and the
accelerometer arrived in this runtime: classes here, hardware there.

### javax.microedition.lcdui

`Display`, `Displayable`, `Canvas`, `Graphics` (the complete drawing API),
`Image`, `Font`, `Command`, `Ticker`, `Alert`, `AlertType`, `Form`, `List`,
`TextBox`, `Item`, `StringItem`, `ImageItem`, `Gauge`, `ChoiceGroup`,
`TextField`, `DateField`, `Spacer`, and the three listener interfaces.

`Graphics` implements all three overloads of `drawString`, `drawRegion`,
`drawRGB`, `copyArea`, alpha blending, XOR mode, dotted strokes, rounded
rectangles and arcs with MIDP's counter-clockwise degree convention.

### javax.microedition.lcdui.game

`GameCanvas`, `Layer`, `LayerManager`, `Sprite`, `TiledLayer` — including all
eight transforms, reference-pixel arithmetic in the rotated coordinate space,
frame sequences, animated tiles, and **pixel-level collision detection** that
uses the same transform as drawing (a mismatch there produces collisions that
do not match what the player sees, which is the classic bug in this area).

### javax.microedition.rms

`RecordStore` with real persistence through the port's file hooks, so a high
score survives a restart. `RecordEnumeration` is implemented; listeners are
accepted and ignored.

One file per store, named `<store>.rms`, in `nj_port.rms_dir`.

**The runner derives that directory per game**, because the old default - the
working directory - had two problems. It littered: a repository accumulated 45
`.rms` files in its root, named after whatever each title called its store
(`bubblet`, `shushan`, `__sfSmsInfo`). And worse, it collided: two games that
both call their store "save" shared one file, so the second one to run read the
first one's high scores.

The default is now

    $XDG_DATA_HOME/nano_j2me/<jar-name>/      or
    $HOME/.local/share/nano_j2me/<jar-name>/  when XDG_DATA_HOME is unset

created on demand, with `--rms-dir` still overriding it and a port that sets
`rms_dir` itself left alone. A bare-metal port with no filesystem still gets the
exception it would get from `openRecordStore` either way.

These are the player's saves, not project files - never commit them. `*.rms` is
gitignored for that reason.

## javax.microedition.media

Implemented, and tested end to end by `tests/test_audio.c` (38 assertions)
driving `tests/fixtures/audiodemo.jar`, built by `tools/make_audio_jar.sh`;
the decoder itself by `tests/test_amr.c` (119 assertions).

| | |
|---|---|
| `Manager` | `createPlayer(InputStream,String)`, `createPlayer(String)`, `getSupportedContentTypes/Protocols`, `playTone`, `TONE_STOP`/`TONE_BLOCKING`/`TONE_CONSTANT_DURATION`, `TONE_DEVICE_LOCATOR` |
| `Player` | the full state machine, `realize`/`prefetch`/`start`/`stop`/`deallocate`/`close`, `setLoopCount`, `getState`/`getDuration`/`getMediaTime`, `add`/`removePlayerListener`, `getControl(s)` |
| `PlayerListener` | `started`, `stopped`, `closed`, with the event strings interned so the reference comparison the javadoc recommends actually works |
| `ToneControl` | real JSR-135 parsing: big-endian length, note/duration triples, malformed input refused at `realize()` rather than mid-frame |
| `VolumeControl` | `setLevel`/`getLevel`/`setMute`/`isMuted` |
| WAV | RIFF/WAVE PCM, 8- and 16-bit, mono and stereo, from a stream or a JAR resource. Behind `NJ_ENABLE_WAV`, on by default; `=0` hands a WAV to `nj_port.audio_decode` instead |
| AMR | AMR-NB (`#!AMR`) speech in all eight modes, 4.75–12.2 kbit/s, decoded by the runtime itself into 8 kHz mono PCM — no host codec, no dependency. `nj_port.audio_decode` is offered the file first, so a platform with its own AMR codec wins; behind `NJ_ENABLE_AMR`, on by default, and `=0` removes the built-in decoder, leaving the port hook and then a refusal by name. AMR-WB has no built-in decoder at all and is refused by name |
| MIDI | Standard MIDI Files, type 0 and 1, played by the runtime's own synthesiser: 16 timbres, channel volume (CC7) and expression (CC11), pitch bend, sustain pedal, channel 10 percussion, 22050 Hz mono or stereo. A file that contains a second `MThd` chunk is two files glued together and only the first is played — every other player does the same, and merging them lets the second file's tempo land on tick 0 |
| `MIDIControl` | all of it: `setChannelVolume`/`getChannelVolume`, `setProgram`, `getProgram` (both spellings), `shortMidiEvent`, `longMidiEvent`, `getBankList`, `getProgramList`, `getProgramName`, `getKeyName`, `isBankQuerySupported`. It is offered for a MIDI `Player` only, and works whether the platform's synth or the runtime's renderer is playing |

**`Controllable.getControl` takes either spelling.** The javadoc asks for
"either ... the fully-qualified name of the class; or if the package of the
class is not given, the package `javax.microedition.media.control` is assumed",
and titles use both. Only the short form used to match, so
`getControl("javax.microedition.media.control.VolumeControl")` answered `NULL`;
`开心农场_240` then died with `NullPointerException at g.b(I)Z pc=65`, because a
`checkcast` on `null` succeeds and the `setLevel` that follows does not. The
last dot-separated component is what gets compared now, so both spellings reach
the same control and a name in somebody else's package is accepted rather than
turned into a null.

`TONE_BLOCKING` blocks the CALLING JAVA THREAD cooperatively: it returns
`NJ_ERR_TIMEOUT` and the interpreter suspends that thread, exactly as
`Thread.sleep()` does. Sleeping the C thread instead would freeze every other
thread in the VM, which for a game means the picture stops.

**A port's synth plays the file until a game sends it an event.** `MIDIControl`
cannot be honoured by a synth that only takes whole files - Android's
`MediaPlayer` is one - so the first `shortMidiEvent`/`longMidiEvent`/
`setProgram`/`setChannelVolume` call moves the song to the runtime's renderer,
seeking to the position the platform player had reached; a port whose synth
*can* take single events (the two optional hooks `midi_short_event` and
`midi_long_event`) keeps playing instead. The takeover is logged, because the
timbres change audibly and a listener deserves to know why.

**The renderer can be left out.** `NJ_ENABLE_MIDI=0` (CMake variable, or
`-DNJ_ENABLE_MIDI=0` for the hand-built Android library) removes the synthesiser
and the injection API with it: a `.mid` then becomes a silent `Player` unless
the port's `midi_open` takes it, and `MIDIControl` disappears (there is nothing
left to send events to). On Android it saves 18,680 bytes of `.so` per ABI
(x86_64: 1,100,768 -> 1,082,088) and 8,192 bytes of the single-ABI APK
(505,928 -> 497,736).

**MIDI is rendered, not delegated.** The runtime's synthesiser is deliberately
small — sixteen generated timbres, one-pole envelopes, no samples — and it is
what plays a `.mid` on every port that has no synthesiser of its own. A port
that *does* have one can take the whole file over: `midi_open` returning a
handle is the port saying "I have this", and `NULL` leaves the file to the
runtime, exactly like `audio_decode` does for a container. The rest of the hooks
(`midi_start`, `midi_stop`, `midi_close`, `midi_seek`, `midi_time`,
`midi_volume`, `midi_finished`) then back the same `Player` state machine, so
`getDuration()`, `getMediaTime()`, `setLoopCount()` and `VolumeControl` keep
working without the media layer knowing which of the two is playing. On Android
the platform's own synth (Sonivox EAS, behind `MediaPlayer`) plays the file, so
a phone gets the platform's timbres while a microcontroller still gets the
built-in ones. `tests/fixtures/mididemo.jar` (`tools/make_mididemo_jar.sh`)
covers the runtime path, and `tests/test_midi.c` asserts among other things that
a glued second sequence is ignored rather than merged.

**Not implemented, and refused by name rather than silently:** `http://` and
`rtsp://` raise `MediaException` naming the protocol, as does AMR wideband. There
is no video, no audio capture, and no custom controls beyond the two above.

**The headless port records sound instead of playing it** - tone calls, tones
offered, stops, PCM opens, frames accepted, device format - so a test can assert
that a MIDlet asked for the right notes in the right order without a sound card.
A port with no audio leaves the hooks NULL and sound does not play, which is not
an error.

**The SDL2 port really plays it.** Tones are synthesised as square waves - which
is what the format sounds like, a sine would be prettier and wrong - and queued
with `SDL_QueueAudio`, so a one-second tone does not stop the frame loop for a
second. `pcm_free()` reports the queue depth so a long WAV is throttled rather
than buffered without bound. Verified headless with SDL's dummy driver, which is
the one check that distinguishes "the game is silent" from "the device never
opened":

    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        build/nj2me --port sdl2 tests/fixtures/audiodemo.jar
    INFO: audio: dummy opened, 22050 Hz, 1 channel(s)
    INFO: AUDIO wav=200000 state=400

## The device a title believes it is on

`System.getProperty("microedition.platform")` is how a J2ME title decides which
handset it is running on, and a great many of them decide badly: they ask, get a
string they do not recognise, and take a path that was never finished. A few
stop outright. `1945空战英雄_6105` reads the first five characters, requires
`"Nokia"`, and throws `IllegalAccessException` out of its MIDlet constructor
when it does not get one — the runtime's own `"nano_j2me"` is a refusal, and the
title never draws a frame.

Of the 394 readable collected titles, 65 ask for `microedition.platform` and 31
of those also compare it against `"Nokia"`. The corpus probes the handset's
*identity* far more often than its platform: `com.sonyericsson.imei` in 14
titles, `com.nokia.mid.imei` in 13, `com.nokia.IMEI` in 12, `com.samsung.imei`
in 12, `com.nokia.mid.imsi` in 8, `com.siemens.IMEI` in 8, and a dozen more
spellings of IMEI, IMSI, MCC and MNC. A real device answers all of them; a
runtime that answers `null` leaves the MIDlet holding a null it never checks
for.

So the runtime can be told which handset it is on:

    build/nj2me --list-devices                       # the built-in profiles
    build/nj2me --device nokia6230 game.jar          # answer as that phone
    build/nj2me --prop microedition.platform=Nokia6230/05.50 game.jar

`--prop key=value` may be repeated and wins over the profile. The profiles
themselves live in `include/nj2me/nj_device.h` and `src/core/nj_device.c`, and
carry the platform string plus the vendor's own IMEI/IMSI/network spellings —
`nokia6230` answers `Nokia6230/05.50`, `com.nokia.keyboard.type` as
`PhoneKeypad` and eight identity keys; `generic`, the default, answers only the
port's own identity and no vendor keys at all.

The IMEI/IMSI values are **synthetic**: a valid length and Luhn check digit, a
TAC and an MCC/MNC that belong to nobody. They are there so a title that reads
or parses an identity sees something shaped like one. They identify neither this
host, nor its owner, nor any subscriber. An embedding installs the same table
through `nj_run_opts.props` / `nj_vm_set_system_properties()`.

The lookup order is: the installed table (last match wins, so a caller puts its
own overrides after a profile), then the runtime's own facts, then the port's
`device_property` hook, and only then `null`. A port's answer for
`microedition.platform` is therefore live — it used to be unreachable, because
the runtime answered that key before consulting the port.

## Known behavioural differences

| Area | Difference |
|---|---|
| Threads | cooperative, switched at an instruction-count boundary. A tight loop with no allocation or I/O still yields (the quantum is enforced), but a thread that never blocks cannot be preempted *mid-instruction* — irrelevant for correctness, occasionally visible in interleaving |
| `synchronized` | compiles and runs, but does not lock: a cooperatively scheduled thread is never preempted inside a critical section |
| `Object.wait`/`notify` | implemented; a `wait` with no notifier left is detected and the game is reported as finished rather than hanging |
| `System.exit` | stops the VM cleanly and the code reaches the host |
| `Class.forName` | works for classes the archive actually contains plus the built-in library |
| Reflection | absent, as in CLDC |
| Networking | `file://` and `http://`/`https://` through the port; sockets, datagrams and OBEX are absent |
| `TimeZone` | UTC only |
| Locale | `en-US` fixed; `toLowerCase`/`toUpperCase` are ASCII-only |
| Fonts | three built-in bitmap sizes from Liberation Mono; CJK needs a glyph provider installed through the font fallback hook |
| Image size limits | 8192 px per axis, checked in 64-bit before any allocation |

### The clip does not move when the coordinate system does

MIDP 2.0 states the rule in an unexpected place — `Graphics.translate()`:

> Operations on the coordinate system, such as `translate()`, do not modify the
> clip. The methods `getClipX()`, `getClipY()`, `getClipWidth()` and
> `getClipHeight()` must return a rectangle that, if passed to `setClip` without
> an intervening change to the `Graphics` object's coordinate system, must result
> in the identical set of pixels in the clip.

So the clip is a rectangle of *pixels* on the target: `setClip()` takes its
coordinates in the coordinate system current at that moment (which is why
`setClip` after a `translate` lands somewhere else), and `translate()` itself
moves only the origin. The runtime used to drag the clip along with the origin,
which makes the two orders equivalent — convenient, and wrong.

帝国时代3-亚洲王朝_6288 is what that cost. It draws its campaign map by
translating the origin by the camera offset (`-336,-216` in the mission that was
tested) and then blitting tiles at *map* coordinates: the tiles land on the
screen, but with the clip dragged off it every one of them was discarded. The
game ran, drew its HUD (which is not translated), and presented a black field.
With the clip left where it belongs, the same run draws the terrain, the units
and the tutorial panel: `gfx` calls in the map frames went from a few hundred to
tens of thousands, and the frame is no longer one colour.

Two tests pin it down, because the pixel layer and the MIDP layer can disagree:
`tests/test_clip.c` (both orders, plus the empty clip) and the
`check_clip_translate` fixture, which runs the four cases through a real MIDlet
and also checks the `getClipX()` round trip above.

## Codec coverage

Decoding goes through a registry
([`include/nj2me/nj_codec.h`](../include/nj2me/nj_codec.h)) so a platform can
substitute a hardware decoder; see [PORTING.md](PORTING.md).

| Format | Built in | Notes |
|---|---|---|
| PNG | yes | all colour types, bit depths 1–16, Adam7 interlacing, `tRNS`. `NJ_ENABLE_PNG=0` removes it and leaves an actionable refusal |
| JPEG | yes by default | a software baseline decoder; `NJ_ENABLE_JPEG=0` leaves an actionable refusal instead |
| GIF | yes | first frame only, as MIDP expects. `NJ_ENABLE_GIF=0` removes it |
| BMP | no | never built in; a provider can be installed |
| WebP | no | never built in; a provider can be installed |
| WBMP | no | never built in; a provider can be installed |

Benchmark titles overwhelmingly use PNG for sprite art. JPEG appears mostly for
splash screens and photographic backgrounds, which is why it is the first thing
to hand to a hardware block.

**Every decoder is cuttable and replaceable.** Two independent mechanisms, and
a build can use either:

* *Cut it out.* `NJ_ENABLE_PNG`, `NJ_ENABLE_JPEG`, `NJ_ENABLE_GIF`,
  `NJ_ENABLE_INFLATE` (DEFLATE, used by every zip entry and by PNG's IDAT),
  `NJ_ENABLE_WAV`, `NJ_ENABLE_AMR` and `NJ_ENABLE_MIDI` are CMake variables and
  `-D` defines, all on by default. Switching one off removes its code and
  leaves the runtime saying *which* format it cannot read rather than
  mis-detecting the file, so a MIDlet can fall back to another asset.
* *Replace it.* A port registers a provider through `nj_port.install_codecs`
  (see [PORTING.md](PORTING.md)) and the registry prefers it over the built-in
  decoder. The Android port does exactly this for BMP and WebP, which nothing
  in this runtime can read, and for PNG/JPEG/GIF/AMR/MP3/AAC once the built-in
  decoder is cut.

The two compose: cut the built-in decoder, and the platform's provider answers
instead of a refusal.

## JSR 184 (Mobile 3D Graphics): what it takes

Three collected titles need it - need-for-speed 14 and 16, and
Metropolis (du-shi-mo-tian-lou) - and they are the only class-level gap
left anywhere in the corpus.  A scan of every unresolved reference in all
58 titles found exactly three names, all of them m3g:

    javax/microedition/m3g/Image2D
    javax/microedition/m3g/Graphics3D

"Add the missing classes" is the wrong reading of that, and it is worth
writing down what the surface actually is before anyone tries.  Reading
every m3g call out of those three titles gives 70-odd distinct methods
across about 25 classes, including:

    Node.setRenderingEnable(Z)V          114 calls
    Group.addChild(Node)V                 74
    Transformable.setScale(FFF)V          24
    Transformable.setTransform(Transform)V 21
    Object3D.animate(I)I                  12
    Graphics3D.render(World)V              6
    Graphics3D.bindTarget(Object)V         4
    Loader.load(String)[LObject3D;        1
    Image2D.set(IIII[B)V                   1
    Camera.setPerspective(FFFF)V           1

Those are not class declarations, they are a scene graph, a fixed-point
transform stack, a camera and projection pipeline, a software rasteriser
with depth buffering, and a parser for the .m3g binary container that
Loader.load() reads.

It is being implemented, behind `NJ_ENABLE_M3G`, in the order titles stop
failing in:

1. the classes and the scene graph, so a title runs at all and can draw its
   2D interface around an empty 3D view - **done**;
2. the transform stack and camera - **done**; the rasteriser is partial, and
   no collected title reaches a triangle through it yet;
3. `Loader.load()` and the `.m3g` container, for the titles that read a
   resource rather than build their scene from code - **done**: sections
   (including zlib-compressed ones), the object table, a `World`'s children
   along with its active camera and background, and `find()` falling back to
   the file's own object table when the child list does not name the object a
   title asks for.

`毁灭战士3_10598` is the title this was written against.  It used to stop in
its constructor - first `addChild(null)`, then
`NoSuchMethodError: javax/microedition/m3g/Group.postRotate(FFFF)V`, then
`construction of Doom/Doom blocked` - and now runs: 120 frames, no uncaught
exception, `m3g=2`, `tris=0`.  The `tris=0` was the honest part of that
sentence: the scene is built and `render()` is called, and nothing was
rasterised, so its 3D view stayed empty.  That is no longer true - see
"### The geometry reaches the target" below.

### A declared member is not a working member

The member list is not an impression: `tools/data/m3g-api-1.1.txt` is the JSR 184
API surface (30 classes, 308 constructors, methods and constants), and
`tools/m3g_api_audit.py` parses `src/midp/nj_lib_m3g.c` the way the compiler
would, follows superclasses for inherited members, and exits non-zero for the
first member that is missing.  It is a ctest (`check_m3g_api`), and it reports
31 classes / 330 members against the reference's 30 / 308 - the extra class is
`Sprite`, the JSR 184 1.0 name the loader maps file type 18 to.

Completing the surface raised the more interesting question: how much of it
*behaved*.  `tests/m3gdemo/M3GDemo.java` calls every area once and prints one
summary line (`M3G checks=104 failures=0`); `tools/check_m3gdemo.sh` asserts it
as a ctest (`check_m3gdemo`).  The first run reported fourteen defects, and they
were not exotic:

| what the fixture asked | what the runtime did |
| --- | --- |
| `Group.getReferences()` | never reported the group's children, only AnimationTracks |
| `new VertexArray(n, c, 2).getComponentType()` | answered BYTE; `set()` clobbered the type |
| `new VertexArray(1, 3, 4).getComponentType()` | answered FIXED for a four-byte component's size, which is right, but the ctor read the third argument as a type |
| `new Sprite3D(false, img, app)` | stored nothing: `getImage()` answered null |
| `Graphics3D.setLight(i, light)` | counted the light and kept none, so `getLight()` threw |
| `Background.setImageMode(x, y)` | stored only `x` |
| `CompositingMode.getAlphaThreshold()` | answered the float's bits as an integer |
| `Object3D.getReferences()` with a too-small array | no `IllegalArgumentException` |
| `MorphingMesh`/`SkinnedMesh` `instanceof Mesh` | false: their class rows named Object3D as the superclass |
| `Transformable.align()` then `getAlignmentReference(NONE)` | the reference was stored and not found again |
| `Transform.transform(VertexArray, float[], boolean)` | transformed two components per vertex: `_nj_compSize` was read where `_nj_numComp` was meant |
| a `VertexArray.set(0, 3, byte[9])` of normals, read back | vertices 1 and 2 came back zero and 144: the same `_nj_compSize`-as-count mistake copied 3 of the 9 bytes |
| `VertexBuffer.getPositions(float[])` | read two components of a three-component position, so `z` was 0 |
| a model `Transform` handed to `render()` | ignored: `nj_array_length()` answers *elements*, and the code compared it against `sizeof(float) * 16` (64), so the matrix was never copied |
| `setCamera(cam, transform)` | the camera's placement was used *as* the view matrix instead of its inverse, and then read with the wrong layout |
| `setLight(1, light, transform)` | threw `InternalError`: the array helper was asked for an element of type `Light` for a field declared `[LLight;` |
| `TriangleStripArray(int[] indices, int[] stripLengths)` | stored the **indices** as the strip lengths (found while adding lighting; the C test had encoded the same mistake) |
| `g3d.clear(background)` then a second `render()` | the second render was rejected by the first one's depths: `clear()` cleared the colour and not the depth buffer |

Most of them were a line or two.  The lesson is the one this file keeps
recording: a member that exists can still be a member that lies, and only a
caller that uses the answer finds out.

The fixture prints `M3G checks=130 failures=0` since the rasteriser and lighting
rounds below added checks that read pixels back off the target (depth, culling,
texture, lighting, a model transform, the viewport after `bindTarget`).

### The geometry reaches the target

`Graphics3D.render(VertexBuffer, IndexBuffer, Appearance, Transform[, int])`
used to answer without drawing (the `World` overload was the only one with a
path).  It now transforms the vertices - view matrix from the camera, the
projection from the camera's own fields (a camera whose angles were never set is
the JSR's default perspective: 45 degrees, aspect 1, near 1, far 1000), the
model matrix from the `Transform` - and draws every triangle through
`nj_m3g_project` + the target's `Graphics.fillTriangle`, with the viewport
origin honoured (it used to be dropped, which put a viewport that was not at
(0, 0) in the wrong place).

What is honest about it: positions are read from the `VertexArray` as BYTE,
SHORT or FIXED (the last divided by 65536) with the `setPositions` scale and
bias applied; the fill colour is the appearance's `Material.DIFFUSE` when there
is one and the average of the per-vertex colours otherwise; indices come from an
explicit `IndexBuffer`, from a `TriangleStripArray`'s strip lengths, or from
consecutive vertex triples.  The `--stats` line carries `tris=` so a title can
be checked for it, and `tests/m3gdemo/M3GDemo.java` asserts the pixels of a
rendered triangle and of a strip.

That first version drew through the target's `Graphics.fillTriangle`, one
interpreter round trip per triangle and a flat colour per triangle: no depth,
no texture, no culling.  It has since been replaced by a rasteriser that writes
the target's pixels itself - see the next section.

One real bug came out of wiring it: `new TriangleStripArray(indices,
stripLengths)` stored the **indices** array as the strip lengths (the
constructor's arguments were swapped), so every strip was one vertex long and
no triangle was ever assembled.  `tests/test_m3g.c` had encoded the same
mistake, which is why it passed.

### The rasteriser: depth, texture, culling

`Graphics3D.render(VertexBuffer, ...)` now rasterises into the target surface
itself (`nj_midp_surface()` answers an ARGB8888 view of an `Image`, or the
screen when the target is a `Graphics` for the display), which is what makes
the three things a flat `fillTriangle` cannot express possible:

- **A depth buffer per target.**  Four fields on the `Graphics3D`
  (`_nj_zbuf`, `_nj_zw`, `_nj_zh`, `_nj_zframe`) hold a Java `int[]` of
  `1/w * 65536`, so **larger is nearer** and a freshly allocated (all zero)
  array already means "nothing has been drawn here" - the right initial state
  for free.  It is cleared once per frame and reset at `bindTarget`, because
  the depth buffer belongs to the target the way the viewport does.  The
  fixture renders two triangles three units apart in z in both draw orders and
  requires the nearer colour both times; without the test the far triangle wins
  when it is drawn second.
- **Perspective-correct texture mapping.**  Unit 0 of the `Appearance`:
  nearest sampling, `WRAP_REPEAT` (the default) or `WRAP_CLAMP`, `MODULATE`
  with the material colour.  s/w and t/w are interpolated and divided by the
  interpolated 1/w.  The fixture renders a 2x2 texture with a different colour
  at each corner and reads three pixels back.
- **Back-face culling** from the appearance's `PolygonMode` (default
  `CULL_BACK`, winding `WINDING_CCW`).  The sign convention is worth writing
  down because it is easy to get backwards: the JSR's front face is
  counter-clockwise in NDC (y up), and this projection flips y for the screen,
  so a **front face has a negative screen-space signed area**.  The first
  version had it inverted and culled exactly the wrong triangles; the fixture
  now pins both directions, and `CULL_NONE` must draw the same triangle.
  A triangle strip alternates winding between its triangles, so the strip
  walker swaps the first two vertices of its odd triangles.

- **Alpha blending and the alpha test** from the appearance's
  `CompositingMode`: `ALPHA` (the JSR default) writes
  `src·a + dst·(1−a)`, `ALPHA_ADD`, `MODULATE`, `MODULATE_X2` and `REPLACE`
  are implemented, and `setAlphaThreshold` discards a fragment **before** the
  depth write - so a cut-out sprite leaves no depth behind and the thing
  behind it shows through.  The source alpha comes from the texture
  (`Image2D.RGBA` texels); a `Texture2D.FUNC_REPLACE`/`FUNC_DECAL` texture
  keeps its own colour instead of being tinted by the material.
- **Fog** from `Appearance.getFog()`: `LINEAR` fades towards `Fog`'s colour
  between its near and far distances, `EXPONENTIAL` by `exp(-density·d)`,
  where d is the depth along the view axis - the fog coordinate GL ES uses by
  default.  The factor is computed per vertex and interpolated with the rest
  of the colour, which is what the fixed-function pipeline does; the fixture
  puts a triangle half way into a 4..6 fog and reads the pixel back.

What is still honest about it: no mipmaps, no bilinear filtering
(`FILTER_LINEAR` samples nearest), no `FUNC_ADD`/`FUNC_BLEND` (they need a
second texture unit), no per-fragment lighting (lighting is per vertex,
below), and no stencil or `PolygonMode` offset.

### Lighting, and two matrix conventions that cannot be mixed

`Light`, `Material` and `Appearance` were data holders until this round: the
values round-tripped and nothing read them.  A mesh with three normals and an
appearance with a material is now lit per vertex and Gouraud-interpolated
across the triangle:

- emission plus `Material.AMBIENT` against every `AMBIENT` light, then one
  term per other light: `Material.DIFFUSE * light * max(N.L, 0)`, plus a
  specular term `Material.SPECULAR * light * pow(max(N.H, 0), shininess)`
  when the material asks for one.  `N` is the eye-space normal (the upper 3x3
  of the model-view matrix), `L` points at the light, `V` at the eye.
- `DIRECTIONAL` lights shine along the **negative** z axis of their own
  transform, `OMNI` lights sit at the transform's origin, `SPOT` adds the
  `spotAngle` cone and the `spotExponent` falloff, and attenuation is
  `1 / (a0 + a1 d + a2 d^2)` with the JSR's `(1, 0, 0)` default.
- `Material.VERTEX_COLORS` multiplies the vertex colour into the ambient and
  diffuse terms.
- A `VertexBuffer` with no normals is not lit at all: the material's diffuse
  colour is used flat, which is what an unlit appearance means.  A title that
  never calls `setLight` keeps its geometry unlit and flat, exactly as before.
  The last part is a rule of its own, and the World path learned it the hard
  way: `3D拉力赛-进化汉化版_6366` never adds a light - no class of it calls
  `Graphics3D.addLight` - but every mesh of it carries a Material, so the JSR's
  own condition (a Material plus normals) turned lighting on, and with an empty
  light array the equation leaves the material's emissive colour, which is
  black.  Its track was a black silhouette on a white sky.  Lighting now also
  requires something to light with, `r.nlights > 0`, which is the same picture
  a buffer with no normals already gets.

The World path (`render(World)`) draws through this same pipeline: each
collected mesh keeps the world matrix the walk accumulated for it, its own
`VertexBuffer` and first-submesh `Appearance`, and a World that carries a
`Background` has its viewport cleared to that background's colour before the
meshes are drawn - the clear is the same native `Graphics3D.clear()` a title
would have called, so there is one implementation of it.  The title above sets
a white background of its own, which is why its sky is white: that is the
title's data, not a missing clear.

The two conventions are the part worth remembering, because getting them
crossed is silent:

| where a matrix comes from | storage | translation |
| --- | --- | --- |
| the Java API: any `Transform`, `setCamera`'s transform, `Camera.setGeneric` | row-major, acting on column vectors (`v' = M v`), which is what `m3g_xf_transform` and `mat_translate` implement | last **column**: `m[3]`, `m[7]`, `m[11]` |
| the M3G file loader, `nj_m3g_project()`, `m3g_cam_projection()` | the transpose of the above (OpenGL's column-major layout) | last **row**: `m[12]`, `m[13]`, `m[14]` |

Feeding a Java matrix to `nj_m3g_project()` produced a camera at the origin and
a model that never moved - every `translate()` and `rotate()` in a title was
silently dropped, and the scene still rendered, which is why it survived a
whole round.  `Render(VertexBuffer, ...)` now has its own reader (`jr_project`)
and multiplies with `mat_mul()`, the world path transposes the camera with
`mat_from_java()`, and the light transform is read the Java way (origin in the
last column, z axis in the third column).

The second half of the same problem: `setCamera(camera, transform)` stores the
transform **as the title set it** (because `getCamera()` hands it straight
back), but a transform that *places* a camera is not a view matrix - the view
is its inverse.  Both render paths now invert a copy at render time.
`Graphics3D.clear(background)` also clears the depth buffer now, not only the
colour: a second `render()` into the same target in the same frame was being
rejected by the first one's depths.

All of it is pinned by pixel checks in `tests/m3gdemo/M3GDemo.java` (130
checks): a directional light that lights the face towards it and only grazes it
after a 90-degree rotation, a buffer without normals staying flat, a model
transform moving the geometry across the viewport, the depth test, the texture
corners and the culling both ways.  The rounded shapes in
`浴火银河2完整版_7689` (Galaxy on Fire 2) are now shaded rather than flat.

A bug the fixture caught immediately, and the kind that only a pixel test
finds: the three edge functions were paired with the wrong vertices.  Each edge
function skips one vertex, so `e0` is the weight of `v2`, `e1` of `v0` and
`e2` of `v1`; using them in order rotated every vertex's colour and texture
coordinate onto its neighbour.  The triangle still covered the right pixels and
still had the right depth, so only the texture corners gave it away.

### Three more things between a 3D title and its first frame

Driving `浴火银河2完整版_7689` (Galaxy on Fire 2) until its engine runs found
the rest of the chain.  Each link was invisible until the one before it was
fixed, and the run went from an IOException with `m3g=0 tris=0` to
`m3g=461 tris=5763` with a textured planet on the screen:

| what the title did | what the runtime did |
| --- | --- |
| `Loader.load(byte[], int)` on a `.m3g` it had unpacked itself | checked the file identifier and then threw "this runtime has no object parser yet": the `String` overload had the parser, the `byte[]` overload never got it.  Both call one `m3g_build_objects()` now |
| `Image.createImage(bytes, 0, len)` of `data/interface/items.png`, a 5456x15 sprite sheet | refused it - "PNG image is larger than 4096x4096".  The single-dimension cap was ours, not the format's: 81,840 pixels is nothing.  The cap is now 65535 a side, with a 16.7-million-pixel bound, which is what actually costs memory |
| `bindTarget(g)` and then `render(world)` once a frame, never calling `setViewport` | skipped every frame ("no viewport, no pixels"): the viewport fields started at 0x0 and stayed there.  The JSR says the state is "viewport : undefined (reset at bindTarget)", so binding now resets it to the whole target, and `render()` still falls back to the target's size if it is unset |

The third has a footnote worth keeping: the fallback first asked the `Graphics`
for `getWidth()`, which is a `Canvas`/`Displayable` method - the title's thread
died on `NoSuchMethodError: Graphics.getWidth`, which is how the mistake was
found.  A `Graphics` reports `getClipWidth()`/`getClipHeight()`, and a Graphics
nobody has clipped is clipped to its whole target.

`tests/m3gdemo/M3GDemo.java` now asks for the reset as well ("a render after
bindTarget covers the target"), so a title that never sets a viewport cannot
regress to a black screen: 140 checks, 0 failures.

### A float stored in an array came back as 1.06535e9

The fixture's simplest check - put `1.0f` in a `float[]` and read it back -
failed.  The interpreter's `fastore` took the operand from the operand stack's
`.i` (the bits) and then wrote `(float)bits` into the array, so `1.0f`
(0x3F800000) was stored as the integer 1065353216 converted to float and read
back as 1.06535e9.  `src/core/nj_interp.c` now copies the four bytes instead.
Any title that kept float state in an array was reading noise; no collected
title's verdict changed, because they keep state in fields, which had been
fixed already.

A platform that cannot afford the rasteriser builds with
`-DNJ_ENABLE_M3G=0` and gets the previous behaviour back - the classes are
gone and a MIDlet that needs them gets `NoClassDefFoundError`, which is the
honest answer for a build that left the module out.

What was *not* acceptable is the middle ground of declaring the classes and
letting the calls do nothing: that moves the failure from "no such class" to
a wrong picture, and a wrong picture is the harder bug because it looks like
it works.

## Reporting a gap

The failure modes are designed to name themselves:

```
java.lang.NoSuchMethodError: javax/microedition/lcdui/Form.append
java.lang.UnsatisfiedLinkError: <init>
link com/example/Game: superclass not found: javax/microedition/lcdui/Canvas
link com/example/Game: interface this runtime does not provide: java/lang/Runnable
```

Run `tools/nj_inspect game.jar --parse-only` first: if that is clean, the class
files are fine and the problem is an API gap, which the message will name.

## Mascot Capsule V3: measured, not yet implemented

JSR 184 is not the only 3D API in the collection.  Two titles call Mascot
Capsule instead (鬼泣3D中文版_3376, 生化危机3D(CAPCOM经典)_3671), and `javap` over
every class in both says the caller-side surface is **13 classes and 41
distinct members** - the same order of magnitude as the JSR 184 work, with the
same shape: a scene description (Figure, FigureLayout, ActionTable, Light,
Effect3D, Texture) plus a rasteriser (`Graphics3D.renderFigure`,
`renderPrimitives`).

    AffineTrans.rotationY(I)V                        37 calls
    AffineTrans.multiply(AffineTrans, AffineTrans)V  26
    Vector3D.unit()V                                 24
    Graphics3D.flush()V                              21
    FigureLayout.setAffineTrans(AffineTrans)V        18
    Graphics3D.renderPrimitives(Texture,II,FigureLayout,
                                Effect3D,II,[I[I[I[I)V  15
    Util3D.sqrt(I)I                                  13

Two details that change the estimate:

  - The same API appears under **three package names**: `com.mascotcapsule.
    micro3d.v3`, the older flat `com.mascotcapsule`, and Capcom's own
    `com.capcom.mascotcapsule`.  The class tables are constant data, so the
    aliases cost table entries, not code.
  - `Util3D.sqrt(I)I` returns an **int** - fixed-point arithmetic throughout.
    A rasteriser here cannot be float-only and still match what the titles
    expect.

Nothing of this is written yet; the measurement is recorded so the next round
decides with a number instead of an impression, which is how JSR 184 was
scoped.

## Every collected title can now resolve what it asks for

`tools/fetch_52emu_games.py` has collected 395 real titles.  A sweep over all of
them (8 frames each, `--port headless`, 240x320, `--heap 16`) looks for the
runtime *naming* something it does not have:

    NoClassDefFoundError | NoSuchMethodError | NoSuchFieldError
    UnsatisfiedLinkError | ClassNotFoundException

Result: **0 of 395**.  Not one title is stopped by a missing class, method,
field or unbound native any more.

This is worth stating precisely, because it is not the same as "395 titles
work".  What it says is that the *static* surface -- the vocabulary of classes
and members a MIDlet can name -- is complete for everything collected here.  A
title can still fail on behaviour: drawing that does not reach the picture,
`.m3g` containers this runtime declines to parse, Mascot Capsule models with no
geometry parser, or the game's own bugs.  Those failures no longer look like
"the emulator is missing X"; they look like "the emulator does X differently".

Re-run it with:

    for jar in tests/games/*.jar; do
      build/nj2me --port headless --size 240x320 --heap 16 --run-frames 8 "$jar" 2>&1 \
        | grep -oE "(NoClassDefFoundError|NoSuchMethodError|NoSuchFieldError|UnsatisfiedLinkError|ClassNotFoundException): [^ ]+"
    done | sort -u

The count is a moving target only in one direction: new titles get fetched, and
if one names something new, this is how it gets found.

## 0e: the whole corpus under ASan + UBSan + LSan

Built on a clean detached worktree at the revision named below, not on the
shared tree, so an in-flight edit by another writer cannot poison the build:

    git worktree add --detach /tmp/wt118 <rev>
    cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNJ_SANITIZE=ON
    cmake --build b -j 4 --target nj2me-cli
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:halt_on_error=0
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
    # every title: ./b/nj2me --port headless --size 240x320 --heap 16 --run-frames 10

| revision | titles | sanitizer findings | non-standard exits | timeouts |
|---|---|---|---|---|
| 298-title corpus (earlier) | 298 | 0 | 0 | 0 |
| `b869aff` | 395 | **0** | 0 | 0 |
| `406fc7b` (Mascot titles only) | 3 | **0** | 0 | 0 |

Zero AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer findings
across 395 real titles, and zero timeouts -- the titles that used to hang all
end at the frame budget now.

Two notes on how to read this, because a sweep is easy to run in a way that
reports a comforting nothing:

* **An empty findings file is not a result.**  The first version of this script
  used `set -e`, and a MIDlet exiting non-zero is a normal outcome, so the sweep
  aborted at the first such title and left `/tmp/san118.txt` empty.  That file
  is byte-identical to one written by a completed, clean sweep.  Check the
  script's own summary line (`ran=… findings=…`), not the file's emptiness.
* The sweep covers the revision it was built at.  `406fc7b` is a three-line
  move of a statistics counter in `src/midp/nj_lib_mascot.c`, so the three
  titles that can reach that code were re-run at it rather than re-sweeping all
  395.

## The Mascot Capsule rasterizer cannot be validated by this corpus

Four collected titles reference `com.mascotcapsule.micro3d.v3`:
鬼泣3D中文版_3376, 生化危机3D(CAPCOM经典)_3671, 浴火银河_10189, 浴火银河_3621.
All four read `tris=0`, and the rasterizer they would exercise is real code
(`mascot_g3d_renderPrimitives` projects through the layout matrix and fills
triangles).  Two probes settled why.

**They never call it.**  A probe at the top of `mascot_g3d_renderPrimitives`,
plus one on each early-return path, run over all four titles for 60 frames:

    entered=0  early=0      (all four titles)

**The probe works.**  A negative result is worth nothing without a positive
control, so `test_mascot` -- which does drive the rasterizer -- was run against
the same build:

    PROBE renderPrimitives ENTERED   x2      (test_mascot, 40 checks / 0 failures)

So `tris=0` is the **correct** reading here: there is nothing to rasterize.
The disassembly references are dead paths -- `javap` counts references in the
bytecode, not executions, and this is the second time in this project that
distinction mattered.

What this means for the module: the Mascot code is exercised only by its own
unit test, not by any real title, so it is held to the same standard as the
`.m3g` container parser -- implemented, tested against the API as documented,
and **not** claimed to work on real software, because nothing collected here
reaches it.
