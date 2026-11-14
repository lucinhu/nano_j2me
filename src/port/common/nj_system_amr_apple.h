/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 nano_j2me contributors
 * Apple runtime service bridge, included only by nj_system_amr.c.
 * Public API contracts:
 * https://developer.apple.com/documentation/audiotoolbox/audioconverternew(_:_:_:)
 * https://developer.apple.com/documentation/audiotoolbox/audioconverterfillcomplexbuffer(_:_:_:_:_:_:)
 * An historical format list is not treated as present-day capability. Each
 * open must successfully create and run a converter on the actual host.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <dlfcn.h>
#include "nj_system_amr_apple_abi.h"
#ifdef NJ_SYSTEM_AMR_APPLE_TEST
#include "system_amr_mock_apple.h"
#endif
#ifndef NJ_SYSTEM_AMR_MAX_PCM_BYTES
#define NJ_SYSTEM_AMR_MAX_PCM_BYTES (32u * 1024u * 1024u)
#endif

typedef struct apple_amr {
    nj_av_mem mem;
    nj_u1 *pcm;
    nj_u4 len, off;
} apple_amr;
typedef struct apple_packets {
    const nj_u1 *data;
    nj_u4 len, off, supplied;
    int invalid;
    nj_apple_packet packet;
} apple_packets;

static unsigned apple_frame_bytes(nj_u1 toc)
{
    static const unsigned sizes[16] = {13,14,16,18,20,21,27,32,6,0,0,0,0,0,0,1};
    return (toc & 0x83u) ? 0u : sizes[(toc >> 3) & 15u];
}
static int apple_accepts(const nj_u1 *data, nj_u4 len)
{ return data && len >= 6u && !memcmp(data, "#!AMR\n", 6u); }
static int32_t apple_input(nj_apple_converter converter, uint32_t *count,
                           nj_apple_buffers *buffers, nj_apple_packet **desc,
                           void *ud)
{
    apple_packets *p = (apple_packets *)ud;
    unsigned n;
    (void)converter;
    if (!p || !count || !buffers) return -1;
    buffers->count = 1;
    buffers->buffers[0].channels = 1;
    buffers->buffers[0].size = 0;
    buffers->buffers[0].data = NULL;
    if (desc) *desc = NULL;
    if (!*count || p->off == p->len) { *count = 0; return 0; }
    n = apple_frame_bytes(p->data[p->off]);
    if (!n || n > p->len - p->off) { p->invalid = 1; *count = 0; return -1; }
    /* Supply one packet so the description has a fixed, owned lifetime until
     * the callback is next invoked. Input storage remains live through Fill. */
    p->packet.offset = 0;
    p->packet.frames = 0; /* constant 160 frames in the stream description */
    p->packet.size = n;
    buffers->buffers[0].data = (void *)(p->data + p->off);
    buffers->buffers[0].size = n;
    if (desc) *desc = &p->packet;
    *count = 1;
    p->off += n;
    ++p->supplied;
    return 0;
}
static nj_av_dec *apple_open(const nj_u1 *data, nj_u4 len, const nj_av_mem *mem,
                             nj_av_info *info, const char **error)
{
    void *lib = NULL;
    nj_apple_converter converter = NULL;
    int32_t (*create)(const nj_apple_asbd *, const nj_apple_asbd *, nj_apple_converter *) = NULL;
    int32_t (*dispose)(nj_apple_converter) = NULL;
    int32_t (*fill)(nj_apple_converter, nj_apple_input, void *, uint32_t *,
                    nj_apple_buffers *, nj_apple_packet *) = NULL;
    nj_apple_asbd input = {0}, output = {0};
    apple_amr *st = NULL;
    apple_packets packets;
    nj_u4 off, frames = 0, size, produced = 0;
    int good = 0;
    if (error) *error = "Apple AMR system decoder unavailable or rejected input";
    if (!mem || !mem->alloc || !mem->free || !info || !apple_accepts(data, len)) return NULL;
    for (off = 6; off < len;) {
        unsigned n = apple_frame_bytes(data[off]);
        if (!n || n > len - off || frames >= NJ_SYSTEM_AMR_MAX_PCM_BYTES / 320u) return NULL;
        off += n; ++frames;
    }
    if (!frames) return NULL;
    size = frames * 320u;
    lib = dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_NOW | RTLD_LOCAL);
    if (!lib) goto done;
#define APPLE_SYMBOL(fn, name) do { \
    void *symbol = dlsym(lib, name); \
    if (!symbol || sizeof(fn) != sizeof(symbol)) goto done; \
    memcpy(&(fn), &symbol, sizeof(fn)); \
} while (0)
    APPLE_SYMBOL(create, "AudioConverterNew");
    APPLE_SYMBOL(dispose, "AudioConverterDispose");
    APPLE_SYMBOL(fill, "AudioConverterFillComplexBuffer");
