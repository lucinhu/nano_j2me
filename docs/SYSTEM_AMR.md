# System AMR decoding and built-in fallback

AMR-NB playback tries the host service first and retains the built-in decoder as a complete fallback. This policy applies to raw `#!AMR` clips, audio-only 3GP/MP4 `samr` tracks, and AMR movie audio. It does not require a system codec to be installed.

The runtime implementations are MIT. The built-in decoder's numeric data have separate Apache-2.0 attribution; bundled fallback fonts remain under OFL-1.1. See [AMR data licenses](AMR_DATA_LICENSES.md), [codec provenance](CODEC_PROVENANCE.md), [runtime review](notes/2026-11-12-amr-independent-implementation.md), and [font licenses](FONT_LICENSES.md). Using a system decoder does not remove the notices required for fallback data that remain in the binary.

## Selection and complete-output checks

`nj_avcodec_decode_amr()` in `include/nj2me/nj_avcodec.h` uses this order:

1. The port's existing `audio_decode` hook, when present.
2. Installed AMR providers, newest first. The automatic native provider is registered after the software provider; a port can install its own provider afterward.
3. Direct built-in decoding from the original compressed input.

Both passes of the port hook must report the expected size, 8 kHz and one channel. Registered providers may return PCM in multiple chunks. The helper validates metadata, consumed-input/output bounds, sample counts, progress and total output. It closes each provider before trying another. Errors, stalls, early end or short output discard that attempt and restart from the original compressed input. Input and output buffers must not overlap; overlap and insufficient capacity are rejected before callbacks.

Every accepted 20 ms AMR block must produce 160 signed PCM16LE samples, or 320 bytes. The accepted block count comes from the bounded raw-frame walker. Its established handling of incomplete trailing raw frames and reserved types remains unchanged; this is not a stricter raw-file conformance claim. Container samples are validated individually, including their declared frames-per-sample, before compressed packets are gathered for the same helper. Malformed or truncated `samr` samples are refused instead of accepting a partially decoded track.

The built-in path retains all eight narrowband speech modes, 8 kHz mono output and the 15,000-block limit. System services can produce different PCM from the floating-point built-in decoder. The fallback always starts with fresh decoder state, so its output does not contain a prefix from a failed service.

## Comfort noise (SID) and discontinuous transmission

The built-in decoder now synthesises comfort noise instead of publishing silence for `SID_FIRST`, `SID_UPDATE`, `SID_BAD` and the `NO_DATA` frames that follow them. The receiving state is selected from the frame quality/type bits first: only `SPEECH_GOOD` enters the ordinary speech unpack path, the SID types enter the comfort-noise path, and `SPEECH_BAD`/`NO_DATA` inside comfort noise continue the same model without parsing a payload. `SID_FIRST`/`SID_BAD` never overwrite a running model; a new `SPEECH_GOOD` leaves it.

`SID_UPDATE` carries 3+8+9+9+6 codec bits: the 3-bit mode indication, three LSF index sets from the existing MR740 codebooks, 80 reference residual coefficients and a 6-bit logarithmic RMS. The synthesis uses hangover LSF/log-energy history, LSP and log-energy interpolation, an eight-way mean-removed LSF variation, a 31-bit PN state, ten ±1 pulses per 40 samples, an all-pole LP filter with gain normalisation, a shared high-pass filter and cleanup of pitch/adaptive memory before returning to speech. Missing SID frames decay the log RMS gradually after 50 frames. All state is on the caller stack: no allocation, no writable globals and no external libm.

The 80 residual coefficients come from the pinned OpenCORE commit `3b67218fb8efb776bcd79e7445774e02d778321d` (`past_rq_init` in `q_plsf_3_tbl.cpp`, input SHA-256 `3ab12e9a16e13952f642b09755c9979569899b5bb0e0fb2596c2fd8bc4611d0a`), reshaped `[8][10]` without numeric transformation and covered by the existing Apache-2.0/PacketVideo notices. The original 44 generated codebook definitions and their typed fingerprints are unchanged.

This is deliberately **not** a bit-exact TS 26.073/26.104 decoder and no conformance suite is claimed. Measured against an external OpenCORE fixed-point encoder oracle: pure-SID level RMS 343.80 vs 343.14 (31.03 dB), and real background comfort noise over 8 modes × 320 frames gives SNR 8.45–12.10 dB with RMS about 2–5 % high. Known gaps: full LSF variability weighting/limiting, hangover eligibility timing, DTX→speech predictor/postfilter transition, SID mode indication (a non-MR122 stream that starts on SID may miss mode-level correction), low-level fixed-point rounding, and speech-mode erasure handling, which remains explicit silence plus excitation reset rather than full parameter-substitution PLC.

## Corrupt and adversarial frames

