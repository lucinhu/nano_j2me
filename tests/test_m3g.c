/*
 * nano_j2me - test_m3g.c
 *
 * What JSR 184 has to get right before a single triangle is drawn:
 *
 *   1. every class the collected titles name exists,
 *   2. the transform maths is real - a translation, a scale and a rotation
 *      compose the way the specification says, and a matrix inverts back,
 *   3. the scene graph is real - addChild records the parent, a second
 *      parent is refused, and find() walks the children,
 *   4. the things that are deliberately not implemented say so: Loader.load
 *      throws IOException rather than returning an empty scene.
 *
 * Point 4 is the one worth having: a test that only checked "does not crash"
 * would pass on a module that returned null everywhere.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_config.h"
#include "nj2me/nj_vm.h"
#include "nj_internal.h"   /* nj_field_lookup, to read what the loader wrote */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int checks;
static int failures;

/*
 * Host allocations, counted through the port hook that nj_host_alloc() calls.
 * The renderer's "nothing allocates while a title paints" claim is measured,
 * not asserted by inspection: the skinned-mesh test renders the same mesh
 * again and requires the count not to move.
 */
static unsigned host_alloc_calls;

static void *counting_alloc(void *ud, nj_size n)
{
    (void)ud;
    host_alloc_calls++;
    return malloc(n);
}

static void counting_free(void *ud, void *p)
{
    (void)ud;
    free(p);
}

static void put_le32(nj_u1 *p, nj_u4 v)
{
    p[0] = (nj_u1)v;
    p[1] = (nj_u1)(v >> 8);
    p[2] = (nj_u1)(v >> 16);
    p[3] = (nj_u1)(v >> 24);
}

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

#define CLS(x) "javax/microedition/m3g/" x

static nj_ref make(nj_vm *vm, const char *cls)
{
    nj_ref o = nj_object_new(vm, cls);
    if (!o) {
        return 0;
    }
    if (nj_call_special(vm, o, cls, "<init>", "()V", NULL, NULL) != NJ_OK) {
        return 0;
    }
    return o;
}

static void call(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                 const char *desc, nj_value *args, nj_value *ret)
{
    nj_status st = nj_call(vm, o, cls, name, desc, args, ret);
    if (st != NJ_OK) {
        /* A silent failure here is how Transform.get() looked like it worked
           while the native was never entered. */
        fprintf(stderr, "[call] %s.%s%s failed, status %d\n", cls, name, desc, (int)st);
    }
}

static float xf_elem(nj_vm *vm, nj_ref xf, int i)
{
    nj_ref out = nj_array_new(vm, "F", 16);
    nj_value a[1];
    float v = -12345.0f;

    if (!out || !xf) {
        return v;
    }
    a[0].r = out;
    call(vm, xf, CLS("Transform"), "get", "([F)V", a, NULL);
    v = ((float *)out->u.bytes)[i];
    return v;
}

/* Exercise camera clipping through the same mutable Image / Graphics3D API
 * as the scene rendering tests. Positions use half-unit shorts so z=-0.5 is
 * represented exactly, and rejected geometry must leave BOTH buffers alone. */
static void clip_draw(nj_vm *vm, nj_ref g3d, nj_ref vb, nj_ref ib, nj_ref app,
                      nj_ref data, const nj_s2 pos[9], nj_u4 colour)
{
    nj_value a[5];

    memcpy(nj_array_data(data), pos, 9 * sizeof(*pos));
    a[0].i = (nj_s4)colour;
    call(vm, vb, CLS("VertexBuffer"), "setDefaultColor", "(I)V", a, NULL);
    a[0].r = vb; a[1].r = ib; a[2].r = app; a[3].r = 0; a[4].i = 0;
    call(vm, g3d, CLS("Graphics3D"), "render",
         "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
         CLS("Appearance") ";L" CLS("Transform") ";I)V", a, NULL);
}

static void test_camera_clip(nj_vm *vm)
{
    const nj_s2 background[9] = {-12,-12,-6, 12,-12,-6, 0,12,-6};
    const nj_s2 crossing[9] = {-2,-2,-4, 2,-2,-4, 0,2,2};
    const nj_s2 legal[9] = {-2,-2,-4, 2,-2,-4, 0,2,-4};
    const nj_s2 before_near[9] = {-2,-2,-1, 2,-2,-1, 0,2,-1};
    const nj_s2 beyond_far[9] = {-20,-20,-40, 20,-20,-40, 0,20,-40};
    const nj_s2 positive_background[9] = {-12,-12,6, 12,-12,6, 0,12,6};
    const nj_s2 positive_legal[9] = {-2,-2,4, 2,-2,4, 0,2,4};
    const nj_s2 before_zero[9] = {-2,-2,1, 2,-2,1, 0,2,1};
    const nj_s2 before_negative[9] = {-2,-2,24, 2,-2,24, 0,2,24};
    nj_ref g3d = nj_object_new(vm, CLS("Graphics3D"));
    nj_ref cam = make(vm, CLS("Camera")), xf = make(vm, CLS("Transform"));
    nj_ref va = nj_object_new(vm, CLS("VertexArray"));
    nj_ref vb = make(vm, CLS("VertexBuffer"));
    nj_ref app = make(vm, CLS("Appearance")), poly = make(vm, CLS("PolygonMode"));
    nj_ref bg = make(vm, CLS("Background"));
    nj_ref ib = nj_object_new(vm, CLS("TriangleStripArray"));
    nj_ref lengths = nj_array_new(vm, "I", 1u);
    nj_ref data = nj_array_new(vm, "S", 9u), img, pixels, depth;
    nj_value a[8], r;
    int mode, rejected, k;

    a[0].i = 240; a[1].i = 320; r.r = 0;
    check(nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
          "(II)Ljavax/microedition/lcdui/Image;", a, &r) == NJ_OK && r.r,
          "camera clip mutable image constructs");
    img = r.r;
    a[0].i = 3; a[1].i = 3; a[2].i = 2;
    check(nj_call_special(vm, va, CLS("VertexArray"), "<init>", "(III)V", a, NULL) == NJ_OK,
          "camera clip vertex array constructs");
    a[0].i = 0; a[1].i = 3; a[2].r = data;
    call(vm, va, CLS("VertexArray"), "set", "(II[S)V", a, NULL);
    /* VertexArray.set copies its input; mutate its backing array per draw. */
    data = nj_rget(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_data", "[S", 0));
    a[0].r = va; a[1].f = 0.5f; a[2].r = 0;
    call(vm, vb, CLS("VertexBuffer"), "setPositions", "(L" CLS("VertexArray") ";F[F)V", a, NULL);
    ((nj_s4 *)nj_array_data(lengths))[0] = 3;
    a[0].i = 0; a[1].r = lengths;
    check(nj_call_special(vm, ib, CLS("TriangleStripArray"), "<init>", "(I[I)V", a, NULL) == NJ_OK,
          "camera clip index buffer constructs");
    a[0].i = 162;
    call(vm, poly, CLS("PolygonMode"), "setCulling", "(I)V", a, NULL);
    a[0].r = poly;
    call(vm, app, CLS("Appearance"), "setPolygonMode", "(L" CLS("PolygonMode") ";)V", a, NULL);
    call(vm, img, "javax/microedition/lcdui/Image", "getGraphics",
         "()Ljavax/microedition/lcdui/Graphics;", NULL, &r);
    a[0].r = r.r;
    call(vm, g3d, CLS("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V", a, NULL);
    a[0].i = 0; a[1].i = 0; a[2].i = 240; a[3].i = 320;
    call(vm, g3d, CLS("Graphics3D"), "setViewport", "(IIII)V", a, NULL);
    pixels = nj_rget(vm, img, nj_field_lookup(vm, "javax/microedition/lcdui/Image", "imgPixels", "[I", 0));

    for (mode = 0; mode < 7; mode++) {
        const nj_u4 *p;
        int parallel = mode == 2 || mode >= 4;
        const nj_s2 *near_pos = mode == 5 ? before_zero :
                               mode == 6 ? before_negative : before_near;
        const nj_s2 *far_pos = mode == 6 ? before_near : beyond_far;
        const nj_s2 *legal_pos = mode == 6 ? positive_legal : legal;
        cam = make(vm, CLS("Camera"));
        if (mode >= 3) {
            call(vm, xf, CLS("Transform"), "setIdentity", "()V", NULL, NULL);
            a[0].r = xf;
            call(vm, cam, CLS("Camera"), "setGeneric", "(L" CLS("Transform") ";)V", a, NULL);
        }
        a[0].f = parallel ? 4.0f : 90.0f;
        a[1].f = 1.0f;
        a[2].f = mode == 5 ? 0.0f : mode == 6 ? -10.0f : 1.0f;
        a[3].f = mode == 6 ? 0.0f : 10.0f;
        call(vm, cam, CLS("Camera"), parallel ? "setParallel" : "setPerspective", "(FFFF)V", a, NULL);
        if (mode == 1) {
            a[0].r = xf;
            call(vm, cam, CLS("Camera"), "getProjection", "(L" CLS("Transform") ";)I", a, &r);
            call(vm, cam, CLS("Camera"), "setGeneric", "(L" CLS("Transform") ";)V", a, NULL);
        }
        a[0].r = cam; a[1].r = 0;
        call(vm, g3d, CLS("Graphics3D"), "setCamera", "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);
        a[0].r = bg;
        call(vm, g3d, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
        clip_draw(vm, g3d, vb, ib, app, data,
                  mode == 6 ? positive_background : background, 0xFF0000FFu);
        p = (const nj_u4 *)nj_array_data(pixels);
        check((p[160 * 240 + 120] & 0xFFFFFFu) == 0x0000FFu,
              "legal background renders with camera projection");
        if (!parallel) {
            clip_draw(vm, g3d, vb, ib, app, data, crossing, 0xFF808080u);
            check((p[160 * 240 + 120] & 0xFFFFFFu) == 0x0000FFu,
                  "near-plane crossing leaves the centre blue");
            check((p[212 * 240 + 120] & 0xFFFFFFu) == 0x0000FFu &&
                  (p[220 * 240 + 120] & 0xFFFFFFu) == 0x808080u &&
                  (p[239 * 240 + 120] & 0xFFFFFFu) == 0x808080u &&
                  (p[241 * 240 + 120] & 0xFFFFFFu) == 0x0000FFu,
                  "near-plane crossing retains only the bottom trapezoid");
        }
        for (rejected = 0; rejected < 2; rejected++) {
            int colour_clean = 1, depth_clean = 1;
            const nj_u4 *z;
            a[0].r = bg;
            call(vm, g3d, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
            clip_draw(vm, g3d, vb, ib, app, data,
                      rejected ? far_pos : near_pos, 0xFF808080u);
            depth = nj_rget(vm, g3d, nj_field_lookup(vm, CLS("Graphics3D"), "_nj_zbuf", "[I", 0));
            z = depth ? (const nj_u4 *)nj_array_data(depth) : NULL;
            for (k = 0; k < 240 * 320; k++) {
                if ((p[k] & 0xFFFFFFu) != 0) { colour_clean = 0; }
                if (!z || z[k] != 0) { depth_clean = 0; }
            }
            check(colour_clean, rejected ? "beyond far writes no colour" : "before near writes no colour");
            check(depth_clean, rejected ? "beyond far writes no depth" : "before near writes no depth");
            clip_draw(vm, g3d, vb, ib, app, data, legal_pos, 0xFF00FF00u);
            check((p[160 * 240 + 120] & 0xFFFFFFu) == 0x00FF00u,
                  "legal geometry still renders after rejected geometry");
        }
    }
    /* With an identity generic projection, z=-1 and z=+1 are exact clip
       boundaries. Vertices on either plane remain inside, including when
       the renderer bypasses clipping for a wholly contained triangle. */
    {
        const nj_s2 on_plane[2][9] = {
            {-1,-1,-2, 1,-1,-2, 0,1,-2},
            {-1,-1, 2, 1,-1, 2, 0,1, 2}
        };
        const nj_u4 *p = (const nj_u4 *)nj_array_data(pixels);
        int plane;
        call(vm, xf, CLS("Transform"), "setIdentity", "()V", NULL, NULL);
        a[0].r = xf;
        call(vm, cam, CLS("Camera"), "setGeneric", "(L" CLS("Transform") ";)V", a, NULL);
        a[0].r = cam; a[1].r = 0;
        call(vm, g3d, CLS("Graphics3D"), "setCamera",
             "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);
        for (plane = 0; plane < 2; plane++) {
            a[0].r = bg;
            call(vm, g3d, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
            clip_draw(vm, g3d, vb, ib, app, data, on_plane[plane], 0xFF2468ACu);
            check(p[160 * 240 + 120] == 0xFF2468ACu && (p[0] & 0xFFFFFFu) == 0,
                  plane ? "triangle on far plane keeps its colour and extent" :
                          "triangle on near plane keeps its colour and extent");
        }
    }
    call(vm, g3d, CLS("Graphics3D"), "releaseTarget", "()V", NULL, NULL);
}

/* Parallel near/far may be zero or negative; zero is a parameter value,
 * not the implicit perspective camera's request for constructor defaults. */
static void test_parallel_zero_planes(nj_vm *vm)
{
    const float planes[2][2] = {{0.0f, 10.0f}, {-10.0f, 0.0f}};
    nj_ref cam = make(vm, CLS("Camera")), xf = make(vm, CLS("Transform"));
    nj_ref arr = nj_array_new(vm, "F", 16u);
    nj_value a[4];
    int k;

    for (k = 0; k < 2; k++) {
        const float *m;
        float near = planes[k][0], far = planes[k][1];
        a[0].f = 4.0f; a[1].f = 2.0f; a[2].f = near; a[3].f = far;
        call(vm, cam, CLS("Camera"), "setParallel", "(FFFF)V", a, NULL);
        a[0].r = xf;
        call(vm, cam, CLS("Camera"), "getProjection", "(L" CLS("Transform") ";)I", a, NULL);
        a[0].r = arr;
        call(vm, xf, CLS("Transform"), "get", "([F)V", a, NULL);
        m = (const float *)nj_array_data(arr);
        check(fabsf(m[0] - 0.25f) < 0.00001f && fabsf(m[5] - 0.5f) < 0.00001f,
              "parallel height and aspect retain their scale");
        check(fabsf(-near * m[10] + m[11] + 1.0f) < 0.00001f,
              "parallel near plane maps to clip z=-w even when zero");
        check(fabsf(-far * m[10] + m[11] - 1.0f) < 0.00001f,
              "parallel far plane maps to clip z=w even when zero");
    }
}

/* ------------------------------------------------------------------ */
/* Image2D: mutable images and set(x, y, width, height, pixels)        */
/* ------------------------------------------------------------------ */
/*
 * JSR 184's mutable Image2D is built from (format, width, height) and filled
 * later with set(x, y, width, height, pixels).  The upload is a tightly
 * packed row-major rectangle whose bytes are RGB/RGBA/... according to the
 * image format, and the image owns a copy: the caller may reuse its array.
 *
 * The shipped runtime used to read set() as (format, width, height, pixels),
 * so a game's set(0, 0, w, h, px) stored format 0, width 0 and an int
 * argument masked into a ref.  The 3D viewport rendered black while the 2D
 * HUD stayed correct - exactly the failure this section pins down.
 *
 * Format is read through getFormat(): mutability lives in bit 0x100 of the
 * same word, so the raw _nj_fmt value is deliberately never asserted on.
 * The white-box peeks below are limited to the private storage fields, which
 * are the only way to see "storage is allocated lazily and reused" and
 * "an upload invalidates the mip chain".
 */
static int i2d_threw(nj_vm *vm, nj_status st, const char *want_class)
{
    nj_ref exc = nj_take_exception(vm);
    const char *name;

    if (st == NJ_OK || !exc) {
        return 0;
    }
    name = nj_ref_class_name(exc);
    return name != NULL && strstr(name, want_class) != NULL;
}

static nj_ref i2d_new3(nj_vm *vm, int fmt, int w, int h, int *ok)
{
    nj_value a[3];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));

    *ok = 0;
    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].i = w; a[2].i = h;
    *ok = nj_call_special(vm, img, CLS("Image2D"), "<init>", "(III)V",
                          a, NULL) == NJ_OK;
    return img;
}

/* Construct and report the exception class of a rejected (III) form. */
static int i2d_new3_saw(nj_vm *vm, int fmt, int w, int h,
                        const char *want_class)
{
    nj_value a[3];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));
    nj_status st;

    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].i = w; a[2].i = h;
    st = nj_call_special(vm, img, CLS("Image2D"), "<init>", "(III)V", a, NULL);
    return i2d_threw(vm, st, want_class);
}

static int i2d_new_bytes_saw(nj_vm *vm, int fmt, int w, int h, nj_ref px,
                             const char *want_class)
{
    nj_value a[4];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));
    nj_status st;

    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].i = w; a[2].i = h; a[3].r = px;
    st = nj_call_special(vm, img, CLS("Image2D"), "<init>", "(III[B)V",
                         a, NULL);
    return i2d_threw(vm, st, want_class);
}

static int i2d_new_paletted_saw(nj_vm *vm, int fmt, int w, int h,
                                nj_ref px, nj_ref pal,
                                const char *want_class)
{
    nj_value a[5];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));
    nj_status st;

    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].i = w; a[2].i = h; a[3].r = px; a[4].r = pal;
    st = nj_call_special(vm, img, CLS("Image2D"), "<init>", "(III[B[B)V",
                         a, NULL);
    return i2d_threw(vm, st, want_class);
}

static int i2d_new_obj_saw(nj_vm *vm, int fmt, nj_ref obj,
                           const char *want_class)
{
    nj_value a[2];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));
    nj_status st;

    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].r = obj;
    st = nj_call_special(vm, img, CLS("Image2D"), "<init>",
                         "(ILjava/lang/Object;)V", a, NULL);
    return i2d_threw(vm, st, want_class);
}

static int i2d_new_bytes2_saw(nj_vm *vm, int fmt, int h, nj_ref px,
                              const char *want_class)
{
    nj_value a[3];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));
    nj_status st;

    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].i = h; a[2].r = px;
    st = nj_call_special(vm, img, CLS("Image2D"), "<init>", "(II[B)V",
                         a, NULL);
    return i2d_threw(vm, st, want_class);
}

static nj_ref i2d_new_bytes(nj_vm *vm, int fmt, int w, int h, nj_ref px,
                            int *ok)
{
    nj_value a[4];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));

    *ok = 0;
    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].i = w; a[2].i = h; a[3].r = px;
    *ok = nj_call_special(vm, img, CLS("Image2D"), "<init>", "(III[B)V",
                          a, NULL) == NJ_OK;
    return img;
}

static nj_ref i2d_new_paletted(nj_vm *vm, int fmt, int w, int h,
                               nj_ref px, nj_ref pal, int *ok)
{
    nj_value a[5];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));

    *ok = 0;
    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].i = w; a[2].i = h; a[3].r = px; a[4].r = pal;
    *ok = nj_call_special(vm, img, CLS("Image2D"), "<init>", "(III[B[B)V",
                          a, NULL) == NJ_OK;
    return img;
}

static nj_ref i2d_new_obj(nj_vm *vm, int fmt, nj_ref obj, int *ok)
{
    nj_value a[2];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));

    *ok = 0;
    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].r = obj;
    *ok = nj_call_special(vm, img, CLS("Image2D"), "<init>",
                          "(ILjava/lang/Object;)V", a, NULL) == NJ_OK;
    return img;
}

static nj_ref i2d_new_bytes2(nj_vm *vm, int fmt, int w, nj_ref px, int *ok)
{
    nj_value a[3];
    nj_ref img = nj_object_new(vm, CLS("Image2D"));

    *ok = 0;
    if (!img) {
        return 0;
    }
    a[0].i = fmt; a[1].i = w; a[2].r = px;
    *ok = nj_call_special(vm, img, CLS("Image2D"), "<init>", "(II[B)V",
                          a, NULL) == NJ_OK;
    return img;
}

static int i2d_set_ok(nj_vm *vm, nj_ref img, int x, int y, int w, int h,
                      nj_ref px)
{
    nj_value a[5];

    if (!img) {
        return 0;
    }
    a[0].i = x; a[1].i = y; a[2].i = w; a[3].i = h; a[4].r = px;
    return nj_call(vm, img, CLS("Image2D"), "set", "(IIII[B)V",
                   a, NULL) == NJ_OK;
}

static int i2d_set_saw(nj_vm *vm, nj_ref img, int x, int y, int w, int h,
                       nj_ref px, const char *want_class)
{
    nj_value a[5];
    nj_status st;

    if (!img) {
        return 0;
    }
    a[0].i = x; a[1].i = y; a[2].i = w; a[3].i = h; a[4].r = px;
    st = nj_call(vm, img, CLS("Image2D"), "set", "(IIII[B)V", a, NULL);
    return i2d_threw(vm, st, want_class);
}

static int i2d_int(nj_vm *vm, nj_ref img, const char *name)
{
    nj_value r;

    memset(&r, 0, sizeof(r));
    if (!img || nj_call(vm, img, CLS("Image2D"), name, "()I", NULL, &r) != NJ_OK) {
        return -12345;
    }
    return r.i;
}

static int i2d_mutable(nj_vm *vm, nj_ref img)
{
    nj_value r;

    memset(&r, 0, sizeof(r));
    if (!img || nj_call(vm, img, CLS("Image2D"), "isMutable", "()Z",
                        NULL, &r) != NJ_OK) {
        return -12345;
    }
    return r.i;
}

static nj_ref i2d_pixels(nj_vm *vm, nj_ref img)
{
    const nj_field *f;

    if (!img) {
        return 0;
    }
    f = nj_field_lookup(vm, CLS("Image2D"), "_nj_pixels", "[B", 0);
    return f ? nj_rget(vm, img, f) : 0;
}

static nj_ref i2d_mips(nj_vm *vm, nj_ref img)
{
    const nj_field *f;

    if (!img) {
        return 0;
    }
    f = nj_field_lookup(vm, CLS("Image2D"), "_nj_mips", "[B", 0);
    return f ? nj_rget(vm, img, f) : 0;
}

/* A deterministic byte pattern; the seed keeps formats distinguishable. */
static void i2d_pattern(nj_u1 *p, int n, unsigned seed)
{
    int k;

    for (k = 0; k < n; k++) {
        p[k] = (nj_u1)((seed + (unsigned)k * 7u) & 0xFFu);
    }
}

static int i2d_matches(nj_ref arr, const nj_u1 *want, int n)
{
    const nj_u1 *p;

    if (!arr || nj_array_length(arr) < (nj_u4)n) {
        return 0;
    }
    p = (const nj_u1 *)nj_array_data(arr);
    return p != NULL && memcmp(p, want, (size_t)n) == 0;
}

/* Overwrite the block at (x, y), size w*h*bpp, from a patch pattern. */
static void i2d_expect_patch(nj_u1 *want, int iw, int x, int y, int w, int h,
                             int bpp, unsigned seed)
{
    int row, col, byte;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            int src = (row * w + col) * bpp;
            int dst = (((y + row) * iw) + (x + col)) * bpp;

            for (byte = 0; byte < bpp; byte++) {
                want[dst + byte] =
                    (nj_u1)((seed + (unsigned)(src + byte) * 7u) & 0xFFu);
            }
        }
    }
}


/*
 * Does set() store (x, y, w, h, pixels) where it is supposed to?  This runs
 * before the white-box storage checks so a runtime that still has the old ABI
 * is reported by assertions instead of being dereferenced through a bogus ref.
 */
static int i2d_abi_ok(nj_vm *vm)
{
    nj_ref img;
    nj_ref src = nj_byte_array_new(vm, 64u);
    int ok = 0;

    if (!src) {
        return 0;
    }
    i2d_pattern((nj_u1 *)nj_array_data(src), 64, 0x31u);
    img = i2d_new3(vm, 100, 4, 4, &ok);
    if (!ok || !i2d_set_ok(vm, img, 0, 0, 4, 4, src)) {
        return 0;
    }
    if (i2d_int(vm, img, "getFormat") != 100 ||
        i2d_int(vm, img, "getWidth") != 4 ||
        i2d_int(vm, img, "getHeight") != 4 ||
        i2d_mutable(vm, img) != 1) {
        return 0;
    }
    return i2d_pixels(vm, img) != 0 &&
           nj_array_length(i2d_pixels(vm, img)) == 64u;
}

/* ------------------------------------------------------------------ */
/* A textured quad that covers a 240x320 target, camera included.       */
/*
 * Parallel projection with a generic identity camera is the combination
 * tests/test_m3g.c already uses for camera-clipping pixels: model units are
 * clip units, so a quad spanning -1..1 paints the whole viewport, and the
 * texture's 0..1 coordinates land on it in the obvious way.  The quad sits
 * at z=-4, inside the 1..10 parallel near/far range.
 */
typedef struct {
    nj_ref g3d, camera, xf, vb, ib, app, poly, bg, target, pixels, tcva;
} i2d_canvas;

static int i2d_canvas_init(nj_vm *vm, i2d_canvas *c)
{
    nj_value a[6], r;
    nj_ref pos = nj_array_new(vm, "S", 12u);
    nj_ref tc = nj_array_new(vm, "S", 8u);
    nj_ref lens = nj_array_new(vm, "I", 1u);
    nj_ref posva, tcva;
    int ok = 1;

    memset(c, 0, sizeof(*c));
    if (!pos || !tc || !lens) {
        return 0;
    }
    {
        nj_s2 *p = (nj_s2 *)nj_array_data(pos);
        nj_s2 *t = (nj_s2 *)nj_array_data(tc);

        p[0] = -4; p[1] = -4; p[2] = -4;
        p[3] =  4; p[4] = -4; p[5] = -4;
        p[6] = -4; p[7] =  4; p[8] = -4;
        p[9] =  4; p[10] = 4; p[11] = -4;
        t[0] = 0; t[1] = 0;
        t[2] = 64; t[3] = 0;
        t[4] = 0; t[5] = 64;
        t[6] = 64; t[7] = 64;
    }
    ((nj_s4 *)nj_array_data(lens))[0] = 4;

    posva = nj_object_new(vm, CLS("VertexArray"));
    tcva = nj_object_new(vm, CLS("VertexArray"));
    c->tcva = tcva;
    c->vb = make(vm, CLS("VertexBuffer"));
    c->ib = nj_object_new(vm, CLS("TriangleStripArray"));
    c->app = make(vm, CLS("Appearance"));
    c->poly = make(vm, CLS("PolygonMode"));
    c->camera = make(vm, CLS("Camera"));
    c->xf = make(vm, CLS("Transform"));
    c->g3d = make(vm, CLS("Graphics3D"));
    c->bg = make(vm, CLS("Background"));
    if (!posva || !tcva || !c->vb || !c->ib || !c->app || !c->poly ||
        !c->camera || !c->xf || !c->g3d || !c->bg) {
        return 0;
    }

    a[0].i = 4; a[1].i = 3; a[2].i = 2;
    ok = ok && nj_call_special(vm, posva, CLS("VertexArray"), "<init>",
                               "(III)V", a, NULL) == NJ_OK;
    a[0].i = 0; a[1].i = 4; a[2].r = pos;
    ok = ok && nj_call(vm, posva, CLS("VertexArray"), "set", "(II[S)V",
                       a, NULL) == NJ_OK;
    a[0].r = posva; a[1].f = 0.5f; a[2].r = 0;
    ok = ok && nj_call(vm, c->vb, CLS("VertexBuffer"), "setPositions",
                       "(L" CLS("VertexArray") ";F[F)V", a, NULL) == NJ_OK;

    a[0].i = 4; a[1].i = 2; a[2].i = 2;
    ok = ok && nj_call_special(vm, tcva, CLS("VertexArray"), "<init>",
                               "(III)V", a, NULL) == NJ_OK;
    a[0].i = 0; a[1].i = 4; a[2].r = tc;
    ok = ok && nj_call(vm, tcva, CLS("VertexArray"), "set", "(II[S)V",
                       a, NULL) == NJ_OK;
    a[0].i = 0; a[1].r = tcva; a[2].f = 1.0f / 64.0f; a[3].r = 0;
    ok = ok && nj_call(vm, c->vb, CLS("VertexBuffer"), "setTexCoords",
                       "(IL" CLS("VertexArray") ";F[F)V", a, NULL) == NJ_OK;

    a[0].i = 0; a[1].r = lens;
    ok = ok && nj_call_special(vm, c->ib, CLS("TriangleStripArray"), "<init>",
                               "(I[I)V", a, NULL) == NJ_OK;

    a[0].i = 160;   /* CULL_NONE: both windings render. */
    ok = ok && nj_call(vm, c->poly, CLS("PolygonMode"), "setCulling",
                       "(I)V", a, NULL) == NJ_OK;
    a[0].r = c->poly;
    ok = ok && nj_call(vm, c->app, CLS("Appearance"), "setPolygonMode",
                       "(L" CLS("PolygonMode") ";)V", a, NULL) == NJ_OK;

    ok = ok && nj_call(vm, c->xf, CLS("Transform"), "setIdentity",
                       "()V", NULL, NULL) == NJ_OK;
    a[0].r = c->xf;
    ok = ok && nj_call(vm, c->camera, CLS("Camera"), "setGeneric",
                       "(L" CLS("Transform") ";)V", a, NULL) == NJ_OK;
    a[0].f = 4.0f; a[1].f = 1.0f; a[2].f = 1.0f; a[3].f = 10.0f;
    ok = ok && nj_call(vm, c->camera, CLS("Camera"), "setParallel",
                       "(FFFF)V", a, NULL) == NJ_OK;

    memset(&r, 0, sizeof(r));
    a[0].i = 240; a[1].i = 320;
    ok = ok && nj_call_static(vm, "javax/microedition/lcdui/Image",
                              "createImage",
                              "(II)Ljavax/microedition/lcdui/Image;",
                              a, &r) == NJ_OK && r.r != 0;
    c->target = r.r;
    a[0].r = c->target;
    ok = ok && nj_call(vm, c->target, "javax/microedition/lcdui/Image",
                       "getGraphics",
                       "()Ljavax/microedition/lcdui/Graphics;", NULL, &r) == NJ_OK;
    a[0].r = r.r;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "bindTarget",
                       "(Ljava/lang/Object;)V", a, NULL) == NJ_OK;
    a[0].i = 0; a[1].i = 0; a[2].i = 240; a[3].i = 320;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "setViewport",
                       "(IIII)V", a, NULL) == NJ_OK;
    a[0].r = c->camera; a[1].r = 0;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "setCamera",
                       "(L" CLS("Camera") ";L" CLS("Transform") ";)V",
                       a, NULL) == NJ_OK;
    c->pixels = nj_rget(vm, c->target,
                        nj_field_lookup(vm, "javax/microedition/lcdui/Image",
                                        "imgPixels", "[I", 0));
    return ok && c->pixels != 0;
}

static int i2d_canvas_draw(nj_vm *vm, i2d_canvas *c, nj_ref img)
{
    nj_value a[5];
    nj_ref tex;
    int ok = 1;

    tex = nj_object_new(vm, CLS("Texture2D"));
    if (!tex) {
        return 0;
    }
    a[0].r = c->bg;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "clear",
                       "(L" CLS("Background") ";)V", a, NULL) == NJ_OK;
    a[0].r = img;
    ok = ok && nj_call_special(vm, tex, CLS("Texture2D"), "<init>",
                               "(L" CLS("Image2D") ";)V", a, NULL) == NJ_OK;
    a[0].i = 228;   /* REPLACE: the texture colour is the pixel colour. */
    ok = ok && nj_call(vm, tex, CLS("Texture2D"), "setBlending",
                       "(I)V", a, NULL) == NJ_OK;
    a[0].i = 240; a[1].i = 240;   /* WRAP_CLAMP */
    ok = ok && nj_call(vm, tex, CLS("Texture2D"), "setWrapping",
                       "(II)V", a, NULL) == NJ_OK;
    a[0].i = 208; a[1].i = 208;   /* FILTER_BASE_LEVEL, NEAREST */
    ok = ok && nj_call(vm, tex, CLS("Texture2D"), "setFiltering",
                       "(II)V", a, NULL) == NJ_OK;
    a[0].i = 0; a[1].r = tex;
    ok = ok && nj_call(vm, c->app, CLS("Appearance"), "setTexture",
                       "(IL" CLS("Texture2D") ";)V", a, NULL) == NJ_OK;
    a[0].r = c->vb; a[1].r = c->ib; a[2].r = c->app; a[3].r = 0; a[4].i = 0;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "render",
                       "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                       CLS("Appearance") ";L" CLS("Transform") ";I)V",
                       a, NULL) == NJ_OK;
    return ok;
}

static nj_u4 i2d_canvas_pixel(const i2d_canvas *c, int x, int y)
{
    const nj_u4 *p = (const nj_u4 *)nj_array_data(c->pixels);

    return p[y * 240 + x] & 0xFFFFFFu;
}

/*
 * A lit quad: positions and normals in the same 240x320 parallel-projection
 * canvas, a white diffuse Material and one Light on the Graphics3D.  The
 * quad faces the camera, so a light pointing along -z lights it.  Used to pin
 * the spot-cone rule: a zero spot angle is a cone of no width and lights
 * nothing, exactly as the reference implementation computes it.
 */
static int i2d_canvas_render_lit(nj_vm *vm, i2d_canvas *c, nj_ref light)
{
    nj_value a[6], r;
    nj_ref nva, mat;
    int ok = 1;

    nva = nj_object_new(vm, CLS("VertexArray"));
    mat = make(vm, CLS("Material"));
    if (!nva || !mat) {
        return 0;
    }
    a[0].i = 4; a[1].i = 3; a[2].i = 2;          /* W, 3 floats, BYTE */
    ok = ok && nj_call_special(vm, nva, CLS("VertexArray"), "<init>",
                               "(III)V", a, NULL) == NJ_OK;
    {
        nj_ref nb = nj_array_new(vm, "S", 12u);
        nj_s2 mine[12];
        int k;

        for (k = 0; k < 4; k++) {                /* (0,0,1) at scale 1/64 */
            mine[k * 3 + 0] = 0;
            mine[k * 3 + 1] = 0;
            mine[k * 3 + 2] = 64;
        }
        memcpy(nj_array_data(nb), mine, sizeof mine);
        a[0].i = 0; a[1].i = 4; a[2].r = nb;
        ok = ok && nj_call(vm, nva, CLS("VertexArray"), "set", "(II[S)V",
                           a, NULL) == NJ_OK;
    }
    a[0].r = nva; a[1].f = 1.0f / 64.0f; a[2].r = 0;
    ok = ok && nj_call(vm, c->vb, CLS("VertexBuffer"), "setNormals",
                       "(L" CLS("VertexArray") ";)V", a, NULL) == NJ_OK;
    a[0].i = 2048; a[1].i = (nj_s4)0xFFFFFFFFu;   /* Material.DIFFUSE, white */
    ok = ok && nj_call(vm, mat, CLS("Material"), "setColor", "(II)V",
                       a, NULL) == NJ_OK;
    a[0].r = mat;
    ok = ok && nj_call(vm, c->app, CLS("Appearance"), "setMaterial",
                       "(L" CLS("Material") ";)V", a, NULL) == NJ_OK;
    a[0].r = c->bg;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "clear",
                       "(L" CLS("Background") ";)V", a, NULL) == NJ_OK;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "resetLights", "()V",
                       NULL, NULL) == NJ_OK;
    /*
     * Put the light behind the quad (the camera sits at the origin looking
     * down -z, the quad is at z=-4), so the quad lies along the light's own
     * axis and a wide cone must cover its middle.
     */
    /*
     * The camera sits at the origin looking down -z and the quad is at z=-4,
     * so put the light 10 units in front of the camera (z=+10): the quad is
     * then 12 units along the light's own -z axis, inside a 45 degree cone
     * (half angle 22.5, the quad's corners subtend 9.5 degrees) and facing
     * it, which is the configuration the specification describes.
     */
    a[0].f = 0.0f; a[1].f = 0.0f; a[2].f = 10.0f;
    ok = ok && nj_call(vm, c->xf, CLS("Transform"), "setIdentity", "()V",
                       NULL, NULL) == NJ_OK;
    ok = ok && nj_call(vm, c->xf, CLS("Transform"), "postTranslate", "(FFF)V",
                       a, NULL) == NJ_OK;
    memset(&r, 0, sizeof r);
    a[0].r = light; a[1].r = c->xf;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "addLight",
                       "(L" CLS("Light") ";L" CLS("Transform") ";)I",
                       a, &r) == NJ_OK && r.i == 0;
    a[0].r = c->vb; a[1].r = c->ib; a[2].r = c->app; a[3].r = 0; a[4].i = 0;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "render",
                       "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                       CLS("Appearance") ";L" CLS("Transform") ";I)V",
                       a, NULL) == NJ_OK;
    return ok;
}

static int i2d_canvas_lit_pixels(const i2d_canvas *c)
{
    int x, y, lit = 0;

    for (y = 40; y < 280; y++) {
        for (x = 20; x < 220; x++) {
            if (i2d_canvas_pixel(c, x, y) != 0u) {
                lit++;
            }
        }
    }
    return lit;
}

