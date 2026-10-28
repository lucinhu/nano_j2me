/*
 * nano_j2me - nj_config.h
 *
 * Compile-time configuration for the nano_j2me J2ME (CLDC 1.1 / MIDP 2.0) runtime.
 *
 * Everything here can be overridden from the build system with -DNJ_xxx=...
 * The defaults target a hosted desktop build; embedded ports (ESP32-S3, STM32)
 * are expected to override NJ_MAX_HEAP, NJ_ENABLE_FLOAT and the feature flags.
 *
 * The whole runtime is C99 and freestanding-friendly, but "freestanding" has a
 * precise meaning here: with NJ_USE_LIBC=0 the external symbols required are
 * exactly those declared in nj2me/nj_libc.h - the string and memory primitives,
 * a couple of ctype helpers, and the libm transcendentals java.lang.Math needs
 * (which NJ_ENABLE_FLOAT=0 removes) - plus the nj_port callbacks.  Nothing else:
 * allocation, time and file access all go through the port.
 *
 * This used to claim three functions and was wrong in both directions: half the
 * sources did not compile with NJ_USE_LIBC=0 at all, and the set is larger than
 * three.  tools/size_report.sh compiles every file in that mode, so the claim
 * is checked rather than asserted.
 */
#ifndef NJ_CONFIG_H
#define NJ_CONFIG_H

/* The C library surface, in both modes.  Included here so that no source file
 * has to know whether it is being built hosted or freestanding. */
#include "nj2me/nj_libc.h"

/* ------------------------------------------------------------------ */
/* Platform detection (only used to pick sane defaults)                */
/* ------------------------------------------------------------------ */
#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
#  define NJ_PLATFORM_HOSTED 1
#else
#  define NJ_PLATFORM_HOSTED 0
#endif

/* ------------------------------------------------------------------ */
/* Basic build switches                                                */
/* ------------------------------------------------------------------ */

/*
 * Alignment of a Java object in the arena, and therefore of every block a port
 * hands back from nj_port.alloc: 16, so that long and double fields are
 * naturally aligned.  Public because the port's allocator has to honour it.
 */
#ifndef NJ_OBJ_ALIGN
#  define NJ_OBJ_ALIGN 16u
#endif

/* Use <string.h>, <stdio.h>, <stdlib.h> where convenient. */
#ifndef NJ_USE_LIBC
#  define NJ_USE_LIBC 1
#endif

/* 64-bit arithmetic. CLDC 1.1 mandates long/double support: leave at 1 for a
 * compliant runtime.  Set to 0 only for a CLDC-1.0-only, footprint-first build
 * (long/double opcodes then raise a class-load error instead of misbehaving). */
#ifndef NJ_ENABLE_LONG
#  define NJ_ENABLE_LONG 1
#endif

/* Floating point (CLDC 1.1 mandatory).  Set 0 on FPU-less MCUs; float/double
 * natives are then stubbed and Float/Double classes are omitted. */
/*
 * How many log messages a single run may emit before they are counted instead.
 * Generous for a game, and it stops a runaway MIDlet from filling a serial
 * console or a filesystem - and from looking like a hang in the test suite.
 */
#ifndef NJ_LOG_LIMIT
#  define NJ_LOG_LIMIT 2000u
#endif

/* JSR 184 (Mobile 3D Graphics).  On by default: the classes must exist for a
 * MIDlet's Class.forName to work, and a build that turns this off simply does
 * not have them.  Nothing here is needed to run a 2D title. */
#ifndef NJ_ENABLE_M3G
#define NJ_ENABLE_M3G 1
#endif

/*
 * Mascot Capsule Micro3D v3 (com.mascotcapsule.micro3d.v3), the other 3D API of
 * that era, used by two titles in the collection.  0 removes every class in
 * that package.
 */
#ifndef NJ_ENABLE_MASCOT
#define NJ_ENABLE_MASCOT 1
#endif

#ifndef NJ_ENABLE_FLOAT
#  define NJ_ENABLE_FLOAT 1
#endif

/* Backwards-compat / debugging helpers: Disassembler, class dumps, trace. */
#ifndef NJ_ENABLE_DEBUG
#  define NJ_ENABLE_DEBUG 0
#endif

/* Exception message objects and stack traces. */
#ifndef NJ_ENABLE_EXCEPTION_MESSAGES
#  define NJ_ENABLE_EXCEPTION_MESSAGES 1
#endif

/* Pure-C class library features.  Turning one off makes the corresponding
 * MIDP API raise UnsupportedOperationException instead of linking. */
