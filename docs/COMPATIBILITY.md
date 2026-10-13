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
| JSR 135 (Mobile Media) | tone and sampled PCM playback, WAV from a stream or a JAR resource, MIDI rendered by the runtime itself (or by the platform's synthesizer through the port's MIDI hooks), and AMR narrowband decoded by the runtime itself; no video. MP3, AAC and anything else the platform can decode arrive through `nj_port.audio_decode`, which is what the Android port does with MediaCodec |
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
| `Integer`/`Long`/`Short`/`Byte`/`Character`/`Boolean`/`Float`/`Double` | constructors, `*Value`, `parseInt`, `toString(radix)`, `toHexString`, `valueOf` |
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
