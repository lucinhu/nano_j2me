/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 nano_j2me contributors
 * Private Windows Media Foundation bridge, included by nj_system_amr.c.
 * Public contracts, not codec implementation sources:
 * https://learn.microsoft.com/en-us/windows/win32/medfound/audio-subtype-guids
 * https://learn.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
 * AMR_NB is a Windows 8.1+ format identifier, not a promise that a decoder is
 * installed. Enumerate, configure and completely decode on every open.
 */
#ifndef CINTERFACE
#define CINTERFACE
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <string.h>
#ifdef NJ_SYSTEM_AMR_WINDOWS_TEST
#include "system_amr_mock_windows.h"
#endif
#ifndef NJ_SYSTEM_AMR_MAX_PCM_BYTES
#define NJ_SYSTEM_AMR_MAX_PCM_BYTES (32u * 1024u * 1024u)
#endif

/* Private copies of public SDK identifiers avoid a mandatory mfuuid/uuid link.
 * These are format/API identifiers, not decoder tables or codec code. */
static const GUID win_amr_major = {0x48eba18e,0xf8c9,0x4687,{0xbf,0x11,0x0a,0x74,0xc9,0xf9,0x6a,0x8f}};
static const GUID win_amr_subtype = {0xf7e34c9a,0x42e8,0x4714,{0xb7,0x4b,0xcb,0x29,0xd7,0x2c,0x35,0xe5}};
static const GUID win_amr_channels = {0x37e48bf5,0x645e,0x4c5b,{0x89,0xde,0xad,0xa9,0xe2,0x9b,0x69,0x6a}};
static const GUID win_amr_rate = {0x5faeeae7,0x0290,0x4c31,{0x9e,0x8a,0xc5,0x34,0xf6,0x8d,0x9d,0xba}};
static const GUID win_amr_bits = {0xf2deb57f,0x40fa,0x4764,{0xaa,0x33,0xed,0x4f,0x2d,0x1f,0xf6,0x69}};
static const GUID win_amr_align = {0x322de230,0x9eeb,0x43bd,{0xab,0x7a,0xff,0x41,0x22,0x51,0x54,0x1d}};
static const GUID win_amr_average = {0x1aab75c8,0xcfef,0x451c,{0xab,0x95,0xac,0x03,0x4b,0x8e,0x17,0x31}};
static const GUID win_amr_audio = {0x73647561,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
static const GUID win_amr_nb = {0x7361,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
static const GUID win_amr_pcm = {1,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
static const GUID win_amr_category = {0x9ea73fb4,0xef7a,0x4559,{0x8d,0x5d,0x71,0x9d,0x8f,0x04,0x26,0xc7}};
static const GUID win_amr_transform_iid = {0xbf94c121,0x5b05,0x4e6f,{0x80,0,0xba,0x59,0x89,0x61,0x41,0x4d}};

typedef struct win_amr_api {
    HMODULE mf, ole;
    HRESULT (WINAPI *startup)(ULONG, DWORD);
    HRESULT (WINAPI *shutdown)(void);
    HRESULT (WINAPI *media_type)(IMFMediaType **);
    HRESULT (WINAPI *sample)(IMFSample **);
    HRESULT (WINAPI *buffer)(DWORD, DWORD, IMFMediaBuffer **);
    HRESULT (WINAPI *enumerate)(GUID, UINT32, const MFT_REGISTER_TYPE_INFO *,
                               const MFT_REGISTER_TYPE_INFO *, IMFActivate ***, UINT32 *);
    HRESULT (WINAPI *co_init)(LPVOID, DWORD);
    void (WINAPI *co_uninit)(void);
    void (WINAPI *co_free)(LPVOID);
} win_amr_api;
typedef struct win_amr_state {
    nj_av_mem mem;
    nj_u1 *pcm;
    nj_u4 len, off;
} win_amr_state;

static int win_amr_load(win_amr_api *a)
{
    memset(a, 0, sizeof(*a));
    /* Restrict DLL lookup to the OS directory, never game/current directories. */
    a->mf = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    a->ole = LoadLibraryExW(L"ole32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!a->mf || !a->ole) return 0;
#define WIN_AMR_SYMBOL(lib, field, name) do { \
    FARPROC p = GetProcAddress(a->lib, name); \
    if (!p || sizeof(p) != sizeof(a->field)) return 0; \
    memcpy(&a->field, &p, sizeof(p)); \
} while (0)
    WIN_AMR_SYMBOL(mf, startup, "MFStartup");
    WIN_AMR_SYMBOL(mf, shutdown, "MFShutdown");
    WIN_AMR_SYMBOL(mf, media_type, "MFCreateMediaType");
    WIN_AMR_SYMBOL(mf, sample, "MFCreateSample");
    WIN_AMR_SYMBOL(mf, buffer, "MFCreateAlignedMemoryBuffer");
    WIN_AMR_SYMBOL(mf, enumerate, "MFTEnumEx");
    WIN_AMR_SYMBOL(ole, co_init, "CoInitializeEx");
    WIN_AMR_SYMBOL(ole, co_uninit, "CoUninitialize");
    WIN_AMR_SYMBOL(ole, co_free, "CoTaskMemFree");
#undef WIN_AMR_SYMBOL
    return 1;
}
static int win_amr_accepts(const nj_u1 *data, nj_u4 len)
{ return data && len >= 6u && !memcmp(data, "#!AMR\n", 6u); }
static nj_u4 win_amr_packet(nj_u1 toc)
{
    static const nj_u1 sizes[16] = {13,14,16,18,20,21,27,32,6,0,0,0,0,0,0,1};
    return (toc & 0x83u) ? 0u : sizes[(toc >> 3u) & 15u];
}
static int win_amr_type(const win_amr_api *a, IMFMediaType **out, int pcm)
{
    IMFMediaType *t = NULL;
    if (FAILED(a->media_type(&t)) || !t) {
        if (t) IMFMediaType_Release(t);
        return 0;
    }
#define WIN_AMR_ATTR(call) do { if (FAILED(call)) goto bad; } while (0)
    WIN_AMR_ATTR(IMFMediaType_SetGUID(t, &win_amr_major, &win_amr_audio));
    WIN_AMR_ATTR(IMFMediaType_SetGUID(t, &win_amr_subtype, pcm ? &win_amr_pcm : &win_amr_nb));
    WIN_AMR_ATTR(IMFMediaType_SetUINT32(t, &win_amr_channels, 1));
    WIN_AMR_ATTR(IMFMediaType_SetUINT32(t, &win_amr_rate, 8000));
    if (pcm) {
        WIN_AMR_ATTR(IMFMediaType_SetUINT32(t, &win_amr_bits, 16));
        WIN_AMR_ATTR(IMFMediaType_SetUINT32(t, &win_amr_align, 2));
        WIN_AMR_ATTR(IMFMediaType_SetUINT32(t, &win_amr_average, 16000));
    }
#undef WIN_AMR_ATTR
    *out = t;
    return 1;
bad:
    IMFMediaType_Release(t);
    return 0;
}
static int win_amr_format(IMFTransform *m, DWORD output_id)
{
    IMFMediaType *t = NULL;
    GUID major, sub;
    UINT32 rate = 0, channels = 0, bits = 0, align = 0;
    int good = SUCCEEDED(IMFTransform_GetOutputCurrentType(m, output_id, &t)) && t;
    if (good) good = SUCCEEDED(IMFMediaType_GetGUID(t, &win_amr_major, &major)) &&
        !memcmp(&major, &win_amr_audio, sizeof(GUID)) &&
        SUCCEEDED(IMFMediaType_GetGUID(t, &win_amr_subtype, &sub)) &&
        !memcmp(&sub, &win_amr_pcm, sizeof(GUID)) &&
        SUCCEEDED(IMFMediaType_GetUINT32(t, &win_amr_rate, &rate)) && rate == 8000 &&
        SUCCEEDED(IMFMediaType_GetUINT32(t, &win_amr_channels, &channels)) && channels == 1 &&
        SUCCEEDED(IMFMediaType_GetUINT32(t, &win_amr_bits, &bits)) && bits == 16 &&
        SUCCEEDED(IMFMediaType_GetUINT32(t, &win_amr_align, &align)) && align == 2;
    if (t) IMFMediaType_Release(t);
    return good;
}
/* SDK sample/buffer objects use their API ownership rules; our persistent
 * handle and PCM alone use the caller's copied alloc/free hooks. */
static IMFSample *win_amr_sample(const win_amr_api *a, DWORD cap, DWORD alignment,
                                 const nj_u1 *data, DWORD bytes, LONGLONG pts)
{
    IMFSample *s = NULL;
    IMFMediaBuffer *b = NULL;
    BYTE *p = NULL;
    DWORD max = 0;
    int good = 0, locked = 0;
    if (FAILED(a->sample(&s)) || !s ||
        FAILED(a->buffer(cap, alignment, &b)) || !b) goto done;
    if (data) {
        if (FAILED(IMFMediaBuffer_Lock(b, &p, &max, NULL))) goto done;
        locked = 1;
        if (!p || bytes > max) goto done;
        memcpy(p, data, bytes);
        locked = 0;
        if (FAILED(IMFMediaBuffer_Unlock(b)) ||
            FAILED(IMFMediaBuffer_SetCurrentLength(b, bytes))) goto done;
    }
    if (FAILED(IMFSample_AddBuffer(s, b))) goto done;
    if (data && (FAILED(IMFSample_SetSampleTime(s, pts)) ||
                 FAILED(IMFSample_SetSampleDuration(s, 200000)))) goto done;
    good = 1;
done:
    if (locked) (void)IMFMediaBuffer_Unlock(b);
    if (b) IMFMediaBuffer_Release(b);
    if (!good && s) { IMFSample_Release(s); s = NULL; }
    return s;
}
static int win_amr_copy(IMFSample *s, win_amr_state *st, nj_u4 *written)
{
    IMFMediaBuffer *b = NULL;
    BYTE *p = NULL;
    DWORD max = 0, bytes = 0;
    int locked = 0, good = 0;
    if (!s || FAILED(IMFSample_ConvertToContiguousBuffer(s, &b)) || !b) goto done;
    if (FAILED(IMFMediaBuffer_Lock(b, &p, &max, &bytes))) goto done;
    locked = 1;
    if (bytes > max || bytes > st->len - *written || (bytes & 1u) || (bytes && !p)) goto done;
    if (bytes) memcpy(st->pcm + *written, p, bytes);
    *written += bytes;
    good = 1;
done:
    if (locked && FAILED(IMFMediaBuffer_Unlock(b))) good = 0;
    if (b) IMFMediaBuffer_Release(b);
    return good;
}
static int win_amr_run(const win_amr_api *a, IMFTransform *m,
                        const nj_u1 *data, nj_u4 len, win_amr_state *st,
                        DWORD started_at)
{
    DWORD inputs = 0, outputs = 0, input_id = 0, output_id = 0;
    MFT_OUTPUT_STREAM_INFO oi;
    MFT_INPUT_STREAM_INFO ii;
    IMFMediaType *in = NULL, *out = NULL;
    IMFSample *pending = NULL;
    nj_u4 off = 6, frame = 0, written = 0, idle = 0;
    nj_u4 budget = st->len / 320u * 8u + 256u;
    int started = 0, draining = 0, good = 0;
    HRESULT hr;
    if (FAILED(IMFTransform_GetStreamCount(m, &inputs, &outputs)) || inputs != 1 || outputs != 1) goto done;
    hr = IMFTransform_GetStreamIDs(m, 1, &input_id, 1, &output_id);
    if (hr == E_NOTIMPL) input_id = output_id = 0;
    else if (FAILED(hr)) goto done;
    if (!win_amr_type(a, &in, 0) || !win_amr_type(a, &out, 1) ||
        FAILED(IMFTransform_SetInputType(m, input_id, in, 0)) ||
        FAILED(IMFTransform_SetOutputType(m, output_id, out, 0)) ||
        !win_amr_format(m, output_id)) goto done;
    memset(&oi, 0, sizeof(oi));
    memset(&ii, 0, sizeof(ii));
    if (FAILED(IMFTransform_GetOutputStreamInfo(m, output_id, &oi)) ||
        FAILED(IMFTransform_GetInputStreamInfo(m, input_id, &ii)) ||
        oi.cbSize > NJ_SYSTEM_AMR_MAX_PCM_BYTES || ii.cbSize > NJ_SYSTEM_AMR_MAX_PCM_BYTES ||
        oi.cbAlignment > 65536u || (oi.cbAlignment && (oi.cbAlignment & (oi.cbAlignment - 1u))) ||
        ii.cbAlignment > 65536u || (ii.cbAlignment && (ii.cbAlignment & (ii.cbAlignment - 1u)))) goto done;
    /* Stream info gives byte alignment; MFCreateAlignedMemoryBuffer takes
     * the alignment-minus-one mask, with zero also meaning no requirement. */
    if (FAILED(IMFTransform_ProcessMessage(m, MFT_MESSAGE_COMMAND_FLUSH, 0)) ||
        FAILED(IMFTransform_ProcessMessage(m, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0))) goto done;
    started = 1;
    if (FAILED(IMFTransform_ProcessMessage(m, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0))) goto done;
    while (budget--) {
        MFT_OUTPUT_DATA_BUFFER result;
        IMFSample *supplied = NULL;
        DWORD status = 0;
        nj_u4 before = written;
        int progress = 0;
        /* A synchronous OS call cannot be forcibly interrupted; cap elapsed
         * time between calls and every loop, including zero-byte successes. */
        if ((DWORD)(GetTickCount() - started_at) > 10000u) goto done;
        if (off < len) {
            nj_u4 n = win_amr_packet(data[off]);
            if (!pending) pending = win_amr_sample(a, ii.cbSize > n ? ii.cbSize : n,
                ii.cbAlignment ? ii.cbAlignment - 1u : 0u,
                data + off, n, (LONGLONG)frame * 200000);
            if (!pending) goto done;
            hr = IMFTransform_ProcessInput(m, input_id, pending, 0);
            if (SUCCEEDED(hr)) {
                IMFSample_Release(pending); pending = NULL;
                off += n; ++frame; progress = 1;
            } else if (hr != MF_E_NOTACCEPTING) goto done;
        } else if (!draining) {
            if (FAILED(IMFTransform_ProcessMessage(m, MFT_MESSAGE_NOTIFY_END_OF_STREAM, input_id)) ||
                FAILED(IMFTransform_ProcessMessage(m, MFT_MESSAGE_COMMAND_DRAIN, 0))) goto done;
            draining = 1; progress = 1;
        }
        memset(&result, 0, sizeof(result));
        result.dwStreamID = output_id;
        if (!(oi.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            DWORD cap = oi.cbSize > 320u ? oi.cbSize : 320u;
            supplied = win_amr_sample(a, cap, oi.cbAlignment ? oi.cbAlignment - 1u : 0u, NULL, 0, 0);
            if (!supplied) goto done;
            result.pSample = supplied;
        }
        hr = IMFTransform_ProcessOutput(m, 0, 1, &result, &status);
        if (result.pEvents) IMFCollection_Release(result.pEvents);
        if (SUCCEEDED(hr)) {
            if (status || (result.dwStatus & ~MFT_OUTPUT_DATA_BUFFER_INCOMPLETE) ||
                !win_amr_format(m, output_id) || !win_amr_copy(result.pSample, st, &written)) hr = E_FAIL;
        }
        /* Caller-owned samples remain caller-owned on all ProcessOutput paths.
         * A provider-owned replacement has its own reference to release. */
        if (result.pSample && result.pSample != supplied) IMFSample_Release(result.pSample);
        if (supplied) IMFSample_Release(supplied);
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            if (draining) { good = off == len && written == st->len; goto done; }
        } else if (FAILED(hr)) goto done; /* format changes require a new open/fallback */
        progress = progress || written != before;
        idle = progress ? 0u : idle + 1u;
        if (idle >= 64u) goto done;
    }
done:
    if (pending) IMFSample_Release(pending);
    if (started) {
        if (FAILED(IMFTransform_ProcessMessage(m, MFT_MESSAGE_NOTIFY_END_STREAMING, 0))) good = 0;
        if (FAILED(IMFTransform_ProcessMessage(m, MFT_MESSAGE_COMMAND_FLUSH, 0))) good = 0;
    }
    if (out) IMFMediaType_Release(out);
    if (in) IMFMediaType_Release(in);
    return good;
}
static nj_av_dec *win_amr_open(const nj_u1 *data, nj_u4 len, const nj_av_mem *mem,
                                nj_av_info *info, const char **error)
{
    win_amr_api a;
    win_amr_state *st = NULL;
    IMFActivate **list = NULL;
    MFT_REGISTER_TYPE_INFO input, output;
    UINT32 count = 0, i;
    nj_u4 off, frames = 0;
    DWORD started_at = GetTickCount();
    HRESULT hr;
    int co_owned = 0, mf_started = 0, good = 0;
    if (error) *error = "Windows AMR system decoder unavailable or rejected input";
    if (!win_amr_accepts(data, len) || !mem || !mem->alloc || !mem->free || !info) return NULL;
    for (off = 6; off < len;) {
        nj_u4 n = win_amr_packet(data[off]);
        if (!n || n > len - off || frames >= NJ_SYSTEM_AMR_MAX_PCM_BYTES / 320u ||
            frames >= 0xffffffffu / 320u) return NULL;
        off += n; ++frames;
    }
    if (!frames) return NULL;
    if (!win_amr_load(&a)) goto done;
    hr = a.co_init(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) goto done;
    co_owned = SUCCEEDED(hr);
    if (FAILED(a.startup(MF_VERSION, MFSTARTUP_LITE))) goto done;
    mf_started = 1;
    input.guidMajorType = output.guidMajorType = win_amr_audio;
    input.guidSubtype = win_amr_nb; output.guidSubtype = win_amr_pcm;
    if (FAILED(a.enumerate(win_amr_category,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
        &input, &output, &list, &count)) || !list || !count || count > 64u) goto done;
    st = (win_amr_state *)mem->alloc(mem->ud, (nj_u4)sizeof(*st));
    if (!st) goto done;
    memset(st, 0, sizeof(*st)); st->mem = *mem;
    st->len = frames * 320u;
    st->pcm = (nj_u1 *)mem->alloc(mem->ud, st->len);
    if (!st->pcm) goto done;
    for (i = 0; i < count && !good; ++i) {
        IMFTransform *m = NULL;
        if ((DWORD)(GetTickCount() - started_at) > 10000u) break;
        if (!list[i]) continue;
        hr = IMFActivate_ActivateObject(list[i], &win_amr_transform_iid, (void **)&m);
        if (SUCCEEDED(hr) && m) good = win_amr_run(&a, m, data, len, st, started_at);
        if (m) IMFTransform_Release(m);
        if (FAILED(IMFActivate_ShutdownObject(list[i]))) good = 0;
    }
done:
    if (list) {
        for (i = 0; i < count; ++i) if (list[i]) IMFActivate_Release(list[i]);
        if (a.co_free) a.co_free(list);
    }
    if (mf_started && FAILED(a.shutdown())) good = 0;
    if (co_owned) a.co_uninit();
    if (a.ole) FreeLibrary(a.ole);
    if (a.mf) FreeLibrary(a.mf);
    if (!good) {
        if (st) { if (st->pcm) mem->free(mem->ud, st->pcm); mem->free(mem->ud, st); }
        return NULL;
    }
    memset(info, 0, sizeof(*info));
    info->format = NJ_AV_AUDIO_AMR; info->rate = 8000; info->channels = 1;
    info->frames = frames * 160u; info->duration_ms = frames * 20u;
    if (error) *error = NULL;
    return (nj_av_dec *)st;
}
static int win_amr_decode(nj_av_dec *dec, nj_av_step *step)
{
    win_amr_state *st = (win_amr_state *)dec;
    nj_u4 n;
    if (!st || !step) return NJ_AV_ERR_CORRUPT;
    step->in_used = step->out_used = step->frames = 0;
    step->time_us = (nj_s8)(st->off / 2u) * 125;
    if (st->off == st->len) return NJ_AV_ERR_END;
    n = st->len - st->off;
    if (n > step->out_cap) n = step->out_cap;
    n &= ~1u;
    if (!step->out || !n) return NJ_AV_ERR_AGAIN;
    memcpy(step->out, st->pcm + st->off, n); st->off += n;
    step->out_used = n; step->frames = n / 2u;
    return NJ_AV_OK;
}
static int win_amr_seek(nj_av_dec *dec, nj_s8 time_us)
{
    win_amr_state *st = (win_amr_state *)dec;
    if (!st) return NJ_AV_ERR_CORRUPT;
    if (time_us <= 0) st->off = 0;
    else if (time_us >= (nj_s8)(st->len / 320u) * 20000) st->off = st->len;
    else st->off = (nj_u4)(time_us / 20000) * 320u;
    return NJ_AV_OK;
}
static void win_amr_close(nj_av_dec *dec)
{
    win_amr_state *st = (win_amr_state *)dec;
    if (st) { nj_av_mem mem = st->mem; mem.free(mem.ud, st->pcm); mem.free(mem.ud, st); }
}
const nj_av_audio_ops *nj_avcodec_system_amr(void)
{
    static const nj_av_audio_ops ops = {
        "system-amr-windows", NJ_AV_AUDIO_AMR, win_amr_accepts, win_amr_open,
        win_amr_decode, win_amr_seek, win_amr_close, 2u
    };
    return &ops;
}