#ifndef NJ_ENABLE_LC_DUPLI
#  define NJ_ENABLE_LC_DUPLI 1
#endif
#ifndef NJ_ENABLE_GAME_PACKAGE   /* javax.microedition.lcdui.game */
#  define NJ_ENABLE_GAME_PACKAGE 1
#endif
#ifndef NJ_ENABLE_RMS           /* javax.microedition.rms */
#  define NJ_ENABLE_RMS 1
#endif
#ifndef NJ_ENABLE_IO            /* javax.microedition.io + java.io */
#  define NJ_ENABLE_IO 1
#endif
/* Named UTF-8/ASCII character streams.  One int of streaming state per
 * InputStreamReader/OutputStreamWriter; no buffers or VM-global storage.
 * Set to 0 for Latin-1 only, with no state field in either class. */
#ifndef NJ_ENABLE_STREAM_UTF8
#  define NJ_ENABLE_STREAM_UTF8 1
#endif
/* Optional streaming XML parser. Desktop CMake enables it; small standalone
 * ports opt in explicitly. Zero removes the SAX and parser classes. */
#ifndef NJ_ENABLE_SAX
#  define NJ_ENABLE_SAX 0
#endif
#ifndef NJ_ENABLE_MEDIA         /* javax.microedition.media (tone only) */
#  define NJ_ENABLE_MEDIA 0
#endif
/*
 * AMR narrowband speech, on by default: 182 assets across 22 titles in
 * tests/games/ are "#!AMR" - sixty-three of them not even named ".amr" - and a
 * sound loader that is handed a MediaException by createPlayer() either plays
 * on in silence or dereferences the null Player and throws: The Sims 3 does
 * the second, once per sound effect, for as long as it runs.
 *
 * What it costs is mostly nj_amr_tables.h, a hundred kilobytes of the
 * standard's codebooks, so a build with no voice clips in it can drop the lot
 * with -DNJ_ENABLE_AMR=0; createPlayer() then refuses AMR exactly as it did
 * before nj_amr.c existed.
 */
#ifndef NJ_ENABLE_AMR
#  define NJ_ENABLE_AMR 1
#endif
/*
 * The built-in MIDI renderer (src/midp/nj_midi.c), on by default.
 *
 * It is the answer for every port that has no synthesizer of its own, and the
 * fallback for the files a port that has one still refuses.  A port whose
 * platform plays MIDI itself does not need it, and -DNJ_ENABLE_MIDI=0 takes it
 * out: createPlayer() then asks the port, and a port that declines leaves the
 * Player silent the way it was before nj_midi.c existed.
 *
 * What that costs is MIDIControl.  A MIDlet that plays its sound effects by
 * sending single note events (shortMidiEvent) needs a synthesizer it can talk
 * to; a port that only forwards whole files to the platform's player cannot
 * take them, so such a Player falls back to this renderer.  A port whose synth
 * does accept events implements nj_port.midi_short_event/midi_long_event and
 * can drop the renderer as well.
 */
/*
 * MP3 and video decoders.
 *
 * Both are software decoders compiled in by default because a MIDP title that
 * carries MP3 music or a 3GP clip is common; both can be trimmed out with -D
 * when a product size matters more than that, and both are reached through
 * nj_avcodec.h's provider registry so a port can install a hardware decoder in
 * front of the software one (or remove it) without touching the runtime.
 */
/*
 * NJ_MP3_EXPERIMENTAL registers the Layer III decoder with the runtime.  It is
 * on by default: tests/test_mp3.c embeds four LAME fixtures (MPEG-1 and
 * MPEG-2.5, mono and stereo) and asserts the decoded PCM stays within 1 LSB of
 * ffmpeg's decode, which is what makes the provider fit to hand to
 * Manager.createPlayer().  Turn it off to save ~18 KB of text on a build that
 * never sees MP3.
 */
#ifndef NJ_MP3_EXPERIMENTAL
#define NJ_MP3_EXPERIMENTAL 1
#endif

#ifndef NJ_ENABLE_MP3
#  define NJ_ENABLE_MP3 1
#endif
#ifndef NJ_ENABLE_VIDEO
#  define NJ_ENABLE_VIDEO 1
#endif

/*
 * Finer trims, all on by default.  Each one is a real decision on a small
 * part, so each is its own macro rather than a single "minimal" switch:
 *
 *   NJ_MP3_LAYER3_ONLY        drop Layer I and II (nobody shipped them)
 *   NJ_MP3_ENABLE_ID3         skip/parse ID3v2 tags before/between frames
 *   NJ_MP3_ENABLE_SEEK        keep the table needed for Player.setMediaTime
 *   NJ_MP3_ENABLE_INTENSITY   intensity stereo, which only MPEG-1 needs
 *   NJ_ENABLE_H263            H.263 baseline decoder (follows NJ_ENABLE_VIDEO)
 *   NJ_ENABLE_M4V             MPEG-4 Part 2 decoder (follows NJ_ENABLE_VIDEO)
 *   NJ_VIDEO_H263_ONLY        legacy alias: H.263 on, MPEG-4 Part 2 off
 */
