/*
 * AMR complete-clip system/provider fallback contract (MIT).
 * No platform codec or external fixture is required. The synthetic 3GP fixture
 * is adapted in memory to contain five complete AMR blocks; expected fallback
 * PCM comes from an independent invocation of the built-in whole-file decoder.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_avcodec.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include "nj_amr.h"
#if NJ_ENABLE_MEDIA
#include "private/amr_movie_fixture.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLAYER "javax/microedition/media/Player"
#define BLOCKS 5u
#define PCM_BYTES (BLOCKS * 320u)
#define AMR_BYTES (6u + BLOCKS * 13u)
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "%s:%d: %s [%s]\n", __FILE__, __LINE__, #c, case_name); \
    failures++; } } while (0)

static int checks, failures;
static const char *case_name = "setup";

#if NJ_ENABLE_AMR
enum behavior {
    COMPLETE, PROGRESS_AGAIN, PRELOADED_COMPLETE, OPEN_FAIL, BAD_RATE,
    BAD_CHANNELS, BAD_FORMAT,
    PARTIAL_ERROR, PARTIAL_STALL, PARTIAL_END, SHORT_END, STALL, INPUT_AGAIN,
    BAD_INPUT_USED, BAD_OUTPUT_USED, BAD_FRAME_COUNT, ODD_OUTPUT
};
enum hook_behavior {
    HOOK_NONE, HOOK_COMPLETE, HOOK_QUERY_FAIL, HOOK_BAD_SIZE, HOOK_BAD_RATE,
    HOOK_BAD_CHANNELS, HOOK_PARTIAL_ERROR, HOOK_SHORT, HOOK_RENDER_BAD_RATE,
    HOOK_RENDER_BAD_CHANNELS
};
typedef struct provider_probe {
    enum behavior mode;
    int opens, decodes, closes;
} provider_probe;
typedef struct environment {
    nj_port port;
    nj_vm *vm;
    provider_probe probe[2];
    enum hook_behavior hook;
    int queries, renders, allocations, frees, live, fail_next;
    int written, room, pcm_opens, pcm_closes;
    nj_u1 output[PCM_BYTES * 3u];
} environment;
/* Test callbacks are serial; the headless port retains ownership of port.ud. */
static environment *active;
static nj_u1 amr[AMR_BYTES], software[PCM_BYTES];

static void *count_alloc(void *ud, nj_size bytes)
{
    void *p;
    (void)ud;
    if (active->fail_next) { active->fail_next = 0; return NULL; }
    p = malloc(bytes);
    if (p) { active->allocations++; active->live++; }
    return p;
}
static void count_free(void *ud, void *p)
{
    (void)ud;
    if (p) { active->frees++; active->live--; CHECK(active->live >= 0); free(p); }
}
static nj_u1 sentinel(int identity, nj_u4 offset)
{
    /* Positive, varying s16le samples, distinct for each provider/the port. */
    nj_u4 sample = (nj_u4)(1200 + identity * 1000) + (offset / 2u) % 701u;
    return (nj_u1)((offset & 1u) ? sample >> 8 : sample);
}
static void fill_sentinel(nj_u1 *out, nj_u4 n, int identity, nj_u4 offset)
{
    nj_u4 i;
    for (i = 0; i < n; ++i) out[i] = sentinel(identity, offset + i);
}
static int is_sentinel(const nj_u1 *out, nj_u4 n, int identity, nj_u4 offset)
{
    nj_u4 i;
    for (i = 0; i < n; ++i) if (out[i] != sentinel(identity, offset + i)) return 0;
    return 1;
}
static int accepts(const nj_u1 *data, nj_u4 len)
{ return data && len >= 6u && memcmp(data, "#!AMR\n", 6u) == 0; }

typedef struct fake_state {
    /* Copy the hooks: open()'s nj_av_mem argument is borrowed, not permanent. */
    nj_av_mem mem;
    int identity, calls;
    nj_u4 offset, input_len;
    nj_u1 input[AMR_BYTES];
} fake_state;

