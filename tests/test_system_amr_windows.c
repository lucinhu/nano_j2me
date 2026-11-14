/* SPDX-License-Identifier: MIT
 * Windows SDK ABI protocol mocks; run the cross-compiled test under Wine.
 * x86_64-w64-mingw32-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -Itests
 *   tests/test_system_amr_windows.c -o build-system-amr-windows/mock.exe
 * WINEDEBUG=-all wine build-system-amr-windows/mock.exe
 * Define NJ_WINDOWS_AMR_NATIVE_PROBE to try the actual host service instead.
 * Mocks verify the bridge, not native decoder installation or PCM accuracy.
 */
#define CONST_VTABLE
#include "nj2me/nj_avcodec.h"
#ifndef NJ_WINDOWS_AMR_NATIVE_PROBE
#define NJ_SYSTEM_AMR_WINDOWS_TEST 1
#define NJ_SYSTEM_AMR_MAX_PCM_BYTES 1280u
#endif
#include "../src/port/common/nj_system_amr_windows.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NJ_WINDOWS_AMR_NATIVE_PROBE
enum fault {
    GOOD, LOAD_FAIL, SYMBOL_FAIL, CO_FAIL, CO_APARTMENT, MF_FAIL, ENUM_FAIL,
    ENUM_EMPTY, ACTIVATE_FAIL, ACTIVATE_ERROR_HANDLE, TYPE_FAIL, SET_INPUT_FAIL,
    SET_OUTPUT_FAIL, BAD_RATE, BAD_CHANNELS, BAD_BITS, BAD_SUBTYPE, BAD_ALIGN,
    STREAMS_FAIL, IDS_FAIL, OUTPUT_INFO_FAIL, BIG_OUTPUT, BAD_ALIGNMENT,
    MESSAGE_FAIL, SAMPLE_FAIL, BUFFER_FAIL, LOCK_FAIL, UNLOCK_FAIL,
    SMALL_INPUT_BUFFER, INPUT_FAIL, INPUT_BLOCKED, OUTPUT_FAIL, OUTPUT_CHANGE,
    EARLY_END, ODD_OUTPUT, EXCESS_OUTPUT, EMPTY_OUTPUT, LONG_OUTPUT, PROVIDED,
    REPLACED, SHUTDOWN_OBJECT_FAIL, SHUTDOWN_FAIL, TIME_LIMIT, IDS_DEFAULT,
    LOAD_PARTIAL, SYMBOL_LATE_FAIL, LAST_FAULT
};
static enum fault fault;
static unsigned libs, co_refs, mf_refs, objects, host_allocs, calls, fail_alloc;
static unsigned inputs_seen, output_calls, packet_mode, input_calls, drain_seen;
static DWORD ticks;
typedef struct mock_type {
    IMFMediaType iface;
    ULONG refs;
    GUID major, subtype;
    UINT32 channels, rate, bits, align, average;
} mock_type;
typedef struct mock_buffer {
    IMFMediaBuffer iface;
    ULONG refs;
    DWORD cap, len;
    int locked;
    BYTE *data;
} mock_buffer;
typedef struct mock_sample {
    IMFSample iface;
    ULONG refs;
    IMFMediaBuffer *buffer;
    LONGLONG time, duration;
} mock_sample;
typedef struct mock_transform {
    IMFTransform iface;
    ULONG refs;
    IMFMediaType *out;
    unsigned queued, emitted;
    int draining;
} mock_transform;
typedef struct mock_activate {
    IMFActivate iface;
    ULONG refs;
} mock_activate;
static const IMFMediaTypeVtbl type_vtbl;
static const IMFMediaBufferVtbl buffer_vtbl;
static const IMFSampleVtbl sample_vtbl;
static const IMFTransformVtbl transform_vtbl;
static const IMFActivateVtbl activate_vtbl;
static void *object(size_t n)
{ void *p = calloc(1, n); assert(p); ++objects; return p; }
static void unobject(void *p)
{ assert(p && objects); --objects; free(p); }
static int same(REFGUID a, REFGUID b) { return !memcmp(a, b, sizeof(GUID)); }
static ULONG STDMETHODCALLTYPE type_add(IMFMediaType *p)
{ return ++((mock_type *)p)->refs; }
static ULONG STDMETHODCALLTYPE type_release(IMFMediaType *p)
{ mock_type *t = (mock_type *)p; ULONG n = --t->refs; if (!n) unobject(t); return n; }
static HRESULT STDMETHODCALLTYPE type_guid_set(IMFMediaType *p, REFGUID k, REFGUID v)
{
    mock_type *t = (mock_type *)p;
    if (same(k, &win_amr_major)) t->major = *v;
    else if (same(k, &win_amr_subtype)) t->subtype = *v;
    else assert(0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE type_guid_get(IMFMediaType *p, REFGUID k, GUID *v)
{
    mock_type *t = (mock_type *)p;
    if (same(k, &win_amr_major)) *v = t->major;
    else if (same(k, &win_amr_subtype)) {
        *v = t->subtype;
        if (fault == BAD_SUBTYPE) v->Data1 = 3;
    } else assert(0);
    return S_OK;
}
static UINT32 *type_attr(mock_type *t, REFGUID k)
{
    if (same(k, &win_amr_channels)) return &t->channels;
    if (same(k, &win_amr_rate)) return &t->rate;
    if (same(k, &win_amr_bits)) return &t->bits;
    if (same(k, &win_amr_align)) return &t->align;
    if (same(k, &win_amr_average)) return &t->average;
    assert(0); return NULL;
}
static HRESULT STDMETHODCALLTYPE type_u32_set(IMFMediaType *p, REFGUID k, UINT32 v)
{ *type_attr((mock_type *)p, k) = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE type_u32_get(IMFMediaType *p, REFGUID k, UINT32 *v)
{
    *v = *type_attr((mock_type *)p, k);
    if (fault == BAD_RATE && same(k, &win_amr_rate)) *v = 16000;
    if (fault == BAD_CHANNELS && same(k, &win_amr_channels)) *v = 2;
    if (fault == BAD_BITS && same(k, &win_amr_bits)) *v = 32;
    if (fault == BAD_ALIGN && same(k, &win_amr_align)) *v = 4;
    return S_OK;
}
static const IMFMediaTypeVtbl type_vtbl = {
    .AddRef = type_add, .Release = type_release, .SetGUID = type_guid_set,
    .GetGUID = type_guid_get, .SetUINT32 = type_u32_set, .GetUINT32 = type_u32_get
};
static ULONG STDMETHODCALLTYPE buffer_add(IMFMediaBuffer *p)
{ return ++((mock_buffer *)p)->refs; }
static ULONG STDMETHODCALLTYPE buffer_release(IMFMediaBuffer *p)
{
    mock_buffer *b = (mock_buffer *)p;
    ULONG n = --b->refs;
    if (!n) { assert(!b->locked); free(b->data); unobject(b); }
    return n;
}
static HRESULT STDMETHODCALLTYPE buffer_lock(IMFMediaBuffer *p, BYTE **data, DWORD *max, DWORD *len)
{
    mock_buffer *b = (mock_buffer *)p;
    if (fault == LOCK_FAIL) return E_FAIL;
    assert(!b->locked); b->locked = 1; *data = b->data;
    if (max) *max = fault == SMALL_INPUT_BUFFER && b->cap < 33 ? 0 : b->cap;
    if (len) *len = b->len;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE buffer_unlock(IMFMediaBuffer *p)
{
    mock_buffer *b = (mock_buffer *)p;
    assert(b->locked); b->locked = 0;
    return fault == UNLOCK_FAIL ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE buffer_set(IMFMediaBuffer *p, DWORD n)
{ mock_buffer *b = (mock_buffer *)p; assert(n <= b->cap); b->len = n; return S_OK; }
static const IMFMediaBufferVtbl buffer_vtbl = {
    .AddRef = buffer_add, .Release = buffer_release, .Lock = buffer_lock,
    .Unlock = buffer_unlock, .SetCurrentLength = buffer_set
};
static HRESULT WINAPI make_buffer(DWORD cap, DWORD alignment, IMFMediaBuffer **out)
{
    mock_buffer *b;
    assert(cap && (alignment == 0 || alignment == 15));
    if (fault == BUFFER_FAIL) return E_FAIL;
    b = (mock_buffer *)object(sizeof(*b)); b->iface.lpVtbl = &buffer_vtbl;
    b->refs = 1; b->cap = cap; b->data = (BYTE *)malloc(cap); assert(b->data);
    *out = &b->iface; return S_OK;
}
static ULONG STDMETHODCALLTYPE sample_release(IMFSample *p)
{
    mock_sample *s = (mock_sample *)p; ULONG n = --s->refs;
    if (!n) { if (s->buffer) IMFMediaBuffer_Release(s->buffer); unobject(s); }
    return n;
}
static HRESULT STDMETHODCALLTYPE sample_add(IMFSample *p, IMFMediaBuffer *b)
{
    mock_sample *s = (mock_sample *)p;
    assert(!s->buffer); s->buffer = b; IMFMediaBuffer_AddRef(b); return S_OK;
}
static HRESULT STDMETHODCALLTYPE sample_time(IMFSample *p, LONGLONG n)
{ ((mock_sample *)p)->time = n; return S_OK; }
static HRESULT STDMETHODCALLTYPE sample_duration(IMFSample *p, LONGLONG n)
{ ((mock_sample *)p)->duration = n; return S_OK; }
static HRESULT STDMETHODCALLTYPE sample_contiguous(IMFSample *p, IMFMediaBuffer **out)
{
    mock_sample *s = (mock_sample *)p;
    assert(s->buffer); *out = s->buffer; IMFMediaBuffer_AddRef(*out); return S_OK;
}
static const IMFSampleVtbl sample_vtbl = {
    .Release = sample_release, .AddBuffer = sample_add,
    .SetSampleTime = sample_time, .SetSampleDuration = sample_duration,
    .ConvertToContiguousBuffer = sample_contiguous
};
static HRESULT WINAPI make_sample(IMFSample **out)
{
    mock_sample *s;
    if (fault == SAMPLE_FAIL) return E_FAIL;
    s = (mock_sample *)object(sizeof(*s)); s->iface.lpVtbl = &sample_vtbl;
    s->refs = 1; *out = &s->iface; return S_OK;
}
static ULONG STDMETHODCALLTYPE transform_release(IMFTransform *p)
{
    mock_transform *m = (mock_transform *)p; ULONG n = --m->refs;
    if (!n) { if (m->out) IMFMediaType_Release(m->out); unobject(m); }
    return n;
}
static HRESULT STDMETHODCALLTYPE transform_count(IMFTransform *p, DWORD *in, DWORD *out)
{ (void)p; *in = *out = 1; return fault == STREAMS_FAIL ? E_FAIL : S_OK; }
static HRESULT STDMETHODCALLTYPE transform_ids(IMFTransform *p, DWORD ni, DWORD *in, DWORD no, DWORD *out)
{
    (void)p; assert(ni == 1 && no == 1); *in = 4; *out = 8;
    if (fault == IDS_DEFAULT) return E_NOTIMPL;
    return fault == IDS_FAIL ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE transform_input_info(IMFTransform *p, DWORD id, MFT_INPUT_STREAM_INFO *i)
{ (void)p; assert(id == (fault == IDS_DEFAULT ? 0u : 4u)); memset(i, 0, sizeof(*i)); i->cbAlignment = 16; return S_OK; }
static HRESULT STDMETHODCALLTYPE transform_info(IMFTransform *p, DWORD id, MFT_OUTPUT_STREAM_INFO *i)
{
    (void)p; assert(id == (fault == IDS_DEFAULT ? 0u : 8u)); memset(i, 0, sizeof(*i));
    i->cbSize = fault == BIG_OUTPUT ? 65536 : 320;
    i->cbAlignment = fault == BAD_ALIGNMENT ? 3 : 16;
    if (fault == PROVIDED) { i->dwFlags = MFT_OUTPUT_STREAM_PROVIDES_SAMPLES; i->cbSize = 0; }
    return fault == OUTPUT_INFO_FAIL ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE transform_input_type(IMFTransform *p, DWORD id, IMFMediaType *t, DWORD flags)
{
    mock_type *v = (mock_type *)t;
    (void)p; assert(id == (fault == IDS_DEFAULT ? 0u : 4u) && !flags && v->rate == 8000 && v->channels == 1);
    assert(same(&v->subtype, &win_amr_nb));
    return fault == SET_INPUT_FAIL ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE transform_output_type(IMFTransform *p, DWORD id, IMFMediaType *t, DWORD flags)
{
    mock_transform *m = (mock_transform *)p; mock_type *v = (mock_type *)t;
    assert(id == (fault == IDS_DEFAULT ? 0u : 8u) && !flags && v->rate == 8000 && v->channels == 1 && v->bits == 16);
    assert(v->align == 2 && v->average == 16000 && same(&v->subtype, &win_amr_pcm));
    if (fault == SET_OUTPUT_FAIL) return E_FAIL;
    m->out = t; IMFMediaType_AddRef(t); return S_OK;
}
static HRESULT STDMETHODCALLTYPE transform_current(IMFTransform *p, DWORD id, IMFMediaType **t)
{
    mock_transform *m = (mock_transform *)p;
    assert(id == (fault == IDS_DEFAULT ? 0u : 8u) && m->out); *t = m->out; IMFMediaType_AddRef(*t); return S_OK;
}
static HRESULT STDMETHODCALLTYPE transform_message(IMFTransform *p, MFT_MESSAGE_TYPE type, ULONG_PTR n)
{
    mock_transform *m = (mock_transform *)p;
    if (type == MFT_MESSAGE_NOTIFY_END_OF_STREAM) assert(n == (fault == IDS_DEFAULT ? 0u : 4u));
    else assert(n == 0);
    if (type == MFT_MESSAGE_COMMAND_DRAIN) { m->draining = 1; ++drain_seen; }
    return fault == MESSAGE_FAIL ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE transform_input(IMFTransform *p, DWORD id, IMFSample *s, DWORD flags)
{
    mock_transform *m = (mock_transform *)p; mock_sample *v = (mock_sample *)s;
    mock_buffer *b = (mock_buffer *)v->buffer;
    assert(id == (fault == IDS_DEFAULT ? 0u : 4u) && !flags && b && b->len == win_amr_packet(b->data[0]));
    assert((b->data[0] >> 3) == packet_mode && v->duration == 200000);
    assert(v->time == (LONGLONG)inputs_seen * 200000);
    ++input_calls;
    if (fault == INPUT_FAIL) return E_FAIL;
    if (fault == INPUT_BLOCKED) return MF_E_NOTACCEPTING;
    ++m->queued; ++inputs_seen; return S_OK;
}
static HRESULT STDMETHODCALLTYPE transform_output(IMFTransform *p, DWORD flags, DWORD count,
                                                   MFT_OUTPUT_DATA_BUFFER *out, DWORD *status)
{
    mock_transform *m = (mock_transform *)p;
    mock_buffer *b;
    unsigned i, bytes = 320;
    assert(!flags && count == 1 && out->dwStreamID == (fault == IDS_DEFAULT ? 0u : 8u) && status);
    ++output_calls;
    if (fault == OUTPUT_FAIL) return E_FAIL;
    if (fault == OUTPUT_CHANGE) return MF_E_TRANSFORM_STREAM_CHANGE;
    if (fault == EMPTY_OUTPUT) bytes = 0;
    if (fault == EARLY_END) return MF_E_TRANSFORM_NEED_MORE_INPUT;
    if (fault == LONG_OUTPUT) return S_OK;
    if (!m->queued && fault != EXCESS_OUTPUT) return MF_E_TRANSFORM_NEED_MORE_INPUT;
    if (fault == ODD_OUTPUT) bytes = 319;
    if (fault == PROVIDED || fault == REPLACED) {
        IMFSample *s;
        IMFMediaBuffer *buf;
        assert(SUCCEEDED(make_sample(&s)) && SUCCEEDED(make_buffer(320, 0, &buf)));
        assert(SUCCEEDED(IMFSample_AddBuffer(s, buf))); IMFMediaBuffer_Release(buf);
        out->pSample = s;
    }
    b = (mock_buffer *)((mock_sample *)out->pSample)->buffer;
    assert(b->cap >= bytes);
    for (i = 0; i < bytes; ++i) b->data[i] = (BYTE)(i + m->emitted + 1);
    b->len = bytes;
    if (m->queued) --m->queued;
    ++m->emitted;
    return S_OK;
}
static const IMFTransformVtbl transform_vtbl = {
    .Release = transform_release, .GetStreamCount = transform_count,
    .GetStreamIDs = transform_ids, .GetInputStreamInfo = transform_input_info,
    .GetOutputStreamInfo = transform_info, .SetInputType = transform_input_type,
    .SetOutputType = transform_output_type, .GetOutputCurrentType = transform_current,
    .ProcessMessage = transform_message, .ProcessInput = transform_input,
    .ProcessOutput = transform_output
};
static ULONG STDMETHODCALLTYPE activate_release(IMFActivate *p)
{ mock_activate *a = (mock_activate *)p; ULONG n = --a->refs; if (!n) unobject(a); return n; }
static HRESULT STDMETHODCALLTYPE activate_object(IMFActivate *p, REFIID iid, void **out)
{
    mock_transform *m;
    (void)p; assert(same(iid, &win_amr_transform_iid));
    if (fault == ACTIVATE_FAIL) return E_FAIL;
    m = (mock_transform *)object(sizeof(*m)); m->iface.lpVtbl = &transform_vtbl;
    m->refs = 1; *out = m;
    return fault == ACTIVATE_ERROR_HANDLE ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE activate_shutdown(IMFActivate *p)
{ (void)p; return fault == SHUTDOWN_OBJECT_FAIL ? E_FAIL : S_OK; }
static const IMFActivateVtbl activate_vtbl = {
    .Release = activate_release, .ActivateObject = activate_object, .ShutdownObject = activate_shutdown
};
static HRESULT WINAPI make_type(IMFMediaType **out)
{
    mock_type *t;
    if (fault == TYPE_FAIL) return E_FAIL;
    t = (mock_type *)object(sizeof(*t)); t->iface.lpVtbl = &type_vtbl; t->refs = 1;
    *out = &t->iface; return S_OK;
}
static HRESULT WINAPI enumerate(GUID category, UINT32 flags, const MFT_REGISTER_TYPE_INFO *in,
                                 const MFT_REGISTER_TYPE_INFO *out, IMFActivate ***list, UINT32 *count)
{
    mock_activate *a;
    assert(same(&category, &win_amr_category));
    assert(flags == (MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER));
    assert(same(&in->guidSubtype, &win_amr_nb) && same(&out->guidSubtype, &win_amr_pcm));
    if (fault == ENUM_FAIL) return E_FAIL;
    if (fault == ENUM_EMPTY) return S_OK;
    *list = (IMFActivate **)object(sizeof(**list)); *count = 1;
    a = (mock_activate *)object(sizeof(*a)); a->iface.lpVtbl = &activate_vtbl; a->refs = 1;
    (*list)[0] = &a->iface; return S_OK;
}
static HRESULT WINAPI co_init(LPVOID p, DWORD flags)
{
    assert(!p && flags == COINIT_MULTITHREADED);
    if (fault == CO_FAIL) return E_FAIL;
    if (fault == CO_APARTMENT) return RPC_E_CHANGED_MODE;
    ++co_refs; return S_OK;
}
static void WINAPI co_uninit(void) { assert(co_refs == 1); --co_refs; }
static void WINAPI co_free(LPVOID p) { unobject(p); }
static HRESULT WINAPI startup(ULONG version, DWORD flags)
{ assert(version == MF_VERSION && flags == MFSTARTUP_LITE); if (fault == MF_FAIL) return E_FAIL; ++mf_refs; return S_OK; }
static HRESULT WINAPI mock_mf_shutdown(void)
{ assert(mf_refs == 1); --mf_refs; return fault == SHUTDOWN_FAIL ? E_FAIL : S_OK; }
HMODULE WINAPI nj_test_win_load(LPCWSTR name, HANDLE file, DWORD flags)
{
    assert(!file && flags == LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(!wcscmp(name, L"mfplat.dll") || !wcscmp(name, L"ole32.dll"));
    if (fault == LOAD_FAIL || (fault == LOAD_PARTIAL && !wcscmp(name, L"ole32.dll"))) return NULL;
    ++libs; return (HMODULE)(ULONG_PTR)(!wcscmp(name, L"mfplat.dll") ? 1 : 2);
}
BOOL WINAPI nj_test_win_unload(HMODULE p)
{ assert(p && libs && !objects && !co_refs && !mf_refs); --libs; return TRUE; }
DWORD WINAPI nj_test_win_ticks(void)
{ DWORD value = ticks; ticks += fault == TIME_LIMIT ? 10001u : 1u; return value; }
FARPROC WINAPI nj_test_win_symbol(HMODULE p, LPCSTR name)
{
    FARPROC result = NULL;
    assert(p && libs == 2);
    if (fault == SYMBOL_FAIL || (fault == SYMBOL_LATE_FAIL && !strcmp(name, "CoTaskMemFree"))) return NULL;
#define ADDRESS(fn) do { __typeof__(&(fn)) f = &(fn); assert(sizeof(f) == sizeof(result)); memcpy(&result, &f, sizeof(f)); } while (0)
    if (!strcmp(name, "MFStartup")) ADDRESS(startup);
    else if (!strcmp(name, "MFShutdown")) ADDRESS(mock_mf_shutdown);
    else if (!strcmp(name, "MFCreateMediaType")) ADDRESS(make_type);
    else if (!strcmp(name, "MFCreateSample")) ADDRESS(make_sample);
    else if (!strcmp(name, "MFCreateAlignedMemoryBuffer")) ADDRESS(make_buffer);
    else if (!strcmp(name, "MFTEnumEx")) ADDRESS(enumerate);
    else if (!strcmp(name, "CoInitializeEx")) ADDRESS(co_init);
    else if (!strcmp(name, "CoUninitialize")) ADDRESS(co_uninit);
    else if (!strcmp(name, "CoTaskMemFree")) ADDRESS(co_free);
    else assert(0);
#undef ADDRESS
    return result;
}
static void clean(void)
{ assert(!objects && !libs && !co_refs && !mf_refs); }
static void reset(enum fault f, unsigned allocation)
{
    clean(); assert(!host_allocs); fault = f; fail_alloc = allocation;
    calls = inputs_seen = output_calls = input_calls = drain_seen = 0; ticks = 0;
}
#endif
static void *take(void *ud, nj_u4 n)
{
    (void)ud;
#ifndef NJ_WINDOWS_AMR_NATIVE_PROBE
    if (++calls == fail_alloc) return NULL;
    ++host_allocs;
#endif
    return malloc(n);
}
static void drop(void *ud, void *p)
{
    (void)ud; assert(p);
#ifndef NJ_WINDOWS_AMR_NATIVE_PROBE
    assert(host_allocs); --host_allocs;
#endif
    free(p);
}
static unsigned storage(unsigned char *data, unsigned mode)
{
    unsigned n = win_amr_packet((BYTE)((mode << 3) | 4)), off = 6, i;
    memcpy(data, "#!AMR\n", 6);
    for (i = 0; i < 2; ++i) {
        data[off] = (BYTE)((mode << 3) | 4); memset(data + off + 1, 0, n - 1); off += n;
    }
    return off;
}
int main(void)
{
    const nj_av_audio_ops *ops = nj_avcodec_system_amr();
    nj_av_mem mem = {NULL, take, drop};
    nj_av_info info;
    const char *error;
    unsigned char data[70], pcm[642];
    nj_av_dec *dec;
    unsigned len;
#ifdef NJ_WINDOWS_AMR_NATIVE_PROBE
    len = storage(data, 0);
    dec = ops->open(data, len, &mem, &info, &error);
    if (!dec) { printf("Wine actual MF AMR unavailable/rejected: %s; built-in retry required\n", error); return 0; }
    ops->close(dec);
    printf("Wine actual MF AMR decoded synthetic packets (not native Windows validation)\n");
    (void)pcm;
#else
    unsigned mode, f;
    nj_av_step step;
    assert(ops && ops->pcm_frame_bytes == 2 && !strcmp(ops->name, "system-amr-windows"));
    for (mode = 0; mode < 16; ++mode) {
        nj_av_mem temporary_hooks = mem;
        if (mode > 8 && mode != 15) continue;
        reset(GOOD, 0); packet_mode = mode; len = storage(data, mode);
        assert(ops->accepts(data, len));
        dec = ops->open(data, len, &temporary_hooks, &info, &error);
        memset(&temporary_hooks, 0, sizeof(temporary_hooks)); /* provider must copy */
        assert(dec && !error && inputs_seen == 2 && drain_seen == 1 && host_allocs == 2); clean();
        assert(info.rate == 8000 && info.channels == 1 && info.frames == 320 && info.duration_ms == 40);
        memset(pcm, 0xa5, sizeof(pcm)); memset(&step, 0, sizeof(step));
        step.out = pcm + 1; step.out_cap = 1;
        assert(ops->decode(dec, &step) == NJ_AV_ERR_AGAIN && !step.out_used);
        step.out_cap = 639;
        assert(ops->decode(dec, &step) == NJ_AV_OK && step.out_used == 638 && step.frames == 319);
        assert(step.in_used == 0 && step.time_us == 0 && pcm[0] == 0xa5 && pcm[639] == 0xa5);
        assert(pcm[1] == 1 && pcm[320] == 64 && pcm[321] == 2);
        step.out_cap = 4;
        assert(ops->decode(dec, &step) == NJ_AV_OK && step.out_used == 2 && step.time_us == 39875);
        assert(ops->decode(dec, &step) == NJ_AV_ERR_END);
        assert(ops->seek(dec, 20001) == NJ_AV_OK);
        assert(ops->decode(dec, &step) == NJ_AV_OK && step.time_us == 20000);
        assert(ops->seek(dec, 0x7fffffffffffffffLL) == NJ_AV_OK && ops->decode(dec, &step) == NJ_AV_ERR_END);
        assert(ops->seek(dec, -1) == NJ_AV_OK && ops->decode(dec, &step) == NJ_AV_OK && !step.time_us);
        ops->close(dec); assert(!host_allocs); clean();
    }
    packet_mode = 0;
    for (f = 1; f < LAST_FAULT; ++f) {
        int success = f == CO_APARTMENT || f == PROVIDED || f == REPLACED || f == IDS_DEFAULT;
        reset((enum fault)f, 0); len = storage(data, 0);
        dec = ops->open(data, len, &mem, &info, &error);
        assert((dec != NULL) == success);
        if (dec) ops->close(dec); else assert(error);
        assert(!host_allocs); clean();
        assert(input_calls < 100 && output_calls < 100);
    }
    for (f = 1; f <= 2; ++f) {
        reset(GOOD, f); len = storage(data, 0);
        assert(!ops->open(data, len, &mem, &info, &error)); assert(!host_allocs); clean();
    }
    reset(GOOD, 0); len = storage(data, 0);
    assert(!ops->open(data, len - 1, &mem, &info, &error));
    data[6] = (9u << 3); assert(!ops->open(data, len, &mem, &info, &error));
    len = storage(data, 0); data[6] |= 0x80; assert(!ops->open(data, len, &mem, &info, &error));
    assert(!ops->open(data, 6, &mem, &info, &error));
    assert(!ops->open(NULL, 6, &mem, &info, &error));
    assert(!ops->open(data, len, NULL, &info, &error));
    assert(!ops->open(data, len, &mem, NULL, &error));
    assert(!ops->accepts(NULL, 0));
    for (mode = 9; mode < 15; ++mode) {
        memcpy(data, "#!AMR\n", 6); data[6] = (BYTE)(mode << 3);
        assert(!ops->open(data, 7, &mem, &info, &error));
    }
    memcpy(data, "#!AMR\n", 6); memset(data + 6, (15u << 3) | 4u, 5);
    assert(!ops->open(data, 11, &mem, &info, &error)); /* 1600 > test cap 1280 */
    assert(ops->decode(NULL, &step) == NJ_AV_ERR_CORRUPT);
    assert(ops->seek(NULL, 0) == NJ_AV_ERR_CORRUPT);
    assert(!calls); clean();
    printf("PASS Windows MF AMR SDK mocks: all modes, %u faults, allocation/format/bounds/drain/cleanup/seek\n", LAST_FAULT - 1);
#endif
    return 0;
}
