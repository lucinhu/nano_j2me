/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 nano_j2me contributors
 *
 * Optional Android, Apple and Windows AMR-NB services, independently
 * implemented against public native APIs. No codec implementation is included
 * here. Other hosts return NULL.
 */
#include "nj2me/nj_system_amr.h"
#include "nj2me/nj_config.h"

#if NJ_ENABLE_AMR && NJ_ENABLE_SYSTEM_AMR && \
    (defined(__ANDROID__) || defined(NJ_SYSTEM_AMR_ANDROID_TEST))
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#ifdef NJ_SYSTEM_AMR_ANDROID_TEST
/* Build-only seam for exercising native service failures on a non-Android
 * test host; no mock implementation enters a production build. */
#include "system_amr_mock_ndk.h"
#else
#include <jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#endif

/* Whole-file predecode, like the built-in AMR provider. Reject larger input
 * rather than multiplying untrusted frame counts or allocating without limit.
 * A port can lower this bound at compile time. */
#ifndef NJ_SYSTEM_AMR_MAX_PCM_BYTES
#define NJ_SYSTEM_AMR_MAX_PCM_BYTES (32u * 1024u * 1024u)
#endif
#define AMR_FRAME_BYTES 320u
#define AMR_RATE 8000u
#define AMR_IDLE_LIMIT 100u

/* API 21 entry points only. Each open owns its library reference and table;
 * there is no global initialization, codec cache, or process-lifetime handle. */
typedef struct amr_api {
    void *lib;
    AMediaCodec *(*create)(const char *);
    media_status_t (*destroy)(AMediaCodec *);
    media_status_t (*configure)(AMediaCodec *, const AMediaFormat *,
                                ANativeWindow *, AMediaCrypto *, uint32_t);
    media_status_t (*start)(AMediaCodec *);
    media_status_t (*stop)(AMediaCodec *);
    ssize_t (*input_index)(AMediaCodec *, int64_t);
    uint8_t *(*input_buffer)(AMediaCodec *, size_t, size_t *);
    /* The NDK queue offset ABI is sizeof(long), even on LP32 builds with
     * _FILE_OFFSET_BITS=64; spelling it off_t would silently corrupt calls. */
    media_status_t (*queue)(AMediaCodec *, size_t, long, size_t, uint64_t, uint32_t);
    ssize_t (*output_index)(AMediaCodec *, AMediaCodecBufferInfo *, int64_t);
    uint8_t *(*output_buffer)(AMediaCodec *, size_t, size_t *);
    media_status_t (*release)(AMediaCodec *, size_t, bool);
    AMediaFormat *(*output_format)(AMediaCodec *);
    AMediaFormat *(*format_new)(void);
    media_status_t (*format_delete)(AMediaFormat *);
    void (*set_string)(AMediaFormat *, const char *, const char *);
    void (*set_int)(AMediaFormat *, const char *, int32_t);
    bool (*get_int)(AMediaFormat *, const char *, int32_t *);
} amr_api;

typedef struct system_amr {
    nj_av_mem mem;              /* copy: caller's hooks struct may be temporary */
    nj_u1 *pcm;
    nj_u4 len;
    nj_u4 off;
} system_amr;

/* Android app runtimes own native-service callback initialization. A plain
 * adb-shell executable on API21 can create/start a MediaCodec yet receive no
 * buffers and block in stop(). Do not enter that service from a VM-less CLI.
 * Query the public JNI invocation API already present in the process; never
 * load libart, create a VM, attach a thread, or retain a Java reference here. */
static int android_app_runtime(void)
{
    jint (*created_vms)(JavaVM **, jsize, jsize *) = NULL;
    JavaVM *vm = NULL;
    jsize count = 0;
    void *symbol = dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (!symbol || sizeof(symbol) != sizeof(created_vms)) return 0;
    memcpy(&created_vms, &symbol, sizeof(created_vms));
    return created_vms(&vm, 1, &count) == 0 && count > 0 && vm != NULL;
}

