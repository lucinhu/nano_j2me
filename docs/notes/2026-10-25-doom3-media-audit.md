# 毁灭战士3: the intro video that never appears, and the 120 seconds of PCM

Audit of what this build does with `tests/games/毁灭战士3_10598.jar`'s intro
movie (`vid/doom.3gp`, MPEG-4 Part 2 video + AMR-NB audio) and its sound track.

Snapshot: working tree at commit `4653ee7`, 2026-10-25 ~21:05; runtime evidence
from `build/nj2me` sha256 `2dde3224…` (20:56, built when the tree still had
`696ee99` as HEAD; `src/midp/nj_lib_media.c`, `src/midp/nj_h263.c`,
`src/core/nj_avcodec.c` are byte-identical between the two trees, so the
anchors below hold for both). Nothing in this note was inferred from a
screenshot alone.

## Short answer

The intro is skipped by the game itself, in its own `catch (Exception)`:

1. `Doom.video()` asks `Manager.getSupportedContentTypes(null)` for a video
   content type and looks for the strings `video/3gppvideo` and `video/mpeg`.
   This runtime returns a one-element array, `{"audio/x-wav"}`
   (`src/midp/nj_lib_media.c:2585-2611`), and neither literal the game uses is a
   MIME type any implementation would advertise, so the scan can never match
   (see (a)). `type` stays `null`.
2. `Manager.createPlayer(stream, null)` sniffs the stream to MP4
   (`sniff_media()` at `src/midp/nj_lib_media.c:1464`) and enters the MP4 video
   branch. The demuxer reads the track (`mp4v`, 320×240, 189 samples, 15 fps),
   the only installed video provider is H.263, and H.263 refuses MPEG-4 Part 2
   by design (`src/midp/nj_h263.c:1197-1202`). No video Player is built.
3. The code falls through to the AMR branch and builds an **audio-only**
   `Player` (`kind == 1`) from the movie's AMR track: 624 AMR-NB frames decoded
   into 199,680 bytes of PCM (99,840 PCM frames, 12.48 s). Its only `Control`
   is `VolumeControl`; `getControl("VideoControl")` returns `NULL`
   (`src/midp/nj_lib_media.c:2897-2957`).
4. Doom's bytecode checkcasts the `NULL` (harmless) and immediately calls
   `initDisplayMode()` through it. The interpreter throws the NPE the user saw
   at `pc=142`; the game's own `catch (Exception)` handler at bytecode 224 (its frame
   reports pc=229, past the `printStackTrace` call) prints
   a stack trace and returns. The intro is over before its first frame.
5. The AMR track of the movie is decoded at `createPlayer()` time but the
   game's `player.start()` is never reached, so none of it is played. The PCM
   the port does receive is the game's **theme music**, started 12 s later:
   960,160 frames = one full pass of `snd/d3theme.amr`. That number is a
   decoded-and-pushed counter, not wall-clock playback (see (b)).

The user's other console line — `Erroor load texture !` with
`java.io.IOException: /men/Doom3bg11bak.png` — is unrelated to video: the game
references a PNG it did not ship and prints that text from its own tolerant
texture loader. Details in "The console lines".

## What the game asks the runtime to do