#undef APPLE_SYMBOL
    input.rate = 8000; input.format = NJ_APPLE_AMR;
    input.frames_packet = 160; input.channels = 1;
    output.rate = 8000; output.format = NJ_APPLE_PCM;
    output.flags = NJ_APPLE_S16_PACKED;
    output.bytes_packet = 2; output.frames_packet = 1; output.bytes_frame = 2;
    output.channels = 1; output.bits = 16;
    /* AudioConverterNew is the runtime capability query. Unsupported AMR
     * formats must fail here; successful creation alone is not enough: below
     * we also require a complete, bounded conversion of the actual input. */
    if (create(&input, &output, &converter) || !converter) goto done;
    st = (apple_amr *)mem->alloc(mem->ud, (nj_u4)sizeof(*st));
    if (!st) goto done;
    memset(st, 0, sizeof(*st)); st->mem = *mem;
    st->pcm = (nj_u1 *)mem->alloc(mem->ud, size);
    if (!st->pcm) goto done;
    memset(&packets, 0, sizeof(packets));
    packets.data = data; packets.len = len; packets.off = 6;
    /* Request one AMR frame of PCM per call. A converter may emit fewer
     * samples while buffering. Bound calls to prevent an unavailable service
     * that repeatedly reports success without progress from spinning forever. */
    for (off = 0; off < frames * 4u + 16u && produced < size; ++off) {
        nj_apple_buffers buffers;
        uint32_t samples = (size - produced) / 2u;
        uint32_t capacity;
        if (samples > 160u) samples = 160u;
        capacity = samples * 2u;
        memset(&buffers, 0, sizeof(buffers));
        buffers.count = 1; buffers.buffers[0].channels = 1;
        buffers.buffers[0].size = capacity;
        buffers.buffers[0].data = st->pcm + produced;
        if (fill(converter, apple_input, &packets, &samples, &buffers, NULL) ||
            packets.invalid || buffers.count != 1u ||
            buffers.buffers[0].channels != 1u ||
            buffers.buffers[0].data != st->pcm + produced ||
            samples > capacity / 2u || buffers.buffers[0].size != samples * 2u)
            goto done;
        produced += samples * 2u;
    }
    if (produced != size || packets.supplied != frames || packets.off != len) goto done;
    st->len = size;
    memset(info, 0, sizeof(*info));
    info->rate = 8000; info->channels = 1; info->frames = frames * 160u;
    info->format = NJ_AV_AUDIO_AMR;
    info->duration_ms = frames * 20u;
    good = 1;
done:
    if (converter && dispose && dispose(converter)) good = 0;
    if (lib) dlclose(lib);
    if (!good) {
        if (st) { if (st->pcm) mem->free(mem->ud, st->pcm); mem->free(mem->ud, st); }
        return NULL;
    }
    if (error) *error = NULL;
    return (nj_av_dec *)st;
}
static int apple_decode(nj_av_dec *dec, nj_av_step *step)
{
    apple_amr *st = (apple_amr *)dec;
    nj_u4 take;
    if (!st || !step) return NJ_AV_ERR_CORRUPT;
    step->in_used = step->out_used = step->frames = 0;
    step->time_us = (nj_s8)(st->off / 2u) * 1000000LL / 8000;
    if (st->off == st->len) return NJ_AV_ERR_END;
    take = st->len - st->off;
    if (take > step->out_cap) take = step->out_cap;
    take &= ~1u;
    if (!step->out || !take) return NJ_AV_ERR_AGAIN;
    memcpy(step->out, st->pcm + st->off, take);
    st->off += take; step->out_used = take; step->frames = take / 2u;
    return NJ_AV_OK;
}
static int apple_seek(nj_av_dec *dec, nj_s8 time_us)
{
    apple_amr *st = (apple_amr *)dec;
    if (!st) return NJ_AV_ERR_CORRUPT;
    if (time_us <= 0) st->off = 0;
    else if (time_us >= (nj_s8)(st->len / 320u) * 20000LL) st->off = st->len;
    else st->off = (nj_u4)(time_us / 20000LL) * 320u;
    return NJ_AV_OK;
}
static void apple_close(nj_av_dec *dec)
{
    apple_amr *st = (apple_amr *)dec;
    if (st) { nj_av_mem mem = st->mem; mem.free(mem.ud, st->pcm); mem.free(mem.ud, st); }
}
const nj_av_audio_ops *nj_avcodec_system_amr(void)
{
    static const nj_av_audio_ops ops = {
        "system-amr-apple", NJ_AV_AUDIO_AMR, apple_accepts, apple_open,
        apple_decode, apple_seek, apple_close, 2u
    };
    return &ops;
}
