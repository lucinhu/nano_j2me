# Streaming SAX support and Angry Birds level loading

## Reproduction and cause

The unmodified `tests/games/愤怒的小鸟_383.jar` has SHA-256
`3bbcc151a326ed798954d7a3ee8596439fad3270fe3ec11f5317b610c07d1cde`.
It initially failed during startup with
`NoClassDefFoundError: org/xml/sax/helpers/DefaultHandler`. The JAR contains
131 XML assets, including UTF-8 and UTF-16 LE/BE documents, and uses
`SAXParserFactory.newInstance()`, `newSAXParser()`,
`SAXParser.parse(InputStream, DefaultHandler)` and `Attributes.getValue(String)`.

After implementing that path, the main menu and episode/level selectors loaded.
Entering a level then failed at `XMLLevel$LevelHandler.startElement`, bytecode
pc 592, because `java/lang/Float.parseFloat(Ljava/lang/String;)F` was missing.
The handler uses it to convert XML coordinates. Registering and implementing
this entry lets the first level finish loading.

## Implementation

The optional [SAX module](../../src/midp/nj_lib_sax.c) reads through Java
`InputStream` in 4096-byte blocks. It decodes UTF-8 and UTF-16, normalizes XML
newlines and attribute whitespace, handles declarations, comments, CDATA,
processing instructions, predefined entities and numeric character references.
Names use the XML 1.0 character ranges. Invalid structure, duplicate attributes,
invalid encodings and unsupported declarations produce `SAXParseException`.

The module exposes document, element, text and processing-instruction callbacks.
`AttributesImpl` implements the `Attributes` interface and stores each element's
attribute snapshot in GC-visible Java fields. Callback arguments, input buffers
and partially constructed attributes stay rooted across GC and recursive calls.
Parse state is local to each invocation, so a callback may reuse the same parser
for another document. Input-stream and handler exceptions propagate unchanged;
cleanup does not replace their identity with a parsing exception. The caller
retains ownership of its stream.

Resource limits are 64 element levels, 128 attributes per element, 65,536 UTF-16
units per token and 1,048,576 decoded characters per document. Text is delivered
in chunks of at most 512 UTF-16 units. There is no DOM or whole-document input
copy. Growable native name/value buffers are tracked and freed after use.

`NJ_ENABLE_SAX` defaults to 1 in desktop CMake and 0 for standalone ports.
`-DNJ_ENABLE_SAX=0` removes the parser and Java class registrations; only empty
registration stubs remain. The explicit ESP-IDF source list includes the module
so its disabled stub is available there too. No new dependencies were added.

[Float parsing](../../src/core/nj_lib_boxing.c) shares the existing decimal
arithmetic used by `Double.valueOf(String)`. Its new entry handles Java trim
characters U+0000–U+0020, signs, decimal fractions/exponents, numeric suffixes,
NaN, Infinity, signed zero and float overflow/underflow. Malformed input raises
`NumberFormatException`; null raises `NullPointerException`. The existing Double
string entry keeps its previous input behavior.

## Validation

- [SAX tests](../../tests/test_sax.c): 3014 assertions, zero failures. Cases include
  UTF-8/UTF-16, character references, attribute queries, retained snapshots after
  GC, root-table growth inside callbacks, same-parser reentry, exact callback and
  I/O exception identity, a failed read between UTF-16 surrogate units, malformed
  XML and all four resource limits.
- [Library tests](../../tests/test_lib.c): 704 checks passed, including Float
  literal bit patterns, exponent/suffix/trim cases, signed zero, finite and
  infinite overflow boundaries, subnormals, malformed strings and null.
- Full Release CTest: **85/85 passed**.
- AddressSanitizer and UndefinedBehaviorSanitizer: both SAX and library tests
  passed with leak detection enabled and no sanitizer report.
- SAX-disabled Release build and its disabled-feature test passed. Boxing also
  passed a syntax check with `NJ_USE_LIBC=0` and `NJ_ENABLE_FLOAT=0`.
- The original JAR hash was rechecked. Runs used fresh RMS directories under
  `.local-tools/debug-runs/sax-angry-20261020/`; user saves were not changed.

The final headless replay used 640×360, a 32 MiB configured heap, 30 fps and
1560 frames. It selected Play, the first episode and first level, dismissed the
tutorial, waited for the camera to settle, then dragged and released the bird.
Frame 1260 shows the bird in flight and its trajectory; later frames show the
camera following it, the target structure, and the next bird on the slingshot.
The run reported 29.8 fps, two GCs and 19,893,696 live heap bytes at exit, with
no uncaught exception. The SDL2 replay completed the same 1560-frame
menu/level/input sequence at 29.6 fps, also without an uncaught exception. Its
bytecode and graphics counts agree closely with the headless run. See the
isolated `sdl-throw/runtime.log` under that run directory.

The replay input was:

```text
300:pressed@320,260 303:released@320,260
420:pressed@180,165 423:released@180,165
540:pressed@55,65 543:released@55,65
780:pressed@320,180 783:released@320,180
1200:pressed@275,235 1206:dragged@210,280 1212:released@210,280
```

For example, the headless command (combine the keys above onto one line) is:

```sh
./build/nj2me --port headless --size 640x360 --heap 32M --fps 30 \
  --rms-dir .local-tools/debug-runs/sax-angry-20261020/throw/rms \
  --run-frames 1560 --keys "$KEYS" \
  --dump-frames .local-tools/debug-runs/sax-angry-20261020/throw/frames/ \
  --dump-every 60 --stats 'tests/games/愤怒的小鸟_383.jar'
```

The earlier main-menu comparison used local FreeJ2ME-Plus with
`-Dfile.encoding=ISO_8859_1`; both emulators rendered the menu. The final
first-level/throw observations above are nano_j2me validation, not a claim of
full-game equivalence or completion.

## Footprint and remaining scope

Release executables with identical other options and Float support:

| SAX | text | data | BSS | text + data increase |
| --- | ---: | ---: | ---: | ---: |
| Disabled | 2,694,126 | 156,712 | 6,688 | — |
| Enabled | 2,721,534 | 159,752 | 6,688 | 30,448 bytes (29.7 KiB) |

Static BSS is unchanged. The input array and attribute snapshots are Java heap
allocations made while parsing; bounded native token buffers are temporary.
Each VM also retains one host-allocated class pointer for the AttributesImpl
interface relation (8 payload bytes on this x86-64 build, plus allocation
bookkeeping). This does not imply that the game's images and gameplay fit a
small MCU heap.

This is a non-validating, namespace-disabled SAX subset. It preserves qualified
names and `xmlns` attributes while URI/local-name queries are empty. Requesting
validation or namespace processing is rejected by `newSAXParser()`. DTDs,
external entities, DOM, the full mutable AttributesImpl API and wider JSR-172
web-services APIs are outside this implementation. Malformed input throws
SAXParseException directly; it does not dispatch the handler's fatalError
callback. SAX exception locator and line/column APIs are not supplied. Float
conversion retains the existing small
decimal core's rounding and very-long-significand limitations; hexadecimal
literals and an arbitrary-precision conversion are not added here.