static nj_av_dec *open_provider(int identity, const nj_u1 *data, nj_u4 len,
                               const nj_av_mem *mem, nj_av_info *info,
                               const char **err)
{
    provider_probe *p = &active->probe[identity];
    fake_state *st;
    p->opens++;
    CHECK(len == AMR_BYTES && memcmp(data, amr, AMR_BYTES) == 0);
    CHECK(mem && mem->alloc && mem->free && mem->ud == active->vm);
    if (p->mode == OPEN_FAIL) { if (err) *err = "test open failure"; return NULL; }
    st = mem->alloc(mem->ud, (nj_u4)sizeof(*st));
    if (!st) { if (err) *err = "test allocation failure"; return NULL; }
    memset(st, 0, sizeof(*st));
    st->mem = *mem; st->identity = identity; st->input_len = len;
    memcpy(st->input, data, len);
    info->format = p->mode == BAD_FORMAT ? NJ_AV_AUDIO_MP3 : NJ_AV_AUDIO_AMR;
    info->rate = p->mode == BAD_RATE ? 16000u : 8000u;
    info->channels = p->mode == BAD_CHANNELS ? 2u : 1u;
    info->frames = BLOCKS * 160u; info->duration_ms = BLOCKS * 20u;
    return (nj_av_dec *)st;
}
static nj_av_dec *open_first(const nj_u1 *d, nj_u4 n, const nj_av_mem *m,
                             nj_av_info *i, const char **e)
{ return open_provider(0, d, n, m, i, e); }
static nj_av_dec *open_second(const nj_u1 *d, nj_u4 n, const nj_av_mem *m,
                              nj_av_info *i, const char **e)
{ return open_provider(1, d, n, m, i, e); }
static int decode_provider(nj_av_dec *dec, nj_av_step *step)
{
    fake_state *st = (fake_state *)dec;
    provider_probe *p = &active->probe[st->identity];
    nj_u4 take;
    st->calls++; p->decodes++;
    CHECK(memcmp(st->input, amr, AMR_BYTES) == 0);
    CHECK(step->out_cap == PCM_BYTES - st->offset);
    /* A broken caller must fail the bounded-AGAIN assertion, never hang CI. */
    if (st->calls > 16) return NJ_AV_ERR_CORRUPT;
    if (p->mode == STALL) return NJ_AV_ERR_AGAIN;
    if (p->mode == INPUT_AGAIN) {
        if (st->calls == 1) step->in_used = step->in_len;
        return NJ_AV_ERR_AGAIN;
    }
    if (st->calls > 1) {
        CHECK(step->in_len == (p->mode == PRELOADED_COMPLETE ? st->input_len : 0u));
        if (p->mode == PARTIAL_ERROR) return NJ_AV_ERR_CORRUPT;
        if (p->mode == PARTIAL_STALL) return NJ_AV_ERR_AGAIN;
        if (p->mode == PARTIAL_END || p->mode == SHORT_END) return NJ_AV_ERR_END;
    }
    take = step->out_cap < 218u ? step->out_cap : 218u;
    if (p->mode == SHORT_END) take = step->out_cap - 2u;
    CHECK(step->out != NULL && take > 0u);
    fill_sentinel(step->out, take, st->identity, st->offset);
    /* Some real decoders preload the whole clip in open() and ignore step.in. */
    step->in_used = st->calls == 1 && p->mode != PRELOADED_COMPLETE ? st->input_len : 0u;
    step->out_used = take; step->frames = take / 2u;
    st->offset += take;
    if (p->mode == BAD_INPUT_USED) step->in_used = step->in_len + 1u;
    if (p->mode == BAD_OUTPUT_USED) step->out_used = step->out_cap + 2u;
    if (p->mode == BAD_FRAME_COUNT) step->frames = step->out_cap / 2u + 1u;
    if (p->mode == ODD_OUTPUT) step->out_used--;
    return p->mode == PROGRESS_AGAIN ? NJ_AV_ERR_AGAIN : NJ_AV_OK;
}
static void close_provider(nj_av_dec *dec)
{
    fake_state *st = (fake_state *)dec;
    nj_av_mem mem = st->mem;
    active->probe[st->identity].closes++;
    mem.free(mem.ud, st);
}
static const nj_av_audio_ops first_ops = {
    "test-first", NJ_AV_AUDIO_AMR, accepts, open_first, decode_provider,
    NULL, close_provider, 2u
};
static const nj_av_audio_ops second_ops = {
    "test-second", NJ_AV_AUDIO_AMR, accepts, open_second, decode_provider,
    NULL, close_provider, 2u
};

