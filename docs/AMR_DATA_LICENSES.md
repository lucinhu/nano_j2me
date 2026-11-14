# AMR-NB data sources and licenses

The built-in AMR decoder's numeric data are separately licensed under
**Apache-2.0**. The Apache data retain their own license; this is not a claim
that the complete binary is MIT-only.
Platform system-provider selection is documented separately by the media layer.
This report covers the data import, not the authorship review of decoder code.

All 44 definitions in [the generated header](../src/midp/nj_amr_tables.h) now
come from a new numeric-input path: 40 definitions derive from pinned OpenCORE
Apache inputs, and four are original arithmetic formula output. The old
FFmpeg extraction is no longer an input to the generator. Existing values
were used only for the compatibility comparison described below.

The separate [SID reference header](../src/midp/nj_amr_dtx.h) contains one
additional Apache-derived definition, `nj_amr_sid_reference[8][10]`. Its 80
signed Q15 values come directly from `past_rq_init` in the same pinned
`common/src/q_plsf_3_tbl.cpp`: the only transformation is reshaping the flat
array into eight rows of ten. No scaling, rounding, or runtime-header extraction
is used. Both headers are generated from the numeric manifest; the original
44-definition compatibility fingerprint excludes this separate SID definition.

## Sources and redistribution

- Upstream: [BelledonneCommunications/opencore-amr](https://github.com/BelledonneCommunications/opencore-amr).
- Fixed revision: `3b67218fb8efb776bcd79e7445774e02d778321d`.
- [Checked-in numeric inputs](../tools/data/amr-opencore.json) carry the
  upstream URL, revision, SHA-256 of each complete source file, exact selected
  integer arrays, the documented mode energy values, and a modification notice.
- [Full Apache License 2.0](licenses/AMR-Apache-2.0.txt) is copied byte for
  byte from that revision's root license.
- [AMR attribution and modification notices](licenses/AMR-OpenCORE-NOTICE.txt)
  preserve PacketVideo's 1998–2009 notices, the 1998–2010 notice on
  `post_pro.cpp`, and the 2004 3GPP Organizational Partners attribution and
  explicit permission to distribute, modify and use under the Apache terms.
  The applicable AMR-NB portions of upstream `opencore/NOTICE` are included.
  Its unrelated MPEG-4/AAC/AMR-WB and other code notices are not imported.

Distribute both license/notice files with source and binary packages that
include these data. Preserve their attribution and modification statements.
The MIT project license does not replace the Apache data license. Apache's
patent and trademark provisions apply as written; this provenance work is not
an AMR patent clearance or an upstream endorsement.

The import extracts numeric values only. It does not import OpenCORE decoder
functions or its fixed-point helper implementations. The runtime implementation
and the MIT generator/checker code remain separate from the licensed data.

## Complete definition mapping

Input paths in the following table are relative to
`opencore/codecs_v2/audio/gsm_amr/amr_nb/` in the pinned revision. Each source
array is recorded in the numeric manifest and checked independently against
the complete upstream file when `--upstream` is supplied. The source SHA-256
catalog follows the mapping. Definitions with more than one source cite all
inputs; integer selections preserve their values exactly.

| Generated definition | Inputs | Transformation / formula |
| --- | --- | --- |
| `nj_amr_b60_sinc` | `common/src/pred_lt.cpp#inter_6_pred_lt` | Q15 / 32768 rounded to 6 significant digits |
| `nj_amr_base_five_table` | Original MIT formula | three base-five digits for 0..124, zero padding 125..127 |
| `nj_amr_energy_mean` | `common/src/gc_pred.cpp#mode-mean-comments` | documented mode mean energies in gc_pred.cpp |
| `nj_amr_energy_pred_fac` | `common/src/gc_pred.cpp#pred` | reverse Q13 / 8192 rounded to two decimals |
| `nj_amr_gains_4k75` | `dec/src/qgain475_tab.cpp#table_gain_MR475` | identity |
| `nj_amr_gains_high` | `common/src/qua_gain_tbl.cpp#table_gain_highrates` | first two columns of four |
| `nj_amr_gains_low` | `common/src/qua_gain_tbl.cpp#table_gain_lowrates` | first two columns of four |
| `nj_amr_gray_decode` | `common/src/gray_tbl.cpp#dgray` | multiply inverse Gray positions by track stride five |
| `nj_amr_highpass_gain` | `dec/src/post_pro.cpp#a`<br>`dec/src/post_pro.cpp#b` | b[0]/8192 |
| `nj_amr_highpass_poles` | `dec/src/post_pro.cpp#a`<br>`dec/src/post_pro.cpp#b` | -a[1:]/8192 |
| `nj_amr_highpass_zeros` | `dec/src/post_pro.cpp#a`<br>`dec/src/post_pro.cpp#b` | b[1:]/b[0] |
| `nj_amr_ir_filter_medium` | `common/src/ph_disp_tab.cpp#ph_imp_mid` | Q15 / 32768 rounded to six decimals |
| `nj_amr_ir_filter_strong` | `common/src/ph_disp_tab.cpp#ph_imp_low` | Q15 / 32768 rounded to six decimals |
| `nj_amr_ir_filter_strong_7k95` | `common/src/ph_disp_tab.cpp#ph_imp_low_MR795` | Q15 / 32768 rounded to six decimals |
| `nj_amr_lsf_3_1` | `common/src/q_plsf_3_tbl.cpp#dico1_lsf_3` | identity |
| `nj_amr_lsf_3_1_7k95` | `common/src/q_plsf_3_tbl.cpp#mr795_1_lsf` | identity |
| `nj_amr_lsf_3_2` | `common/src/q_plsf_3_tbl.cpp#dico2_lsf_3` | identity |
| `nj_amr_lsf_3_3` | `common/src/q_plsf_3_tbl.cpp#dico3_lsf_3` | identity |
| `nj_amr_lsf_3_3_5k15` | `common/src/q_plsf_3_tbl.cpp#mr515_3_lsf` | identity |
| `nj_amr_lsf_3_mean` | `common/src/q_plsf_3_tbl.cpp#mean_lsf_3` | frequency /16384 *4000 rounded to 6 significant digits |
| `nj_amr_lsf_5_1` | `common/src/q_plsf_5_tbl.cpp#dico1_lsf_5` | identity |
| `nj_amr_lsf_5_2` | `common/src/q_plsf_5_tbl.cpp#dico2_lsf_5` | identity |
| `nj_amr_lsf_5_3` | `common/src/q_plsf_5_tbl.cpp#dico3_lsf_5` | identity |
| `nj_amr_lsf_5_4` | `common/src/q_plsf_5_tbl.cpp#dico4_lsf_5` | identity |
| `nj_amr_lsf_5_5` | `common/src/q_plsf_5_tbl.cpp#dico5_lsf_5` | identity |
| `nj_amr_lsf_5_mean` | `common/src/q_plsf_5_tbl.cpp#mean_lsf_5` | frequency /16384 *4000 rounded to 6 significant digits |
| `nj_amr_lsp_avg_init` | `common/src/q_plsf_5_tbl.cpp#mean_lsf_5` | identity |
| `nj_amr_lsp_sub4_init` | `common/src/lsp_tab.cpp#lsp_init_data` | divide initial Q15 samples by 1000 (all exact multiples) |
| `nj_amr_order_10k2` | `common/src/bitno_tab.cpp#bitno_MR102`<br>`common/src/bitreorder_tab.cpp#reorderBits_MR102` | inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets |
| `nj_amr_order_12k2` | `common/src/bitno_tab.cpp#bitno_MR122`<br>`common/src/bitreorder_tab.cpp#reorderBits_MR122` | inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets |
| `nj_amr_order_4k75` | `common/src/bitno_tab.cpp#bitno_MR475`<br>`common/src/bitreorder_tab.cpp#reorderBits_MR475` | inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets |
| `nj_amr_order_5k15` | `common/src/bitno_tab.cpp#bitno_MR515`<br>`common/src/bitreorder_tab.cpp#reorderBits_MR515` | inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets |
| `nj_amr_order_5k9` | `common/src/bitno_tab.cpp#bitno_MR59`<br>`common/src/bitreorder_tab.cpp#reorderBits_MR59` | inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets |
| `nj_amr_order_6k7` | `common/src/bitno_tab.cpp#bitno_MR67`<br>`common/src/bitreorder_tab.cpp#reorderBits_MR67` | inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets |
| `nj_amr_order_7k4` | `common/src/bitno_tab.cpp#bitno_MR74`<br>`common/src/bitreorder_tab.cpp#reorderBits_MR74` | inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets |
| `nj_amr_order_7k95` | `common/src/bitno_tab.cpp#bitno_MR795`<br>`common/src/bitreorder_tab.cpp#reorderBits_MR795` | inverse OpenCORE bit permutation, per-byte bit reversal, local field offsets |
| `nj_amr_payload_bytes` | `common/src/bitno_tab.cpp#bitno_MR475`<br>`common/src/bitno_tab.cpp#bitno_MR515`<br>`common/src/bitno_tab.cpp#bitno_MR59`<br>`common/src/bitno_tab.cpp#bitno_MR67`<br>`common/src/bitno_tab.cpp#bitno_MR74`<br>`common/src/bitno_tab.cpp#bitno_MR795`<br>`common/src/bitno_tab.cpp#bitno_MR102`<br>`common/src/bitno_tab.cpp#bitno_MR122`<br>`common/src/bitno_tab.cpp#bitno_MRDTX` | ceil(parameter bits /8), including SID bitno_MRDTX |
| `nj_amr_pow_0_55` | Original MIT formula | round(0.55 ** i, 6) for i=1..10 |
| `nj_amr_pow_0_7` | Original MIT formula | round(0.7 ** i, 6) for i=1..10 |
| `nj_amr_pow_0_75` | Original MIT formula | round(0.75 ** i, 6) for i=1..10 |
| `nj_amr_pred_fac` | `common/src/q_plsf_3_tbl.cpp#pred_fac_3` | Q15 /32768 rounded to six decimals |
| `nj_amr_qua_gain_code` | `common/src/gains_tbl.cpp#qua_gain_code` | first column of 33x3, omit sentinel final row |
| `nj_amr_qua_gain_pit` | `common/src/gains_tbl.cpp#qua_gain_pitch` | identity |
| `nj_amr_track_position` | `common/src/c2_9pf_tab.cpp#startPos` | identity |

Independent SID header:

| Generated definition | Input | Transformation |
| --- | --- | --- |
| `nj_amr_sid_reference` | `common/src/q_plsf_3_tbl.cpp#past_rq_init` | identity signed Q15 values; reshape 80 entries into `[8][10]` |

The bit maps are reconstructed by inverting OpenCORE's canonical-to-packet
bit permutations, reversing the bit number within each byte for the local
LSB-addressed unpacker, and grouping the bits by the project's frame field
layout. The generator validates that each permutation covers exactly the
mode's bit count. The final zero sentinel and local `NJ_AMR_OFF` macros are
project format details. No legacy FFmpeg bit-position list is imported.

Floating-point conversions explicitly retain the decoder's decimal
quantization: interpolation sinc and LSF means use six significant digits;
phase impulse responses and spectral prediction factors use six decimal
places; energy predictor coefficients use two decimal places. Postfilter
coefficients use exact fixed-point ratios. Formula powers are rounded to six
decimal places. The emitted `float` values match the prior binary32 storage
bits exactly, including the postfilter coefficients whose decimal spelling
changed. Base-five triples cover 0–124 with three zero-padded entries.

## Pinned complete-file hashes

| Input file | SHA-256 |
| --- | --- |
| [common/src/bitno_tab.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/bitno_tab.cpp) | `609b27cdb2792703d833f4939858d5fbe9fcc8f97fd5454095eb31ea607dab46` |
| [common/src/bitreorder_tab.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/bitreorder_tab.cpp) | `4b4c2ab8326ea5382cdfda02dfa1a2b1745303c6c05d27f144336da6e6e193ec` |
| [common/src/c2_9pf_tab.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/c2_9pf_tab.cpp) | `1e341cd603f552d435bd61104c73a9a2fc22c2441f8142f17561da65a09edb50` |
| [common/src/gains_tbl.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/gains_tbl.cpp) | `4453ba4fb31568e7494df68372db19d250d703c6ff2b46eb36ef6a2dc24c31c1` |
| [common/src/gc_pred.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/gc_pred.cpp) | `d1a78c0c13282b671a6805597d5380a10ae820c360f03874f55a81b6f63424c2` |
| [common/src/gray_tbl.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/gray_tbl.cpp) | `455a07453d0b1acdb2be8c1c5d8ce85a1d882bb191e3f4c5d9e4bb5d716f1c95` |
| [common/src/lsp_tab.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/lsp_tab.cpp) | `342972b741cb61b7926568aae3234a5fa65c9d670f76ae483f3ff68f909ee1b2` |
| [common/src/ph_disp_tab.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/ph_disp_tab.cpp) | `887a153b0554727c9c15c256cf71184a231679f629b64d0953a6db6ef6ab106a` |
| [common/src/pred_lt.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/pred_lt.cpp) | `0a49d7f512d065136cfe175a367ebab60c6f8fb9c8f4cff36993defabd9d6f44` |
| [common/src/q_plsf_3_tbl.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/q_plsf_3_tbl.cpp) | `3ab12e9a16e13952f642b09755c9979569899b5bb0e0fb2596c2fd8bc4611d0a` |
| [common/src/q_plsf_5_tbl.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/q_plsf_5_tbl.cpp) | `8c89a6adec9395210e8566df8ff19b5a5197504de34a22432b591239cb1126f2` |
| [common/src/qua_gain_tbl.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/common/src/qua_gain_tbl.cpp) | `c646f376d33563ede32b03618ad0d42d4142361d0de134a6d3755715f1518621` |
| [dec/src/post_pro.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/dec/src/post_pro.cpp) | `1db8123de197fb0f0483ef87f4373366bbb69ad6df3bc36b61c154af8ab5734d` |
| [dec/src/qgain475_tab.cpp](https://github.com/BelledonneCommunications/opencore-amr/blob/3b67218fb8efb776bcd79e7445774e02d778321d/opencore/codecs_v2/audio/gsm_amr/amr_nb/dec/src/qgain475_tab.cpp) | `0953ffa7730348061cb621c63454561327c00ad385e2e846a43ed20bee6a7fda` |

## Reproduce and verify

Offline checks use only Python's standard library and the checked-in numeric
manifest. They require no FFmpeg checkout, network access or decoder toolchain.

```sh
python3 tools/gen_amr_tables.py --check
python3 tools/check_amr_provenance.py
```

For an independent source comparison, obtain the fixed OpenCORE revision and
compare every stored numeric input to its original source file:

```sh
git clone https://github.com/BelledonneCommunications/opencore-amr.git /tmp/nano-j2me-opencore-data
git -C /tmp/nano-j2me-opencore-data checkout --detach 3b67218fb8efb776bcd79e7445774e02d778321d
python3 tools/check_amr_provenance.py --upstream /tmp/nano-j2me-opencore-data
python3 tools/gen_amr_tables.py --check --verify-upstream /tmp/nano-j2me-opencore-data
python3 tools/gen_amr_tables.py --import-opencore /tmp/nano-j2me-opencore-data > /tmp/amr-opencore-reproduced.json
cmp tools/data/amr-opencore.json /tmp/amr-opencore-reproduced.json
```

After reviewing a deliberate generator change, regenerate each header separately:

```sh
python3 tools/gen_amr_tables.py --write
python3 tools/gen_amr_tables.py --sid --write
```

With no write/check flag, `--sid` emits only the SID header on stdout; the
default stdout remains the original 44-definition header. `--check` always
checks both complete headers byte for byte. The SID generation reads only
`past_rq_init` from the manifest, never the checked runtime header.

The import subcommand reads only the complete-file hashes embedded in the
source catalog. The default path accepts only the SHA-256-pinned checked-in
manifest; output is reconstructed from its raw numeric arrays and formulas.
The checker additionally verifies all 44 definition names, traceability of
40 imported datasets, the four formula-only exceptions, full license/notice
copies, exact reproduction of both generated headers, the SID's exact 80
signed values and `[8][10]` declaration, and the original aggregate typed-value
compatibility fingerprint. Every stored numeric source array must map to at
least one generated definition; the new SID array is included in this check.

## Verification recorded 2026-11-12

- All 44 definitions reproduce from the new inputs; no old header is read by
  the generation path. An independent comparison against the pre-change header
  found zero differences in integers, binary32 values, offset macros,
  declaration types or dimensions.
- Typed-value SHA-256:
  `379f403e3764de00ee6674f2673fb5c32cd44fe6322b65c729b05552818b3e26`.
  This is a compatibility fingerprint, not a provenance source.
- Both offline checks and all 14 complete-file upstream hash/numeric checks
  passed. Re-importing the pinned checkout reproduces the numeric manifest.
- Focused AMR, AMR-in-MP4, MP3, media replay and backpressure tests passed
  (5/5). The AMR, AMR-in-MP4 and MP3 tests also passed with ASan/UBSan enabled
  (3/3), with no sanitizer diagnostics. Provider integration tests belong to
  the media layer's separate verification.

## SID verification recorded 2026-11-13

- Both offline checks reproduce the two headers byte for byte. The separate SID
  output matches the frozen `nj_amr_dtx.h`, including its declaration, all 80
  values and attribution. Generation does not read either runtime header.
- Independent re-import from the pinned OpenCORE sources matches the complete
  manifest, including `past_rq_init`. All 14 complete-file SHA-256 checks,
  upstream license comparison, and both `--verify-upstream` / `--upstream`
  commands pass.
- The manifest SHA-256 is
  `5b089192e6daf64e3fc25331481289409b56156858db3c3d7206584b52515880`.
  The original 44-definition typed-value SHA-256 remains
  `379f403e3764de00ee6674f2673fb5c32cd44fe6322b65c729b05552818b3e26`.
- Deliberately changed SID values, dimensions, missing entries, and an input
  outside signed Q15 range are rejected. The bundled license and NOTICE bytes
  remain unchanged.

The decoder runtime authorship and system-provider behavior are outside this
mapping. [The codec provenance report](CODEC_PROVENANCE.md) records historical
findings; its earlier MIT-only-data gate was superseded by accepting separately
licensed Apache data. This new input path resolves that data-source replacement
without retroactively asserting that the previous FFmpeg extraction was MIT.