#ifndef NJ_MP3_LAYER3_ONLY
#  define NJ_MP3_LAYER3_ONLY 1
#endif
#ifndef NJ_MP3_ENABLE_ID3
#  define NJ_MP3_ENABLE_ID3 1
#endif
#ifndef NJ_MP3_ENABLE_SEEK
#  define NJ_MP3_ENABLE_SEEK 1
#endif
#ifndef NJ_MP3_ENABLE_INTENSITY
#  define NJ_MP3_ENABLE_INTENSITY 1
#endif
/* H.263 and MPEG-4 Part 2 share the one video switch above by default, but a
 * port that carries only one of them can cut the other.  NJ_VIDEO_H263_ONLY is
 * the historical name for "H.263 on, MPEG-4 off" and still works. */
#ifndef NJ_ENABLE_H263
#  define NJ_ENABLE_H263 NJ_ENABLE_VIDEO
#endif
#ifndef NJ_VIDEO_H263_ONLY
#  define NJ_VIDEO_H263_ONLY 0
#endif
#ifndef NJ_ENABLE_M4V
#  if NJ_VIDEO_H263_ONLY
#    define NJ_ENABLE_M4V 0
#  else
#    define NJ_ENABLE_M4V NJ_ENABLE_VIDEO
#  endif
#endif

#ifndef NJ_ENABLE_MIDI
#  define NJ_ENABLE_MIDI 1
#endif
/*
 * ============================================================================
 *  DECODERS: WHAT IS BUILT IN, AND WHAT A PORT CAN PUT IN FRONT OF IT
 * ============================================================================
 *
 * Every decoder below is optional, and every one of them has a replacement
 * path, so a port can either take the built-in software decoder, drop it and
 * hand the bytes to the platform, or do both with the platform winning:
 *
 *   format   built-in            replacement
 *   -------  ------------------  --------------------------------------------
 *   PNG      nj_png.c            nj_codec provider, priority PLATFORM
 *   JPEG     nj_jpeg.c           nj_codec provider
 *   GIF      nj_gif.c            nj_codec provider
 *   BMP      none                nj_codec provider only (Android: BitmapFactory)
 *   DEFLATE  nj_inflate.c        nj_port.inflate (Android: zlib)
 *   ZIP/JAR  nj_zip.c            the container itself is always the runtime's;
 *                                only DEFLATE is replaceable
 *   WAV      nj_lib_media.c      nj_port.audio_decode
 *   AMR      nj_amr.c            nj_port.audio_decode
 *   MP3/AAC  none                nj_port.audio_decode (Android: MediaCodec)
 *   MIDI     nj_midi.c           nj_port.midi_open (Android: Soniox EAS)
 *
 * Cutting a decoder never turns into a silent wrong answer: the image codecs
 * register an "unavailable" provider that names the format in the exception,
 * and the audio paths either ask the port or refuse by name.  A build with
 * every decoder off still loads a JAR (stored entries need no DEFLATE) and
 * still runs a MIDlet; it just cannot show or play the media inside it.
 */
#ifndef NJ_ENABLE_PNG
#  define NJ_ENABLE_PNG 1
#endif
#ifndef NJ_ENABLE_JPEG
#  define NJ_ENABLE_JPEG 0
#endif
/*
 * GIF, on by default: MIDP 1.0 required it, and a MIDlet that cannot decode
 * its own artwork tends to swallow the IOException and carry on painting a
 * null Image, which reads as a black screen rather than as an error.
 */
#ifndef NJ_ENABLE_GIF
#  define NJ_ENABLE_GIF 1
#endif
/*
 * The built-in DEFLATE decompressor (src/zip/nj_inflate.c), on by default.
 *
 * It reads every compressed entry of every JAR and every PNG's IDAT, so with
 * it off a port needs nj_port.inflate_raw (zip) and nj_port.inflate (PNG,
 * .m3g) of its own - a phone with zlib in ROM, an MCU with a miniz in its SDK.
 * A build with this off still opens a JAR whose entries are STORED (method 0)
 * and says a compressed entry is unreadable instead of guessing at it.
 *
 * The CRC-32 and Adler-32 helpers stay in the build either way: the zip reader
 * and the PNG decoder validate with them.
 */
#ifndef NJ_ENABLE_INFLATE
#  define NJ_ENABLE_INFLATE 1
#endif
/*
 * The built-in RIFF/WAVE reader (in src/midp/nj_lib_media.c), on by default.
 *
 * PCM WAV is the one sampled format this runtime reads without help, and 42
 * corpus titles ship one.  With this off createPlayer() hands the file to
 * nj_port.audio_decode first (as it already does for AMR, MP3 and anything
 * else) and otherwise refuses it by name, which is what a port that decodes
 * WAV through MediaCodec or libavcodec wants.
 */
