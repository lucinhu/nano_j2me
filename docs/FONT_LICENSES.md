# Font data and runtime code

The runtime's original code is offered under the [MIT license](../LICENSE).
Font files and generated glyph data keep their own licenses. The font licenses
do not require independently written VM, renderer or libretro adapter code to
use OFL. Decoder provenance is a separate, still-open review; see
[CODEC_PROVENANCE.md](CODEC_PROVENANCE.md).

## Bundled fallback fonts

| Font-derived data | Upstream | License and full notice |
| --- | --- | --- |
| [Latin bitmap](../src/gfx/nj_font_data.h), [Latin AA](../src/gfx/nj_font_data_aa.h) | Liberation Mono Regular / Bold 2.1.5; digitized data copyright 2010 Google Corporation; copyright 2012 Red Hat, Inc. | [SIL OFL 1.1](licenses/Liberation-Fonts-OFL.txt) |
| [CJK bitmap](../src/gfx/nj_font_cjk.h), [CJK AA](../src/gfx/nj_font_cjk_aa.h) | Adobe Source Han Sans CN Regular | [SIL OFL 1.1 and upstream attribution](licenses/source-han-sans-OFL.txt) |

Rasterizing a font into C arrays does not turn the font-derived data into MIT
code. Keep both full notices with source and binary packages that include these
data. Preserve the upstream copyright, license and reserved font name conditions;
the generated tables are not advertised as an upstream font release.
The [submission checklist](LIBRETRO_SUBMISSION.md#liberation-mono-provenance)
records the exact source hashes. The scripts that generate glyph tables are
separate code; their license does not replace that of the fonts they process.

## External fonts in libretro

The core can read a font already installed on the user's system or supplied in
RetroArch's system directory. It does not redistribute those files or change
their licenses. See [the setup instructions](LIBRETRO.md#fonts).
Only fonts the user has the right to use should be supplied. A font's glyph
coverage and outline format determine whether it can serve a given game.

External fonts currently replace the primary faces at runtime. Built-in OFL
fallback glyphs remain compiled in for missing characters and load failures.
Selecting an external font therefore does **not** produce a binary without OFL
font material. This design keeps Latin/CJK text available when a host font lacks
glyphs. A package containing these fallbacks must retain the OFL notices even
when external fonts are the default.

The MIT renderer reads TrueType `glyf` outlines, including supported TTC faces.
It does not execute TrueType hinting programs and does not support CFF/OTTO
outlines; some system CJK collections consequently cannot be used. This is a
font parsing limit, not a license conversion.

## Validation on 2026-11-12

`tools/check_libretro_fonts.sh` exercises external, built-in, missing, invalid
and automatic font selection, manual save/load, and rejection of a state whose
font identity differs even when the envelope CRC is valid. Five animated runs
passed 33 checks each. Two TextDemo runs passed 32 checks each with one expected
static-animation skip; their frame hashes differed between the generated TTF
and built-in glyphs, proving an actual text rendering change.

Windows x64 and i686 each passed six font cases under Wine, including the same
text-rendering distinction. This is Wine validation, not a native Windows or
mobile-device result. A real RetroArch 1.22.2 headless TextDemo run also loaded
an installed TrueType font and exited cleanly after 120 frames.

The memory-font API test releases the caller's font bytes before its first
glyph lookup, verifies rasterization and checks that a rejected replacement
preserves the current provider. It passes normally and under ASan/UBSan; the
`NJ_ENABLE_TTF=0` API stub also compiles with warnings treated as errors. All
seven integration cases also pass with an instrumented frontend and core under
ASan/UBSan, with leak detection and immediate failure on sanitizer errors.