A long run of legal framing with all-`0xFF` MR67 payloads previously drove the excitation into a non-finite range: `binary32` has no fractional bits above 2^23, the truncation helper returned the saturated value, and the resulting NaN/overflow reached the PCM conversion. Excitation rounding now checks finiteness and the ±2^23 magnitude bound; a frame that trips it is published as silence and the decoder state is re-initialised so later valid frames recover. This is malicious-numeric-state recovery, not a speech-quality or PLC claim. The fix was exercised with 480,000 adversarial frames (8 modes × 4 patterns × 15,000) under ASan, UBSan and float-cast-overflow, and 120,000 mixed good/bad/SID/no-data frames with output canaries and state bounds.

## Platform services

| Host | Native bridge | Availability and verification |
| --- | --- | --- |
| Android API 21+ | NDK MediaCodec, MIME `audio/3gpp`, loaded from `libmediandk.so` | Requires an existing application Java VM. An actual API 21 x86_64 app passed all eight speech modes plus SID. ARM64 and ARMv7 builds were compiled; their native execution was not tested here. |
| Apple | AudioToolbox `AudioConverter`, loaded at runtime | Actual converter creation and full decoding determine availability. Host protocol mocks and ARM64/x86_64 Mach-O compilation passed; native Apple decoding remains unverified. |
| Windows | Media Foundation AMR-NB transforms, dynamically enumerated and configured | The AMR-NB format identifier is documented from Windows 8.1. Installation and stream support are checked at open time. MinGW x64/i686 SDK mocks passed under Wine; Wine's real service rejected/unavailable path returned cleanly. Native Windows capability and accuracy remain unverified. |
| Linux and other hosts | No automatic native bridge | Built-in decoding, or a provider explicitly installed by the port. |

A non-NULL `nj_avcodec_system_amr()` table means a bridge is compiled, not that a system decoder is available. Its `open()` can return NULL. No system codec implementation is bundled or linked into the runtime.

Android queries `JNI_GetCreatedJavaVMs` from the existing process before allocating or calling MediaCodec. It does not load/create a Java VM or attach a thread. A VM-less `adb shell` executable returns immediately for software fallback. This avoids the observed API 21 shell-service initialization hang while retaining the tested app path. The native bridge requires `libdl`, with no mandatory `libmediandk` import or production Java bridge. The pre-existing Android port's Java `audio_decode` hook remains first in the selection order.

Apple loads AudioToolbox at open time. Windows loads MF/COM DLLs from the system directory and adds no mandatory MF/COM imports. Each open owns its codec, service references and temporary buffers; successful predecode retains only caller-allocated PCM. Failure paths release resources before fallback. Iteration, allocation and progress limits bound work around native calls. A synchronous operating-system call cannot be forcibly interrupted by those checks; the Windows elapsed-time limit is checked between calls.

## Build controls

CMake accepts:

```sh
cmake -S . -B build -DNJ_ENABLE_SYSTEM_AMR=AUTO
```

`AUTO` uses the header default: hosted non-legacy Android, Apple and Windows register their native bridge. Other and freestanding targets do not. Use `-DNJ_ENABLE_SYSTEM_AMR=0` to disable automatic native registration or `1` to enable its compiled bridge explicitly. Manual builds can define the same macro numerically. Explicit port hooks and custom installed providers remain usable with automatic registration disabled.

`NJ_ENABLE_AMR=0` removes AMR support, including these media routes and the built-in decoder. It is not a system-only AMR configuration. Default builds preserve the built-in fallback and its separately licensed data.

## Playback and snapshots

AMR clips are fully decoded into VM-owned heap PCM before playback. Seeking, stop/resume and PCM backpressure use the existing Player interface. Native codec/service objects are closed after predecode and are never serialized.

Snapshots retain the decoded PCM, playback time and resume position. Restore clears native handles and device queues. A paused PCM Player keeps its saved cursor; a playing one resumes from the recorded audible media time rather than its discarded queue frontier. Actual AMR regressions save both cases at 40 ms, restore into a fresh VM with all codecs removed, and resume from sample 320 without reopening a decoder. A five-block clip retains all 1,600 PCM bytes and emits the remaining 960 bytes. A subsequent 20 ms seek/resume emits the expected 1,280 bytes.

This does not add serialization of other native encoded media, rewind/runahead determinism or cross-platform state compatibility. The core's existing incomplete/platform-dependent serialization declarations remain.

## Verification and reproduction

The combined local candidate checks completed on 2026-11-13:

| Check | Result |
| --- | --- |
| Full CMake suite, run serially to avoid shared Java-fixture generation races | 182/182 passed, no skips; 317.48 seconds |
| ASan/UBSan focused AMR, provider, snapshot and font tests | 9/9 passed, with leak and halt-on-error checks |
| Final libretro mock frontend matrix | 11/11 passed, no skips |
| Instrumented libretro font matrix | All seven cases passed, including font-aware state rejection and visibly different TextDemo pixels |
| Actual RetroArch 1.22.2 | TextDemo with automatic external font selection, 120 frames, clean exit |
| Default-feature core builds | All eight Linux, Android and Windows targets passed; all 25 ABI exports present before and after stripping |
| Final Windows Wine checks | Usual suites 4/4 per architecture, 12 font cases, and nine actual registered AMR fallback modes per architecture passed |

