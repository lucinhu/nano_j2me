/* SPDX-License-Identifier: MIT
 * System service protocol tests, not an AMR decoder accuracy test.
 * Host stub: compile with src/port/common/nj_system_amr.c normally.
 * Android protocol: add -DNJ_ENABLE_SYSTEM_AMR=1
 * -DNJ_SYSTEM_AMR_ANDROID_TEST=1 -Itests/private to both translation units.
 */
#include "nj2me/nj_system_amr.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

#ifdef NJ_SYSTEM_AMR_ANDROID_TEST
#include "private/system_amr_mock_ndk.h"

enum fault {
    GOOD, LOAD_FAIL, SYMBOL_FAIL, CREATE_FAIL, FORMAT_FAIL, CONFIG_FAIL,
    START_FAIL, INPUT_ERROR, INPUT_NULL, INPUT_SMALL, QUEUE_FAIL,
    OUTPUT_ERROR, OUTPUT_NULL, OUTPUT_NEGATIVE, OUTPUT_LARGE, OUTPUT_ODD,
    BAD_RATE, BAD_CHANNELS, BAD_ENCODING, NO_FORMAT, RELEASE_FAIL,
    EARLY_EOS, SHORT_PCM, STALL, FORMAT_LOOP, NO_FORMAT_EVENT,
    NO_VM_SYMBOL, NO_VM, VM_QUERY_FAIL, NULL_VM,
    LAST_FAULT
};
static enum fault fault;
static int libraries, codecs, formats, allocations, alloc_calls, fail_alloc;
static int stop_calls, released, output_calls, queue_calls;
struct AMediaFormat { int dummy; };
struct AMediaCodec {
    unsigned queued, produced;
    int eos, announced;
    uint8_t input[32], output[320];
};

static AMediaCodec *mock_create(const char *mime)
{
    AMediaCodec *c;
    assert(strcmp(mime, "audio/3gpp") == 0);
    if (fault == CREATE_FAIL) return NULL;
    c = calloc(1, sizeof(*c));
    assert(c);
    ++codecs;
    return c;
}
static media_status_t mock_delete(AMediaCodec *c)
{ assert(c); free(c); --codecs; return AMEDIA_OK; }
static AMediaFormat *mock_format_new(void)
{
    AMediaFormat *f;
    if (fault == FORMAT_FAIL) return NULL;
    f = malloc(sizeof(*f)); assert(f); ++formats; return f;
}
static media_status_t mock_format_delete(AMediaFormat *f)
{ assert(f); free(f); --formats; return AMEDIA_OK; }
static void mock_set_string(AMediaFormat *f, const char *key, const char *value)
{ assert(f && !strcmp(key, "mime") && !strcmp(value, "audio/3gpp")); }
static void mock_set_int(AMediaFormat *f, const char *key, int32_t value)
{
    assert(f);
    assert((!strcmp(key, "sample-rate") && value == 8000) ||
           (!strcmp(key, "channel-count") && value == 1) ||
           (!strcmp(key, "max-input-size") && value == 32));
}
static bool mock_get_int(AMediaFormat *f, const char *key, int32_t *value)
{
    assert(f);
    if (!strcmp(key, "sample-rate")) *value = fault == BAD_RATE ? 16000 : 8000;
    else if (!strcmp(key, "channel-count")) *value = fault == BAD_CHANNELS ? 2 : 1;
    else if (!strcmp(key, "pcm-encoding")) *value = fault == BAD_ENCODING ? 4 : 2;
    else return false;
    return true;
}
static media_status_t mock_configure(AMediaCodec *c, const AMediaFormat *f,
                                    ANativeWindow *w, AMediaCrypto *crypto, uint32_t flags)
{ assert(c && f && !w && !crypto && !flags); return fault == CONFIG_FAIL ? -99 : 0; }
static media_status_t mock_start(AMediaCodec *c)
{ assert(c); return fault == START_FAIL ? -99 : 0; }
static media_status_t mock_stop(AMediaCodec *c)
{ assert(c); ++stop_calls; return 0; }
static ssize_t mock_input_index(AMediaCodec *c, int64_t timeout)
{
    assert(c && timeout == 0);
    if (fault == INPUT_ERROR) return -99;
    if (fault == STALL) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    return 0;
}
static uint8_t *mock_input_buffer(AMediaCodec *c, size_t index, size_t *cap)
{
    assert(c && index == 0 && cap);
    *cap = fault == INPUT_SMALL ? 1 : sizeof(c->input);
    return fault == INPUT_NULL ? NULL : c->input;
}
static media_status_t mock_queue(AMediaCodec *c, size_t index, long offset,
                                size_t size, uint64_t time, uint32_t flags)
{
    assert(c && index == 0 && offset == 0 && time == c->queued * 20000u);
    ++queue_calls;
    if (fault == QUEUE_FAIL) return -99;
    if (flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        assert(!size && !c->eos); c->eos = 1;
    } else {
        assert(size >= 1 && size <= 32); ++c->queued;
    }
    return 0;
}
static ssize_t mock_output_index(AMediaCodec *c, AMediaCodecBufferInfo *info, int64_t timeout)
{
    assert(c && info && timeout <= 10000);
    ++output_calls;
    if (fault == OUTPUT_ERROR) return -99;
    if (fault == FORMAT_LOOP) return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
    if (!c->announced && fault != NO_FORMAT_EVENT) {
        c->announced = 1; return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
    }
    if (fault == STALL) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    if (fault == EARLY_EOS) {
        info->flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM; return 0;
    }
    if (c->produced < c->queued) {
        size_t i;
        for (i = 0; i < sizeof(c->output); ++i)
            c->output[i] = (uint8_t)(c->produced + i);
        info->size = fault == OUTPUT_NEGATIVE ? -1 :
                     fault == OUTPUT_LARGE ? 100000 :
                     fault == OUTPUT_ODD ? 319 : fault == SHORT_PCM ? 318 : 320;
        /* Both fields have been unreliable in older NDK releases. The returned
         * pointer is already at the valid bytes: offset must not be added. */
        info->offset = INT32_MAX;
        ++c->produced;
        return 0;
    }
    if (c->eos) { info->flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM; return 0; }
    return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}