Disassembled from `Doom/Doom.class` (extract the class file, then
`javap -c -p`; `javap -classpath` refuses this archive's zip layout):

```
Doom.video():Ljavax/microedition/media/Player;   // called from Doom.run
  0   Display.getDisplay(this).setCurrent(m)
  10  aconst_null; astore_1                       // type = null
  17  Manager.getSupportedContentTypes(null)      // String[] in local 2
       loop: element.toString().equals("video/3gppvideo") -> local 1
             element.toString().equals("video/mpeg")      -> local 1
  73  getResourceAsStream("/vid/doom.3gp")
  83  Manager.createPlayer(InputStream, type)     // type == null here
  92  player.prefetch()
 101  player.realize()
 110  player.getControl("VideoControl") -> checkcast VideoControl
 137  vidCtrl.initDisplayMode(1, m)               // 1 = USE_DIRECT_VIDEO
 142  checkcast javax/microedition/lcdui/Item
 148  vidCtrl.setDisplaySize(240,320)
 163  vidCtrl.setVisible(true)
 173  Display.setCurrent(m)
 183  player.start()
 192  Thread.sleep(12000)
 201  vidCtrl.setVisible(false)
 catch (Exception) at 224: printStackTrace(); return player at 229
```

`USE_DIRECT_VIDEO` is the correct mode for playing into the MIDlet's own
`Canvas`: it returns `null` by specification, which is why the `checkcast Item`
at `pc=142` is valid device code. The game never paints the video itself; it
hands the Canvas to the implementation, starts the player, sleeps for the
clip's length, and hides the video again.

The game has a second, unrelated 12 s sleep of its own: `Doom/m.run()` calls
`saund()` (create the theme player), then `Thread.sleep(12000)`, then
`setFullScreenMode(true)` and `s(g3d,g)`, which begins with `playSoundZ()`
(theme `Player.start()`). On this runtime that sleep is the only reason the run
shows a starfield splash for the first ~12 s; it is not the intro.

## (a) `Manager.getSupportedContentTypes(null)` in this build

The native is `media_supported_types`
(`src/midp/nj_lib_media.c:2585-2611`) and it builds exactly one string:

```
nj_string_new(vm, "audio/x-wav");          // :2598
nj_array_new(vm, "java/lang/String", 1u);  // one element
a->u.refs[0] = s;                          // :2607
```

The function's comment says "One element: `audio/x-wav`, which is what this
implementation decodes" — stale: the runtime also plays MIDI, AMR/AMR-WB, MP3,
and AMR-inside-MP4. The `protocol` argument is not read at all, so even
`getSupportedContentTypes("video")` would return `audio/x-wav`. The sibling
`media_supported_protocols` (`:2613`) returns the one string `{"tone"}`. Both
are registered in `nj_lib_media_table` (`:4093-4096`) and `manager_methods`
(`:3934-3941`).

Confirmed in the shipped binary, not just the source: `nm build/nj2me` gives
`media_supported_types` at `0xe03a0`; `objdump -d` around it shows
`mov $0x1,%edx` before `nj_array_new` (one array element), and the referenced
`.rodata` literal at `0x1a8052` is exactly `61 75 64 69 6f 2f 78 2d 77 61 76`
(`audio/x-wav`), with `"java/lang/String"` at `0x1a2e89`.

Spec: JSR-135 `Manager.getSupportedContentTypes(String protocol)` says that a
`null` protocol returns the content types of **all** supported protocols.
Under-reporting is not a crash, but this runtime advertises WAV only while
decoding several other types, so the array (and the ignored protocol argument)
is not a truthful answer to the query.

Does Doom's scan ever match? No — on this or any implementation. The two
literals are `video/3gppvideo` and `video/mpeg`. The first is not a MIME type
(the 3GPP movie type is `video/3gpp`); the second is MPEG-1 video, not an
ISO-BMFF/3GP file. The scan uses `String.equals`, not a prefix test, so even a
device that advertises `video/3gpp` cannot satisfy it. Even if it could,
`audio/x-wav` does not match. Therefore `type` is always `null` and
`createPlayer(InputStream, null)` is used.

Passing `null` as the type is legitimate MMAPI (the implementation sniffs the
stream), so the game does play its intro on real phones through the sniffing
path. The typo costs it nothing there; it costs nothing here either, because
sniffing still produces an MP4 player. What breaks here is what sniffing finds
behind that player (next section).

## (b) Which `createPlayer` branch runs, and what Player comes out

`Manager.createPlayer(InputStream, String)` is
`media_createPlayer_stream` (`src/midp/nj_lib_media.c:1859`). With a `null`
type it sets the player's content type from the sniffer
(`:1948-1954`): `sniff_media()` sees `ftyp` at offset 4 and reports `MP4`, and
`mime_for_container()` maps MP4 to `audio/mp4` (`:1584`). Then, in order:
WAV parser (no), MIDI (`:1973`, no), port audio hook (`:2086`, no), MP3
(`:2124`, no), MP4 video (`:2183-2242`), AMR (`:2245-2403`), then the refusal
at `:2404-2406`.

**The MP4 video branch** is entered and fails inside itself:

- `nj_mp4_open()` succeeds on `vid/doom.3gp`: codec `mp4v` (0x6d703476),
  320×240, 189 samples, 15 fps, timescale 90000, duration 1,134,000
  (12.6 s). Probe log: `.local-tools/debug-runs/doom-media-audit-01/probe-assets.log`.
- `nj_avcodec_find_video()` (`:2188`) walks the installed providers. The only
  video provider is H.263, and its `accepts()` returns 1 for **any** buffer
  with `ftyp` at offset 4 (`src/midp/nj_h263.c:1097-1113`) — including the
  audio-only `d3theme.amr`, as the probe shows.
- Its `open()` then refuses the codec: `"H.263: MPEG-4 Part 2 video is not
  decoded"` (`src/midp/nj_h263.c:1197-1202`; the probe reproduces
  `accepts=1, open=NULL`).
- `dec` is `NULL`, so the branch does not build the kind-2 Player and the code
  falls out of the `if`.

**The AMR branch** (`:2245`) then matches because the sniffed container is
`MP4` as well as `AMR`. It finds the `samr` track with
`nj_amr_mp4_frames()` = 624 frames, allocates `624 × 160 × 2 = 199,680` bytes
of PCM (`:2348`), decodes the whole track once, and returns
`player_new(vm, 1)`. The resulting object:

| Property | Value |
| --- | --- |
| Java class | `javax.microedition.media.Player`, `kind == 1` (audio) |
| Content type | `audio/mp4` (from the sniffer, not `video/3gpp`) |
| Decoded data | 624 AMR frames → 99,840 PCM frames (16-bit mono, 8 kHz) |
| `F_P_RATE` / `F_P_CHAN` | 8000 / 1 (`:2357`, `:2358`) |
| Duration | 12.48 s (`:2361`) |
| Controls | one `VolumeControl` only |
| State after `prefetch()` | 300 (realized, prefetched), never started, never closed |

`getControl("VideoControl")` is `NULL`: `player_getControl()`
(`:2897-2957`) strips the package name and has branches only for
`ToneControl` (kind 0), `VolumeControl` (always), and `MIDIControl` (MIDI
players); everything else returns `NULL` at `:2954-2957`, with a comment that
`NULL` is what MIDP requires for an unsupported control.
`getControls()` (`:2968`) returns a one-element array containing only the
`VolumeControl`, because this Player's kind is neither tone nor MIDI.

So `getControl("VideoControl")` returning null is *correct* for a runtime
whose MP4 player is an audio player. There is no `VideoControl` object at all:
the class is a 10-method abstract interface stub
(`src/midp/nj_lib_media.c:3999-4020`), registered so that MIDlets can name and
checkcast it, with a comment that names this exact game: "`getControl()` keeps
returning null, the game's `catch (Exception)` takes the NPE that follows, and
the intro is skipped instead of the game." It has no natives and no fields.

### Why the NPE is at `pc=142`

The `invokeinterface` for `VideoControl.initDisplayMode` starts at bytecode 137
and is 5 bytes long. In `src/core/nj_interp.c` the invoke handler has already
advanced the interpreter PC past the 2-byte index and 2 reserved bytes
(`:2326-2328`), and the `THROW` macro stores `fr->pc = pc` before throwing
(`:1069-1074`). For a null receiver the handler throws
(`:2402-2405`), so the logged PC is the *next* instruction, the `checkcast
Item` at 142 — matching

```
thrown: java/lang/NullPointerException at Doom/Doom.video()Ljavax/microedition/media/Player; pc=142: (no message)
```

### Why the stack trace mixes threads

The game's `printStackTrace()` is the emulator's live-chain printer, not a
captured trace: `src/core/nj_lib_object.c:616-628` documents that limitation,
and `throwable_trace()` (`:708-756`) asks `nj_vm_describe_threads()`
(`src/core/nj_vm.c:358`) to walk **all** threads, innermost frame of each first,
with the thread header lines filtered out. That is why the user's output shows

```
at Doom/Doom.<init>()V pc=96          <-- main thread, blocked in Thread.join
at java/lang/Throwable.printStackTrace()V pc=0
at Doom/Doom.video()... pc=229        <-- the game thread, catch block at 224
at Doom/Doom.run()V pc=4
at Doom/m.run()V pc=0
```

`Doom.<init>()` starts two threads at bytecodes 74 and 89 and joins at 93, so
its main thread (pc=96) is merely waiting there; the exception was thrown in
the intro thread. The trace order is an emulator artifact, not three nested
calls.

### Does the movie's AMR track decode? Does it play?

Decoded: **yes**, at `createPlayer()` time — the whole 12.48 s track is turned
into 199,680 bytes of PCM before the NPE ever happens. Played: **no** — Doom's
`player.start()` is bytecode 183, after the failing `getControl()`, and it is
never reached. `realize()`/`prefetch()` do not push PCM; only `start()` does
(`player_start()`, `:2701`, calls `pcm_push(vm, self, 1 << 20)` at `:2761`).
The proof is the port counter, below: the intro track would have added
624 × 160 = 99,840 frames; it never does.

## Where `observed: pcm_frames=960160` comes from

`pcm_frames` counts **PCM frames** (16-bit mono samples), not bytes: it is the
headless port's "frames accepted, total" counter
(`src/port/posix/nj_port_headless.c:90`, incremented at `:607`, exposed by
`nj_port_headless_pcm_frames()` at `:639-641`, printed by
`src/runner/nj_run.c:1349-1354`).

  960160 = 6001 × 160

`snd/d3theme.amr` is an MP4 file with one AMR-NB track of 6001 frames
(`nj_amr_mp4_open` in the probe log; the 2026-10-21 note already recorded
6001 frames / 216,624 bytes). AMR-NB frames are 20 ms = 160 samples at 8 kHz,
so one full pass is 960,160 samples = 120.02 s. That is the game's theme
music, not the intro.

Timeline:

- `Doom/m.run()` creates the theme Player via `saund()` (`createPlayer` +
  `realize` + `prefetch` + `VolumeControl.setLevel(60)`) but does not start
  it, then sleeps 12,000 ms for its own splash.
- After the sleep, `s(g3d, g)` starts with `playSoundZ()` → `Player.start()`
  → `player_start()` → `pcm_push(vm, self, 1 << 20)`. 1,048,576 frames is
  more than 960,160, and the headless port's `pcm_write()` accepts
  everything offered (`nj_port_headless.c:603-611`), so the entire theme is
  accepted in one call: the counter goes 0 → 960,160 and the Player is never
  registered on the pump list.
- The counter is cumulative for the run and is not reset. No loop count is
  set, so the theme plays once; 960,160 is the ceiling. (A looping count would
  have shown 1,920,320 etc., not this number.)

Measured with the pre-M4V snapshot
`/tmp/doomaudit/nj2me-audit-34767c102aca` (built from `build/nj2me` at 20:45),
toggling only `--run-frames`:

| `--run-frames` | wall time | `pcm_frames` |
| --- | --- | --- |
| 1, 3, 10, 30, 60, 120 | 12.06 s at 120 | 0 |
| 130, 150, 170, 190, 210, 230, 240, 480 | 12.52 s at 130 | 960,160 |

So the counter stays zero exactly while the game is inside its own 12 s sleep,
then jumps to one full theme pass. The headless run finishing "120 s of audio"
in ~18 s is expected: the platform buffer is emptied as fast as the emulator
offers frames. A real port (`pcm_free()`) takes only what the device buffer
holds and the Player stays registered until the rest is pushed, pacing at
8 kHz; `media_video_step` has the analogue problem for video (see (d)).

Evidence runs: `.local-tools/debug-runs/doom-media-audit-pcm/f<N>/console.log`
(N = 1, 3, 10, 30, 60, 120, 130, 150, 170, 190, 210, 230, 240, 480).

## (c) Is skipping the intro correct behaviour?

For a runtime without a video decoder or `VideoControl`: **yes, the game is
supposed to skip it, and the NPE is the game's own bug, not an emulator spec
violation.**

- MMAPI says `Player.getControl(String)` returns `null` when the Player does
  not support that control type. Doom stores that null, checkcasts it (legal),
  and dereferences it — no null check anywhere. Its `catch (Exception)` is
  clearly written to tolerate exactly this class of device.
- `createPlayer()` is the only debatable part of this runtime's behaviour. A
  device that cannot render a `video/3gpp` file may either throw
  `MediaException` or build a Player for the tracks it can play. This build
  chooses the latter: it returns an audio-only Player whose content type it
  has relabelled `audio/mp4`. That is defensible ("we can play this file's
  sound") but it is not what a device that supports `video/3gpp` would do, and
  `audio/mp4` is the wrong content type for a 3GP movie. Either choice ends at
  the same caught NPE, so it does not change what the user sees; it only
  changes how the failure is described.

What a real handset of the era would do (S60 3rd edition, Sony Ericsson with
MPEG-4 Simple Profile + AMR-NB + `VideoControl`): `createPlayer(stream, null)`
sniffs a video/3gpp Player, `getControl("VideoControl")` returns a real
control, `initDisplayMode(USE_DIRECT_VIDEO, canvas)` returns `null` as
specified, `setDisplaySize(240, 320)` and `setVisible(true)` show the video in
the Canvas, `start()` plays the 12.6 s clip with its AMR sound, the game
sleeps 12 s, hides it, and continues to its own 12 s splash. The intro plays.
A handset without video support behaves like this build: the plan is skipped or
the NPE is caught, and the game continues. Note that the game ships no fallback
image for a skipped intro, so on such devices the user simply goes straight to
the splash/loading screen.

**FreeJ2ME-Plus** (`.local-tools/freej2me-plus`, headless adapter
`.local-tools/run-freej2me-headless.sh`): for this title it never even reaches
the intro. In `.local-tools/debug-runs/doom-media-audit-f2p/console.log` the
MIDlet constructor dies with an invoked NPE:

```
java.lang.reflect.InvocationTargetException
caused by java.lang.NullPointerException: Cannot invoke "javax.microedition.m3g.World.find(int)" because "this.worldMars" is null
    at Doom.m.gg(...)
    at Doom.m.<init>(...)
    at Doom.Doom.<init>(...)
```

All five captured shots (3/7/11/15/17 s, 924 bytes each) are blank white
frames; the game never starts. Its code would not show the intro either:
`Manager.getSupportedContentTypes()` returns nine audio-only types (no
`video/*`), `PlatformPlayer` maps a 3GP stream to `unknown` + `BasicPlayer` with
`disableControls`, and `getControl("VideoControl")` returns a `VolumeControl`
(via `controls[0]`) or otherwise only Volume/Tempo/MIDI/Tone controls — never a
`VideoControl`. Doom's `checkcast VideoControl` would throw a
`ClassCastException`, caught by the same `catch (Exception)`, so the intro would
be skipped there too. The comparison therefore gives no working counterexample,
only a second no-video implementation.

## (d) What the intro needs, and what is already in the tree

For `vid/doom.3gp` to appear and sound, six things must exist; four of the six
already exist or are half done:

| Piece | State (2026-10-25 ~21:05) |
| --- | --- |
| ISO-BMFF demux (`nj_mp4`: sample tables, AMR track, video track geometry) | **Done** (`src/midp/nj_mp4.c`; round 17 in `docs/CODECS.md:293`) |
| A `kind == 2` video Player: RGB565 buffer, one sample per emulated frame in `nj_media_frame()`, `endOfMedia` | **Done** (`docs/CODECS.md:237-273`; `src/midp/nj_lib_media.c:2183-2242`, `:1184-1227`, `:1230`) |
| `nj_avcodec` provider seam (`find_video`, replaceable video decoder) | **Done** (`src/core/nj_avcodec.c:100-116`, install/reset at `:57`/`:157-175`) |
| H.263 decoder as the one video provider (proof the seam works) | **Done** (`src/midp/nj_h263.c`; ops table `:1343-1350`; `docs/CODECS.md:38-68`) |
| MPEG-4 Part 2 (Simple Profile) decoder for `mp4v` | **In progress.** Framing only: `fd02094` parses VOL from the reference decoder, `4653ee7` parses VOP headers; `src/midp/nj_m4v.c` is 290 lines of start-code/VOL/VOP parsing (`nj_m4v_accepts()` exists at `:274`) but there is no macroblock layer, no `nj_av_video_ops`, and it is not registered in `nj_avcodec_reset()` — the id exists only as a plan in `include/nj2me/nj_avcodec.h:76` (`0x0202u`) and nothing in `src/` names it. The handoff says to reuse the H.263 IDCT, 4:2:0→RGB565 and half-pel MC, add B-16/B-17 VLC tables and the macroblock layer, then register the provider (`docs/CODECS.md:312-333`, `:354-395`). |
| `VideoControl` object model and Canvas presentation | **Not started.** Only the 10-method interface stub at `src/midp/nj_lib_media.c:3999-4020`; `getControl()` has no branch for it. Nothing reads `F_P_VBUF` except `media_video_step()` (defined `:173`, read `:1188`, written `:2219`, field table `:3856`). `docs/CODECS.md:70-76` describes VideoControl frames going "through the same painting path the rest of LCDUI uses" — that is the intended design, not the shipped code. |
| AV: one Player that plays both tracks, and wheel pacing | **Not started.** `createPlayer` builds either a video Player or an audio Player, never both; the intro's AMR track is decoded but belongs to the audio-only fallback Player that is never started. `media_video_step()` decodes one sample per emulated frame with no DTS comparison, so the 189-sample clip would run at 30 fps in 6.3 s instead of 12.6 s. |

Concretely, the shortest road to a playing intro is:

1. Add the MPEG-4 Part 2 macroblock layer to `src/midp/nj_m4v.c`, register it
   as `NJ_AV_VIDEO_MPEG4` in `nj_avcodec_reset()`. The H.263 provider's
   `open()` must stop claiming MP4V files (it already refuses them; the M4V
   `accepts()` becomes the one that claims them).
2. Implement `VideoControl`: a real class whose methods are bound to the
   Player. `initDisplayMode(1, Canvas)` records the Canvas and starts
   presenting `F_P_VBUF`; `setDisplaySize(240, 320)` decides the rectangle
   (the movie is 320×240, so the game's portrait 240×320 target scales it);
   `setVisible` toggles presentation; `getSnapshot` returns an `Image`;
   `getDisplay*`/`getSource*` report geometry. `Player.getControl()` must
   return it for `kind == 2` Players, and LCDUI must actually paint the buffer
   each frame (today nothing does).
3. Either build an AV Player (`createPlayer` keeps both decoders + the AMR
   PCM and the pump pushes video frames and audio chunks together, honouring
   sample DTS), or accept a silent video for the first cut. Doom calls
   `start()` once, so a single Player has to cover both tracks to match a
   phone.
4. Optionally make `getSupportedContentTypes()` truthful (list `audio/x-wav`,
   `audio/amr`, `audio/midi`, `audio/mpeg`, `video/3gpp` …). It cannot help
   this title because of the game's literal strings, but it is the spec-correct
   answer and other titles read it.

For reference, if the decoder had existed, the video branch would have
allocated `320 × 240 × 2 = 153,600` bytes for the frame buffer and reported
`video/3gpp`, 320×240, 189 frames at 15 fps, with duration 12.6 s. The heap
cost paid today is the 199,680-byte PCM array plus the 624-frame table — decoded
and thrown away.

## The console lines, explained

Run: `.local-tools/debug-runs/doom-media-audit-current/` —
`./build/nj2me tests/games/毁灭战士3_10598.jar --port headless --size 240x320
--fps 30 --rms-dir <run>/rms --run-frames 280 --dump-frames <run>/frames/
--dump-every 60 --stats`; exit status 0.

| Console line | Source |
| --- | --- |
| `running Doom.Doom (Doom3D 1.0)` | runner banner |
| `thrown: java/lang/NullPointerException at Doom/Doom.video()… pc=142: (no message)` | interpreter `THROW` at `src/core/nj_interp.c:2402-2405`, logged by `nj_throw()` at `src/core/nj_vm.c:1090-1094`; null `vidCtrl` receiver for `initDisplayMode` at bytecode 137, logged PC already advanced to 142 |
| `exception: java.lang.NullPointerException` + `at Doom/Doom.<init>()V pc=96 …` | the game's `catch (Exception)` → `printStackTrace()`; live-chain printer walks all threads (see (b)) |
| `thrown: java/io/IOException at javax/microedition/lcdui/Image.createImage… pc=0: /men/Doom3bg11bak.png` | the game asks for a PNG that is not in its own JAR; `Image.createImage()` correctly throws |
| `Erroor load texture !java.io.IOException: /men/Doom3bg11bak.png` | `Doom/i.i(String)` catch block: appends `Erroor load texture !` and the exception and `System.out.println`s them, then returns the old/null image. This is the game's tolerant texture helper, not an emulator error. |
| `observed: … pcm_frames=960160` | one full pass of the theme music, see "Where 960160 comes from" |

The missing texture: `Doom/m.<init>` assigns `ss = "/men/Doom3bg11bak.png"`
(bytecode 241), and `Doom/m.s(Graphics3D, Graphics)` passes `ss` to
`Doom/i.i(String)` at bytecode 40 together with ~15 other texture paths (all of
which do exist). The JAR's `men/` directory holds only
`Doom3bg1exit.png`, `Doom3bg1loadgame1.png`, `Doom3bg1multipl.png`,
`Doom3bg1newgame123.png`, `Doom3loadgame.png`, `Doom3mous.png`, `bg1bg0003.png`,
`dead042.png`, `dead1.png`, `ico.png`, `schet4.png` (plus `Thumbs.db`);
`unzip -l … | grep -ci Doom3bg11bak` is 0. So the game itself references an
asset it does not ship; on a phone the same `IOException` is caught and the
text printed, and the game continues. It is unrelated to the intro — it
happens after the intro thread has already been skipped.

The two failures do not depend on each other: the video NPE comes from
`Doom.video()` on the intro thread; the texture line comes from
`Doom/m.s()` during menu construction and would happen on any device.

## Evidence

- `.local-tools/debug-runs/doom-media-audit-01/probe-assets.log` — the
  read-only demux/provider probe built from `build/libnj2me.a`: `nj_mp4_open`
  on all four assets, `nj_av_video_h263.accepts/open` on each (accepts=1 for
  every ftyp file, open refuses MP4V and audio-only files), `nj_amr_mp4_*`
  frame counts (doom.3gp 624, d3theme 6001, pistol_use_01 30,
  pistol_shot_02a 35).
- `.local-tools/debug-runs/doom-media-audit-pcm/f<N>/console.log` — the
  `pcm_frames` bisect under `--run-frames` (0 until the 12 s sleep ends, then
  960,160).
- `.local-tools/debug-runs/doom-media-audit-current/` — current-build
  280-frame run: `console.log` with the exact user lines, `frames/00060.png`
  and `frames/00120.png` (dark starfield splash), `frames/00240.png` (Doom 3
  main menu: NEW GAME / LOAD GAME / MULTIPLAYER / EXIT GAME). Prior run
  `.local-tools/debug-runs/doom-20261025-lead-001/frame-00250.png` shows the
  same menu, so the intro being absent does not stop the game from reaching
  the menu (the game thread continues).
- `.local-tools/debug-runs/doom-media-audit-f2p/` — FreeJ2ME-Plus headless
  run: `console.log` (M3G NPE in `Doom.m.<init>`) and blank shots.
- Binaries/probes used: `/tmp/doomaudit/nj2me-audit-34767c102aca` (snapshot
  sha256 `34767c10…`, for the `--run-frames` bisect), `build/nj2me` sha256
  `2dde3224…` (current-build run), `/tmp/doomaudit/probe_doom3` (probe; source
  `/tmp/doomaudit/probe_doom3.c`, linked against `build/libnj2me.a`).
- JAR facts: `vid/doom.3gp` 226,190 B, MP4 brand `3gp4` (ffprobe: video
  mpeg4/`mp4v` 320×240 15 fps 189 frames 12.6 s; audio `amr_nb`/`samr` 8 kHz
  mono 624 frames 12.48 s); `snd/d3theme.amr` 216,624 B, 6001 frames; the
  three SFX files are MP4/AMR as well.

## Still open

- Whether an audio-only Player for a movie file is acceptable is a design
  decision, not a fact. It does not change this title's outcome.
- No real handset was available for this audit; the device behaviour is
  inferred from the game's bytecode plus MMAPI rules. FreeJ2ME-Plus cannot run
  the title, so it is not a working comparison.
- The MPEG-4 Part 2 decoder is being written by another agent right now
  (`fd02094`, `4653ee7` committed; a follow-up macroblock layer pending). The
  expected value of `pcm_frames` and the intro behaviour will not change until
  that provider plus a `VideoControl` exist; when they do, the intro should be
  audible (AMR track) and visible (RGB565 frames) — but only if the pump gains
  both DTS pacing and an AV Player, otherwise it will run at 30 fps without
  sound.
- `VideoControl` rendering into the Canvas is the larger unknown: LCDUI today
  has no consumer of the frame buffer, so "present through the painting path"
  (`docs/CODECS.md:70-76`) is a design statement that needs a concrete
  integration point (a canvas-level video layer, or an off-screen RGB565
  surface that `paint()` composites).