static int api_load(amr_api *api)
{
    memset(api, 0, sizeof(*api));
    api->lib = dlopen("libmediandk.so", RTLD_NOW | RTLD_LOCAL);
    if (!api->lib) return 0;
    /* POSIX dlsym conversion; memcpy avoids ISO C object/function pointer
     * aliasing casts and works on Android's supported ABIs. */
#define LOAD(member, symbol) do { \
    void *p = dlsym(api->lib, symbol); \
    if (!p || sizeof(api->member) != sizeof(p)) goto missing; \
    memcpy(&api->member, &p, sizeof(p)); \
} while (0)
    LOAD(create, "AMediaCodec_createDecoderByType");
    LOAD(destroy, "AMediaCodec_delete");
    LOAD(configure, "AMediaCodec_configure");
    LOAD(start, "AMediaCodec_start");
    LOAD(stop, "AMediaCodec_stop");
    LOAD(input_index, "AMediaCodec_dequeueInputBuffer");
    LOAD(input_buffer, "AMediaCodec_getInputBuffer");
    LOAD(queue, "AMediaCodec_queueInputBuffer");
    LOAD(output_index, "AMediaCodec_dequeueOutputBuffer");
    LOAD(output_buffer, "AMediaCodec_getOutputBuffer");
    LOAD(release, "AMediaCodec_releaseOutputBuffer");
    LOAD(output_format, "AMediaCodec_getOutputFormat");
    LOAD(format_new, "AMediaFormat_new");
    LOAD(format_delete, "AMediaFormat_delete");
    LOAD(set_string, "AMediaFormat_setString");
    LOAD(set_int, "AMediaFormat_setInt32");
    LOAD(get_int, "AMediaFormat_getInt32");
#undef LOAD
    return 1;
missing:
    dlclose(api->lib);
    api->lib = NULL;
    return 0;
}

static int system_accepts(const nj_u1 *data, nj_u4 len)
{
    return data && len >= 6u && memcmp(data, "#!AMR\n", 6u) == 0;
}

/* Storage framing (RFC 4867 section 5.1): sizes include the one-byte ToC.
 * Reserved modes and truncated packets are refused, never read partially. */
static nj_u4 packet_size(nj_u1 toc)
{
    static const nj_u1 sizes[16] = {
        13, 14, 16, 18, 20, 21, 27, 32, 6, 0, 0, 0, 0, 0, 0, 1
    };
    if (toc & 0x83u) return 0u; /* storage padding bits, not RTP continuation */
    return sizes[(toc >> 3u) & 15u];
}

static nj_u4 count_frames(const nj_u1 *data, nj_u4 len)
{
    nj_u4 off = 6u, frames = 0u;
    while (off < len) {
        nj_u4 size = packet_size(data[off]);
        if (!size || size > len - off ||
            frames >= NJ_SYSTEM_AMR_MAX_PCM_BYTES / AMR_FRAME_BYTES) return 0u;
        off += size;
        ++frames;
    }
    return frames;
}

static int pcm_format_ok(const amr_api *api, AMediaCodec *codec)
{
    AMediaFormat *format = api->output_format(codec);
    int32_t rate = 0, channels = 0, encoding = 2; /* PCM_16BIT default */
    int ok;
    if (!format) return 0;
    ok = api->get_int(format, "sample-rate", &rate) && rate == 8000 &&
         api->get_int(format, "channel-count", &channels) && channels == 1;
    /* This literal key also works before its NDK exported constant (API28).
     * The platform contract defaults to signed native-endian 16-bit PCM;
     * Android ABIs are little endian. Explicit float/other PCM is rejected. */
    (void)api->get_int(format, "pcm-encoding", &encoding);
    ok = ok && encoding == 2;
    api->format_delete(format);
    return ok;
}

