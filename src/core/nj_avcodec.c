/*
 * nano_j2me - nj_avcodec.c
 *
 * The audio/video decoder registry behind nj_avcodec.h.  Small on purpose: the
 * decoders are the interesting part, and this file only decides which of them
 * a stream is handed to.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_avcodec.h"

#include "../core/nj_internal.h"

#ifndef NJ_AVCODEC_MAX
#  define NJ_AVCODEC_MAX 4
#endif

/* The registry lives in the VM: the runtime has no mutable globals. */
#define AUDIO_OF(vm) ((vm)->av_audio)
#define VIDEO_OF(vm) ((vm)->av_video)

/* Defined by the providers, so a trimmed build still links. */
extern const nj_av_audio_ops nj_av_audio_mp3;
extern const nj_av_video_ops nj_av_video_h263;
#if NJ_ENABLE_AMR
/* nj_amr.c decodes a whole AMR file into PCM the caller owns. */
extern nj_u4 nj_amr_frames(const void *data, nj_u4 size);
extern nj_u4 nj_amr_decode(const void *data, nj_u4 size, nj_u1 *pcm);
#endif

int nj_avcodec_install_audio(struct nj_vm *vm, const nj_av_audio_ops *ops)
{
    nj_u4 n;

    if (vm == NULL) {
        return 0;
    }
    n = vm->av_audio_n;
    if (ops == NULL) {          /* "remove everything of this kind" */
        vm->av_audio_n = 0;
        return 0;
    }
    if (n >= NJ_AVCODEC_MAX) {
        return (int)n;
    }
    /* Later installs are tried first: a port's decoder wins. */
    {
        nj_u4 i;
        for (i = n; i > 0; i--) {
            vm->av_audio[i] = vm->av_audio[i - 1];
        }
    }
    vm->av_audio[0] = ops;
    vm->av_audio_n = n + 1u;
    return (int)(n + 1u);
}

int nj_avcodec_install_video(struct nj_vm *vm, const nj_av_video_ops *ops)
{
    nj_u4 n;

    if (vm == NULL) {
        return 0;
    }
    n = vm->av_video_n;
    if (ops == NULL) {
        vm->av_video_n = 0;
        return 0;
    }
    if (n >= NJ_AVCODEC_MAX) {
        return (int)n;
    }
    {
        nj_u4 i;
        for (i = n; i > 0; i--) {
            vm->av_video[i] = vm->av_video[i - 1];
        }
    }
    vm->av_video[0] = ops;
    vm->av_video_n = n + 1u;
    return (int)(n + 1u);
}

const nj_av_audio_ops *nj_avcodec_find_audio(struct nj_vm *vm,
                                            const nj_u1 *data, nj_u4 len)
{
    nj_u4 i;

    if (vm == NULL) {
        return NULL;
    }
    for (i = 0; i < vm->av_audio_n; i++) {
        if (vm->av_audio[i] != NULL && vm->av_audio[i]->accepts != NULL
            && vm->av_audio[i]->accepts(data, len)) {
            return vm->av_audio[i];
        }
    }
    return NULL;
}

const nj_av_video_ops *nj_avcodec_find_video(struct nj_vm *vm,
                                            const nj_u1 *data, nj_u4 len)
{
    nj_u4 i;

    if (vm == NULL) {
        return NULL;
    }
    for (i = 0; i < vm->av_video_n; i++) {
        if (vm->av_video[i] != NULL && vm->av_video[i]->accepts != NULL
            && vm->av_video[i]->accepts(data, len)) {
            return vm->av_video[i];
        }
    }
    return NULL;
}

const nj_av_audio_ops *nj_avcodec_sw_mp3(void)
{
#if !NJ_MP3_EXPERIMENTAL
    return NULL;   /* parser done, PCM not yet reference-clean */
#else
#if NJ_ENABLE_MP3
    return &nj_av_audio_mp3;
#else
    return NULL;
#endif
#endif
}

#if NJ_ENABLE_AMR
static const nj_av_audio_ops amr_ops;
#endif

const nj_av_audio_ops *nj_avcodec_sw_amr(void)
{
#if NJ_ENABLE_AMR
    return &amr_ops;
#endif
    /*
     * AMR is decoded by nj_amr.c through the media library's own path: it is a
     * one-shot "file in, PCM out" decoder with no streaming state, so it gains
     * nothing from an nj_avcodec provider today.  The lookup stays here so a
     * port that installs a streaming AMR decoder is not special-cased.
     */
    return NULL;
}

const nj_av_video_ops *nj_avcodec_sw_h263(void)
{
#if NJ_ENABLE_VIDEO
    return &nj_av_video_h263;
#else
    return NULL;
#endif
}

void nj_avcodec_reset(struct nj_vm *vm)
{
    const nj_av_audio_ops *a;
    const nj_av_video_ops *v;

    if (vm == NULL) {
        return;
    }
    vm->av_audio_n = 0;
    vm->av_video_n = 0;
    a = nj_avcodec_sw_mp3();
    if (a != NULL) {
        (void)nj_avcodec_install_audio(vm, a);
    }
    v = nj_avcodec_sw_h263();
    if (v != NULL) {
        (void)nj_avcodec_install_video(vm, v);
    }
#if NJ_ENABLE_AMR
    a = nj_avcodec_sw_amr();
    if (a != NULL) {
        (void)nj_avcodec_install_audio(vm, a);
    }
#endif
}

