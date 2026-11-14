/* M3G public API -> one native triangle transaction. A deliberately synthetic
 * backend tests ownership/dispatch/commit; DECLINE is compared byte-for-byte to
 * independent software. Driver rendering is covered by the EGL backend suite. */
#include "nj2me/nj_config.h"
#if NJ_ENABLE_M3G && NJ_ENABLE_GFX_ACCEL
#define main m3g_targets_fixture_main
#include "test_m3g_targets.c"
#undef main
#include "nj2me/nj_gfx_triangles.h"
#include <math.h>

typedef struct batch_probe {
    target *target;
    nj_u1 *color;
    nj_s4 *z;
    nj_u4 gc_count, primitive_count;
    unsigned calls;
    unsigned list_calls, list_max_packets;
    nj_u4 list_packets, list_triangles;
    int accept, list_accept, journal_mode, expected_fog, expected_mips;
    int texture_first, width[4], height[4], roi[4];
    double depth0, bounds[4];
    nj_gfx_triangle_vertex vertices[3];
} batch_probe;
static unsigned workers, worker_depth, alloc_calls, fail_at;
static target *failing;

static void row_workers(void *ud, void (*work)(void *, nj_u4), void *ctx, nj_u4 parts)
{
    nj_u4 i;
    (void)ud;
    CHECK(parts == 2u);
    ++workers; ++worker_depth;
    for (i = 0; i < parts; ++i) work(ctx, i);
    --worker_depth;
}
static void *scratch_alloc(void *ud, nj_size n)
{
    if (owner(ud) == failing && ++alloc_calls == fail_at) return NULL;
    return count_alloc(ud, n);
}
static void snapshot(batch_probe *b)
{
    target *t = b->target;
    size_t bytes = (size_t)t->surface.stride * H;
    if (!b->color) b->color = malloc(bytes);
    if (!b->z) b->z = malloc(W * H * sizeof(*b->z));
    CHECK(b->color && b->z);
    memcpy(b->color, t->surface.pixels, bytes);
    memcpy(b->z, depth(t), W * H * sizeof(*b->z));
    b->gc_count = t->vm->heap.gc_count;
}
static void unchanged_colour(batch_probe *b)
{
    CHECK(memcmp(b->color, b->target->surface.pixels,
                 (size_t)b->target->surface.stride * H) == 0);
}
static void unchanged(batch_probe *b)
{
    CHECK(memcmp(b->color, b->target->surface.pixels,
                 (size_t)b->target->surface.stride * H) == 0);
    CHECK(memcmp(b->z, depth(b->target), W * H * sizeof(*b->z)) == 0);
}
static int triangles(void *ud, const nj_gfx_triangles *p, nj_u4 *submitted)
{
    batch_probe *b = ud;
    nj_u4 i;
    CHECK(!worker_depth && p && submitted);
    CHECK(nj_gfx_triangles_validate(p, submitted));
    CHECK(p->surface->pixels == b->target->surface.pixels);
    CHECK(p->depth == depth(b->target) && p->depth_stride == W);
    CHECK(p->count > 0 && p->count <= NJ_GFX_TRIANGLES_MAX_COUNT);
    CHECK(b->target->vm->heap.gc_count == b->gc_count);
    /* The journal fallback replays declined packets in order, so a later
     * single-packet call legitimately sees the earlier packet committed. */
    if (!b->journal_mode) unchanged(b);
    ++b->calls;
    b->primitive_count = p->count;
    b->roi[0] = p->x; b->roi[1] = p->y;
    b->roi[2] = p->width; b->roi[3] = p->height;
    b->bounds[0] = b->bounds[2] = p->triangles[0].vertices[0].x;
    b->bounds[1] = b->bounds[3] = p->triangles[0].vertices[0].y;
    for (i = 0; i < p->count; ++i) {
        const nj_gfx_triangle_cmd *t = &p->triangles[i];
        int k;
        for (k = 0; k < 3; ++k) {
            double x = t->vertices[k].x, y = t->vertices[k].y;
            if (x < b->bounds[0]) b->bounds[0] = x;
            if (y < b->bounds[1]) b->bounds[1] = y;
            if (x > b->bounds[2]) b->bounds[2] = x;
            if (y > b->bounds[3]) b->bounds[3] = y;
        }
        CHECK(t->fog == b->expected_fog);
        CHECK(t->depth_test == 0 || t->depth_test == 1);
        CHECK(t->depth_write == 0 || t->depth_write == 1);
        if (i < 4) {
            b->width[i] = t->texture_width;
            b->height[i] = t->texture_height;
        }
    }
    b->texture_first = p->triangles[0].texture ? p->triangles[0].texture[0] : -1;
    b->depth0 = p->triangles[0].vertices[0].depth;
    memcpy(b->vertices, p->triangles[0].vertices, sizeof(b->vertices));
    if (b->expected_mips) {
        CHECK(p->count == 2 && p->triangles[0].texture);
        CHECK(p->triangles[0].texture_width < p->triangles[1].texture_width);
        CHECK(p->triangles[1].texture_width == 8);
        CHECK(p->triangles[0].texture != p->triangles[1].texture);
    }
    if (!b->accept) return NJ_GFX_ACCEL_DECLINE; /* No result write, either. */
    /* A synthetic complete backend result, independent of software visibility.
     * Checking the untouched remainder after return detects accidental replay. */
    nj_surface_set(p->surface, p->x, p->y, 0xff123456u);
    p->depth[(size_t)p->y * p->depth_stride + p->x] = 12345;
    *submitted = p->count;
    return NJ_GFX_ACCEL_DONE;
}
/* Ordered-list backend used by the core journal. DECLINE must fall back to the
 * single-packet callbacks and then to software, never partially committing. */
