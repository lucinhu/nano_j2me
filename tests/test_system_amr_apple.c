/* SPDX-License-Identifier: MIT
 * Host protocol and cleanup tests, not a native Apple codec accuracy test.
 * Compile together with nj_system_amr.c, -DNJ_ENABLE_SYSTEM_AMR=1
 * -DNJ_SYSTEM_AMR_APPLE_TEST=1 -Itests/private -Iinclude.
 */
#include "nj2me/nj_system_amr.h"
#include "private/system_amr_mock_apple.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum fault { GOOD, LOAD_FAIL, SYMBOL_FAIL, CREATE_FAIL, CREATE_NULL,
    CREATE_ERROR_HANDLE, FILL_FAIL, DISPOSE_FAIL, BAD_COUNT, BAD_CHANNELS,
    BAD_POINTER, TOO_MANY_SAMPLES, BAD_SIZE, ODD_SIZE, NO_PROGRESS,
    EARLY_END, NO_INPUT, LAST_FAULT };
static enum fault fault;
static unsigned libraries, converters, allocated, alloc_calls, fail_alloc;
static unsigned callback_calls, symbols, fills;
static unsigned mock_converter;

static int32_t mock_new(const nj_apple_asbd *in, const nj_apple_asbd *out,
                        nj_apple_converter *converter)
{
    assert(in && out && converter);
    assert(in->rate == 8000 && in->format == NJ_APPLE_AMR && in->frames_packet == 160);
    assert(in->channels == 1 && in->bytes_packet == 0 && in->flags == 0);
    assert(out->rate == 8000 && out->format == NJ_APPLE_PCM && out->flags == 12);
    assert(out->bytes_packet == 2 && out->frames_packet == 1 && out->bytes_frame == 2);
    assert(out->channels == 1 && out->bits == 16 && out->reserved == 0);
    if (fault == CREATE_FAIL) return -5;
    if (fault == CREATE_NULL) { *converter = NULL; return 0; }
    *converter = &mock_converter; ++converters;
    return fault == CREATE_ERROR_HANDLE ? -5 : 0;
}
static int32_t mock_dispose(nj_apple_converter converter)
{
    assert(converter == &mock_converter && converters == 1);
    --converters;
    return fault == DISPOSE_FAIL ? -7 : 0;
}
static int32_t mock_fill(nj_apple_converter converter, nj_apple_input input,
                         void *ud, uint32_t *samples, nj_apple_buffers *out,
                         nj_apple_packet *out_packets)
{
    nj_apple_buffers in;
    nj_apple_packet *packet = NULL;
    uint32_t count = 3, i;
    unsigned expected;
    assert(converter == &mock_converter && input && ud && samples && out && !out_packets);
    assert(*samples <= 160 && out->count == 1 && out->buffers[0].channels == 1);
    assert(out->buffers[0].size == *samples * 2u && out->buffers[0].data);
    ++fills;
    if (fault == FILL_FAIL) return -9;
    if (fault == NO_PROGRESS || fault == NO_INPUT || (fault == EARLY_END && fills > 1)) {
        *samples = 0; out->buffers[0].size = 0; return 0;
    }
    memset(&in, 0, sizeof(in));
    /* Caller permits a zero packet request; it must not consume input. */
    count = 0;
    assert(input(converter, &count, &in, &packet, ud) == 0 && !count);
    assert(in.count == 1 && in.buffers[0].size == 0 && !packet);
    count = 3;
    assert(input(converter, &count, &in, &packet, ud) == 0);
    if (!count) { *samples = 0; out->buffers[0].size = 0; return 0; }
    ++callback_calls;
    assert(count == 1 && in.count == 1 && in.buffers[0].channels == 1);
    assert(packet && packet->offset == 0 && packet->frames == 0);
    expected = ((const unsigned char *)in.buffers[0].data)[0] >> 3;
    assert(expected < 16 && packet->size == in.buffers[0].size);
    assert(packet->size >= 1 && packet->size <= 32);
    for (i = 0; i < *samples * 2u; ++i)
        ((unsigned char *)out->buffers[0].data)[i] = (unsigned char)(i + callback_calls);
    if (fault == BAD_COUNT) out->count = 2;
    if (fault == BAD_CHANNELS) out->buffers[0].channels = 2;
    if (fault == BAD_POINTER) out->buffers[0].data = NULL;
    if (fault == TOO_MANY_SAMPLES) ++*samples;
    if (fault == BAD_SIZE) out->buffers[0].size += 2;
    if (fault == ODD_SIZE) --out->buffers[0].size;
    return 0;
}
void *nj_test_apple_dlopen(const char *path, int flags)
{
    (void)flags;
    assert(!strcmp(path, "/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox"));
    if (fault == LOAD_FAIL) return NULL;
    ++libraries; return &libraries;
}
int nj_test_apple_dlclose(void *lib)
{ assert(lib == &libraries && libraries == 1 && !converters); --libraries; return 0; }
void *nj_test_apple_dlsym(void *lib, const char *name)
{
    void *result = NULL;
    assert(lib == &libraries && libraries == 1);
    ++symbols;
    if (fault == SYMBOL_FAIL) return NULL;
#define ADDRESS(fn) do { __typeof__(&(fn)) p = &(fn); \
    assert(sizeof(result) == sizeof(p)); memcpy(&result, &p, sizeof(p)); } while (0)
    if (!strcmp(name, "AudioConverterNew")) ADDRESS(mock_new);
    else if (!strcmp(name, "AudioConverterDispose")) ADDRESS(mock_dispose);
    else if (!strcmp(name, "AudioConverterFillComplexBuffer")) ADDRESS(mock_fill);
    else assert(0);