static int predecode(const amr_api *api, AMediaCodec *codec,
                     const nj_u1 *data, nj_u4 len, system_amr *st)
{
    nj_u4 off = 6u, frame = 0u, written = 0u, idle = 0u;
    nj_u4 budget = (st->len / AMR_FRAME_BYTES) * 8u + 256u;
    int sent_eos = 0, format_ok = 0;
    while (budget--) {
        int progress = 0;
        AMediaCodecBufferInfo info;
        ssize_t index;
        if (!sent_eos) {
            index = api->input_index(codec, 0);
            if (index >= 0) {
                size_t cap = 0u;
                uint8_t *buffer = api->input_buffer(codec, (size_t)index, &cap);
                nj_u4 size = off < len ? packet_size(data[off]) : 0u;
                uint32_t flags = size ? 0u : AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
                if (!buffer || cap < size) return 0;
                if (size) memcpy(buffer, data + off, size);
                if (api->queue(codec, (size_t)index, 0, size,
                               (uint64_t)frame * 20000u, flags) != AMEDIA_OK) return 0;
                off += size;
                if (size) ++frame;
                else sent_eos = 1;
                progress = 1;
            } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER) return 0;
        }
        memset(&info, 0, sizeof(info));
        index = api->output_index(codec, &info, 10000); /* <=10ms idle wait */
        if (index >= 0) {
            /* NDK docs also mark getOutputBuffer's out_size invalid through
             * API35. BufferInfo.size is the platform's valid byte count. */
            uint8_t *buffer = api->output_buffer(codec, (size_t)index, NULL);
            int valid = info.size >= 0 && (nj_u4)info.size <= st->len - written &&
                        (info.size & 1) == 0 &&
                        (info.size == 0 || buffer != NULL);
            if (info.size && !format_ok) format_ok = pcm_format_ok(api, codec);
            valid = valid && (info.size == 0 || format_ok);
            /* NDK getOutputBuffer already returns the accessible buffer view.
             * NDK docs: BufferInfo.offset is invalid through API35; ignore it.
             * Adding it would double-offset on older devices. */
            if (valid && info.size) {
                memcpy(st->pcm + written, buffer, (size_t)info.size);
                written += (nj_u4)info.size;
            }
            if (api->release(codec, (size_t)index, false) != AMEDIA_OK) return 0;
            if (!valid) return 0;
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
                return sent_eos && off == len && written == st->len;
            progress = 1;
        } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            format_ok = pcm_format_ok(api, codec);
            if (!format_ok) return 0;
            progress = 1;
        } else if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            progress = 1; /* byte-buffer API obtains the current pointer each time */
        } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER) return 0;
        idle = progress ? 0u : idle + 1u;
        if (idle >= AMR_IDLE_LIMIT) return 0;
    }
    return 0; /* faulty service cannot keep us in this loop indefinitely */
}