static int journal_list(void *ud, const nj_gfx_triangles_list *list,
                        nj_u4 *submitted)
{
    batch_probe *b = ud;
    nj_u4 i, total = 0;
    CHECK(!worker_depth && list && submitted);
    CHECK(nj_gfx_triangles_list_validate(list, submitted));
    CHECK(list->surface->pixels == b->target->surface.pixels);
    CHECK(list->depth == depth(b->target) && list->depth_stride == W);
    CHECK(list->count > 0 && list->count <= NJ_GFX_TRIANGLES_LIST_MAX_PACKETS);
    CHECK(b->target->vm->heap.gc_count == b->gc_count);
    /* Colour must be untouched: the journal owns every queued draw. Depth may
     * already hold this render's fresh-bind clear, which precedes collection. */
    unchanged_colour(b);
    ++b->list_calls;
    b->list_packets = list->count;
    if (list->count > b->list_max_packets) b->list_max_packets = list->count;
    for (i = 0; i < list->count; ++i) {
        nj_u4 scratch = 0;
        CHECK(list->packets[i] && list->packets[i]->version == NJ_GFX_TRIANGLES_VERSION);
        CHECK(list->packets[i]->surface->pixels == list->surface->pixels);
        CHECK(nj_gfx_triangles_validate(list->packets[i], &scratch));
        total += list->packets[i]->count;
    }
    b->list_triangles = total;
    if (!b->list_accept) return NJ_GFX_ACCEL_DECLINE;
    *submitted = total;
    return NJ_GFX_ACCEL_DONE;
}
static void attach(target *t, batch_probe *b)
{
    memset(b, 0, sizeof(*b)); b->target = t;
    t->port.gfx_triangles = triangles;
    t->port.gfx_triangles_ud = b;
}
static void detach(batch_probe *b)
{
    free(b->color); free(b->z);
}
static void same(target *a, target *b)
{
    compare(a, b);
    CHECK(memcmp(depth(a), depth(b), W * H * sizeof(nj_s4)) == 0);
    CHECK(a->vm->tris == b->vm->tris);
    CHECK(a->vm->m3g_renders == b->vm->m3g_renders);
}
static void no_texture(target *t)
{
    nj_value a[2];
    a[0].i = 0; a[1].r = NULL;
    call(t, t->app, C("Appearance"), "setTexture", "(IL" C("Texture2D") ";)V", a);
}
static void paired_init(target *a, target *b, int format)
{
    init(a, format); init(b, format);
    clear(a); clear(b);
    /* Warm Java conversion/mip caches BEFORE measuring the C-only collector. */
    render(a); render(b); clear(a); clear(b);
}
static void replay_cases(int format)
{
    target a, b;
    batch_probe probe;
    nj_u8 before, renders;
    unsigned calls, live, jobs;
    nj_value arg[2];
    int mode;
    paired_init(&a, &b, format); attach(&a, &probe);
    snapshot(&probe); jobs = workers;
    render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 1 && probe.primitive_count == 2 && workers > jobs);
    /* Every compositing/fog state is collected, then rejected atomically. */
    for (mode = 64; mode <= 68; ++mode) {
        integer(&a, a.cm, C("CompositingMode"), "setBlending", "(I)V", mode);
        integer(&b, b.cm, C("CompositingMode"), "setBlending", "(I)V", mode);
        clear(&a); clear(&b); snapshot(&probe);
        render(&a); render(&b); same(&a, &b);
    }
    calls = probe.calls; live = a.outstanding;
    failing = &a; fail_at = 1; alloc_calls = 0; a.port.alloc = scratch_alloc;
    clear(&a); clear(&b); snapshot(&probe);
    render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == calls && alloc_calls > 0 && a.outstanding == live);
    a.port.alloc = count_alloc; failing = NULL;
    /* DONE counts submitted work even when every triangle would be occluded. */
    probe.accept = 1;
    clear(&a); snapshot(&probe); before = a.vm->tris; renders = a.vm->m3g_renders;
    render(&a);
    CHECK(a.vm->tris == before + probe.primitive_count);
    CHECK(a.vm->m3g_renders == renders + 1);
    CHECK(depth(&a)[probe.roi[1] * W + probe.roi[0]] == 12345);
    /* Rebuild the synthetic result in the snapshot to check the ENTIRE buffer. */
    {
        nj_surface expected = a.surface;
        expected.pixels = probe.color;
        nj_surface_set(&expected, probe.roi[0], probe.roi[1], 0xff123456u);
        probe.z[probe.roi[1] * W + probe.roi[0]] = 12345;
        unchanged(&probe); guards(&a);
    }
    /* Depth offset is resolved exactly once (uniform plane => units only). */
    arg[0].f = 7.f; arg[1].f = 4.f;
    call(&a, a.cm, C("CompositingMode"), "setDepthOffset", "(FF)V", arg);
    clear(&a); snapshot(&probe); render(&a);
    CHECK(fabs(probe.depth0 - (5.0 / 9.0 + 4.0 / 16777215.0)) < 1.e-6);
    detach(&probe); destroy(&a); destroy(&b);
}
static void roi_cases(int format)
{
    target a, b;
    target *both[2] = {&a, &b};
    batch_probe probe;
    nj_value arg[4];
    unsigned calls;
    nj_u8 tris;
    int k, i;
    paired_init(&a, &b, format); attach(&a, &probe);
    for (k = 0; k < 2; ++k) {
        nj_s2 *v = nj_array_data(both[k]->positions);
        no_texture(both[k]);
        for (i = 0; i < 4; ++i) { v[i*3] /= 4; v[i*3+1] /= 4; }
        clear(both[k]);
    }
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 1 && probe.primitive_count == 2);
    CHECK(probe.roi[0] > 0 && probe.roi[1] > 0);
    CHECK(probe.roi[2] < W/2 && probe.roi[3] < H/2);
    /* Each unclipped side has a full extra pixel beyond the geometric bounds. */
    CHECK(probe.roi[0] <= probe.bounds[0]-1.0 && probe.bounds[0]-probe.roi[0] < 2.0);
    CHECK(probe.roi[1] <= probe.bounds[1]-1.0 && probe.bounds[1]-probe.roi[1] < 2.0);
    CHECK(probe.roi[0]+probe.roi[2] >= probe.bounds[2]+1.0);
    CHECK(probe.roi[1]+probe.roi[3] >= probe.bounds[3]+1.0);
    /* Near-plane clipping creates real fan triangles with negative device XY. */
    for (k = 0; k < 2; ++k) {
        geometry(both[k], -4, 4);
        ((nj_s2 *)nj_array_data(both[k]->positions))[2] = -1;
        clear(both[k]);
    }
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 2 && probe.primitive_count > 2);
    CHECK(probe.bounds[0] < 0.0 && probe.bounds[3] > H);
    CHECK(probe.roi[0] == 0 && probe.roi[1] == 0 && probe.roi[2] == W && probe.roi[3] == H);
    /* Finite coordinates much larger than INT_MAX must be clamped BEFORE cast. */
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        nj_ref camera = make(t, C("Camera"));
        arg[0].f = 1.e-9f; arg[1].f = 1.f; arg[2].f = 1.f; arg[3].f = 10.f;
        call(t, camera, C("Camera"), "setParallel", "(FFFF)V", arg);
        arg[0].r = camera; arg[1].r = NULL;
        call(t, t->g3d, C("Graphics3D"), "setCamera", "(L" C("Camera") ";L" C("Transform") ";)V", arg);
        geometry(t, -4, 4); clear(t);
    }
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 3 && probe.bounds[0] < -2147483648.0 && probe.bounds[2] > 2147483647.0);
    CHECK(probe.roi[0] == 0 && probe.roi[1] == 0 && probe.roi[2] == W && probe.roi[3] == H);
    /* Keep the huge projection but put the whole batch outside the right edge. */
    for (k = 0; k < 2; ++k) {
        nj_s2 *v = nj_array_data(both[k]->positions);
        for (i = 0; i < 4; ++i) v[i*3] = (nj_s2)(10 + (i&1));
        clear(both[k]);
    }
    calls = probe.calls; tris = a.vm->tris;
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == calls && a.vm->tris == tris); /* Empty ROI submits zero. */
    unchanged(&probe);
    /* Graphics clip limits the expanded footprint, including colour/Z commits. */
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        geometry(t, -4, 4);
        arg[0].i = 17; arg[1].i = 23; arg[2].i = 47; arg[3].i = 59;
        call(t, t->g, G, "setClip", "(IIII)V", arg);
        bind(t); viewport(t, 0, 0, W, H); clear(t);
    }
    depth(&a)[0] = -123; /* Only the ROI depth values need to be valid. */
    probe.accept = 1; snapshot(&probe); render(&a);
    CHECK(probe.calls == calls+1);
    CHECK(probe.roi[0] == 17 && probe.roi[1] == 23 && probe.roi[2] == 47 && probe.roi[3] == 59);
    {
        nj_surface expected = a.surface;
        expected.pixels = probe.color;
        nj_surface_set(&expected, 17, 23, 0xff123456u); probe.z[23*W+17] = 12345;
        unchanged(&probe); guards(&a);
    }
    CHECK(depth(&a)[0] == -123);
    detach(&probe); destroy(&a); destroy(&b);
}
static void resolved_bias_and_culling(void)
{
    target a, b;
    target *both[2] = {&a, &b};
    batch_probe probe;
    nj_gfx_triangle_vertex v[3];
    nj_value arg[2];
    double dx, dy, area, bias;
    unsigned calls;
    int k;
    paired_init(&a, &b, NJ_PIXFMT_ARGB8888); attach(&a, &probe);
    for (k = 0; k < 2; ++k)
        ((nj_s2 *)nj_array_data(both[k]->positions))[5] = -5;
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    memcpy(v, probe.vertices, sizeof(v));
    area = ((double)v[1].x-v[0].x)*((double)v[2].y-v[0].y) -
           ((double)v[1].y-v[0].y)*((double)v[2].x-v[0].x);
    dx = ((v[1].depth-v[0].depth)*((double)v[2].y-v[0].y) -
          (v[2].depth-v[0].depth)*((double)v[1].y-v[0].y))/area;
    dy = (((double)v[1].x-v[0].x)*(v[2].depth-v[0].depth) -
          ((double)v[2].x-v[0].x)*(v[1].depth-v[0].depth))/area;
    CHECK(fmax(fabs(dx), fabs(dy)) > 0.0);
    bias = fmax(fabs(dx), fabs(dy))*2.0 + 3.0/16777215.0;
    for (k = 0; k < 2; ++k) {
        arg[0].f = 2.f; arg[1].f = 3.f;
        call(both[k], both[k]->cm, C("CompositingMode"), "setDepthOffset", "(FF)V", arg);
        clear(both[k]);
    }
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    for (k = 0; k < 3; ++k)
        CHECK(fabs(probe.vertices[k].depth - (v[k].depth+bias)) < 1.e-12);
    calls = probe.calls;
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        nj_ref poly = nj_rget(t->vm, t->app,
            nj_field_lookup(t->vm, C("Appearance"), "_nj_pm", "L" C("PolygonMode") ";", 0));
        integer(t, poly, C("PolygonMode"), "setCulling", "(I)V", 161);
        clear(t);
    }
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == calls); /* Entirely front-culled, no callback. */
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        nj_ref poly = nj_rget(t->vm, t->app,
            nj_field_lookup(t->vm, C("Appearance"), "_nj_pm", "L" C("PolygonMode") ";", 0));
        integer(t, poly, C("PolygonMode"), "setWinding", "(I)V", 169);
    }
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == calls+1 && probe.primitive_count == 2);
    detach(&probe); destroy(&a); destroy(&b);
}
static void mips_and_mutation(void)
{
    target a, b;
    target *both[2] = {&a, &b};
    batch_probe probe;
    int k, i;
    nj_value arg[5];
    paired_init(&a, &b, NJ_PIXFMT_ARGB8888);
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        nj_ref image = nj_root(t->vm, nj_object_new(t->vm, C("Image2D")));
        nj_ref tex = nj_root(t->vm, nj_object_new(t->vm, C("Texture2D")));
        nj_ref coords = nj_rget(t->vm, t->vb,
                    nj_field_lookup(t->vm, C("VertexBuffer"), "_nj_tex0", "L" C("VertexArray") ";", 0));
        nj_ref data = nj_rget(t->vm, coords,
                    nj_field_lookup(t->vm, C("VertexArray"), "_nj_data", "[S", 0));
        static const nj_s2 uv[8] = {0,0, 16384,0, 0,16384, 16384,0};
        nj_ref pixels;
        arg[0].i = 100; arg[1].i = 8; arg[2].i = 8;
        CHECK(nj_call_special(t->vm, image, C("Image2D"), "<init>", "(III)V", arg, NULL) == NJ_OK);
        arg[0].r = image;
        CHECK(nj_call_special(t->vm, tex, C("Texture2D"), "<init>", "(L" C("Image2D") ";)V", arg, NULL) == NJ_OK);
        integer(t, tex, C("Texture2D"), "setBlending", "(I)V", 228);
        arg[0].i = 240; arg[1].i = 240;
        call(t, tex, C("Texture2D"), "setWrapping", "(II)V", arg);
        arg[0].i = 210; arg[1].i = 208;
        call(t, tex, C("Texture2D"), "setFiltering", "(II)V", arg);
        pixels = nj_root(t->vm, nj_byte_array_new(t->vm, 8 * 8 * 4));
        CHECK(pixels);
        for (i = 0; i < 64; ++i) {
            nj_u1 *p = (nj_u1 *)nj_array_data(pixels) + i * 4;
            p[0] = 43; p[1] = 71; p[2] = 119; p[3] = 255;
        }
        arg[0].i = arg[1].i = 0; arg[2].i = arg[3].i = 8; arg[4].r = pixels;
        call(t, image, C("Image2D"), "set", "(IIII[B)V", arg);
        memcpy(nj_array_data(data), uv, sizeof(uv));
        arg[0].i = 0; arg[1].r = tex;
        call(t, t->app, C("Appearance"), "setTexture", "(IL" C("Texture2D") ";)V", arg);
        t->image = image; t->tex = tex;
        render(t); clear(t);
    }
    attach(&a, &probe); probe.expected_mips = 1; snapshot(&probe);
    render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 1 && probe.texture_first == 43);
    /* Mutable content must be observed with the same image/texture pointers. */
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        nj_ref data = nj_root(t->vm, nj_byte_array_new(t->vm, 8 * 8 * 4));
        nj_u1 *pixels = nj_array_data(data);
        for (i = 0; i < 64; ++i) {
            pixels[i * 4] = 91; pixels[i * 4 + 1] = 71;
            pixels[i * 4 + 2] = 119; pixels[i * 4 + 3] = 255;
        }
        arg[0].i = arg[1].i = 0; arg[2].i = arg[3].i = 8; arg[4].r = data;
        call(t, t->image, C("Image2D"), "set", "(IIII[B)V", arg);
        /* Cache generation belongs to jr_setup, outside the collection test. */
        if (k == 0) t->port.gfx_triangles = NULL;
        render(t); clear(t);
        if (k == 0) t->port.gfx_triangles = triangles;
    }
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 2 && probe.texture_first == 91);
    detach(&probe); destroy(&a); destroy(&b);
}
static void growth_failure(void)
{
    target a, b;
    target *both[2] = {&a, &b};
    batch_probe probe;
    unsigned live;
    int i, k;
    paired_init(&a, &b, NJ_PIXFMT_ARGB8888);
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        nj_ref indices = nj_root(t->vm, nj_array_new(t->vm, "I", 300));
        for (i = 0; i < 300; ++i) ((nj_s4 *)nj_array_data(indices))[i] = i % 4;
        nj_rput(t->vm, t->ib, nj_field_lookup(t->vm, C("TriangleStripArray"), "_nj_ib_data", "[I", 0), indices);
        ((nj_s4 *)nj_array_data(t->lengths))[0] = 300;
    }
    attach(&a, &probe); snapshot(&probe); live = a.outstanding;
    failing = &a; fail_at = 2; alloc_calls = 0; a.port.alloc = scratch_alloc;
    render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 0 && alloc_calls == 2 && a.outstanding == live);
    a.port.alloc = count_alloc; failing = NULL;
    clear(&a); clear(&b); snapshot(&probe);
    render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 1 && probe.primitive_count == 298);
    detach(&probe); destroy(&a); destroy(&b);
}
static void alias_and_fog(void)
{
    target a, b;
    target *both[2] = {&a, &b};
    batch_probe probe;
    int k;
    nj_value arg;
    paired_init(&a, &b, NJ_PIXFMT_ARGB8888);
    attach(&a, &probe);
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        nj_ref fog = make(t, C("Fog"));
        no_texture(t);
        integer(t, fog, C("Fog"), "setMode", "(I)V", 81);
        arg.r = fog;
        call(t, t->app, C("Appearance"), "setFog", "(L" C("Fog") ";)V", &arg);
        clear(t);
    }
    probe.expected_fog = 81; snapshot(&probe);
    render(&a); render(&b); same(&a, &b); CHECK(probe.calls == 1);
    /* A source aliased with destination needs the original serial feedback
     * sampling. The whole transaction must bypass the batch callback. */
    for (k = 0; k < 2; ++k) {
        target *t = both[k];
        nj_value texarg[2];
        nj_ref pixels = nj_root(t->vm, nj_byte_array_new(t->vm, t->surface.stride * H));
        memset(nj_array_data(pixels), 0xa5, (size_t)t->surface.stride * H);
        nj_rput(t->vm, t->image, nj_field_lookup(t->vm, C("Image2D"), "_nj_pixels", "[B", 0), pixels);
        t->surface.pixels = nj_array_data(pixels); t->vm->screen = t->surface;
        texarg[0].i = 0; texarg[1].r = t->tex;
        call(t, t->app, C("Appearance"), "setTexture", "(IL" C("Texture2D") ";)V", texarg);
        bind(t); clear(t);
    }
    snapshot(&probe); render(&a); render(&b); same(&a, &b);
    CHECK(probe.calls == 1);
    detach(&probe); destroy(&a); destroy(&b);
}
static void journal_cases(int format)
{
    target a, b;
    batch_probe probe;
    nj_u8 before, renders;
    paired_init(&a, &b, format);
    attach(&a, &probe);
    probe.journal_mode = 1;
    a.port.gfx_triangles_list = journal_list;
    a.port.gfx_triangles_list_ud = &probe;
    /* Two immediate render calls inside one bound target become one list. */
    snapshot(&probe);
    render(&a); render(&a);
    nj_m3g_flush(a.vm);
    render(&b); render(&b);
    same(&a, &b);
    CHECK(probe.list_calls == 1 && probe.list_packets == 2);
    CHECK(probe.list_max_packets == 2);
    /* A barrier between renders flushes the first batch and starts a second. */
    clear(&a); clear(&b); snapshot(&probe);
    probe.list_max_packets = 0; /* per-phase maximum */
    render(&a); bind(&a);
    snapshot(&probe); /* committed state before the second batch */
    render(&a);
    nj_m3g_flush(a.vm);
    render(&b); bind(&b); render(&b);
    same(&a, &b);
    CHECK(probe.list_calls == 3 && probe.list_max_packets == 1);
    /* DONE: the list owns the commit, counts as submitted work, never replays. */
    probe.list_accept = 1;
    clear(&a); snapshot(&probe);
    before = a.vm->tris; renders = a.vm->m3g_renders;
    render(&a); render(&a);
    nj_m3g_flush(a.vm);
    CHECK(probe.list_calls == 4 && probe.list_packets == 2);
    CHECK(a.vm->tris == before + probe.list_triangles);
    CHECK(a.vm->m3g_renders == renders + 2);
    unchanged(&probe); /* DONE backend wrote nothing: no software replay either */
    guards(&a);
    detach(&probe); destroy(&a); destroy(&b);
}
int main(void)
{
    test_parallel_for = row_workers;
    replay_cases(NJ_PIXFMT_ARGB8888);
    replay_cases(NJ_PIXFMT_RGB888);
    replay_cases(NJ_PIXFMT_RGB565);
    roi_cases(NJ_PIXFMT_ARGB8888); roi_cases(NJ_PIXFMT_RGB888); roi_cases(NJ_PIXFMT_RGB565);
    resolved_bias_and_culling(); mips_and_mutation(); growth_failure(); alias_and_fog();
    journal_cases(NJ_PIXFMT_ARGB8888); journal_cases(NJ_PIXFMT_RGB888);
    journal_cases(NJ_PIXFMT_RGB565);
    CHECK(!worker_depth && workers > 0);
    printf("PASS M3G triangle transactions: %u checks, %u software row jobs\n", checks, workers);
    return 0;
}
#else
#include <stdio.h>
int main(void)
{
    puts("SKIP M3G triangle transactions: requires M3G and GFX_ACCEL");
    return 0;
}
#endif
