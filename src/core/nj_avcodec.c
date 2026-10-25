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
#if NJ_ENABLE_MP3
    return &nj_av_audio_mp3;
#else
    return NULL;
#endif
}

const nj_av_audio_ops *nj_avcodec_sw_amr(void)
{
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
}
