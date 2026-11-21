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
