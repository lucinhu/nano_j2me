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
| JSR 82 (Bluetooth) | classes present; every operation refuses with `BluetoothStateException`, because there is no radio — a MIDlet that is told "no radio" keeps working, one that is told "no such class" does not |
| JSR 120/205 (messaging) | classes present; connections refuse the same way |
| JSR 135 (Mobile Media) | tone and sampled PCM playback, WAV from a stream or a JAR resource; no video. A platform codec plugs in through `nj_port.audio_decode`, so AMR/MP3 can be handed to the host instead of refused |
| JSR 184 (Mobile 3D Graphics) | the classes, scene graph, transform maths, and the member surface real titles use, behind `NJ_ENABLE_M3G`. **No rasterizer and no `.m3g` container parser**, so nothing collected here renders 3D through it (see below) |
| Mascot Capsule Micro3D v3 | classes, fixed-point transform maths and a real triangle rasterizer, behind `NJ_ENABLE_MASCOT`. **No collected title reaches the rasterizer**, so it is exercised only by its own unit test (see below) |

Anything not implemented raises. That is deliberate: a
`java.lang.NoSuchMethodError` naming the method is a five-minute fix, whereas a
silently-ignored call is a two-day one. The one exception is a radio or a
sensor that does not exist on the host: those classes are present and refuse at
run time, because "this device has no Bluetooth" is a normal answer a MIDlet is
written to handle, while "this build has no Bluetooth class" is not.

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
| `String` | complete; both the embedded and the wrapped representation |
| `StringBuffer` | complete |
| `StringBuilder` | present as a `StringBuffer` subclass — not in CLDC, but every modern javac lowers `a + b` to it |
| `Math` | complete, plus `cbrt`/`rint` beyond CLDC |
| `System` | `currentTimeMillis`, `nanoTime`, `arraycopy`, `identityHashCode`, `gc`, `exit`, `getProperty`, `out`, `err` |
| `Thread`, `Runnable` | green threads, `sleep`, `join`, `yield`, `wait`/`notify` on `Object` |
| `Throwable` and the full exception hierarchy | complete, with `toString`/`getMessage`/`printStackTrace` |
| `Class` | `getName`, `isArray`, `isInterface`, `isInstance`, `getSuperclass`, `forName` |
| `Integer`/`Long`/`Short`/`Byte`/`Character`/`Boolean`/`Float`/`Double` | constructors, `*Value`, `parseInt`, `toString(radix)`, `toHexString`, `valueOf` |
| `Runtime` | `gc`, `freeMemory`, `totalMemory`, `exit` |

`System.getProperty` answers for `microedition.platform`, `.configuration`,
`.profiles`, `.locale`, `.encoding`, `line.separator`, `java.version`. Anything
else returns `null`, which is what a real device does for unknown keys.

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

`Connector.open` for `file://` only. Networking raises
`ConnectionNotFoundException` rather than pretending: there is no socket stack
on a bare-metal target, and a game that silently gets a null stream from
`http://` is harder to diagnose than one that throws.

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

## javax.microedition.m3g

Implemented, and tested end to end by `tests/test_audio.c` (24 assertions)
driving `tests/fixtures/audiodemo.jar`, built by `tools/make_audio_jar.sh`.

| | |
|---|---|
| `Manager` | `createPlayer(InputStream,String)`, `createPlayer(String)`, `getSupportedContentTypes/Protocols`, `playTone`, `TONE_STOP`/`TONE_BLOCKING`/`TONE_CONSTANT_DURATION`, `TONE_DEVICE_LOCATOR` |
| `Player` | the full state machine, `realize`/`prefetch`/`start`/`stop`/`deallocate`/`close`, `setLoopCount`, `getState`/`getDuration`/`getMediaTime`, `add`/`removePlayerListener`, `getControl(s)` |
| `PlayerListener` | `started`, `stopped`, `closed`, with the event strings interned so the reference comparison the javadoc recommends actually works |
| `ToneControl` | real JSR-135 parsing: big-endian length, note/duration triples, malformed input refused at `realize()` rather than mid-frame |
| `VolumeControl` | `setLevel`/`getLevel`/`setMute`/`isMuted` |
| WAV | RIFF/WAVE PCM, 8- and 16-bit, mono and stereo, from a stream or a JAR resource |

`TONE_BLOCKING` blocks the CALLING JAVA THREAD cooperatively: it returns
`NJ_ERR_TIMEOUT` and the interpreter suspends that thread, exactly as
`Thread.sleep()` does. Sleeping the C thread instead would freeze every other
thread in the VM, which for a game means the picture stops.

**Not implemented, and refused by name rather than silently:** `http://` and
`rtsp://` raise `MediaException` naming the protocol. There is no MIDI, no video,
no audio capture, and no custom controls beyond the two above.

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

## Known behavioural differences

| Area | Difference |
|---|---|
| Threads | cooperative, switched at an instruction-count boundary. A tight loop with no allocation or I/O still yields (the quantum is enforced), but a thread that never blocks cannot be preempted *mid-instruction* — irrelevant for correctness, occasionally visible in interleaving |
| `synchronized` | compiles and runs, but does not lock: a cooperatively scheduled thread is never preempted inside a critical section |
| `Object.wait`/`notify` | implemented; a `wait` with no notifier left is detected and the game is reported as finished rather than hanging |
| `System.exit` | stops the VM cleanly and the code reaches the host |
| `Class.forName` | works for classes the archive actually contains plus the built-in library |
| Reflection | absent, as in CLDC |
| Networking | `file://` only |
| `TimeZone` | UTC only |
| Locale | `en-US` fixed; `toLowerCase`/`toUpperCase` are ASCII-only |
| Fonts | three built-in bitmap sizes from Liberation Mono; CJK needs a glyph provider installed through the font fallback hook |
| Image size limits | 8192 px per axis, checked in 64-bit before any allocation |

## Codec coverage

Decoding goes through a registry
([`include/nj2me/nj_codec.h`](../include/nj2me/nj_codec.h)) so a platform can
substitute a hardware decoder; see [PORTING.md](PORTING.md).

| Format | Built in | Notes |
|---|---|---|
| PNG | yes | all colour types, bit depths 1–16, Adam7 interlacing, `tRNS` |
| JPEG | adapter ready | a software baseline decoder is optional; without one, a JPEG fails with an actionable message rather than being mis-detected |
| GIF | no | a provider can be installed |
| BMP | no | a provider can be installed |

Benchmark titles overwhelmingly use PNG for sprite art. JPEG appears mostly for
splash screens and photographic backgrounds, which is why it is the first thing
to hand to a hardware block.

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

It is being implemented, behind `NJ_ENABLE_M3G`.  The order below is the
order titles stop failing in, and it is also the order of the work:

1. the classes and the scene graph, so a title runs at all and can draw its
   2D interface around an empty 3D view;
2. the transform stack, camera and software rasteriser, so that view fills
   in;
3. `Loader.load()` and the `.m3g` container, for the titles that read a
   resource rather than build their scene from code.

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