static uint8_t *mock_output_buffer(AMediaCodec *c, size_t index, size_t *cap)
{
    assert(c && index == 0);
    if (cap) *cap = 1; /* old out_size is not a valid accessible byte count */
    return fault == OUTPUT_NULL ? NULL : c->output;
}
static media_status_t mock_release(AMediaCodec *c, size_t index, bool render)
{ assert(c && index == 0 && !render); ++released; return fault == RELEASE_FAIL ? -99 : 0; }
static AMediaFormat *mock_output_format(AMediaCodec *c)
{ assert(c); return fault == NO_FORMAT ? NULL : mock_format_new(); }

void *system_amr_mock_dlopen(const char *name, int flags)
{
    (void)flags;
    assert(!strcmp(name, "libmediandk.so"));
    if (fault == LOAD_FAIL) return NULL;
    ++libraries; return &libraries;
}
int system_amr_mock_dlclose(void *lib)
{ assert(lib == &libraries); --libraries; return 0; }
static jint mock_created_vms(JavaVM **vm, jsize cap, jsize *count)
{
    assert(vm && count && cap == 1);
    *vm = fault == NULL_VM ? NULL : (JavaVM *)&allocations;
    *count = fault == NO_VM ? 0 : 1;
    return fault == VM_QUERY_FAIL ? -1 : 0;
}
void *system_amr_mock_dlsym(void *lib, const char *name)
{
    if (!strcmp(name, "JNI_GetCreatedJavaVMs")) {
        jint (*fn)(JavaVM **, jsize, jsize *) = mock_created_vms;
        void *p = NULL;
        assert(lib == RTLD_DEFAULT && !libraries && !codecs && !formats && !allocations);
        if (fault == NO_VM_SYMBOL) return NULL;
        assert(sizeof(fn) == sizeof(p)); memcpy(&p, &fn, sizeof(p)); return p;
    }
    assert(lib == &libraries);
    if (fault == SYMBOL_FAIL) return NULL;
    /* This mock uses the same function/object pointer representation as POSIX
     * dlsym, without violating strict aliasing in either translation unit. */
#define SYMBOL(symbol, fn) if (!strcmp(name, symbol)) { \
    __typeof__(&fn) f = &fn; void *p = NULL; \
    assert(sizeof(f) == sizeof(p)); memcpy(&p, &f, sizeof(p)); return p; }
    SYMBOL("AMediaCodec_createDecoderByType", mock_create)
    SYMBOL("AMediaCodec_delete", mock_delete)
    SYMBOL("AMediaCodec_configure", mock_configure)
    SYMBOL("AMediaCodec_start", mock_start)
    SYMBOL("AMediaCodec_stop", mock_stop)
    SYMBOL("AMediaCodec_dequeueInputBuffer", mock_input_index)
    SYMBOL("AMediaCodec_getInputBuffer", mock_input_buffer)
    SYMBOL("AMediaCodec_queueInputBuffer", mock_queue)
    SYMBOL("AMediaCodec_dequeueOutputBuffer", mock_output_index)
    SYMBOL("AMediaCodec_getOutputBuffer", mock_output_buffer)
    SYMBOL("AMediaCodec_releaseOutputBuffer", mock_release)
    SYMBOL("AMediaCodec_getOutputFormat", mock_output_format)
    SYMBOL("AMediaFormat_new", mock_format_new)
    SYMBOL("AMediaFormat_delete", mock_format_delete)
    SYMBOL("AMediaFormat_setString", mock_set_string)
    SYMBOL("AMediaFormat_setInt32", mock_set_int)
    SYMBOL("AMediaFormat_getInt32", mock_get_int)