#if NJ_ENABLE_AMR
/*
 * AMR behind the seam.
 *
 * nj_amr_decode() is a whole-file decoder: it walks the 20 ms frames and writes
 * PCM the caller owns.  The provider keeps that shape - open() decodes once and
 * hands out the samples in slices - because the alternative (a stateful
 * per-frame API) is a bigger change to the decoder for no RAM win: the samples
 * have to exist in memory either way, and the media Player already decides
 * whether that is a Java array or the port's buffer.
 *
 * A port with a hardware AMR decoder installs its own provider and this one is
 * simply never reached; -DNJ_ENABLE_AMR=0 removes both.
 */
#define NJ_AMR_SAMPLE_RATE 8000u
#define NJ_AMR_BLOCK_FRAMES 160u          /* 20 ms at 8 kHz */

typedef struct amr_state {
    const nj_av_mem *mem;
    nj_u1           *pcm;
    nj_u4            len;                 /* bytes of PCM           */
    nj_u4            off;                 /* bytes handed out so far */
} amr_state;

static int amr_accepts(const nj_u1 *data, nj_u4 len)
{
    return data != NULL && len >= 6u && memcmp(data, "#!AMR\n", 6u) == 0;
}

static nj_av_dec *amr_open(const nj_u1 *data, nj_u4 len, const nj_av_mem *mem,
                           nj_av_info *info, const char **err)
{
    amr_state *st;
    nj_u4 frames;
    nj_u4 got;

    if (!amr_accepts(data, len)) {
        if (err) *err = "not an AMR narrowband stream";
        return NULL;
    }
    frames = nj_amr_frames(data, len);
    if (frames == 0u) {
        if (err) *err = "no AMR frames";
        return NULL;
    }
    st = (amr_state *)mem->alloc(mem->ud, (nj_size)sizeof(*st));
    if (st == NULL) {
        if (err) *err = "out of memory";
        return NULL;
    }
    st->mem = mem;
    st->len = frames * NJ_AMR_BLOCK_FRAMES * 2u;
    st->off = 0u;
    st->pcm = (nj_u1 *)mem->alloc(mem->ud, (nj_size)st->len);
    if (st->pcm == NULL) {
        mem->free(mem->ud, st);
        if (err) *err = "out of memory";
        return NULL;
    }
    got = nj_amr_decode(data, len, st->pcm);
    if (got == 0u) {
        mem->free(mem->ud, st->pcm);
        mem->free(mem->ud, st);
        if (err) *err = "AMR decode failed";
        return NULL;
    }
    st->len = got * NJ_AMR_BLOCK_FRAMES * 2u;
    info->format = NJ_AV_AUDIO_AMR;
    info->rate = NJ_AMR_SAMPLE_RATE;
    info->channels = 1u;
    info->frames = got * NJ_AMR_BLOCK_FRAMES;
    info->duration_ms = got * 20u;
    return (nj_av_dec *)st;
}

static int amr_decode(nj_av_dec *dec, nj_av_step *step)
{
    amr_state *st = (amr_state *)dec;
    nj_u4 left;
    nj_u4 take;

    if (st->off >= st->len) {
        return NJ_AV_ERR_END;
    }
    left = st->len - st->off;
    take = step->out_cap < left ? step->out_cap : left;
    take -= take % 2u;
    if (take == 0u || step->out == NULL) {
        return NJ_AV_ERR_AGAIN;
    }
    memcpy(step->out, st->pcm + st->off, take);
    st->off += take;
    step->out_used = take;
    step->frames = take / 2u;
    step->in_used = 0u;
    step->time_us = (nj_s8)(st->off / 2u) * 1000000LL /
                    (nj_s8)NJ_AMR_SAMPLE_RATE;
    return NJ_AV_OK;
}

static int amr_seek(nj_av_dec *dec, nj_s8 time_us)
{
    amr_state *st = (amr_state *)dec;
    nj_s8 frames = time_us > 0 ? time_us * (nj_s8)NJ_AMR_SAMPLE_RATE / 1000000LL : 0;
    nj_u4 bytes = (nj_u4)frames * 2u;

    bytes -= bytes % (NJ_AMR_BLOCK_FRAMES * 2u);
    if (bytes > st->len) {
        bytes = st->len;
    }
    st->off = bytes;
    return NJ_AV_OK;
}

static void amr_close(nj_av_dec *dec)
{
    amr_state *st = (amr_state *)dec;

    st->mem->free(st->mem->ud, st->pcm);
    st->mem->free(st->mem->ud, st);
}

static const nj_av_audio_ops amr_ops = {
    "amr-nb", NJ_AV_AUDIO_AMR, amr_accepts, amr_open, amr_decode,
    amr_seek, amr_close, 2u
};
#endif /* NJ_ENABLE_AMR */