static nj_status decode_hook(void *ud, const nj_u1 *in, nj_u4 len,
                            nj_u1 *out, nj_u4 cap, nj_u4 *bytes,
                            nj_u4 *rate, nj_u4 *channels)
{
    enum hook_behavior mode = active->hook;
    (void)ud;
    CHECK(len == AMR_BYTES && memcmp(in, amr, AMR_BYTES) == 0);
    *bytes = PCM_BYTES; *rate = 8000u; *channels = 1u;
    if (!out) {
        active->queries++; CHECK(cap == 0u);
        if (mode == HOOK_QUERY_FAIL) return NJ_ERR_UNSUPPORTED;
        if (mode == HOOK_BAD_SIZE) *bytes -= 2u;
        if (mode == HOOK_BAD_RATE) *rate = 16000u;
        if (mode == HOOK_BAD_CHANNELS) *channels = 2u;
        return NJ_OK;
    }
    active->renders++; CHECK(cap == PCM_BYTES);
    if (mode == HOOK_PARTIAL_ERROR || mode == HOOK_SHORT) {
        fill_sentinel(out, 218u, 2, 0u); *bytes = 218u;
        return mode == HOOK_PARTIAL_ERROR ? NJ_ERR_FORMAT : NJ_OK;
    }
    fill_sentinel(out, cap, 2, 0u);
    if (mode == HOOK_RENDER_BAD_RATE) *rate = 16000u;
    if (mode == HOOK_RENDER_BAD_CHANNELS) *channels = 2u;
    return NJ_OK;
}
static int pcm_open(void *ud, int rate, int channels)
{ (void)ud; CHECK(rate == 8000 && channels == 1); active->pcm_opens++; return 0; }
static int pcm_room(void *ud) { (void)ud; return active->room; }
static int pcm_write(void *ud, const nj_s2 *data, int frames)
{
    nj_u4 n;
    (void)ud;
    if (frames > active->room) frames = active->room;
    n = (nj_u4)frames * 2u;
    CHECK(active->written + (int)n <= (int)sizeof(active->output));
    if (active->written + (int)n > (int)sizeof(active->output)) return -1;
    memcpy(active->output + active->written, data, n);
    active->written += (int)n;
    return frames;
}
static void pcm_close(void *ud) { (void)ud; active->pcm_closes++; }
static int begin(environment *e)
{
    memset(e, 0, sizeof(*e)); active = e; e->room = 128;
    CHECK(nj_port_headless(&e->port) == NJ_OK);
    e->port.alloc = count_alloc; e->port.free = count_free;
    e->port.audio_decode = NULL;
    e->port.pcm_open = pcm_open; e->port.pcm_free = pcm_room;
    e->port.pcm_write = pcm_write; e->port.pcm_close = pcm_close;
    e->vm = nj_vm_create(&e->port, 2u * 1024u * 1024u);
    CHECK(e->vm != NULL);
    if (!e->vm) { nj_port_shutdown(&e->port); return 0; }
    /* Deterministic on hosts that really do have a system AMR decoder. */
    CHECK(nj_avcodec_install_audio(e->vm, NULL) == 0);
    return 1;
}
static void end(environment *e)
{
    nj_vm_destroy(e->vm);
    CHECK(e->live == 0 && e->allocations == e->frees);
    nj_port_shutdown(&e->port);
    active = NULL;
}
static void install(environment *e, int identity)
{
    CHECK(nj_avcodec_install_audio(e->vm, identity ? &second_ops : &first_ops) > 0);
}
static int all_byte(const nj_u1 *p, size_t n, nj_u1 v)
{
    size_t i;
    for (i = 0; i < n; ++i) if (p[i] != v) return 0;
    return 1;
}
static void decode_checked(environment *e, int identity)
{
    nj_u1 guarded[PCM_BYTES + 32u];
    int live = e->live, allocs = e->allocations, frees = e->frees;
    memset(guarded, 0xa5, sizeof(guarded));
    CHECK(nj_avcodec_decode_amr(e->vm, amr, sizeof(amr), guarded + 16u,
                                PCM_BYTES + 16u) == BLOCKS);
    CHECK(all_byte(guarded, 16u, 0xa5) &&
          all_byte(guarded + 16u + PCM_BYTES, 16u, 0xa5));
    if (identity >= 0) CHECK(is_sentinel(guarded + 16u, PCM_BYTES, identity, 0u));
    else CHECK(memcmp(guarded + 16u, software, PCM_BYTES) == 0);
    /* Cleanup must happen before return, not merely when the VM is destroyed. */
    CHECK(e->live == live && e->allocations - allocs == e->frees - frees);
}
static void test_providers(void)
{
    static const struct { enum behavior mode; const char *name; int calls; } cases[] = {
        { COMPLETE, "complete multichunk provider", 8 },
        { PROGRESS_AGAIN, "AGAIN with output progress completes", 8 },
        { PRELOADED_COMPLETE, "preloaded decoder may ignore step.in", 8 },
        { OPEN_FAIL, "open failure", 0 }, { BAD_RATE, "bad provider rate", 0 },
        { BAD_CHANNELS, "bad provider channels", 0 },
        { BAD_FORMAT, "bad provider format", 0 },
        { PARTIAL_ERROR, "partial then error restarts original", 2 },
        { PARTIAL_STALL, "partial then stalled AGAIN restarts original", 2 },
        { PARTIAL_END, "partial then END restarts original", 2 },
        { SHORT_END, "undersized provider output", 2 },
        { STALL, "zero-progress AGAIN is bounded", 1 },
        { INPUT_AGAIN, "input progress then AGAIN is bounded", 2 },
        { BAD_INPUT_USED, "provider overreports input", 1 },
        { BAD_OUTPUT_USED, "provider overreports output", 1 },
        { BAD_FRAME_COUNT, "provider overreports frames", 1 },
        { ODD_OUTPUT, "provider bytes disagree with PCM frames", 1 }
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        environment e;
        case_name = cases[i].name;
        if (!begin(&e)) continue;
        e.probe[0].mode = cases[i].mode; install(&e, 0);
        decode_checked(&e, cases[i].mode <= PRELOADED_COMPLETE ? 0 : -1);
        CHECK(e.probe[0].opens == 1 && e.probe[0].decodes == cases[i].calls);
        CHECK(e.probe[0].closes == (cases[i].mode == OPEN_FAIL ? 0 : 1));
        if (cases[i].mode == STALL || cases[i].mode == INPUT_AGAIN ||
            cases[i].mode == PARTIAL_STALL) CHECK(e.probe[0].decodes <= 2);
        end(&e);
    }
}
static void test_priority_and_memory(void)
{
    environment e;
    case_name = "later provider first, failed one retries earlier from original";
    if (!begin(&e)) return;
    install(&e, 0); install(&e, 1);
    decode_checked(&e, 1);
    CHECK(e.probe[1].opens == 1 && e.probe[0].opens == 0);
    e.probe[1].mode = PARTIAL_ERROR;
    decode_checked(&e, 0);
    CHECK(e.probe[1].opens == 2 && e.probe[1].closes == 2 &&
          e.probe[0].opens == 1 && e.probe[0].closes == 1);
    e.probe[0].mode = PARTIAL_END;
    decode_checked(&e, -1); /* both overwrite a prefix, neither may poison SW */
    end(&e);

    case_name = "provider VM allocation fails without leaking";
    if (!begin(&e)) return;
    install(&e, 0); e.fail_next = 1;
    decode_checked(&e, -1);
    CHECK(e.fail_next == 0 && e.probe[0].opens == 1 &&
          e.probe[0].closes == 0 && e.probe[0].decodes == 0);
    end(&e);
}
static void test_port_hook(void)
{
    int i;
    for (i = HOOK_COMPLETE; i <= HOOK_RENDER_BAD_CHANNELS; ++i) {
        environment e;
        case_name = "port hook validates query/render, then provider priority";
        if (!begin(&e)) continue;
        e.hook = (enum hook_behavior)i; e.port.audio_decode = decode_hook;
        install(&e, 0);
        decode_checked(&e, i == HOOK_COMPLETE ? 2 : 0);
        CHECK(e.queries == 1);
        CHECK(e.renders == (i >= HOOK_PARTIAL_ERROR || i == HOOK_COMPLETE ? 1 : 0));
        CHECK(e.probe[0].opens == (i == HOOK_COMPLETE ? 0 : 1));
        end(&e);
    }
    {
        environment e;
        case_name = "partial failed port and provider both restart in software";
        if (!begin(&e)) return;
        e.hook = HOOK_PARTIAL_ERROR; e.port.audio_decode = decode_hook;
        e.probe[0].mode = PARTIAL_ERROR; install(&e, 0);
        decode_checked(&e, -1);
        CHECK(e.renders == 1 && e.probe[0].decodes == 2);
        end(&e);
    }
}
static void test_invalid_arguments(void)
{
    environment e;
    nj_u1 out[PCM_BYTES + 16u], invalid[AMR_BYTES];
    case_name = "invalid/null/short buffers never write or open a decoder";
    if (!begin(&e)) return;
    install(&e, 0); e.hook = HOOK_COMPLETE; e.port.audio_decode = decode_hook;
    memset(out, 0x6d, sizeof(out)); memcpy(invalid, amr, sizeof(invalid)); invalid[0] = '!';
    CHECK(nj_avcodec_decode_amr(NULL, amr, sizeof(amr), out, sizeof(out)) == 0u);
    CHECK(nj_avcodec_decode_amr(e.vm, NULL, sizeof(amr), out, sizeof(out)) == 0u);
    CHECK(nj_avcodec_decode_amr(e.vm, amr, sizeof(amr), NULL, sizeof(out)) == 0u);
    CHECK(nj_avcodec_decode_amr(e.vm, amr, sizeof(amr), out, PCM_BYTES - 1u) == 0u);
    CHECK(nj_avcodec_decode_amr(e.vm, amr, sizeof(amr), out, 0u) == 0u);
    CHECK(nj_avcodec_decode_amr(e.vm, invalid, sizeof(invalid), out, sizeof(out)) == 0u);
    CHECK(nj_avcodec_decode_amr(e.vm, amr, 6u, out, sizeof(out)) == 0u);
    CHECK(all_byte(out, sizeof(out), 0x6d));
    {
        static const nj_u4 input_at[] = { 0u, 0u, 16u };
        static const nj_u4 output_at[] = { 0u, 16u, 0u };
        nj_u1 shared[PCM_BYTES + AMR_BYTES + 32u], before[sizeof(shared)];
        size_t i;
        for (i = 0; i < sizeof(input_at) / sizeof(input_at[0]); ++i) {
            /* Exact alias, output within input, and input within output. */
            memset(shared, 0x6d, sizeof(shared));
            memcpy(shared + input_at[i], amr, sizeof(amr));
            memcpy(before, shared, sizeof(before));
            CHECK(nj_avcodec_decode_amr(e.vm, shared + input_at[i], AMR_BYTES,
                    shared + output_at[i], PCM_BYTES) == 0u);
            CHECK(memcmp(shared, before, sizeof(shared)) == 0);
        }
    }
    CHECK(e.queries == 0 && e.renders == 0 && e.probe[0].opens == 0);
    end(&e);
}