static nj_av_dec *system_open(const nj_u1 *data, nj_u4 len,
                              const nj_av_mem *mem, nj_av_info *info,
                              const char **err)
{
    amr_api api;
    AMediaCodec *codec = NULL;
    AMediaFormat *format = NULL;
    system_amr *st = NULL;
    nj_u4 frames;
    int started = 0, success = 0;
    const char *why = "Android AMR service unavailable";
    if (err) *err = NULL;
    if (!system_accepts(data, len) || !mem || !mem->alloc || !mem->free || !info) {
        if (err) *err = "invalid system AMR request";
        return NULL;
    }
    frames = count_frames(data, len);
    if (!frames) {
        if (err) *err = "invalid, empty, or oversized AMR-NB storage stream";
        return NULL;
    }
    if (!android_app_runtime()) {
        if (err) *err = "system AMR requires an initialized Android app runtime";
        return NULL;
    }
    if (!api_load(&api)) {
        if (err) *err = why;
        return NULL;
    }
    codec = api.create("audio/3gpp");
    if (!codec) goto done;
    format = api.format_new();
    if (!format) goto done;
    api.set_string(format, "mime", "audio/3gpp");
    api.set_int(format, "sample-rate", 8000);
    api.set_int(format, "channel-count", 1);
    api.set_int(format, "max-input-size", 32);
    if (api.configure(codec, format, NULL, NULL, 0) != AMEDIA_OK) goto done;
    if (api.start(codec) != AMEDIA_OK) goto done;
    started = 1;
    st = (system_amr *)mem->alloc(mem->ud, (nj_u4)sizeof(*st));
    why = "out of memory for system AMR PCM";
    if (!st) goto done;
    st->mem = *mem;
    st->len = frames * AMR_FRAME_BYTES;
    st->off = 0u;
    st->pcm = (nj_u1 *)mem->alloc(mem->ud, st->len);
    if (!st->pcm) goto done;
    why = "Android AMR service rejected or failed to decode the stream";
    if (!predecode(&api, codec, data, len, st)) goto done;
    memset(info, 0, sizeof(*info));
    info->format = NJ_AV_AUDIO_AMR;
    info->rate = AMR_RATE;
    info->channels = 1u;
    info->frames = frames * 160u;
    info->duration_ms = frames * 20u;
    success = 1;
done:
    if (started) (void)api.stop(codec);
    if (format) api.format_delete(format);
    if (codec) api.destroy(codec);
    dlclose(api.lib);
    if (!success) {
        if (st) {
            if (st->pcm) mem->free(mem->ud, st->pcm);
            mem->free(mem->ud, st);
        }
        if (err) *err = why;
        return NULL;
    }
    return (nj_av_dec *)st;
}

static int system_decode(nj_av_dec *dec, nj_av_step *step)
{
    system_amr *st = (system_amr *)dec;
    nj_u4 take;
    if (!st || !step) return NJ_AV_ERR_CORRUPT;
    step->in_used = step->out_used = step->frames = 0u;
    step->time_us = (nj_s8)(st->off / 2u) * 1000000LL / AMR_RATE;
    if (st->off == st->len) return NJ_AV_ERR_END;
    take = st->len - st->off;
    if (take > step->out_cap) take = step->out_cap;
    take &= ~1u;
    if (!step->out || !take) return NJ_AV_ERR_AGAIN;
    memcpy(step->out, st->pcm + st->off, take);
    st->off += take;
    step->out_used = take;
    step->frames = take / 2u;
    return NJ_AV_OK;
}

static int system_seek(nj_av_dec *dec, nj_s8 time_us)
{
    system_amr *st = (system_amr *)dec;
    nj_s8 duration;
    if (!st) return NJ_AV_ERR_CORRUPT;
    duration = (nj_s8)(st->len / AMR_FRAME_BYTES) * 20000LL;
    /* Clamp before multiplication, including INT64_MAX, and align to 20ms. */
    if (time_us <= 0) st->off = 0u;
    else if (time_us >= duration) st->off = st->len;
    else st->off = (nj_u4)(time_us / 20000LL) * AMR_FRAME_BYTES;
    return NJ_AV_OK;
}

static void system_close(nj_av_dec *dec)
{
    system_amr *st = (system_amr *)dec;
    if (st) {
        nj_av_mem mem = st->mem;
        mem.free(mem.ud, st->pcm);
        mem.free(mem.ud, st);
    }
}

const nj_av_audio_ops *nj_avcodec_system_amr(void)
{
    static const nj_av_audio_ops ops = {
        "system-amr-android", NJ_AV_AUDIO_AMR, system_accepts, system_open,
        system_decode, system_seek, system_close, 2u
    };
    return &ops;
}
#elif NJ_ENABLE_AMR && NJ_ENABLE_SYSTEM_AMR && \
      (defined(__APPLE__) || defined(NJ_SYSTEM_AMR_APPLE_TEST))
#include "nj_system_amr_apple.h"
#elif NJ_ENABLE_AMR && NJ_ENABLE_SYSTEM_AMR && \
      (defined(_WIN32) || defined(NJ_SYSTEM_AMR_WINDOWS_TEST))
#include "nj_system_amr_windows.h"
#else
const nj_av_audio_ops *nj_avcodec_system_amr(void)
{
    return NULL;
}
#endif