#undef ADDRESS
    return result;
}
static void *take(void *ud, nj_u4 size)
{
    void *p;
    assert(ud == &allocated);
    if (++alloc_calls == fail_alloc) return NULL;
    p = malloc(size); assert(p); ++allocated; return p;
}
static void drop(void *ud, void *p)
{ assert(ud == &allocated && p && allocated); free(p); --allocated; }
static void clear(enum fault f, unsigned fail)
{
    assert(!libraries && !converters && !allocated);
    fault = f; fail_alloc = fail; alloc_calls = 0;
    callback_calls = symbols = fills = 0;
}
static unsigned stream(unsigned char *data, unsigned mode)
{
    static const unsigned sizes[16] = {13,14,16,18,20,21,27,32,6,0,0,0,0,0,0,1};
    unsigned i, len = 6, n = sizes[mode];
    memcpy(data, "#!AMR\n", 6);
    for (i = 0; i < 2; ++i) {
        data[len] = (unsigned char)((mode << 3) | 4);
        memset(data + len + 1, 0x5a, n - 1); len += n;
    }
    return len;
}
int main(void)
{
    const nj_av_audio_ops *ops = nj_avcodec_system_amr();
    nj_av_mem mem = { &allocated, take, drop };
    nj_av_info info;
    nj_av_step step;
    nj_av_dec *dec;
    unsigned char data[70], pcm[642];
    const char *error;
    unsigned mode, len, f;
    assert(sizeof(nj_apple_asbd) == 40 && offsetof(nj_apple_asbd, reserved) == 36);
    assert(sizeof(nj_apple_packet) == 16 && offsetof(nj_apple_packet, size) == 12);
    assert(ops && !strcmp(ops->name, "system-amr-apple") && ops->pcm_frame_bytes == 2);
    for (mode = 0; mode < 16; ++mode) {
        if (mode > 8 && mode != 15) continue;
        clear(GOOD, 0); len = stream(data, mode);
        assert(ops->accepts(data, len));
        error = "stale";
        dec = ops->open(data, len, &mem, &info, &error);
        assert(dec && !error && callback_calls == 2 && symbols == 3);
        assert(info.format == NJ_AV_AUDIO_AMR && info.rate == 8000 && info.channels == 1);
        assert(info.frames == 320 && info.duration_ms == 40);
        assert(!libraries && !converters && allocated == 2);
        memset(pcm, 0xa5, sizeof(pcm)); memset(&step, 0, sizeof(step));
        step.out = pcm + 1; step.out_cap = 1;
        assert(ops->decode(dec, &step) == NJ_AV_ERR_AGAIN && !step.out_used);
        assert(pcm[1] == 0xa5);
        step.out_cap = 641;
        assert(ops->decode(dec, &step) == NJ_AV_OK && step.out_used == 640 && step.frames == 320);
        assert(pcm[0] == 0xa5 && pcm[641] == 0xa5);
        assert(ops->decode(dec, &step) == NJ_AV_ERR_END && !step.out_used);
        assert(ops->seek(dec, INT64_MAX) == NJ_AV_OK && ops->decode(dec, &step) == NJ_AV_ERR_END);
        assert(ops->seek(dec, INT64_MIN) == NJ_AV_OK);
        step.out_cap = 2;
        assert(ops->decode(dec, &step) == NJ_AV_OK && step.out_used == 2 && step.time_us == 0);
        assert(ops->seek(dec, 20001) == NJ_AV_OK);
        assert(ops->decode(dec, &step) == NJ_AV_OK && step.time_us == 20000);
        ops->close(dec); assert(!allocated);
    }
    len = stream(data, 7);
    for (f = LOAD_FAIL; f < LAST_FAULT; ++f) {
        clear((enum fault)f, 0); error = NULL;
        assert(!ops->open(data, len, &mem, &info, &error) && error);
        assert(!libraries && !converters && !allocated);
        if (f == NO_PROGRESS || f == EARLY_END || f == NO_INPUT) assert(fills <= 24);
    }
    for (f = 1; f <= 2; ++f) {
        clear(GOOD, f);
        assert(!ops->open(data, len, &mem, &info, &error));
        assert(!libraries && !converters && !allocated);
    }
    clear(GOOD, 0);
    assert(!ops->open(data, 6, &mem, &info, &error));
    assert(!ops->open(data, len - 1, &mem, &info, &error));
    data[6] |= 0x80;
    assert(!ops->open(data, len, &mem, &info, &error));
    data[6] = 9 << 3;
    assert(!ops->open(data, len, &mem, &info, &error));
    assert(!ops->open(NULL, len, &mem, &info, &error));
    assert(!ops->open(data, len, NULL, &info, &error));
    assert(!ops->open(data, len, &mem, NULL, &error));
    assert(!libraries && !converters && !allocated);
    puts("Apple AMR protocol: 10 frame types, 16 service failures, memory/bounds/seek checks passed");
    return 0;
}