/* ------------------------------------------------------------------ */
/* The checks                                                          */
/* ------------------------------------------------------------------ */
static void test_image2d_set(nj_vm *vm)
{
    static const int formats[5] = {96, 97, 98, 99, 100};
    static const int bpps[5] = {1, 1, 2, 3, 4};
    nj_ref small8 = nj_byte_array_new(vm, 8u);
    nj_ref zeros64 = nj_byte_array_new(vm, 64u);
    int f, ok, abi;

    if (small8) {
        memset(nj_array_data(small8), 0, 8u);
    }
    if (zeros64) {
        memset(nj_array_data(zeros64), 0, 64u);
    }
    abi = i2d_abi_ok(vm);
    check(abi, "set() preserves image metadata and creates correctly sized storage");

    printf("== Image2D mutable constructor, getters, lazy storage ==\n");
    for (f = 0; f < 5; f++) {
        nj_ref img = i2d_new3(vm, formats[f], 5, 4, &ok);

        check(ok && img != 0, "mutable Image2D(fmt, 5, 4) constructs");
        check(i2d_int(vm, img, "getFormat") == formats[f],
              "getFormat returns the constructor format, not the mutable bit");
        check(i2d_int(vm, img, "getWidth") == 5,
              "getWidth returns the constructor width");
        check(i2d_int(vm, img, "getHeight") == 4,
              "getHeight returns the constructor height");
        check(i2d_mutable(vm, img) == 1,
              "the three-int constructor makes a mutable image");
        check(i2d_pixels(vm, img) == 0,
              "pixel storage is not allocated before the first set()");
    }

    check(i2d_new3_saw(vm, 95, 4, 4, "IllegalArgumentException"),
          "Image2D(95, ...) is rejected");
    check(i2d_new3_saw(vm, 101, 4, 4, "IllegalArgumentException"),
          "Image2D(101, ...) is rejected");
    check(i2d_new3_saw(vm, 356, 4, 4, "IllegalArgumentException"),
          "Image2D(0x164, ...) cannot spoof the mutable bit through the format");
    check(i2d_new3_saw(vm, 100, 0, 4, "IllegalArgumentException"),
          "Image2D(100, 0, 4) is rejected");
    check(i2d_new3_saw(vm, 100, 4, 0, "IllegalArgumentException"),
          "Image2D(100, 4, 0) is rejected");
    check(i2d_new3_saw(vm, 100, -1, 4, "IllegalArgumentException"),
          "Image2D(100, -1, 4) is rejected");
    check(i2d_new3_saw(vm, 100, 4, -1, "IllegalArgumentException"),
          "Image2D(100, 4, -1) is rejected");

    printf("== Image2D immutable constructors and format validation ==\n");
    check(i2d_new_bytes_saw(vm, 95, 2, 2, small8, "IllegalArgumentException"),
          "Image2D(byte[] form) rejects format 95");
    check(i2d_new_paletted_saw(vm, 95, 2, 2, small8, small8,
                               "IllegalArgumentException"),
          "Image2D(palette form) rejects format 95");
    check(i2d_new_obj_saw(vm, 95, small8, "IllegalArgumentException"),
          "Image2D(Object form) rejects format 95");
    check(i2d_new_bytes2_saw(vm, 95, 2, small8, "IllegalArgumentException"),
          "Image2D(int, byte[]) form rejects format 95");
    check(i2d_new_bytes_saw(vm, 356, 2, 2, small8,
                            "IllegalArgumentException"),
          "Image2D(byte[] form) rejects a spoofed format 0x164");
    {
        nj_ref pal = nj_byte_array_new(vm, 16u);
        nj_ref i_bytes, i_pal, i_obj, i_b2;

        if (pal) {
            memset(nj_array_data(pal), 0, 16u);
        }
        i_bytes = i2d_new_bytes(vm, 100, 2, 2, small8, &ok);
        i_pal = i2d_new_paletted(vm, 98, 2, 2, small8, pal, &ok);
        i_obj = i2d_new_obj(vm, 100, small8, &ok);
        i_b2 = i2d_new_bytes2(vm, 99, 2, small8, &ok);

        check(i2d_mutable(vm, i_bytes) == 0, "Image2D(byte[] form) is immutable");
        check(i2d_mutable(vm, i_pal) == 0, "Image2D(palette form) is immutable");
        check(i2d_mutable(vm, i_obj) == 0, "Image2D(Object form) is immutable");
        check(i2d_mutable(vm, i_b2) == 0, "Image2D(int, byte[]) form is immutable");

        check(i2d_set_saw(vm, i_bytes, 0, 0, 1, 1, small8,
                          "IllegalStateException"),
              "set() on an Image2D(byte[]) is refused as immutable");
        check(i2d_set_saw(vm, i_pal, 0, 0, 1, 1, small8,
                          "IllegalStateException"),
              "set() on an Image2D(palette) is refused as immutable");
        check(i2d_set_saw(vm, i_obj, 0, 0, 1, 1, small8,
                          "IllegalStateException"),
              "set() on an Image2D(Object) is refused as immutable");
        check(i2d_set_saw(vm, i_b2, 0, 0, 1, 1, small8,
                          "IllegalStateException"),
              "set() on an Image2D(int, byte[]) is refused as immutable");
        check(i2d_set_saw(vm, i_bytes, 0, 0, 1, 1, 0,
                          "NullPointerException"),
              "a null pixel buffer is a NullPointerException, checked before mutability");
    }

    if (abi) {
    printf("== set(x, y, w, h, pixels): ABI, storage reuse, own copy ==\n");
    {
        nj_ref img = i2d_new3(vm, 100, 4, 4, &ok);
        nj_ref src = nj_byte_array_new(vm, 64u);
        nj_ref ref1, patch, patch2, src3;
        nj_u1 want[64], want2[64], want3[64];

        check(ok, "mutable RGBA image for set() constructs");
        i2d_pattern((nj_u1 *)nj_array_data(src), 64, 0x20u);
        i2d_pattern(want, 64, 0x20u);
        check(i2d_set_ok(vm, img, 0, 0, 4, 4, src),
              "set(0, 0, 4, 4, 64 bytes) succeeds");
        check(i2d_pixels(vm, img) != 0,
              "the first set() allocates the image's own pixel storage");
        check(nj_array_length(i2d_pixels(vm, img)) == 64u,
              "RGBA 4x4 storage is 64 bytes");
        check(i2d_matches(i2d_pixels(vm, img), want, 64),
              "set() copies the source bytes row-major");
        check(i2d_int(vm, img, "getFormat") == 100 &&
              i2d_int(vm, img, "getWidth") == 4 &&
              i2d_int(vm, img, "getHeight") == 4,
              "set() leaves format, width and height alone");
        check(i2d_mutable(vm, img) == 1,
              "set() preserves the mutable flag");

        ref1 = i2d_pixels(vm, img);
        patch = nj_byte_array_new(vm, 16u);
        i2d_pattern((nj_u1 *)nj_array_data(patch), 16, 0x77u);
        check(i2d_set_ok(vm, img, 1, 1, 2, 2, patch),
              "a sub-rectangle update succeeds");
        check(i2d_pixels(vm, img) == ref1,
              "a later set() reuses the storage allocated by the first one");
        memcpy(want2, want, 64);
        i2d_expect_patch(want2, 4, 1, 1, 2, 2, 4, 0x77u);
        check(i2d_matches(i2d_pixels(vm, img), want2, 64),
              "a sub-rectangle update changes only the covered bytes");
        memset(nj_array_data(patch), 0xEE, 16u);
        check(i2d_matches(i2d_pixels(vm, img), want2, 64),
              "set() owns its copy: mutating the source afterwards does not alias");

        patch2 = nj_byte_array_new(vm, 16u);
        i2d_pattern((nj_u1 *)nj_array_data(patch2), 16, 0x33u);
        check(i2d_set_ok(vm, img, 2, 2, 2, 2, patch2),
              "a second, overlapping sub-rectangle update succeeds");
        check(i2d_pixels(vm, img) == ref1,
              "the second update still reuses the same storage");
        memcpy(want3, want2, 64);
        i2d_expect_patch(want3, 4, 2, 2, 2, 2, 4, 0x33u);
        check(i2d_matches(i2d_pixels(vm, img), want3, 64),
              "the second update overwrites exactly its rectangle");
        check(i2d_int(vm, img, "getFormat") == 100 &&
              i2d_mutable(vm, img) == 1,
              "format and mutability survive repeated sub-rectangle updates");

        src3 = nj_byte_array_new(vm, 64u);
        i2d_pattern((nj_u1 *)nj_array_data(src3), 64, 0x44u);
        check(i2d_set_ok(vm, img, 0, 0, 4, 4, src3),
              "a full-image set() after sub-rectangles succeeds");
        check(i2d_pixels(vm, img) == ref1 &&
              i2d_matches(i2d_pixels(vm, img),
                          (i2d_pattern((nj_u1 *)want, 64, 0x44u), want), 64),
              "the full set() reuses storage and replaces every byte");
    }

    printf("== set() per format: row copy, preservation, own copy ==\n");
    for (f = 0; f < 5; f++) {
        int bpp = bpps[f];
        int iw = 6, ih = 5, n = iw * ih * bpp, np = 3 * 2 * bpp;
        nj_ref img = i2d_new3(vm, formats[f], iw, ih, &ok);
        nj_ref src = nj_byte_array_new(vm, (nj_u4)n);
        nj_ref patch = nj_byte_array_new(vm, (nj_u4)np);
        nj_u1 want[6 * 5 * 4];

        check(ok, "mutable image for the per-format set() checks constructs");
        i2d_pattern((nj_u1 *)nj_array_data(src), n, 0x10u + (unsigned)f);
        i2d_pattern(want, n, 0x10u + (unsigned)f);
        check(i2d_set_ok(vm, img, 0, 0, iw, ih, src),
              "a full-image set() succeeds for this format");
        check(nj_array_length(i2d_pixels(vm, img)) == (nj_u4)n,
              "storage size is width*height*bytes-per-pixel");
        check(i2d_matches(i2d_pixels(vm, img), want, n),
              "the bytes land row-major in the image storage");
        memset(nj_array_data(src), 0xEE, (size_t)n);
        check(i2d_matches(i2d_pixels(vm, img), want, n),
              "the image owns its copy for this format");
        i2d_pattern((nj_u1 *)nj_array_data(patch), np, 0x55u);
        i2d_expect_patch(want, iw, 1, 2, 3, 2, bpp, 0x55u);
        check(i2d_set_ok(vm, img, 1, 2, 3, 2, patch),
              "a sub-rectangle set() succeeds for this format");
        check(i2d_matches(i2d_pixels(vm, img), want, n),
              "the sub-rectangle bytes count bytes-per-pixel");
        memset(nj_array_data(patch), 0xEE, (size_t)np);
        check(i2d_matches(i2d_pixels(vm, img), want, n),
              "the sub-rectangle copy is owned for this format");
        check(i2d_int(vm, img, "getFormat") == formats[f] &&
              i2d_int(vm, img, "getWidth") == iw &&
              i2d_int(vm, img, "getHeight") == ih &&
              i2d_mutable(vm, img) == 1,
              "format, size and mutability stay constant across set()");
    }

    }
    if (!abi) {
        printf("  SKIP: the storage checks need the corrected set() ABI\n");
    }

    printf("== set() rejects bad regions, short buffers and null ==\n");
    {
        nj_ref img = i2d_new3(vm, 100, 4, 4, &ok);
        nj_ref short63 = nj_byte_array_new(vm, 63u);
        nj_ref short15 = nj_byte_array_new(vm, 15u);
        nj_ref src16 = nj_byte_array_new(vm, 16u);
        nj_ref big65 = nj_byte_array_new(vm, 65u);

        check(ok, "mutable RGBA image for the rejection checks constructs");
        if (short63) { memset(nj_array_data(short63), 0, 63u); }
        if (short15) { memset(nj_array_data(short15), 0, 15u); }
        if (src16) { i2d_pattern((nj_u1 *)nj_array_data(src16), 16, 0x66u); }
        if (big65) { i2d_pattern((nj_u1 *)nj_array_data(big65), 65, 0x22u); }

        check(i2d_set_saw(vm, img, 0, 0, 4, 4, 0, "NullPointerException"),
              "set() with a null pixel buffer throws NullPointerException");
        check(i2d_set_saw(vm, img, -1, 0, 1, 1, zeros64,
                          "IllegalArgumentException"),
              "a negative x is rejected");
        check(i2d_set_saw(vm, img, 0, -1, 1, 1, zeros64,
                          "IllegalArgumentException"),
              "a negative y is rejected");
        check(i2d_set_saw(vm, img, 0, 0, 0, 1, zeros64,
                          "IllegalArgumentException"),
              "a zero width is rejected");
        check(i2d_set_saw(vm, img, 0, 0, 1, 0, zeros64,
                          "IllegalArgumentException"),
              "a zero height is rejected");
        check(i2d_set_saw(vm, img, 0, 0, -1, 1, zeros64,
                          "IllegalArgumentException"),
              "a negative width is rejected");
        check(i2d_set_saw(vm, img, 0, 0, 1, -1, zeros64,
                          "IllegalArgumentException"),
              "a negative height is rejected");
        check(i2d_set_saw(vm, img, 4, 0, 1, 1, zeros64,
                          "IllegalArgumentException"),
              "a left edge at the image width is rejected");
        check(i2d_set_saw(vm, img, 5, 0, 1, 1, zeros64,
                          "IllegalArgumentException"),
              "an x beyond the image is rejected");
        check(i2d_set_saw(vm, img, 0, 4, 1, 1, zeros64,
                          "IllegalArgumentException"),
              "a top edge at the image height is rejected");
        check(i2d_set_saw(vm, img, 2, 0, 3, 1, zeros64,
                          "IllegalArgumentException"),
              "x + width beyond the image is rejected");
        check(i2d_set_saw(vm, img, 0, 2, 1, 3, zeros64,
                          "IllegalArgumentException"),
              "y + height beyond the image is rejected");
        check(i2d_set_saw(vm, img, 0, 0, 4, 4, short63,
                          "IllegalArgumentException"),
              "a buffer one byte short is rejected");
        check(i2d_set_saw(vm, img, 0, 0, 2, 2, short15,
                          "IllegalArgumentException"),
              "a sub-rectangle buffer one byte short is rejected");
        check(i2d_set_ok(vm, img, 0, 0, 2, 2, src16),
              "a buffer of exactly the sub-rectangle size is accepted");
        check(i2d_set_ok(vm, img, 0, 0, 4, 4, big65),
              "a buffer longer than needed is accepted");
        check(i2d_int(vm, img, "getFormat") == 100,
              "the rejected calls left the image format intact");
    }

    printf("== set() extreme bounds: no overflow, no wild allocation ==\n");
    {
        nj_ref one = nj_byte_array_new(vm, 1u);
        nj_ref four = nj_byte_array_new(vm, 4u);
        nj_ref wide96 = i2d_new3(vm, 96, 65536, 65536, &ok);
        nj_ref wide100 = i2d_new3(vm, 100, 65536, 65536, &ok);
        nj_ref small = i2d_new3(vm, 100, 4, 4, &ok);

        if (one) { memset(nj_array_data(one), 0, 1u); }
        if (four) { memset(nj_array_data(four), 0, 4u); }

        check(i2d_set_saw(vm, small, 0x7FFFFFFF, 0, 1, 1, zeros64,
                          "IllegalArgumentException"),
              "x = INT_MAX is rejected without overflowing x + width");
        check(i2d_set_saw(vm, small, 0, 0x7FFFFFFF, 1, 1, zeros64,
                          "IllegalArgumentException"),
              "y = INT_MAX is rejected without overflowing y + height");
        check(i2d_set_saw(vm, small, -0x7FFFFFFF - 1, 0, 1, 1, zeros64,
                          "IllegalArgumentException"),
              "x = INT_MIN is rejected without overflowing x + width");
        check(i2d_set_saw(vm, small, 0, 0, 0x7FFFFFFF, 1, zeros64,
                          "IllegalArgumentException"),
              "width = INT_MAX is rejected without overflowing x + width");
        check(i2d_set_saw(vm, small, 0, 0, 1, 0x7FFFFFFF, zeros64,
                          "IllegalArgumentException"),
              "height = INT_MAX is rejected without overflowing y + height");
        check(i2d_set_saw(vm, wide96, 0, 0, 1, 1, one, "OutOfMemoryError"),
              "a one-byte format whose full storage exceeds 2G reports OOM, not a crash");
        check(i2d_set_saw(vm, wide100, 0, 0, 1, 1, four, "OutOfMemoryError"),
              "an RGBA format whose full storage exceeds 2G reports OOM, not a crash");
        check(i2d_pixels(vm, wide96) == 0 && i2d_pixels(vm, wide100) == 0,
              "the OOM path leaves no half-built storage behind");
    }

    if (abi) {
    printf("== set() detects storage that cannot hold the image ==\n");
    {
        nj_ref img = i2d_new3(vm, 100, 2, 2, &ok);
        nj_ref src = nj_byte_array_new(vm, 16u);
        nj_ref tiny = nj_byte_array_new(vm, 8u);
        const nj_field *pf;

        if (src) { i2d_pattern((nj_u1 *)nj_array_data(src), 16, 0x11u); }
        if (tiny) { memset(nj_array_data(tiny), 0, 8u); }
        check(ok && i2d_set_ok(vm, img, 0, 0, 2, 2, src),
              "the image for the invalid-storage check has valid storage");
        pf = nj_field_lookup(vm, CLS("Image2D"), "_nj_pixels", "[B", 0);
        if (pf && tiny) {
            nj_field_write_ref(vm, img, pf, tiny);
        }
        check(i2d_set_saw(vm, img, 0, 0, 1, 1, src, "IllegalStateException"),
              "storage shorter than the image is reported, not overrun");
    }

    printf("== Image2D rendered pixels: RGB and RGBA textures ==\n");
    {
        i2d_canvas canvas;
        nj_ref rgba1 = i2d_new3(vm, 100, 1, 1, &ok);
        nj_ref rgb1 = i2d_new3(vm, 99, 1, 1, &ok);
        nj_ref rgba2 = i2d_new3(vm, 100, 2, 1, &ok);
        nj_ref single = nj_byte_array_new(vm, 4u);
        nj_ref single_rgb = nj_byte_array_new(vm, 3u);
        nj_ref two = nj_byte_array_new(vm, 8u);
        nj_ref update = nj_byte_array_new(vm, 4u);
        nj_ref immutable_img;
        nj_u1 rgba_red[4] = {255, 0, 0, 255};
        nj_u1 rgb_blue[3] = {0, 0, 255};
        nj_u1 rgba_left_right[8] = {255, 0, 0, 255, 0, 255, 0, 255};
        nj_u1 rgba_yellow[4] = {255, 255, 0, 255};
        nj_u1 one_red[4] = {255, 0, 0, 255};
        int canvas_ok;

        memcpy(nj_array_data(single), rgba_red, 4u);
        memcpy(nj_array_data(single_rgb), rgb_blue, 3u);
        memcpy(nj_array_data(two), rgba_left_right, 8u);
        memcpy(nj_array_data(update), rgba_yellow, 4u);

        canvas_ok = i2d_canvas_init(vm, &canvas);
        check(canvas_ok, "the textured-quad render harness initialises");
        if (canvas_ok) {
            check(i2d_set_ok(vm, rgba1, 0, 0, 1, 1, single),
                  "a 1x1 RGBA mutable image accepts its pixel");
            check(i2d_canvas_draw(vm, &canvas, rgba1),
                  "drawing with a 1x1 RGBA texture succeeds");
            check(i2d_canvas_pixel(&canvas, 40, 40) == 0xFF0000u &&
                  i2d_canvas_pixel(&canvas, 200, 40) == 0xFF0000u &&
                  i2d_canvas_pixel(&canvas, 40, 280) == 0xFF0000u &&
                  i2d_canvas_pixel(&canvas, 120, 160) == 0xFF0000u &&
                  i2d_canvas_pixel(&canvas, 200, 280) == 0xFF0000u,
                  "the RGBA texel red reaches the rendered target");

            check(i2d_set_ok(vm, rgb1, 0, 0, 1, 1, single_rgb),
                  "a 1x1 RGB mutable image accepts its pixel");
            check(i2d_canvas_draw(vm, &canvas, rgb1),
                  "drawing with a 1x1 RGB texture succeeds");
            check(i2d_canvas_pixel(&canvas, 40, 70) == 0x0000FFu &&
                  i2d_canvas_pixel(&canvas, 200, 250) == 0x0000FFu,
                  "the RGB texel blue reaches the rendered target");

            check(i2d_set_ok(vm, rgba2, 0, 0, 2, 1, two),
                  "a 2x1 RGBA image accepts two texels");
            check(i2d_canvas_draw(vm, &canvas, rgba2),
                  "drawing with a 2x1 RGBA texture succeeds");
            check(i2d_canvas_pixel(&canvas, 60, 160) == 0xFF0000u &&
                  i2d_canvas_pixel(&canvas, 180, 160) == 0x00FF00u,
                  "the two texels land on the two halves of the quad");
            check(i2d_mips(vm, rgba2) != 0 &&
                  nj_array_length(i2d_mips(vm, rgba2)) == 4u,
                  "a 2x1 RGBA texture caches only its 1x1 mip tail");
            check(i2d_mips(vm, rgba1) == 0,
                  "a 1x1 RGBA texture needs no mip cache");

            check(i2d_set_ok(vm, rgba2, 1, 0, 1, 1, update),
                  "a sub-rectangle update of a texture succeeds");
            check(i2d_mips(vm, rgba2) == 0,
                  "an upload invalidates the cached mip chain");
            check(i2d_canvas_draw(vm, &canvas, rgba2),
                  "drawing after the texture update succeeds");
            check(i2d_canvas_pixel(&canvas, 180, 160) == 0xFFFF00u,
                  "the updated texel reaches the rendered target");
            check(i2d_canvas_pixel(&canvas, 60, 160) == 0xFF0000u,
                  "the untouched texel still renders its old colour");
            check(i2d_mips(vm, rgba2) != 0,
                  "the next render rebuilds the mip chain");
            {
                static const nj_s2 mixed_uv[8] = {
                    -32767, -32767, 32767, -32767, -32767, 32767, 0, 0
                };
                static const nj_u1 average[4] = {255, 128, 0, 255};
                nj_ref uv = nj_array_new(vm, "S", 8u);
                nj_value args[3];
                memcpy(nj_array_data(uv), mixed_uv, sizeof(mixed_uv));
                args[0].i = 0; args[1].i = 4; args[2].r = uv;
                check(nj_call(vm, canvas.tcva, CLS("VertexArray"), "set",
                              "(II[S)V", args, NULL) == NJ_OK &&
                      i2d_canvas_draw(vm, &canvas, rgba2),
                      "one strip renders a minified triangle before a base-level triangle");
                check(i2d_canvas_pixel(&canvas, 60, 240) == 0xFF8000u,
                      "the minified triangle samples the rounded mip average");
                check(i2d_canvas_pixel(&canvas, 180, 80) == 0xFFFF00u,
                      "the following triangle restores the original base pixels");
                check(nj_array_length(i2d_mips(vm, rgba2)) == 4u &&
                      memcmp(nj_array_data(i2d_mips(vm, rgba2)), average, 4u) == 0,
                      "the rebuilt tail keeps the existing RGBA box-filter rounding");
            }

            immutable_img = i2d_new_bytes(vm, 100, 1, 1, single, &ok);
            memcpy(nj_array_data(single), one_red, 4u);
            check(ok && i2d_mutable(vm, immutable_img) == 0,
                  "an immutable RGBA image for rendering constructs");
            check(i2d_canvas_draw(vm, &canvas, immutable_img),
                  "drawing with an immutable RGBA texture succeeds");
            check(i2d_canvas_pixel(&canvas, 120, 160) == 0xFF0000u,
                  "an immutable RGBA texel renders too");
        }
    }
    }
}

/* Clamp and repeat must write the same bytes for coordinates within one
 * texture repetition. Both modes can use the flat nearest spans; the wider
 * mode/target tests also support captures against NJ_M3G_FAST_PATHS=0. */
static int i2d_canvas_render_tex(nj_vm *vm, i2d_canvas *c, nj_ref img,
                                 nj_ref mat, int wrap)
{
    nj_value a[5];
    nj_ref tex;
    int ok = 1;

    tex = nj_object_new(vm, CLS("Texture2D"));
    if (!tex) {
        return 0;
    }
    a[0].r = c->bg;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "clear",
                       "(L" CLS("Background") ";)V", a, NULL) == NJ_OK;
    a[0].r = img;
    ok = ok && nj_call_special(vm, tex, CLS("Texture2D"), "<init>",
                               "(L" CLS("Image2D") ";)V", a, NULL) == NJ_OK;
    a[0].i = 227;                       /* FUNC_MODULATE */
    ok = ok && nj_call(vm, tex, CLS("Texture2D"), "setBlending",
                       "(I)V", a, NULL) == NJ_OK;
    a[0].i = wrap; a[1].i = wrap;       /* 241 REPEAT, 240 CLAMP */
    ok = ok && nj_call(vm, tex, CLS("Texture2D"), "setWrapping",
                       "(II)V", a, NULL) == NJ_OK;
    a[0].i = 208; a[1].i = 208;         /* FILTER_BASE_LEVEL, NEAREST */
    ok = ok && nj_call(vm, tex, CLS("Texture2D"), "setFiltering",
                       "(II)V", a, NULL) == NJ_OK;
    a[0].r = mat;
    ok = ok && nj_call(vm, c->app, CLS("Appearance"), "setMaterial",
                       "(L" CLS("Material") ";)V", a, NULL) == NJ_OK;
    a[0].i = 0; a[1].r = tex;
    ok = ok && nj_call(vm, c->app, CLS("Appearance"), "setTexture",
                       "(IL" CLS("Texture2D") ";)V", a, NULL) == NJ_OK;
    a[0].r = c->vb; a[1].r = c->ib; a[2].r = c->app; a[3].r = 0; a[4].i = 0;
    ok = ok && nj_call(vm, c->g3d, CLS("Graphics3D"), "render",
                       "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                       CLS("Appearance") ";L" CLS("Transform") ";I)V",
                       a, NULL) == NJ_OK;
    return ok;
}

static void test_spot_light_cone(nj_vm *vm)
{
    i2d_canvas canvas;
    nj_ref light;
    int lit_cone, lit_zero, ok;

    printf("== a spot light lights inside its cone and nothing at angle 0 ==\n");
    light = make(vm, CLS("Light"));
    ok = i2d_canvas_init(vm, &canvas);
    check(light != 0 && ok, "a canvas and a Light for the cone check");
    if (!light || !ok) {
        return;
    }
    {
        nj_value a[4];

        a[0].i = 131;                            /* Light.SPOT */
        call(vm, light, CLS("Light"), "setMode", "(I)V", a, NULL);
        a[0].i = 0x00FFFFFF;
        call(vm, light, CLS("Light"), "setColor", "(I)V", a, NULL);
        a[0].f = 1.0f;
        call(vm, light, CLS("Light"), "setIntensity", "(F)V", a, NULL);
        a[0].f = 1.0f; a[1].f = 0.0f; a[2].f = 0.0f;
        call(vm, light, CLS("Light"), "setAttenuation", "(FFF)V", a, NULL);
        a[0].f = 45.0f;
        call(vm, light, CLS("Light"), "setSpotAngle", "(F)V", a, NULL);
    }
    nj_clear_exception(vm);

    (void)i2d_canvas_render_lit(vm, &canvas, light);
    lit_cone = i2d_canvas_lit_pixels(&canvas);
    check(lit_cone > 30000, "a 45 degree cone lights the quad");

    {
        nj_value a[1];

        a[0].f = 0.0f;
        call(vm, light, CLS("Light"), "setSpotAngle", "(F)V", a, NULL);
        check(!nj_has_exception(vm), "angle 0 is an accepted argument");
        nj_clear_exception(vm);
    }
    (void)i2d_canvas_render_lit(vm, &canvas, light);
    lit_zero = i2d_canvas_lit_pixels(&canvas);
    check(lit_zero == 0,
          "and a cone of no width lights nothing, as the reference computes");
}

static void test_textured_span_variants(nj_vm *vm)
{
    /* Two texel colours that survive RGB565 losslessly, mixed with white and
     * black so the set of colours in the target is unambiguous. */
    static const nj_u1 texels[2 * 2 * 4] = {
        255,   0,   0, 255,
          0, 255,   0, 255,
          0,   0, 255, 255,
        255, 255, 255, 255
    };
    static const nj_u4 want[4] = { 0xFF0000u, 0x00FF00u, 0x0000FFu, 0xFFFFFFu };
    i2d_canvas canvas;
    nj_ref rgba, rgb, bytes, rgbbytes, mat;
    nj_s4 *clamp_px = NULL;
    int ok = 1, canvas_ok, k, found, distinct = 0, all_known = 1;

    printf("== textured spans: WRAP_REPEAT and MODULATE match the clamped "
           "loop ==\n");
    rgba = i2d_new3(vm, 100, 2, 2, &ok);        /* ARGB8888 */
    rgb  = i2d_new3(vm, 99, 2, 2, &ok);         /* RGB888 */
    bytes = nj_byte_array_new(vm, 16u);
    rgbbytes = nj_byte_array_new(vm, 12u);
    mat = make(vm, CLS("Material"));
    canvas_ok = i2d_canvas_init(vm, &canvas);
    check(rgba && rgb && bytes && rgbbytes && mat && canvas_ok,
          "the textures, a material and the render harness exist");
    if (!rgba || !rgb || !bytes || !rgbbytes || !mat || !canvas_ok) {
        return;
    }
    memcpy(nj_array_data(bytes), texels, sizeof(texels));
    {
        nj_u1 *t = (nj_u1 *)nj_array_data(rgbbytes);
        int j;

        for (j = 0; j < 4; j++) {
            t[j * 3 + 0] = texels[j * 4 + 0];
            t[j * 3 + 1] = texels[j * 4 + 1];
            t[j * 3 + 2] = texels[j * 4 + 2];
        }
    }
    check(i2d_set_ok(vm, rgba, 0, 0, 2, 2, bytes),
          "a 2x2 RGBA texture accepts its four texels");
    check(i2d_set_ok(vm, rgb, 0, 0, 2, 2, rgbbytes),
          "and so does the 2x2 RGB one (three bytes per texel are read)");
    /* A white diffuse keeps MODULATE an identity, so the expected colours are
     * the texels themselves. */
    {
        nj_value a[2], r;

        a[0].i = 2048;                  /* Material.DIFFUSE */
        a[1].i = (nj_s4)0xFFFFFFFFu;
        r.i = 0;
        call(vm, mat, CLS("Material"), "setColor", "(II)V", a, &r);
    }
    clamp_px = (nj_s4 *)malloc(240u * 320u * sizeof(nj_s4));
    check(clamp_px != NULL, "the comparison buffer exists");
    if (!clamp_px) {
        return;
    }
    check(i2d_canvas_render_tex(vm, &canvas, rgba, mat, 240),
          "a CLAMP texture renders through the flat nearest loop");
    memcpy(clamp_px, nj_array_data(canvas.pixels), 240u * 320u * sizeof(nj_s4));
    check(i2d_canvas_render_tex(vm, &canvas, rgba, mat, 241),
          "the same texture with REPEAT renders through the specialised loop");
    {
        const nj_s4 *now = (const nj_s4 *)nj_array_data(canvas.pixels);
        int same = 1;

        for (k = 0; k < 240 * 320; k++) {
            if ((now[k] & 0xFFFFFF) != (clamp_px[k] & 0xFFFFFF)) {
                same = 0;
                break;
            }
        }
        check(same, "the repeated draw is byte-identical to the clamped one");
        /* One column at the right edge falls outside the quad, so the colour
         * set is checked over the interior only. */
        for (k = 0; k < 240 * 320; k++) {
            int x = k % 240, y = k / 240, j;

            if (x < 4 || x > 235 || y < 4 || y > 315) {
                continue;
            }
            found = 0;
            for (j = 0; j < 4; j++) {
                if ((nj_u4)(now[k] & 0xFFFFFF) == want[j]) { found = 1; }
            }
            if (!found) { all_known = 0; }
        }
        for (k = 0; k < 4; k++) {
            int seen = 0, y, x;

            for (y = 4; y < 316 && !seen; y++) {
                for (x = 4; x < 236; x++) {
                    if ((nj_u4)(now[y * 240 + x] & 0xFFFFFF) == want[k]) {
                        seen = 1;
                        break;
                    }
                }
            }
            distinct += seen;
        }
        check(all_known, "every pixel is one of the four modulated texels");
        check(distinct == 4, "and all four texels appear");
    }
    /* The RGB texture takes the three-byte texel path (alpha reads as 255). */
    check(i2d_canvas_render_tex(vm, &canvas, rgb, mat, 241),
          "an RGB texture renders through the specialised loop too");
    {
        const nj_s4 *now = (const nj_s4 *)nj_array_data(canvas.pixels);
        int same = 1;

        for (k = 0; k < 240 * 320; k++) {
            if ((now[k] & 0xFFFFFF) != (clamp_px[k] & 0xFFFFFF)) {
                same = 0;
                break;
            }
        }
        check(same, "an RGB texture writes the same pixels as the RGBA one");
    }
    free(clamp_px);
}

/*
 * SkinnedMesh.addTransform used to fill a fixed 64-slot Object[] and throw
 * IndexOutOfBoundsException on the 65th entry.  A real title
 * (3D激爆拳击_8805) builds rigs of several hundred links at load time; the
 * exception was caught by the game, which then waited on its LOADING screen
 * forever.  The roster now starts at those same 64 slots and doubles, so the
 * <=64-bone case keeps its single allocation and larger rigs are accepted.
 * These checks pin the boundary, the growth (old entries survive) and the
 * "no bones = no allocation" edge, in the same style as the rest of the file:
 * after the VM-side native ran, the Java fields the game would see must hold
 * exactly what the calls asked for.
 */
static void test_skinned_mesh_bones(nj_vm *vm)
{
    static const char *skin = CLS("SkinnedMesh");
    nj_value a[4];
    nj_ref mesh = nj_object_new(vm, skin);
    nj_ref node;
    nj_ref roster;
    nj_field *fb;
    nj_field *fn;
    nj_status st;
    nj_s4 i;

    printf("== the SkinnedMesh bone roster grows past 64 ==\n");
    check(mesh != 0, "new SkinnedMesh object");
    memset(a, 0, sizeof(a));
    a[3].r = make(vm, CLS("Group"));
    call(vm, mesh, skin, "<init>",
         "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
         CLS("Appearance") ";L" CLS("Group") ";)V", a, NULL);
    nj_clear_exception(vm);

    fb = nj_field_lookup(vm, skin, "_nj_bones",
                         "[L" "javax/microedition/m3g/Node" ";", 0);
    fn = nj_field_lookup(vm, skin, "_nj_nbones", "I", 0);
    check(fb != 0 && fn != 0, "SkinnedMesh keeps a bone roster and a count");
    if (!fb || !fn) {
        return;
    }

    roster = nj_rget(vm, mesh, fb);
    check(roster == 0 && nj_iget(vm, mesh, fn) == 0,
          "a fresh SkinnedMesh holds no bones and allocates no roster");

    node = make(vm, CLS("Group"));
    check(node != 0, "the first bone node constructs");
    a[0].r = node;
    a[1].i = 255;   /* weight */
    a[2].i = 0;     /* firstVertex */
    a[3].i = 1;     /* vertexCount */
    st = nj_call(vm, mesh, skin, "addTransform",
                 "(L" CLS("Node") ";III)V", a, NULL);
    check(st == NJ_OK, "addTransform records the first bone");
    roster = nj_rget(vm, mesh, fb);
    check(nj_iget(vm, mesh, fn) == 1 &&
          nj_array_length(roster) == 64u &&
          nj_array_get_ref(roster, 0u) == node,
          "the first bone fits the old 64-slot allocation");

    for (i = 1; i < 64; i++) {
        (void)nj_call(vm, mesh, skin, "addTransform",
                      "(L" CLS("Node") ";III)V", a, NULL);
    }
    roster = nj_rget(vm, mesh, fb);
    check(nj_iget(vm, mesh, fn) == 64 && nj_array_length(roster) == 64u,
          "64 bones still fit the initial allocation");
    check(nj_array_get_ref(roster, 0u) == node &&
          nj_array_get_ref(roster, 63u) == node,
          "all 64 slots name the recorded node");

    /* The regression proper: entry 65 used to throw and the game never got
       its rig recorded, so it sat on LOADING. */
    st = nj_call(vm, mesh, skin, "addTransform",
                 "(L" CLS("Node") ";III)V", a, NULL);
    roster = nj_rget(vm, mesh, fb);
    check(st == NJ_OK, "the 65th addTransform is accepted");
    check(nj_iget(vm, mesh, fn) == 65 &&
          nj_array_length(roster) == 128u &&
          nj_array_get_ref(roster, 64u) == node,
          "the roster doubled once and kept the 65th bone");
    check(nj_array_get_ref(roster, 0u) == node &&
          nj_array_get_ref(roster, 63u) == node,
          "growing the roster keeps the old entries");

    for (i = 65; i < 200; i++) {
        (void)nj_call(vm, mesh, skin, "addTransform",
                      "(L" CLS("Node") ";III)V", a, NULL);
    }
    roster = nj_rget(vm, mesh, fb);
    check(nj_iget(vm, mesh, fn) == 200 && nj_array_length(roster) == 256u,
          "a 200-bone rig doubles twice and records every link");
    check(nj_array_get_ref(roster, 199u) == node,
          "the last recorded bone is still addressable");

    /* Null receiver still fails the way the reference API says. */
    a[0].r = 0;
    st = nj_call(vm, mesh, skin, "addTransform",
                 "(L" CLS("Node") ";III)V", a, NULL);
    check(st != NJ_OK && nj_exception_describe(vm) != NULL &&
          strstr(nj_exception_describe(vm), "NullPointerException") != NULL,
          "addTransform(null) still throws NullPointerException");
    check(nj_iget(vm, mesh, fn) == 200,
          "a rejected addTransform leaves the count alone");
    nj_clear_exception(vm);

    /* A corrupt count is refused without touching the roster. */
    nj_field_write_int(vm, mesh, fn, -1);
    a[0].r = node;
    st = nj_call(vm, mesh, skin, "addTransform",
                 "(L" CLS("Node") ";III)V", a, NULL);
    check(st != NJ_OK &&
          strstr(nj_exception_describe(vm), "IndexOutOfBoundsException") != NULL,
          "a negative bone count is refused instead of read out of bounds");
    nj_clear_exception(vm);
    nj_field_write_int(vm, mesh, fn, 200);
}


/*
 * KeyframeSequence.getSample() is the only way a MIDlet can sample an
 * animation curve by hand, and it used to be missing altogether.  The
 * numbers below are the ones the specification's interpolation rules
 * produce for a one-component, three-keyframe sequence at 0/100/200.
 */
static void kfs_sample(nj_vm *vm, nj_ref kfs, float t, float *out, int *ret,
                       int *ok)
{
    nj_value a[2], r;
    nj_ref dst = nj_array_new(vm, "F", 4);
    nj_status st;

    *ok = 0;
    if (!dst) {
        return;
    }
    a[0].f = t;
    a[1].r = dst;
    st = nj_call(vm, kfs, CLS("KeyframeSequence"), "getSample", "(F[F)I", a, &r);
    if (st != NJ_OK) {
        return;
    }
    if (out) {
        out[0] = ((float *)nj_array_data(dst))[0];
    }
    *ret = r.i;
    *ok = 1;
}