#ifndef NJ_ENABLE_WAV
#  define NJ_ENABLE_WAV 1
#endif
/*
 * Built-in CJK glyph provider (src/gfx/nj_font_cjk.h, generated by
 * tools/gen_cjk_font.py).  On by default: the fixed cells cover ASCII and
 * Latin-1 only, so with this off a Chinese MIDlet's text renders as blank
 * cells - which reads as a MIDlet that drew nothing.  Costs about 553 KiB of
 * const data; embedded builds that cannot spare it can set this to 0 and
 * supply their own glyph_lookup provider instead.
 */
#ifndef NJ_ENABLE_FONT_CJK
#  define NJ_ENABLE_FONT_CJK 1
#endif
/* Four-level CJK and Latin coverage (packed 2bpp rows) adds about 434 KiB
 * of const data, no heap use. Desktop CMake enables it; embedded users opt
 * in explicitly. When on, both src/gfx/nj_font_cjk_aa.h (tools/
 * gen_cjk_font.py --bpp 2) and src/gfx/nj_font_data_aa.h (tools/
 * gen_fonts.py --bpp 2) replace their monochrome tables; cell metrics and
 * line layout are identical either way. */
#ifndef NJ_ENABLE_FONT_CJK_AA
#  define NJ_ENABLE_FONT_CJK_AA 0
#endif
/*
 * TrueType text (src/gfx/nj_ttf.c + nj_font_ttf.c): render through a real font
 * file instead of the generated bitmap faces.
 *
 * Two different questions, so two switches.  NJ_ENABLE_TTF=0 removes the
 * rasteriser from the build entirely - that is the embedded build: no parser,
 * no font file to store, no glyph cache in RAM, and the 33 KB of bitmap tables
 * stay because they are what draws the text.  NJ_ENABLE_TTF=1 only makes the
 * capability *available*; the face is still chosen at runtime, so a build that
 * has it can still run with the built-in font.
 *
 * The bitmap faces are never removed by this switch.  They are the fallback
 * for any character a supplied face has no glyph for, and a font that silently
 * renders nothing for a missing glyph reads as a blank screen rather than as a
 * missing font.
 */
#ifndef NJ_ENABLE_TTF
#  define NJ_ENABLE_TTF 1
#endif

/* ------------------------------------------------------------------ */
/* Sizing                                                              */
/* ------------------------------------------------------------------ */

/* Java heap size in bytes.  Overridden by the port at runtime when the port
 * supplies its own heap block (nj_port.heap_block).
 *
 * The desktop default is 16 MB because MIDlet suites of that size are ordinary
 * in the corpus this port is measured against.  At 8 MB 愤怒的小鸟-高清版_2353
 * raises OutOfMemoryError inside luma.k.c while it loads its level tables, and
 * the game then divides by a rate that never got its value - an
 * ArithmeticException on screen that looks like a runtime bug and is not.  The
 * heap is mmap'd lazily and only the live set is touched, so the larger
 * reservation costs address space, not memory; ports that know better
 * (ESP32-S3, Android) pin NJ_MAX_HEAP themselves. */
#ifndef NJ_MAX_HEAP
#  define NJ_MAX_HEAP (16u * 1024u * 1024u)
#endif

/* Maximum java.lang.Thread stack depth (number of frames), and the operand +
 * local slot budget of one thread, in nj_value units (8 bytes each). */
#ifndef NJ_MAX_FRAMES
#  define NJ_MAX_FRAMES 256
#endif
#ifndef NJ_MAX_STACK_SLOTS
#  define NJ_MAX_STACK_SLOTS 8192
#endif

/* Interned system strings + constant string cache. */
#ifndef NJ_MAX_INTERNED
#  define NJ_MAX_INTERNED 512
#endif

/* Maximum number of simultaneously resident classes. */
#ifndef NJ_MAX_CLASSES
#  define NJ_MAX_CLASSES 512
#endif

/* Maximum number of static fields across all classes (single flat block). */
#ifndef NJ_MAX_STATICS
#  define NJ_MAX_STATICS 4096
#endif

/* Maximum number of registered native methods. */
#ifndef NJ_MAX_NATIVES
#  define NJ_MAX_NATIVES 1024
#endif

/* Native call recursion guard. */
#ifndef NJ_MAX_NATIVE_DEPTH
#  define NJ_MAX_NATIVE_DEPTH 64
#endif

/* Open files / record stores. */
#ifndef NJ_MAX_RECORD_STORES
#  define NJ_MAX_RECORD_STORES 8
#endif

/* Cooperative thread scheduler.
 *
 * Was 16, which a real title reached: 生化危机-险境求生_3290 reports
 * "OutOfMemoryError: too many threads" at exactly that count.  A port that
 * cannot afford 32 stacks can still set this lower - the ceiling is honest
 * either way, and what it must not do is be low enough to refuse a game that
 * a phone from the same era ran.
 */
#ifndef NJ_MAX_THREADS
#  define NJ_MAX_THREADS 32
#endif

