# MinecraftJ2ME PE 0.6.1 V2.2 compatibility

Test archive: `tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar`.
SHA-256: `fbe72c5c5cdd3042daa69475bdcbd50f7d0e85afcbd699caf3b1f627e014bcd6`.
The original archive was not changed.

## Loading failure

`MC.run()` draws `Loading...` and then calls `R.load()`. The first terrain
texture constructs `java/lang/ref/WeakReference`, which was absent from the
runtime. The VM raised `NoClassDefFoundError` in `R$Tex.<init>`. The game's
initialization handler catches `Throwable`, logs the failure, and keeps running
without setting `initDone` or retrying initialization. Presented frames continue
while the initial loading image remains on screen.

The runtime now provides the CLDC 1.1 `Reference` / `WeakReference` hierarchy,
constructor, `get()` and `clear()`. GC does not trace the private referent as a
strong edge. Once strong marking finishes, it clears unreachable weak referents
before sweeping. A subclass's own fields remain strong, including a field that
hides the base referent.

## World persistence

Following the original game beyond the menu exposed additional file API gaps:

- `FileSystemRegistry.listRoots()` returned a complete URL. It now returns
  `root/`, which the application can prefix with `file:///`. With no mounted
  filesystem it returns an empty enumeration.
- SDL2 lacked directory callbacks. SDL2 and headless now share the POSIX
  directory iterator so saved worlds can be listed on the desktop too.
- `truncate` had the wrong `(I)V` signature. It now implements `(J)V`, checks
  connection mode, and never extends a file. Rename and offset output support
  complete the game's temporary-file and in-place chunk save paths.
- File output previously buffered the whole world in a growing Java array.
  A roughly 21 MB chunks file exhausted a 64 MB heap during array growth.
  Output now reopens the host file at its stored position for each write.
  Input uses the same approach for reads and skips, so several streams over
  the chunks file can coexist without copying the whole file into the heap.

Streams keep Java state, not host file handles, so snapshot restoration does
not depend on serializing a `FILE *`. Snapshot loading also resolves method
frames, exception methods and canonical Class mirrors through the saved
class-name mapping, allowing class registration order to differ.

## Terrain placement

After startup and file access worked, the world still appeared as scattered
faces. `LevelRenderer.makePart()` reuses one `float[3]` bias array for successive
terrain buffers. `VertexBuffer.setPositions()` retained that array reference,
so constructing a later part moved every earlier part to the last bias.
The renderer now takes a copy of the three bias values. A pixel regression
mutates the caller's bias array after setting positions and verifies that the
already configured geometry does not move.

## Running

From the project root, create a dedicated filesystem directory and keep using
it on subsequent runs:

```sh
mkdir -p "$HOME/.local/share/nano_j2me/minecraft-files"
./build/nj2me --port sdl2 --heap 64 --size 240x320 \
  --fs-dir "$HOME/.local/share/nano_j2me/minecraft-files" \
  tests/games/MinecraftJ2ME_PE_0.6.1_V2.2.jar
```

The command-line runner exposes no JSR 75 filesystem unless `--fs-dir` is
provided. RMS and the world's JSR 75 files are separate stores. Initial terrain
generation is CPU intensive in the interpreter; an advancing `Building terrain`
bar after the menu is different from the initialization failure above.

Local evidence is kept under `.local-tools/debug-runs/minecraft-*` (excluded
from version control). FreeJ2ME-Plus comparison used commit
`baca76a4dda6f30abac67fb4a3db009eed05f91c`, the same original JAR and 240x320.
Optional Nokia multitouch, Android and UDP probes can throw caught exceptions;
these do not by themselves establish failure of single-player startup.

## Validation

- The unified build and all 123 CTest tests pass after the final runtime and
  terrain-bias changes. The class library test also passes 1432 individual checks.
- Regressions cover weak-reference lifetime, strong fields on subclasses,
  relocated snapshot classes, root enumeration, truncate/rename/offset writes,
  and byte-for-byte file content through both the port hooks and libc fallback.
- A 128 KB Java heap can write a file larger than 1 MB and simultaneously open
  three input files of 1–2 MB, read and skip across boundaries, and release all
  host handles before returning from each operation.
- The original game creates a world on SDL2 with a 64 MB heap. Its saved
  `chunks.dat` is 22,024,192 bytes; FreeJ2ME-Plus loads the same saved world.
- Restarting nano_j2me with the same RMS and filesystem directories loads that
  world, reports `offset write ok`, saves changed chunks in place, and returns
  to the title screen. This run has no game-error, OOM or uncaught-exception
  log. Initial world preparation and the first 3D frame remain slow.
- Headless frame dumps after the bias fix show continuous ice, shoreline and
  terrain, a changed view after holding Up, and the pause menu after a soft key.
  The earlier shared-bias implementation showed displaced fragments instead.
- The final SDL2 replay also shows the corrected terrain, opens the pause menu,
  saves three modified chunks in place, and returns to the title screen. The
  replay exits without game errors, OOM or uncaught exceptions; its save takes
  267 ms in this run. Evidence: `.local-tools/debug-runs/minecraft-sdl2-verified-r01/`.
- The Options screen renders its sliders and checkboxes and reacts to keyboard
  focus: "Server visible by default" toggles, and the username editor accepts
  typed text through the CLDC TextBox plus its OK command. The changed name
  (`SteveHero`) was reloaded from the options file by a later run. Inventory
  and creative block selection render, and a pause-menu quit saves before
  returning to the title.
- `--save-state`/`--load-state` round-trip a live world: a 68 MB snapshot taken
  after frame 2000 resumes the loading thread, renders the restored terrain and
  responds to movement. The stock game then saves changed chunks through the
  same offset path. Evidence: `.local-tools/debug-runs/minecraft-snapshot-r01/`.

## Touch input

Minecraft PE is built for a touchscreen: the Nokia MultipointTouch listener
drives its menus, block taps and the look-around drag. The runtime only had the
older Series 40 shape of that class (`setMultipointTouchListener`), so
`getInstance()` raised `NoSuchMethodError`, the game caught it, and every tap
had to go through the keypad.

`MultipointTouch` now answers both generations: `getInstance()` returns one
per-VM singleton, `addMultipointTouchListener`/`remove...` register on it,
`getState`/`getX`/`getY` read pointer 0, and the pointer states plus
`pointersChanged()` are delivered from the runtime's pointer dispatch. Scripted
pointer replay (`--keys '150:pressed@120,125 154:released@120,125'`) opens
"Select world" from the title, so the click reaches the game's own button hit
test. `tests/test_nokia_touch.c` pins the class surface, the constants and the
coordinates `getState()`/`getX()`/`getY()` report.

Known gaps after this work:

- LAN play still needs `javax.microedition.io.DatagramConnection` and
  `datagram://`, so "Join Game" reports a caught `ConnectionNotFoundException`
  and single-player continues. The runtime's threads are cooperative, so a
  native blocking receive would also stop the frame pump until it returned;
  that needs I/O-aware scheduling rather than a small stub.
- `Graphics3D.getProperties()` reports `supportAntialiasing = false`, which this
  title uses as its "3D hardware present" probe, so it draws its HUD through
  `Graphics` instead of the m3g path. The reported capability is accurate: the
  software rasteriser does not antialias edges.
- The Nokia multipoint-touch API is absent, so the game's own multitouch
  listener cannot register; it falls back to Canvas pointer events and
  keyboard. Pointer taps on the Options rows activate neither path, whereas
  keyboard focus does.
- Initial terrain generation (~100 s) and world load (~70 s) are interpreter
  bound; they are slow, not stalled.
