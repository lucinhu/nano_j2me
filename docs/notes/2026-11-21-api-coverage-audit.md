# API coverage audit (2026-11-21)

`tools/check_api_coverage.py` inventories the optional J2ME APIs a corpus
references and compares them with the classes the runtime actually defines.
Implemented names come from the builtin class tables (`nj_builtin_class`
entries) and the `NJ_C_*` class-name macros; a reference is "missing" when a
JAR names a vendor/optional class the runtime does not define - exactly the
kind of gap that ends in `NoClassDefFoundError` at run time.

```
tools/check_api_coverage.py --rows .local-tools/debug-runs/r151-rows-unique/rows.jsonl --top 40
tools/check_api_coverage.py --kemulator /tmp/kemulator/src     # diff against KEmulator
```

## Corpus result (899 unique titles)

Only **19 of 897** scanned JARs reference optional/vendor classes the runtime
does not define, and the gaps are concentrated in Nokia device-demo suites
(`com/nokia/mid/appl/{boun,snake,vime}/*` - those JARs were packed without the
firmware's shared classes), one marker interface
(`com/nokia/mid/ui/DirectGraphicsImplemented`, 11 JARs), one Siemens class
(`com/siemens/mp/game/Light`) and one SVG class
(`javax/microedition/m2g/SVGAnimator`).  Everything the other 878 titles use -
MIDP 2.0 LCDUI/game/media/rms/io (incl. file and http/socket/datagram),
JSR-75 PIM, JSR-120 WMA, JSR-82 Bluetooth, JSR-256 sensor, M3G, Mascot
Capsule, Nokia UI (FullCanvas/DirectGraphics/DirectUtils/DeviceControl/
FileSelect/sound/MultipointTouch), Samsung/Vibration/Motorola vibrator - is
present.

## KEmulator comparison (reference implementation)

KEmulator (`shinovon/KEmulator`) implements 822 optional/vendor API classes;
543 are implemented here.  The remaining families, by usefulness:

| family | classes | notes |
|---|---:|---|
| `com/nttdocomo/*` (i-mode/DoJa) | 131 | separate UI model; would need a LCDUI mapping layer; high effort, opens Japanese i-appli titles |
| `com/siemens/*` | 63 | `mp.game`, `mp.io`, `mp.lcdui`, `mp.media` families; static utilities are cheap |
| `com/motorola/*` | 57 | `com.motorola.audio`, `phone`, `ui` families; mostly small utilities |
| `com/nokia/*` extras | 57 | CanvasItem/CanvasGraphicsItem, gestures, GlobalIndicators, Clipboard, JoystickEventListener, lcdui listeners, text editors, IAP |
| `com/jblend/*` | 50 | Aplix OEM API; mostly small stubs |
| `com/vodafone/*` | 43 | Vodafone API families (media/util/location) |
| `javax/microedition/amms` (JSR-234) | 43 | GlobalManager/EffectModule/controls; stub-friendly |
| `javax/microedition/location` (JSR-179) | 14 | emulated provider is straightforward and useful |
| `javax/microedition/khronos` (JSR-239) | 13 | OpenGL ES wrappers; needs a GL context (Android/GLES port) |
| `com/vodafone`, `com/nec`, `com/sprintpcs`, `com/mexa`, `com/immersion` | ~30 | small OEM utilities |

## Roadmap

1. `javax.microedition.location` (JSR-179): emulated provider with fixed
   coordinates + in-memory LandmarkStore (functional, not a stub).
2. Nokia UI extras: `CanvasItem`/`CanvasGraphicsItem` overlay rendering,
   gestures manager (no-op registration), `Clipboard`, `GlobalIndicators`,
   `lcdui` visibility/display-state listeners, text-editor stubs,
   `TactileFeedback`, `DirectGraphicsImplemented` marker.
3. Siemens / Motorola / Vodafone / JBlend / NEC / Samsung utility families:
   honest stubs that keep class availability while returning documented
   defaults (`Light.setLight(false)`, `VibeTonz` no-op, etc.).
4. JSR-234 AMMS: GlobalManager/EffectModule/controls as documented stubs.
5. i-mode/DoJa (`com.nttdocomo.ui.*`): map the UI subset onto LCDUI so
   Japanese i-appli titles can start; staged, starting with the drawing
   primitives.
6. JSR-239 OpenGL ES wrappers over the existing GLES backend (Android port
   only first).

Every added class needs a unit test (class availability + documented
behaviour) and, when it changes runtime code, a full CTest run.  KEmulator is
used as behavioural reference only (no code copied; it is a Java emulator and
this is a C runtime).

## Round 152 (2026-11-21): JSR-179 phase 1 shipped

Roadmap item 1 is in: `src/midp/nj_lib_location.c` adds
`javax.microedition.location` with value classes and an emulated provider.

* Classes: `Coordinates` (+`DD_MM_SS`/`DD_MM`, both `convert` overloads,
  `azimuthTo`, `distance`, equals/hashCode), `QualifiedCoordinates`,
  `AddressInfo` (standard 4-string constructor, line/phone/email getters and
  the JSR-179 field codes 1..17), `Orientation`, `Criteria`, `Location`,
  `LocationException`, and the `LocationListener` / `ProximityListener`
  interfaces.  `LocationProvider` is an abstract builtin with the emulated
  `LocationProviderImpl` behind the static factories.
* Provider behaviour: always one available provider; every fix is
  `60.1699 N, 24.9384 E`, altitude 10 m, horizontal/vertical accuracy 30/10 m,
  speed 0, course 0, method 0.  `setLocationListener` delivers one immediate
  `locationUpdated()` from the caller's thread; proximity monitoring throws
  `LocationException` (no radio to tell us the user moved) and `reset()`
  clears the listener.  Deliberately no invented movement: a game polling
  `getLocation()` sees a stationary device, which is honest.
* `convert(double, int)` now matches the reference colon format
  (`24:56.304` for `DD_MM`, `dd:mm:ss.ss` for `DD_MM_SS`, leading `-` for
  negative coordinates); `convert(String)` accepts the same colon forms plus
  space-separated D/M/S and plain decimal degrees.
* Two bugs found while wiring the runtime: JVM argument slots for
  `double`/`float` parameters were read one slot too low (slot 0/1 instead of
  0/2 when a double is in front), which made the first fixture run report
  `accuracy=0/10`; and the boolean field helpers looked fields up with
  descriptor `I` instead of `Z`, so `Criteria`/`Orientation` booleans read
  `false` no matter what was written.  Both fixed; a first run that only
  checked class availability would have missed them, which is why the fixture
  asserts concrete values.
* Coverage: `tests/locationdemo/LocationDemo.java` (MIDlet implementing
  `LocationListener`) exercises provider state, last known fix, criteria,
  distance/azimuth, both `convert` forms, orientation, address info, an
  invalid coordinate (caught), proximity (caught), a listener update and
  `reset`; `tools/check_location.sh` asserts every marker and runs as CTest
  `test_location` (200th test, TIMEOUT 120).
* Phase 2 (next): `Landmark`, `LandmarkStore` (in-memory registry, no RMS
  persistence claimed for now), `AddressInfo` `EXTRA_FIELD_*` round-trip
  through `byte[]`.

## Round 153 (2026-11-21): JSR-179 phase 2 -- Landmark and LandmarkStore

`Landmark`, `LandmarkStore` and `LandmarkException` are implemented in the
same file, making the roadmap item complete enough for titles that keep
favourite places.

* `Landmark`: standard constructor (name required, description/coordinates/
  address/extra info optional), getters/setters, `equals`/`hashCode`.
* `LandmarkStore`: `getInstance` (creates the store when it does not exist
  yet, per JSR-179), `createLandmarkStore`, `deleteLandmarkStore`,
  `listLandmarkStores`, `addLandmark`, `getLandmarks()` / by category /
  by category+name / by coordinate rectangle, `updateLandmark`,
  `deleteLandmark`, `removeLandmarkFromCategory`, `getCategories`,
  `addCategory`, `deleteCategory`.  Enumeration results are real
  `java.util.Enumeration` objects (Vector enumerators).
* Storage is a per-VM in-memory registry: a static `java/util/Vector` field
  on `LandmarkStore` holding `Object[3]{store, landmark, category}` records
  (NULL landmark + NULL category = store marker, NULL landmark + category =
  category marker).  It is a Java field, so the GC and save states see it;
  nothing is a C global and nothing is persisted to RMS yet (documented
  limitation - a fresh VM starts with empty stores).
* The registry is created lazily on the first store call, not in
  `nj_location_late_init()`: a Java call during builtin setup fails
  (`nj_call_special` on `Vector.<init>` reports no exception and no action),
  which is the same constraint that made the Boolean boxed singletons be
  materialised directly.  Lazy creation sidesteps it.
* Fixture now covers: create store, add category, add landmark, enumerate
  by category, `listLandmarkStores`, `getCategories`, `updateLandmark`,
  `removeLandmarkFromCategory` (the landmark stays in the store,
  uncategorised - removing from a category is not deleting it),
  `deleteLandmark`, `deleteCategory`, `deleteLandmarkStore`;
  `tools/check_location.sh` asserts all of it.

## Round 154 (2026-11-21): the remaining small Nokia UI families

Roadmap item 2 is done for the classes below; `CanvasItem`/`CanvasGraphicsItem`
overlay rendering and the text-editor family remain future work.

Implemented in `src/midp/nj_lib_nokia.c` with the usual builtin pattern
(class/method/field tables, natives, constant writes in `nj_nokia_late_init`):

* `com/nokia/mid/ui/DirectGraphicsImplemented` - marker interface. 11 corpus
  JARs reference it; a MIDlet's `instanceof` check is the only behaviour.
* `com/nokia/mid/ui/Clipboard` - in-VM static String; `copyToClipboard(null)`
  clears it.  No system clipboard exists on the emulated handset.
* `com/nokia/mid/ui/GlobalIndicators` - `MAIL_INDICATOR` (274),
  `CHAT_INDICATOR` (260), `INDICATOR_UNINITIALIZED`/`HIDE`/`SHOW` (0/1/2);
  per-VM state, `setGlobalIndicator` returns the previous value, an unknown
  indicator throws `GlobalIndicatorsException` carrying `geCode`.
* `GlobalIndicatorsException` / `SoftNotificationException` - code field,
  three constructors, `getErrorCode()`.
* `com/nokia/mid/ui/TactileFeedback` - `FEEDBACK_STYLE_BASIC`/`_SENSITIVE`
  (1/2), `directFeedback` validates its argument and throws
  `IllegalArgumentException` for anything else, `isTouchFeedbackSupported()`
  reports 0, registration calls are accepted and ignored (no tactile screen).
* `com/nokia/mid/ui/SoftNotification` (abstract) + `SoftNotificationImpl` -
  `newInstance()`/`newInstance(int)` work through the real factory natives;
  `getId`, `setText(title, body)`, `setSoftkeyLabels(left, right)`,
  `setImage(byte[])`, `setListener` store the state; `post()`/`remove()` are
  no-ops because the emulated device has no notification centre.
* `SoftNotificationListener`, `JoystickEventListener` - interfaces; joystick
  events are never delivered (no joystick - a game that requires them sees
  none, which is the honest device behaviour).
* `com/nokia/mid/ui/gestures/*` - empty `GestureListener`, `GestureEvent`
  interface, `GestureInteractiveZone(int)` storing its type (TAP=1,
  LONG_PRESS=2, LONG_PRESS_REP=4, DRAG=8, FLICK=16, PINCH=32, DOUBLE_TAP=64),
  `GestureRegistrationManager.register()` returns `false` and `setListener()`
  is a no-op: the runtime does not synthesise gestures from its single
  pointer stream.

Verification: `tests/nokiaextdemo/NokiaExtDemo.java` drives every class through
real bytecode (including the two documented exceptions, caught in the MIDlet),
installs a Canvas, paints five frames and destroys itself;
`tools/check_nokiaext.sh` asserts the `NOKIAEXT ...` markers, requires both
provoked exceptions to appear, and rejects `uncaught exception`,
`NoSuchMethodError`, `NoClassDefFoundError`.  It is wired as `test_nokiaext`
and the fixture is generated by `nj_fixtures` (needs
`tools/midp_stubs.py`'s printed classpath; there is no `--out` flag).
Full CTest: 201/201.

Lesson kept: builtin instance fields must be looked up with `NJ_F` (instance)
and only static fields with `NJ_FS`; using `NJ_FS` on an instance field returns
NULL silently, so the values stay zero.  That was the only bug found while
bringing the fixture up.  `tools/check_field_descs.py` now also compares the
lookup's static/instance kind against the declaration, so that mistake is a
hard failure instead of a silent zero (`NJ_F`/`NJ_FS` and the trailing 0/1 of a
direct `nj_field_lookup()` are all checked).

## Round 159 correction: JAR-bundled classes are not API gaps

The first audit counted every optional-namespace class a JAR *references*, so
retail ports that bundle handset compatibility classes were reported as
"missing API" gaps: `Boun_*.jar` ships `com/nokia/mid/appl/boun/*`,
`Vime_*.jar` ships `com/nokia/mid/appl/vime/*`, and `3D滚球`/`兔子波比2` ship
`com/nokia/mid/ui/r.class`; those classes load from the JAR, not the runtime.
`tools/check_api_coverage.py` now also collects the class names each JAR
provides (`jar_provided_classes()`) and ignores references satisfied that way.

Recomputed on the in-repo corpus (403 JARs, 598 implemented classes):
**402 of 403 JARs have no external optional API gap at all.**  The only real
remaining reference is `毁灭战士3_10598.jar` -> `javax/microedition/m2g/SVG*`
(JSR-226), and the title runs 240 frames without `NoClassDefFoundError` (its
NetBeans SVG splash wrapper is never instantiated on the path the game takes),
so JSR-226 stays deferred rather than faked with a blank renderer.

The earlier "19 of 897" figure mixed corpus generations and counted bundled
classes; the corrected statement is the one above.  `--kemulator` mode is
unaffected (it diffs a reference source tree, not JARs).

## Round 162: CanvasItem / CanvasGraphicsItem overlay compositing

KEmulator implements `CanvasItem` as a state holder and leaves the canvas hook
`_addNokiaCanvasItem` unimplemented, so its overlay items never render.  That
is enough for class resolution, but HUD overlays built on the API would stay
invisible. The runtime now composites them for real:

* `src/midp/nj_lib_nokia.c` adds `com/nokia/mid/ui/CanvasItem` (abstract state:
  parent, size, position, visibility, z) and `CanvasGraphicsItem`
  (`(II)` constructor validating width/height >= 1, `setParent` validating the
  parent is a Canvas, `setSize`, `setPosition`, `setZPosition` rejecting
  negative z, `repaint`, abstract `paint(Graphics)` and the final
  `_invokePaint` hook that dispatches virtually).
* Attached items live in a package-private `cItems` Vector field on the
  `Canvas` builtin (`src/midp/nj_lib_lcdui.c`), created lazily on the first
  attach. Keeping the list in the object graph means the GC sees the items;
  no side table can leak them.
* After the canvas paint, `nj_nokia_paint_canvas_items()` walks the list,
  skips detached/invisible items, and for each visible item takes a Graphics
  onto the canvas backing image (`nj_midp_image_graphics()`, because
  `Image.getGraphics()` refuses the deliberately non-mutable backing image),
  translates by the item position, clips to the item rectangle, and calls
  `_invokePaint`. An exception raised by an item clears and stops the overlay
  pass instead of aborting the frame.
* Painting happens after the canvas paint, so items appear on top of it, as
  on a device.

Verification: `tests/canvasitemdemo/CanvasItemDemo.java` paints a blue canvas
and attaches a green 40x30 item at (10,20); `tools/check_canvasitem.sh` runs it,
dumps the last frame and asserts green inside the item rectangle and blue
everywhere around it (including one pixel left of the item), plus the
`getParent`/position/size values seen from Java and both documented
`IllegalArgumentException` paths. Wired as `test_canvasitem`
(`tools/make_canvasitem_jar.sh` is part of `nj_fixtures`).

`FullCanvas` was already present with the key constants; `TextEditor` is a
substantial canvas-item editor (caret, focus, listener actions 0x1-0x800,
touch-control interfaces) with no corpus usage yet, so it stays on the roadmap
rather than being faked.

## Round 163 (2026-11-22): resource streams, Siemens APIs, Immersion VibeTonz

The KEmulator class-presence diff advertised 37 missing Nokia classes, but the
corpus audit showed only one in-repo JAR referencing a truly missing optional
API (Doom3 → the unused JSR-226 SVG hooks).  The useful signal came from the
fetched 330-JAR corpus instead: a handful of titles failed on **vendor APIs and
non-standard connection schemes**, and each one was a linkage error that
aborted the game.  Three fixes, all verified against the actual titles:

* `Connector.openInputStream("resource:<jar-entry>")` — nine corpus JARs
  (Wizardry, 鬼泣3D, 俄罗斯方块, 生化危机3D, 口袋仓鼠, 家园4/5, 疯狂农场2,
  HeroesLore) carry the literal `resource:` prefix in their own class files.
  The runtime now implements the scheme: the name is resolved inside the MIDlet
  JAR, and a missing entry throws `ConnectionNotFoundException`.
* Wizardry's chunked resource loader expects `read(byte[],int,int)` to return
  **0** (not the standard -1) at the end of a `resource:` chunk, then opens the
  next chunk file; with -1 the offset went negative and its sprite sheets stayed
  zero-filled.  The scheme therefore returns a dedicated
  `nano_j2me/ResourceStream` whose ranged read answers 0 at EOF, while every
  other stream keeps standard `InputStream` semantics.  Wizardry now renders
  its main menu (`paints=180 gfx=5.75M`), where it used to be a black screen.
* `com.siemens.mp.color_game` / `com.siemens.mp.game` — Layer, LayerManager,
  Sprite, ExtendedImage and Light, implemented from the KEmulator reference
  semantics: the manager saves/restores the clip and translate while painting
  visible layers last-to-first, Sprite draws its frame with `drawRegion`, and
  ExtendedImage blits into the current canvas backing buffer.  Covered by
  `tests/siemensdemo` + `tools/check_siemens.sh` (pixel-level: sprite, layer and
  extended-image rectangles) and gated as `test_siemens`.
* `com.immersion.VibeTonz` — the vibration engine `Dig_Dug_240x400` links from
  its `GameCanvas.<clinit>`.  Honest no-motor semantics: `OpenDevice` returns
  0 (no device) and the play calls return 0 without vibrating.  Dig Dug now
  runs (`paints=121 gfx=13.7k`) instead of aborting before installing a
  Displayable.

Correction (round 167): the Gameloft **GlomoReg** billing stack is *not* a
runtime gap. `3D孤胆枪手2010` and `急速狂飙完美破解版` bundle their own
Glomo classes and run `ok` in the sweep (600 frames each).  The one failing
Glomo user, `ArcadePark2`, resolves `GlomoReg/GlomoCountry` in
`GlomoConfig.<clinit>` but its JAR ships only `GlomoCountryShort` — a title
packaging bug that a real handset hits the same way, so it is no longer
reported as a runtime `failure`: the sweep now detects a `NoClassDefFoundError`
naming a class absent from the title's own JAR and records
`title-exception: title references a class it does not bundle`.

### Round 168: `Class.newInstance()` access rules match the handsets (2026-11-22)

`snake_2.jar` (Nokia Snake EX2) failed with `Snake.startApp()` NPE at the
`SnakeDevice.initialise()` call: the MIDlet resolves
`com.nokia.mid.appl.snake.MidiDevice` (not bundled, caught), then
`ToneDevice`, then instantiates it via `Class.newInstance()`.  Our
implementation enforced the strict CLDC rule that *the nullary constructor*
must be public as well; `ToneDevice` is a public class with a package-private
`ToneDevice()` constructor, so reflection returned `IllegalAccessException`,
the game swallowed it, `f` stayed null and `startApp()` threw.

Real handsets do not check the constructor's access flags for
`Class.newInstance()` (they resolve `<init>()V` and call it), so the runtime now
only requires the *class* to be public.  The package-private class case is
still `IllegalAccessException`, which `tests/excdemo/ExcDemo.java` +
`tools/check_stack_trace.sh` assert (PASS after the change).  `1945空战英雄`
(needs `--device nokia6230`) and `CS反恐精英` are unchanged.

Result: `snake_2.jar` runs 600 frames / 553 paints (Snake EX2 menu renders:
"New game / High scores / Settings / Instructions"), the sweep row moved
`failure -> ok`, and `docs/COMPATIBILITY_MATRIX.md` now has 1483 running
titles.  The one remaining corpus `failure` in the round-167 sweep was exactly
this title.

## Round 171: `Class.forName()` must initialise the class (GoldenSolitaire)

`GoldenSolitaire.jar` was the last fetched title that painted nothing.  It ran
600 frames and 600 paints with `gfx=0`; the only runtime lines were a caught
`InvalidRecordIDException` (expected on a fresh RecordStore) and a caught
`ArrayIndexOutOfBoundsException at a.Code(ILjava/lang/Object;)V pc=11`.

The game ships an obfuscated resource manager (`a`) with a deliberate
initialisation hook: its archive loader begins with

    Class.forName("p")        // a.Code(): force the interface's <clinit>

because interface `p`'s non-constant static fields are *handles* handed out by
`a.Exceptions(int)`.  That helper increments the global `V` slot counter; the
archive then sizes its lookup table as `code_count + base_count + V`.  The
handles' low 16 bits are absolute slot numbers (`V + base + code | (n-1)<<16`).

Our `java/lang/Class.forName` only looked the class up and returned its
mirror - it never ran `<clinit>`.  So the "force init" call did nothing, the
table was sized with `V == 0`, and the first indexed store from the resource
manager (`Exceptions[handle & 0xFFFF]`) was out of range.  Every paint then
threw from a game-side catch handler and the canvas stayed black.

Fix (commit `2c4b62d`): `class_forName()` now calls `nj_class_init(vm, c)` after
the lookup, exactly like `Class.newInstance()` already did.  JVMS 5.5 makes
that a language contract, so this is a runtime bug, not a game quirk.

Evidence:

* `GoldenSolitaire` CLI 300 frames: `gfx=13,859` (was 0), bytecodes 8.9M (was
  219k), no uncaught exceptions; the frame dump is the language-selection
  screen with **73,978 non-black pixels**, matching the FreeJ2ME-Plus
  reference (73,983).
* Sweep row re-measured: 600 frames / 599 paints / **gfx=18,875** /
  34,909,698 bytecodes / 1 expected RMS exception / `ok` (was gfx=0 / 262,490
  bytecodes / AIOOBE).  Matrix regenerated deterministically (1502 titles).
* Libretro visual probe on the rebuilt core: **PASS** (300 frames).
* New regression fixture `tests/classinitdemo` (+ `tools/make_classinit_jar.sh`
  and `tools/check_classinit.sh`, CTest `test_classinit`): an `InitProbe` class
  prints a marker from its static initialiser, the MIDlet calls
  `Class.forName("InitProbe")` and then prints the value.  The gate asserts the
  initialiser line appears *between* "before" and "forName returned"; it FAILS
  on build `c192f46` and PASSES with the fix.

This is the second title fixed by making a lazy reference point match MIDP
semantics (`Class.newInstance()` in round 168).  Neither was an API the game
called explicitly for its result: the titles call these reflection entry
points purely for their side effects, so "look up and return" is not enough.