/* JAR entries held in the loaded app (per-MIDlet-class count). */
#ifndef NJ_MAX_APP_ENTRIES
#  define NJ_MAX_APP_ENTRIES 512
#endif

/* Maximum size of one decompressed class file. */
#ifndef NJ_MAX_CLASS_BYTES
#  define NJ_MAX_CLASS_BYTES (192u * 1024u)
#endif

/* Screen geometry used when the port does not override it. */
#ifndef NJ_SCREEN_WIDTH
#  define NJ_SCREEN_WIDTH 240
#endif
#ifndef NJ_SCREEN_HEIGHT
#  define NJ_SCREEN_HEIGHT 320
#endif

/* ------------------------------------------------------------------ */
/* Feature knobs                                                       */
/* ------------------------------------------------------------------ */

/* Cooperative scheduling quantum: java bytecodes executed between scheduler
 * polls.  Smaller = smoother threading, larger = faster. */
#ifndef NJ_SCHED_QUANTUM
#  define NJ_SCHED_QUANTUM 20000
#endif

/*
 * JSR 256 (javax.microedition.sensor) shape limits.
 *
 * Both are compile-time because they size arrays inside nj_sensor_desc, which
 * a port fills in: a sensor with more channels than this cannot be described,
 * and a device with more sensors than this reports the first N.  Four channels
 * covers every sensor a phone has (the widest is a three-axis accelerometer,
 * plus a scalar for a rotation vector's accuracy); sixteen sensors covers the
 * lot with room to spare, and costs about 2.5 KB of descriptors on the stack
 * of the call that asks.
 */
#ifndef NJ_SENSOR_MAX_CHANNELS
#  define NJ_SENSOR_MAX_CHANNELS 4
#endif
#ifndef NJ_SENSOR_MAX_DESCRIPTORS
#  define NJ_SENSOR_MAX_DESCRIPTORS 16
#endif
/*
 * How long a sensor url may be.  The url is the port's own spelling - JSR 256
 * only says it identifies the sensor - and in practice it carries the model
 * name, which is why this is bigger than a "sensor:acceleration" would need:
 * a handset model name is long, and a URL that does not fit is a connection
 * that cannot be opened.
 */
#ifndef NJ_SENSOR_MAX_URL
#  define NJ_SENSOR_MAX_URL 128
#endif

/* Pixel format produced by the internal rasterizer. */
#define NJ_PIXFMT_RGB565 0
#define NJ_PIXFMT_RGB888 1
#define NJ_PIXFMT_ARGB8888 2
#ifndef NJ_PIXFMT
#  define NJ_PIXFMT NJ_PIXFMT_RGB565
#endif

/*
 * Threaded (computed-goto) interpreter dispatch.
 *
 * GCC and Clang both support taking the address of a label; nothing else
 * guaranteed to build this project does.  When it is 0 the interpreter uses
 * the portable switch and loses speed, not correctness - see the note in
 * nj_interp.c.
 *
 * DEFAULT OFF, and explicitly so.  Every number in docs/PERFORMANCE.md and
 * every ctest run has been measured on the switch interpreter; the threaded
 * body is a second implementation of the same 256 opcodes and needs to prove
 * itself against that baseline before it becomes the default (see
 * tools/check_threaded_dispatch.sh, which exercises it on every `ctest`).
 *
 * This block used to sit INSIDE the `#ifndef NJ_PIXFMT` guard above, so any
 * build that did not happen to define a pixel format (a hand-compile, an
 * early Android script) defaulted it to 1 while CMake's builds got 0.  The two
 * interpreters then disagreed about which one was tested, and the threaded one
 * was broken: a converted handler's DISPATCH() already fetches the next
 * opcode, but the table sent unconverted opcodes to L_slow, which fetched
 * again.  tests/fixtures/keysdemo.jar turned the resulting off-by-one byte
 * into "receiver is not an object reference".  Keep the define independent of
 * NJ_PIXFMT, and keep the default 0.
 */
#ifndef NJ_THREADED_DISPATCH
#  define NJ_THREADED_DISPATCH 0
#endif

/*
 * Interpreter fast paths.
 *
 * All of these trade FLASH/CODE SIZE for dispatch work, never RAM: they fuse,
 * fold or skip bytecode that the interpreter would otherwise dispatch again.
 * The fused forms are recognised at run time from the bytecode stream and the
 * instruction is still executed exactly as the JVM spec says - the code array
 * is never rewritten, so a jar behaves identically with every switch below
 * off.  A port with a tight flash budget can turn them off one by one.
 *
 *   NJ_ENABLE_SUPERINSTR       run the C-level semantics of a fused bytecode
 *                              pair directly instead of dispatching its second
 *                              opcode (e.g. `push const; iand`).  Saves one
 *                              dispatch and one instruction-count bump.
 *   NJ_ENABLE_JUMP_THREADING   when control flow lands on a goto, follow the
 *                              chain to its final target without going back
 *                              through the dispatch table.  J2ME compilers emit
 *                              long if/goto chains; this flattens them.
 *   NJ_ENABLE_ENGINE           build the pluggable execution-engine interface
 *                              (see docs/JIT.md).  This is the only switch here
 *                              that can add RAM: one pointer in nj_vm, plus
 *                              whatever an installed engine allocates.  A
 *                              default build has neither.
 */
