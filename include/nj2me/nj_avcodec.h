/*
 * nano_j2me - nj_avcodec.h
 *
 * ============================================================================
 *  AUDIO / VIDEO DECODER PROVIDERS
 * ============================================================================
 *
 * This is the same idea as nj_codec.h, one level up: the runtime ships portable
 * C decoders for the formats MIDP titles actually carry, and a port replaces
 * any of them with the platform's own (hardware) decoder, or with nothing at
 * all when a product never plays that format.
 *
 * Two axes are deliberately separate:
 *
 *   TRIMMING      nj_config.h decides which software providers are compiled in
 *                 at all.  -DNJ_ENABLE_MP3=0 links no MP3 decoder; a title that
 *                 asks for one gets MediaException, which is what this runtime
 *                 already answers for a format it cannot play.  NJ_ENABLE_VIDEO
 *                 does the same for the video path.  Finer trims exist per
 *                 provider (see the NJ_MP3_* macros) because Layer III only,
 *                 mono only, or no intensity stereo are all defensible size
 *                 decisions on a 256 KB part.
 *
 *   ADAPTING      decoders are function tables, installed at runtime.  A port
 *                 that has a hardware decoder installs its provider before the
 *                 first Player is created and it wins for every matching
 *                 stream; the software provider stays linked but idle.  A port
 *                 with no interest in one direction installs a provider whose
 *                 open() returns NJ_AV_ERR_UNSUPPORTED and gets the same
 *                 MediaException the trimmed build gives.
 *
 * Providers are the *decoder*; containers are somebody else's problem.  The
 * caller (src/midp/nj_lib_media.c) sniffs the container, hands the provider the
 * elementary stream it can carry, and asks for PCM or for a frame.
 *
 * ============================================================================
 *  WHAT A PROVIDER MUST GUARANTEE
 * ============================================================================
 *   - `accepts()` is cheap and whole-buffer safe: it looks at a header, never
 *     scans far, never trusts a length field without bounding it.
 *   - `open()` either produces a decoder handle or returns an error code; on
 *     error it sets *err to a static string when it has something useful to
 *     say, and it must not leave allocations behind.
 *   - Decoding is streaming: `decode()` consumes input bytes and fills an
 *     output buffer the caller owns.  It reports how many input bytes it used
 *     and how many PCM frames / video frames it produced, so a caller can keep
 *     the buffers small - the Java heap never sees a whole song or clip.
 *   - All memory goes through the request's alloc/free hooks, never malloc, so
 *     teardown is complete and the heap can be PSRAM.
 *   - Untrusted input stays untrusted: every read is bounds-checked.  A decoder
 *     that can be made to write outside its output buffer is a remote code
 *     execution bug inside a game loader.
 *   - No mutable global state: a provider handle holds its own state.
 */
#ifndef NJ_AVCODEC_H
#define NJ_AVCODEC_H

#include "nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nj_vm;
typedef struct nj_vm nj_vm;


/* ------------------------------------------------------------------ */
/* Formats                                                             */
/* ------------------------------------------------------------------ */
#define NJ_AV_AUDIO_MP3       0x0101u   /* MPEG-1/2/2.5 Layer III          */
#define NJ_AV_AUDIO_AMR       0x0102u   /* AMR-NB, the existing decoder    */
#define NJ_AV_AUDIO_MIDI      0x0103u   /* the built-in synthesizer        */

#define NJ_AV_VIDEO_H263      0x0201u   /* H.263 baseline, as in 3GP       */
#define NJ_AV_VIDEO_MPEG4     0x0202u   /* MPEG-4 Part 2 Simple Profile    */

/* Output pixel layouts a video provider may produce. */
#define NJ_AV_PIX_RGB565      1
#define NJ_AV_PIX_ARGB8888    2

/* Return codes.  Anything else is a provider-specific negative number. */
#define NJ_AV_OK              0
#define NJ_AV_ERR_UNSUPPORTED (-1)      /* not this provider's format      */
#define NJ_AV_ERR_CORRUPT     (-2)      /* the stream is damaged           */
#define NJ_AV_ERR_MEMORY      (-3)      /* the alloc hook said no          */
#define NJ_AV_ERR_END         (-4)      /* no more output from this input  */
#define NJ_AV_ERR_AGAIN       (-5)      /* need more input before output   */


/* ------------------------------------------------------------------ */
/* Memory hooks                                                        */
/* ------------------------------------------------------------------ */
typedef void *(*nj_av_alloc_fn)(void *ud, nj_u4 bytes);
typedef void  (*nj_av_free_fn)(void *ud, void *ptr);

typedef struct nj_av_mem {
    void           *ud;
    nj_av_alloc_fn  alloc;
    nj_av_free_fn   free;
} nj_av_mem;