#undef SYMBOL
    assert(!"unexpected API symbol"); return NULL;
}
static void *test_alloc(void *ud, nj_u4 bytes)
{
    void *p;
    assert(ud == &allocations);
    if (++alloc_calls == fail_alloc) return NULL;
    p = malloc(bytes); assert(p); ++allocations; return p;
}
static void test_free(void *ud, void *p)
{ assert(ud == &allocations && p); free(p); --allocations; }
static void reset(enum fault value)
{
    assert(!libraries && !codecs && !formats && !allocations);
    fault = value; alloc_calls = fail_alloc = stop_calls = released = 0;
    output_calls = queue_calls = 0;
}
static nj_u4 stream(nj_u1 *data)
{
    /* Cover all eight speech modes, SID, and no-data framing. Payload content
     * is arbitrary here: the mocked service emits known PCM instead. */
    static const unsigned sizes[10] = {13,14,16,18,20,21,27,32,6,1};
    unsigned i, off = 6;
    memcpy(data, "#!AMR\n", 6);
    for (i = 0; i < 10; ++i) {
        memset(data + off, 0, sizes[i]);
        data[off] = (nj_u1)(((i == 9 ? 15 : i) << 3) | 4);
        off += sizes[i];
    }
    return off;
}
int main(void)
{
    const nj_av_audio_ops *ops = nj_avcodec_system_amr();
    nj_av_mem mem = { &allocations, test_alloc, test_free };
    nj_u1 input[256], out[333];
    nj_u4 len = stream(input);
    nj_av_info info;
    nj_av_dec *dec;
    const char *err;
    unsigned mode;
    assert(ops && ops->format == NJ_AV_AUDIO_AMR);
    assert(!ops->accepts(NULL, 999) && !ops->accepts(input, 5));
    assert(ops->accepts(input, len));
    for (mode = GOOD; mode < LAST_FAULT; ++mode) {
        nj_u4 total = 0;
        reset((enum fault)mode);
        memset(&info, 0xAB, sizeof(info));
        dec = ops->open(input, len, &mem, &info, &err);
        if (mode != GOOD && mode != NO_FORMAT_EVENT) {
            assert(!dec && err && !libraries && !codecs && !formats && !allocations);
            assert(output_calls < 400);
            continue;
        }
        assert(dec && !err && info.frames == 1600 && info.rate == 8000 &&
               info.channels == 1 && info.duration_ms == 200);
        assert(!libraries && !codecs && !formats && allocations == 2);
        /* Memory hooks are copied into the decoder, rather than retaining this
         * stack struct. Output guards also check odd capacities are rounded. */
        mem.alloc = NULL; mem.free = NULL;
        for (;;) {
            nj_av_step step;
            int rc;
            memset(out, 0xCC, sizeof(out));
            memset(&step, 0xAB, sizeof(step));
            step.out = out + 1; step.out_cap = 331;
            rc = ops->decode(dec, &step);
            assert(!step.in_used && out[0] == 0xCC && out[331] == 0xCC && out[332] == 0xCC);
            if (rc == NJ_AV_ERR_END) { assert(!step.out_used && !step.frames); break; }
            assert(rc == NJ_AV_OK && step.out_used <= 330 && !(step.out_used & 1));
            assert(step.frames == step.out_used / 2 && step.time_us == (int64_t)total * 1000000 / 16000);
            {
                nj_u4 i;
                for (i = 0; i < step.out_used; ++i)
                    assert(step.out[i] == (uint8_t)((total + i) / 320 + (total + i) % 320));
            }
            total += step.out_used;
        }
        assert(total == 3200);
        assert(ops->seek(dec, INT64_MAX) == 0);
        assert(ops->seek(dec, INT64_MIN) == 0);
        {
            nj_av_step step = {0};
            step.out = out; step.out_cap = 1;
            assert(ops->decode(dec, &step) == NJ_AV_ERR_AGAIN && !step.out_used);
            assert(ops->seek(dec, 39999) == 0);
            step.out_cap = 2;
            assert(ops->decode(dec, &step) == NJ_AV_OK && step.time_us == 20000);
        }
        ops->close(dec);
        mem.alloc = test_alloc; mem.free = test_free;
        assert(!allocations);
    }
    for (mode = 1; mode <= 2; ++mode) {
        reset(GOOD); fail_alloc = (int)mode;
        assert(!ops->open(input, len, &mem, &info, &err) && err);
        assert(!allocations && !libraries && !codecs && !formats && stop_calls == 1);
    }
    reset(GOOD);
    assert(!ops->open(input, len, NULL, &info, &err));
    assert(!ops->open(input, len, &mem, NULL, &err));
    assert(!ops->open(input, 6, &mem, &info, &err));
    assert(!ops->open(input, len - 2, &mem, &info, &err));
    input[6] = 9u << 3;
    assert(!ops->open(input, len, &mem, &info, &err));
    input[6] = 0x84;
    assert(!ops->open(input, len, &mem, &info, &err));
    assert(!libraries && !allocations && !queue_calls);
    puts("system AMR: service protocol, failure cleanup, framing and PCM bounds PASS");
    return 0;
}
#else
int main(void)
{
    assert(nj_avcodec_system_amr() == NULL);
    puts("system AMR: unsupported/disabled host reports unavailable PASS");
    return 0;
}
#endif