static void kfs_set_key(nj_vm *vm, nj_ref kfs, int idx, int time, float value)
{
    nj_value a[3];
    nj_ref f = nj_array_new(vm, "F", 1);

    if (!f) {
        return;
    }
    ((float *)nj_array_data(f))[0] = value;
    a[0].i = idx;
    a[1].i = time;
    a[2].r = f;
    call(vm, kfs, CLS("KeyframeSequence"), "setKeyframe", "(II[F)V", a, NULL);
}

static nj_ref kfs_new(nj_vm *vm, int ncomp, int nkeys, int interp)
{
    nj_ref kfs = nj_object_new(vm, CLS("KeyframeSequence"));
    nj_value a[3];

    if (!kfs) {
        return 0;
    }
    a[0].i = ncomp;
    a[1].i = nkeys;
    a[2].i = interp;
    call(vm, kfs, CLS("KeyframeSequence"), "<init>", "(III)V", a, NULL);
    return kfs;
}

static void test_keyframe_sampling(nj_vm *vm)
{
    nj_ref kfs;
    float v = -1.0f;
    int r = -1, ok = 0;
    int i;

    /* LINEAR (176): one component, keys 0/10/20 at 0/100/200 ms. */
    kfs = kfs_new(vm, 1, 3, 176);
    if (!kfs) {
        check(0, "create a LINEAR KeyframeSequence");
        return;
    }
    for (i = 0; i < 3; i++) {
        kfs_set_key(vm, kfs, i, i * 100, 10.0f * (float)i);
    }
    kfs_sample(vm, kfs, 0.0f, &v, &r, &ok);
    check(ok && v == 0.0f && r == 1, "a LINEAR sample at a keyframe copies it");
    kfs_sample(vm, kfs, 50.0f, &v, &r, &ok);
    check(ok && v == 5.0f && r == 1, "LINEAR halfway between 0 and 10 is 5");
    kfs_sample(vm, kfs, 150.0f, &v, &r, &ok);
    check(ok && v == 15.0f, "LINEAR halfway between 10 and 20 is 15");
    kfs_sample(vm, kfs, 200.0f, &v, &r, &ok);
    check(ok && v == 20.0f && r == 0x7fffffff,
          "past the last keyframe the sample holds it and the delay is infinite");
    kfs_sample(vm, kfs, -25.0f, &v, &r, &ok);
    check(ok && v == 0.0f && r == 25,
          "before the first keyframe the sample holds it and reports the delay");

    /* LOOP (193) wraps the time into [0, duration); the same sequence with
       duration 200 must give the 150 ms value at 350 ms. */
    {
        nj_value a[1];
        a[0].i = 200;
        call(vm, kfs, CLS("KeyframeSequence"), "setDuration", "(I)V", a, NULL);
        a[0].i = 193;
        call(vm, kfs, CLS("KeyframeSequence"), "setRepeatMode", "(I)V", a, NULL);
    }
    kfs_sample(vm, kfs, 350.0f, &v, &r, &ok);
    check(ok && v == 15.0f, "LOOP wraps 350 ms of a 200 ms curve to 15");

    /* STEP (180) holds the start keyframe until the next one arrives. */
    kfs = kfs_new(vm, 1, 3, 180);
    if (!kfs) {
        check(0, "create a STEP KeyframeSequence");
        return;
    }
    for (i = 0; i < 3; i++) {
        kfs_set_key(vm, kfs, i, i * 100, 10.0f * (float)i);
    }
    kfs_sample(vm, kfs, 50.0f, &v, &r, &ok);
    check(ok && v == 0.0f && r == 50,
          "STEP holds the previous keyframe and reports the time to the next");

    /* A sample array shorter than the component count is refused, as the
       specification says. */
    {
        nj_value a[2], out;
        nj_ref tiny = nj_array_new(vm, "F", 1);
        nj_status st;
        nj_ref q = kfs_new(vm, 2, 2, 176);
        a[0].f = 10.0f;
        a[1].r = tiny;
        nj_clear_exception(vm);
        st = nj_call(vm, q, CLS("KeyframeSequence"), "getSample", "(F[F)I", a, &out);
        check(st != NJ_OK && nj_exception_describe(vm) != NULL &&
              strstr(nj_exception_describe(vm), "IllegalArgumentException") != NULL,
              "a sample array shorter than the component count is refused");
        nj_clear_exception(vm);
    }

    /* SLERP (177) and SQUAD (179) are quaternion curves: the constructor
       refuses component counts other than four. */
    {
        nj_ref bad = nj_object_new(vm, CLS("KeyframeSequence"));
        nj_value a[3];
        nj_status st;
        a[0].i = 3;
        a[1].i = 2;
        a[2].i = 177;
        nj_clear_exception(vm);
        st = nj_call(vm, bad, CLS("KeyframeSequence"), "<init>", "(III)V", a, NULL);
        check(st != NJ_OK && nj_exception_describe(vm) != NULL &&
              strstr(nj_exception_describe(vm), "IllegalArgumentException") != NULL,
              "SLERP with three components is refused");
        nj_clear_exception(vm);
    }
}

/*
 * Five methods the specification names that used to be missing outright.
 * Each one is checked against the rule the JSR states, not just "returns
 * something": an empty active interval means always active, the interval is
 * half-open, an empty matrix slot is refused, the root node is the top of the
 * parent chain, and an updateProperty shorter than the target list zeroes the
 * targets it does not mention.
 */
static nj_s4 call_int(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                      const char *desc, nj_s4 v)
{
    nj_value a[2], r;
    nj_s4 out = -123456;

    memset(a, 0, sizeof a);
    a[0].i = v;
    r.i = 0;
    if (nj_call(vm, o, cls, name, desc, a, &r) == NJ_OK) {
        out = r.i;
    }
    return out;
}