/* What a stream turned out to be.  Filled by open(). */
typedef struct nj_av_info {
    nj_u4 format;           /* NJ_AV_AUDIO_* / NJ_AV_VIDEO_*                  */
    nj_u4 rate;             /* audio: samples per second                      */
    nj_u4 channels;         /* audio: 1 or 2                                  */
    nj_u4 frames;           /* audio: PCM frames, 0 when unknown              */
    nj_u4 duration_ms;      /* 0 when unknown                                 */
    nj_u4 width;            /* video                                          */
    nj_u4 height;
    nj_u4 fps;              /* video: frames per second, 0 when unknown       */
    nj_u4 pixfmt;           /* video: NJ_AV_PIX_*                             */
    nj_u4 bitrate;          /* 0 when unknown                                 */
} nj_av_info;

/* One decode step.  The caller owns `out`. */
typedef struct nj_av_step {
    const nj_u1 *in;        /* input bytes                                    */
    nj_u4        in_len;    /* how many are available                         */
    nj_u4        in_used;   /* provider -> caller: bytes consumed             */
    nj_u1       *out;       /* output buffer                                  */
    nj_u4        out_cap;   /* bytes available in out                         */
    nj_u4        out_used;  /* provider -> caller: bytes written              */
    nj_u4        frames;    /* provider -> caller: PCM frames / video frames  */
    nj_s8        time_us;   /* provider -> caller: pts of this output, or -1  */
} nj_av_step;

/* Opaque decoder state; each provider defines what it points at. */
typedef struct nj_av_dec nj_av_dec;

/*
 * A provider.  `name` is static and used in logs and in the API audit.
 * Either table may be installed to override the built-in software decoder of
 * the same format; the first provider that accepts a stream wins.
 */
typedef struct nj_av_audio_ops {
    const char *name;
    nj_u4       format;     /* NJ_AV_AUDIO_*                                  */

    /* Cheap sniff.  Returns 1 when this provider can decode the stream. */
    int (*accepts)(const nj_u1 *data, nj_u4 len);

    /* Open a decoder for `data`.  `info` is filled on success. */
    nj_av_dec *(*open)(const nj_u1 *data, nj_u4 len, const nj_av_mem *mem,
                       nj_av_info *info, const char **err);

    /*
     * Decode more PCM.  Returns NJ_AV_OK with step->frames filled, NJ_AV_ERR_AGAIN
     * when the buffered input was not enough (feed more), or NJ_AV_ERR_END when
     * the stream is over.
     */
    int (*decode)(nj_av_dec *dec, nj_av_step *step);

    /* Seek to a stream position in microseconds; NULL means "no seeking". */
    int (*seek)(nj_av_dec *dec, nj_s8 time_us);

    /* Release the decoder and everything it allocated through `mem`. */
    void (*close)(nj_av_dec *dec);

    /* Size of one PCM frame in bytes (rate*channels*2 / rate), 0 if n/a. */
    nj_u4 pcm_frame_bytes;
} nj_av_audio_ops;

typedef struct nj_av_video_ops {
    const char *name;
    nj_u4       format;

    int (*accepts)(const nj_u1 *data, nj_u4 len);
    nj_av_dec *(*open)(const nj_u1 *data, nj_u4 len, const nj_av_mem *mem,
                       nj_av_info *info, const char **err);
    int (*decode)(nj_av_dec *dec, nj_av_step *step);
    void (*close)(nj_av_dec *dec);
} nj_av_video_ops;


/* ------------------------------------------------------------------ */
/* Registry                                                            */
/* ------------------------------------------------------------------ */
/*
 * Install a provider.  Providers installed later are tried first, so a port
 * calls these during startup, after nj_vm_create(), and wins over the software
 * defaults compiled into the build.  Passing NULL for a table removes the
 * matching software provider (a product that must not decode MP3 can drop it
 * at runtime as well as with -DNJ_ENABLE_MP3=0).
 *
 * Returns the number of providers of that kind now installed.
 */
/*
 * Install an audio provider.  Later installs are tried first, so a port's
 * hardware decoder wins over the built-in software one.  Returns the number of
 * installed providers after the call (0 when `vm` is NULL); passing NULL
 * removes all of them, which is what a port does when it tears down the codec
 * server it installed.
 */
int nj_avcodec_install_audio(struct nj_vm *vm, const nj_av_audio_ops *ops);
int nj_avcodec_install_video(struct nj_vm *vm, const nj_av_video_ops *ops);

/*
 * Look up a provider for a stream.  Returns NULL when nothing accepts it; the
 * caller then answers MediaException with the message it likes.
 */
const nj_av_audio_ops *nj_avcodec_find_audio(struct nj_vm *vm,
                                             const nj_u1 *data, nj_u4 len);
const nj_av_video_ops *nj_avcodec_find_video(struct nj_vm *vm,
                                             const nj_u1 *data, nj_u4 len);

/*
 * The software providers this build was compiled with, or NULL when trimmed
 * out.  They are installed by nj_avcodec_reset(), which nj_vm_create() calls;
 * a port that wants a different set can call reset and install again.
 */
const nj_av_audio_ops *nj_avcodec_sw_mp3(void);
const nj_av_audio_ops *nj_avcodec_sw_amr(void);
const nj_av_video_ops *nj_avcodec_sw_h263(void);

/* Drop every provider and reinstall the compiled-in software defaults. */
void nj_avcodec_reset(struct nj_vm *vm);

#ifdef __cplusplus
}
#endif

#endif /* NJ_AVCODEC_H */