#ifndef NJ_ENABLE_SUPERINSTR
#  define NJ_ENABLE_SUPERINSTR 1
#endif
#ifndef NJ_ENABLE_JUMP_THREADING
#  define NJ_ENABLE_JUMP_THREADING 1
#endif
/* Baseline JIT is opt-in. An enabled JIT also needs the engine seam. */
#ifndef NJ_ENABLE_JIT
#  define NJ_ENABLE_JIT 0
#endif
#ifndef NJ_ENABLE_ENGINE
#  define NJ_ENABLE_ENGINE NJ_ENABLE_JIT
#endif
#if NJ_ENABLE_JIT && !NJ_ENABLE_ENGINE
#  error "NJ_ENABLE_JIT requires NJ_ENABLE_ENGINE=1"
#endif

/* These switches constrain GENERATED instructions, not the C compiler's ISA.
 * Defaults follow -march. Explicit overrides must match the actual CPU. A is
 * exposed for future atomic lowering; the cooperative VM needs no AMOs today.
 * F/D=0 keeps Java floating point in the interpreter, not disabled in Java.
 * D requires F; C=0 emits only 32-bit instructions. See docs/JIT.md. */
#ifndef NJ_JIT_RV32_M
#  if defined(__riscv_mul) && defined(__riscv_div)
#    define NJ_JIT_RV32_M 1
#  else
#    define NJ_JIT_RV32_M 0
#  endif
#endif
#ifndef NJ_JIT_RV32_A
#  if defined(__riscv_atomic)
#    define NJ_JIT_RV32_A 1
#  else
#    define NJ_JIT_RV32_A 0
#  endif
#endif
#ifndef NJ_JIT_RV32_F
#  if defined(__riscv_flen) && __riscv_flen >= 32
#    define NJ_JIT_RV32_F 1
#  else
#    define NJ_JIT_RV32_F 0
#  endif
#endif
#ifndef NJ_JIT_RV32_D
#  if NJ_JIT_RV32_F && defined(__riscv_flen) && __riscv_flen >= 64
#    define NJ_JIT_RV32_D 1
#  else
#    define NJ_JIT_RV32_D 0
#  endif
#endif
#ifndef NJ_JIT_RV32_C
#  if defined(__riscv_compressed)
#    define NJ_JIT_RV32_C 1
#  else
#    define NJ_JIT_RV32_C 0
#  endif
#endif
#if (NJ_ENABLE_JIT != 0 && NJ_ENABLE_JIT != 1) || \
    (NJ_JIT_RV32_M != 0 && NJ_JIT_RV32_M != 1) || \
    (NJ_JIT_RV32_A != 0 && NJ_JIT_RV32_A != 1) || \
    (NJ_JIT_RV32_F != 0 && NJ_JIT_RV32_F != 1) || \
    (NJ_JIT_RV32_D != 0 && NJ_JIT_RV32_D != 1) || \
    (NJ_JIT_RV32_C != 0 && NJ_JIT_RV32_C != 1)
#  error "JIT feature macros must be 0 or 1"
#endif
#if NJ_JIT_RV32_D && !NJ_JIT_RV32_F
#  error "NJ_JIT_RV32_D requires NJ_JIT_RV32_F=1"
#endif

/* AArch64 scalar FP is separate from Advanced SIMD. The current backend uses
 * scalar float/double only; SIMD and LSE are reserved capability switches and
 * do not enable vector/atomic lowering. Turning FP off preserves Java FP via
 * the interpreter. As with RV32, these macros constrain JIT output only. */
#ifndef NJ_JIT_ARM64_FP
#  if defined(__aarch64__) && defined(__ARM_FP) && (__ARM_FP & 12) == 12
#    define NJ_JIT_ARM64_FP 1
#  else
#    define NJ_JIT_ARM64_FP 0
#  endif
#endif
#ifndef NJ_JIT_ARM64_SIMD
#  if defined(__aarch64__) && defined(__ARM_NEON)
#    define NJ_JIT_ARM64_SIMD 1
#  else
#    define NJ_JIT_ARM64_SIMD 0
#  endif
#endif
#ifndef NJ_JIT_ARM64_LSE
#  if defined(__aarch64__) && defined(__ARM_FEATURE_ATOMICS)
#    define NJ_JIT_ARM64_LSE 1
#  else
#    define NJ_JIT_ARM64_LSE 0
#  endif
#endif
#if (NJ_JIT_ARM64_FP != 0 && NJ_JIT_ARM64_FP != 1) || \
    (NJ_JIT_ARM64_SIMD != 0 && NJ_JIT_ARM64_SIMD != 1) || \
    (NJ_JIT_ARM64_LSE != 0 && NJ_JIT_ARM64_LSE != 1)