#if NJ_ENABLE_MEDIA
static void make_container(nj_u1 *p, int audio_only)
{
    CHECK(nj_test_amr_movie_fixture(p, audio_only));
    CHECK(nj_amr_mp4_frames(p, NJ_TEST_AMR_MOVIE_BYTES) == BLOCKS);
}
static nj_ref make_player(nj_vm *vm, const nj_u1 *data, nj_u4 bytes)
{
    nj_ref array, stream;
    nj_value args[2], ret;
    array = nj_root(vm, nj_byte_array_new(vm, bytes));
    stream = nj_root(vm, nj_object_new(vm, "java/io/ByteArrayInputStream"));
    CHECK(array && stream); if (!array || !stream) return NULL;
    memcpy(nj_array_data(array), data, bytes);
    args[0].r = array;
    CHECK(nj_call_special(vm, stream, "java/io/ByteArrayInputStream", "<init>",
                          "([B)V", args, NULL) == NJ_OK);
    args[0].r = stream; args[1].r = NULL; memset(&ret, 0, sizeof(ret));
    CHECK(nj_call_static(vm, "javax/microedition/media/Manager", "createPlayer",
          "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;",
          args, &ret) == NJ_OK);
    return nj_root(vm, ret.r);
}
static nj_ref player_pcm(nj_vm *vm, nj_ref player)
{
    nj_field *f = nj_field_lookup(vm, PLAYER, "pData", "[B", 0);
    CHECK(f != NULL); return f ? nj_field_read_ref(vm, player, f) : NULL;
}
static void pump_player(environment *e, nj_ref player)
{
    nj_value ret;
    int i;
    memset(&ret, 0, sizeof(ret));
    for (i = 0; i < 32; ++i) {
        nj_media_frame(e->vm);
        CHECK(nj_call(e->vm, player, PLAYER, "getState", "()I", NULL, &ret) == NJ_OK);
        if (ret.i != 400) break;
    }
    CHECK(i < 32 && ret.i == 300);
}
static void test_media_routes(void)
{
    nj_u1 container[NJ_TEST_AMR_MOVIE_BYTES];
    int layout, fallback;
    for (layout = 0; layout < 2; ++layout) for (fallback = 0; fallback < 2; ++fallback) {
        environment e;
        nj_ref player, pcm;
        nj_value args[2], ret;
        const nj_u1 *expected;
        nj_u1 system[PCM_BYTES];
        int calls;
        case_name = layout ? "audio-only 3GP Player uses unified AMR policy" :
                             "bare AMR Player uses unified AMR policy";
        if (!begin(&e)) continue;
        e.probe[0].mode = fallback ? PARTIAL_ERROR : COMPLETE; install(&e, 0);
        if (layout) make_container(container, 1);
        player = make_player(e.vm, layout ? container : amr,
                              layout ? (nj_u4)sizeof(container) : (nj_u4)sizeof(amr));
        CHECK(player != NULL); if (!player) { end(&e); continue; }
        CHECK(e.probe[0].opens == 1 && e.probe[0].closes == 1);
        CHECK(e.probe[0].decodes == (fallback ? 2 : 8));
        pcm = player_pcm(e.vm, player);
        fill_sentinel(system, PCM_BYTES, 0, 0u); expected = fallback ? software : system;
        CHECK(pcm && nj_array_length(pcm) == PCM_BYTES);
        if (pcm) CHECK(memcmp(nj_array_data(pcm), expected, PCM_BYTES) == 0);
        CHECK(nj_call(e.vm, player, PLAYER, "getDuration", "()J", NULL, &ret) == NJ_OK);
        CHECK(ret.j == 100000);
        calls = e.probe[0].opens;
        CHECK(nj_call(e.vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
        pump_player(&e, player);
        CHECK(e.written == (int)PCM_BYTES && memcmp(e.output, expected, PCM_BYTES) == 0);
        /* Seek/resume must use the chosen PCM, including after fallback; no
         * repeated provider decode or prefix from its failed first chunk. */
        e.written = 0; e.room = 0; args[0].j = 20000; args[1].bits = 0u;
        CHECK(nj_call(e.vm, player, PLAYER, "setMediaTime", "(J)J", args, &ret) == NJ_OK);
        CHECK(ret.j == 20000);
        CHECK(nj_call(e.vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
        CHECK(nj_call(e.vm, player, PLAYER, "stop", "()V", NULL, NULL) == NJ_OK);
        e.room = 128;
        CHECK(nj_call(e.vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
        pump_player(&e, player);
        CHECK(e.written == (int)PCM_BYTES - 320 &&
              memcmp(e.output, expected + 320u, PCM_BYTES - 320u) == 0);
        CHECK(e.probe[0].opens == calls);
        CHECK(nj_call(e.vm, player, PLAYER, "close", "()V", NULL, NULL) == NJ_OK);
        CHECK(e.pcm_opens > 0 && e.pcm_opens == e.pcm_closes);
        end(&e);
    }
#if NJ_ENABLE_VIDEO
    for (fallback = 0; fallback < 2; ++fallback) {
        environment e;
        nj_u1 pcm[PCM_BYTES + 16u];
        int live;
        case_name = "movie AMR track uses same complete provider/fallback PCM";
        if (!begin(&e)) continue;
        e.probe[0].mode = fallback ? PARTIAL_END : COMPLETE; install(&e, 0);
        make_container(container, 0); live = e.live;
        memset(pcm, 0xa5, sizeof(pcm));
        CHECK(nj_media_movie_amr_decode(e.vm, container, sizeof(container), pcm,
                                        sizeof(pcm)) == BLOCKS);
        if (fallback) CHECK(memcmp(pcm, software, PCM_BYTES) == 0);
        else CHECK(is_sentinel(pcm, PCM_BYTES, 0, 0u));
        CHECK(all_byte(pcm + PCM_BYTES, 16u, 0xa5));
        CHECK(e.probe[0].opens == 1 && e.probe[0].closes == 1 && e.live == live);
        end(&e);
    }
#endif
}
#endif /* NJ_ENABLE_MEDIA */
#endif /* NJ_ENABLE_AMR */
int main(void)
{
#if NJ_ENABLE_AMR
    nj_u4 i, j;
    memcpy(amr, "#!AMR\n", 6u);
    for (i = 0; i < BLOCKS; ++i) {
        amr[6u + i * 13u] = 0x04u;
        for (j = 0; j < 12u; ++j) amr[7u + i * 13u + j] = (nj_u1)(11u + j * 37u + i * 17u);
    }
    CHECK(nj_amr_frames(amr, sizeof(amr)) == BLOCKS);
    CHECK(nj_amr_decode(amr, sizeof(amr), software) == BLOCKS);
    CHECK(!all_byte(software, sizeof(software), 0u));
    CHECK(!is_sentinel(software, sizeof(software), 0, 0u));
    test_providers(); test_priority_and_memory(); test_port_hook();
    test_invalid_arguments();
#if NJ_ENABLE_MEDIA
    test_media_routes();
#endif
    printf("AMR fallback: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
#else
    puts("AMR fallback: disabled (NJ_ENABLE_AMR=0)");
    return 77;
#endif
}
