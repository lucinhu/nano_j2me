# AMR runtime: independent implementation and bounded authorship review

Date: 2026-11-12. Scope: `src/midp/nj_amr.c`, its private header, and `tests/private/test_amr_equations.c`. Table provenance and platform/provider integration are documented separately in [AMR_DATA_LICENSES.md](../AMR_DATA_LICENSES.md) and [SYSTEM_AMR.md](../SYSTEM_AMR.md).

## Decision and source boundary

The runtime implementation is MIT, with separately attributed Apache-2.0 numeric data. This is **not an MIT-only license claim for the combined decoder**. The selected product policy is system AMR where available, with this built-in fallback elsewhere; provider selection is outside this task.

The previous assertion that commit `b32e52c` was simply “ours” was insufficient: the subsequent functional comparison found specific expression and structural overlap with FFmpeg in spectral reconstruction, pitch decoding, pulse assembly, synthesis, postfiltering and phase processing. This task replaces the nontrivial codec implementations described below and explicitly reviews the retained arithmetic and integration code. Changing attribution text alone would not resolve that finding.

Primary mathematical source: [ETSI TS 126 090 V17.0.0 / 3GPP TS 26.090](https://www.etsi.org/deliver/etsi_ts/126000_126099/126090/17.00.00_60/ts_126090v170000p.pdf). The local PDF at `/tmp/nano-j2me-mit-audio-audit/26090.pdf` was hash checked: SHA-256 `b3db2d0216a37ed9f4fbfa99954377f3b1d6f4e40875e862e66605b91b6b087c`. Sections 5.2.4–5.2.6, 5.6.1, 5.7.1, 5.8.2, 6.1 and 6.2 supply the equations and codec geometry. RFC 4867 supplies the storage framing facts. Existing code was read to identify problems and preserve the API and rounding contract; this was an independent equation implementation after exposure, **not a claim of a blinded clean-room process**. FFmpeg or OpenCORE decoder implementation text was not used as replacement code.

The table task rebuilt all 44 arrays (14,028 typed numeric tokens) without changing their typed values: 40 Apache-derived arrays and four original formula arrays. Its frozen generated-header SHA-256 is `7ada26fcaf9a1c70e97af8cc5f79f106a2019ee3296771a435a93669be592835`. The inputs are 14 hash-pinned OpenCORE files at revision `3b67218fb8efb776bcd79e7445774e02d778321d`. Data attribution and transformation details remain in [AMR_DATA_LICENSES.md](../AMR_DATA_LICENSES.md), [Apache license](../licenses/AMR-Apache-2.0.txt), and [OpenCORE notice](../licenses/AMR-OpenCORE-NOTICE.txt). Neither task relicenses these arrays as MIT. This runtime task did not edit the tables or their generator.

## Replacements and mathematical basis

| Routines / historical group | Final implementation and reason |
| --- | --- |
| Historical `nj_amr_lsf2lsp_12k2_part`, `nj_amr_lsf2lsp_12k2`, `nj_amr_lsf2lsp_3` → `nj_amr_decode_spectrum` | A single band/set traversal fetches residuals, adds the MA predictor, enforces frequency separation, forms cosine LSPs, and interpolates. Sections 5.2.5–5.2.6, (20)–(28). Replaces the old helper decomposition and split-by-split temporary residual construction. Preserves separate binary32 mean/predictor operations for PCM. |
| Historical `nj_amr_lsp2poly` plus `nj_amr_lspd2lpc` → new `nj_amr_lspd2lpc` | Expands even/odd root products together in a two-dimensional half-polynomial workspace, then combines symmetric/antisymmetric terms. Section 5.2.4, (18)–(19) and its explicitly stated recurrence. The moving midpoint's factor two is normative mathematics. The private test independently expands full degree-ten products. |
| Historical sixth/third pitch helpers → `nj_amr_pitch_window`, `nj_amr_pitch_coordinates` | Uses physical coordinates in sixths or thirds and ordinary division; removes reciprocal multiply/shift expressions and misleading quarter-sample wording. Section 5.6.1 provides ranges; encoded index numbering is a retained format fact. `nj_amr_decode_pitch_vector` now calls this coordinate decoder. |
| Historical generic interpolation → `nj_amr_interpolate_pitch` | Direct two-sided, ten-term evaluation of (40), fixed to sixth-sample coefficient spacing. Maintains pairwise binary32 accumulation and recursive extension of excitation history. No generic upstream DSP helper interface remains. |
| Historical `nj_amr_10bit_pulse` and 8-pulse assembly → new `nj_amr_8_pulses_31bits` | A lane map unpacks two three-coordinate words and one folded pair, then a uniform eight-pulse traversal assigns tracks and relative signs. Section 5.7.1/Table 4 supplies tracks; compressed word permutations are format facts. Removed old three-output helper and track-pair construction. |
| Low-rate branch of `nj_amr_decode_fixed_sparse` | One pulse traversal selects shifts and track coordinates per mode; Tables 5–8 supply track sets. The bit locations are the unpacked wire format, not imported source expressions. Preserves the existing 5.9 collision policy. |
| `nj_amr_10_pulses_35bits` | Earlier independent replacement retained: uniform pulse traversal with pair-relative sign and attributed Gray-position data, Table 3. It was reread in this task and included in equation/layout, sanitizer and complete PCM checks. |
| `nj_amr_set_fixed_vector`, historical sparse clearing helper | Finite geometric pulse trains implement pitch sharpening. A normal bounded `for` traversal replaces the old conditional/do-while assembly. Complete work-vector clearing replaces sparse reverse clearing; the old clear helper is removed. Pulse order remains stable for binary32 accumulation. |
| `nj_amr_decode_gains`, `nj_amr_set_fixed_gain` | Explicit scalar/joint mode selection with Q14/Q11/Q12 units; predicted dB, amplitude, code RMS, and chronological MA-error update are separate stages. Sections 5.8.2 and 6.1, (53)–(58), (66)–(69). Removes the old nested mode selection and compound gain/predictor expression. |
| `nj_amr_fixed_gain_smooth` | Earlier independent distance/mean/clamped blend implementation retained and reread: 6.1 (70)–(74), explicit elapsed counters, binary32 blend `4*distance-1.6`. Included in complete PCM tests. |
| `nj_amr_apply_ir_filter`, `nj_amr_phase_level`, `nj_amr_anti_sparseness` | Earlier independent replacement retained and reread: finite sparse-basis Horner evaluation of circular convolution, insertion-sort median, onset/hold/one-level release, explicit mode/filter selection. Section 6.1 step 5. New tests compare against direct dense long-double convolution. Data provenance was closed separately, not by removing notices. |
| `nj_amr_synthesis` | Constructs the weighted excitation and its energy together, optionally emphasizes pitch and computes normalization, then applies (79). Section 6.1 (75)–(79). Replaces generic weighted-sum/energy-scaling helper choreography and early overflow scan. |
| Historical tilt helper + postfilter helper pipeline → new `nj_amr_postfilter` | Computes an explicit 22-sample impulse recurrence, then interleaves pole/zero filtering and forward tilt with energy accumulation; applies the specified AGC recurrence. Section 6.2 (80)–(86). Removes the old zero-filter, reverse-tilt, generic AGC and energy-scale helpers. Preserves pole-before-zero order for binary32 PCM. |

## Retained-source inventory and bounded rationale

Every remaining function, type and constant group in the runtime/header was read. “Retained” is a reasoned scope decision, not an inference from the old license label.

| Retained functions / declarations | Review result and reason |
| --- | --- |
| `nj_amr_sqrt`, `nj_amr_exp10`, `nj_amr_log10`, `nj_amr_cos` | Local libm-free Newton, range-reduced exponential Taylor, atanh logarithm, and quadrant-reduced sine/cosine series. These are elementary numerical methods, not the identified upstream AMR decoder implementation. Their bounded loops, seeds, precision and formulas were inspected; new tests compare 241 points per function with host libm. |
| `nj_amr_truncf`, `nj_amr_dot`, `nj_amr_weighted_sum` | A C truncating cast, left-to-right inner product, and elementwise two-weight sum. No nontrivial codec logic is hidden here. Preserve binary32 operation order. |
| `nj_amr_lp_synthesis` | Direct `y[n] = x[n] - sum(a[k]*y[n-k])`, equation (79), with ten preceding history samples. A minimal all-pole recurrence follows immediately from the equation; no specialized upstream fast path is retained. |
| `nj_amr_order_2` | Elementary direct-form-II biquad state recurrence for section 6.2.2 (87). Coefficients and scale are separately attributed numeric data. Two state words shift per sample; no codec-specific upstream decomposition remains. |
| `nj_amr_pitch_sharpening` | Direct gain clamp and assignment of lag/beta, with the 4.75 pair-update policy. A small normative arithmetic/state step, reviewed for dependency order and bound. No separate nontrivial DSP algorithm. |
| `nj_amr_state_init`, `nj_amr_update_state` | Zeroed local state, attributed initial-value arrays, fixed history copies, and gain/error queues. Frame/subframe geometry controls lengths. Ordinary local lifecycle code; no allocations or process-global mutable state. |
| `nj_amr_bit_reorder`, `nj_amr_unpack`, `nj_amr_frame_len` | Table-record bit gathering, payload-length check, and fixed type-length lookup. Tables are trusted compile-time data; the `size` argument clears the destination and does not validate arbitrary tables. Source comment corrected accordingly. Framing and table representation are format/interface facts. |
| `nj_amr_decode_pitch_vector` | Local pointer geometry around the decoded fractional delay and copy into the pitch work vector. Its nontrivial coordinate/interpolation algorithms were replaced, above. |
| `nj_amr_decode_frame` | Codec stage ordering required by dependencies: unpack → spectrum → four subframes → high-pass → spectral mean update. It calls the replaced algorithms. Retained buffer handling, gain queues and quantized history implement documented compatibility policies below. This integration is not counted as proof of reference conformance. |
| `nj_amr_put_s16` | Round nearest away at half, clip signed 16-bit limits, write two little-endian bytes. Ordinary output serialization. |
| `nj_amr_frames`, `nj_amr_decode` | Local raw-file API, magic check, bounded walk, caller-owned output, 15,000-frame cap. The storage framing derives from RFC 4867. No upstream codec implementation is embedded in this walk. |
| `nj_amr_mp4_frames`, `nj_amr_mp4_decode` | Local adapters to the separately owned sample iterator, strict frames-per-sample validation, frame decode and PCM serialization. Moved the decode cap check before output (boundary fix below). Included in container regression and bitwise corpus comparisons. `nj_amr_mp4.c` itself was not edited or claimed by this audit. |
| `nj_amr_subframe`, `nj_amr_frame`, `nj_amr_fixed`, `nj_amr_state`, macros, enums, pointer lists; all header declarations | Parameter grouping, history/workspace geometry, standard mode numbers and private API declarations. The historical parameter layout is retained because the generated maps bind field offsets through `offsetof`. This simple data interface is not a copied algorithm. Array contents and initial states retain their separate data notices. The disabled branch contains only an empty-unit typedef. |

Within this explicitly bounded runtime review, there is no remaining identified runtime-implementation authorship gate. This conclusion rests on the replacements and inventory, not on the SPDX change, output similarity, or the earlier commit message. It does not assess unrelated codec sources, platform SDK terms, patent rights, the standard document's redistribution terms, or the separate container parser's authorship. Existing historical records are not erased.

## Preserved compatibility policies and limits

- All eight narrowband speech modes, raw and MP4/3GP APIs, 8 kHz mono signed little-endian PCM, caller-owned buffers, no runtime allocation, and the 15,000-frame limit remain.
- SID and no-data produce silence; there is no comfort-noise synthesis or missing-frame concealment. Reserved raw frame types stop the walk; incomplete trailing frames are ignored. Container samples are validated strictly.
- The Gray maps, compressed pulse numbering, sign-bearing word order, and 5.9 duplicate-position collapse remain format/compatibility facts. Apache data attribution is preserved.
- Excitation history is built with unsmoothed gain, truncated to integer-valued floats, and carried between subframes. Spectral smoothing uses the previous frame's mean. The 4.75 sharpening update occurs on the second/fourth subframe.
- Phase selection retains the two-subframe onset hold, quiet gain below five bypass, and median rule on every non-onset level. These details are not all fixed by the cited prose.
- Synthesis overflow retry quarters the current pitch vector and omits emphasis, rather than scaling all excitation history. Postfilter denominator-before-numerator order and suppression of negative tilt in all modes remain. Binary32 expression/accumulation order is preserved where it affects PCM.
- These deliberate policies and floating-point arithmetic mean this is not a bit-exact TS 26.073 conformance claim. PCM parity proves compatibility with the preserved local runtime, not normative correctness or authorship by itself.

## Validation results

The pre-edit source was preserved before replacements as `/tmp/nano-j2me-mit-audio-audit/task16/nj_amr.baseline.c`, SHA-256 `326c51f8f918a5a5dd428a46aa5bd14d0b4410a4756a754da5ca29c8752ed884`, compiled as a separate shared object with `cc -std=c99 -O2`. The comparison loads old and new runtimes independently and checks both frame counts and every output byte. The reference is the task-start runtime, which already contained the earlier independent phase/10-pulse/smoothing changes, not a newly copied third-party decoder. The table task independently established typed numeric identity across its data replacement.

| Bitwise comparison group | Streams | Frames | SHA-256 of concatenated replacement PCM |
| --- | ---: | ---: | --- |
| Per-mode synthetic (all zero, all one, seeded random; 16 frames each) | 800 | 12,800 | `3cc96cbf8b66e43c6ec9be7b12abc2269ee564890667095bb6732aac2cfc466a` |
| Mixed speech/SID/no-data and quality-bit values; 64 frames each | 200 | 12,800 | `e864e89239e643fdfed7b1157b0c1be0616c594f7691af88f8d6f5ed59dff353` |
| Raw AMR assets discovered by bytes in local game JARs | 191 | 16,524 | `4d2106bd05f8777673f457743663d64f1b6b59d57a0bb55e7d3c08a5a8c17ab8` |
| AMR tracks in local game ISO containers | 4 | 6,690 | `5c86144497f80920495b061fafc2c2de59372f1873efa6768f73a50ea65c9061` |

All 1,195 streams / **48,814 frames** match bitwise. The harness and machine-readable result are temporary local audit artifacts `/tmp/nano-j2me-mit-audio-audit/task16/compare.py` and `comparison.json`; the committed private test does not depend on them or on game assets.

`tests/private/test_amr_equations.c` passes with `-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer`: 300 independent full-product LPC checks; absolute pitch enumeration and all in-format pitch codewords over 127 prior lags; 109,568 pulse-layout cases; 2,000 dense long-double circular-convolution comparisons; gain prediction, zero-LPC synthesis/postfilter and emphasized-energy identities; all mode truncations, reserved types, SID/no-data, null inputs, 5,000 bounded malformed byte strings, 16,384 random speech frames, output guards and the 15,000-frame cap. No sanitizer finding occurred. The initial pitch-range test used incorrect relative-index bit widths; those test widths were corrected from the generated format records before the passing run. Runtime code was not changed to accommodate that test mistake.

Final bounded-input review found an existing off-by-one in `nj_amr_mp4_decode`: it checked the frame cap only after writing frame 15,001. A new minimal synthetic one-chunk fixture reproduced this through the output guard at byte 4,800,000. The guard now runs before decode/output; exact-cap containers still decode 15,000 frames, while over-cap containers return zero without writing beyond the cap. Their earlier partial output remains unspecified and must not be consumed after a zero return. The regression failed before the fix and passes under the three sanitizers afterward. This malformed-input boundary correction is the only intentional behavior change; all valid-stream PCM comparison results above were repeated after it.

The existing container regression also passes independently under the same three sanitizers: **106 checks, zero failures**. The isolated CMake build `build-amr-runtime-mit` passes `test_amr`, `test_amr_mp4`, `test_avcodec`, and `test_amr_fallback` (4/4), with CLI and tests ON, libretro/SDL/fixture generation OFF, `NJ_ENABLE_FLOAT=0`, and `NJ_ENABLE_M3G=0`. Initial build setup attempts exposed the existing M3G/libm configuration requirement and an incorrect requested target name (`test_avcodecs` instead of `test_avcodec`); these were configuration corrections, not decoder failures.

Reproduce the independent sanitizer check from the repository root:

```sh
cc -std=c99 -O1 -g -Wall -Wextra \
  -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer \
  -Iinclude -Isrc/midp tests/private/test_amr_equations.c \
  src/midp/nj_amr_mp4.c -lm -o /tmp/test_amr_equations
/tmp/test_amr_equations
```

The `-lm` above belongs to the test's independent mathematical oracle. Runtime AMR continues to use its local transcendental implementations.