The built-in rewrite also preserved every PCM byte in 1,195 comparison streams / 48,814 frames. The independently regenerated AMR header preserves all 44 typed definitions. These are bounded compatibility and provenance checks, not a formal conformance or legal certification.

The SID/comfort-noise and corrupt-frame work (2026-11-13) kept that parity narrower and explicit:

| Check | Result |
| --- | --- |
| Uninterrupted speech parity | 203 streams / 23,622 frames (8 encoded mode files, 191 raw assets, 4 ISO assets) keep PCM SHA-256 identical to the frozen pre-DTX baseline |
| Real speech reference | External FFmpeg float oracle: 8 modes/408 frames max 1 LSB (87.4 dB), 191 raw/16,524 frames max 30 LSB (84.4 dB), 4 ISO/6,690 frames max 4 LSB (89.7 dB) — not fixed-point conformance |
| Real DTX streams | OpenCORE encoder voiced→zero-background→voiced and deterministic-noise cases: SID/`NO_DATA` counts, durations, canaries and bounded levels; whole-clip vs fixed-point SNR ≈ 5.2–13.7 dB |
| Corrupt frames | 480,000 adversarial frames and 120,000 mixed DTX frames under ASan/UBSan/float-cast-overflow; no non-finite output |
| Strict builds | ISO C99 `-pedantic-errors`, `NJ_ENABLE_FLOAT=0 NJ_USE_LIBC=0 -ffreestanding`, and NDK r10e GCC 4.9 ARMv5/API5 soft-float builds all pass with no libm import |
| Public tests | `test_amr` 156 checks, plus `test_amr_fallback`, `test_amr_mp4` and `test_avcodec`; the independent equation test now checks max-SID non-zero output, the OpenCORE RMS 343.140019 tolerance band and explicit energy-0 silence |

Evidence for the SID work is in `.local-tools/debug-runs/gfx-full-20261113/amr/` (`REPORT.md`, `media-elst-review.md`, the `dtx-*` scripts and logs, `speech-parity.py`, `dtx-speech-parity.json`); those generated artifacts are not prerequisites for building the project. Movie audio with an independent edit list is covered by `test_movie_amr_timing` (19 cases, 753,210 checks), which maps the track edit list onto the predecoded PCM timeline.

The full-suite log is `.local-tools/debug-runs/libretro-ready-20261112/amr-final-ctest.log`; focused sanitizer, frontend, font and RetroArch logs use the same directory with the `amr-final-` prefix. Generated platform results, exact binary hashes, dependencies and source-cohort checks are in `build/libretro-final-report.md`, `build/libretro-final-artifacts.json` and `build/libretro-final-artifacts.sha256`. Native Android provider evidence is separate from the build-only Android libretro cores. Apple and native Windows codec success remain unverified; the Windows probes establish actual Wine service rejection and complete software retry.

The normal CMake suite includes `test_amr_fallback`, `test_save`, `test_amr_equations`, `test_system_amr_android_mock` and `test_system_amr_apple_mock`. Native-service mocks and equation tests explicitly retain assertions in Release. A Windows build also exposes `test_system_amr_windows_mock`.

```sh
cmake --build build --target test_amr_fallback test_save test_amr_equations \
  test_system_amr_android_mock test_system_amr_apple_mock
ctest --test-dir build --output-on-failure \
  -R '^(test_amr_fallback|test_save|test_amr_equations|test_system_amr_android_mock|test_system_amr_apple_mock)$'
```

The fallback tests cover multiple chunks, metadata errors, early EOF, no progress, allocation failures, partial output, port/provider priority, buffer overlap, strict container validation, complete software retry and playback lifecycle. The snapshot tests verify cursor behavior and that no codec or native resource reopens on load. The independent equation test includes malformed inputs and raw/container output-cap guards.

Android app sources and the exercised API 21 recipe are in [the JNI test app directory](../tests/private/system_amr_android_app/README.md). Local device results and source hashes are recorded in `build-system-amr/system-amr-report.md` and its JSON counterpart; these generated artifacts are not prerequisites for building the project.

The Windows standalone mock test can be cross-compiled and run under Wine:

```sh
x86_64-w64-mingw32-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -Itests \
  tests/test_system_amr_windows.c -o build-system-amr-windows/mock.exe
WINEDEBUG=-all wine build-system-amr-windows/mock.exe
```

Use `i686-w64-mingw32-gcc` for the 32-bit test. The test includes the private bridge directly; do not also link the common source. Add `-DNJ_WINDOWS_AMR_NATIVE_PROBE=1` to probe the actual installed service instead of protocol mocks. A clean unavailable result confirms fallback behavior, not native decode accuracy.
