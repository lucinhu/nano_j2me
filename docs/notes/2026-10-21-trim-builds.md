# Trimmed builds: a configuration, not a patch

`nj_config.h` has documented `NJ_ENABLE_*` switches from the start, and a port
that wants a smaller runtime is supposed to be able to turn them off.  It
could not: several of them were missing from the CMake cache, so the only way
to set them was `-D` on `CMAKE_C_FLAGS` - which then collides with the
`target_compile_definitions` entries for the rest, and a `-DNJ_WERROR=ON`
build stops with "macro redefined" before it compiles anything.

Fixing that exposed one real defect and a handful of warnings that only the
trimmed configuration reaches.  This note records what was wrong, what the
trims cost, and what was verified, so the next person does not repeat it.

## What was broken

| problem | effect |
| --- | --- |
| `NJ_ENABLE_FONT_CJK` was not a CMake option and not a compile definition | `-DNJ_ENABLE_FONT_CJK=0` was silently ignored: the 600 KB of mono CJK tables stayed linked.  The header's default (1) won. |
| `NJ_ENABLE_LONG`, `NJ_ENABLE_EXCEPTION_MESSAGES`, `NJ_ENABLE_LC_DUPLI`, `NJ_ENABLE_GAME_PACKAGE`, `NJ_ENABLE_MEDIA` were the same | reachable only by patching the compiler flags, and those flags then clash with the definitions CMake already passes |
| `set(NJ_ENABLE_MASCOT ...)`'s description said "Build JSR 184" | a user reading `cmake -LA` got the wrong answer for two different APIs |
| trimmed build under `-Werror` | `-Wstring-compare` in the JSR 82 property table, `-Wunused-function` for three M3G helpers when `NJ_ENABLE_M3G=0`, an `-Wunused-parameter`, and one dead `save_written` flag |

### The JSR 82 defect

`LocalDevice.getProperty()` copied the name into a 32-byte buffer.  The
longest property the specification defines, `bluetooth.sd.attr.retrievable.max`,
is 33 characters, so the copy was truncated one byte short and the `strcmp`
could never match - the property was always silently null.  `32` is the kind
of number that looks right until the compiler counts for you; an ordinary
build does not, a trimmed build under `-Werror` does:

```
src/midp/nj_lib_bt.c:1239: error: 'strcmp' of a string of length 33 and an
array of size 32 evaluates to nonzero [-Werror=string-compare]
```

Fixed by making the buffer 64 bytes and adding a regression test
(`tests/test_wireless.c`) that hands the plain headless port a fake radio and
checks all three properties (`64`, `7`, `1`) plus a control that an undefined
property is still null.  Reverting the buffer to 32 fails exactly the
`retrievable.max` assertion.

## What changed

* `CMakeLists.txt`: `NJ_ENABLE_FONT_CJK` and the five switches above are now
  cache options like the rest, and all of them are passed through
  `target_compile_definitions`; the MASCOT description names Mascot Capsule.
* `src/midp/nj_lib_bt.c`: 64-byte property-name buffer.
* `src/midp/nj_lib_m3g.c`: `m3g_node_matrix`, `m3g_image_texel` and
  `m3g_free_refs` are inside `#if NJ_ENABLE_M3G`, so a 2D build does not carry
  three warnings it cannot use.
* `src/gfx/nj_text.c`: `blit_glyph` lost a `const nj_font *` parameter it never
  read.
* `src/runner/nj_run.c`: removed `save_written`, assigned and never read.
* `tests/test_wireless.c`: the fake-radio property test above.

## Footprint

Measured with `size` on `Release`, GCC, x86-64, `-DNJ_WERROR=ON`,
`-DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF -DNJ_BUILD_SDL2=OFF`:

