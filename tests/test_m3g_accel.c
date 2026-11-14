/* M3G public APIs -> exact candidate packets -> synchronous reference merge.
 * Compare full colour/depth storage with the original software renderer. The
 * real GLES tests separately prove that a graphics driver accepts these packets. */
#include "nj2me/nj_config.h"
#if NJ_ENABLE_M3G && NJ_ENABLE_GFX_ACCEL
#define main m3g_targets_fixture_main
#include "test_m3g_targets.c"
#undef main
#include "nj2me/nj_gfx_raster.h"

typedef struct accel_probe {
    target *target;
    unsigned calls, accepted, declined, occluded, row_jobs;
    int force_decline;
} accel_probe;
static unsigned worker_depth, worker_calls, allocation_failures;
static target *fail_alloc_target;

static void row_workers(void *ud, void (*work)(void *, nj_u4), void *context, nj_u4 parts)
{
    nj_u4 i;
    target *t = owner(ud);
    if (t->port.gfx_raster) ++((accel_probe *)t->port.gfx_raster_ud)->row_jobs;
    CHECK(parts == 2u);
    ++worker_calls;
    ++worker_depth;
    for (i = 0; i < parts; ++i) work(context, i);
    --worker_depth;
}
static void *fail_scratch_alloc(void *ud, nj_size n)
{
    if (owner(ud) == fail_alloc_target) {
        ++allocation_failures;
        return NULL;
    }
    return count_alloc(ud, n);
}
static int merge_packet(void *ud, const nj_gfx_raster *p, nj_u4 *drawn)
{
    accel_probe *probe = (accel_probe *)ud;
    unsigned outstanding = probe->target->outstanding;
    int result;
    CHECK(worker_depth == 0); /* GL must never be reached from parallel_for. */
    CHECK(p && drawn && p->surface->pixels == probe->target->surface.pixels);
    CHECK(p->depth == depth(probe->target));
    CHECK(p->depth_stride == W && p->key_stride == p->width);
    CHECK(nj_gfx_raster_validate(p));
    ++probe->calls;
    if (probe->force_decline) {
        ++probe->declined;
        return NJ_GFX_ACCEL_DECLINE;
    }
    result = nj_gfx_raster_software(p, drawn);
    CHECK(result == NJ_GFX_ACCEL_DONE && *drawn <= 1u);
    CHECK(probe->target->outstanding == outstanding);
    ++probe->accepted;
    if (!*drawn) ++probe->occluded;
    return result;
}
static void attach(target *t, accel_probe *p)
{
    memset(p, 0, sizeof(*p));
    p->target = t;
    t->port.gfx_raster = merge_packet;
    t->port.gfx_raster_ud = p;
}
static void flat(target *t, nj_u4 color)
{
    nj_value a[2];
    a[0].i = 0; a[1].r = NULL;
    call(t, t->app, C("Appearance"), "setTexture", "(IL" C("Texture2D") ";)V", a);
    integer(t, t->vb, C("VertexBuffer"), "setDefaultColor", "(I)V", (nj_s4)color);
}
static void same(target *a, target *b)
{
    compare(a, b);
    CHECK(memcmp(depth(a), depth(b), W * H * sizeof(nj_s4)) == 0);
    CHECK(a->vm->tris == b->vm->tris);
}
static void paired_draw(target *a, target *b, nj_u4 color, int z)
{
    flat(a, color); flat(b, color);
    geometry(a, z, 4); geometry(b, z, 4);
    render(a); render(b); same(a, b);
}
static void controls(target *t, int test, int write)
{
    integer(t, t->cm, C("CompositingMode"), "setDepthTestEnable", "(Z)V", test);
    integer(t, t->cm, C("CompositingMode"), "setDepthWriteEnable", "(Z)V", write);
}
static void clipped(target *t)
{
    nj_iput(t->vm, t->g, nj_field_lookup(t->vm, G, "gTx", "I", 0), 3);
    nj_iput(t->vm, t->g, nj_field_lookup(t->vm, G, "gTy", "I", 0), -2);
    nj_iput(t->vm, t->g, nj_field_lookup(t->vm, G, "gClipX", "I", 0), 11);
    nj_iput(t->vm, t->g, nj_field_lookup(t->vm, G, "gClipY", "I", 0), 13);
    nj_iput(t->vm, t->g, nj_field_lookup(t->vm, G, "gClipW", "I", 0), 180);
    nj_iput(t->vm, t->g, nj_field_lookup(t->vm, G, "gClipH", "I", 0), 190);
    bind(t); viewport(t, 5, 7, 200, 260);
}
static void supported_cases(int format)
{
    target gpu, cpu;
    accel_probe probe;
    unsigned before, workers, live;
    int test, write, i;
    nj_value a[2];
    init(&gpu, format); init(&cpu, format); attach(&gpu, &probe);
    fill(&gpu, 0xff355b83u); fill(&cpu, 0xff355b83u);
    clear(&gpu); clear(&cpu);
    workers = worker_calls;
    paired_draw(&gpu, &cpu, 0xffca2419u, -4);
    CHECK(probe.accepted > 0 && probe.row_jobs == 0);
    /* Only the baseline invoked row workers. Accepted packets stay on caller. */
    CHECK(worker_calls > workers);
    before = probe.accepted;
    integer(&gpu, gpu.cm, C("CompositingMode"), "setBlending", "(I)V", 68);
    integer(&cpu, cpu.cm, C("CompositingMode"), "setBlending", "(I)V", 68);
    paired_draw(&gpu, &cpu, 0xff23ad68u, -4); /* equal depth must pass */
    CHECK(probe.accepted > before && probe.row_jobs == 0);
    before = probe.occluded;
    paired_draw(&gpu, &cpu, 0xff182acdu, -8);
    CHECK(probe.occluded > before);
    for (test = 0; test < 2; ++test) for (write = 0; write < 2; ++write) {
        controls(&gpu, test, write); controls(&cpu, test, write);
        paired_draw(&gpu, &cpu, 0xff49e893u, -6);
    }
    controls(&gpu, 1, 1); controls(&cpu, 1, 1);
    /* Nonconstant, perspective-correct window depth, reversed range and bias. */
    a[0].f = .9f; a[1].f = .1f;
    call(&gpu, gpu.g3d, C("Graphics3D"), "setDepthRange", "(FF)V", a);
    call(&cpu, cpu.g3d, C("Graphics3D"), "setDepthRange", "(FF)V", a);
    a[0].f = .25f; a[1].f = 4.f;
    call(&gpu, gpu.cm, C("CompositingMode"), "setDepthOffset", "(FF)V", a);
    call(&cpu, cpu.cm, C("CompositingMode"), "setDepthOffset", "(FF)V", a);
    clear(&gpu); clear(&cpu); flat(&gpu, 0xffdd5123u); flat(&cpu, 0xffdd5123u);
    geometry(&gpu, -4, 4); geometry(&cpu, -4, 4);
    for (i = 0; i < 4; ++i) {
        ((nj_s2 *)nj_array_data(gpu.positions))[i * 3 + 2] = (nj_s2)(-4 - i * 2);
        ((nj_s2 *)nj_array_data(cpu.positions))[i * 3 + 2] = (nj_s2)(-4 - i * 2);
    }
    render(&gpu); render(&cpu); same(&gpu, &cpu);
    /* The 25th key bit and far clamp both survive packet transfer. */
    for (i = 0; i < 2; ++i) {
        a[0].f = 0.f; a[1].f = i ? -1.e20f : 1.e20f;
        call(&gpu, gpu.cm, C("CompositingMode"), "setDepthOffset", "(FF)V", a);
        call(&cpu, cpu.cm, C("CompositingMode"), "setDepthOffset", "(FF)V", a);
        clear(&gpu); clear(&cpu);
        paired_draw(&gpu, &cpu, 0xffd5a127u, -4);
        CHECK(depth(&gpu)[160 * W + 120] == (i ? NJ_GFX_RASTER_MAX_KEY : 1));
    }
    a[0].f = a[1].f = 0.f;
    call(&gpu, gpu.cm, C("CompositingMode"), "setDepthOffset", "(FF)V", a);
    call(&cpu, cpu.cm, C("CompositingMode"), "setDepthOffset", "(FF)V", a);
    /* Captured Graphics translation/clip intersects viewport in device space. */
    clipped(&gpu); clipped(&cpu); clear(&gpu); clear(&cpu);
    paired_draw(&gpu, &cpu, 0xff719bd3u, -4);
    probe.force_decline = 1;
    before = probe.declined; workers = worker_calls;
    paired_draw(&gpu, &cpu, 0xffb141e9u, -6);
    CHECK(probe.declined > before && worker_calls > workers);
    probe.force_decline = 0;
    before = probe.calls; live = gpu.outstanding;
    fail_alloc_target = &gpu; gpu.port.alloc = fail_scratch_alloc;
    paired_draw(&gpu, &cpu, 0xff16db28u, -4);
    CHECK(probe.calls == before && allocation_failures > 0);
    CHECK(gpu.outstanding == live);
    gpu.port.alloc = count_alloc; fail_alloc_target = NULL;
    /* New CPU writes and CPU clears must be visible to a later packet. */
    fill(&gpu, 0xff5d3271u); fill(&cpu, 0xff5d3271u);
    clear(&gpu); clear(&cpu);
    paired_draw(&gpu, &cpu, 0xfff28d71u, -4);
    CHECK(probe.accepted > 0 && probe.occluded > 0 && probe.declined > 0);
    printf("M3G format %d: accepted=%u occluded=%u declined=%u\n",
           format, probe.accepted, probe.occluded, probe.declined);
    destroy(&gpu); destroy(&cpu);
}
static void unsupported_cases(void)
{
    target gpu, cpu;
    accel_probe probe;
    nj_ref fog[2], shades[2];
    target *t[2] = {&gpu, &cpu};
    unsigned before;
    int state, k;
    nj_value a[2];
    init(&gpu, NJ_PIXFMT_ARGB8888); init(&cpu, NJ_PIXFMT_ARGB8888);
    attach(&gpu, &probe);
    for (k = 0; k < 2; ++k) {
        nj_ref data;
        nj_value b[3];
        static const nj_u1 colors[12] = {255,0,0, 0,255,0, 0,0,255, 255,255,0};
        fog[k] = make(t[k], C("Fog"));
        shades[k] = nj_root(t[k]->vm, nj_object_new(t[k]->vm, C("VertexArray")));
        CHECK(shades[k]);
        b[0].i = 4; b[1].i = 3; b[2].i = 1;
        CHECK(nj_call_special(t[k]->vm, shades[k], C("VertexArray"), "<init>",
                              "(III)V", b, NULL) == NJ_OK);
        data = nj_root(t[k]->vm, nj_byte_array_new(t[k]->vm, 12));
        CHECK(data);
        memcpy(nj_array_data(data), colors, sizeof(colors));
        b[0].i = 0; b[1].i = 4; b[2].r = data;
        call(t[k], shades[k], C("VertexArray"), "set", "(II[B)V", b);
    }
    for (state = 0; state < 6; ++state) {
        for (k = 0; k < 2; ++k) {
            flat(t[k], state == 2 ? 0x8081d743u : 0xff81d743u);
            a[0].r = state == 5 ? shades[k] : NULL;
            call(t[k], t[k]->vb, C("VertexBuffer"), "setColors", "(L" C("VertexArray") ";)V", a);
            a[0].i = 0; a[1].r = state == 0 ? t[k]->tex : NULL;
            call(t[k], t[k]->app, C("Appearance"), "setTexture", "(IL" C("Texture2D") ";)V", a);
            a[0].r = state == 1 ? fog[k] : NULL;
            call(t[k], t[k]->app, C("Appearance"), "setFog", "(L" C("Fog") ";)V", a);
            integer(t[k], t[k]->cm, C("CompositingMode"), "setAlphaWriteEnable", "(Z)V", state != 3);
            integer(t[k], t[k]->cm, C("CompositingMode"), "setBlending", "(I)V", state == 4 ? 65 : 64);
            clear(t[k]); geometry(t[k], -4, 4);
        }
        before = probe.calls;
        render(&gpu); render(&cpu); same(&gpu, &cpu);
        CHECK(probe.calls == before);
    }
    destroy(&gpu); destroy(&cpu);
}
static void independent_vms(void)
{
    target a, b, ar, br;
    accel_probe pa, pb;
    unsigned before;
    init(&a, NJ_PIXFMT_ARGB8888); init(&ar, NJ_PIXFMT_ARGB8888);
    init(&b, NJ_PIXFMT_RGB565); init(&br, NJ_PIXFMT_RGB565);
    attach(&a, &pa); attach(&b, &pb);
    clear(&a); clear(&ar); clear(&b); clear(&br);
    paired_draw(&a, &ar, 0xffefa391u, -4);
    CHECK(pa.accepted > 0 && pb.calls == 0);
    before = pa.calls;
    paired_draw(&b, &br, 0xff5d27dau, -4);
    CHECK(pb.accepted > 0 && pa.calls == before);
    destroy(&a); destroy(&ar); destroy(&b); destroy(&br);
}
int main(void)
{
    test_parallel_for = row_workers;
    supported_cases(NJ_PIXFMT_ARGB8888);
    supported_cases(NJ_PIXFMT_RGB888);
    supported_cases(NJ_PIXFMT_RGB565);
    unsupported_cases(); independent_vms();
    CHECK(worker_calls > 0 && allocation_failures > 0 && worker_depth == 0);
    printf("PASS M3G packet equivalence: %u checks, %u row jobs, %u allocation failures\n",
           checks, worker_calls, allocation_failures);
    return 0;
}
#else
#include <stdio.h>
int main(void)
{
    puts("SKIP M3G packet equivalence: requires NJ_ENABLE_M3G=1 and NJ_ENABLE_GFX_ACCEL=1");
    return 0;
}
#endif