#  error "JIT feature macros must be 0 or 1"
#endif

/* Bounded per-VM caches. Heat, refusals and compiled slots live in per-method
 * probe maps, so they survive code-cache eviction without a hash lookup. */
#ifndef NJ_JIT_CACHE_ENTRIES
#  define NJ_JIT_CACHE_ENTRIES 64
#endif
#ifndef NJ_JIT_CACHE_WAYS
#  define NJ_JIT_CACHE_WAYS 2
#endif
/* Probe maps: a fixed arena holds one 16-bit state word per bytecode offset of
 * the methods that are currently interesting, plus a bounded method table.
 * `NJ_JIT_METHOD_ENTRIES` caps concurrently mapped methods; the arena is
 * refilled by evicting the least recently used method (metadata only, compiled
 * code stays in the code cache). 4096 B of arena maps 2048 bytecode offsets,
 * which covers ~20-30 typical game methods at once. */
#ifndef NJ_JIT_METHOD_ENTRIES
#  define NJ_JIT_METHOD_ENTRIES 32
#endif
#ifndef NJ_JIT_METHOD_ARENA_BYTES
#  define NJ_JIT_METHOD_ARENA_BYTES 4096
#endif
/* A block may return to the interpreter at up to this many guarded
 * instructions (null receiver, bad array, zero divisor). Field- and
 * array-heavy loops need enough slots that the whole loop body still fits in
 * one block; each slot costs a four-byte fault record in every cache entry. */
#ifndef NJ_JIT_MAX_FAULTS
#  define NJ_JIT_MAX_FAULTS 12
#endif
/* A block leaves native code through a small set of exits: the end of the
 * straight-line window, taken forward branches that leave the window and the
 * quantum-budget check of a loop. Four exits cover the common loop shape; the
 * block is refined (or split) when it needs more. */
#ifndef NJ_JIT_MAX_EXITS
#  define NJ_JIT_MAX_EXITS 4
#endif
#ifndef NJ_JIT_MIN_BLOCK_INSNS
#  define NJ_JIT_MIN_BLOCK_INSNS 4
#endif
/* Hot counters use 32-bit pending batches, folded at execution boundaries.
 * STATS=0 removes all per-probe/per-execution statistics; slow compilation and
 * memory accounting remain available. Detailed counts are opt-in. */
#ifndef NJ_JIT_STATS
#  define NJ_JIT_STATS 1
#endif
#ifndef NJ_JIT_DETAILED_STATS
#  define NJ_JIT_DETAILED_STATS 0
#endif
#ifndef NJ_JIT_RV32_REG_CACHE
#  define NJ_JIT_RV32_REG_CACHE 1
#endif
#if (NJ_JIT_STATS != 0 && NJ_JIT_STATS != 1) || \
    (NJ_JIT_DETAILED_STATS != 0 && NJ_JIT_DETAILED_STATS != 1) || \
    (NJ_JIT_RV32_REG_CACHE != 0 && NJ_JIT_RV32_REG_CACHE != 1)
#  error "JIT feature macros must be 0 or 1"
#endif
#if NJ_JIT_DETAILED_STATS && !NJ_JIT_STATS
#  error "NJ_JIT_DETAILED_STATS requires NJ_JIT_STATS=1"
#endif
#ifndef NJ_JIT_HOT_THRESHOLD
#  define NJ_JIT_HOT_THRESHOLD 8
#endif
#ifndef NJ_JIT_MAX_BLOCK_INSNS
#  define NJ_JIT_MAX_BLOCK_INSNS 64
#endif
#ifndef NJ_JIT_MAX_CODE_BYTES
#  define NJ_JIT_MAX_CODE_BYTES 2048
#endif
#if NJ_JIT_CACHE_ENTRIES < 1 || NJ_JIT_CACHE_ENTRIES > 1024 || \
    (NJ_JIT_CACHE_WAYS != 1 && NJ_JIT_CACHE_WAYS != 2 && NJ_JIT_CACHE_WAYS != 4) || \
    NJ_JIT_CACHE_ENTRIES % NJ_JIT_CACHE_WAYS != 0 || \
    NJ_JIT_METHOD_ENTRIES < 1 || NJ_JIT_METHOD_ENTRIES > 1024 || \
    NJ_JIT_METHOD_ARENA_BYTES < 256 || NJ_JIT_METHOD_ARENA_BYTES > 1048576 || \
    NJ_JIT_METHOD_ARENA_BYTES % 2 != 0 || \
    NJ_JIT_MAX_FAULTS < 1 || NJ_JIT_MAX_FAULTS > 16 || \
    NJ_JIT_MAX_EXITS < 1 || NJ_JIT_MAX_EXITS > 8 || \
    NJ_JIT_HOT_THRESHOLD < 1 || NJ_JIT_HOT_THRESHOLD > 16383 || \
    NJ_JIT_MAX_BLOCK_INSNS < 1 || NJ_JIT_MAX_BLOCK_INSNS > 128 || \
    NJ_JIT_MIN_BLOCK_INSNS < 1 || NJ_JIT_MIN_BLOCK_INSNS > NJ_JIT_MAX_BLOCK_INSNS || \
    NJ_JIT_MAX_CODE_BYTES < 64 || NJ_JIT_MAX_CODE_BYTES > 16384