| configuration | text (B) | data (B) | dec (B) | text vs default |
| --- | ---: | ---: | ---: | ---: |
| default | 2,704,622 | 159,376 | 2,870,686 | - |
| 2D: `M3G=0 MASCOT=0` | 2,354,824 | 126,560 | 2,488,072 | -349,798 (-12.9%) |
| lean 2D: the above plus `TTF=0 FONT_CJK=0 FONT_CJK_AA=0 GAME_PACKAGE=0 LC_DUPLI=0 LONG=0 FLOAT=0 MEDIA=0 AMR=0 MIDI=0 SAX=0` | 1,194,283 | 123,264 | 1,324,235 | -1,510,339 (-55.9%) |
| minimal: lean plus `RMS=0 IO=0 PNG=0 GIF=0 JPEG=0 INFLATE=0 WAV=0` | 1,146,651 | 123,168 | 1,276,507 | -1,557,971 (-57.6%) |

The lean build is the interesting one: it still decodes PNG/GIF/JPEG/INFLATE
and still has `java.io`, `RecordStore` and WAV, and it is 54% of the default's
text.  A build that turns those off as well saves only another 1.7%, which
says where the bytes are: the JSR 184 rasteriser and the CJK font tables, not
the codecs.

## Validation matrix

Every configuration was built with `-DNJ_WERROR=ON` and
`-DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF -DNJ_BUILD_SDL2=OFF`; all six
finish with zero warnings:

| configuration | result |
| --- | --- |
| `FONT_CJK_AA=0 TTF=0` (mono text) | ok |
| `PNG=0 GIF=0 JPEG=0` | ok |
| `AMR=0 MIDI=0 WAV=0 SAX=0` | ok |
| `M3G=0 MASCOT=0` | ok |
| `LONG=0 FLOAT=0` with M3G on | rejected by the existing `#error "JSR 184 needs libm"` - correct, the guard is the answer |
| `RMS=0 IO=0` | ok |
| all of the above off (minimal) | ok |

The default configuration also builds warning-free under `-DNJ_WERROR=ON`, and
the full suite still passes **87/87**.

## Real titles on a trimmed build

The lean 2D build is not a stub: it renders real titles.

* `/tmp/nj-2d` (2D trim, `M3G=0 MASCOT=0`), `1945空战英雄_6105` at 176x208
  with `--device nokia6230`: 700 frames, `paints=290 gfx=8296 tris=0 m3g=0`,
  30.1 fps, exit 0.  Frame 600 is the title screen with its background art
  (`.local-tools/debug-runs/trim-r9/1945/00600.png`).
* The lean build (`/tmp/nj-lean`, 54% of default text) on the same title:
  700 frames, 30.2 fps, exit 0, same title screen
  (`.local-tools/debug-runs/trim-r9/lean1945/00600.png`).
* `超级马里奥_10283` on `/tmp/nj-2d`: sound prompt and loading screen render,
  2600 frames at 30.3 fps, exit 0.

What a trim removes is capability, not correctness: `M3G=0` means
`Class.forName("javax.microedition.m3g...")` fails the way it does on a handset
without JSR 184, which is the same honest answer the default build gives for
Mascot Capsule titles.

## Building one

```
cmake -S . -B build-lean -DCMAKE_BUILD_TYPE=Release -DNJ_WERROR=ON \
  -DNJ_BUILD_TESTS=OFF -DNJ_BUILD_LIBRETRO=OFF -DNJ_BUILD_SDL2=OFF \
  -DNJ_ENABLE_M3G=0 -DNJ_ENABLE_MASCOT=0 -DNJ_ENABLE_TTF=0 \
  -DNJ_ENABLE_FONT_CJK=0 -DNJ_ENABLE_FONT_CJK_AA=0 \
  -DNJ_ENABLE_GAME_PACKAGE=0 -DNJ_ENABLE_LC_DUPLI=0 -DNJ_ENABLE_LONG=0 \
  -DNJ_ENABLE_FLOAT=0 -DNJ_ENABLE_MEDIA=0 -DNJ_ENABLE_AMR=0 \
  -DNJ_ENABLE_MIDI=0 -DNJ_ENABLE_SAX=0
cmake --build build-lean --target nj2me-cli -j
```

`-DNJ_ENABLE_FLOAT=1` (the default) leaves libm in the link; `M3G=0` makes
`FLOAT=0` possible for an FPU-less target.  Turning `NJ_ENABLE_FONT_CJK=0`
off drops the built-in CJK faces; text then falls back to the MIDP cell font
and any CJK in a title renders as the placeholder box, which is the honest
answer for a build that left the tables out.