static void test_spec_methods(nj_vm *vm)
{
    nj_ref ctl = make(vm, CLS("AnimationController"));
    nj_ref node, child, root;
    int ok = ctl != 0;

    check(ok, "AnimationController.<init> works");
    if (!ok) {
        return;
    }

    /* A controller that was never scheduled has an empty interval and is
       active at every world time, so it never deactivates. */
    check(call_int(vm, ctl, CLS("AnimationController"), "isActive", "(I)Z", 0) == 1,
          "isActive is true with an empty active interval");
    check(call_int(vm, ctl, CLS("AnimationController"), "isActive", "(I)Z", 99999) == 1,
          "and stays true at any later world time");
    check(call_int(vm, ctl, CLS("AnimationController"), "timeToActivation", "(I)I", 500) == 0,
          "timeToActivation is zero for a controller that is always active");
    check(call_int(vm, ctl, CLS("AnimationController"), "timeToDeactivation", "(I)I", 500) == 0x7FFFFFFF,
          "timeToDeactivation is MAX_VALUE for a controller that never deactivates");

    {
        nj_value a[2];

        a[0].i = 100; a[1].i = 200;
        call(vm, ctl, CLS("AnimationController"), "setActiveInterval", "(II)V", a, NULL);
    }
    check(call_int(vm, ctl, CLS("AnimationController"), "isActive", "(I)Z", 99) == 0,
          "the interval excludes times before it starts");
    check(call_int(vm, ctl, CLS("AnimationController"), "isActive", "(I)Z", 100) == 1,
          "the interval includes its start");
    check(call_int(vm, ctl, CLS("AnimationController"), "isActive", "(I)Z", 199) == 1,
          "the interval includes the time before its end");
    check(call_int(vm, ctl, CLS("AnimationController"), "isActive", "(I)Z", 200) == 0,
          "the interval excludes its end");
    check(call_int(vm, ctl, CLS("AnimationController"), "timeToActivation", "(I)I", 0) == 100,
          "timeToActivation counts down to the start");
    check(call_int(vm, ctl, CLS("AnimationController"), "timeToActivation", "(I)I", 150) == 0,
          "timeToActivation is zero inside the interval");
    check(call_int(vm, ctl, CLS("AnimationController"), "timeToActivation", "(I)I", 200) == 0x7FFFFFFF,
          "timeToActivation is MAX_VALUE once the interval has passed");
    check(call_int(vm, ctl, CLS("AnimationController"), "timeToDeactivation", "(I)I", 0) == 200,
          "timeToDeactivation counts down from the start of the interval");
    check(call_int(vm, ctl, CLS("AnimationController"), "timeToDeactivation", "(I)I", 150) == 50,
          "and from inside it");
    check(call_int(vm, ctl, CLS("AnimationController"), "timeToDeactivation", "(I)I", 200) == 0,
          "and is zero once deactivated");
    {
        /* The JSR swaps the two bounds when they arrive the wrong way round,
           so the same interval works either way. */
        nj_value a[2];

        a[0].i = 200; a[1].i = 100;
        call(vm, ctl, CLS("AnimationController"), "setActiveInterval", "(II)V", a, NULL);
    }
    check(call_int(vm, ctl, CLS("AnimationController"), "isActive", "(I)Z", 150) == 1,
          "a reversed interval is swapped, not refused");

    /* getMatrix(float[]) is the local transform, row major. */
    node = make(vm, CLS("Transformable"));
    if (node) {
        nj_ref out = nj_array_new(vm, "F", 16);
        nj_value a[3];

        a[0].f = 1.0f; a[1].f = 2.0f; a[2].f = 3.0f;
        call(vm, node, CLS("Transformable"), "setTranslation", "(FFF)V", a, NULL);
        memset(a, 0, sizeof a);
        a[0].r = out;
        call(vm, node, CLS("Transformable"), "getMatrix", "([F)V", a, NULL);
        check(!nj_has_exception(vm), "getMatrix accepts a sixteen float array");
        nj_clear_exception(vm);
        check(((float *)nj_array_data(out))[3] == 1.0f &&
              ((float *)nj_array_data(out))[7] == 2.0f &&
              ((float *)nj_array_data(out))[11] == 3.0f,
              "getMatrix reports the translation column the transform holds");
        {
            nj_ref small = nj_array_new(vm, "F", 4);

            memset(a, 0, sizeof a);
            a[0].r = small;
            call(vm, node, CLS("Transformable"), "getMatrix", "([F)V", a, NULL);
            check(nj_has_exception(vm), "getMatrix refuses an array shorter than sixteen");
            nj_clear_exception(vm);
        }
    }

    /* getRootNode walks the parent chain addChild keeps. */
    root = make(vm, CLS("Group"));
    child = make(vm, CLS("Group"));
    if (root && child) {
        nj_value a[1], r;

        a[0].r = child;
        call(vm, root, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", a, NULL);
        a[0].r = 0;
        r.r = 0;
        call(vm, child, CLS("Node"), "getRootNode", "()L" CLS("Node") ";", a, &r);
        check(r.r == root, "getRootNode returns the top of the parent chain");
        a[0].r = 0;
        r.r = 0;
        call(vm, root, CLS("Node"), "getRootNode", "()L" CLS("Node") ";", a, &r);
        check(r.r == root, "and a detached node is its own root");
    }

    /* updateProperty(MORPH_WEIGHTS, values) is how a track sets the weights. */
    {
        nj_ref vb = make(vm, CLS("VertexBuffer"));
        nj_ref ib = make(vm, CLS("IndexBuffer"));
        nj_ref app = make(vm, CLS("Appearance"));
        nj_ref targets = nj_object_array_new(vm, CLS("VertexBuffer"), 2);
        nj_ref morph;

        if (vb && app && targets) {
            nj_value a[4], r;

            nj_array_set_ref(targets, 0, vb);
            nj_array_set_ref(targets, 1, vb);

            memset(a, 0, sizeof a);
            a[0].r = vb; a[1].r = targets; a[2].r = ib; a[3].r = app;
            morph = nj_object_new(vm, CLS("MorphingMesh"));
            r.i = 0;
            if (morph && nj_call_special(vm, morph, CLS("MorphingMesh"), "<init>",
                                         "(L" CLS("VertexBuffer") ";[L"
                                         CLS("VertexBuffer") ";L" CLS("IndexBuffer")
                                         ";L" CLS("Appearance") ";)V", a, NULL) == NJ_OK) {
                nj_ref value = nj_array_new(vm, "F", 3);
                float *w;

                check(!nj_has_exception(vm), "a morphing mesh with two targets is built");
                nj_clear_exception(vm);
                ((float *)nj_array_data(value))[0] = 0.25f;
                ((float *)nj_array_data(value))[1] = 0.75f;
                ((float *)nj_array_data(value))[2] = 9.0f;  /* beyond the targets */
                memset(a, 0, sizeof a);
                a[0].i = 266;                              /* MORPH_WEIGHTS */
                a[1].r = value;
                call(vm, morph, CLS("MorphingMesh"), "updateProperty", "(I[F)V", a, NULL);
                w = (float *)nj_array_data(nj_rget(vm, morph,
                                                   nj_field_lookup(vm, CLS("MorphingMesh"),
                                                                   "_nj_weights", "[F", 0)));
                check(w && w[0] == 0.25f && w[1] == 0.75f,
                      "updateProperty copies the weights it is given");
                {
                    nj_ref shortv = nj_array_new(vm, "F", 1);

                    ((float *)nj_array_data(shortv))[0] = 0.5f;
                    memset(a, 0, sizeof a);
                    a[0].i = 266;
                    a[1].r = shortv;
                    call(vm, morph, CLS("MorphingMesh"), "updateProperty", "(I[F)V", a, NULL);
                    w = (float *)nj_array_data(nj_rget(vm, morph,
                                                       nj_field_lookup(vm, CLS("MorphingMesh"),
                                                                       "_nj_weights", "[F", 0)));
                    check(w && w[0] == 0.5f && w[1] == 0.0f,
                          "a short weight array zeroes the targets it omits");
                }
            }
        }
    }
}

/*
 * A World with more Mesh nodes than one render used to draw.
 *
 * m3g_render_scene() walked the scene into a fixed 64-entry stack array and
 * m3g_world_draw() stopped at 64 as well, so a World with more Mesh nodes
 * silently lost every mesh past the 64th.  The World's Background clears the
 * frame black, so the missing meshes showed up as black holes in a scene that
 * otherwise rendered - Doom 3's level World carries 173 Mesh nodes.
 *
 * The fixture is one narrow white quad per Mesh node, each node translated to
 * its own column of the viewport, so every column has exactly one owner in
 * the scene graph.  A count of lit columns equal to the node count says the
 * whole graph was drawn; 64 lit columns is the old truncation.  The nodes
 * share one vertex buffer, one index buffer and one appearance; only the node
 * transform differs, which is exactly how a level splits its rooms into
 * separate Mesh nodes.
 */
static int world_many_meshes_scene(nj_vm *vm, int meshes, nj_ref *out_world,
                                   int *centres)
{
    nj_value a[6];
    nj_ref world = make(vm, CLS("World"));
    nj_ref vb = make(vm, CLS("VertexBuffer"));
    nj_ref ib = nj_object_new(vm, CLS("TriangleStripArray"));
    nj_ref app = make(vm, CLS("Appearance"));
    nj_ref poly = make(vm, CLS("PolygonMode"));
    nj_ref pos, posva, lens;
    int ok = 1;
    int i;

    if (!world || !vb || !ib || !app || !poly) {
        return 0;
    }
    memset(out_world, 0, sizeof(*out_world));
    pos = nj_array_new(vm, "S", 12u);
    lens = nj_array_new(vm, "I", 1u);
    posva = nj_object_new(vm, CLS("VertexArray"));
    if (!pos || !lens || !posva) {
        return 0;
    }
    {
        nj_s2 *pp = (nj_s2 *)nj_array_data(pos);

        /* One quad in camera space: 100 units wide and 100 units tall at
           z = -200, which a 60 degree perspective fills the viewport with. */
        pp[0] = -50;  pp[1] = -50;  pp[2] = -200;
        pp[3] =  50;  pp[4] = -50;  pp[5] = -200;
        pp[6] = -50;  pp[7] =  50;  pp[8] = -200;
        pp[9] =  50;  pp[10] = 50;  pp[11] = -200;
    }
    ((nj_s4 *)nj_array_data(lens))[0] = 4;

    /* VertexArray(numVertices, numComponents, componentSize). */
    a[0].i = 4; a[1].i = 3; a[2].i = 2;
    ok = ok && nj_call_special(vm, posva, CLS("VertexArray"), "<init>",
                               "(III)V", a, NULL) == NJ_OK;
    a[0].i = 0; a[1].i = 4; a[2].r = pos;
    ok = ok && nj_call(vm, posva, CLS("VertexArray"), "set", "(II[S)V",
                       a, NULL) == NJ_OK;
    a[0].r = posva; a[1].f = 1.0f; a[2].r = 0;
    ok = ok && nj_call(vm, vb, CLS("VertexBuffer"), "setPositions",
                       "(L" CLS("VertexArray") ";F[F)V", a, NULL) == NJ_OK;
    a[0].i = 0xFFFFFFFFu;   /* white: no Material and no normals, so unlit */
    ok = ok && nj_call(vm, vb, CLS("VertexBuffer"), "setDefaultColor",
                       "(I)V", a, NULL) == NJ_OK;
    a[0].i = 0; a[1].r = lens;
    ok = ok && nj_call_special(vm, ib, CLS("TriangleStripArray"), "<init>",
                               "(I[I)V", a, NULL) == NJ_OK;
    a[0].i = 162;   /* CULL_NONE: the quad's winding does not matter here */
    ok = ok && nj_call(vm, poly, CLS("PolygonMode"), "setCulling",
                       "(I)V", a, NULL) == NJ_OK;
    a[0].r = poly;
    ok = ok && nj_call(vm, app, CLS("Appearance"), "setPolygonMode",
                       "(L" CLS("PolygonMode") ";)V", a, NULL) == NJ_OK;

    for (i = 0; i < meshes; i++) {
        nj_ref mesh = nj_object_new(vm, CLS("Mesh"));
        int centre = 2 + (i * 233) / (meshes - 1);
        nj_value ma[3];

        ok = ok && mesh != 0;
        if (!mesh) {
            break;
        }
        ma[0].r = vb; ma[1].r = ib; ma[2].r = app;
        ok = ok && nj_call_special(vm, mesh, CLS("Mesh"), "<init>",
                                   "(L" CLS("VertexBuffer") ";L"
                                   CLS("IndexBuffer") ";L" CLS("Appearance")
                                   ";)V", ma, NULL) == NJ_OK;
        /* Three pixels wide: the 60 degree camera at z = -200 puts one
           camera-space unit on 1.3857 pixels (vertical 160/115.47 and, with
           aspect 0.75, horizontal the same), so 1.0825 units either side of
           the column centre is 1.5 pixels.  Narrow enough that a column is
           lit only by its own node even when the walk stops at 64. */
        ma[0].f = 0.021651f; ma[1].f = 1.0f; ma[2].f = 1.0f;
        ok = ok && nj_call(vm, mesh, CLS("Node"), "setScale", "(FFF)V",
                           ma, NULL) == NJ_OK;
        ma[0].f = ((float)centre - 120.0f) / 1.385651f;
        ma[1].f = 0.0f; ma[2].f = 0.0f;
        ok = ok && nj_call(vm, mesh, CLS("Node"), "setTranslation", "(FFF)V",
                           ma, NULL) == NJ_OK;
        ma[0].r = mesh;
        ok = ok && nj_call(vm, world, CLS("Group"), "addChild",
                           "(L" CLS("Node") ";)V", ma, NULL) == NJ_OK;
        centres[i] = centre;
    }
    if (!ok) {
        return 0;
    }
    *out_world = world;
    return 1;
}

static void test_world_mesh_count(nj_vm *vm)
{
    enum { NODES = 70 };
    int centres[NODES];
    nj_ref world = 0;
    nj_ref target, targetg, pixels, cam, cam_xf, bg;
    nj_ref g3d = make(vm, CLS("Graphics3D"));
    nj_value a[5], r;
    const nj_u4 *px;
    int i, lit = 0, ok;

    ok = g3d != 0 && world_many_meshes_scene(vm, NODES, &world, centres);
    check(ok, "a World with 70 Mesh nodes is built");
    if (!ok) {
        return;
    }
    memset(&r, 0, sizeof(r));
    a[0].i = 240; a[1].i = 320;
    ok = nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                        "(II)Ljavax/microedition/lcdui/Image;", a, &r) == NJ_OK
         && r.r != 0;
    check(ok, "the 70-mesh target image constructs");
    if (!ok) {
        return;
    }
    target = r.r;
    r.r = 0;
    call(vm, target, "javax/microedition/lcdui/Image", "getGraphics",
         "()Ljavax/microedition/lcdui/Graphics;", NULL, &r);
    targetg = r.r;
    pixels = nj_rget(vm, target,
                     nj_field_lookup(vm, "javax/microedition/lcdui/Image",
                                     "imgPixels", "[I", 0));
    check(targetg != 0 && pixels != 0, "the 70-mesh target has pixels");
    if (!targetg || !pixels) {
        return;
    }
    bg = make(vm, CLS("Background"));
    cam = make(vm, CLS("Camera"));
    cam_xf = make(vm, CLS("Transform"));
    check(bg && cam && cam_xf, "a camera and a black Background");
    if (!bg || !cam || !cam_xf) {
        return;
    }
    a[0].i = (nj_s4)0xFF000000u;   /* opaque black */
    call(vm, bg, CLS("Background"), "setColor", "(I)V", a, NULL);
    a[0].f = 60.0f; a[1].f = 0.75f; a[2].f = 1.0f; a[3].f = 1000.0f;
    call(vm, cam, CLS("Camera"), "setPerspective", "(FFFF)V", a, NULL);
    call(vm, cam_xf, CLS("Transform"), "setIdentity", "()V", NULL, NULL);

    a[0].r = targetg;
    call(vm, g3d, CLS("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V",
         a, NULL);
    a[0].i = 0; a[1].i = 0; a[2].i = 240; a[3].i = 320;
    call(vm, g3d, CLS("Graphics3D"), "setViewport", "(IIII)V", a, NULL);
    a[0].r = cam; a[1].r = cam_xf;
    call(vm, g3d, CLS("Graphics3D"), "setCamera",
         "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);
    a[0].r = bg;
    call(vm, g3d, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V",
         a, NULL);
    {
        int q, nb = 0;
        const nj_u4 *qq = (const nj_u4 *)nj_array_data(pixels);

        for (q = 0; q < 240 * 320; q++) {
            if ((qq[q] & 0xFFFFFFu) == 0u) {
                nb++;
            }
        }
        /*
         * Image.createImage() starts white, so "not black at the column
         * centre" would pass vacuously on a render that draws nothing at all.
         * The clear has to reach every pixel first.
         */
        check(nb == 240 * 320,
              "clear(Background) fills the whole 240x320 target black");
    }
    a[0].r = world;
    call(vm, g3d, CLS("Graphics3D"), "render", "(L" CLS("World") ";)V",
         a, NULL);
    call(vm, g3d, CLS("Graphics3D"), "releaseTarget", "()V", NULL, NULL);

    px = (const nj_u4 *)nj_array_data(pixels);
    for (i = 0; i < NODES; i++) {
        if ((px[160 * 240 + centres[i]] & 0xFFFFFFu) != 0u) {
            lit++;
        }
    }
    check(lit == NODES,
          "render(World) draws every Mesh node, not the first 64");
    if (lit != NODES) {
        printf("  (lit %d of %d columns: the truncating render draws 64)\n",
               lit, NODES);
    }
}

/*
 * ------------------------------------------------------------------
 * Animated nodes
 * ------------------------------------------------------------------
 * animate(time) used to be a no-op: the loader did not decode a file's
 * KeyframeSequence/AnimationTrack/AnimationController objects either, so a
 * title that shipped an animated rig got its static bind pose forever.  NFS:
 * Hot Pursuit's car models are the visible case - animrig_car.m3g holds eleven
 * (sequence, controller, track) triples, and without them the wheels stay
 * where the bind pose put them, under the road.
 *
 * These assertions exercise both halves: the loader decodes a file and links
 * each object's tracks (built below as the same bytes NFS's rig has), and
 * animate() samples them through the controller and writes the blended value
 * into the node's component transform.
 */
static void put_f32(nj_u1 *p, float f)
{
    nj_u4 bits;

    memcpy(&bits, &f, 4u);
    put_le32(p, bits);
}

static void kfs_set_key3(nj_vm *vm, nj_ref kfs, int idx, int time,
                         float x, float y, float z)
{
    nj_value a[3];
    nj_ref f = nj_array_new(vm, "F", 3);
    float *p;

    if (!f) {
        return;
    }
    p = (float *)nj_array_data(f);
    p[0] = x; p[1] = y; p[2] = z;
    a[0].i = idx; a[1].i = time; a[2].r = f;
    call(vm, kfs, CLS("KeyframeSequence"), "setKeyframe", "(II[F)V", a, NULL);
}

static void kfs_set_key4(nj_vm *vm, nj_ref kfs, int idx, int time,
                         float x, float y, float z, float w)
{
    nj_value a[3];
    nj_ref f = nj_array_new(vm, "F", 4);
    float *p;

    if (!f) {
        return;
    }
    p = (float *)nj_array_data(f);
    p[0] = x; p[1] = y; p[2] = z; p[3] = w;
    a[0].i = idx; a[1].i = time; a[2].r = f;
    call(vm, kfs, CLS("KeyframeSequence"), "setKeyframe", "(II[F)V", a, NULL);
}

static nj_ref atrack_new(nj_vm *vm, nj_ref kfs, int prop)
{
    nj_ref t = nj_object_new(vm, CLS("AnimationTrack"));
    nj_value a[2];

    if (!t) {
        return 0;
    }
    a[0].r = kfs;
    a[1].i = prop;
    if (nj_call_special(vm, t, CLS("AnimationTrack"), "<init>",
                        "(L" CLS("KeyframeSequence") ";I)V", a, NULL) != NJ_OK) {
        return 0;
    }
    return t;
}

/* Attach `track` to `node`: the whole JSR-184 animation API a MIDlet uses. */
static void node_add_track(nj_vm *vm, nj_ref node, nj_ref track, nj_ref ctl)
{
    nj_value a[2];

    if (ctl) {
        a[0].r = ctl;
        call(vm, track, CLS("AnimationTrack"), "setController",
             "(L" CLS("AnimationController") ";)V", a, NULL);
    }
    a[0].r = track;
    call(vm, node, CLS("Object3D"), "addAnimationTrack",
         "(L" CLS("AnimationTrack") ";)V", a, NULL);
}

static float node_matrix(nj_vm *vm, nj_ref node, int elem)
{
    nj_ref xf = make(vm, CLS("Transform"));
    nj_value a[1];
    float v = -12345.0f;

    if (!xf) {
        return v;
    }
    a[0].r = xf;
    call(vm, node, CLS("Node"), "getTransform",
         "(L" CLS("Transform") ";)V", a, NULL);
    v = xf_elem(vm, xf, elem);
    return v;
}

/*
 * A JSR-184 file carrying exactly the animation objects animrig_car.m3g has,
 * one of each: a two-keyframe translation curve, its controller, the track
 * that binds them, and a Group whose Object3D header names that track.  Ref
 * values are "object number + 2", the numbering the loader's ref-mode
 * detection resolves, and the lengths are the ones the scanner requires to
 * consume each payload exactly.
 */
static nj_u4 build_anim_file(nj_u1 *b, nj_u4 cap)
{
    static const nj_u1 ident[12] = {
        0xAB, 0x4A, 0x53, 0x52, 0x31, 0x38, 0x34,
        0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    nj_u4 hdr_sec = 30u, body, sec, total, len[4], k;
    nj_u1 *p;

    /* payload lengths: curve 35+2*(4+12), controller 12+24, track 12+12,
       group 16+14 */
    len[0] = 67u; len[1] = 36u; len[2] = 24u; len[3] = 30u;
    body = 0u;
    for (k = 0u; k < 4u; k++) {
        body += 5u + len[k];
    }
    sec = 9u + body + 4u;
    total = 12u + hdr_sec + sec;
    if (cap < total) {
        return 0u;
    }
    memset(b, 0, cap);
    memcpy(b, ident, 12u);
    /* the file header section */
    b[12] = 0u;                        /* stored, not compressed */
    put_le32(b + 13, hdr_sec);
    put_le32(b + 17, 17u);             /* its body's length */
    put_le32(b + 22, 12u);             /* Object3D header length */
    b[26] = 1u;                        /* file version 1.0 */
    put_le32(b + 29, total);           /* totalFileSize */
    put_le32(b + 33, total);           /* approximate content size */
    /* the object section */
    b[42] = 0u;
    put_le32(b + 43, sec);
    put_le32(b + 47, body);
    p = b + 51;

    /* object 0: the curve, 0 -> (0,0,0) at 0 ms, (10,0,0) at 1000 ms */
    p[0] = 19u;
    put_le32(p + 1, len[0]);
    p += 5;
    p[12] = 176u;                      /* LINEAR */
    p[13] = 192u;                      /* CONSTANT */
    p[14] = 0u;                        /* uncompressed keys */
    put_le32(p + 15, 1000u);           /* duration */
    put_le32(p + 19, 0u);              /* valid range first */
    put_le32(p + 23, 1u);              /* valid range last */
    put_le32(p + 27, 3u);              /* components */
    put_le32(p + 31, 2u);              /* keyframes */
    put_le32(p + 35, 0u);              /* key 0: t=0 */
    put_f32(p + 39, 0.0f); put_f32(p + 43, 0.0f); put_f32(p + 47, 0.0f);
    put_le32(p + 51, 1000u);           /* key 1: t=1000 */
    put_f32(p + 55, 10.0f); put_f32(p + 59, 0.0f); put_f32(p + 63, 0.0f);
    p += len[0];

    /* object 1: the controller, at the specification's defaults */
    p[0] = 1u;
    put_le32(p + 1, len[1]);
    p += 5;
    put_f32(p + 12, 1.0f);             /* speed */
    put_f32(p + 16, 1.0f);             /* weight */
    put_le32(p + 20, 0u);              /* active interval start */
    put_le32(p + 24, 0u);              /* active interval end */
    put_f32(p + 28, 0.0f);             /* reference sequence time */
    put_le32(p + 32, 0u);              /* reference world time */
    p += len[1];

    /* object 2: the track, property TRANSLATION */
    p[0] = 2u;
    put_le32(p + 1, len[2]);
    p += 5;
    put_le32(p + 12, 2u);              /* sequence ref = object 0 + 2 */
    put_le32(p + 16, 3u);              /* controller ref = object 1 + 2 */
    put_le32(p + 20, 275u);            /* TRANSLATION */
    p += len[2];

    /* object 3: the animated Group */
    p[0] = 9u;
    put_le32(p + 1, len[3]);
    p += 5;
    put_le32(p + 4, 1u);               /* one animation track */
    put_le32(p + 8, 4u);               /* track ref = object 2 + 2 */
    put_le32(p + 12, 0u);              /* no user parameters */
    p[16] = 0u;                        /* no component transform */
    p[17] = 0u;                        /* no general transform */
    p[18] = 1u;                        /* rendering enabled */
    p[19] = 1u;                        /* picking enabled */
    p[20] = 0xFFu;                     /* alpha */
    put_le32(p + 21, 0u);              /* picking scope */
    p[25] = 0u;                        /* no alignment */
    put_le32(p + 26, 0u);              /* no children */
    return total;
}

static void test_animation(nj_vm *vm)
{
    printf("== animate() writes a track's keyframes into the node ==\n");
    {
        nj_ref node = make(vm, CLS("Group"));
        nj_ref kfs = kfs_new(vm, 3, 2, 176);
        nj_ref track = 0, ctl = 0;
        nj_value a[2], r;
        float m;

        check(node && kfs, "a Group node and a translation curve");
        if (node && kfs) {
            kfs_set_key3(vm, kfs, 0, 0, 0.0f, 0.0f, 0.0f);
            kfs_set_key3(vm, kfs, 1, 1000, 10.0f, 0.0f, 0.0f);
            track = atrack_new(vm, kfs, 275);
            ctl = make(vm, CLS("AnimationController"));
            check(track && ctl, "an AnimationTrack for TRANSLATION and a controller");
            if (track && ctl) {
                node_add_track(vm, node, track, ctl);
                check(!nj_has_exception(vm),
                      "addAnimationTrack returns without an exception");
                nj_clear_exception(vm);

                a[0].i = 250;
                r.i = -1;
                call(vm, node, CLS("Object3D"), "animate", "(I)I", a, &r);
                /* getSample() reports 1 while it is interpolating between
                   keyframes (the reference implementation does the same), so
                   animate() can only promise the sampling layer's hint. */
                check(r.i == 1,
                      "animate(250) reports the sampling layer's next-event hint");
                m = node_matrix(vm, node, 3);
                check(fabsf(m - 2.5f) < 1e-4f,
                      "and the node's translation is the curve's halfway value");

                a[0].i = 1500;
                call(vm, node, CLS("Object3D"), "animate", "(I)I", a, &r);
                check(r.i == 0x7fffffff,
                      "past the last keyframe there is no next keyframe");
                m = node_matrix(vm, node, 3);
                check(fabsf(m - 10.0f) < 1e-4f,
                      "and the curve's last value holds");

                a[0].i = -1;
                call(vm, node, CLS("Object3D"), "animate", "(I)I", a, &r);
                check(r.i == 0, "a negative time is the JSR's invalid time (0)");
            }
        }
    }

    printf("== an AnimationController maps world time to sequence time ==\n");
    {
        nj_ref ctl = make(vm, CLS("AnimationController"));
        nj_value a[2], r;

        check(ctl != 0, "a fresh AnimationController");
        if (ctl) {
            a[0].f = 100.0f; a[1].i = 0;
            call(vm, ctl, CLS("AnimationController"), "setPosition", "(FI)V",
                 a, NULL);
            a[0].i = 250;
            r.f = -1.0f;
            call(vm, ctl, CLS("AnimationController"), "getPosition", "(I)F",
                 a, &r);
            check(fabsf(r.f - 350.0f) < 1e-4f,
                  "at speed 1 a position of 100 ms is 350 ms at world time 250");

            a[0].f = 2.0f; a[1].i = 250;
            call(vm, ctl, CLS("AnimationController"), "setSpeed", "(FI)V",
                 a, NULL);
            a[0].i = 250;
            r.f = -1.0f;
            call(vm, ctl, CLS("AnimationController"), "getPosition", "(I)F",
                 a, &r);
            check(fabsf(r.f - 350.0f) < 1e-4f,
                  "setSpeed does not move the current pose");
            a[0].i = 500;
            r.f = -1.0f;
            call(vm, ctl, CLS("AnimationController"), "getPosition", "(I)F",
                 a, &r);
            check(fabsf(r.f - 850.0f) < 1e-4f,
                  "and afterwards the pose advances at the new speed");
            call(vm, ctl, CLS("AnimationController"), "getRefWorldTime", "()I",
                 NULL, &r);
            check(r.i == 250, "getRefWorldTime reports the reference world time");
        }
    }

    printf("== animate() applies scale and orientation too ==\n");
    {
        nj_ref node = make(vm, CLS("Group"));
        nj_ref kfs = kfs_new(vm, 3, 1, 176);
        nj_ref track, ctl = make(vm, CLS("AnimationController"));
        nj_value a[1];
        float e0, e5, e10;

        if (node && kfs && ctl) {
            kfs_set_key3(vm, kfs, 0, 0, 2.0f, 3.0f, 4.0f);
            track = atrack_new(vm, kfs, 270);
            if (track) {
                node_add_track(vm, node, track, ctl);
                a[0].i = 0;
                call(vm, node, CLS("Object3D"), "animate", "(I)I", a, NULL);
                e0 = node_matrix(vm, node, 0);
                e5 = node_matrix(vm, node, 5);
                e10 = node_matrix(vm, node, 10);
                check(fabsf(e0 - 2.0f) < 1e-4f && fabsf(e5 - 3.0f) < 1e-4f &&
                      fabsf(e10 - 4.0f) < 1e-4f,
                      "a SCALE track writes the node's three axes");
            }
        }

        node = make(vm, CLS("Group"));
        kfs = kfs_new(vm, 4, 2, 176);
        if (node && kfs && ctl) {
            /* 60 degrees about +Y, as a unit quaternion at the last key. */
            kfs_set_key4(vm, kfs, 0, 0, 0.0f, 0.0f, 0.0f, 1.0f);
            kfs_set_key4(vm, kfs, 1, 1000, 0.0f, 0.5f, 0.0f, 0.8660254f);
            track = atrack_new(vm, kfs, 268);
            if (track) {
                node_add_track(vm, node, track, ctl);
                a[0].i = 1000;
                call(vm, node, CLS("Object3D"), "animate", "(I)I", a, NULL);
                e0 = node_matrix(vm, node, 0);
                e10 = node_matrix(vm, node, 2);
                check(fabsf(e0 - 0.5f) < 1e-4f && fabsf(e10 - 0.8660254f) < 1e-4f,
                      "an ORIENTATION track rotates the node's basis");
            }
        }
    }

    printf("== animate() applies alpha and visibility too ==\n");
    {
        nj_ref node = make(vm, CLS("Group"));
        nj_ref kfs = kfs_new(vm, 1, 2, 176);
        nj_ref track, ctl = make(vm, CLS("AnimationController"));
        nj_value a[1], r;

        check(node && kfs && ctl, "a Group for the alpha and visibility curves");
        if (node && kfs && ctl) {
            kfs_set_key(vm, kfs, 0, 0, 0.5f);
            kfs_set_key(vm, kfs, 1, 1000, 0.5f);
            track = atrack_new(vm, kfs, 256);
            if (track) {
                node_add_track(vm, node, track, ctl);
                a[0].i = 0;
                call(vm, node, CLS("Object3D"), "animate", "(I)I", a, NULL);
                r.f = -1.0f;
                call(vm, node, CLS("Node"), "getAlphaFactor", "()F", NULL, &r);
                check(fabsf(r.f - 0.5f) < 1e-4f,
                      "an ALPHA track writes the node's alpha factor");
            }

            kfs = kfs_new(vm, 1, 2, 176);
            check(kfs != 0, "a curve for the VISIBILITY property");
            if (kfs) {
                kfs_set_key(vm, kfs, 0, 0, 0.0f);
                kfs_set_key(vm, kfs, 1, 1000, 0.0f);
                track = atrack_new(vm, kfs, 276);
                if (track) {
                    node_add_track(vm, node, track, ctl);
                    a[0].i = 0;
                    call(vm, node, CLS("Object3D"), "animate", "(I)I", a,
                         NULL);
                    r.i = 1;
                    call(vm, node, CLS("Node"), "isRenderingEnabled", "()Z",
                         NULL, &r);
                    check(r.i == 0,
                          "a VISIBILITY track hides the node from the walk");
                }
            }
        }
    }
}

static void test_animation_colour(nj_vm *vm)
{
    printf("== animate() applies a COLOR track to a background ==\n");
    {
        nj_ref bg = make(vm, CLS("Background"));
        nj_ref kfs = kfs_new(vm, 3, 2, 176);
        nj_ref ctl = make(vm, CLS("AnimationController"));
        nj_ref track;
        nj_value a[1], r;
        nj_u4 c;

        check(bg && kfs && ctl, "a Background and a three-component colour curve");
        if (bg && kfs && ctl) {
            kfs_set_key3(vm, kfs, 0, 0, 0.0f, 0.0f, 0.0f);
            kfs_set_key3(vm, kfs, 1, 1000, 1.0f, 0.5f, 0.25f);
            track = atrack_new(vm, kfs, 258);          /* AnimationTrack.COLOR */
            check(track != 0, "an AnimationTrack for COLOR");
            if (track) {
                /* An alpha the colour animation must not clobber. */
                a[0].i = (nj_s4)0xFF204080u;
                call(vm, bg, CLS("Background"), "setColor", "(I)V", a, NULL);
                node_add_track(vm, bg, track, ctl);

                a[0].i = 0;
                call(vm, bg, CLS("Background"), "animate", "(I)I", a, &r);
                r.i = -1;
                call(vm, bg, CLS("Background"), "getColor", "()I", NULL, &r);
                check(((nj_u4)r.i & 0x00FFFFFFu) == 0u,
                      "at t=0 a COLOR track writes the first keyframe's black");
                check(((nj_u4)r.i & 0xFF000000u) == 0xFF000000u,
                      "and the colour's alpha byte is preserved");

                a[0].i = 500;
                call(vm, bg, CLS("Background"), "animate", "(I)I", a, NULL);
                r.i = -1;
                call(vm, bg, CLS("Background"), "getColor", "()I", NULL, &r);
                c = (nj_u4)r.i & 0x00FFFFFFu;
                check(((c >> 16) & 0xFFu) == 128u && ((c >> 8) & 0xFFu) == 64u &&
                      (c & 0xFFu) == 32u,
                      "halfway is the linear blend, scaled to 8 bits (128,64,32)");

                a[0].i = 1000;
                call(vm, bg, CLS("Background"), "animate", "(I)I", a, NULL);
                r.i = -1;
                call(vm, bg, CLS("Background"), "getColor", "()I", NULL, &r);
                check(((nj_u4)r.i & 0x00FFFFFFu) == 0xFF8040u,
                      "the last keyframe is (255,128,64)");
            }
        }
    }
    printf("== a COLOR track also reaches Light and Fog ==\n");
    {
        nj_ref light = make(vm, CLS("Light"));
        nj_ref kfs = kfs_new(vm, 3, 2, 176);
        nj_ref ctl = make(vm, CLS("AnimationController"));
        nj_ref track;
        nj_value a[1], r;

        if (light && kfs && ctl) {
            kfs_set_key3(vm, kfs, 0, 0, 0.0f, 1.0f, 0.0f);
            kfs_set_key3(vm, kfs, 1, 1000, 0.0f, 1.0f, 0.0f);
            track = atrack_new(vm, kfs, 258);
            if (track) {
                a[0].i = (nj_s4)0xFF102030u;
                call(vm, light, CLS("Light"), "setColor", "(I)V", a, NULL);
                node_add_track(vm, light, track, ctl);
                a[0].i = 1000;
                call(vm, light, CLS("Light"), "animate", "(I)I", a, NULL);
                r.i = -1;
                call(vm, light, CLS("Light"), "getColor", "()I", NULL, &r);
                check(((nj_u4)r.i & 0x00FFFFFFu) == 0x00FF00u,
                      "a Light's colour comes from its COLOR track");
            }
        }

        kfs = kfs_new(vm, 3, 2, 176);
        light = make(vm, CLS("Fog"));
        if (light && kfs && ctl) {
            kfs_set_key3(vm, kfs, 0, 0, 0.0f, 0.0f, 1.0f);
            kfs_set_key3(vm, kfs, 1, 1000, 0.0f, 0.0f, 1.0f);
            track = atrack_new(vm, kfs, 258);
            if (track) {
                a[0].i = (nj_s4)0xFF102030u;
                call(vm, light, CLS("Fog"), "setColor", "(I)V", a, NULL);
                node_add_track(vm, light, track, ctl);
                a[0].i = 1000;
                call(vm, light, CLS("Fog"), "animate", "(I)I", a, NULL);
                r.i = -1;
                call(vm, light, CLS("Fog"), "getColor", "()I", NULL, &r);
                check(((nj_u4)r.i & 0x00FFFFFFu) == 0x0000FFu,
                      "a Fog's colour comes from its COLOR track");
            }
        }
    }
    printf("== the animated background colour is what clear() paints ==\n");
    {
        i2d_canvas c;
        nj_ref kfs = kfs_new(vm, 3, 2, 176);
        nj_ref ctl = make(vm, CLS("AnimationController"));
        nj_ref track;
        nj_value a[1];
        int ok;

        ok = i2d_canvas_init(vm, &c);
        check(ok, "the 3D canvas for the colour clear");
        if (ok && kfs && ctl) {
            kfs_set_key3(vm, kfs, 0, 0, 0.0f, 0.0f, 0.0f);
            kfs_set_key3(vm, kfs, 1, 1000, 1.0f, 0.0f, 0.0f);
            track = atrack_new(vm, kfs, 258);
            if (track) {
                node_add_track(vm, c.bg, track, ctl);
                a[0].i = 1000;
                call(vm, c.bg, CLS("Background"), "animate", "(I)I", a, NULL);
            }
            a[0].r = c.bg;
            call(vm, c.g3d, CLS("Graphics3D"), "clear",
                 "(L" CLS("Background") ";)V", a, NULL);
            check(i2d_canvas_pixel(&c, 20, 20) == 0xFF0000u &&
                  i2d_canvas_pixel(&c, 220, 300) == 0xFF0000u,
                  "an animated red Background clears the target red");

            a[0].i = 0;
            call(vm, c.bg, CLS("Background"), "animate", "(I)I", a, NULL);
            a[0].r = c.bg;
            call(vm, c.g3d, CLS("Graphics3D"), "clear",
                 "(L" CLS("Background") ";)V", a, NULL);
            check(i2d_canvas_pixel(&c, 20, 20) == 0u,
                  "and moving the animation back to t=0 clears it black again");
        }
    }
}

static void test_animation_file(nj_vm *vm)
{
    printf("== a file's animation objects load and animate ==\n");
    {
        nj_u1 buf[320];
        nj_u4 n = build_anim_file(buf, (nj_u4)sizeof buf);
        nj_ref bytes, arr = 0, grp = 0, track = 0, kfs = 0, ctl = 0;
        nj_value a[4], r;
        nj_status st;
        nj_u4 i, len;

        check(n > 0u && n <= (nj_u4)sizeof buf, "the crafted file has a length");
        if (n == 0u) {
            return;
        }
        bytes = nj_array_new(vm, "B", n);
        check(bytes != 0, "a byte array to hand Loader.load");
        if (!bytes) {
            return;
        }
        memcpy(nj_array_data(bytes), buf, n);

        a[0].r = bytes;
        a[1].i = 0;
        nj_clear_exception(vm);
        st = nj_call(vm, 0, CLS("Loader"), "load",
                     "([BI)[L" CLS("Object3D") ";", a, &r);
        check(st == NJ_OK && r.r != 0,
              "Loader.load returns the file's root objects");
        if (st != NJ_OK || !r.r) {
            nj_clear_exception(vm);
            return;
        }
        arr = r.r;
        len = nj_array_length(arr);
        for (i = 0u; i < len; i++) {
            nj_ref o = nj_array_get_ref(arr, i);
            const char *cn = o ? nj_ref_class_name(o) : NULL;

            if (cn && strstr(cn, "Group")) {
                grp = o;
            }
        }
        check(grp != 0, "the animated Group is one of the roots");
        if (!grp) {
            return;
        }
        a[0].i = 0;
        r.i = -1;
        call(vm, grp, CLS("Group"), "getAnimationTrackCount", "()I", NULL, &r);
        check(r.i == 1, "the Object3D header's track is attached to the node");
        track = nj_call_ref(vm, grp, CLS("Group"), "getAnimationTrack",
                            "(I)L" CLS("AnimationTrack") ";", a);
        check(track != 0, "getAnimationTrack(0) returns it");
        if (!track) {
            return;
        }
        call(vm, track, CLS("AnimationTrack"), "getTargetProperty", "()I",
             NULL, &r);
        check(r.i == 275, "the track carries the file's TRANSLATION property");
        kfs = nj_call_ref(vm, track, CLS("AnimationTrack"),
                          "getKeyframeSequence",
                          "()L" CLS("KeyframeSequence") ";", NULL);
        check(kfs != 0, "the track's sequence is resolved");
        if (kfs) {
            call(vm, kfs, CLS("KeyframeSequence"), "getKeyframeCount", "()I",
                 NULL, &r);
            check(r.i == 2, "the sequence's two keyframes are decoded");
            call(vm, kfs, CLS("KeyframeSequence"), "getDuration", "()I", NULL, &r);
            check(r.i == 1000, "and its duration");
        }
        ctl = nj_call_ref(vm, track, CLS("AnimationTrack"), "getController",
                          "()L" CLS("AnimationController") ";", NULL);
        check(ctl != 0, "the track's controller is resolved");
        if (ctl) {
            r.f = -1.0f;
            call(vm, ctl, CLS("AnimationController"), "getSpeed", "()F", NULL, &r);
            check(r.f == 1.0f, "and the controller's speed");
        }
        a[0].i = 250;
        r.i = -1;
        call(vm, grp, CLS("Object3D"), "animate", "(I)I", a, &r);
        check(r.i == 1,
              "animate(250) on the loaded node reports a pending event");
        check(fabsf(node_matrix(vm, grp, 3) - 2.5f) < 1e-4f,
              "and the loaded curve moves the node to half its 10-unit sweep");
        a[0].i = 1500;
        call(vm, grp, CLS("Object3D"), "animate", "(I)I", a, &r);
        check(fabsf(node_matrix(vm, grp, 3) - 10.0f) < 1e-4f,
              "past the last keyframe the loaded curve holds its last value");
    }
}


/* ---------------------------- SkinnedMesh ------------------------------ */

static void put_le16(nj_u1 *p, int v)
{
    p[0] = (nj_u1)(v & 0xFF);
    p[1] = (nj_u1)((v >> 8) & 0xFF);
}

/*
 * A minimal JSR-184 file holding one SkinnedMesh: a quad at z = -200 whose
 * four vertices are all driven by one bone, plus the skeleton Group that owns
 * that bone.  References use object number + 2, the numbering the loader's
 * ref-mode detection settles on for a file whose Groups carry no children
 * list it can check against.
 *
 * with_skin == 0 emits the same SkinnedMesh without the skeleton tail.  The
 * format allows that, and it must still draw its bind pose rather than turn
 * into an invisible surface.
 */
static nj_u4 build_skin_file(nj_u1 *b, nj_u4 cap, int with_skin)
{
    static const nj_u1 ident[12] = {
        0xAB, 0x4A, 0x53, 0x52, 0x31, 0x38, 0x34,
        0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    nj_u4 lens[8];
    nj_u4 nobj = 0u, body, hdrsec, sec, total, i;
    nj_u1 *p;

    lens[nobj++] = 41u;   /* VertexArray: four xyz shorts            */
    lens[nobj++] = 48u;   /* VertexBuffer: no texture units          */
    lens[nobj++] = 41u;   /* TriangleStripArray: four u32 indices    */
    lens[nobj++] = 33u;   /* Appearance: no textures                 */
    lens[nobj++] = 15u;   /* PolygonMode: CULL_NONE                  */
    if (with_skin) {
        lens[nobj++] = 30u;  /* Group: the skeleton, one child       */
        lens[nobj++] = 26u;  /* Group: the bone                      */
    }
    lens[nobj++] = with_skin ? 62u : 38u;   /* SkinnedMesh            */
    body = 0u;
    for (i = 0u; i < nobj; i++) {
        body += 5u + lens[i];
    }
    hdrsec = 30u;
    sec = 9u + body + 4u;
    total = 12u + hdrsec + sec;
    if (cap < total) {
        return 0u;
    }
    memset(b, 0, cap);
    memcpy(b, ident, 12u);
    b[12] = 0u;
    put_le32(b + 13, hdrsec);
    put_le32(b + 17, 17u);
    put_le32(b + 22, 12u);
    b[26] = 1u;
    put_le32(b + 29, total);
    put_le32(b + 33, total);
    b[42] = 0u;
    put_le32(b + 43, sec);
    put_le32(b + 47, body);
    p = b + 51;

    /* object 0: positions, -50..50 in x/y at z = -200 */
    p[0] = 20u; put_le32(p + 1, lens[0]); p += 5;
    p[12] = 2u; p[13] = 3u; p[14] = 0u; put_le16(p + 15, 4);
    put_le16(p + 17, -50); put_le16(p + 19, -50); put_le16(p + 21, -200);
    put_le16(p + 23, 50);  put_le16(p + 25, -50); put_le16(p + 27, -200);
    put_le16(p + 29, -50); put_le16(p + 31, 50);  put_le16(p + 33, -200);
    put_le16(p + 35, 50);  put_le16(p + 37, 50);  put_le16(p + 39, -200);
    p += lens[0];

    /* object 1: the vertex buffer */
    p[0] = 21u; put_le32(p + 1, lens[1]); p += 5;
    put_le32(p + 12, 0xFFFFFFFFu);      /* opaque white default color */
    put_le32(p + 16, 2u);               /* positions = object 0 + 2   */
    put_f32(p + 20, 0.0f); put_f32(p + 24, 0.0f); put_f32(p + 28, 0.0f);
    put_f32(p + 32, 1.0f);              /* position scale             */
    p += lens[1];

    /* object 2: the triangle strip */
    p[0] = 11u; put_le32(p + 1, lens[2]); p += 5;
    p[12] = 128u;                       /* u32 indices                */
    put_le32(p + 13, 4u);
    put_le32(p + 17, 0u); put_le32(p + 21, 1u);
    put_le32(p + 25, 2u); put_le32(p + 29, 3u);
    put_le32(p + 33, 1u);               /* one strip                  */
    put_le32(p + 37, 4u);               /* four indices               */
    p += lens[2];

    /* object 3: the appearance, pointing at the polygon mode */
    p[0] = 3u; put_le32(p + 1, lens[3]); p += 5;
    put_le32(p + 21, 6u);               /* PolygonMode = object 4 + 2 */
    p += lens[3];

    /* object 4: no culling, so the winding cannot hide the quad */
    p[0] = 8u; put_le32(p + 1, lens[4]); p += 5;
    p[12] = 162u; p[13] = 165u; p[14] = 168u;
    p += lens[4];

    if (with_skin) {
        /* object 5: the skeleton Group, holding the bone as its child */
        p[0] = 9u; put_le32(p + 1, lens[5]); p += 5;
        p[14] = 1u; p[15] = 1u; p[16] = 0xFFu;
        put_le32(p + 17, 0xFFFFFFFFu);
        put_le32(p + 22, 1u);           /* one child                  */
        put_le32(p + 26, 8u);           /* bone = object 6 + 2        */
        p += lens[5];

        /* object 6: the bone */
        p[0] = 9u; put_le32(p + 1, lens[6]); p += 5;
        p[14] = 1u; p[15] = 1u; p[16] = 0xFFu;
        put_le32(p + 17, 0xFFFFFFFFu);
        p += lens[6];
    }

    /* the SkinnedMesh itself */
    p[0] = 16u; put_le32(p + 1, lens[nobj - 1u]); p += 5;
    p[14] = 1u; p[15] = 1u; p[16] = 0xFFu;
    put_le32(p + 17, 0xFFFFFFFFu);      /* alpha + scope = the marker */
    put_le32(p + 22, 3u);               /* VertexBuffer = object 1 + 2 */
    put_le32(p + 26, 1u);               /* one submesh                */
    put_le32(p + 30, 4u);               /* index buffer = object 2 + 2 */
    put_le32(p + 34, 5u);               /* Appearance = object 3 + 2   */
    if (with_skin) {
        put_le32(p + 38, 7u);           /* skeleton = object 5 + 2    */
        put_le32(p + 42, 1u);           /* one bone record            */
        put_le32(p + 46, 8u);           /* bone = object 6 + 2        */
        put_le32(p + 50, 0u);           /* firstVertex                */
        put_le32(p + 54, 4u);           /* vertexCount                */
        put_le32(p + 58, 255u);         /* weight                     */
    }
    return total;
}

/* Load a crafted file and pick its SkinnedMesh out of the root array. */
static nj_ref skin_load(nj_vm *vm, int with_skin)
{
    nj_u1 buf[448];
    nj_u4 n = build_skin_file(buf, (nj_u4)sizeof buf, with_skin);
    nj_ref bytes, arr = 0, skin = 0;
    nj_value a[4], r;
    nj_u4 i;

    check(n > 0u, "the crafted SkinnedMesh file has a length");
    if (n == 0u) {
        return 0;
    }
    bytes = nj_array_new(vm, "B", n);
    check(bytes != 0, "a byte array to hand Loader.load");
    if (!bytes) {
        return 0;
    }
    memcpy(nj_array_data(bytes), buf, n);
    a[0].r = bytes;
    a[1].i = 0;
    nj_clear_exception(vm);
    if (nj_call(vm, 0, CLS("Loader"), "load",
                "([BI)[L" CLS("Object3D") ";", a, &r) != NJ_OK ||
        r.r == 0) {
        check(0, "Loader.load returns the SkinnedMesh file's roots");
        return 0;
    }
    arr = r.r;
    for (i = 0u; i < nj_array_length(arr); i++) {
        nj_ref o = nj_array_get_ref(arr, i + 1u);
        const char *cn = nj_ref_class_name(o);

        if (cn && strstr(cn, "SkinnedMesh")) {
            skin = o;
            break;
        }
    }
    check(skin != 0, "the file's SkinnedMesh is one of its roots");
    return skin;
}

/*
 * Render `node` into `targetg` from an identity camera and return the number
 * of pixels that are not the black clear colour, plus their mean column.
 */
static int skin_render(nj_vm *vm, nj_ref g3d, nj_ref node, nj_ref bg,
                       nj_ref cam, nj_ref cam_xf, nj_ref targetg,
                       nj_ref pixels, float *mean_x)
{
    nj_value a[4];
    const nj_u4 *px;
    int i, lit = 0, black = 0;
    double sum = 0.0;

    a[0].r = targetg;
    call(vm, g3d, CLS("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V",
         a, NULL);
    a[0].i = 0; a[1].i = 0; a[2].i = 240; a[3].i = 320;
    call(vm, g3d, CLS("Graphics3D"), "setViewport", "(IIII)V", a, NULL);
    a[0].r = cam; a[1].r = cam_xf;
    call(vm, g3d, CLS("Graphics3D"), "setCamera",
         "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);
    a[0].r = bg;
    call(vm, g3d, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V",
         a, NULL);
    px = (const nj_u4 *)nj_array_data(pixels);
    if (!px || !targetg) {
        return 0;
    }
    for (i = 0; i < 240 * 320; i++) {
        if ((px[i] & 0xFFFFFFu) == 0u) {
            black++;
        }
    }
    /* Image.createImage() starts white: without a clear that reaches every
       pixel, "some pixels are not black" would pass on a render that drew
       nothing at all. */
    check(black == 240 * 320, "clear(Background) blackens the whole target");
    a[0].r = node; a[1].r = cam_xf;
    call(vm, g3d, CLS("Graphics3D"), "render",
         "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
    call(vm, g3d, CLS("Graphics3D"), "releaseTarget", "()V", NULL, NULL);
    for (i = 0; i < 240 * 320; i++) {
        if ((px[i] & 0xFFFFFFu) != 0u) {
            lit++;
            sum += (double)(i % 240);
        }
    }
    if (mean_x) {
        *mean_x = lit > 0 ? (float)(sum / (double)lit) : 0.0f;
    }
    return lit;
}

static int skin_render_prepare(nj_vm *vm, nj_ref g3d, nj_ref *target,
                               nj_ref *targetg, nj_ref *pixels)
{
    nj_value a[2], r;

    *target = 0; *targetg = 0; *pixels = 0;
    a[0].i = 240; a[1].i = 320;
    if (nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                       "(II)Ljavax/microedition/lcdui/Image;", a, &r) != NJ_OK
        || r.r == 0) {
        return 0;
    }
    *target = r.r;
    r.r = 0;
    call(vm, *target, "javax/microedition/lcdui/Image", "getGraphics",
         "()Ljavax/microedition/lcdui/Graphics;", NULL, &r);
    *targetg = r.r;
    *pixels = nj_rget(vm, *target,
                      nj_field_lookup(vm, "javax/microedition/lcdui/Image",
                                      "imgPixels", "[I", 0));
    return *targetg != 0 && *pixels != 0;
}

/*
 * A PolygonMode's three booleans are file data, not just an API setting: the
 * loader has to carry them to the Java object, because the six titles that
 * ask for two-sided lighting ask for it in the file (43 PolygonModes: the two
 * rally titles, Colin McRae, Counter-Strike, the zombie shooter and the
 * Bluetooth 3D fighter).  This fixture runs the whole path - container,
 * object walk, decode, link - rather than the decoder alone, and the shorter
 * payload proves that a file without the flags keeps the class defaults
 * instead of reading the absent bytes as "false".
 *
 * Layout copied from build_anim_file(): a thirty-byte file header section,
 * then one stored object section holding an unreferenced root.
 */
static nj_u4 build_pm_file(nj_u1 *b, nj_u4 cap, int with_flags,
                           int two, int local, int persp)
{
    static const nj_u1 ident[12] = {
        0xAB, 0x4A, 0x53, 0x52, 0x31, 0x38, 0x34,
        0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    nj_u4 hdr_sec = 30u, plen = with_flags ? 18u : 15u;
    nj_u4 body = 5u + plen, sec, total;
    nj_u1 *p;

    sec = 9u + body + 4u;
    total = 12u + hdr_sec + sec;
    if (cap < total) {
        return 0u;
    }
    memset(b, 0, total);
    memcpy(b, ident, 12u);
    b[12] = 0u;                        /* stored, not compressed */
    put_le32(b + 13, hdr_sec);
    put_le32(b + 17, 17u);
    put_le32(b + 22, 12u);
    b[26] = 1u;                        /* file version 1.0 */
    put_le32(b + 29, total);
    put_le32(b + 33, total);
    b[42] = 0u;
    put_le32(b + 43, sec);
    put_le32(b + 47, body);
    p = b + 51;
    p[0] = 8u;                         /* PolygonMode */
    put_le32(p + 1, plen);
    p += 5;
    p[12] = 162u; p[13] = 165u; p[14] = 168u;   /* NONE, SMOOTH, CCW */
    if (with_flags) {
        p[15] = (nj_u1)(two ? 1 : 0);
        p[16] = (nj_u1)(local ? 1 : 0);
        p[17] = (nj_u1)(persp ? 1 : 0);
    }
    return total;
}

static void test_polygon_mode_file(nj_vm *vm)
{
    struct pm_case {
        int with_flags, two, local, persp;
        int want_two, want_local, want_persp;
    };
    /* The first file exercises all three flags; the second omits them. */
    static const struct pm_case cases[2] = {
        { 1, 1, 1, 0, 1, 1, 0 },
        { 0, 0, 0, 0, 0, 0, 1 }
    };
    int c;

    printf("== a file's PolygonMode carries its lighting flags ==\n");
    for (c = 0; c < 2; c++) {
        nj_u1 buf[128];
        nj_u4 n = build_pm_file(buf, (nj_u4)sizeof buf, cases[c].with_flags,
                                cases[c].two, cases[c].local, cases[c].persp);
        nj_ref bytes, arr = 0, pm = 0;
        nj_value a[2], r;
        nj_status st;
        nj_u4 i, len;

        check(n > 0u, "the crafted PolygonMode file has a length");
        if (n == 0u) {
            continue;
        }
        bytes = nj_array_new(vm, "B", n);
        check(bytes != 0, "a byte array to hand Loader.load");
        if (!bytes) {
            continue;
        }
        memcpy(nj_array_data(bytes), buf, n);

        a[0].r = bytes; a[1].i = 0;
        memset(&r, 0, sizeof r);
        nj_clear_exception(vm);
        st = nj_call(vm, 0, CLS("Loader"), "load",
                     "([BI)[L" CLS("Object3D") ";", a, &r);
        check(st == NJ_OK && r.r != 0,
              "Loader.load returns the PolygonMode file's roots");
        if (st != NJ_OK || !r.r) {
            nj_clear_exception(vm);
            continue;
        }
        arr = r.r;
        len = nj_array_length(arr);
        for (i = 0u; i < len; i++) {
            nj_ref o = nj_array_get_ref(arr, i);
            const char *cn = o ? nj_ref_class_name(o) : NULL;

            if (cn && strstr(cn, "PolygonMode")) {
                pm = o;
            }
        }
        check(pm != 0, "the PolygonMode is one of the roots");
        if (!pm) {
            continue;
        }

        r.i = -1;
        call(vm, pm, CLS("PolygonMode"), "isTwoSidedLightingEnabled", "()Z",
             NULL, &r);
        check(r.i == cases[c].want_two,
              cases[c].with_flags
                  ? "the file's two-sided flag reaches the object"
                  : "without the flag two-sided lighting stays off");
        r.i = -1;
        call(vm, pm, CLS("PolygonMode"), "isLocalCameraLightingEnabled", "()Z",
             NULL, &r);
        check(r.i == cases[c].want_local,
              cases[c].with_flags
                  ? "the file's local-camera-lighting flag reaches the object"
                  : "without the flag local camera lighting stays off");
        r.i = -1;
        call(vm, pm, CLS("PolygonMode"), "isPerspectiveCorrectionEnabled", "()Z",
             NULL, &r);
        check(r.i == cases[c].want_persp,
              cases[c].with_flags
                  ? "the file's perspective-correction flag reaches the object"
                  : "without the flag perspective correction keeps its default");
    }
}

static void test_skinned_mesh(nj_vm *vm)
{
    printf("== a file's SkinnedMesh decodes, poses and falls back ==\n");
    {
        nj_ref g3d = make(vm, CLS("Graphics3D"));
        nj_ref skin = skin_load(vm, 1);
        nj_ref vb, skel, bone = 0;
        nj_ref world, cam, cam_xf, bg, target, targetg, pixels;
        nj_value a[4], r;
        float mean_a = 0.0f, mean_b = 0.0f;
        int lit_a, lit_b;

        check(g3d != 0 && skin != 0, "the SkinnedMesh fixture loads");
        if (!g3d || !skin) {
            return;
        }
        vb = nj_call_ref(vm, skin, CLS("SkinnedMesh"), "getVertexBuffer",
                         "()L" CLS("VertexBuffer") ";", NULL);
        check(vb != 0, "its VertexBuffer is linked (the old loader left it null)");
        skel = nj_call_ref(vm, skin, CLS("SkinnedMesh"), "getSkeleton",
                           "()L" CLS("Group") ";", NULL);
        check(skel != 0, "its skeleton Group is linked");
        if (skel) {
            r.i = -1;
            call(vm, skel, CLS("Group"), "getChildCount", "()I", NULL, &r);
            check(r.i == 1, "the skeleton owns the file's bone");
            if (r.i == 1) {
                a[0].i = 0;
                bone = nj_call_ref(vm, skel, CLS("Group"), "getChild",
                                   "(I)L" CLS("Node") ";", a);
                check(bone != 0, "getChild(0) returns the bone");
            }
        }
        if (bone) {
            nj_ref idx = nj_array_new(vm, "I", 8);
            nj_ref wts = nj_array_new(vm, "F", 8);
            const nj_s4 *iv;
            const float *fv;
            int k, ok = 1;

            a[0].r = bone; a[1].r = idx; a[2].r = wts;
            r.i = -1;
            call(vm, skin, CLS("SkinnedMesh"), "getBoneVertices",
                 "(L" CLS("Node") ";[I[F)I", a, &r);
            check(r.i == 4, "the bone owns the quad's four vertices");
            iv = (const nj_s4 *)nj_array_data(idx);
            fv = (const float *)nj_array_data(wts);
            for (k = 0; k < 4 && iv && fv && r.i == 4; k++) {
                if (iv[k] != k || fabsf(fv[k] - 1.0f) > 1e-4f) {
                    ok = 0;
                }
            }
            check(ok, "and their normalised weights are 1");
        }

        world = make(vm, CLS("World"));
        cam = make(vm, CLS("Camera"));
        cam_xf = make(vm, CLS("Transform"));
        bg = make(vm, CLS("Background"));
        check(world && cam && cam_xf && bg, "world, camera and black Background");
        if (!world || !cam || !cam_xf || !bg ||
            !skin_render_prepare(vm, g3d, &target, &targetg, &pixels)) {
            return;
        }
        a[0].r = skin;
        call(vm, world, CLS("Group"), "addChild", "(L" CLS("Node") ";)V",
             a, NULL);
        a[0].i = (nj_s4)0xFF000000u;
        call(vm, bg, CLS("Background"), "setColor", "(I)V", a, NULL);
        a[0].f = 60.0f; a[1].f = 0.75f; a[2].f = 1.0f; a[3].f = 1000.0f;
        call(vm, cam, CLS("Camera"), "setPerspective", "(FFFF)V", a, NULL);
        call(vm, cam_xf, CLS("Transform"), "setIdentity", "()V", NULL, NULL);

        lit_a = skin_render(vm, g3d, world, bg, cam, cam_xf, targetg, pixels,
                            &mean_a);
        check(lit_a > 1000,
              "the skinned quad renders (the old loader drew nothing)");
        if (bone && lit_a > 1000) {
            a[0].f = 30.0f; a[1].f = 0.0f; a[2].f = 0.0f;
            call(vm, bone, CLS("Node"), "setTranslation", "(FFF)V", a, NULL);
            lit_b = skin_render(vm, g3d, world, bg, cam, cam_xf, targetg,
                                pixels, &mean_b);
            check(lit_b > 1000 && mean_b > mean_a + 20.0f,
                  "moving the bone moves the skinned vertices on screen");
        }
        /*
         * Posing used to allocate the position, normal and weight arrays with
         * nj_host_alloc() on every frame and free them after the draw - two or
         * three host allocations per skinned mesh per frame.  The arrays now
         * live on the SkinnedMesh as _nj_pose (seven floats per vertex), and
         * the host allocation counter must not move on a repeated render.
         */
        {
            nj_field *pf = nj_field_lookup(vm, CLS("SkinnedMesh"),
                                           "_nj_pose", "[F", 0);
            nj_ref pose1 = pf ? nj_rget(vm, skin, pf) : 0;
            unsigned before;
            int rerendered;

            check(pf != 0 && pose1 != 0,
                  "the pose buffers are cached on the SkinnedMesh");
            before = host_alloc_calls;
            rerendered = skin_render(vm, g3d, world, bg, cam, cam_xf, targetg,
                                     pixels, NULL);
            check(rerendered > 1000,
                  "and a render through the cached pose still draws it");
            check(host_alloc_calls == before,
                  "a repeated skinned render allocates no host memory");
            if (pf) {
                check(nj_rget(vm, skin, pf) == pose1,
                      "and reuses the same pose array instead of a new one");
            }
        }
    }
    {
        nj_ref g3d = make(vm, CLS("Graphics3D"));
        nj_ref skin = skin_load(vm, 0);
        nj_ref vb, skel;
        nj_ref world, cam, cam_xf, bg, target, targetg, pixels;
        nj_value a[4];
        int lit;

        check(g3d != 0 && skin != 0,
              "a SkinnedMesh with no skeleton tail still loads");
        if (!g3d || !skin) {
            return;
        }
        vb = nj_call_ref(vm, skin, CLS("SkinnedMesh"), "getVertexBuffer",
                         "()L" CLS("VertexBuffer") ";", NULL);
        check(vb != 0, "it keeps its VertexBuffer (bind-pose fallback)");
        skel = nj_call_ref(vm, skin, CLS("SkinnedMesh"), "getSkeleton",
                           "()L" CLS("Group") ";", NULL);
        check(skel == 0, "and has no skeleton to pose against");
        world = make(vm, CLS("World"));
        cam = make(vm, CLS("Camera"));
        cam_xf = make(vm, CLS("Transform"));
        bg = make(vm, CLS("Background"));
        if (!world || !cam || !cam_xf || !bg ||
            !skin_render_prepare(vm, g3d, &target, &targetg, &pixels)) {
            return;
        }
        a[0].r = skin;
        call(vm, world, CLS("Group"), "addChild", "(L" CLS("Node") ";)V",
             a, NULL);
        a[0].i = (nj_s4)0xFF000000u;
        call(vm, bg, CLS("Background"), "setColor", "(I)V", a, NULL);
        a[0].f = 60.0f; a[1].f = 0.75f; a[2].f = 1.0f; a[3].f = 1000.0f;
        call(vm, cam, CLS("Camera"), "setPerspective", "(FFFF)V", a, NULL);
        call(vm, cam_xf, CLS("Transform"), "setIdentity", "()V", NULL, NULL);
        lit = skin_render(vm, g3d, world, bg, cam, cam_xf, targetg, pixels,
                          NULL);
        check(lit > 1000, "and it renders its bind pose instead of nothing");
    }
    /*
     * The render tests that follow keep their images and targets in plain C
     * locals, which the collector cannot see.  A collection triggered from
     * inside one of them would reclaim those and make its bindTarget fail, so
     * collect the garbage this test made here instead.
     */
    nj_gc_collect(vm);
}


/* ------------------------------- */
/* A Mesh whose strip array uses the file format's implicit-index encoding. */

static nj_u4 build_tsa0_file(nj_u1 *b, nj_u4 cap)
{
    static const nj_u1 ident[12] = { 0xAB, 0x4A, 0x53, 0x52, 0x31, 0x38, 0x34,
                                     0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    static const nj_u4 lens[6] = { 53u, 48u, 29u, 33u, 18u, 38u };
    nj_u4 body = 0u, total, k;
    nj_u1 *p;

    for (k = 0u; k < 6u; k++) {
        body += 5u + lens[k];
    }
    total = 12u + 30u + 9u + body + 4u;
    if (cap < total) {
        return 0u;
    }
    memset(b, 0, cap);
    memcpy(b, ident, 12u);
    b[12] = 0u;                             /* header section, stored */
    put_le32(b + 13, 30u);
    put_le32(b + 17, 17u);
    put_le32(b + 22, 12u);                  /* Object3D header length */
    b[26] = 1u;                             /* file version 1.0 */
    put_le32(b + 29, total);
    put_le32(b + 33, total);
    b[42] = 0u;                             /* object section */
    put_le32(b + 43, 9u + body + 4u);
    put_le32(b + 47, body);
    p = b + 51u;

    /* object 0: four positions */
    p[0] = 20u;
    put_le32(p + 1, lens[0]);
    p += 5;
    p[12] = 2u;                             /* componentSize */
    p[13] = 3u;                             /* components */
    p[14] = 0u;                             /* uncompressed shorts */
    put_le16(p + 15, 6);                    /* vertexCount */
    /* The first strip is the lower-left half of a quad; the second is a
       degenerate heap in the lower-right corner.  Read as one six-vertex
       strip (what the renderer falls back to when the array is refused), the
       join adds the upper-right triangle as well, so the lit area doubles and
       the test can tell the two readings apart. */
    put_le16(p + 17, -50); put_le16(p + 19, -50); put_le16(p + 21, -200);
    put_le16(p + 23, 50);  put_le16(p + 25, -50); put_le16(p + 27, -200);
    put_le16(p + 29, -50); put_le16(p + 31, 50);  put_le16(p + 33, -200);
    put_le16(p + 35, 50);  put_le16(p + 37, 50);  put_le16(p + 39, -200);
    put_le16(p + 41, 50);  put_le16(p + 43, 50);  put_le16(p + 45, -200);
    put_le16(p + 47, 50);  put_le16(p + 49, 50);  put_le16(p + 51, -200);
    p += lens[0];

    /* object 1: the vertex buffer, positions only */
    p[0] = 21u;
    put_le32(p + 1, lens[1]);
    p += 5;
    put_le32(p + 12, 0xFFFFFFFFu);          /* default colour, opaque white */
    put_le32(p + 16, 2u);                   /* positions -> object 0 */
    put_f32(p + 32, 1.0f);                  /* position scale */
    p += lens[1];

    /* object 2: the strips, encoding zero (no index table at all) */
    p[0] = 11u;
    put_le32(p + 1, lens[2]);
    p += 5;
    p[12] = 0u;                             /* encoding zero */
    put_le32(p + 13, 0u);                   /* first vertex */
    put_le32(p + 17, 2u);                   /* strip count */
    put_le32(p + 21, 3u);                   /* first strip length */
    put_le32(p + 25, 3u);                   /* second strip length */
    p += lens[2];

    /* object 3: an appearance with only a polygon mode */
    p[0] = 3u;
    put_le32(p + 1, lens[3]);
    p += 5;
    put_le32(p + 21, 6u);                   /* polygon mode -> object 4 */
    p += lens[3];

    /* object 4: cull nothing, so winding cannot hide the quad */
    p[0] = 8u;
    put_le32(p + 1, lens[4]);
    p += 5;
    p[12] = 162u;
    p[13] = 165u;
    p[14] = 168u;
    p += lens[4];

    /* object 5: the Mesh that binds them */
    p[0] = 14u;
    put_le32(p + 1, lens[5]);
    p += 5;
    p[14] = 1u;                             /* rendering enabled */
    p[15] = 1u;                             /* picking enabled */
    p[16] = 0xFFu;                          /* alpha */
    put_le32(p + 17, 0xFFFFFFFFu);          /* scope */
    put_le32(p + 22, 3u);                   /* vertex buffer -> object 1 */
    put_le32(p + 26, 1u);                   /* one submesh */
    put_le32(p + 30, 4u);                   /* its index buffer -> object 2 */
    put_le32(p + 34, 5u);                   /* its appearance -> object 3 */

    return total;
}

static nj_ref tsa0_load(nj_vm *vm)
{
    nj_u1 buf[512];
    nj_u4 n = build_tsa0_file(buf, (nj_u4)sizeof buf);
    nj_ref bytes, arr = 0, mesh = 0;
    nj_value a[2], r;
    nj_u4 i;

    check(n > 0u, "the crafted encoding-zero Mesh file has a length");
    if (n == 0u) {
        return 0;
    }
    bytes = nj_array_new(vm, "B", n);
    if (!bytes) {
        check(0, "a byte array to hand Loader.load");
        return 0;
    }
    memcpy(nj_array_data(bytes), buf, n);
    a[0].r = bytes;
    a[1].i = 0;
    nj_clear_exception(vm);
    if (nj_call(vm, 0, CLS("Loader"), "load",
                "([BI)[L" CLS("Object3D") ";", a, &r) != NJ_OK ||
        r.r == 0) {
        check(0, "Loader.load returns the encoding-zero file's roots");
        return 0;
    }
    arr = r.r;
    for (i = 0u; i < nj_array_length(arr); i++) {
        nj_ref o = nj_array_get_ref(arr, i);
        const char *cn = nj_ref_class_name(o);

        if (cn && strcmp(cn, CLS("Mesh")) == 0) {
            mesh = o;
            break;
        }
    }
    check(mesh != 0, "the file's Mesh is one of its roots");
    return mesh;
}

static void test_triangle_strip_encoding0(nj_vm *vm)
{
    printf("== a strip array with encoding zero provides its indices ==\n");
    {
        nj_ref g3d = make(vm, CLS("Graphics3D"));
        nj_ref mesh = tsa0_load(vm);
        nj_ref cam, cam_xf, bg, target, targetg, pixels;
        nj_ref ib;
        nj_value a[4], r;
        int lit;

        check(g3d != 0 && mesh != 0, "the encoding-zero fixture loads");
        if (!g3d || !mesh) {
            return;
        }
        r.i = -1;
        call(vm, mesh, CLS("Mesh"), "getSubmeshCount", "()I", NULL, &r);
        check(r.i == 1, "the Mesh links its one submesh");
        ib = nj_call_ref(vm, mesh, CLS("Mesh"), "getIndexBuffer",
                         "(I)L" CLS("IndexBuffer") ";", NULL);
        if (!ib) {
            a[0].i = 0;
            ib = nj_call_ref(vm, mesh, CLS("Mesh"), "getIndexBuffer",
                             "(I)L" CLS("IndexBuffer") ";", a);
        }
        check(ib != 0, "its index buffer is linked (the old loader refused it)");

        cam = make(vm, CLS("Camera"));
        cam_xf = make(vm, CLS("Transform"));
        bg = make(vm, CLS("Background"));
        check(cam && cam_xf && bg, "camera, transform and black Background");
        if (!cam || !cam_xf || !bg ||
            !skin_render_prepare(vm, g3d, &target, &targetg, &pixels)) {
            return;
        }
        a[0].i = (nj_s4)0xFF000000u;
        call(vm, bg, CLS("Background"), "setColor", "(I)V", a, NULL);
        a[0].f = 60.0f; a[1].f = 0.75f; a[2].f = 1.0f; a[3].f = 1000.0f;
        call(vm, cam, CLS("Camera"), "setPerspective", "(FFFF)V", a, NULL);
        call(vm, cam_xf, CLS("Transform"), "setIdentity", "()V", NULL, NULL);

        lit = skin_render(vm, g3d, mesh, bg, cam, cam_xf, targetg, pixels,
                          NULL);
        /* Two three-vertex strips: half a quad plus a degenerate one, about
           9.6k pixels.  Read as a single six-vertex strip instead, the joins
           fill the other half of the quad as well, about 19k pixels. */
        check(lit > 7000 && lit < 14000,
              "the strips render from their own implicit indices");
    }
    nj_gc_collect(vm);
}

/* ------------------------------------------------------------------ */
/* Two-sided lighting                                                  */
/*
 * JSR 184 (PolygonMode.TWO_SIDED_LIGHTING): a back-facing polygon is lit with
 * its vertices' normals negated, so the visible back side is shaded like a
 * front side would be.  This fixture authors one triangle in the back-facing
 * winding and gives it (0,0,1) normals; the only light shines towards -z (its
 * transform is rotated 180 degrees about y), so the front-side colour is black
 * - the material has no ambient or emissive term - while the back-side colour,
 * computed from the negated normal, is white.  With the flag off the triangle
 * draws black; with it on the same pixels turn white.
 */
static void test_two_sided_lighting(nj_vm *vm)
{
    i2d_canvas c;
    nj_value a[6], r;
    nj_ref pos, posva, nva, vb, ib, app, poly, mat, light, lxf, lens;
    int ok = 1, off_lit = -1, on_lit = -1;

    printf("== two-sided lighting lights the back of a polygon ==\n");
    ok = i2d_canvas_init(vm, &c);
    pos = nj_array_new(vm, "S", 9u);
    lens = nj_array_new(vm, "I", 1u);
    posva = nj_object_new(vm, CLS("VertexArray"));
    nva = nj_object_new(vm, CLS("VertexArray"));
    vb = make(vm, CLS("VertexBuffer"));
    ib = nj_object_new(vm, CLS("TriangleStripArray"));
    app = make(vm, CLS("Appearance"));
    poly = make(vm, CLS("PolygonMode"));
    mat = make(vm, CLS("Material"));
    light = make(vm, CLS("Light"));
    lxf = make(vm, CLS("Transform"));
    ok = ok && pos != 0 && lens != 0 && posva != 0 && nva != 0 && vb != 0 &&
         ib != 0 && app != 0 && poly != 0 && mat != 0 && light != 0 &&
         lxf != 0;
    if (!ok) {
        check(0, "the two-sided scene allocates");
        return;
    }

    /*
     * A triangle whose projection winds the other way round: the JSR front
     * face is counter-clockwise, so on screen this one is the back side.
     * Model units are clip units here - 60 pixels each - so it covers the
     * lower-left half of the target.
     */
    {
        nj_s2 mine[9] = { -4, -4, -4,   -4, 4, -4,   4, -4, -4 };

        memcpy(nj_array_data(pos), mine, sizeof mine);
        ((nj_s4 *)nj_array_data(lens))[0] = 3;
        a[0].i = 3; a[1].i = 3; a[2].i = 2;      /* 3 vertices, 3 by 2 bytes */
        ok = ok && nj_call_special(vm, posva, CLS("VertexArray"), "<init>",
                                   "(III)V", a, NULL) == NJ_OK;
        a[0].i = 0; a[1].i = 3; a[2].r = pos;
        ok = ok && nj_call(vm, posva, CLS("VertexArray"), "set", "(II[S)V",
                           a, NULL) == NJ_OK;
        a[0].r = posva; a[1].f = 0.5f; a[2].r = 0;
        ok = ok && nj_call(vm, vb, CLS("VertexBuffer"), "setPositions",
                           "(L" CLS("VertexArray") ";F[F)V", a, NULL) == NJ_OK;
    }
    /* Every normal points at the camera; the light will be on the far side. */
    {
        nj_s2 mine[9] = { 0, 0, 64,   0, 0, 64,   0, 0, 64 };

        memcpy(nj_array_data(pos), mine, sizeof mine);
        a[0].i = 3; a[1].i = 3; a[2].i = 2;
        ok = ok && nj_call_special(vm, nva, CLS("VertexArray"), "<init>",
                                   "(III)V", a, NULL) == NJ_OK;
        a[0].i = 0; a[1].i = 3; a[2].r = pos;
        ok = ok && nj_call(vm, nva, CLS("VertexArray"), "set", "(II[S)V",
                           a, NULL) == NJ_OK;
        a[0].r = nva; a[1].f = 1.0f / 64.0f; a[2].r = 0;
        ok = ok && nj_call(vm, vb, CLS("VertexBuffer"), "setNormals",
                           "(L" CLS("VertexArray") ";)V", a, NULL) == NJ_OK;
    }
    a[0].i = 0; a[1].r = lens;                   /* first index, strip lengths */
    ok = ok && nj_call_special(vm, ib, CLS("TriangleStripArray"), "<init>",
                               "(I[I)V", a, NULL) == NJ_OK;

    /* CULL_NONE (162) / SHADE_SMOOTH / winding CCW, two-sided off to start. */
    a[0].i = 162;
    ok = ok && nj_call(vm, poly, CLS("PolygonMode"), "setCulling", "(I)V",
                       a, NULL) == NJ_OK;
    a[0].i = 165;
    ok = ok && nj_call(vm, poly, CLS("PolygonMode"), "setShading", "(I)V",
                       a, NULL) == NJ_OK;
    a[0].i = 168;
    ok = ok && nj_call(vm, poly, CLS("PolygonMode"), "setWinding", "(I)V",
                       a, NULL) == NJ_OK;
    a[0].i = 0;
    ok = ok && nj_call(vm, poly, CLS("PolygonMode"),
                       "setTwoSidedLightingEnable", "(Z)V", a, NULL) == NJ_OK;

    /* No ambient and no emissive term: front-side lighting on this face is 0. */
    a[0].i = 1024; a[1].i = 0;
    ok = ok && nj_call(vm, mat, CLS("Material"), "setColor", "(II)V",
                       a, NULL) == NJ_OK;
    a[0].i = 2048; a[1].i = (nj_s4)0xFFFFFFFFu;
    ok = ok && nj_call(vm, mat, CLS("Material"), "setColor", "(II)V",
                       a, NULL) == NJ_OK;
    a[0].i = 4096; a[1].i = 0;
    ok = ok && nj_call(vm, mat, CLS("Material"), "setColor", "(II)V",
                       a, NULL) == NJ_OK;
    a[0].i = 8192; a[1].i = 0;
    ok = ok && nj_call(vm, mat, CLS("Material"), "setColor", "(II)V",
                       a, NULL) == NJ_OK;
    a[0].r = mat;
    ok = ok && nj_call(vm, app, CLS("Appearance"), "setMaterial",
                       "(L" CLS("Material") ";)V", a, NULL) == NJ_OK;
    a[0].r = poly;
    ok = ok && nj_call(vm, app, CLS("Appearance"), "setPolygonMode",
                       "(L" CLS("PolygonMode") ";)V", a, NULL) == NJ_OK;

    /* One directional light, rotated 180 degrees about y: it shines at +z. */
    a[0].i = 129;
    ok = ok && nj_call(vm, light, CLS("Light"), "setMode", "(I)V", a, NULL) == NJ_OK;
    a[0].i = (nj_s4)0xFFFFFFFFu;
    ok = ok && nj_call(vm, light, CLS("Light"), "setColor", "(I)V", a, NULL) == NJ_OK;
    a[0].f = 1.0f;
    ok = ok && nj_call(vm, light, CLS("Light"), "setIntensity", "(F)V",
                       a, NULL) == NJ_OK;
    ok = ok && nj_call(vm, lxf, CLS("Transform"), "setIdentity", "()V",
                       NULL, NULL) == NJ_OK;
    a[0].f = 180.0f; a[1].f = 0.0f; a[2].f = 1.0f; a[3].f = 0.0f;
    ok = ok && nj_call(vm, lxf, CLS("Transform"), "postRotate", "(FFFF)V",
                       a, NULL) == NJ_OK;
    ok = ok && nj_call(vm, c.g3d, CLS("Graphics3D"), "resetLights", "()V",
                       NULL, NULL) == NJ_OK;
    memset(&r, 0, sizeof r);
    a[0].r = light; a[1].r = lxf;
    ok = ok && nj_call(vm, c.g3d, CLS("Graphics3D"), "addLight",
                       "(L" CLS("Light") ";L" CLS("Transform") ";)I",
                       a, &r) == NJ_OK && r.i == 0;

    a[0].r = c.bg;
    ok = ok && nj_call(vm, c.g3d, CLS("Graphics3D"), "clear",
                       "(L" CLS("Background") ";)V", a, NULL) == NJ_OK;
    a[0].r = vb; a[1].r = ib; a[2].r = app; a[3].r = 0; a[4].i = 0;
    ok = ok && nj_call(vm, c.g3d, CLS("Graphics3D"), "render",
                       "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                       CLS("Appearance") ";L" CLS("Transform") ";I)V",
                       a, NULL) == NJ_OK;
    off_lit = ok ? i2d_canvas_lit_pixels(&c) : -1;

    a[0].i = 1;                                  /* two-sided on */
    ok = ok && nj_call(vm, poly, CLS("PolygonMode"),
                       "setTwoSidedLightingEnable", "(Z)V", a, NULL) == NJ_OK;
    a[0].r = c.bg;
    ok = ok && nj_call(vm, c.g3d, CLS("Graphics3D"), "clear",
                       "(L" CLS("Background") ";)V", a, NULL) == NJ_OK;
    a[0].r = vb; a[1].r = ib; a[2].r = app; a[3].r = 0; a[4].i = 0;
    ok = ok && nj_call(vm, c.g3d, CLS("Graphics3D"), "render",
                       "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                       CLS("Appearance") ";L" CLS("Transform") ";I)V",
                       a, NULL) == NJ_OK;
    on_lit = ok ? i2d_canvas_lit_pixels(&c) : -1;

    check(ok, "the two-sided scene builds and renders");
    check(off_lit >= 0 && off_lit < 600,
          "a back face stays black while two-sided lighting is off");
    check(on_lit > 15000,
          "two-sided lighting shades the back face white");
    check(on_lit > off_lit + 15000,
          "enabling two-sided lighting is what changed the pixels");
}

int main(void)
{
    nj_port port;

#if !NJ_ENABLE_M3G
    /*
     * A build that trimmed the module out has no m3g classes, and that is the
     * module working as intended - so this test skips rather than failing.
     * The skip is loud, because a silent pass would hide the module being
     * left out of a build that wanted it.
     */
    printf("SKIP: JSR 184 is compiled out of this build (NJ_ENABLE_M3G=0)\n");
    return 0;
#endif

    nj_vm *vm;
    static const char *classes[] = {
        "Object3D", "Transform", "Transformable", "Node", "Group", "World",
        "Camera", "Background", "Graphics3D", "Loader", "Image2D", "Mesh",
        "Appearance", "Material", "PolygonMode", "CompositingMode",
        "Texture2D", "VertexBuffer", "IndexBuffer", "Sprite3D", "AnimationTrack"
    };
    size_t i;

    nj_port_headless_ex(&port, 240, 320, 0, NULL);
    /* Both hooks, so every nj_host_alloc() is counted and every block is
       released with the matching allocator. */
    port.alloc = counting_alloc;
    port.free = counting_free;
    /*
     * 4 MB, not 2 MB: this suite deliberately never releases its fixtures (the
     * canvas alone pins a 240x320 ARGB buffer, 307 KB), so the late tests that
     * build a fresh canvas - the spot-light cone and the Transform matrix
     * checks - ran out of heap and reported their fixture as failed.
     *
     * Measured: with the suite finished, one collection leaves 4,064 bytes
     * used, i.e. nothing is retained and there is no VM-held reference leak.
     * The 2 MB figure was simply short during the moment when a canvas and the
     * M3G objects around it are live together.
     */
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    host_alloc_calls = 0u;      /* count from here, not the VM's own setup */
    /*
     * Lay the builtin classes out and seed their constants before anything is
     * looked up.  Without this the static slots the constants live in do not
     * exist yet, and nj_field_read_int() on one dereferences a slot that was
     * never assigned - a segfault, not a failing assertion.
     */
    (void)nj_lib_late_init(vm);
    if (!vm) {
        printf("FAILED: no VM\n");
        return 1;
    }

    printf("== the package exists ==\n");
    for (i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        char full[96];
        snprintf(full, sizeof(full), CLS("%s"), classes[i]);
        check(nj_class_lookup(vm, full) != NULL, classes[i]);
    }
    /* The control: the lookup itself must be able to fail. */
    check(nj_class_lookup(vm, CLS("NoSuchClassHere")) == NULL,
          "a class that is not there is not found");

    printf("== transform maths ==\n");
    {
        nj_ref t = make(vm, CLS("Transform"));
        nj_ref out = nj_array_new(vm, "F", 16);
        nj_value a[4];

        a[0].f = 10.0f; a[1].f = 20.0f; a[2].f = 30.0f;
        call(vm, t, CLS("Transform"), "postTranslate", "(FFF)V", a, NULL);
        a[0].r = out;
        call(vm, t, CLS("Transform"), "get", "([F)V", a, NULL);
        check(out && ((float *)out->u.bytes)[3] == 10.0f &&
              ((float *)out->u.bytes)[7] == 20.0f,
              "get() hands back the matrix");
    }

    {
        nj_ref t = make(vm, CLS("Transform"));
        nj_ref v;
        nj_value a[4], n;   /* the transform natives take three floats */
        float got = 0.0f;

        check(t != 0, "new Transform()");
        v = nj_array_new(vm, "F", 4);
        check(v != 0, "float[4]");
        if (t && v) {
            ((float *)v->u.bytes)[0] = 1.0f;
            ((float *)v->u.bytes)[1] = 2.0f;
            ((float *)v->u.bytes)[2] = 3.0f;
            ((float *)v->u.bytes)[3] = 1.0f;

            a[0].f = 10.0f; a[1].f = 20.0f; a[2].f = 30.0f;
            call(vm, t, CLS("Transform"), "postTranslate", "(FFF)V", a, NULL);
            a[0].r = v;
            call(vm, t, CLS("Transform"), "transform", "([F)V", a, &n);
            got = ((float *)v->u.bytes)[0];
            check(fabsf(got - 11.0f) < 1e-4f, "a translation moves x by 10");

            /* postScale multiplies on the right, so the scale applies first. */
            a[0].f = 2.0f; a[1].f = 2.0f; a[2].f = 2.0f;
            call(vm, t, CLS("Transform"), "postScale", "(FFF)V", a, NULL);
            ((float *)v->u.bytes)[0] = 1.0f;
            ((float *)v->u.bytes)[1] = 2.0f;
            ((float *)v->u.bytes)[2] = 3.0f;
            ((float *)v->u.bytes)[3] = 1.0f;
            a[0].r = v;
            call(vm, t, CLS("Transform"), "transform", "([F)V", a, &n);
            check(fabsf(((float *)v->u.bytes)[0] - 12.0f) < 1e-4f,
                  "a scale after a translation scales before it");

            /* Inverting an affine matrix undoes it. */
            call(vm, t, CLS("Transform"), "invert", "()V", NULL, NULL);
            ((float *)v->u.bytes)[0] = 12.0f;
            ((float *)v->u.bytes)[1] = 24.0f;
            ((float *)v->u.bytes)[2] = 36.0f;
            ((float *)v->u.bytes)[3] = 1.0f;
            a[0].r = v;
            call(vm, t, CLS("Transform"), "transform", "([F)V", a, &n);
            check(fabsf(((float *)v->u.bytes)[0] - 1.0f) < 1e-3f &&
                  fabsf(((float *)v->u.bytes)[1] - 2.0f) < 1e-3f,
                  "invert() undoes the transform");

            /* A 90 degree rotation about z maps +x onto +y. */
            call(vm, t, CLS("Transform"), "setIdentity", "()V", NULL, NULL);
            a[0].f = 90.0f; a[1].f = 0.0f; a[2].f = 0.0f; a[3].f = 1.0f;
            call(vm, t, CLS("Transform"), "postRotate", "(FFFF)V", a, NULL);
            ((float *)v->u.bytes)[0] = 1.0f;
            ((float *)v->u.bytes)[1] = 0.0f;
            ((float *)v->u.bytes)[2] = 0.0f;
            ((float *)v->u.bytes)[3] = 1.0f;
            a[0].r = v;
            call(vm, t, CLS("Transform"), "transform", "([F)V", a, &n);
            check(fabsf(((float *)v->u.bytes)[0]) < 1e-4f &&
                  fabsf(((float *)v->u.bytes)[1] - 1.0f) < 1e-4f,
                  "postRotate(90, z) maps +x to +y");
        }
    }

    printf("== the scene graph ==\n");
    {
        nj_ref g = make(vm, CLS("Group"));
        nj_ref child = make(vm, CLS("Group"));
        nj_ref other = make(vm, CLS("Group"));
        nj_value a[4], n;   /* the transform natives take three floats */
        int child_count = -1;

        check(g != 0 && child != 0, "two Groups");
        a[0].r = child;
        call(vm, g, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", a, NULL);
        call(vm, g, CLS("Group"), "getChildCount", "()I", NULL, &n);
        child_count = n.i;
        check(child_count == 1, "addChild makes the count one");

        call(vm, child, CLS("Node"), "getParent",
             "()L" CLS("Node") ";", NULL, &n);
        check(n.r == g, "the child knows its parent");

        /* A node with a parent cannot be added again - the JSR says so. */
        {
            nj_status st;
            nj_ref exc;
            a[0].r = child;
            st = nj_call(vm, other, CLS("Group"), "addChild",
                         "(L" CLS("Node") ";)V", a, NULL);
            exc = nj_take_exception(vm);
            check(st != NJ_OK && exc != 0, "a second parent is refused");
        }

        /* find() walks children: give the child an id and ask the group. */
        if (child) {
            a[0].i = 4242;
            call(vm, child, CLS("Object3D"), "setUserID", "(I)V", a, NULL);
            a[0].i = 4242;
            call(vm, g, CLS("Object3D"), "find", "(I)L" CLS("Object3D") ";",
                 a, &n);
            check(n.r == child, "find(id) reaches a child");
        }

        /* Object3D.find() is inherited by every node type, but only a World
         * carries the file-wide object table.  A Mesh must answer from the
         * scene graph (null here) instead of reading the World-only slot: the
         * old code read whatever sat at that offset and crashed the VM on a
         * corpus fighting game. */
        {
            nj_ref world = make(vm, CLS("World"));
            nj_ref mesh = make(vm, CLS("Mesh"));
            n.r = 0;
            a[0].i = 4242;
            call(vm, mesh, CLS("Object3D"), "find", "(I)L" CLS("Object3D") ";",
                 a, &n);
            check(world && mesh && n.r == 0,
                  "find() on a Mesh does not consult the World table");
            check(nj_take_exception(vm) == 0, "find() on a Mesh does not throw");
        }

        /* The check above is a guard, not a reproducer: a bare Mesh has
         * _nj_nsub == 0, so the old aliasing read would have fetched slot zero
         * and survived.  Give the Mesh one submesh and the aliased value is a
         * small non-zero integer - on the old code nj_array_length() then
         * dereferences it and the VM dies, while the class check returns null
         * before any World slot is read. */
        {
            nj_field *sub = nj_field_lookup(
                vm, "javax/microedition/m3g/Mesh", "_nj_nsub", "I", 0);
            nj_ref world = make(vm, CLS("World"));
            nj_ref mesh = make(vm, CLS("Mesh"));

            n.r = 0;
            check(sub && world && mesh, "a Mesh, a World and its submesh count");
            if (sub && world && mesh) {
                nj_field_write_int(vm, mesh, sub, 1);
                a[0].i = 4242;
                call(vm, mesh, CLS("Object3D"), "find",
                     "(I)L" CLS("Object3D") ";", a, &n);
                check(n.r == 0,
                      "find() on a Mesh with a submesh still answers null");
                check(nj_take_exception(vm) == 0,
                      "find() with a submesh does not throw");
            }
        }
    }

    printf("== setTransform(null) restores the identity ==\n");
    {
        nj_ref g = make(vm, CLS("Group"));
        nj_ref t = make(vm, CLS("Transform"));
        nj_field *mf = nj_field_lookup(vm, "javax/microedition/m3g/Transformable",
                                       "_nj_m", "[F", 0);
        nj_value a[4];

        check(g && t && mf, "a Group, a Transform and its matrix");
        if (g && t && mf) {
            float *m;

            a[0].f = 5.0f; a[1].f = 6.0f; a[2].f = 7.0f;
            call(vm, t, CLS("Transform"), "postTranslate", "(FFF)V", a, NULL);
            a[0].r = t;
            call(vm, g, CLS("Node"), "setTransform",
                 "(L" CLS("Transform") ";)V", a, NULL);
            m = nj_rget(vm, g, mf) ? (float *)nj_array_data(nj_rget(vm, g, mf)) : NULL;
            check(m && m[3] == 5.0f, "a Transform is copied into the node");

            a[0].r = 0;
            call(vm, g, CLS("Node"), "setTransform",
                 "(L" CLS("Transform") ";)V", a, NULL);
            m = nj_rget(vm, g, mf) ? (float *)nj_array_data(nj_rget(vm, g, mf)) : NULL;
            check(m && m[0] == 1.0f && m[3] == 0.0f && m[5] == 1.0f &&
                  m[7] == 0.0f && m[15] == 1.0f,
                  "setTransform(null) puts the identity back");
        }
    }

    printf("== getTransformTo walks both nodes ==\n");
    {
        nj_ref root = make(vm, CLS("Group"));
        nj_ref a = make(vm, CLS("Group")), b = make(vm, CLS("Group"));
        nj_ref xf = make(vm, CLS("Transform"));
        nj_ref rel = make(vm, CLS("Group"));
        nj_value av[4], n;

        check(root && a && b && xf, "a scene graph and a Transform");
        if (root && a && b && xf) {
            av[0].r = a;
            call(vm, root, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", av, NULL);
            av[0].r = b;
            call(vm, root, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", av, NULL);
            av[0].f = 1.0f; av[1].f = 0.0f; av[2].f = 0.0f;
            call(vm, a, CLS("Node"), "setTranslation", "(FFF)V", av, NULL);
            av[0].f = 2.0f;
            call(vm, b, CLS("Node"), "setTranslation", "(FFF)V", av, NULL);

            av[0].r = b;
            av[1].r = xf;
            call(vm, a, CLS("Node"), "getTransformTo",
                 "(L" CLS("Node") ";L" CLS("Transform") ";)Z", av, &n);
            check(n.i == 1, "siblings in one graph answer true");
            check(fabsf(xf_elem(vm, xf, 3) + 1.0f) < 1e-4f,
                  "a's origin is at -1 in b's space (the body's part counts)");
            check(fabsf(xf_elem(vm, xf, 7)) < 1e-4f &&
                  fabsf(xf_elem(vm, xf, 11)) < 1e-4f,
                  "the other axes are untouched");

            /* Ancestor case: root -> a is a's translation inverted. */
            av[0].r = a;
            call(vm, root, CLS("Node"), "getTransformTo",
                 "(L" CLS("Node") ";L" CLS("Transform") ";)Z", av, &n);
            check(n.i == 1 && fabsf(xf_elem(vm, xf, 3) + 1.0f) < 1e-4f,
                  "an ancestor query still lands on -1");

            /* A node in another graph has no answer. */
            av[0].r = rel;
            av[1].r = xf;
            call(vm, a, CLS("Node"), "getTransformTo",
                 "(L" CLS("Node") ";L" CLS("Transform") ";)Z", av, &n);
            check(n.i == 0, "nodes from different graphs answer false");
        }
    }

    printf("== what is not implemented says so ==\n");
    {
        nj_value a[4], n;   /* the transform natives take three floats */
        nj_ref s = nj_string_new(vm, "level.m3g");
        nj_status st;
        nj_ref exc;

        a[0].r = s;
        st = nj_call(vm, 0, CLS("Loader"), "load",
                     "(Ljava/lang/String;)[L" CLS("Object3D") ";", a, &n);
        exc = nj_take_exception(vm);
        check(st != NJ_OK && exc != 0, "Loader.load throws rather than lying");
        if (exc) {
            check(nj_ref_class_name(exc) != NULL &&
                  strstr(nj_ref_class_name(exc), "IOException") != NULL,
                  "and it throws java.io.IOException");
        }
    }

    printf("== an M3G file embedded in a container stops at its own length ==\n");
    {
        /*
         * 3D激爆拳击_8805 stores eight M3G files back to back and calls
         * Loader.load(data, 0), load(data, 517), ...  The file header's
         * totalFileSize is the authoritative end of one file; walking the
         * sections until the buffer runs out instead read the next file's
         * 0xAB identifier byte as a compression scheme and rejected the
         * model ("the section at 517 uses compression scheme 171").  The
         * reference loader bounds the walk by totalFileSize for the same
         * reason.
         */
        nj_u1 buf[96];
        static const nj_u1 ident[12] = {
            0xAB, 0x4A, 0x53, 0x52, 0x31, 0x38, 0x34,
            0xBB, 0x0D, 0x0A, 0x1A, 0x0A
        };
        nj_m3g_object objs[4];
        nj_u4 nobj = 0, nsec = 0;
        char err[160];
        nj_status st;

        /* one file: 12 byte identifier, 30 byte header section, then one
           18 byte section holding a single zero-payload Group object */
        memset(buf, 0, sizeof(buf));
        memcpy(buf, ident, 12);
        put_le32(buf + 13, 30);          /* header section length */
        put_le32(buf + 17, 17);          /* its uncompressed length */
        put_le32(buf + 22, 12);          /* Object3D header length */
        buf[26] = 1;                     /* version 1.0 */
        put_le32(buf + 29, 60);          /* totalFileSize */
        put_le32(buf + 33, 60);          /* approximate content size */
        put_le32(buf + 43, 18);          /* object section length */
        put_le32(buf + 47, 5);           /* its uncompressed length */
        buf[51] = 9;                     /* Group */
        memcpy(buf + 60, ident, 12);     /* the next file's identifier */

        st = nj_m3g_container_probe(buf, 72, NULL, 0, err, sizeof err);
        check(st == NJ_OK,
              "a container with a second file after the first probes clean");
        st = nj_m3g_scan_objects(0, buf, 72, objs, 4, &nobj, &nsec,
                                 err, sizeof err);
        check(st == NJ_OK && nsec == 2u && nobj == 1u,
              "and its objects are read from the first file only");
        st = nj_m3g_container_probe(buf, 60, NULL, 0, err, sizeof err);
        check(st == NJ_OK, "the same file alone probes clean");
        st = nj_m3g_scan_objects(0, buf, 60, objs, 4, &nobj, &nsec,
                                 err, sizeof err);
        check(st == NJ_OK && nobj == 1u,
              "and scans the same way with nothing after it");

        put_le32(buf + 29, 0);           /* no usable declared length */
        st = nj_m3g_container_probe(buf, 72, NULL, 0, err, sizeof err);
        check(st != NJ_OK && strstr(err, "compression scheme") != NULL,
              "without a declared length the trailing bytes are still caught");
        put_le32(buf + 29, 76);          /* declares more than the buffer */
        st = nj_m3g_container_probe(buf, 72, NULL, 0, err, sizeof err);
        check(st != NJ_OK, "a file that declares more than it has is refused");
        put_le32(buf + 29, 12);          /* shorter than the header */
        st = nj_m3g_container_probe(buf, 72, NULL, 0, err, sizeof err);
        check(st != NJ_OK, "and a nonsensical declared length falls back too");
    }

    printf("== the camera says whether it has a projection ==\n");
    {
        nj_ref cam = make(vm, CLS("Camera"));
        nj_ref t = make(vm, CLS("Transform"));
        nj_value a[4], n;

        check(cam != 0 && t != 0, "a Camera and a Transform");
        if (cam && t) {
            a[0].r = t;
            call(vm, cam, CLS("Camera"), "getProjection",
                 "(L" CLS("Transform") ";)I", a, &n);
            /*
             * JSR 184's no-argument Camera has a perspective projection
             * already - 45 degrees, aspect 1, near 1, far 1000 - so a
             * fresh Camera answers 1.  This assertion used to read 0,
             * which is what the runtime answered when its constructor
             * left the camera with no projection at all.
             */
            check(n.i == 1, "a fresh Camera has the default perspective");

            a[0].f = 45.0f; a[1].f = 1.5f; a[2].f = 1.0f; a[3].f = 100.0f;
            call(vm, cam, CLS("Camera"), "setPerspective", "(FFFF)V", a, NULL);
            a[0].r = t;
            call(vm, cam, CLS("Camera"), "getProjection",
                 "(L" CLS("Transform") ";)I", a, &n);
            check(n.i == 1, "after setPerspective: getProjection returns 1");
        }
    }

    /*
     * Light.  The class is registered because 生化危机3D(CAPCOM经典)_3671 asks
     * for it; these assertions are the only thing that keeps it honest.
     */
    {
        nj_ref l = make(vm, CLS("Light"));
        nj_ref atten;
        nj_value a[4], r;

        check(l != 0, "a Light can be constructed");
        call(vm, l, CLS("Light"), "getMode", "()I", NULL, &r);
        check(r.i == 129, "a new Light is directional");

        a[0].i = 131;
        call(vm, l, CLS("Light"), "setMode", "(I)V", a, NULL);
        call(vm, l, CLS("Light"), "getMode", "()I", NULL, &r);
        check(r.i == 131, "setMode and getMode agree");

        a[0].f = 1.0f; a[1].f = 2.0f; a[2].f = 3.0f;
        call(vm, l, CLS("Light"), "setAttenuation", "(FFF)V", a, NULL);
        atten = nj_array_new(vm, "F", 3);
        a[0].r = atten;
        call(vm, l, CLS("Light"), "getAttenuation", "([F)V", a, NULL);
        check(((float *)atten->u.bytes)[0] == 1.0f &&
              ((float *)atten->u.bytes)[2] == 3.0f,
              "attenuation round trips through an array");

        /*
         * The three float fields.  This is the block that matters: the class
         * declared _nj_intensity and _nj_spotAngle as ints while every native
         * looked them up as floats, so constructing a Light raised
         * InternalError, and make() returned a non-null object anyway - the
         * other checks in this block passed on a Light whose constructor had
         * thrown.  浴火银河2 is what noticed.  So: assert no exception is
         * pending, not just that the ref is non-null.
         */
        check(!nj_has_exception(vm),
              "Light.<init> returns without an exception");
        nj_clear_exception(vm);

        a[0].f = 0.75f;
        call(vm, l, CLS("Light"), "setIntensity", "(F)V", a, NULL);
        call(vm, l, CLS("Light"), "getIntensity", "()F", NULL, &r);
        check(!nj_has_exception(vm), "setIntensity is bound to its field");
        check(r.f == 0.75f, "intensity round trips");
        nj_clear_exception(vm);

        a[0].f = 30.0f;
        call(vm, l, CLS("Light"), "setSpotAngle", "(F)V", a, NULL);
        call(vm, l, CLS("Light"), "getSpotAngle", "()F", NULL, &r);
        check(!nj_has_exception(vm), "setSpotAngle is bound to its field");
        check(r.f == 30.0f, "spot angle round trips");
        nj_clear_exception(vm);

        a[0].f = 2.5f;
        call(vm, l, CLS("Light"), "setSpotExponent", "(F)V", a, NULL);
        call(vm, l, CLS("Light"), "getSpotExponent", "()F", NULL, &r);
        check(!nj_has_exception(vm), "setSpotExponent is bound to its field");
        check(r.f == 2.5f, "spot exponent round trips");
        nj_clear_exception(vm);

        /*
         * The static constants are looked up but not read back: this test has
         * no safe way to read a static field, and doing it the obvious way
         * (nj_field_read_int with a NULL receiver) dereferences obj + offset
         * and dies.
         */
    printf("== a Material's default colours are the specification's ==\n");
    {
        nj_ref m = make(vm, CLS("Material"));
        nj_value a[1], r;

        check(m != 0, "a Material constructs");
        a[0].i = 1024;                           /* Material.AMBIENT */
        call(vm, m, CLS("Material"), "getColor", "(I)I", a, &r);
        /*
         * JSR 184 section 11.3: ambient 0x00333333, diffuse 0xFFCCCCCC.
         * The ambient field used to hold 0x3F800000, the bit pattern of the
         * float 1.0, which tinted every default-material surface green.
         */
        check((nj_u4)r.i == 0x00333333u,
              "default ambient is the grey 0x00333333");
        a[0].i = 2048;                           /* Material.DIFFUSE */
        call(vm, m, CLS("Material"), "getColor", "(I)I", a, &r);
        check((nj_u4)r.i == 0xFFCCCCCCu,
              "default diffuse is 0xFFCCCCCC");
    }

    printf("== Light arguments the specification rejects ==\n");
    {
        nj_ref l = make(vm, CLS("Light"));
        nj_ref atten;
        nj_value a[4], r;

        check(l != 0, "a Light to argue with");
        a[0].i = 131;
        call(vm, l, CLS("Light"), "setMode", "(I)V", a, NULL);
        nj_clear_exception(vm);

        /* mode: outside AMBIENT..SPOT is IllegalArgument, value unchanged */
        a[0].i = 127;
        call(vm, l, CLS("Light"), "setMode", "(I)V", a, NULL);
        check(nj_has_exception(vm), "setMode(127) throws");
        nj_take_exception(vm);
        a[0].i = 132;
        call(vm, l, CLS("Light"), "setMode", "(I)V", a, NULL);
        check(nj_has_exception(vm), "setMode(132) throws");
        nj_take_exception(vm);
        call(vm, l, CLS("Light"), "getMode", "()I", NULL, &r);
        check(r.i == 131, "and the mode it already had survives");

        /* spot angle: [0, 90]; the ends are accepted */
        a[0].f = 60.0f;
        call(vm, l, CLS("Light"), "setSpotAngle", "(F)V", a, NULL);
        nj_clear_exception(vm);
        a[0].f = -1.0f;
        call(vm, l, CLS("Light"), "setSpotAngle", "(F)V", a, NULL);
        check(nj_has_exception(vm), "setSpotAngle(-1) throws");
        nj_take_exception(vm);
        a[0].f = 90.5f;
        call(vm, l, CLS("Light"), "setSpotAngle", "(F)V", a, NULL);
        check(nj_has_exception(vm), "setSpotAngle(90.5) throws");
        nj_take_exception(vm);
        call(vm, l, CLS("Light"), "getSpotAngle", "()F", NULL, &r);
        check(r.f == 60.0f, "and the 60 degrees it had survive");
        a[0].f = 0.0f;
        call(vm, l, CLS("Light"), "setSpotAngle", "(F)V", a, NULL);
        check(!nj_has_exception(vm), "setSpotAngle(0) is accepted");
        nj_clear_exception(vm);
        a[0].f = 90.0f;
        call(vm, l, CLS("Light"), "setSpotAngle", "(F)V", a, NULL);
        check(!nj_has_exception(vm), "setSpotAngle(90) is accepted");
        nj_clear_exception(vm);

        /* spot exponent: [0, 128] */
        a[0].f = 0.198f;
        call(vm, l, CLS("Light"), "setSpotExponent", "(F)V", a, NULL);
        nj_clear_exception(vm);
        a[0].f = -0.5f;
        call(vm, l, CLS("Light"), "setSpotExponent", "(F)V", a, NULL);
        check(nj_has_exception(vm), "setSpotExponent(-0.5) throws");
        nj_take_exception(vm);
        a[0].f = 128.5f;
        call(vm, l, CLS("Light"), "setSpotExponent", "(F)V", a, NULL);
        check(nj_has_exception(vm), "setSpotExponent(128.5) throws");
        nj_take_exception(vm);
        call(vm, l, CLS("Light"), "getSpotExponent", "()F", NULL, &r);
        check(r.f == 0.198f, "and the exponent it had survives");
        a[0].f = 128.0f;
        call(vm, l, CLS("Light"), "setSpotExponent", "(F)V", a, NULL);
        check(!nj_has_exception(vm), "setSpotExponent(128) is accepted");
        nj_clear_exception(vm);

        /* attenuation: no negatives, not all zero */
        atten = nj_array_new(vm, "F", 3);
        ((float *)atten->u.bytes)[0] = 1.0f;
        ((float *)atten->u.bytes)[1] = 0.0f;
        ((float *)atten->u.bytes)[2] = 0.0f;
        a[0].f = -1.0f; a[1].f = 0.0f; a[2].f = 0.0f;
        call(vm, l, CLS("Light"), "setAttenuation", "(FFF)V", a, NULL);
        check(nj_has_exception(vm), "setAttenuation(-1,0,0) throws");
        nj_take_exception(vm);
        a[0].f = 0.0f; a[1].f = 0.0f; a[2].f = 0.0f;
        call(vm, l, CLS("Light"), "setAttenuation", "(FFF)V", a, NULL);
        check(nj_has_exception(vm), "setAttenuation(0,0,0) throws");
        nj_take_exception(vm);
        a[0].f = 0.0f; a[1].f = 1.0f; a[2].f = 0.0f;
        call(vm, l, CLS("Light"), "setAttenuation", "(FFF)V", a, NULL);
        check(!nj_has_exception(vm), "setAttenuation(0,1,0) is accepted");
        nj_clear_exception(vm);
        a[0].r = atten;
        call(vm, l, CLS("Light"), "getAttenuation", "([F)V", a, NULL);
        check(((float *)atten->u.bytes)[0] == 0.0f &&
              ((float *)atten->u.bytes)[1] == 1.0f,
              "and the accepted coefficients are what is stored");
    }

    printf("== a Light read out of a file keeps its cone ==\n");
    {
        /*
         * 毁灭战士3_10598 ships its spot light in an M3G asset: mode SPOT,
         * spotAngle 22.5 degrees, spotExponent 0.198, attenuation {1,0,0}.
         * The loader copied only mode, colour and intensity, and an object
         * built there never ran Light's constructor either, so the angle
         * read back as 0 - a cone of no width - and the light rejected every
         * vertex: the whole 3D menu layer was rendered black.  Link the
         * record by hand and check what lands in the object.
         */
        nj_m3g_object objs[1];
        nj_ref arr, l;
        nj_value r;

        memset(objs, 0, sizeof objs);
        objs[0].type = 12u;                      /* the Light object type */
        objs[0].fields.decoded = 1u;
        objs[0].fields.b = 131u;                 /* Light.SPOT */
        objs[0].fields.a = 0x00FFFFFFu;          /* white */
        objs[0].fields.f[0] = 1.0f;              /* attenuation */
        objs[0].fields.f[1] = 0.0f;
        objs[0].fields.f[2] = 0.0f;
        objs[0].fields.f[3] = 1.0f;              /* intensity */
        objs[0].fields.f[4] = 22.5f;             /* spotAngle */
        objs[0].fields.f[5] = 0.198f;            /* spotExponent */

        l = make(vm, CLS("Light"));
        arr = nj_object_array_new(vm, CLS("Light"), 2);
        check(l != 0 && arr != 0, "a Light and the array the loader links");
        if (l && arr) {
            nj_array_set_ref(arr, 1, l);
            nj_root(vm, arr);
            nj_root(vm, l);
            check(nj_m3g_link_misc(vm, objs, 1u, arr) == NJ_OK,
                  "the Light record links");
            call(vm, l, CLS("Light"), "getSpotAngle", "()F", NULL, &r);
            check(r.f == 22.5f, "and its spot angle comes from the file");
            call(vm, l, CLS("Light"), "getSpotExponent", "()F", NULL, &r);
            check(r.f == 0.198f, "as does its spot exponent");
            call(vm, l, CLS("Light"), "getMode", "()I", NULL, &r);
            check(r.i == 131, "and its mode is still the file's");
        }
    }

        check(nj_field_lookup(vm, CLS("Light"), "SPOT", "I", 1) != NULL,
              "Light.SPOT is declared as a static field");
    }

    /*
     * The members that arrived with Light.  test_natives can prove a binding
     * exists; only this can prove the getter reads the field the setter wrote,
     * which is the failure mode that actually happened elsewhere in this tree
     * (Integer.hashCode declared, unbound; a setter writing one field while its
     * getter read another).
     *
     * Only round trips with an unambiguous answer are asserted.  JSR 184 gives
     * Texture2D.setFiltering(int,int) but no per-axis getter, so there is no
     * honest way to check both halves - asserting "one of the two" would be a
     * test that cannot fail.
     */
    {
        nj_value a[4], r;
        nj_ref img, tex, app, g3, lit, xf, poly;

        memset(&r, 0, sizeof(r));
        memset(a, 0, sizeof(a));

        a[0].i = 100; a[1].i = 8; a[2].i = 8;
        img = nj_object_new(vm, CLS("Image2D"));
        check(img != 0 &&
              nj_call_special(vm, img, CLS("Image2D"), "<init>", "(III)V", a, NULL) == NJ_OK,
              "an Image2D can be constructed");

        a[0].r = img;
        tex = nj_object_new(vm, CLS("Texture2D"));
        check(tex != 0 &&
              nj_call_special(vm, tex, CLS("Texture2D"), "<init>",
                              "(L" CLS("Image2D") ";)V", a, NULL) == NJ_OK,
              "a Texture2D can be constructed around it");

        app  = make(vm, CLS("Appearance"));
        g3   = make(vm, CLS("Graphics3D"));
        lit  = make(vm, CLS("Light"));
        xf   = make(vm, CLS("Transform"));
        poly = make(vm, CLS("PolygonMode"));
        check(app && g3 && lit && xf && poly, "the Light-era classes construct");

        if (tex && img) {
            call(vm, tex, CLS("Texture2D"), "getImage",
                 "()L" CLS("Image2D") ";", NULL, &r);
            check(r.r == img, "Texture2D.getImage returns the image it was built with");

            a[0].r = img;
            call(vm, tex, CLS("Texture2D"), "setImage",
                 "(L" CLS("Image2D") ";)V", a, NULL);
            call(vm, tex, CLS("Texture2D"), "getImage",
                 "()L" CLS("Image2D") ";", NULL, &r);
            check(r.r == img, "Texture2D.setImage and getImage agree");

            /* Rally's road repeats 600 times along V.  Texture2D must inherit
               Transformable's storage as well as its methods; otherwise the
               matrix field aliases the image or sampler fields. */
            if (xf) {
                nj_ref matrix = nj_array_new(vm, "F", 16u);
                float *v = (float *)nj_array_data(matrix);

                check(matrix != 0, "a texture transform output array allocates");
                a[0].f = 1.0f; a[1].f = 600.0f; a[2].f = 1.0f;
                call(vm, tex, CLS("Texture2D"), "setScale", "(FFF)V", a, NULL);
                a[0].r = xf;
                call(vm, tex, CLS("Texture2D"), "getCompositeTransform",
                     "(L" CLS("Transform") ";)V", a, NULL);
                if (v) {
                    a[0].r = matrix;
                    call(vm, xf, CLS("Transform"), "get", "([F)V", a, NULL);
                    check(v[0] == 1.0f && v[5] == 600.0f && v[10] == 1.0f,
                          "Texture2D preserves its inherited 600-fold V scale");
                }
                call(vm, tex, CLS("Texture2D"), "getImage",
                     "()L" CLS("Image2D") ";", NULL, &r);
                check(r.r == img, "texture transform storage does not alias its image");
                {
                    nj_ref clone;
                    call(vm, tex, CLS("Object3D"), "duplicate",
                         "()L" CLS("Object3D") ";", NULL, &r);
                    clone = r.r;
                    check(clone && clone != tex, "duplicate creates a distinct texture");
                    call(vm, clone, CLS("Texture2D"), "getImage",
                         "()L" CLS("Image2D") ";", NULL, &r);
                    check(r.r == img, "a duplicate texture retains the shared image");
                    a[0].f = 2.0f; a[1].f = 3.0f; a[2].f = 4.0f;
                    call(vm, clone, CLS("Texture2D"), "setScale", "(FFF)V", a, NULL);
                    a[0].r = xf;
                    call(vm, tex, CLS("Texture2D"), "getCompositeTransform",
                         "(L" CLS("Transform") ";)V", a, NULL);
                    a[0].r = matrix;
                    call(vm, xf, CLS("Transform"), "get", "([F)V", a, NULL);
                    check(v && v[5] == 600.0f,
                          "changing a duplicate texture leaves the source transform intact");
                }
            }
        }

        if (app && tex) {
            a[0].i = 0; a[1].r = tex;
            call(vm, app, CLS("Appearance"), "setTexture",
                 "(IL" CLS("Texture2D") ";)V", a, NULL);
            call(vm, app, CLS("Appearance"), "getTexture",
                 "(I)L" CLS("Texture2D") ";", a, &r);
            check(r.r == tex, "Appearance.setTexture and getTexture agree");
            {
                nj_ref clone;
                call(vm, app, CLS("Object3D"), "duplicate",
                     "()L" CLS("Object3D") ";", NULL, &r);
                clone = r.r;
                a[0].i = 0;
                call(vm, clone, CLS("Appearance"), "getTexture",
                     "(I)L" CLS("Texture2D") ";", a, &r);
                check(r.r == tex, "a duplicate appearance retains its texture");
                a[1].r = 0;
                call(vm, clone, CLS("Appearance"), "setTexture",
                     "(IL" CLS("Texture2D") ";)V", a, NULL);
                call(vm, app, CLS("Appearance"), "getTexture",
                     "(I)L" CLS("Texture2D") ";", a, &r);
                check(r.r == tex, "changing duplicate texture slots leaves the source intact");
            }
        }

        if (g3 && lit && xf) {
            a[0].r = lit; a[1].r = xf;
            call(vm, g3, CLS("Graphics3D"), "addLight",
                 "(L" CLS("Light") ";L" CLS("Transform") ";)I", a, &r);
            check(r.i == 0, "the first addLight is index 0");
            call(vm, g3, CLS("Graphics3D"), "addLight",
                 "(L" CLS("Light") ";L" CLS("Transform") ";)I", a, &r);
            check(r.i == 1, "the second addLight is index 1");
            {
                nj_ref values = nj_array_new(vm, "F", 16);
                float *v = (float *)nj_array_data(values);
                nj_ref out = make(vm, CLS("Transform"));

                memset(v, 0, 16 * sizeof(float));
                v[0] = v[5] = v[10] = v[15] = 1.0f;
                v[3] = 7.0f; v[7] = -4.0f; v[11] = 2.0f;
                a[0].r = values;
                call(vm, xf, CLS("Transform"), "set", "([F)V", a, NULL);
                a[0].i = 1; a[1].r = lit; a[2].r = xf;
                call(vm, g3, CLS("Graphics3D"), "setLight",
                     "(IL" CLS("Light") ";L" CLS("Transform") ";)V", a, NULL);
                a[0].i = 1; a[1].r = out;
                call(vm, g3, CLS("Graphics3D"), "getLight",
                     "(IL" CLS("Transform") ";)L" CLS("Light") ";", a, &r);
                check(r.r == lit, "getLight returns the selected light");
                a[0].r = values;
                call(vm, out, CLS("Transform"), "get", "([F)V", a, NULL);
                check(v[3] == 7.0f && v[7] == -4.0f && v[11] == 2.0f,
                      "getLight preserves its transform");
                a[0].i = 0; a[1].r = out;
                call(vm, g3, CLS("Graphics3D"), "getLight",
                     "(IL" CLS("Transform") ";)L" CLS("Light") ";", a, &r);
                a[0].r = values;
                call(vm, out, CLS("Transform"), "get", "([F)V", a, NULL);
                check(v[0] == 1.0f && v[3] == 0.0f && v[7] == 0.0f,
                      "other light transforms remain independent");
            }
        }

        if (poly) {
            a[0].i = 161;
            call(vm, poly, CLS("PolygonMode"), "setCulling", "(I)V", a, NULL);
            call(vm, poly, CLS("PolygonMode"), "getCulling", "()I", NULL, &r);
            check(r.i == 161, "PolygonMode.setCulling and getCulling agree");
        }
    }

    /*
     * The transform API a node carries, and the node's own switches.
     *
     * 毁灭战士3_10598 calls Group.postRotate() and Node.setScope() while it
     * builds its scene; before these existed the run ended in
     * NoSuchMethodError: javax/microedition/m3g/Group.postRotate(FFFF)V, and a
     * method that is bound but stores the wrong thing would look the same from
     * the launcher.  So each one is read back rather than merely called.
     */
    {
        nj_value a[4], r;
        nj_ref grp = make(vm, CLS("Group"));
        nj_ref xf = make(vm, CLS("Transform"));
        nj_ref m = nj_array_new(vm, "F", 16);
        nj_ref cam = make(vm, CLS("Camera"));
        nj_ref bg = make(vm, CLS("Background"));
        nj_ref world = make(vm, CLS("World"));
        nj_ref img = 0;

        memset(a, 0, sizeof(a));
        memset(&r, 0, sizeof(r));

        a[0].i = 100; a[1].i = 4; a[2].i = 4;
        img = nj_object_new(vm, CLS("Image2D"));
        if (img && nj_call_special(vm, img, CLS("Image2D"), "<init>", "(III)V",
                                   a, NULL) != NJ_OK) {
            img = 0;
        }

        check(grp && xf && m && cam && bg && world && img,
              "the node, camera, background and world classes construct");

        if (grp && xf && m) {
            a[0].f = 5.0f; a[1].f = 0.0f; a[2].f = 0.0f;
            call(vm, grp, CLS("Group"), "translate", "(FFF)V", a, NULL);
            a[0].r = xf;
            call(vm, grp, CLS("Group"), "getCompositeTransform",
                 "(L" CLS("Transform") ";)V", a, NULL);
            a[0].r = m;
            call(vm, xf, CLS("Transform"), "get", "([F)V", a, NULL);
            check(fabsf(((float *)m->u.bytes)[3] - 5.0f) < 1e-4f,
                  "Group.translate reaches getCompositeTransform");

            /*
             * A translation added after a rotation has to stay in the parent's
             * frame, because that is the transform model the JSR describes:
             * T * R * S * M, with translate() editing T.  Applying it to the
             * composite instead rotates the offset through the node's own
             * orientation - 毁灭战士3_10598 gives its door leaves a rotation in
             * the .m3g and then calls translate(0, 2.7, 0) to raise them to
             * door height, so the leaf ended up on the floor beside its frame.
             * Read the composite back after the same sequence: the offset has
             * to be y = 2, not z = -2.
             */
            {
                nj_ref g2 = make(vm, CLS("Group"));

                if (g2) {
                    a[0].f = 90.0f; a[1].f = 1.0f; a[2].f = 0.0f; a[3].f = 0.0f;
                    call(vm, g2, CLS("Group"), "setOrientation", "(FFFF)V", a, NULL);
                    a[0].f = 0.0f; a[1].f = 2.0f; a[2].f = 0.0f;
                    call(vm, g2, CLS("Group"), "translate", "(FFF)V", a, NULL);
                    a[0].r = xf;
                    call(vm, g2, CLS("Group"), "getCompositeTransform",
                         "(L" CLS("Transform") ";)V", a, NULL);
                    a[0].r = m;
                    call(vm, xf, CLS("Transform"), "get", "([F)V", a, NULL);
                    check(fabsf(((float *)m->u.bytes)[7] - 2.0f) < 1e-4f
                              && fabsf(((float *)m->u.bytes)[11]) < 1e-4f,
                          "translate after setOrientation moves in world axes");

                    a[0].f = 7.0f; a[1].f = 0.0f; a[2].f = 4.0f;
                    call(vm, g2, CLS("Group"), "setTranslation", "(FFF)V", a, NULL);
                    a[0].f = 0.0f; a[1].f = 2.0f; a[2].f = 0.0f;
                    call(vm, g2, CLS("Group"), "translate", "(FFF)V", a, NULL);
                    a[0].r = xf;
                    call(vm, g2, CLS("Group"), "getCompositeTransform",
                         "(L" CLS("Transform") ";)V", a, NULL);
                    a[0].r = m;
                    call(vm, xf, CLS("Transform"), "get", "([F)V", a, NULL);
                    check(fabsf(((float *)m->u.bytes)[3] - 7.0f) < 1e-4f
                              && fabsf(((float *)m->u.bytes)[7] - 2.0f) < 1e-4f
                              && fabsf(((float *)m->u.bytes)[11] - 4.0f) < 1e-4f,
                          "translate adds to setTranslation in world axes");

                    /*
                     * setTransform() replaces only the matrix slot, so a scale
                     * set with the component call survives it.  NFS HP sets
                     * setScale(0.0035) and then setTransform() every tick on
                     * the same node.
                     */
                    a[0].f = 0.0f; a[1].f = 0.0f; a[2].f = 1.0f; a[3].f = 0.0f;
                    call(vm, g2, CLS("Group"), "setOrientation", "(FFFF)V", a, NULL);
                    a[0].f = 0.5f; a[1].f = 0.5f; a[2].f = 0.5f;
                    call(vm, g2, CLS("Group"), "setScale", "(FFF)V", a, NULL);
                    a[0].f = 0.0f; a[1].f = 0.0f; a[2].f = 0.0f;
                    call(vm, g2, CLS("Group"), "setTranslation", "(FFF)V", a, NULL);
                    {
                        nj_ref mt = nj_array_new(vm, "F", 16);

                        if (mt) {
                            float *v = (float *)mt->u.bytes;

                            memset(v, 0, 16 * sizeof(float));
                            v[0] = v[5] = v[10] = v[15] = 1.0f;
                            a[0].r = mt;
                            call(vm, xf, CLS("Transform"), "set", "([F)V", a, NULL);
                            a[0].r = xf;
                            call(vm, g2, CLS("Group"), "setTransform",
                                 "(L" CLS("Transform") ";)V", a, NULL);
                            a[0].r = xf;
                            call(vm, g2, CLS("Group"), "getCompositeTransform",
                                 "(L" CLS("Transform") ";)V", a, NULL);
                            a[0].r = m;
                            call(vm, xf, CLS("Transform"), "get", "([F)V", a, NULL);
                            check(fabsf(((float *)m->u.bytes)[0] - 0.5f) < 1e-4f
                                      && fabsf(((float *)m->u.bytes)[5] - 0.5f) < 1e-4f,
                                  "setTransform keeps the component scale");
                        }
                    }
                }
            }

            a[0].f = 2.0f; a[1].f = 2.0f; a[2].f = 2.0f;
            call(vm, grp, CLS("Group"), "scale", "(FFF)V", a, NULL);
            a[0].f = 90.0f; a[1].f = 0.0f; a[2].f = 0.0f; a[3].f = 1.0f;
            call(vm, grp, CLS("Group"), "postRotate", "(FFFF)V", a, NULL);
            check(!nj_has_exception(vm),
                  "a node accepts scale and postRotate without raising");
            nj_clear_exception(vm);

            a[0].i = 3;
            call(vm, grp, CLS("Group"), "setScope", "(I)V", a, NULL);
            call(vm, grp, CLS("Group"), "getScope", "()I", NULL, &r);
            check(r.i == 3, "Node.setScope and getScope agree");

            a[0].i = 1;
            call(vm, grp, CLS("Group"), "setPickingEnable", "(Z)V", a, NULL);
            call(vm, grp, CLS("Group"), "isPickingEnabled", "()Z", NULL, &r);
            check(r.i == 1, "setPickingEnable and isPickingEnabled agree");

            /*
             * setAlphaFactor() asked for _nj_alpha as an "I" while the class
             * declares it "F", so the lookup answered NULL and the setter set
             * nothing - silently, because a C write to a NULL field is a
             * no-op.  There is no getter to ask, so read the field.
             */
            {
                nj_field *af = nj_field_lookup(vm, CLS("Node"), "_nj_alpha", "F", 0);

                a[0].f = 0.25f;
                call(vm, grp, CLS("Node"), "setAlphaFactor", "(F)V", a, NULL);
                check(af != NULL
                          && nj_field_read_bits32(vm, grp, af) == 0x3E800000u,
                      "Node.setAlphaFactor stores the factor's bytes");
            }
        }

        if (cam && xf) {
            a[0].r = xf;
            call(vm, cam, CLS("Camera"), "setGeneric",
                 "(L" CLS("Transform") ";)V", a, NULL);
            a[0].r = xf;
            call(vm, cam, CLS("Camera"), "getProjection",
                 "(L" CLS("Transform") ";)I", a, &r);
            check(r.i == 1 && !nj_has_exception(vm),
                  "setGeneric makes getProjection report a matrix");
            nj_clear_exception(vm);
        }

        if (bg && img) {
            a[0].i = 0;
            call(vm, bg, CLS("Background"), "setColorClearEnable", "(Z)V", a, NULL);
            call(vm, bg, CLS("Background"), "isColorClearEnabled", "()Z", NULL, &r);
            check(r.i == 0, "setColorClearEnable and isColorClearEnabled agree");

            a[0].i = 1;
            call(vm, bg, CLS("Background"), "setDepthClearEnable", "(Z)V", a, NULL);
            call(vm, bg, CLS("Background"), "isDepthClearEnabled", "()Z", NULL, &r);
            check(r.i == 1, "setDepthClearEnable and isDepthClearEnabled agree");

            a[0].i = 1; a[1].i = 2; a[2].i = 3; a[3].i = 4;
            call(vm, bg, CLS("Background"), "setCrop", "(IIII)V", a, NULL);
            call(vm, bg, CLS("Background"), "getCropX", "()I", NULL, &r);
            check(r.i == 1, "setCrop and getCropX agree");
            call(vm, bg, CLS("Background"), "getCropHeight", "()I", NULL, &r);
            check(r.i == 4, "setCrop and getCropHeight agree");

            a[0].r = img;
            call(vm, bg, CLS("Background"), "setImage",
                 "(L" CLS("Image2D") ";)V", a, NULL);
            call(vm, bg, CLS("Background"), "getImage", "()L" CLS("Image2D") ";",
                 NULL, &r);
            check(r.r == img, "Background.setImage and getImage agree");
        }

        if (world && cam && bg) {
            a[0].r = cam;
            call(vm, world, CLS("World"), "setActiveCamera",
                 "(L" CLS("Camera") ";)V", a, NULL);
            call(vm, world, CLS("World"), "getActiveCamera",
                 "()L" CLS("Camera") ";", NULL, &r);
            check(r.r == cam, "World.setActiveCamera and getActiveCamera agree");

            a[0].r = bg;
            call(vm, world, CLS("World"), "setBackground",
                 "(L" CLS("Background") ";)V", a, NULL);
            call(vm, world, CLS("World"), "getBackground",
                 "()L" CLS("Background") ";", NULL, &r);
            check(r.r == bg, "World.setBackground and getBackground agree");
        }
    }

    /*
     * TriangleStripArray stores what its constructors are handed.  The
     * constructor is (int[] indices, int[] stripLengths) - the indices come
     * FIRST - and getting that order wrong made every vertex index a strip
     * length (which this test used to encode, because it asserted the
     * runtime's order rather than the specification's).
     */
    {
        nj_value a[4];
        nj_value r;
        nj_ref tsa = make(vm, CLS("TriangleStripArray"));
        nj_ref lens = nj_int_array_new(vm, 3);
        nj_ref idx = nj_int_array_new(vm, 9);

        check(tsa != 0, "TriangleStripArray can be constructed");
        if (tsa && lens && idx) {
            nj_s4 i;
            for (i = 0; i < 3; i++) {
                ((nj_s4 *)lens->u.bytes)[i] = 3;
            }
            a[0].r = idx; a[1].r = lens;
            call(vm, tsa, CLS("TriangleStripArray"), "<init>", "([I[I)V", a,
                 NULL);
            call(vm, tsa, CLS("TriangleStripArray"), "getStripCount", "()I",
                 NULL, &r);
            check(r.i == 3, "three strip lengths were kept");
            call(vm, tsa, CLS("TriangleStripArray"), "getIndexCount", "()I",
                 NULL, &r);
            check(r.i == 9, "the strip lengths add up to the index count");

            for (i = 0; i < 3; i++) {
                ((nj_s4 *)idx->u.bytes)[i] = 0;
            }
            a[0].r = idx;
            call(vm, tsa, CLS("TriangleStripArray"), "getStripLengths", "([I)V",
                 a, NULL);
            check(((nj_s4 *)idx->u.bytes)[1] == 3,
                  "getStripLengths writes the lengths back");
            check(((nj_s4 *)idx->u.bytes)[2] == 3,
                  "and all of them, not just the first");
        }
    }

    /* ---- Image2D(int, Object): the form 3D激爆拳击 builds textures with --- */
    {
        nj_ref i2 = make(vm, "javax/microedition/m3g/Image2D");
        nj_ref i3 = make(vm, "javax/microedition/m3g/Image2D");
        nj_ref img = 0;
        nj_ref bytes = nj_byte_array_new(vm, 16);
        nj_u4 roots_before;
        nj_value a2[2], ia[2], r;
        const nj_field *fw, *fh, *fp, *fi;

        ia[0].i = 8;
        ia[1].i = 4;
        if (nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                           "(II)Ljavax/microedition/lcdui/Image;", ia, &r) == NJ_OK) {
            img = r.r;
        }
        fw = nj_field_lookup(vm, "javax/microedition/m3g/Image2D", "_nj_w", "I", 0);
        fh = nj_field_lookup(vm, "javax/microedition/m3g/Image2D", "_nj_h", "I", 0);
        fp = nj_field_lookup(vm, "javax/microedition/m3g/Image2D", "_nj_pixels", "[B", 0);
        fi = nj_field_lookup(vm, "javax/microedition/m3g/Image2D", "_nj_image",
                             "Ljavax/microedition/lcdui/Image;", 0);

        roots_before = nj_root_mark(vm);
        a2[0].i = 100;
        a2[1].r = img;
        check(nj_call(vm, i2, "javax/microedition/m3g/Image2D", "<init>",
                      "(ILjava/lang/Object;)V", a2, &r) == NJ_OK && img != 0,
              "Image2D(format, Image) constructs");
        check(fw && fh && nj_field_read_int(vm, i2, fw) == 8 && nj_field_read_int(vm, i2, fh) == 4,
              "Image2D(format, Image) copies the size out of the image");
        check(fi && nj_field_read_ref(vm, i2, fi) == img,
              "Image2D(format, Image) keeps the image itself");
        /* The conversion allocates the RGBA byte array and roots it while it
         * writes; the root must go when the field holds the array.  A root
         * left behind pins that array for the life of the VM and makes every
         * snapshot of the VM refuse to save ("temp root held") - Stranded 2's
         * boot creates several of these Images and could not be snapshotted
         * until it was released. */
        check(nj_root_mark(vm) == roots_before,
              "Image2D(format, Image) leaves no ref root behind");

        a2[1].r = bytes;
        check(nj_call(vm, i3, "javax/microedition/m3g/Image2D", "<init>",
                      "(ILjava/lang/Object;)V", a2, &r) == NJ_OK && fp != 0 &&
              nj_field_read_ref(vm, i3, fp) == bytes,
              "Image2D(format, byte[]) keeps the pixel bytes");

        /* An object of neither shape must not throw: a title that hands over
         * something this runtime cannot hold should keep running. */
        a2[1].r = make(vm, "java/lang/Object");
        check(nj_call(vm, i3, "javax/microedition/m3g/Image2D", "<init>",
                      "(ILjava/lang/Object;)V", a2, &r) == NJ_OK,
              "Image2D(format, something else) does not throw");
    }

    /*
     * The constants the file format and the API fix, checked by what they are
     * FOR rather than by reading the static slot they live in.
     *
     * Reading the slot directly is what the first version of this test did,
     * and it segfaulted: a static field's address is resolved through the
     * class's layout, and a lookup on a class that has not been initialised
     * hands back an offset into a table that was never assigned.  The values
     * that matter are the ones a MIDlet carries inlined by javac, and the way
     * they reach us is as an argument, so this calls the setter with the value
     * JSR 184 defines and with the number that was there before the fix.
     */
    {
        nj_ref pm = make(vm, "javax/microedition/m3g/PolygonMode");
        /* four slots: an instance method's receiver plus its argument, with
           room to spare - a one-element array here is a heap overflow */
        nj_value a[4];
        memset(a, 0, sizeof(a));
        a[0].i = 168;           /* WINDING_CCW, per the specification */
        check(pm && nj_call(vm, pm, "javax/microedition/m3g/PolygonMode",
                            "setWinding", "(I)V", a, NULL) == NJ_OK,
              "setWinding(WINDING_CCW) is rejected");
        a[0].i = 16896;         /* what this runtime used to demand */
        check(pm && nj_call(vm, pm, "javax/microedition/m3g/PolygonMode",
                            "setWinding", "(I)V", a, NULL) != NJ_OK,
              "setWinding(16896) is accepted, so the constants are still ours");
    }
    /* ---- the file's Group children reach the Group --------------------- */
    {
        /*
         * The loader builds one Java object per file object and hands the
         * table to nj_m3g_link_groups().  That used to be a loop inside
         * Loader.load(), which no unit test can reach - it needs a real .m3g
         * inside a real MIDlet suite - so the state it takes is built here
         * instead: three Groups, the first naming the other two.
         */
        nj_m3g_object objs[3];
        nj_ref arr, g0, g1, g2;
        nj_value one, c;

        memset(objs, 0, sizeof(objs));
        objs[0].type = 9u;               /* Group, the file's type number */
        objs[0].fields.nrefs = 2u;
        objs[0].fields.nrefs_total = 2u; /* what the loader records */
        objs[0].fields.refs[0] = 2u;     /* index 1 is the file header */
        objs[0].fields.refs[1] = 3u;
        objs[1].type = 9u;
        objs[2].type = 9u;

        arr = nj_object_array_new(vm, CLS("Object3D"), 4u);
        check(arr != 0, "the loader's object array is built");
        g0 = make(vm, CLS("Group"));
        g1 = make(vm, CLS("Group"));
        g2 = make(vm, CLS("Group"));
        check(g0 && g1 && g2, "three groups exist");
        nj_array_set_ref(arr, 1u, g0);
        nj_array_set_ref(arr, 2u, g1);
        nj_array_set_ref(arr, 3u, g2);
        check(nj_m3g_link_groups(vm, objs, 3u, arr) == NJ_OK,
              "the linker accepts the table");

        call(vm, g0, CLS("Group"), "getChildCount", "()I", NULL, &c);
        check(c.i == 2, "the group has the two children the file names");
        one.i = 0;
        call(vm, g0, CLS("Group"), "getChild", "(I)L" CLS("Node") ";", &one, &c);
        check(c.r == g1, "getChild(0) is the first child the file names");
        one.i = 1;
        call(vm, g0, CLS("Group"), "getChild", "(I)L" CLS("Node") ";", &one, &c);
        check(c.r == g2, "getChild(1) is the second");

        /* The game adds a child the file already listed: it must be adopted,
           not added twice and not refused. */
        one.r = g1;
        call(vm, g0, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", &one, &c);
        call(vm, g0, CLS("Group"), "getChildCount", "()I", NULL, &c);
        check(c.i == 2, "adopting a child the file listed does not duplicate it");

        /* A node with a parent of its own is still refused.  The assertion is
           on the CALL FAILING, not on a value: "c.r == 0 || 1" is true of
           everything and would have been this file's third assertion that
           cannot fail. */
        one.r = g2;
        check(nj_call(vm, g1, CLS("Group"), "addChild", "(L" CLS("Node") ";)V",
                      &one, &c) != NJ_OK,
              "a node that already has a parent is still refused");
        nj_take_exception(vm);
    }
    /* ---- a Mesh is handed the buffers and appearances it names -------- */
    {
        /*
         * The numbers a Mesh carries are the ones every .m3g in the corpus
         * uses: two ahead of the object's place in the file.  That is not a
         * guess - reading the 2,606 Meshes of the 975 files the corpus ships
         * that way puts every vertex buffer on a VertexBuffer, every index
         * buffer on a TriangleStripArray and every appearance on an
         * Appearance, while the format's own one-based reading fits 629 of
         * them and a zero-based one 528.
         *
         * The table below is that shape: the Mesh is object 0 (slot 1) and it
         * names object 3 as 5, object 4 as 6 and object 5 as 7.
         */
        nj_m3g_object objs[6];
        nj_ref arr, mesh, vb, ib, app, other;
        nj_value a[4], r;

        memset(objs, 0, sizeof(objs));
        objs[0].type = 14u;                 /* Mesh */
        objs[0].fields.decoded = 1u;
        objs[0].fields.a = 5u;              /* vertex buffer: object 3 */
        objs[0].fields.b = 1u;              /* one submesh */
        objs[0].fields.nrefs_total = 2u;
        objs[0].fields.refs[0] = 6u;        /* index buffer: object 4 */
        objs[0].fields.refs[1] = 7u;        /* appearance: object 5 */
        objs[1].type = 21u;                 /* VertexBuffer */
        objs[2].type = 21u;                 /* another, never named */
        objs[3].type = 21u;                 /* VertexBuffer, the one named */
        objs[4].type = 11u;                 /* TriangleStripArray */
        objs[5].type = 3u;                  /* Appearance */

        arr = nj_object_array_new(vm, CLS("Object3D"), 7u);
        check(arr != 0, "the array a Mesh is linked into exists");
        mesh = make(vm, CLS("Mesh"));
        vb = make(vm, CLS("VertexBuffer"));
        ib = make(vm, CLS("TriangleStripArray"));
        app = make(vm, CLS("Appearance"));
        other = make(vm, CLS("Appearance"));
        check(mesh && vb && ib && app && other, "the mesh and its parts exist");
        nj_array_set_ref(arr, 1u, mesh);
        nj_array_set_ref(arr, 2u, make(vm, CLS("VertexBuffer")));
        nj_array_set_ref(arr, 3u, make(vm, CLS("VertexBuffer")));
        nj_array_set_ref(arr, 4u, vb);
        nj_array_set_ref(arr, 5u, ib);
        nj_array_set_ref(arr, 6u, app);

        check(nj_m3g_link_meshes(vm, objs, 6u, arr) == NJ_OK,
              "the mesh linker accepts the table");

        call(vm, mesh, CLS("Mesh"), "getVertexBuffer",
             "()L" CLS("VertexBuffer") ";", NULL, &r);
        check(r.r == vb, "the vertex buffer is object 3, named as 5");
        a[0].i = 0;
        call(vm, mesh, CLS("Mesh"), "getIndexBuffer",
             "(I)L" CLS("IndexBuffer") ";", a, &r);
        check(r.r == ib, "the index buffer is object 4, named as 6");
        call(vm, mesh, CLS("Mesh"), "getSubmeshCount", "()I", NULL, &r);
        check(r.i == 1, "the mesh has the one submesh the file names");
        call(vm, mesh, CLS("Mesh"), "getAppearance",
             "(I)L" CLS("Appearance") ";", a, &r);
        check(r.r == app, "the appearance is object 5, named as 7");

        /*
         * setAppearance(int, Appearance) wrote its two arguments into each
         * other's slots: the object went into the slot the index is read from
         * and the index into the object's, so the callee saw a null object and
         * an index made of a pointer.  Every game that patches a material does
         * this, and it threw ArrayIndexOutOfBoundsException instead.
         */
        a[0].i = 0;
        a[1].r = other;
        check(nj_call(vm, mesh, CLS("Mesh"), "setAppearance",
                      "(IL" CLS("Appearance") ";)V", a, NULL) == NJ_OK,
              "setAppearance(0, appearance) is accepted");
        a[0].i = 0;
        call(vm, mesh, CLS("Mesh"), "getAppearance",
             "(I)L" CLS("Appearance") ";", a, &r);
        check(r.r == other, "and it is the appearance that was set");
        {
            nj_ref ib2 = make(vm, CLS("TriangleStripArray"));
            nj_ref ibs = nj_object_array_new(vm, CLS("IndexBuffer"), 2u);
            nj_ref apps = nj_object_array_new(vm, CLS("Appearance"), 2u);
            nj_array_set_ref(ibs, 0u, ib);
            nj_array_set_ref(ibs, 1u, ib2);
            nj_array_set_ref(apps, 1u, app);
            a[0].r = vb; a[1].r = ibs; a[2].r = apps;
            call(vm, mesh, CLS("Mesh"), "<init>",
                 "(L" CLS("VertexBuffer") ";[L" CLS("IndexBuffer") ";[L" CLS("Appearance") ";)V", a, NULL);
            a[0].i = 1;
            call(vm, mesh, CLS("Mesh"), "getIndexBuffer", "(I)L" CLS("IndexBuffer") ";", a, &r);
            check(r.r == ib2, "the second constructor submesh keeps its own buffer");
            call(vm, mesh, CLS("Mesh"), "getAppearance", "(I)L" CLS("Appearance") ";", a, &r);
            check(r.r == app, "a null appearance does not shift the next slot");
            a[0].i = 0;
            call(vm, mesh, CLS("Mesh"), "getAppearance", "(I)L" CLS("Appearance") ";", a, &r);
            check(r.r == 0, "a null appearance stays null");
            objs[0].fields.b = 2u;
            objs[0].fields.nrefs_total = 4u;
            objs[0].fields.refs[1] = 0u;
            objs[0].fields.refs[2] = 6u;
            objs[0].fields.refs[3] = 7u;
            check(nj_m3g_link_meshes(vm, objs, 6u, arr) == NJ_OK, "two serialized submeshes link");
            a[0].i = 1;
            call(vm, mesh, CLS("Mesh"), "getIndexBuffer", "(I)L" CLS("IndexBuffer") ";", a, &r);
            check(r.r == ib, "the second serialized buffer is retained");
            call(vm, mesh, CLS("Mesh"), "getAppearance", "(I)L" CLS("Appearance") ";", a, &r);
            check(r.r == app, "serialized null appearances preserve later slots");
        }
    }
    /* ---- an Appearance is handed the material the file names ---------- */
    {
        /*
         * The loader decodes a whole file before any Java object exists, so
         * the two halves meet in the link step.  Without it an Appearance is
         * an empty box: the rasteriser finds no material and paints every
         * triangle white, which is exactly what the rally title looked like
         * before this step existed.
         *
         * The table is the shape the corpus uses: references count from two
         * (value - 2 is the object's place), so the Appearance is object 0 and
         * names the Material at object 1 as 3.
         */
        nj_m3g_object objs[3];
        nj_ref arr, app, mat, spare;
        nj_value a[2], r;

        memset(objs, 0, sizeof(objs));
        objs[0].type = 3u;                  /* Appearance */
        objs[0].fields.decoded = 1u;
        objs[0].fields.a = 1u;              /* layer */
        objs[0].fields.nrefs = 4u;
        objs[0].fields.refs[0] = 0u;        /* no compositing mode */
        objs[0].fields.refs[1] = 0u;        /* no fog */
        objs[0].fields.refs[2] = 0u;        /* no polygon mode */
        objs[0].fields.refs[3] = 3u;        /* material: object 1 */
        objs[1].type = 13u;                 /* Material */
        objs[1].fields.decoded = 1u;
        objs[1].fields.a = 0xFF204080u;     /* ambient */
        objs[1].fields.b = 0xFF3366CCu;     /* diffuse */
        objs[1].fields.c = 0xFF000000u;     /* emissive */
        objs[1].fields.d = 0xFFFFFFFFu;     /* specular */
        objs[1].fields.f[0] = 20.0f;        /* shininess */
        objs[2].type = 3u;                  /* an Appearance nobody names */

        arr = nj_object_array_new(vm, CLS("Object3D"), 3u);
        app = make(vm, CLS("Appearance"));
        mat = make(vm, CLS("Material"));
        spare = make(vm, CLS("Appearance"));
        check(arr != 0 && app != 0 && mat != 0 && spare != 0,
              "the appearance and its material exist");
        nj_array_set_ref(arr, 1u, app);
        nj_array_set_ref(arr, 2u, mat);
        nj_array_set_ref(arr, 3u, spare);

        check(nj_m3g_link_misc(vm, objs, 3u, arr) == NJ_OK,
              "the misc linker accepts the table");
        call(vm, app, CLS("Appearance"), "getMaterial",
             "()L" CLS("Material") ";", NULL, &r);
        check(r.r == mat, "the appearance is handed the material it names");
        /* The colours are read through the JSR's own targets: 1024 is the
           ambient one and 2048 the diffuse one. */
        a[0].i = 2048;
        call(vm, mat, CLS("Material"), "getColor", "(I)I", a, &r);
        check((nj_u4)r.i == 0xFF3366CCu, "the diffuse colour is the file's");
        a[0].i = 1024;
        call(vm, mat, CLS("Material"), "getColor", "(I)I", a, &r);
        check((nj_u4)r.i == 0xFF204080u, "the ambient colour is the file's");
        call(vm, mat, CLS("Material"), "getShininess", "()F", NULL, &r);
        check(r.f == 20.0f, "the shininess is the file's");
    }
    /* ---- render() is counted, because counting is all it can do --------- */
    {
        /*
         * The module has no rasteriser, so the one honest question is whether
         * a title ever reaches render() at all.  The corpus says they do, by
         * the hundred thousand: 极品飞车之无间风云FX中文版_4518 calls it
         * 495144 times in 30 frames.  That number is why the counter exists,
         * and this is what keeps it honest.
         */
        nj_vm_stats before, after;
        nj_ref w, g3d;
        nj_value one, r;

        nj_vm_get_stats(vm, &before);
        g3d = nj_object_new(vm, CLS("Graphics3D"));
        check(g3d != 0, "Graphics3D can be instantiated for the counter");
        /* The return slot is only read when the call succeeded.  The first
           version read it unconditionally and the test segfaulted inside
           bridge_lookup(): getInstance() fails in a test with no application
           loaded, so the slot held whatever the stack held. */
        r.r = 0;
        if (nj_call(vm, g3d, CLS("Graphics3D"), "getInstance",
                    "()L" CLS("Graphics3D") ";", NULL, &r) == NJ_OK && r.r) {
            g3d = r.r;
        }
        w = make(vm, CLS("World"));
        check(w != 0, "a World can be built for render()");
        one.r = w;
        call(vm, g3d, CLS("Graphics3D"), "render", "(L" CLS("World") ";)V", &one, NULL);
        nj_vm_get_stats(vm, &after);
        check(after.m3g_renders == before.m3g_renders + 1u,
              "render(World) is counted, since it cannot yet draw");
    }
    /* ---- the two halves of a rasteriser -------------------------------- */
    {
        /*
         * One half turns a vertex into a pixel, the other fills the triangle
         * three of them make.  The matrix is a plain scale by 2 in x and y, so
         * a vertex at (0.25, 0.25, 0) lands at three quarters of the way
         * across and a quarter of the way down - a value that can be worked
         * out by hand, which is the point.
         */
        float m[16] = { 2,0,0,0,  0,2,0,0,  0,0,2,0,  0,0,0,1 };
        float v[3] = { 0.25f, 0.25f, 0.0f };
        float xy[2] = { -1.0f, -1.0f };
        nj_u2 px[16 * 16];
        nj_u4 bg_inside, bg_outside;
        nj_surface s;
        nj_gfx g;
        float tri[6];
        int i2;

        nj_m3g_project(m, v, 16, 16, xy);
        check(xy[0] == 12.0f && xy[1] == 4.0f,
              "a vertex projects to the pixel the arithmetic says it should");

        memset(&s, 0, sizeof(s));
        for (i2 = 0; i2 < 16 * 16; i2++) { px[i2] = 0u; }
        s.pixels = px;
        s.width = 16; s.height = 16; s.stride = 16 * 2;
        s.format = NJ_PIXFMT_RGB565; s.bpp = 2;
        nj_surface_fill(&s, 0u);
        nj_gfx_init(&g, &s);
        bg_inside = nj_surface_get(&s, 3, 3);
        bg_outside = nj_surface_get(&s, 15, 15);

        tri[0] = 2.0f;  tri[1] = 2.0f;
        tri[2] = 13.0f; tri[3] = 2.0f;
        tri[4] = 2.0f;  tri[5] = 13.0f;
        check(nj_m3g_raster_tri(&g, tri, 0x00FF0000u) == 1u,
              "the rasteriser reports the triangle it filled");
        /* Both comparisons are against the background read BEFORE the fill.
           The first version compared the inside pixel against 0, which is
           true of every pixel that is not black - it passed with the drawing
           call deleted, which is how it was caught. */
        check(nj_surface_get(&s, 3, 3) != bg_inside,
              "and the pixels inside it are no longer the background");
        check(nj_surface_get(&s, 15, 15) == bg_outside,
              "while the pixels outside it are still the background");
    }
    /* VertexArray.set copies its input and preserves other vertices. */
    {
        nj_ref va = make(vm, CLS("VertexArray"));
        nj_ref src = nj_short_array_new(vm, 6u);
        nj_ref out = nj_short_array_new(vm, 6u);
        nj_ref bv = make(vm, CLS("VertexArray"));
        nj_ref bs = nj_byte_array_new(vm, 4u);
        nj_value a[3], r;
        nj_s2 *p = (nj_s2 *)nj_array_data(src);
        int i;

        for (i = 0; i < 6; i++) { p[i] = (nj_s2)(i + 1); }
        a[0].i = 3; a[1].i = 2; a[2].i = 2;
        call(vm, va, CLS("VertexArray"), "<init>", "(III)V", a, NULL);
        a[0].i = 0; a[1].i = 3; a[2].r = src;
        call(vm, va, CLS("VertexArray"), "set", "(II[S)V", a, NULL);
        p[0] = 99; p[1] = 88;
        a[0].i = 1; a[1].i = 1;
        call(vm, va, CLS("VertexArray"), "set", "(II[S)V", a, NULL);
        a[0].i = 0; a[1].i = 3; a[2].r = out;
        call(vm, va, CLS("VertexArray"), "get", "(II[S)V", a, NULL);
        p = (nj_s2 *)nj_array_data(out);
        check(p[0] == 1 && p[1] == 2, "set copies values before caller reuses input");
        check(p[2] == 99 && p[3] == 88 && p[4] == 5 && p[5] == 6,
              "partial short update preserves the remaining vertices");
        call(vm, va, CLS("VertexArray"), "getVertexCount", "()I", NULL, &r);
        check(r.i == 3, "partial update preserves vertex count");

        a[0].i = 2; a[1].i = 2; a[2].i = 1;
        call(vm, bv, CLS("VertexArray"), "<init>", "(III)V", a, NULL);
        ((nj_s1 *)nj_array_data(bs))[0] = -5;
        ((nj_s1 *)nj_array_data(bs))[1] = 7;
        ((nj_s1 *)nj_array_data(bs))[2] = 8;
        ((nj_s1 *)nj_array_data(bs))[3] = 9;
        a[0].i = 0; a[1].i = 2; a[2].r = bs;
        call(vm, bv, CLS("VertexArray"), "set", "(II[B)V", a, NULL);
        ((nj_s1 *)nj_array_data(bs))[0] = -11;
        ((nj_s1 *)nj_array_data(bs))[1] = 12;
        a[0].i = 1; a[1].i = 1;
        call(vm, bv, CLS("VertexArray"), "set", "(II[B)V", a, NULL);
        a[0].i = 0; a[1].i = 2; a[2].r = bs;
        call(vm, bv, CLS("VertexArray"), "get", "(II[B)V", a, NULL);
        check(((nj_s1 *)nj_array_data(bs))[0] == -5 &&
              ((nj_s1 *)nj_array_data(bs))[1] == 7 &&
              ((nj_s1 *)nj_array_data(bs))[2] == -11 &&
              ((nj_s1 *)nj_array_data(bs))[3] == 12,
              "partial byte update preserves vertices and signed components");
    }
    /* Texture mapping must not rescale geometry (Stranded2 terrain). */
    {
        nj_ref vb = make(vm, CLS("VertexBuffer"));
        nj_ref pb = nj_array_new(vm, "F", 3u);
        nj_ref tb = nj_array_new(vm, "F", 3u);
        nj_value a[4];
        a[0].r = NULL; a[1].f = 1.0f; a[2].r = pb;
        call(vm, vb, CLS("VertexBuffer"), "setPositions",
             "(L" CLS("VertexArray") ";F[F)V", a, NULL);
        a[0].i = 0; a[1].r = NULL; a[2].f = 1.0f / 256.0f; a[3].r = tb;
        call(vm, vb, CLS("VertexBuffer"), "setTexCoords",
             "(IL" CLS("VertexArray") ";F[F)V", a, NULL);
        check(nj_iget(vm, vb, nj_field_lookup(vm, CLS("VertexBuffer"),
                  "_nj_bias", "F", 0)) == 0x3f800000 &&
              nj_rget(vm, vb, nj_field_lookup(vm, CLS("VertexBuffer"),
                  "_nj_scale", "[F", 0)) == pb,
              "texture coordinates preserve position scale and bias");
        check(nj_iget(vm, vb, nj_field_lookup(vm, CLS("VertexBuffer"),
                  "_nj_texScale", "F", 0)) == 0x3b800000 &&
              nj_rget(vm, vb, nj_field_lookup(vm, CLS("VertexBuffer"),
                  "_nj_texBias", "[F", 0)) == tb,
              "texture coordinates retain their own scale and bias");
    }
    /* ---- a file's VertexArray bytes reach the Java object -------------- */
    {
        /*
         * The file keeps a VertexArray's geometry as bytes; the class the
         * titles call keeps it in a short[], the same place set() puts it.
         * This is the join between the two, and it is the geometry the
         * rasteriser will read.
         */
        nj_u1 bytes[12 + 3 + 6];
        nj_m3g_object objs[2];
        nj_ref arr, va, data;
        nj_value c;

        memset(objs, 0, sizeof(objs));
        memset(bytes, 0, sizeof(bytes));
        bytes[12] = 2u;    /* componentSize  */
        bytes[13] = 1u;    /* componentCount */
        bytes[14] = 2u;    /* componentType  */
        bytes[15] = 0x00; bytes[16] = 0x01;   /* 0x0100 */
        bytes[17] = 0x00; bytes[18] = 0x02;   /* 0x0200 */
        bytes[19] = 0x00; bytes[20] = 0x03;   /* 0x0300 */
        objs[0].type = 20u;
        objs[0].fields.a = 2u; objs[0].fields.b = 1u; objs[0].fields.c = 2u;
        objs[0].fields.data = bytes + 15;
        objs[0].fields.data_len = 6u;

        arr = nj_object_array_new(vm, CLS("Object3D"), 2u);
        va = make(vm, CLS("VertexArray"));
        check(va != 0, "a VertexArray exists for the loader to fill");
        nj_array_set_ref(arr, 1u, va);
        check(nj_m3g_load_geometry(vm, objs, 1u, arr) == NJ_OK,
              "the geometry loader accepts the table");

        call(vm, va, CLS("VertexArray"), "getVertexCount", "()I", NULL, &c);
        check(c.i == 3, "the file's three vertices are on the object");
        call(vm, va, CLS("VertexArray"), "getComponentCount", "()I", NULL, &c);
        check(c.i == 1, "and its component count");
        call(vm, va, CLS("VertexArray"), "getComponentSize", "()I", NULL, &c);
        check(c.i == 2, "and its component size");

        data = nj_rget(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_data", "[S", 0));
        /* Array length counts components, independent of their byte width. */
        check(data != 0 && nj_array_length(data) == 3u,
              "the short[] holds one value per vertex");
        if (data) {
            nj_s2 *sh = (nj_s2 *)nj_array_data(data);
            check(sh != NULL && sh[0] == (nj_s2)0x0100 && sh[1] == (nj_s2)0x0200 &&
                  sh[2] == (nj_s2)0x0300,
                  "and they are the numbers the file held, little-endian");
        }
        {
            const nj_u1 byte_delta[6] = {120, 10, 20, 30, 250, 246};
            const nj_u1 short_delta[12] = {248,127, 10,0, 20,0, 30,0, 250,255, 246,255};
            const nj_s2 expected[2][6] = {{120,10,-116,40,-122,30},
                                         {32760,10,-32756,40,-32762,30}};
            int size, j;
            for (size = 1; size <= 2; size++) {
                objs[0].fields.a = (nj_u4)size;
                objs[0].fields.b = 2u;
                objs[0].fields.c = 1u;
                objs[0].fields.data = size == 1 ? byte_delta : short_delta;
                objs[0].fields.data_len = 6u * (nj_u4)size;
                check(nj_m3g_load_geometry(vm, objs, 1u, arr) == NJ_OK,
                      "delta encoded vertex components load");
                data = nj_rget(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_data", "[S", 0));
                check(data && nj_array_length(data) == 6u,
                      "loaded array counts components rather than encoded bytes");
                for (j = 0; j < 6; j++) {
                    check(data && ((nj_s2 *)nj_array_data(data))[j] == expected[size - 1][j],
                          "vertex deltas accumulate per component with source-width overflow");
                }
            }
        }
    }
    /* ---- a Mesh reaches the screen ------------------------------------- */
    {
        /*
         * The whole chain in one assertion: a Mesh names a VertexBuffer, the
         * VertexBuffer names a VertexArray, and the VertexArray holds the
         * positions.  The matrix scales by 1/256 so that integer positions of
         * 128 land at 0.5 in clip space - pixel 12 of 16 across - and the
         * triangle is small enough to check pixel by pixel.
         */
        float m[16] = { 1.0f/256,0,0,0,  0,1.0f/256,0,0,  0,0,1.0f/256,0,  0,0,0,1 };
        nj_s2 pos[9];
        nj_u2 px[16 * 16];
        nj_surface s;
        nj_gfx g;
        nj_ref va, vb, mesh, data;
        nj_vm_stats before, after;
        nj_u4 bg;
        int i2;

        pos[0] = 0;   pos[1] = 0;   pos[2] = 0;
        pos[3] = 128; pos[4] = 0;   pos[5] = 0;
        pos[6] = 0;   pos[7] = 128; pos[8] = 0;

        va = make(vm, CLS("VertexArray"));
        vb = make(vm, CLS("VertexBuffer"));
        mesh = make(vm, CLS("Mesh"));
        check(va && vb && mesh, "a VertexArray, a VertexBuffer and a Mesh exist");
        data = nj_array_new(vm, "S", 9u);
        check(data != 0, "the positions array is built");
        if (data) {
            nj_s2 *d = (nj_s2 *)nj_array_data(data);
            int k;
            check(d != NULL, "the positions array has storage");
            for (k = 0; d && k < 9; k++) { d[k] = pos[k]; }
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_numVerts", "I", 0), 3);
            /* The JSR's constructor is (numVertices, numComponents,
               componentSize): three components of two bytes each.  This test
               used to write only _nj_compSize = 3 - the byte WIDTH of one
               component - and the reader treated it as a component count. */
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_numComp", "I", 0), 3);
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_compSize", "I", 0), 2);
            nj_rput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_data", "[S", 0), data);
        }
        nj_rput(vm, vb, nj_field_lookup(vm, CLS("VertexBuffer"), "_nj_positions",
                                        "L" CLS("VertexArray") ";", 0), va);
        nj_rput(vm, mesh, nj_field_lookup(vm, CLS("Mesh"), "_nj_vb",
                                          "L" CLS("VertexBuffer") ";", 0), vb);

        memset(&s, 0, sizeof(s));
        for (i2 = 0; i2 < 16 * 16; i2++) { px[i2] = 0u; }
        s.pixels = px; s.width = 16; s.height = 16; s.stride = 16 * 2;
        s.format = NJ_PIXFMT_RGB565; s.bpp = 2;
        nj_surface_fill(&s, 0u);
        nj_gfx_init(&g, &s);
        bg = nj_surface_get(&s, 8, 7);

        nj_vm_get_stats(vm, &before);
        check(nj_m3g_raster_mesh(vm, &g, mesh, m, 16, 16, 0x0000FF00u) == 1u,
              "the mesh's one triangle is drawn");
        nj_vm_get_stats(vm, &after);
        check(after.tris == before.tris + 1u, "and the triangle counter moves by one");
        check(nj_surface_get(&s, 8, 7) != bg,
              "and a pixel inside the triangle is no longer the background");
    }
    /* ---- the scene graph walk finds the meshes ------------------------- */
    {
        nj_ref w, grp, mesh, cam, found[4];
        nj_value a, r;

        w = make(vm, CLS("World"));
        grp = make(vm, CLS("Group"));
        mesh = make(vm, CLS("Mesh"));
        cam = make(vm, CLS("Camera"));
        check(w && grp && mesh && cam,
              "a World, a Group, a Mesh and a Camera exist");

        a.r = grp; r.r = 0;
        call(vm, w, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", &a, &r);
        a.r = mesh; r.r = 0;
        call(vm, grp, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", &a, &r);
        /*
         * A Camera is a Node and not a container.  The walk used to read
         * "_nj_kids" out of whatever it was handed, and a field looked up by
         * name is read at that offset in ANY object: a Camera in the graph
         * produced a garbage Vector pointer (0x1 in the wild) and the render
         * crashed inside the collection, which is why the title's 3D area
         * stayed empty instead of drawing.
         */
        a.r = cam; r.r = 0;
        call(vm, w, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", &a, &r);

        memset(found, 0, sizeof(found));
        check(nj_m3g_collect_meshes(vm, w, found, 4u) == 1u,
              "the walk finds the one mesh under the world");
        check(found[0] == mesh, "and it is the mesh that was added");
        check(nj_m3g_collect_meshes(vm, mesh, found, 4u) == 1u,
              "a mesh is itself a mesh");
        check(nj_m3g_collect_meshes(vm, cam, found, 4u) == 0u,
              "and a camera is not walked as a container");
    }
    test_camera_clip(vm);
    test_parallel_zero_planes(vm);
    /* ---- render() really draws the world into the bound target --------- */
    {
        nj_ref img = 0, gr, cam, xf, world, grp, mesh, va, vb, data, rgb, one, g3d;
        nj_value a[8], r;
        nj_s2 pos[9];
        nj_s4 vp[4];
        int k, lit = 0;

        pos[0] = -50;  pos[1] = -50;  pos[2] = -200;
        pos[3] =  50;  pos[4] = -50;  pos[5] = -200;
        pos[6] = -50;  pos[7] =  50;  pos[8] = -200;

        g3d = nj_object_new(vm, CLS("Graphics3D"));
        check(g3d != 0, "a Graphics3D exists to render with");
        {
            nj_value ia[2];
            ia[0].i = 16; ia[1].i = 16; r.r = 0;
            if (nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                               "(II)Ljavax/microedition/lcdui/Image;", ia, &r) == NJ_OK) {
                img = r.r;
            }
        }
        check(img != 0, "a mutable Image exists to draw into");
        a[0].r = img; r.r = 0;
        call(vm, img, "javax/microedition/lcdui/Image", "getGraphics", "()Ljavax/microedition/lcdui/Graphics;", a, &r);
        gr = r.r;
        check(gr != 0, "and it hands out a Graphics");

        cam = make(vm, CLS("Camera"));
        xf  = make(vm, CLS("Transform"));
        check(cam && xf, "a Camera and an identity Transform exist");
        a[0].f = 60.0f; a[1].f = 1.0f; a[2].f = 1.0f; a[3].f = 1000.0f;
        call(vm, cam, CLS("Camera"), "setPerspective", "(FFFF)V", a, &r);

        world = make(vm, CLS("World"));
        grp   = make(vm, CLS("Group"));
        mesh  = make(vm, CLS("Mesh"));
        va    = make(vm, CLS("VertexArray"));
        vb    = make(vm, CLS("VertexBuffer"));
        check(world && grp && mesh && va && vb, "a World, Group, Mesh and buffers exist");
        data = nj_array_new(vm, "S", 9u);
        if (data) {
            nj_s2 *d = (nj_s2 *)nj_array_data(data);
            for (k = 0; d && k < 9; k++) { d[k] = pos[k]; }
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_numVerts", "I", 0), 3);
            /* The JSR's constructor is (numVertices, numComponents,
               componentSize): three components of two bytes each.  This test
               used to write only _nj_compSize = 3 - the byte WIDTH of one
               component - and the reader treated it as a component count. */
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_numComp", "I", 0), 3);
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_compSize", "I", 0), 2);
            nj_rput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_data", "[S", 0), data);
        }
        nj_rput(vm, vb, nj_field_lookup(vm, CLS("VertexBuffer"), "_nj_positions",
                                        "L" CLS("VertexArray") ";", 0), va);
        nj_rput(vm, mesh, nj_field_lookup(vm, CLS("Mesh"), "_nj_vb",
                                          "L" CLS("VertexBuffer") ";", 0), vb);
        a[0].r = grp;  r.r = 0;
        call(vm, world, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", a, &r);
        a[0].r = mesh; r.r = 0;
        call(vm, grp, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", a, &r);

        a[0].r = gr; r.r = 0;
        call(vm, g3d, CLS("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V", a, &r);
        vp[0] = 0; vp[1] = 0; vp[2] = 16; vp[3] = 16;
        a[0].i = vp[0]; a[1].i = vp[1]; a[2].i = vp[2]; a[3].i = vp[3];
        call(vm, g3d, CLS("Graphics3D"), "setViewport", "(IIII)V", a, &r);
        a[0].r = cam; a[1].r = xf; r.r = 0;
        call(vm, g3d, CLS("Graphics3D"), "setCamera",
             "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, &r);

        /* setPerspective used to write into a field the lookup never found,
           so the camera kept no projection at all. */
        {
            nj_ref xf2 = make(vm, CLS("Transform"));
            nj_value pa[2];
            pa[0].r = xf2; r.i = 0;
            call(vm, cam, CLS("Camera"), "getProjection",
                 "(L" CLS("Transform") ";)I", pa, &r);
            check(r.i == 1, "the camera reports a projection after setPerspective");
            {
                nj_ref marr = nj_rget(vm, xf2, nj_field_lookup(vm, CLS("Transform"),
                                                               "_nj_m", "[F", 0));
                const float *mp = marr ? (const float *)nj_array_data(marr) : NULL;
                check(mp != NULL && mp[0] != 0.0f && mp[5] != 0.0f,
                      "and the projection is not all zeros");
            }
        }
        a[0].r = world; r.r = 0;
        call(vm, g3d, CLS("Graphics3D"), "render", "(L" CLS("World") ";)V", a, &r);

        rgb = nj_array_new(vm, "I", 256u);
        one = rgb;
        check(one != 0, "an int[] exists to read the picture back");
        if (one) {
            a[0].r = one; a[1].i = 0; a[2].i = 16; a[3].i = 0;
            a[4].i = 0; a[5].i = 16; a[6].i = 16; a[7].i = 0; r.r = 0;
            call(vm, img, "javax/microedition/lcdui/Image", "getRGB",
                 "([IIIIIII)V", a, &r);
            {
                const nj_s4 *px = (const nj_s4 *)nj_array_data(one);
                for (k = 0; px && k < 256; k++) { if (px[k] != 0) { lit++; } }
            }
        }
        check(lit > 0, "render() put pixels into the target image");

        /* Unlit colours use the buffer default or RGB(A) vertex order. */
        {
            nj_ref colours = make(vm, CLS("VertexArray"));
            nj_ref channels = nj_array_new(vm, "S", 12u);
            nj_ref pixels = nj_rget(vm, img, nj_field_lookup(vm,
                "javax/microedition/lcdui/Image", "imgPixels", "[I", 0));
            const nj_u4 expected[3] = { 0x2468ACu, 0x3579BDu, 0x468ACEu };
            nj_ref colour_app = make(vm, CLS("Appearance"));
            nj_ref colour_bg = make(vm, CLS("Background"));
            nj_ref indices = nj_object_new(vm, CLS("TriangleStripArray"));
            nj_ref strips = nj_array_new(vm, "I", 1u);
            int mode;
            ((nj_s4 *)nj_array_data(strips))[0] = 3;
            a[0].i = 0; a[1].r = strips;
            check(nj_call_special(vm, indices, CLS("TriangleStripArray"), "<init>",
                  "(I[I)V", a, &r) == NJ_OK, "colour triangle constructs");
            a[0].r = vb; a[1].r = indices; a[2].r = colour_app;
            check(nj_call_special(vm, mesh, CLS("Mesh"), "<init>",
                  "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                  CLS("Appearance") ";)V", a, &r) == NJ_OK, "colour mesh constructs");
            check(colours && channels && pixels, "vertex colour fixtures exist");
            if (colours && channels && pixels) {
                for (mode = 0; mode < 3; mode++) {
                    int components = mode == 1 ? 3 : 4;
                    nj_s2 *ch = (nj_s2 *)nj_array_data(channels);
                    for (k = 0; k < 3; k++) {
                        ch[k * components] = (nj_s2)(expected[mode] >> 16);
                        ch[k * components + 1] = (nj_s2)((expected[mode] >> 8) & 255u);
                        ch[k * components + 2] = (nj_s2)(expected[mode] & 255u);
                        if (components == 4) { ch[k * components + 3] = 255; }
                    }
                    nj_iput(vm, colours, nj_field_lookup(vm, CLS("VertexArray"),
                        "_nj_numVerts", "I", 0), 3);
                    nj_iput(vm, colours, nj_field_lookup(vm, CLS("VertexArray"),
                        "_nj_numComp", "I", 0), components);
                    nj_rput(vm, colours, nj_field_lookup(vm, CLS("VertexArray"),
                        "_nj_data", "[S", 0), channels);
                    a[0].i = (nj_s4)(0xFF000000u | expected[0]);
                    call(vm, vb, CLS("VertexBuffer"), "setDefaultColor", "(I)V", a, NULL);
                    a[0].r = mode ? colours : NULL;
                    call(vm, vb, CLS("VertexBuffer"), "setColors",
                         "(L" CLS("VertexArray") ";)V", a, NULL);
                    a[0].r = colour_bg;
                    call(vm, g3d, CLS("Graphics3D"), "clear",
                         "(L" CLS("Background") ";)V", a, NULL);
                    a[0].r = cam; a[1].r = xf;
                    call(vm, g3d, CLS("Graphics3D"), "setCamera",
                         "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);
                    a[0].r = mesh; a[1].r = xf;
                    call(vm, g3d, CLS("Graphics3D"), "render",
                         "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                    check((((nj_u4 *)nj_array_data(pixels))[9 * 16 + 6] & 0xFFFFFFu)
                          == expected[mode], mode == 0 ? "unlit mesh uses default colour" :
                          mode == 1 ? "RGB vertex colours override the default" :
                                      "RGBA vertex colours preserve RGB channel order");
                }
                a[0].r = NULL;
                call(vm, vb, CLS("VertexBuffer"), "setColors",
                     "(L" CLS("VertexArray") ";)V", a, NULL);
                a[0].i = (nj_s4)0xFFFFFFFFu;
                call(vm, vb, CLS("VertexBuffer"), "setDefaultColor", "(I)V", a, NULL);
            }
        }

        /*
         * The other way a title names the camera: World.setActiveCamera().
         * The corpus uses this one and never calls Graphics3D.setCamera() at
         * all, so with only the latter read every render found no camera, took
         * the "no projection" exit and drew nothing - the black 3D area the
         * user reported.
         */
        {
            nj_ref g2 = nj_object_new(vm, CLS("Graphics3D"));
            int lit2 = 0;

            check(g2 != 0, "a second Graphics3D exists");
            a[0].r = gr; r.r = 0;
            call(vm, g2, CLS("Graphics3D"), "bindTarget",
                 "(Ljava/lang/Object;)V", a, &r);
            a[0].i = 0; a[1].i = 0; a[2].i = 16; a[3].i = 16;
            call(vm, g2, CLS("Graphics3D"), "setViewport", "(IIII)V", a, &r);
            /* The World carries the camera, and its active camera must win
               over a setCamera() Transform: The Run sets both and re-places
               the camera node every frame, so reading the stale Transform
               from the last menu left the race world outside the view. */
            a[0].r = cam; r.r = 0;
            call(vm, world, CLS("World"), "setActiveCamera",
                 "(L" CLS("Camera") ";)V", a, &r);
            {
                nj_ref far_xf = make(vm, CLS("Transform"));
                nj_value fa[3];
                fa[0].f = 1000.0f; fa[1].f = 0.0f; fa[2].f = 0.0f;
                call(vm, far_xf, CLS("Transform"), "setIdentity", "()V", NULL, NULL);
                call(vm, far_xf, CLS("Transform"), "postTranslate",
                     "(FFF)V", fa, NULL);
                a[0].r = cam; a[1].r = far_xf; r.r = 0;
                call(vm, g2, CLS("Graphics3D"), "setCamera",
                     "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, &r);
            }

            /* Start from a blank target: the picture this render writes must
               be its own, not pixels left by the render above. */
            {
                nj_ref pix = nj_rget(vm, img, nj_field_lookup(vm,
                    "javax/microedition/lcdui/Image", "imgPixels", "[I", 0));
                if (pix) { memset(nj_array_data(pix), 0, 16u * 16u * sizeof(nj_u4)); }
            }
            a[0].r = world; r.r = 0;
            call(vm, g2, CLS("Graphics3D"), "render", "(L" CLS("World") ";)V",
                 a, &r);
            if (one) {
                const nj_s4 *px;

                a[0].r = one; a[1].i = 0; a[2].i = 16; a[3].i = 0;
                a[4].i = 0; a[5].i = 16; a[6].i = 16; a[7].i = 0; r.r = 0;
                call(vm, img, "javax/microedition/lcdui/Image", "getRGB",
                     "([IIIIIII)V", a, &r);
                px = (const nj_s4 *)nj_array_data(one);
                for (k = 0; px && k < 256; k++) { if (px[k] != 0) { lit2++; } }
            }
            check(lit2 > 0, "a World's active camera draws it even after a stale setCamera()");
        }
        /* Near red and far green cover the same pixels. Turning off either
           the first draw's depth write or the second draw's depth test must
           let the far surface replace the near one. */
        {
            nj_ref app = make(vm, CLS("Appearance"));
            nj_ref cm = make(vm, CLS("CompositingMode"));
            nj_ref material = make(vm, CLS("Material"));
            nj_ref clear_bg = make(vm, CLS("Background"));
            nj_ref ib = nj_object_new(vm, CLS("TriangleStripArray"));
            nj_ref lengths = nj_array_new(vm, "I", 1u);
            nj_ref pixels = nj_rget(vm, img, nj_field_lookup(vm,
                "javax/microedition/lcdui/Image", "imgPixels", "[I", 0));
            int write, test;
            a[0].r = cam; a[1].r = xf;
            call(vm, g3d, CLS("Graphics3D"), "setCamera",
                 "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);
            ((nj_s4 *)nj_array_data(lengths))[0] = 3;
            a[0].i = 0; a[1].r = lengths;
            check(nj_call_special(vm, ib, CLS("TriangleStripArray"), "<init>",
                  "(I[I)V", a, &r) == NJ_OK, "depth regression triangle constructs");
            a[0].r = cm;
            call(vm, app, CLS("Appearance"), "setCompositingMode",
                 "(L" CLS("CompositingMode") ";)V", a, NULL);
            a[0].r = material;
            call(vm, app, CLS("Appearance"), "setMaterial",
                 "(L" CLS("Material") ";)V", a, NULL);
            a[0].r = vb; a[1].r = ib; a[2].r = app;
            check(nj_call_special(vm, mesh, CLS("Mesh"), "<init>",
                  "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                  CLS("Appearance") ";)V", a, &r) == NJ_OK,
                  "depth regression mesh constructs");
            for (write = 0; write <= 1; write++) {
                for (test = 0; test <= 1; test++) {
                    nj_s2 *d = (nj_s2 *)nj_array_data(data);
                    const nj_u4 *p;
                    a[0].r = clear_bg;
                    call(vm, g3d, CLS("Graphics3D"), "clear",
                         "(L" CLS("Background") ";)V", a, NULL);
                    a[0].i = 1;
                    call(vm, cm, CLS("CompositingMode"), "setDepthTestEnable", "(Z)V", a, NULL);
                    a[0].i = write;
                    call(vm, cm, CLS("CompositingMode"), "setDepthWriteEnable", "(Z)V", a, NULL);
                    for (k = 0; k < 9; k++) { d[k] = pos[k] / 2; }
                    a[0].i = 2048; a[1].i = (nj_s4)0xFFFF0000u;
                    call(vm, material, CLS("Material"), "setColor", "(II)V", a, NULL);
                    a[0].r = mesh; a[1].r = xf;
                    call(vm, g3d, CLS("Graphics3D"), "render",
                         "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                    p = (const nj_u4 *)nj_array_data(pixels);
                    check((p[9 * 16 + 6] & 0xFFFFFFu) == 0xFF0000u,
                          "near surface covers the overlap pixel");
                    a[0].i = test;
                    call(vm, cm, CLS("CompositingMode"), "setDepthTestEnable", "(Z)V", a, NULL);
                    a[0].i = 1;
                    call(vm, cm, CLS("CompositingMode"), "setDepthWriteEnable", "(Z)V", a, NULL);
                    for (k = 0; k < 9; k++) { d[k] = pos[k]; }
                    a[0].i = 2048; a[1].i = (nj_s4)0xFF00FF00u;
                    call(vm, material, CLS("Material"), "setColor", "(II)V", a, NULL);
                    a[0].r = mesh; a[1].r = xf;
                    call(vm, g3d, CLS("Graphics3D"), "render",
                         "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                    p = (const nj_u4 *)nj_array_data(pixels);
                    check((p[9 * 16 + 6] & 0xFFFFFFu) ==
                          (write && test ? 0xFF0000u : 0x00FF00u),
                          "depth test and depth write independently control occlusion");
                }
            }
            /* Coplanar opaque triangles keep the first colour until depth
               testing is disabled, for both ALPHA and REPLACE compositing. */
            {
                const int blends[2] = {64, 68};
                int blend;
                for (blend = 0; blend < 2; blend++) {
                    const nj_u4 *p = (const nj_u4 *)nj_array_data(pixels);
                    a[0].i = blends[blend];
                    call(vm, cm, CLS("CompositingMode"), "setBlending", "(I)V", a, NULL);
                    a[0].r = clear_bg;
                    call(vm, g3d, CLS("Graphics3D"), "clear",
                         "(L" CLS("Background") ";)V", a, NULL);
                    a[0].i = 2048; a[1].i = (nj_s4)0xFFFF0000u;
                    call(vm, material, CLS("Material"), "setColor", "(II)V", a, NULL);
                    a[0].r = mesh; a[1].r = xf;
                    call(vm, g3d, CLS("Graphics3D"), "render",
                         "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                    check(p[9 * 16 + 6] == 0xFFFF0000u,
                          "opaque flat triangle writes red and full alpha");
                    a[0].i = 2048; a[1].i = (nj_s4)0xFF00FF00u;
                    call(vm, material, CLS("Material"), "setColor", "(II)V", a, NULL);
                    a[0].r = mesh; a[1].r = xf;
                    call(vm, g3d, CLS("Graphics3D"), "render",
                         "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                    check(p[9 * 16 + 6] == 0xFFFF0000u,
                          "equal depth keeps the first opaque flat triangle");
                    a[0].i = 0;
                    call(vm, cm, CLS("CompositingMode"), "setDepthTestEnable", "(Z)V", a, NULL);
                    a[0].r = mesh; a[1].r = xf;
                    call(vm, g3d, CLS("Graphics3D"), "render",
                         "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                    check(p[9 * 16 + 6] == 0xFF00FF00u,
                          "disabled depth test lets the coplanar green triangle replace red");
                    a[0].i = 1;
                    call(vm, cm, CLS("CompositingMode"), "setDepthTestEnable", "(Z)V", a, NULL);
                }
                a[0].i = 64;
                call(vm, cm, CLS("CompositingMode"), "setBlending", "(I)V", a, NULL);
            }
            /* Moving stored coordinates away and restoring them with the
               API's three-component bias must preserve the rendered image. */
            {
                nj_ref bias = nj_array_new(vm, "F", 3u);
                nj_u4 expected[256];
                nj_s2 *d = (nj_s2 *)nj_array_data(data);
                float *b = (float *)nj_array_data(bias);
                a[0].r = clear_bg;
                call(vm, g3d, CLS("Graphics3D"), "clear",
                     "(L" CLS("Background") ";)V", a, NULL);
                a[0].r = mesh; a[1].r = xf;
                call(vm, g3d, CLS("Graphics3D"), "render",
                     "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                memcpy(expected, nj_array_data(pixels), sizeof(expected));
                check((expected[9 * 16 + 6] & 0xFFFFFFu) == 0x00FF00u,
                      "position bias baseline draws the green triangle");
                b[0] = 2000.0f; b[1] = -1000.0f; b[2] = -300.0f;
                for (k = 0; k < 9; k++) { d[k] = pos[k] - (nj_s2)b[k % 3]; }
                a[0].r = va; a[1].f = 1.0f; a[2].r = bias;
                check(nj_call(vm, vb, CLS("VertexBuffer"), "setPositions",
                      "(L" CLS("VertexArray") ";F[F)V", a, NULL) == NJ_OK,
                      "position bias accepts three components");
                a[0].r = clear_bg;
                call(vm, g3d, CLS("Graphics3D"), "clear",
                     "(L" CLS("Background") ";)V", a, NULL);
                a[0].r = mesh; a[1].r = xf;
                call(vm, g3d, CLS("Graphics3D"), "render",
                     "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                check(memcmp(expected, nj_array_data(pixels), sizeof(expected)) == 0,
                      "render applies all three position bias components");
            }
            /* Out-of-range UVs distinguish the public CLAMP enum (240) from
               REPEAT (241), independently on S and T. */
            {
                nj_ref uv = nj_object_new(vm, CLS("VertexArray"));
                nj_ref coords = nj_array_new(vm, "S", 6u);
                nj_ref rgb = nj_array_new(vm, "B", 12u);
                nj_ref image = nj_object_new(vm, CLS("Image2D"));
                nj_ref texture = nj_object_new(vm, CLS("Texture2D"));
                const nj_u1 colors[12] = {255,0,0, 0,255,0,
                                         0,0,255, 255,255,255};
                const nj_s2 cases[4][2] = {{-1,0},{2,0},{0,-1},{0,2}};
                const nj_u4 expected[4][2] = {{0xFF0000,0x00FF00},
                    {0x00FF00,0xFF0000},{0xFF0000,0x0000FF},
                    {0x0000FF,0xFF0000}};
                int c, mode, vertex;
                memcpy(nj_array_data(rgb), colors, sizeof(colors));
                a[0].i = 3; a[1].i = 2; a[2].i = 2;
                check(nj_call_special(vm, uv, CLS("VertexArray"), "<init>",
                      "(III)V", a, NULL) == NJ_OK, "wrapping UV array constructs");
                a[0].i = 99; a[1].i = 2; a[2].i = 2; a[3].r = rgb;
                check(nj_call_special(vm, image, CLS("Image2D"), "<init>",
                      "(III[B)V", a, NULL) == NJ_OK, "wrapping texture image constructs");
                a[0].r = image;
                call(vm, texture, CLS("Texture2D"), "<init>",
                     "(L" CLS("Image2D") ";)V", a, NULL);
                a[0].i = 228;
                call(vm, texture, CLS("Texture2D"), "setBlending", "(I)V", a, NULL);
                a[0].i = 208; a[1].i = 208;
                call(vm, texture, CLS("Texture2D"), "setFiltering", "(II)V", a, NULL);
                a[0].i = 0; a[1].r = texture;
                call(vm, app, CLS("Appearance"), "setTexture",
                     "(IL" CLS("Texture2D") ";)V", a, NULL);
                a[0].i = 0; a[1].r = uv; a[2].f = 0.5f; a[3].r = 0;
                call(vm, vb, CLS("VertexBuffer"), "setTexCoords",
                     "(IL" CLS("VertexArray") ";F[F)V", a, NULL);
                for (c = 0; c < 4; c++) {
                    nj_s2 *values = (nj_s2 *)nj_array_data(coords);
                    for (vertex = 0; vertex < 3; vertex++) {
                        values[vertex * 2] = cases[c][0];
                        values[vertex * 2 + 1] = cases[c][1];
                    }
                    a[0].i = 0; a[1].i = 3; a[2].r = coords;
                    call(vm, uv, CLS("VertexArray"), "set", "(II[S)V", a, NULL);
                    for (mode = 0; mode < 2; mode++) {
                        a[0].i = 240 + mode; a[1].i = 240 + mode;
                        call(vm, texture, CLS("Texture2D"), "setWrapping", "(II)V", a, NULL);
                        a[0].r = clear_bg;
                        call(vm, g3d, CLS("Graphics3D"), "clear",
                             "(L" CLS("Background") ";)V", a, NULL);
                        a[0].r = mesh; a[1].r = xf;
                        call(vm, g3d, CLS("Graphics3D"), "render",
                             "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                        check((((nj_u4 *)nj_array_data(pixels))[9 * 16 + 6] & 0xFFFFFFu)
                              == expected[c][mode], "texture wrapping clamps or repeats each axis");
                    }
                }
                /* Two-component UV arrays accept a two-float bias. */
                {
                    nj_ref bias = nj_array_new(vm, "F", 2u);
                    const float offsets[3][2] = {{0.75f,0.75f},{0.0f,0.75f},{0.75f,0.0f}};
                    const nj_u4 colors_with_bias[3] = {0xFFFFFFu,0x0000FFu,0x00FF00u};
                    memset(nj_array_data(coords), 0, 6u * sizeof(nj_s2));
                    a[0].i = 0; a[1].i = 3; a[2].r = coords;
                    call(vm, uv, CLS("VertexArray"), "set", "(II[S)V", a, NULL);
                    a[0].i = 240; a[1].i = 240;
                    call(vm, texture, CLS("Texture2D"), "setWrapping", "(II)V", a, NULL);
                    for (c = 0; c < 3; c++) {
                        memcpy(nj_array_data(bias), offsets[c], sizeof(offsets[c]));
                        a[0].i = 0; a[1].r = uv; a[2].f = 0.5f; a[3].r = bias;
                        call(vm, vb, CLS("VertexBuffer"), "setTexCoords",
                             "(IL" CLS("VertexArray") ";F[F)V", a, NULL);
                        a[0].r = clear_bg;
                        call(vm, g3d, CLS("Graphics3D"), "clear",
                             "(L" CLS("Background") ";)V", a, NULL);
                        a[0].r = mesh; a[1].r = xf;
                        call(vm, g3d, CLS("Graphics3D"), "render",
                             "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                        check((((nj_u4 *)nj_array_data(pixels))[9 * 16 + 6] & 0xFFFFFFu)
                              == colors_with_bias[c], "two-component texture bias offsets each axis");
                    }
                }
                /* LINEAR samples are explicit colours, not a second copy of
                   the sampler. The 2x2 corners are red, green / blue, white.
                   At (1/2,1/2), each RGB channel averages to 127.5 (127 after
                   truncation). At (1/4,3/4), the corner shares are 3,1,9,3
                   sixteenths: RGB=(95,63,191). RGBA corner alphas 0,64,128,240
                   average to 108 and 121 at those two points respectively.
                   Constant UVs keep these checks independent of interpolation
                   across the triangle; REPLACE exposes the sampled alpha. */
                {
                    const nj_u1 rgba_colors[16] = {255,0,0,0, 0,255,0,64,
                                                  0,0,255,128, 255,255,255,240};
                    const int filters[2] = {129, 209};
                    const struct {
                        nj_s2 s, t; /* quarter units */
                        int wrap_s, wrap_t;
                        nj_u4 rgb, rgba;
                        const char *name;
                    } samples[] = {
                        { 2, 2,240,240,0xFF7F7F7Fu,0x6C7F7F7Fu,"centre average"},
                        { 1, 3,240,240,0xFF5F3FBFu,0x795F3FBFu,"unequal corner shares"},
                        {-1, 3,240,240,0xFF3F00BFu,0x603F00BFu,"clamped left edge"},
                        { 5, 1,240,240,0xFF3FFF3Fu,0x6C3FFF3Fu,"clamped right edge"},
                        { 1,-2,240,240,0xFFBF3F00u,0x10BF3F00u,"clamped top edge"},
                        { 3, 5,240,240,0xFFBFBFFFu,0xD4BFBFFFu,"clamped bottom edge"},
                        {-1,-1,240,240,0xFFFF0000u,0x00FF0000u,"clamped red corner"},
                        { 5, 5,240,240,0xFFFFFFFFu,0xF0FFFFFFu,"clamped white corner"},
                        {-3, 3,241,240,0xFF5F3FBFu,0x795F3FBFu,"negative repeat S"},
                        { 1,-1,240,241,0xFF5F3FBFu,0x795F3FBFu,"negative repeat T"},
                        {-7,-5,241,241,0xFF5F3FBFu,0x795F3FBFu,"negative repeat both axes"},
                        {-4,-4,241,241,0xFFFF0000u,0x00FF0000u,"negative whole repeat"},
                        {-1,-1,240,241,0xFF3F00BFu,0x603F00BFu,"clamp S and repeat T"},
                        {-1, 5,241,240,0xFFBFBFFFu,0xD4BFBFFFu,"repeat S and clamp T"}
                    };
                    nj_ref rgba = nj_array_new(vm, "B", sizeof(rgba_colors));
                    nj_ref rgba_image = nj_object_new(vm, CLS("Image2D"));
                    int format, filter;
                    size_t sample;
                    memcpy(nj_array_data(rgba), rgba_colors, sizeof(rgba_colors));
                    a[0].i = 100; a[1].i = 2; a[2].i = 2; a[3].r = rgba;
                    check(nj_call_special(vm, rgba_image, CLS("Image2D"), "<init>",
                          "(III[B)V", a, NULL) == NJ_OK, "linear RGBA texture image constructs");
                    a[0].i = 68;
                    call(vm, cm, CLS("CompositingMode"), "setBlending", "(I)V", a, NULL);
                    a[0].i = 1;
                    call(vm, cm, CLS("CompositingMode"), "setAlphaWriteEnable", "(Z)V", a, NULL);
                    a[0].f = 0.0f;
                    call(vm, cm, CLS("CompositingMode"), "setAlphaThreshold", "(F)V", a, NULL);
                    a[0].i = 0; a[1].r = uv; a[2].f = 0.25f; a[3].r = 0;
                    call(vm, vb, CLS("VertexBuffer"), "setTexCoords",
                         "(IL" CLS("VertexArray") ";F[F)V", a, NULL);
                    for (format = 0; format < 2; format++) {
                        a[0].r = format ? rgba_image : image;
                        call(vm, texture, CLS("Texture2D"), "setImage",
                             "(L" CLS("Image2D") ";)V", a, NULL);
                        for (filter = 0; filter < 2; filter++) {
                            a[0].i = 208; a[1].i = filters[filter];
                            call(vm, texture, CLS("Texture2D"), "setFiltering", "(II)V", a, NULL);
                            for (sample = 0; sample < sizeof(samples) / sizeof(samples[0]); sample++) {
                                nj_s2 *values = (nj_s2 *)nj_array_data(coords);
                                nj_u4 want = format ? samples[sample].rgba : samples[sample].rgb;
                                nj_u4 got;
                                char label[160];
                                for (vertex = 0; vertex < 3; vertex++) {
                                    values[vertex * 2] = samples[sample].s;
                                    values[vertex * 2 + 1] = samples[sample].t;
                                }
                                a[0].i = 0; a[1].i = 3; a[2].r = coords;
                                call(vm, uv, CLS("VertexArray"), "set", "(II[S)V", a, NULL);
                                a[0].i = samples[sample].wrap_s; a[1].i = samples[sample].wrap_t;
                                call(vm, texture, CLS("Texture2D"), "setWrapping", "(II)V", a, NULL);
                                a[0].r = clear_bg;
                                call(vm, g3d, CLS("Graphics3D"), "clear",
                                     "(L" CLS("Background") ";)V", a, NULL);
                                a[0].r = mesh; a[1].r = xf;
                                call(vm, g3d, CLS("Graphics3D"), "render",
                                     "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
                                got = ((const nj_u4 *)nj_array_data(pixels))[9 * 16 + 6];
                                snprintf(label, sizeof(label),
                                         "LINEAR %d %s %s: expected %08X, got %08X",
                                         filters[filter], format ? "RGBA" : "RGB", samples[sample].name,
                                         (unsigned)want, (unsigned)got);
                                check(got == want, label);
                            }
                        }
                    }
                }
            }
        }
    }
    nj_clear_exception(vm);
    /* A background image is sampled through its crop, with independent
       repeat/border modes on the two axes. */
    {
        nj_value a[6], r;
        nj_ref target, graphics, bg = make(vm, CLS("Background"));
        nj_ref g3 = make(vm, CLS("Graphics3D"));
        nj_ref image2d = nj_object_new(vm, CLS("Image2D"));
        nj_ref bytes = nj_array_new(vm, "B", 12);
        const nj_u1 colors[12] = {255,0,0, 0,255,0, 0,0,255, 255,255,0};
        nj_ref target_pixels;
        const nj_u4 *p;

        memcpy(nj_array_data(bytes), colors, sizeof(colors));
        a[0].i = 99; a[1].i = 2; a[2].i = 2; a[3].r = bytes;
        check(nj_call_special(vm, image2d, CLS("Image2D"), "<init>",
              "(III[B)V", a, &r) == NJ_OK, "background RGB image constructs");
        a[0].i = 4; a[1].i = 4;
        nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                       "(II)Ljavax/microedition/lcdui/Image;", a, &r);
        target = r.r;
        call(vm, target, "javax/microedition/lcdui/Image", "getGraphics",
             "()Ljavax/microedition/lcdui/Graphics;", NULL, &r);
        graphics = r.r;
        a[0].r = graphics;
        call(vm, g3, CLS("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V", a, NULL);
        a[0].i = 0; a[1].i = 0; a[2].i = 4; a[3].i = 4;
        call(vm, g3, CLS("Graphics3D"), "setViewport", "(IIII)V", a, NULL);
        a[0].r = image2d;
        call(vm, bg, CLS("Background"), "setImage", "(L" CLS("Image2D") ";)V", a, NULL);
        a[0].i = -1; a[1].i = 0; a[2].i = 4; a[3].i = 4;
        call(vm, bg, CLS("Background"), "setCrop", "(IIII)V", a, NULL);
        a[0].i = 33; a[1].i = 32;
        call(vm, bg, CLS("Background"), "setImageMode", "(II)V", a, NULL);
        a[0].r = bg;
        call(vm, g3, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
        target_pixels = nj_rget(vm, target, nj_field_lookup(vm,
            "javax/microedition/lcdui/Image", "imgPixels", "[I", 0));
        p = (const nj_u4 *)nj_array_data(target_pixels);
        check(p[0] == 0xFF00FF00u && p[1] == 0xFFFF0000u &&
              p[2] == p[0] && p[3] == p[1], "background repeats a negative horizontal crop");
        check(p[4] == 0xFFFFFF00u && p[5] == 0xFF0000FFu,
              "background preserves RGB channel order");
        check(p[8] == 0xFF000000u && p[15] == 0xFF000000u,
              "background border uses clear colour outside the image");
        a[0].i = 0;
        call(vm, bg, CLS("Background"), "setColorClearEnable", "(Z)V", a, NULL);
        nj_iput(vm, g3, nj_field_lookup(vm, CLS("Graphics3D"), "_nj_zframe", "I", 0), 77);
        a[0].r = bg;
        call(vm, g3, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
        check(nj_iget(vm, g3, nj_field_lookup(vm, CLS("Graphics3D"), "_nj_zframe", "I", 0)) == -1,
              "depth clears even when colour clear is disabled");
        check(p[0] == 0xFF00FF00u, "disabled colour clear preserves the target");
    }

    /* Animated characters submit camera-facing Sprite3D children through a
       Group. Check pixels, crop orientation and compositing on that path. */
    {
        nj_value a[8], r;
        nj_ref g3 = make(vm, CLS("Graphics3D"));
        nj_ref camera = make(vm, CLS("Camera"));
        nj_ref identity = make(vm, CLS("Transform"));
        nj_ref model = make(vm, CLS("Transform"));
        nj_ref group = make(vm, CLS("Group"));
        nj_ref app = make(vm, CLS("Appearance"));
        nj_ref cm = make(vm, CLS("CompositingMode"));
        nj_ref bg = make(vm, CLS("Background"));
        nj_ref sprite = nj_object_new(vm, CLS("Sprite3D"));
        nj_ref image = nj_object_new(vm, CLS("Image2D"));
        nj_ref bytes = nj_array_new(vm, "B", 16);
        nj_ref target, pixels;
        const nj_u1 colors[16] = {255,0,0,255, 0,255,0,255,
                                  0,0,255,255, 255,255,0,255};
        const nj_u4 *p;
        int k, count;
        memcpy(nj_array_data(bytes), colors, sizeof(colors));
        a[0].i = 100; a[1].i = 2; a[2].i = 2; a[3].r = bytes;
        check(nj_call_special(vm, image, CLS("Image2D"), "<init>",
              "(III[B)V", a, &r) == NJ_OK, "sprite RGBA image constructs");
        a[0].i = 1; a[1].r = image; a[2].r = app;
        check(nj_call_special(vm, sprite, CLS("Sprite3D"), "<init>",
              "(ZL" CLS("Image2D") ";L" CLS("Appearance") ";)V", a, &r) == NJ_OK,
              "scaled Sprite3D constructs");
        call(vm, sprite, CLS("Sprite3D"), "getCropWidth", "()I", NULL, &r);
        check(r.i == 2, "sprite constructor initializes the image crop");
        a[0].r = sprite;
        call(vm, group, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", a, NULL);
        a[0].r = cm;
        call(vm, app, CLS("Appearance"), "setCompositingMode",
             "(L" CLS("CompositingMode") ";)V", a, NULL);
        a[0].i = 16; a[1].i = 16;
        nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                       "(II)Ljavax/microedition/lcdui/Image;", a, &r);
        target = r.r;
        call(vm, target, "javax/microedition/lcdui/Image", "getGraphics",
             "()Ljavax/microedition/lcdui/Graphics;", NULL, &r);
        a[0].r = r.r;
        call(vm, g3, CLS("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V", a, NULL);
        a[0].i = 0; a[1].i = 0; a[2].i = 16; a[3].i = 16;
        call(vm, g3, CLS("Graphics3D"), "setViewport", "(IIII)V", a, NULL);
        a[0].f = 2; a[1].f = 1; a[2].f = 1; a[3].f = 10;
        call(vm, camera, CLS("Camera"), "setParallel", "(FFFF)V", a, NULL);
        a[0].r = camera; a[1].r = identity;
        call(vm, g3, CLS("Graphics3D"), "setCamera",
             "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);
        a[0].f = 0; a[1].f = 0; a[2].f = -2;
        call(vm, model, CLS("Transform"), "postTranslate", "(FFF)V", a, NULL);
        /* render(Node, Transform) replaces the root's local transform. */
        a[0].f = 100; a[1].f = 0; a[2].f = 0;
        call(vm, group, CLS("Group"), "setTranslation", "(FFF)V", a, NULL);
        pixels = nj_rget(vm, target, nj_field_lookup(vm,
            "javax/microedition/lcdui/Image", "imgPixels", "[I", 0));
        p = (const nj_u4 *)nj_array_data(pixels);
        for (k = 0; k < 3; k++) {
            a[0].r = bg;
            call(vm, g3, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
            a[0].i = 0; a[1].i = 0; a[2].i = k == 1 ? -2 : 2; a[3].i = 2;
            call(vm, sprite, CLS("Sprite3D"), "setCrop", "(IIII)V", a, NULL);
            if (k == 2) {
                /* A billboard must not disappear when its node turns edge-on. */
                a[0].f = 90; a[1].f = 0; a[2].f = 1; a[3].f = 0;
                call(vm, sprite, CLS("Sprite3D"), "setOrientation", "(FFFF)V", a, NULL);
            }
            a[0].r = group; a[1].r = model;
            call(vm, g3, CLS("Graphics3D"), "render",
                 "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
            check((p[5 * 16 + 5] & 0xFFFFFFu) == (k == 1 ? 0x00FF00u : 0xFF0000u) &&
                  (p[10 * 16 + 10] & 0xFFFFFFu) == (k == 1 ? 0x0000FFu : 0xFFFF00u),
                  "sprite group renders image crop, horizontal flip and billboard orientation");
        }
        a[0].r = bg;
        call(vm, g3, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
        a[0].f = 0.5f;
        call(vm, cm, CLS("CompositingMode"), "setAlphaThreshold", "(F)V", a, NULL);
        a[0].f = 0;
        call(vm, group, CLS("Node"), "setAlphaFactor", "(F)V", a, NULL);
        a[0].r = group; a[1].r = model;
        call(vm, g3, CLS("Graphics3D"), "render",
             "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
        count = 0;
        for (k = 0; k < 256; k++) if (p[k] & 0xFFFFFFu) count++;
        check(count == 0, "explicit zero parent alpha hides the sprite");
        a[0].f = 1;
        call(vm, group, CLS("Node"), "setAlphaFactor", "(F)V", a, NULL);
        /* Transparent texels are discarded by the appearance alpha threshold. */
        ((nj_u1 *)nj_array_data(bytes))[3] = 0;
        a[0].i = 100; a[1].i = 2; a[2].i = 2; a[3].r = bytes;
        nj_call_special(vm, image, CLS("Image2D"), "<init>", "(III[B)V", a, &r);
        a[0].f = 0.5f;
        call(vm, cm, CLS("CompositingMode"), "setAlphaThreshold", "(F)V", a, NULL);
        a[0].r = bg;
        call(vm, g3, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
        a[0].r = group; a[1].r = model;
        call(vm, g3, CLS("Graphics3D"), "render",
             "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
        check((p[5 * 16 + 5] & 0xFFFFFFu) == 0 &&
              (p[5 * 16 + 10] & 0xFFFFFFu) == 0x00FF00u,
              "sprite transparency obeys appearance alpha threshold");
        a[0].r = bg;
        call(vm, g3, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
        a[0].i = 0; a[1].i = 0; a[2].i = 0; a[3].i = 2;
        call(vm, sprite, CLS("Sprite3D"), "setCrop", "(IIII)V", a, NULL);
        a[0].r = group; a[1].r = model;
        call(vm, g3, CLS("Graphics3D"), "render",
             "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
        count = 0;
        for (k = 0; k < 256; k++) if (p[k] & 0xFFFFFFu) count++;
        check(count == 0, "zero-width sprite crop emits no pixels");
        a[0].r = image;
        call(vm, sprite, CLS("Sprite3D"), "setImage", "(L" CLS("Image2D") ";)V", a, NULL);
        call(vm, sprite, CLS("Sprite3D"), "getCropWidth", "()I", NULL, &r);
        check(r.i == 2, "setImage restores the complete sprite crop");
        a[0].r = bg;
        call(vm, g3, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
        a[0].i = 0; a[1].r = image; a[2].r = app;
        nj_call_special(vm, sprite, CLS("Sprite3D"), "<init>",
                        "(ZL" CLS("Image2D") ";L" CLS("Appearance") ";)V", a, &r);
        a[0].r = group; a[1].r = model;
        call(vm, g3, CLS("Graphics3D"), "render",
             "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
        count = 0;
        for (k = 0; k < 256; k++) if (p[k] & 0xFFFFFFu) count++;
        check(count == 3, "unscaled sprite keeps its two-by-two pixel extent");
        a[0].r = bg;
        call(vm, g3, CLS("Graphics3D"), "clear", "(L" CLS("Background") ";)V", a, NULL);
        a[0].i = 100; a[1].i = 100; a[2].i = 2; a[3].i = 2;
        call(vm, sprite, CLS("Sprite3D"), "setCrop", "(IIII)V", a, NULL);
        a[0].r = group; a[1].r = model;
        call(vm, g3, CLS("Graphics3D"), "render",
             "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
        count = 0;
        for (k = 0; k < 256; k++) if (p[k] & 0xFFFFFFu) count++;
        check(count == 0, "sprite crop outside the image emits no pixels");
    }

    /* ---- each World is lit by its own Light nodes --------------------- */
    /*
     * render(World) takes the Light nodes that are part of that World.  The
     * first implementation collected them once and kept them on the
     * Graphics3D, so the second World was lit by the first World's lights:
     * Doom's menu spot light then rejected every vertex of the level and the
     * 3D world stayed black behind a drawn HUD.  Two Worlds with one red and
     * one green ambient light expose that: B must come out green.
     */
    {
        nj_ref img = 0, gr = 0, g3d, cam, xf, va, vb, norms, data, ndata;
        nj_ref app, mat, worldA, worldB, meshA, meshB, lightA, lightB;
        nj_ref ibA, lengths, bg, pixels, rgb;
        nj_value a[8], r;
        nj_s2 pos[9], nrm[9];
        int k;

        pos[0] = -50;  pos[1] = -50;  pos[2] = -200;
        pos[3] =  50;  pos[4] = -50;  pos[5] = -200;
        pos[6] = -50;  pos[7] =  50;  pos[8] = -200;
        for (k = 0; k < 3; k++) {
            nrm[k * 3] = 0; nrm[k * 3 + 1] = 0; nrm[k * 3 + 2] = -127;
        }

        a[0].i = 16; a[1].i = 16; r.r = 0;
        if (nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                           "(II)Ljavax/microedition/lcdui/Image;", a, &r) == NJ_OK) {
            img = r.r;
        }
        if (img) {
            a[0].r = img; r.r = 0;
            call(vm, img, "javax/microedition/lcdui/Image", "getGraphics",
                 "()Ljavax/microedition/lcdui/Graphics;", a, &r);
            gr = r.r;
        }
        check(img && gr, "a light-regression target image exists");
        g3d = nj_object_new(vm, CLS("Graphics3D"));
        check(g3d != 0, "a light-regression Graphics3D exists");
        if (g3d) {
            a[0].r = gr;
            call(vm, g3d, CLS("Graphics3D"), "bindTarget",
                 "(Ljava/lang/Object;)V", a, NULL);
            a[0].i = 0; a[1].i = 0; a[2].i = 16; a[3].i = 16;
            call(vm, g3d, CLS("Graphics3D"), "setViewport", "(IIII)V", a, NULL);
        }
        cam = make(vm, CLS("Camera"));
        xf = make(vm, CLS("Transform"));
        a[0].f = 60.0f; a[1].f = 1.0f; a[2].f = 1.0f; a[3].f = 1000.0f;
        call(vm, cam, CLS("Camera"), "setPerspective", "(FFFF)V", a, NULL);
        a[0].r = cam; a[1].r = xf;
        call(vm, g3d, CLS("Graphics3D"), "setCamera",
             "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);

        va = make(vm, CLS("VertexArray"));
        vb = make(vm, CLS("VertexBuffer"));
        norms = make(vm, CLS("VertexArray"));
        data = nj_array_new(vm, "S", 9u);
        ndata = nj_array_new(vm, "S", 9u);
        if (data) { memcpy(nj_array_data(data), pos, sizeof pos); }
        if (ndata) { memcpy(nj_array_data(ndata), nrm, sizeof nrm); }
        if (va && vb && norms && data && ndata) {
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"),
                                            "_nj_numVerts", "I", 0), 3);
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"),
                                            "_nj_numComp", "I", 0), 3);
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"),
                                            "_nj_compSize", "I", 0), 2);
            nj_rput(vm, va, nj_field_lookup(vm, CLS("VertexArray"),
                                            "_nj_data", "[S", 0), data);
            nj_iput(vm, norms, nj_field_lookup(vm, CLS("VertexArray"),
                                               "_nj_numVerts", "I", 0), 3);
            nj_iput(vm, norms, nj_field_lookup(vm, CLS("VertexArray"),
                                               "_nj_numComp", "I", 0), 3);
            nj_iput(vm, norms, nj_field_lookup(vm, CLS("VertexArray"),
                                               "_nj_compSize", "I", 0), 1);
            nj_rput(vm, norms, nj_field_lookup(vm, CLS("VertexArray"),
                                               "_nj_data", "[S", 0), ndata);
            nj_rput(vm, vb, nj_field_lookup(vm, CLS("VertexBuffer"),
                                            "_nj_positions",
                                            "L" CLS("VertexArray") ";", 0), va);
            nj_rput(vm, vb, nj_field_lookup(vm, CLS("VertexBuffer"),
                                            "_nj_normals",
                                            "L" CLS("VertexArray") ";", 0), norms);
        }
        app = make(vm, CLS("Appearance"));
        mat = make(vm, CLS("Material"));
        if (mat) {
            a[0].i = 1024; a[1].i = (nj_s4)0xFFFFFFFFu;       /* AMBIENT white */
            call(vm, mat, CLS("Material"), "setColor", "(II)V", a, NULL);
            a[0].i = 2048; a[1].i = (nj_s4)0xFF000000u;       /* DIFFUSE black */
            call(vm, mat, CLS("Material"), "setColor", "(II)V", a, NULL);
            a[0].i = 4096; a[1].i = (nj_s4)0xFF000000u;       /* EMISSIVE black */
            call(vm, mat, CLS("Material"), "setColor", "(II)V", a, NULL);
            a[0].r = mat;
            call(vm, app, CLS("Appearance"), "setMaterial",
                 "(L" CLS("Material") ";)V", a, NULL);
        }
        lengths = nj_array_new(vm, "I", 1u);
        if (lengths) { ((nj_s4 *)nj_array_data(lengths))[0] = 3; }
        ibA = nj_object_new(vm, CLS("TriangleStripArray"));
        if (ibA && lengths) {
            a[0].i = 0; a[1].r = lengths;
            nj_call_special(vm, ibA, CLS("TriangleStripArray"), "<init>",
                            "(I[I)V", a, NULL);
        }
        worldA = make(vm, CLS("World"));
        worldB = make(vm, CLS("World"));
        {
            nj_ref ibB = nj_object_new(vm, CLS("TriangleStripArray"));

            if (ibB && lengths) {
                a[0].i = 0; a[1].r = lengths;
                nj_call_special(vm, ibB, CLS("TriangleStripArray"), "<init>",
                                "(I[I)V", a, NULL);
            }
            meshA = make(vm, CLS("Mesh"));
            meshB = make(vm, CLS("Mesh"));
            if (meshA && vb && ibA && app) {
                a[0].r = vb; a[1].r = ibA; a[2].r = app;
                nj_call_special(vm, meshA, CLS("Mesh"), "<init>",
                    "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                    CLS("Appearance") ";)V", a, NULL);
            }
            if (meshB && vb && ibB && app) {
                a[0].r = vb; a[1].r = ibB; a[2].r = app;
                nj_call_special(vm, meshB, CLS("Mesh"), "<init>",
                    "(L" CLS("VertexBuffer") ";L" CLS("IndexBuffer") ";L"
                    CLS("Appearance") ";)V", a, NULL);
            }
        }
        lightA = make(vm, CLS("Light"));
        lightB = make(vm, CLS("Light"));
        if (lightA && lightB) {
            a[0].i = 128;                                     /* AMBIENT */
            call(vm, lightA, CLS("Light"), "setMode", "(I)V", a, NULL);
            call(vm, lightB, CLS("Light"), "setMode", "(I)V", a, NULL);
            a[0].i = (nj_s4)0xFFFF0000u;                      /* red */
            call(vm, lightA, CLS("Light"), "setColor", "(I)V", a, NULL);
            a[0].i = (nj_s4)0xFF00FF00u;                      /* green */
            call(vm, lightB, CLS("Light"), "setColor", "(I)V", a, NULL);
            a[0].f = 1.0f;
            call(vm, lightA, CLS("Light"), "setIntensity", "(F)V", a, NULL);
            call(vm, lightB, CLS("Light"), "setIntensity", "(F)V", a, NULL);
        }
        if (worldA && meshA && lightA) {
            a[0].r = meshA;
            call(vm, worldA, CLS("Group"), "addChild",
                 "(L" CLS("Node") ";)V", a, NULL);
            a[0].r = lightA;
            call(vm, worldA, CLS("Group"), "addChild",
                 "(L" CLS("Node") ";)V", a, NULL);
        }
        if (worldB && meshB && lightB) {
            a[0].r = meshB;
            call(vm, worldB, CLS("Group"), "addChild",
                 "(L" CLS("Node") ";)V", a, NULL);
            a[0].r = lightB;
            call(vm, worldB, CLS("Group"), "addChild",
                 "(L" CLS("Node") ";)V", a, NULL);
        }
        bg = make(vm, CLS("Background"));
        if (bg) {
            a[0].i = (nj_s4)0xFF000000u;
            call(vm, bg, CLS("Background"), "setColor", "(I)V", a, NULL);
        }
        pixels = nj_rget(vm, img, nj_field_lookup(vm,
            "javax/microedition/lcdui/Image", "imgPixels", "[I", 0));
        rgb = nj_array_new(vm, "I", 256u);

        if (g3d && worldA && worldB && bg && pixels && rgb) {
            int i, a_red = 0, a_green = 0, b_red = 0, b_green = 0;

            a[0].r = bg;
            call(vm, g3d, CLS("Graphics3D"), "clear",
                 "(L" CLS("Background") ";)V", a, NULL);
            a[0].r = worldA;
            call(vm, g3d, CLS("Graphics3D"), "render", "(L" CLS("World") ";)V",
                 a, NULL);
            {
                const nj_u4 *q = (const nj_u4 *)nj_array_data(pixels);

                for (i = 0; i < 256; i++) {
                    nj_u4 c = q[i] & 0xFFFFFFu;
                    if (c == 0xFF0000u) { a_red++; }
                    else if (c == 0x00FF00u) { a_green++; }
                }
            }
            a[0].r = bg;
            call(vm, g3d, CLS("Graphics3D"), "clear",
                 "(L" CLS("Background") ";)V", a, NULL);
            a[0].r = worldB;
            call(vm, g3d, CLS("Graphics3D"), "render", "(L" CLS("World") ";)V",
                 a, NULL);
            {
                const nj_u4 *q = (const nj_u4 *)nj_array_data(pixels);

                for (i = 0; i < 256; i++) {
                    nj_u4 c = q[i] & 0xFFFFFFu;
                    if (c == 0xFF0000u) { b_red++; }
                    else if (c == 0x00FF00u) { b_green++; }
                }
            }
            check(a_red > 0, "the first World is lit by its own red light");
            check(a_green == 0, "the first World is not lit by the second's light");
            check(b_green > 0, "the second World is lit by its own green light");
            check(b_red == 0, "the first World's light does not leak into the second");
        }
    }

    /* Graphics3D.render(Node, Transform) on a scene whose Lights are nodes
       collects them into the Graphics3D's own light list: the conversion
       allocates two arrays and roots them while it fills them.  The roots have
       to go once the fields hold the arrays - a root left behind pins them for
       the life of the VM and makes every snapshot of that title refuse with
       "temp root held", the same silent failure the Image2D constructor had.
       No collected title takes this path yet (a root scan of all 26 M3G JARs
       stayed at zero), which is exactly why the assertion is here: the failure
       mode would otherwise surface as a mysterious cannot-save-state. */
    {
        nj_value a[8], r;
        nj_ref g3 = make(vm, CLS("Graphics3D"));
        nj_ref camera = make(vm, CLS("Camera"));
        nj_ref identity = make(vm, CLS("Transform"));
        nj_ref group = make(vm, CLS("Group"));
        nj_ref light = make(vm, CLS("Light"));
        nj_ref target;
        const nj_field *fn;
        nj_u4 roots_before;
        int nlights;

        a[0].r = light;
        call(vm, group, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", a, NULL);
        a[0].i = 16; a[1].i = 16;
        check(nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                             "(II)Ljavax/microedition/lcdui/Image;", a, &r) == NJ_OK,
              "the node-light target image constructs");
        target = r.r;
        call(vm, target, "javax/microedition/lcdui/Image", "getGraphics",
             "()Ljavax/microedition/lcdui/Graphics;", NULL, &r);
        a[0].r = r.r;
        call(vm, g3, CLS("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V", a, NULL);
        a[0].i = 0; a[1].i = 0; a[2].i = 16; a[3].i = 16;
        call(vm, g3, CLS("Graphics3D"), "setViewport", "(IIII)V", a, NULL);
        a[0].f = 2; a[1].f = 1; a[2].f = 1; a[3].f = 10;
        call(vm, camera, CLS("Camera"), "setParallel", "(FFFF)V", a, NULL);
        a[0].r = camera; a[1].r = identity;
        call(vm, g3, CLS("Graphics3D"), "setCamera",
             "(L" CLS("Camera") ";L" CLS("Transform") ";)V", a, NULL);

        roots_before = nj_root_mark(vm);
        a[0].r = group; a[1].r = identity;
        call(vm, g3, CLS("Graphics3D"), "render",
             "(L" CLS("Node") ";L" CLS("Transform") ";)V", a, NULL);
        fn = nj_field_lookup(vm, CLS("Graphics3D"), "_nj_nlights", "I", 0);
        nlights = fn ? nj_field_read_int(vm, g3, fn) : 0;
        check(nlights == 1, "render(Node) collects the scene's own Light node");
        check(nj_root_mark(vm) == roots_before,
              "render(Node) with scene lights leaves no ref root behind");
    }

    test_image2d_set(vm);
    test_textured_span_variants(vm);
    test_spot_light_cone(vm);
    test_spec_methods(vm);
    test_keyframe_sampling(vm);

    /* The file format's animation objects: animrig_car.m3g, the NFS
       suspension rig, carries its curves as KeyframeSequence + track. */
    test_animation(vm);
    test_animation_colour(vm);
    test_animation_file(vm);
    test_polygon_mode_file(vm);
    test_skinned_mesh(vm);
    test_triangle_strip_encoding0(vm);

    /* Bone rosters are per receiver and grow on demand; the old fixed
       64-entry table threw on the 65th link. */
    test_skinned_mesh_bones(vm);

    /* A World larger than one render's stack array keeps all of its Mesh
       nodes; Doom 3's level (173 meshes) lost everything past the 64th. */
    test_world_mesh_count(vm);
    test_two_sided_lighting(vm);

    /* The file format calls type 18 Sprite; the titles call the class
       Sprite3D.  Both have to be loadable.  These run before the VM is
       destroyed - a lookup on a destroyed VM reads freed memory. */
    check(nj_class_lookup(vm, "javax/microedition/m3g/Sprite") != NULL,
          "javax/microedition/m3g/Sprite is not registered");
    check(nj_class_lookup(vm, "javax/microedition/m3g/Sprite3D") != NULL,
          "javax/microedition/m3g/Sprite3D is not registered");
    check(nj_class_lookup(vm, "javax/microedition/m3g/MorphingMesh") != NULL,
          "javax/microedition/m3g/MorphingMesh is not registered");
    check(nj_class_lookup(vm, "javax/microedition/m3g/SkinnedMesh") != NULL,
          "javax/microedition/m3g/SkinnedMesh is not registered");

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("FAILED (%d failures)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
