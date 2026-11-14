# Codec provenance and implementation/data licenses

This record describes source evidence and completed replacements as of
2026-11-13. Runtime implementation code uses MIT, including attributed
third-party MIT adaptations. Built-in AMR numeric data retain Apache-2.0;
font-derived data retain SIL OFL 1.1. The combined package carries these
separate licenses. The identified MPEG-4 and AMR implementation review items
and the legacy table-input replacements described below are complete within
their stated scopes. Keep the full [MIT notices](licenses/third-party-code-MIT.txt),
[AMR license and attribution](AMR_DATA_LICENSES.md), and
[font notices](FONT_LICENSES.md) with distributions containing that material.

A matching numeric table is not proof of copied implementation, and different
variable names or statement order do not establish independent authorship.
The replacements below are not a formal clean-room claim: the existing source
and comparison implementations were inspected during this audit.

## MP3: reproducible MIT table source

The Huffman pair/quad tables and synthesis window in
[`nj_mp3.c`](../src/midp/nj_mp3.c) now have an explicit input:
[puremp3](https://github.com/Herschel/puremp3/tree/eeb913d6f61cd3ae77aa4d8cb38c6cb3f0f58602),
revision `eeb913d6f61cd3ae77aa4d8cb38c6cb3f0f58602`. Its
[Cargo manifest](https://github.com/Herschel/puremp3/blob/eeb913d6f61cd3ae77aa4d8cb38c6cb3f0f58602/Cargo.toml)
identifies Mike Welsh `<mwelsh@gmail.com>` as author and declares
`MIT OR CC0-1.0`; its README expressly offers that choice. This project selects
**MIT**, not the alternative dedication. The complete upstream MIT permission
and warranty text is embedded next to the generated C tables. The upstream
license text supplies no copyright line; this audit adds the manifest's author
attribution without inventing a copyright year or holder statement.

| Pinned input | SHA-256 |
| --- | --- |
| [src/tables.rs](https://raw.githubusercontent.com/Herschel/puremp3/eeb913d6f61cd3ae77aa4d8cb38c6cb3f0f58602/src/tables.rs) | `c59ddd06e355f9d45e308c23d438fe753918579f6a02f95950c857981d686aa9` |
| [LICENSE-MIT](https://raw.githubusercontent.com/Herschel/puremp3/eeb913d6f61cd3ae77aa4d8cb38c6cb3f0f58602/LICENSE-MIT) | `e37223e2aa609fb8b6a095155db70da62a80592777f5d219c040936334d07ff6` |

The remaining band, scalefactor, bitrate and antialias metadata now use
[NLayer](https://github.com/naudio/NLayer/tree/82f7a92e2c0b994157720c71b7d3cb41095adc70),
revision `82f7a92e2c0b994157720c71b7d3cb41095adc70`. The root MIT license credits
**Copyright (c) 2018 Mark Heath, Andrew Ward & Contributors**. The Layer III
file separately credits **Copyright <2005-2012> Fluendo S.A.** and offers
Ms-PL or its attached MIT grant; this project selects **MIT**. Both complete
notices are embedded in the C source. The distributable
[third-party MIT notice bundle](licenses/third-party-code-MIT.txt) also includes
these grants and the puremp3 notice.

| Pinned NLayer input | SHA-256 |
| --- | --- |
| [LayerIIIDecoder.cs](https://raw.githubusercontent.com/naudio/NLayer/82f7a92e2c0b994157720c71b7d3cb41095adc70/NLayer/Decoder/LayerIIIDecoder.cs) | `23bf221aed434eb04650072205571fb836a3a028038af4fa90a56f18148f818d` |
| [MpegFrame.cs](https://raw.githubusercontent.com/naudio/NLayer/82f7a92e2c0b994157720c71b7d3cb41095adc70/NLayer/Decoder/MpegFrame.cs) | `d3983a37b38f4044ca627760a7db51760942002a92dbc4333466ad97e548c299` |
| [LICENSE](https://raw.githubusercontent.com/naudio/NLayer/82f7a92e2c0b994157720c71b7d3cb41095adc70/LICENSE) | `5c0358c3876d9655bc5b2d6555030516266c996c0efffb678807d53ade152850` |

[`gen_mp3_tables.py`](../tools/gen_mp3_tables.py) validates all five hashes before
using any input. It traverses the source trees, including long-offset
continuations, to enumerate the codeword/symbol assignments. It sorts those
assignments into this decoder's left-aligned binary-search representation,
constructs the 128-entry quad prefix lookup, and reconstructs the synthesis
window's exact units of `1/65536` from the source decimal literals. No upstream
decoder routine is transplanted. Normal builds use the committed C arrays and
need no download, Rust compiler, generator, or additional runtime dependency.

The same generator evaluates project-written formulas for MPEG-1/LSF
intensity ratios, the 64 by 16 synthesis cosine matrix, long/short IMDCT half
matrices, sine windows, quarter powers of two and the cube-root mantissas.
Huffman book/linbits metadata come from the selected puremp3 tree descriptors;
band boundaries, pre-emphasis, scalefactor sizes/counts, bitrates and sample-rate
facts come from the named NLayer initializers or sample-rate switch. Antialias
cosines retain NLayer's binary32 values; sines normalize its eight derived
four-decimal slopes, rounding the slope input to binary32 first. This
reproduces the working sine factors exactly instead of adopting three
slightly differently rounded NLayer sine literals. Near-zero synthesis cosine
results below `1e-14` are normalized to exact zero.

One numeric correction is explicit: NLayer's MPEG-2 **24,000 Hz, long band
boundary 18** is `330`; this project retains its working `332`. As an
independent numeric cross-check, summing the first 18 widths of row 3 in
[minimp3's `g_scf_long`](https://github.com/lieff/minimp3/blob/ea99364f61c14656440e8d77e9c233ccf3124633/minimp3.h)
gives `332` (inspected file SHA-256
`57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad`).
No minimp3 implementation, table or license grant is imported into runtime or
the generator; this is a check of an isolated coding fact. The generator marks
the correction as project-maintained, not NLayer's value. The regeneration
comparison verifies that this boundary and all existing working band values
are retained. This evidence is not an absolute legal-clearance claim.

Reproduce after downloading the five pinned inputs above:

```sh
python3 tools/gen_mp3_tables.py \
  --tables /path/to/tables.rs --license /path/to/LICENSE-MIT \
  --nlayer /path/to/LayerIIIDecoder.cs --frame /path/to/MpegFrame.cs \
  --nlayer-license /path/to/NLayer-LICENSE --check
# Use the same inputs with --write for deliberate regeneration.
```

The initial comparison against the prior C payload found:

- All 1,378 pair codewords, their lengths and symbols, all 15 book descriptors,
  all 128 quad entries, and all 512 synthesis coefficients are numerically
  unchanged, including binary32 coefficient representations.
- The regenerated windows, IMDCT half matrices, MPEG-1 intensity ratios and
  quarter-power coefficients retain their binary32 values.
- Sixteen entries of `mp3_nmat`, indices 256 through 271, change from floating
  point approximations to mathematical zero. The largest absolute change is
  `3.674559378e-15`. These are the row whose cosine arguments are odd multiples
  of pi/2. Other matrix entries retain their binary32 values.

The previous inline-table comment referred to an absent provenance report.
This source trail replaces that unsupported reference for the blocks actually
covered by the new generator. It does **not** retroactively prove the origin
of the old tables or complete a source audit of the entire MP3 decoder.

The subsequent complete-table pass covers **all 33 static table blocks**,
including the function-local cube-root constants and two-dimensional rate
metadata. Every integer/binary32 value is unchanged from the working payload
after the initial cosine-zero correction above. No legacy small table remains
outside the reproducible generator. NLayer supplies the MPEG-2/2.5 band values
that the selected puremp3 revision lacks; its repeated MPEG-1 bands are not used.

### Scoped MP3 implementation review

The existing project-authored MP3 implementation remains in place; its git
history describes an authored decoder but supplies no complete original table
generation report. The reproducible replacement covers today's table payload,
not a retrospective proof of every old table's origin or an authorship audit of
every decoder routine. No imported implementation was identified in the MP3
code during this scoped table review. That statement does not clear other
providers. Both selected MIT notices must accompany source/binary
redistribution as applicable; the root MIT file alone is not those notices.

## AMR: MIT implementation and separately licensed data

The selected licensing model keeps runtime algorithm code under MIT and treats
AMR tables as a separate data component under Apache-2.0. All eight speech modes
remain enabled. Both the data replacement and the bounded runtime authorship
review are complete. Their evidence is separate: data generation establishes
the data source trail, while the [runtime implementation record](notes/2026-11-12-amr-independent-implementation.md)
lists the algorithm replacements, retained-source inventory and equation tests.

### Complete Apache data replacement

The [data license report](AMR_DATA_LICENSES.md) maps **all 44 generated
AMR definitions**: 40 derive from 45 numeric arrays in 14 SHA-256-pinned
OpenCORE source files, and four are generated by project-written formulas.
The upstream revision is
`3b67218fb8efb776bcd79e7445774e02d778321d` of
[opencore-amr](https://github.com/BelledonneCommunications/opencore-amr/tree/3b67218fb8efb776bcd79e7445774e02d778321d).

The data inputs carry PacketVideo copyright 1998–2009, Apache License 2.0, and
notices identifying 3GPP TS 26.073 portions, Copyright 2004 3GPP Organizational
Partners (ARIB, ATIS, CCSA, ETSI, TTA, TTC). Their headers expressly state that
permission to distribute, modify and use those files under the stated terms
was obtained from the copyright holder. The selected source notices are
preserved in full; this report records that public permission statement and
does not claim to have independently inspected a private rights agreement.

The current [generator](../tools/gen_amr_tables.py) imports only the pinned
OpenCORE numeric sources and project formulas. It no longer accepts FFmpeg
inputs. No OpenCORE decoder implementation is incorporated by this data
import. The [offline manifest](../tools/data/amr-opencore.json) records the
complete source hashes, selected arrays and conversions. The generated
[header](../src/midp/nj_amr_tables.h) explicitly separates its data terms from
the MIT runtime. Redistribution must include the complete
[Apache-2.0 license](licenses/AMR-Apache-2.0.txt) and
[PacketVideo/3GPP notices](licenses/AMR-OpenCORE-NOTICE.txt), including the
record of local transformations. Renaming data or choosing system decoding
does not remove these obligations when the built-in fallback data are bundled.

Every integer, binary32 value, field offset, macro expression, type and
dimension is preserved. All 14,028 emitted value/macro tokens match the
previous working header. The aggregate typed-value SHA-256 is
`379f403e3764de00ee6674f2673fb5c32cd44fe6322b65c729b05552818b3e26`.
This preserves the decoder's PCM data contract; it does not relicense those
inputs MIT or establish that every individual numeric fact is copyrightable.

Reproduce from a local copy of the pinned upstream files:

```sh
python3 tools/gen_amr_tables.py --check \
  --verify-upstream /path/to/pinned-opencore
python3 tools/check_amr_provenance.py --upstream /path/to/pinned-opencore
# Optional comparison with a saved pre-replacement generated header:
python3 tools/check_amr_provenance.py --upstream /path/to/pinned-opencore \
  --baseline /path/to/previous/nj_amr_tables.h
```

The checker verifies the complete offline generation, all 14 source hashes,
licenses/notices and all 44 definitions. Modified source/manifest data are
rejected. This supersedes the earlier five-file/11-table correspondence check
and the legacy FFmpeg-input reproduction procedure.

### Runtime algorithms and references

The complete bounded review is recorded in the
[runtime implementation inventory](notes/2026-11-12-amr-independent-implementation.md).
Replacements cover spectral reconstruction and LPC polynomial expansion,
fractional pitch coordinates/interpolation, pulse assembly, gain prediction and
smoothing, dense circular convolution and phase selection, synthesis and
postfiltering. Every retained runtime function, type and constant group has a
stated rationale. That inventory found no remaining identified runtime
implementation authorship gate within its scope; it does not extend to unrelated
sources, SDK terms, patents or the separate container parser.

The replacement preserves the local PCM contract: 1,195 streams / 48,814 frames
match the saved task-start implementation byte for byte, including all speech
modes, SID/no-data and local raw/container assets. Independent equation/layout,
malformed-input and output-guard checks also passed under ASan/UBSan and
float-cast-overflow checks. A pre-existing over-cap container output bug was
fixed and covered by a failing-before/passing-after regression. Exact results,
source boundaries, preserved policies and reproduction commands are in the
inventory. These are scoped implementation results, not a new final full-suite
or platform release claim.

The primary algorithm reference is
[ETSI TS 126 090 V17.0.0 / 3GPP TS 26.090](https://www.etsi.org/deliver/etsi_ts/126000_126099/126090/17.00.00_60/ts_126090v170000p.pdf),
486,157 bytes, SHA-256
`b3db2d0216a37ed9f4fbfa99954377f3b1d6f4e40875e862e66605b91b6b087c`.
It describes the spectral, pitch, pulse and gain algorithms; clause 5.7.1
Table 3 covers the 12.2-kbit/s pulse tracks, and clause 6.1 includes smoothing
equations (70)–(74) and phase dispersion. Normative facts and mathematical
algorithms support an original implementation; the reference C implementation
is not copied or relabeled MIT.

The inspected TS 26.104 reference package did not establish an MIT grant and
is not an input to the new generator. Likewise, the previous FFmpeg table
hashes and implementation comparisons are historical audit evidence, rather
than inputs to the selected Apache data route. The complete new source trail,
conversion rules and notices are in the data report above.

### System decoding and built-in fallback

The common AMR media route first validates a complete AMR stream, then tries
the host audio hook and installed audio providers. A provider must return the
entire expected 8 kHz mono PCM16 clip. Errors, wrong formats, truncated output
and stalled providers are discarded, closed, and retried from the original
input with the built-in decoder. Bare AMR, audio-only 3GP and movie AMR tracks
share this route; container sample boundaries and `damr` metadata are checked
before dispatch. No partially decoded clip is accepted as a success.

[System AMR decoding](SYSTEM_AMR.md) documents native backends and provider
selection. `NJ_ENABLE_SYSTEM_AMR=AUTO` enables the native backend on supported
Android hosted non-legacy builds, Apple and Windows; `0` disables this automatic
native backend, while explicitly installed port/custom providers remain eligible.
The existing Android Java audio hook retains priority. A VM-less Android CLI
skips the service path immediately and uses the built-in fallback. Linux uses
the built-in decoder unless a port or custom provider is installed.

Native validation is bounded: an API 21 x86_64 Android app exercised nine AMR
cases through the actual service; Apple has API mocks and arm64/x86_64 compile
checks; Windows Media Foundation has SDK fault-path tests and both Wine
architectures, where a real AMR codec was unavailable. No native Windows AMR
success or Apple device decode is claimed. The system backend may produce
PCM different from the built-in decoder. Saved decoded heap PCM and its
playback cursor restore without opening a codec; native handles and device
queues are cleared. The separately licensed built-in tables remain bundled
for fallback, so system-first selection does not remove their notices.

## MPEG-4 / H.263 boundary

The separate [table provenance report](M4V_TABLE_PROVENANCE.md) records
normative H.263 coding facts and the subsequent replacement of MPEG-4 intra/DC
and scaling inputs with pinned MIT OxideAV material, revision
`ebfef14e48acf63547f73a9a0f4c36a153db92b2`. Its actual license credits
**Copyright (c) 2026 Karpelès Lab Inc.**; the full notice is in the shared
[MIT notice bundle](licenses/third-party-code-MIT.txt). The replacement generator
pins the license and three source files, regenerating all seven numeric arrays
without payload changes, including all 4,096 intra prefix lookups. The four
escape boundary arrays now derive from those event facts and retain their
values. The former FFmpeg-input table gate is superseded by that source trail.

The implementation pass separately replaces the identified transcribed paths.
The inverse transform and half-pixel/chroma sampling use project-written
implementations of published equations. Coefficient/DC events, motion and AC/DC
prediction, packet boundaries and the common VOP syntax now use new C code
adapted from the pinned MIT OxideAV source and its coding facts. Optional
complexity syntax is preserved by a new descriptor walker for the documented
field layout; unsupported upstream branches were not used to remove working
project features. The source retains the selected MIT attribution.

The [implementation record](notes/2026-11-12-mpeg4-independent-implementation.md)
lists exact routines, compatibility contracts, conformance bounds and negative
input tests. This supersedes the former MPEG-4 table and identified-routine
gates, without claiming a formal clean-room or whole-repository legal audit.
The AMR data replacement and runtime review are covered separately above;
they retain Apache data attribution alongside MIT implementation code.

## Recorded scoped validation

Use an isolated build so concurrent platform work does not share its cache:

```sh
cmake -S . -B build-mit-audio -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DNJ_BUILD_TESTS=ON -DNJ_BUILD_CLI=ON \
  -DNJ_BUILD_SDL2=OFF -DNJ_BUILD_LIBRETRO=OFF \
  -DNJ_BUILD_EMBEDDED_PORTS=OFF -DNJ_BUILD_POSIX_NETWORK=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
cmake --build build-mit-audio --target \
  test_mp3 test_amr test_amr_mp4 test_avcodec \
  test_media_replay test_media_backpressure -j 4
ctest --test-dir build-mit-audio --output-on-failure \
  -R '^(test_mp3|test_amr|test_amr_mp4|test_avcodec|test_media_replay|test_media_backpressure)$'
```

On 2026-11-12, after the complete MP3 table regeneration and partial AMR
implementation rebuild, all six focused tests above passed (6/6, 0.17 seconds)
in the isolated Debug build. This includes the MP3 tests' embedded LAME/FFmpeg
PCM comparisons as well as all AMR mode/container, provider selection, media
replay and backpressure checks. The first AMR trial failed three 12.2-kbit/s
PCM checks because a conventional Gray formula did not match the working map;
the corrected final run retains the map and passes every focused test.

The generator's `--check` passes using all five pinned inputs, covering every
one of the 33 static MP3 table blocks with no generated-block differences.
The second pass preserves every table value. The separate finite-input AMR
probe described above verifies the pulse and smoothing arithmetic contracts.

After the phase replacement, the same six focused tests passed again (6/6,
0.19 seconds). A separately configured `build-mit-audio-asan` with
`-fsanitize=address,undefined -fno-omit-frame-pointer` in C flags and
`-fsanitize=address,undefined` in executable linker flags passed `test_mp3`,
`test_amr` and `test_amr_mp4` (3/3, 0.08 seconds), without sanitizer diagnostics.
A temporary compiled ASan/UBSan phase probe additionally checked:

- 182,880 single-pulse cases across lags 17..143, all 40 positions, both signs,
  six sharpening factors and three kernels: bitwise equality to the previous
  routine plus comparison with an independently expanded dense convolution.
- 10,000 multi-pulse cases with the same bitwise/equation checks.
- 682 zero/short-lag, empty-vector and invalid-position cases against the dense
  equation, without invoking the old helper's undefined short-lag behavior.
- 200,000 finite gain/history/mode cases: identical selector state, bypass
  selection and filtered samples across the old and new implementations.

These earlier experiments preserve the PCM contract and check edge handling.
They are historical scoped results, followed by the complete AMR data
replacement and runtime inventory above. Data permissions are established by
the pinned Apache source grants and preserved notices; implementation review
rests on its replacements and retained-source inventory. PCM equality and
generator reproduction alone are not authorship evidence. For current combined
candidate results, use [system AMR verification](SYSTEM_AMR.md#verification-and-reproduction).