#  error "JIT cache/compilation limits are out of range"
#endif

/* Encoders can be tested on any host; execution requires a matching ABI. */
#if defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 32 && \
    !defined(__riscv_abi_rve) && \
    (!defined(__BYTE_ORDER__) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#  define NJ_JIT_HOST_RV32 1
#else
#  define NJ_JIT_HOST_RV32 0
#endif
/* LP64 flat pointers only. ARM64 ILP32 and pointer-authenticated arm64e ABIs
 * need their own entry conventions and currently stay interpreted. */
#if defined(__aarch64__) && defined(__LP64__) && !defined(__arm64e__) && \
    (!defined(__BYTE_ORDER__) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#  define NJ_JIT_HOST_ARM64 1
#else
#  define NJ_JIT_HOST_ARM64 0
#endif
#define NJ_JIT_HOST_SUPPORTED (NJ_JIT_HOST_RV32 || NJ_JIT_HOST_ARM64)

/* Mascot Capsule triangle rasteriser: write the attribute combinations that
 * dominate real scenes (textured and untextured, flat and smooth shaded,
 * replace and half blend) as separate span loops, so the per-pixel branch tree
 * is resolved once per row.  Worth about 14-28% of the rasteriser's own time
 * on the deterministic bench, at the cost of roughly 10 KB of code (no RAM,
 * no new state).  0 keeps the single generic loop, which draws the same
 * pixels. */
#ifndef NJ_MASCOT_RASTER_SPANS
#  define NJ_MASCOT_RASTER_SPANS 1
#endif

/* M3G common-case raster/clip paths. No persistent storage; native stack use
 * depends on the compiler (see docs/PERFORMANCE.md). Disable for the general
 * implementation when code size or native stack headroom is more important. */
#ifndef NJ_M3G_FAST_PATHS
#  define NJ_M3G_FAST_PATHS 1
#endif

/* Micro3D field access uses the receiver's existing linked class, avoiding a
 * class-name lookup for each matrix/vector component. No cache or persistent
 * RAM; compiler-dependent code size and native stack use can differ. Set 0
 * to keep the general lookup path on targets with tighter code/stack budgets.
 * Trigonometric accuracy and the public API are identical in either mode. */
#ifndef NJ_MASCOT_FAST_FIELDS
#  define NJ_MASCOT_FAST_FIELDS 1
#endif

/* Memoise the few field pointers that the hot Midp natives resolve on every
 * element of bulk I/O or every text line (ByteArray{Input,Output}Stream and
 * Font).  Costs 64 bytes per VM, no allocation and no writable globals; the
 * resolved pointers are identical to what nj_field_lookup() returns.  Set 0
 * to restore the per-call name lookup (the pre-cache behaviour). */
#ifndef NJ_ENABLE_HOT_FIELDS
#  define NJ_ENABLE_HOT_FIELDS 1
#endif

/* Deferred Micro3D rendering grows its queue on demand. This caps queued
 * primitives per Graphics3D (64 bytes/primitive, plus a 4-byte sort index;
 * allocated buffers round up to powers of two). Lower the cap for small heaps;
 * exhaustion throws OutOfMemoryError instead of silently dropping geometry.
 * No queue is allocated until rendering is requested. */
#ifndef NJ_MASCOT_MAX_QUEUED_TRIANGLES
#  define NJ_MASCOT_MAX_QUEUED_TRIANGLES 16384u
#endif

/* Assertions (always compiled out unless explicitly enabled). */
#ifndef NJ_ASSERT_ENABLED
#  define NJ_ASSERT_ENABLED 0
#endif

/* ------------------------------------------------------------------ */
/* Derived                                                             */
/* ------------------------------------------------------------------ */
#if NJ_ENABLE_FLOAT
#  define NJ_HAS_FLOAT 1
#else
#  define NJ_HAS_FLOAT 0
#endif

#if NJ_ENABLE_LONG
#  define NJ_HAS_LONG 1
#else
#  define NJ_HAS_LONG 0
#endif

#endif /* NJ_CONFIG_H */
