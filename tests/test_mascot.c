/*
 * Mascot Capsule Micro3D v3: the arithmetic, the state the classes keep, and
 * the pixels renderPrimitives() paints.
 *
 * The first half checks what a game can observe through its own fields: the
 * fixed-point maths (rotations, concatenation, look-at, normalisation, the
 * integer square root) and the values the remaining classes store and hand
 * back.  The second half binds a real mutable Image, submits geometry through
 * the public eleven-argument ABI and reads the pixels back with
 * Image.getRGB(); the old ABI (three separate coordinate arrays, count in the
 * y slot) and the old FigureLayout field order fail those assertions.
 *
 * What is deliberately NOT tested as working, because the runtime cannot do
 * it: texture sampling, model/Figure rendering (MBAC parsing), MTRA action
 * data, per-vertex lighting, depth sorting and ADD/SUB blending.  Those gap
 * tests assert only what is honest - validation happens, then nothing is
 * drawn - so an unimplemented feature can never look like a wrong picture.
 *
 * With -DNJ_ENABLE_MASCOT=0 the module is trimmed out and this prints a loud
 * SKIP rather than passing silently: a silent pass would hide the module
 * missing from a build that was supposed to have it.
 */

#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_vm.h"
#include "nj2me/nj_config.h"
#include "nj_internal.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#if !NJ_ENABLE_MASCOT

int main(void)
{
    printf("tests/test_mascot: SKIPPED, this build has NJ_ENABLE_MASCOT=0\n");
    return 0;
}

#else

#define V3  "com/mascotcapsule/micro3d/v3/Vector3D"
#define XF  "com/mascotcapsule/micro3d/v3/AffineTrans"
#define U3D "com/mascotcapsule/micro3d/v3/Util3D"
#define FL  "com/mascotcapsule/micro3d/v3/FigureLayout"
#define TEX "com/mascotcapsule/micro3d/v3/Texture"
#define LIG "com/mascotcapsule/micro3d/v3/Light"
#define FX  "com/mascotcapsule/micro3d/v3/Effect3D"
#define G3D "com/mascotcapsule/micro3d/v3/Graphics3D"
#define AT  "com/mascotcapsule/micro3d/v3/ActionTable"

/* 1.0 in this API's fixed point. */
#define ONE 4096

static int checks, failures;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

/* Allocate an object of a class in this package. */
static nj_ref make(nj_vm *vm, const char *cls)
{
    nj_ref o = nj_object_new(vm, cls);

    if (!o) {
        printf("  FAIL  could not allocate %s\n", cls);
        failures++;
        checks++;
    }
    return o;
}

/* Cls.<name>(args...) for a static */
static int call_static(nj_vm *vm, const char *cls, const char *name,
                       const char *desc, nj_value *args, nj_value *ret)
{
    nj_status st = nj_call_static(vm, cls, name, desc, args, ret);

    if (st != NJ_OK) {
        printf("  FAIL  %s.%s%s returned status %d\n", cls, name, desc, (int)st);
        failures++;
        checks++;
        return 0;
    }
    return 1;
}

/* obj.<name>(args...) */
static int call(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                const char *desc, nj_value *args, nj_value *ret)
{
    nj_status st = nj_call(vm, o, cls, name, desc, args, ret);

    if (st != NJ_OK) {
        printf("  FAIL  %s.%s%s returned status %d\n", cls, name, desc, (int)st);
        failures++;
        checks++;
        return 0;
    }
    return 1;
}

/* An int field of the object, read by name; -999999 when it is missing. */
static nj_s4 field(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    nj_field *f = nj_field_lookup(vm, cls, name, "I", 0);

    if (!f) {
        printf("  FAIL  %s has no int field %s\n", cls, name);
        failures++;
        checks++;
        return -999999;
    }
    return nj_iget(vm, o, f);
}

/* A reference field of the object, read by name; 0 when it is missing. */
static nj_ref ref_field(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                        const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);

    if (!f) {
        printf("  FAIL  %s has no reference field %s%s\n", cls, name, desc);
        failures++;
        checks++;
        return 0;
    }
    return nj_rget(vm, o, f);
}

/* Test-only: write an int field by name, to mutate an AffineTrans the way
 * Java code would through the public m... fields. */
static void set_int(nj_vm *vm, nj_ref o, const char *name, nj_s4 v)
{
    nj_field *f = o ? nj_field_lookup_in(o->cls, name, "I", 0) : NULL;

    if (f) {
        nj_field_write_int_fast(vm, o, f, v);
    }
}

/* Read every pixel of an image as one ARGB int array. */
static nj_ref grab(nj_vm *vm, nj_ref img, nj_s4 w, nj_s4 h)
{
    nj_value a[7], r;
    nj_ref dst = nj_int_array_new(vm, (nj_u4)(w * h));

    if (!dst) {
        return 0;
    }
    a[0].r = dst;
    a[1].i = 0;
    a[2].i = w;
    a[3].i = 0;
    a[4].i = 0;
    a[5].i = w;
    a[6].i = h;
    if (nj_call(vm, img, "javax/microedition/lcdui/Image", "getRGB",
                "([IIIIIII)V", a, &r) != NJ_OK) {
        return 0;
    }
    return dst;
}

/* A white mutable Image plus its Graphics: "nothing was drawn" is visible. */
static int target(nj_vm *vm, nj_s4 w, nj_s4 h, nj_ref *img_out, nj_ref *gfx_out)
{
    nj_value a[4], r;
    nj_ref img;

    *img_out = 0;
    *gfx_out = 0;
    a[0].i = w;
    a[1].i = h;
    if (nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                       "(II)Ljavax/microedition/lcdui/Image;", a, &r) != NJ_OK ||
        !r.r) {
        return 0;
    }
    img = r.r;
    if (nj_call(vm, img, "javax/microedition/lcdui/Image", "getGraphics",
                "()Ljavax/microedition/lcdui/Graphics;", NULL, &r) != NJ_OK ||
        !r.r) {
        return 0;
    }
    *img_out = img;
    *gfx_out = r.r;
    a[0].i = 0xFFFFFF;
    (void)nj_call(vm, *gfx_out, "javax/microedition/lcdui/Graphics",
                  "setColor", "(I)V", a, NULL);
    a[0].i = 0;
    a[1].i = 0;
    a[2].i = w;
    a[3].i = h;
    (void)nj_call(vm, *gfx_out, "javax/microedition/lcdui/Graphics",
                  "fillRect", "(IIII)V", a, NULL);
    return 1;
}

/* Image.getRGB of one pixel; 0xDEADBEEF on any kind of failure. */
static nj_u4 pget(nj_vm *vm, nj_ref img, nj_s4 x, nj_s4 y)
{
    nj_value a[7];
    nj_ref dst = nj_int_array_new(vm, 1);

    if (!dst) {
        return 0xDEADBEEFu;
    }
    a[0].r = dst;
    a[1].i = 0;
    a[2].i = 1;
    a[3].i = x;
    a[4].i = y;
    a[5].i = 1;
    a[6].i = 1;
    if (nj_call(vm, img, "javax/microedition/lcdui/Image", "getRGB",
                "([IIIIIII)V", a, NULL) != NJ_OK) {
        return 0xDEADBEEFu;
    }
    return (nj_u4)((nj_s4 *)nj_array_data(dst))[0];
}

/* A Java int[] copied from C values, or 0 when the allocation fails. */
static nj_ref ints(nj_vm *vm, const nj_s4 *v, int n)
{
    nj_ref a = nj_int_array_new(vm, (nj_u4)n);

    if (a && v) {
        memcpy(nj_array_data(a), v, (size_t)n * sizeof(nj_s4));
    }
    return a;
}

/* Graphics3D.renderPrimitives with every Java argument in its real slot. */
static nj_status render_ex(nj_vm *vm, nj_ref g3, nj_ref texture, nj_ref layout,
                           nj_ref fx, nj_s4 command, nj_s4 count, nj_ref verts,
                           nj_ref normals, nj_ref uvs, nj_ref colors,
                           nj_s4 x, nj_s4 y)
{
    nj_value a[11];

    memset(a, 0, sizeof a);
    a[0].r = texture;
    a[1].i = x;
    a[2].i = y;
    a[3].r = layout;
    a[4].r = fx;
    a[5].i = command;
    a[6].i = count;
    a[7].r = verts;
    a[8].r = normals;
    a[9].r = uvs;
    a[10].r = colors;
    return nj_call(vm, g3, G3D, "renderPrimitives",
                   "(L" TEX ";IIL" FL ";L" FX ";II[I[I[I[I)V", a, NULL);
}

/* The solid path: no texture. */
static nj_status render(nj_vm *vm, nj_ref g3, nj_ref layout, nj_ref fx,
                        nj_s4 command, nj_s4 count, nj_ref verts,
                        nj_ref normals, nj_ref uvs, nj_ref colors,
                        nj_s4 x, nj_s4 y)
{
    return render_ex(vm, g3, 0, layout, fx, command, count, verts, normals,
                     uvs, colors, x, y);
}

/* Expect an exception whose description names cls_part. */
static void expect_throw(nj_vm *vm, const char *what, nj_status st,
                         const char *cls_part)
{
    const char *desc;

    if (st == NJ_OK) {
        check(0, what);
        return;
    }
    desc = nj_exception_describe(vm);
    check(desc != NULL && strstr(desc, cls_part) != NULL, what);
    nj_clear_exception(vm);
}

/* new Vector3D(x, y, z) */
static nj_ref vec(nj_vm *vm, nj_s4 x, nj_s4 y, nj_s4 z)
{
    nj_ref o = make(vm, V3);
    nj_value a[3], r;

    a[0].i = x;
    a[1].i = y;
    a[2].i = z;
    if (o) {
        (void)call(vm, o, V3, "<init>", "(III)V", a, &r);
    }
    return o;
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref a, b;
    nj_value args[3], ret;

    nj_port_headless(&port);
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        printf("tests/test_mascot: could not create a VM\n");
        return 1;
    }

    /* ---- Vector3D: fields, set, unit --- */
    {
        nj_ref v = make(vm, V3);

        (void)call(vm, v, V3, "<init>", "()V", NULL, &ret);
        check(field(vm, v, V3, "x") == 0 && field(vm, v, V3, "y") == 0 &&
              field(vm, v, V3, "z") == 0, "a new Vector3D is the zero vector");

        args[0].i = 1; args[1].i = 2; args[2].i = 3;
        (void)call(vm, v, V3, "set", "(III)V", args, &ret);
        check(field(vm, v, V3, "x") == 1 && field(vm, v, V3, "y") == 2 &&
              field(vm, v, V3, "z") == 3, "set(1,2,3) stores all three");

        /* (3,4,0) scaled by 4096 normalises to (2457..2458, 3276..3277, 0). */
        v = vec(vm, 3 * ONE, 4 * ONE, 0);
        (void)call(vm, v, V3, "unit", "()V", NULL, &ret);
        {
            nj_s4 x = field(vm, v, V3, "x");
            nj_s4 y = field(vm, v, V3, "y");
            nj_s4 z = field(vm, v, V3, "z");
            check(x >= 2456 && x <= 2459 && y >= 3275 && y <= 3279 && z == 0,
                  "unit() turns (3,4,0) into three fifths / four fifths of 4096");
        }

        /* A zero vector must not be divided by zero. */
        v = vec(vm, 0, 0, 0);
        (void)call(vm, v, V3, "unit", "()V", NULL, &ret);
        check(field(vm, v, V3, "x") == 0 && field(vm, v, V3, "y") == 0 &&
              field(vm, v, V3, "z") == 0, "unit() leaves the zero vector alone");
    }

    /* ---- innerProduct / outerProduct (both are instance methods) --- */
    {
        nj_ref p = vec(vm, ONE, 0, 0);
        nj_ref q = vec(vm, 0, ONE, 0);
        nj_ref o = make(vm, V3);

        args[0].r = q;
        if (call(vm, p, V3, "innerProduct", "(L" V3 ";)I", args, &ret)) {
            check(ret.i == 0, "the inner product of two perpendicular axes is 0");
        }

        args[0].r = p;
        if (call(vm, p, V3, "innerProduct", "(L" V3 ";)I", args, &ret)) {
            check(ret.i == ONE, "innerProduct(x, x) is 4096 for a unit x");
        }

        args[0].r = p;
        args[1].r = q;
        if (call(vm, o, V3, "outerProduct", "(L" V3 ";L" V3 ";)V", args, &ret)) {
            check(field(vm, o, V3, "x") == 0 && field(vm, o, V3, "y") == 0 &&
                  field(vm, o, V3, "z") == ONE, "x cross y is z");
        }
    }

    /* ---- AffineTrans: identity, rotations, concatenation --- */
    {
        nj_ref m = make(vm, XF);

        (void)call(vm, m, XF, "<init>", "()V", NULL, &ret);
        check(field(vm, m, XF, "m00") == ONE && field(vm, m, XF, "m11") == ONE &&
              field(vm, m, XF, "m22") == ONE && field(vm, m, XF, "m01") == 0 &&
              field(vm, m, XF, "m03") == 0, "a new AffineTrans is the identity");

        /* 1024 of 4096 is a quarter turn: exact, which is the point of doing
         * this in integers. */
        args[0].i = ONE / 4;
        (void)call(vm, m, XF, "rotationY", "(I)V", args, &ret);
        check(field(vm, m, XF, "m00") == 0 && field(vm, m, XF, "m02") == ONE &&
              field(vm, m, XF, "m20") == -ONE && field(vm, m, XF, "m22") == 0,
              "rotationY(quarter turn): m00=m22=0, m02=+1, m20=-1");

        (void)call(vm, m, XF, "rotationX", "(I)V", args, &ret);
        check(field(vm, m, XF, "m11") == 0 && field(vm, m, XF, "m12") == -ONE &&
              field(vm, m, XF, "m21") == ONE && field(vm, m, XF, "m22") == 0,
              "rotationX(quarter turn) is exactly +-4096 where it should be");

        (void)call(vm, m, XF, "rotationZ", "(I)V", args, &ret);
        check(field(vm, m, XF, "m00") == 0 && field(vm, m, XF, "m01") == -ONE &&
              field(vm, m, XF, "m10") == ONE && field(vm, m, XF, "m11") == 0,
              "rotationZ(quarter turn) is exactly +-4096 where it should be");

        /*
         * Concatenation: a translation must survive multiplication by the
         * identity, and must NOT be applied twice.  That is the column-3 bug
         * this file was written to catch.
         */
        (void)call(vm, m, XF, "<init>", "()V", NULL, &ret);
        {
            nj_field *f = nj_field_lookup(vm, XF, "m03", "I", 0);
            nj_field *g = nj_field_lookup(vm, XF, "m13", "I", 0);
            nj_field *h = nj_field_lookup(vm, XF, "m23", "I", 0);
            if (f && g && h) {
                nj_iput(vm, m, f, 10);
                nj_iput(vm, m, g, 20);
                nj_iput(vm, m, h, 30);
            }
        }
        {
            nj_ref id = make(vm, XF);
            (void)call(vm, id, XF, "<init>", "()V", NULL, &ret);
            args[0].r = m;
            args[1].r = id;
            b = make(vm, XF);
            (void)call(vm, b, XF, "multiply", "(L" XF ";L" XF ";)V", args, &ret);
            check(field(vm, b, XF, "m03") == 10 && field(vm, b, XF, "m13") == 20 &&
                  field(vm, b, XF, "m23") == 30,
                  "multiply by the identity keeps the translation (column 3)");
            check(field(vm, b, XF, "m00") == ONE && field(vm, b, XF, "m01") == 0,
                  "multiply by the identity keeps the 3x3 part");
        }

        /* transform() applies the translation to a point. */
        (void)call(vm, m, XF, "<init>", "()V", NULL, &ret);
        {
            nj_field *f = nj_field_lookup(vm, XF, "m03", "I", 0);
            nj_field *g = nj_field_lookup(vm, XF, "m13", "I", 0);
            if (f && g) {
                nj_iput(vm, m, f, 100);
                nj_iput(vm, m, g, 200);
            }
        }
        a = vec(vm, 0, 0, 0);
        args[0].r = a;
        (void)call(vm, m, XF, "transform", "(L" V3 ";)V", args, &ret);
        check(field(vm, a, V3, "x") == 100 && field(vm, a, V3, "y") == 200,
              "transform() adds the translation to the point");

        /* translation() reads back the third column, not a row. */
        (void)call(vm, m, XF, "translation", "(L" V3 ";)V", args, &ret);
        check(field(vm, a, V3, "x") == 100 && field(vm, a, V3, "y") == 200 &&
              field(vm, a, V3, "z") == 0,
              "translation() reads the third column");
    }

    /* ---- setViewTrans: a camera looking down +z from the origin --- */
    {
        nj_ref m = make(vm, XF);
        nj_ref pos = vec(vm, 0, 0, 0);
        nj_ref at = vec(vm, 0, 0, ONE);
        nj_ref up = vec(vm, 0, ONE, 0);

        args[0].r = pos;
        args[1].r = at;
        args[2].r = up;
        (void)call(vm, m, XF, "setViewTrans", "(L" V3 ";L" V3 ";L" V3 ";)V", args, &ret);
        check(field(vm, m, XF, "m22") == ONE, "the look-at forward vector is +z");
        check(field(vm, m, XF, "m00") == -ONE, "the look-at right vector is -x");
        check(field(vm, m, XF, "m11") == ONE, "the look-at up vector is +y");
        check(field(vm, m, XF, "m03") == 0 && field(vm, m, XF, "m13") == 0 &&
              field(vm, m, XF, "m23") == 0,
              "a camera at the origin has no translation");

        /* A degenerate view must not divide by zero. */
        (void)call(vm, m, XF, "setIdentity", "()V", NULL, &ret);
        args[1].r = pos;   /* look at where we already are */
        (void)call(vm, m, XF, "setViewTrans", "(L" V3 ";L" V3 ";L" V3 ";)V", args, &ret);
        check(field(vm, m, XF, "m00") == ONE,
              "a degenerate look-at leaves the matrix alone");
    }

    /* ---- Full-cycle rotation and Util3D trigonometry --- */
    {
        nj_ref m = make(vm, XF);
        nj_value arg, sr, cr;
        int angle, rotations_ok = 1, api_ok = 1;
        static const nj_s4 extremes[] = { INT_MIN, INT_MAX, -1024, -1, 4096 };
        unsigned k;

        /* Independent floating-point oracle, including all four quadrants.
         * The old formula returned -6144 at -90 degrees and quantised the
         * angle to whole degrees, losing most of the 4096 available phases. */
        for (angle = 0; angle < 4096; angle++) {
            double theta = angle * 3.14159265358979323846 / 2048.0;
            nj_s4 s = (nj_s4)floor(sin(theta) * ONE + 0.5);
            nj_s4 c = (nj_s4)floor(cos(theta) * ONE + 0.5);
            arg.i = angle;
            if (nj_call(vm, m, XF, "rotationZ", "(I)V", &arg, &ret) != NJ_OK ||
                field(vm, m, XF, "m00") != c || field(vm, m, XF, "m01") != -s ||
                field(vm, m, XF, "m10") != s || field(vm, m, XF, "m11") != c) {
                rotations_ok = 0;
            }
            /* Check presence once, so a missing method does not generate
             * thousands of identical exception diagnostics on the control. */
            if (api_ok && (nj_call_static(vm, U3D, "sin", "(I)I", &arg, &sr) != NJ_OK ||
                           nj_call_static(vm, U3D, "cos", "(I)I", &arg, &cr) != NJ_OK ||
                           sr.i != s || cr.i != c)) {
                api_ok = 0;
            }
        }
        check(rotations_ok, "rotationZ matches sine/cosine at all 4096 phases");
        check(api_ok, "Util3D.sin/cos expose the same correctly rounded 4096 phases");
        for (k = 0; k < sizeof(extremes) / sizeof(extremes[0]); k++) {
            nj_s4 phase = (nj_s4)((nj_u4)extremes[k] & 4095u);
            double theta = phase * 3.14159265358979323846 / 2048.0;
            nj_s4 s = (nj_s4)floor(sin(theta) * ONE + 0.5);
            nj_s4 c = (nj_s4)floor(cos(theta) * ONE + 0.5);
            arg.i = extremes[k];
            (void)call(vm, m, XF, "rotationX", "(I)V", &arg, &ret);
            check(field(vm, m, XF, "m11") == c && field(vm, m, XF, "m12") == -s &&
                  field(vm, m, XF, "m21") == s && field(vm, m, XF, "m22") == c,
                  "rotationX wraps negative and extreme angles without overflow");
            (void)call(vm, m, XF, "rotationY", "(I)V", &arg, &ret);
            check(field(vm, m, XF, "m00") == c && field(vm, m, XF, "m02") == s &&
                  field(vm, m, XF, "m20") == -s && field(vm, m, XF, "m22") == c,
                  "rotationY wraps negative and extreme angles without overflow");
        }
    }

    /* ---- Util3D.sqrt --- */
    {
        nj_value one;

        one.i = ONE * ONE;            /* the squared length of a unit vector */
        (void)call_static(vm, U3D, "sqrt", "(I)I", &one, &ret);
        check(ret.i == ONE, "sqrt(4096*4096) is 4096");

        one.i = 0;
        (void)call_static(vm, U3D, "sqrt", "(I)I", &one, &ret);
        check(ret.i == 0, "sqrt(0) is 0");

        one.i = -5;
        (void)call_static(vm, U3D, "sqrt", "(I)I", &one, &ret);
        check(ret.i == 0, "sqrt of a negative number is 0, not a crash");

        one.i = 15;
        (void)call_static(vm, U3D, "sqrt", "(I)I", &one, &ret);
        check(ret.i == 3, "sqrt(15) is 3 (floor)");
    }

    /* ---- the state the remaining classes keep --- */
    {
        nj_ref fl = make(vm, FL);
        nj_ref tx = make(vm, TEX);
        nj_ref li = make(vm, LIG);
        nj_ref fx = make(vm, FX);
        nj_ref g3 = make(vm, G3D);
        nj_ref at = make(vm, AT);
        nj_ref g = make(vm, "javax/microedition/lcdui/Graphics");

        (void)call(vm, fl, FL, "<init>", "()V", NULL, &ret);
        /* The reference default: identity affine, parallel projection,
         * scale 512, centre exactly (0,0) - not the middle of the screen. */
        check(field(vm, fl, FL, "_p") == (nj_s4)0x90000000 &&
              field(vm, fl, FL, "_sx") == 512 && field(vm, fl, FL, "_sy") == 512 &&
              field(vm, fl, FL, "_cx") == 0 && field(vm, fl, FL, "_cy") == 0,
              "FigureLayout() defaults to parallel scale 512 and centre 0,0");
        check(ref_field(vm, fl, FL, "_xf", "L" XF ";") == 0,
              "FigureLayout() keeps no AffineTrans: identity is implicit, zero RAM");
        {
            nj_ref xf1 = 0, xf2 = 0;

            check(call(vm, fl, FL, "getAffineTrans", "()L" XF ";", NULL, &ret) &&
                  ret.r, "getAffineTrans() materialises the implicit identity");
            xf1 = ret.r;
            check(field(vm, xf1, XF, "m00") == ONE &&
                  field(vm, xf1, XF, "m11") == ONE &&
                  field(vm, xf1, XF, "m22") == ONE &&
                  field(vm, xf1, XF, "m03") == 0,
                  "the materialised matrix is the identity");
            (void)call(vm, fl, FL, "getAffineTrans", "()L" XF ";", NULL, &ret);
            xf2 = ret.r;
            check(xf2 == xf1,
                  "getAffineTrans returns the stored matrix, not a fresh copy");
            check(ref_field(vm, fl, FL, "_xf", "L" XF ";") == xf1,
                  "the matrix is stored on the layout and stays mutable in Java");
        }

        args[0].i = 60; args[1].i = 4000; args[2].i = 512;
        (void)call(vm, fl, FL, "setPerspective", "(III)V", args, &ret);
        check(field(vm, fl, FL, "_near") == 60 && field(vm, fl, FL, "_far") == 4000 &&
              field(vm, fl, FL, "_p") == (nj_s4)0x92000200,
              "setPerspective(zNear,zFar,angle) keeps near,far and packs the angle in _p");
        args[0].i = 4000; args[1].i = 60; args[2].i = 512;
        expect_throw(vm, "setPerspective with zNear >= zFar is IllegalArgumentException",
                     nj_call(vm, fl, FL, "setPerspective", "(III)V", args, &ret),
                     "IllegalArgumentException");
        args[0].i = 60; args[1].i = 40000; args[2].i = 512;
        expect_throw(vm, "setPerspective with zFar > 32767 is IllegalArgumentException",
                     nj_call(vm, fl, FL, "setPerspective", "(III)V", args, &ret),
                     "IllegalArgumentException");
        args[0].i = 60; args[1].i = 4000; args[2].i = 0;
        expect_throw(vm, "setPerspective with angle 0 is IllegalArgumentException",
                     nj_call(vm, fl, FL, "setPerspective", "(III)V", args, &ret),
                     "IllegalArgumentException");
        check(field(vm, fl, FL, "_near") == 60 && field(vm, fl, FL, "_far") == 4000 &&
              field(vm, fl, FL, "_p") == (nj_s4)0x92000200,
              "invalid setPerspective arguments leave the previous projection intact");

        args[0].i = 120; args[1].i = 160;
        (void)call(vm, fl, FL, "setCenter", "(II)V", args, &ret);
        args[0].i = 120; args[1].i = 160;
        (void)call(vm, fl, FL, "setScale", "(II)V", args, &ret);
        check(field(vm, fl, FL, "_cx") == 120 && field(vm, fl, FL, "_cy") == 160 &&
              field(vm, fl, FL, "_sx") == 120 && field(vm, fl, FL, "_sy") == 160,
              "FigureLayout keeps its center and scale");
        check(field(vm, fl, FL, "_p") == (nj_s4)0x90000000,
              "setScale switches the layout back to parallel projection");

        args[0].i = 8; args[1].i = 4;
        (void)call(vm, tx, TEX, "<init>", "(II)V", args, &ret);
        check(field(vm, tx, TEX, "_w") == 8 && field(vm, tx, TEX, "_h") == 4,
              "Texture(w,h) keeps its size");

        args[0].i = 7;
        (void)call(vm, li, LIG, "setAmbIntensity", "(I)V", args, &ret);
        args[0].i = 9;
        (void)call(vm, li, LIG, "setDirIntensity", "(I)V", args, &ret);
        check(field(vm, li, LIG, "_amb") == 7 && field(vm, li, LIG, "_dif") == 9,
              "Light keeps both intensities");

        args[0].i = 1;
        (void)call(vm, fx, FX, "setSemiTransparentEnabled", "(Z)V", args, &ret);
        args[0].i = 255;
        (void)call(vm, fx, FX, "setShading", "(I)V", args, &ret);
        check(field(vm, fx, FX, "_semi") == 1 && field(vm, fx, FX, "_sh") == 255,
              "Effect3D keeps shading and the transparency flag");

        /* bind stores the target, release clears it: an unbalanced pair is
         * visible instead of silent. */
        args[0].r = g;
        (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V", args, &ret);
        {
            nj_field *f = nj_field_lookup(vm, G3D, "_g", "Ljavax/microedition/lcdui/Graphics;", 0);
            check(f != NULL && nj_rget(vm, g3, f) == g, "Graphics3D.bind remembers its target");
            (void)call(vm, g3, G3D, "release", "(Ljavax/microedition/lcdui/Graphics;)V", args, &ret);
            check(f != NULL && nj_rget(vm, g3, f) == 0, "Graphics3D.release forgets it");
        }

        args[0].i = 0;
        (void)call(vm, at, AT, "getNumFrame", "(I)I", args, &ret);
        check(ret.i == 0, "ActionTable reports no frames rather than inventing a pose");
    }

    /* ---- the constructors the wider corpus asked for --- */
    {
        nj_ref li = make(vm, LIG);
        nj_ref dir = vec(vm, 0, -ONE, 0);
        nj_ref tx = make(vm, TEX);
        nj_ref fx = make(vm, FX);
        nj_ref bytes = nj_byte_array_new(vm, 4);

        args[0].r = dir; args[1].i = 300; args[2].i = 100;
        (void)call(vm, li, LIG, "<init>", "(L" V3 ";II)V", args, &ret);
        check(field(vm, li, LIG, "_dif") == 300 && field(vm, li, LIG, "_amb") == 100,
              "Light(direction, dirIntensity, ambIntensity) keeps both");

        args[0].r = bytes; args[1].i = 1;
        (void)call(vm, tx, TEX, "<init>", "([BZ)V", args, &ret);
        check(field(vm, tx, TEX, "_blend") == 1,
              "Texture(byte[], true) remembers the blend flag");

        /* Four slots, so it gets its own array: the shared `args` at the top of
         * main holds three, and writing a fourth into it smashes the stack. */
        {
            nj_value a4[4];

            a4[0].r = li; a4[1].i = 255; a4[2].i = 1; a4[3].r = tx;
            (void)call(vm, fx, FX, "<init>", "(L" LIG ";IZL" TEX ";)V", a4, &ret);
        }
        check(field(vm, fx, FX, "_sh") == 255 && field(vm, fx, FX, "_semi") == 1,
              "Effect3D(light, shading, semi, sphereMap) keeps shading and the flag");
        {
            nj_field *lf = nj_field_lookup(vm, FX, "_lig", "L" LIG ";", 0);
            nj_field *tf = nj_field_lookup(vm, FX, "_sph", "L" TEX ";", 0);
            check(lf && tf && nj_rget(vm, fx, lf) == li && nj_rget(vm, fx, tf) == tx,
                  "Effect3D keeps the light and the sphere map it was given");
        }
    }

    /* ---- renderPrimitives: the corrected ABI, observed through pixels --- */
    /*
     * The old implementation read three separate position arrays out of slots
     * 7, 8 and 9 and the primitive count out of slot 2, which is the Java y
     * argument of a real caller.  The corrected contract is the one the games
     * use: slot 7 is one interleaved xyz int array, slot 6 is the number of
     * primitives, slots 1 and 2 are drawing offsets.  These checks go through
     * a real bound Image and read the pixels back with Image.getRGB(), so
     * they fail on the old ABI and pass only on the real one.
     */
    {
        nj_ref img, g, g3, fl, fx;
        nj_ref verts, colors;
        nj_vm_stats before, after;
        nj_value a[3];
        nj_status st;

        check(target(vm, 256, 256, &img, &g),
              "a white 256x256 target Image and its Graphics exist");
        g3 = make(vm, G3D);
        fl = make(vm, FL);
        fx = make(vm, FX);
        (void)call(vm, fl, FL, "<init>", "()V", NULL, NULL);
        (void)call(vm, fx, FX, "<init>", "()V", NULL, NULL);
        a[0].r = g;
        check(call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                   a, NULL), "Graphics3D.bind accepts a real Image Graphics");

        /* Default FigureLayout: parallel projection, scale 512, centre 0,0. */
        {
            nj_s4 v[3] = { 1024, 1024, 0 };
            nj_s4 c[1] = { 0x00FF0000 };

            verts = ints(vm, v, 3);
            colors = ints(vm, c, 1);
            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, 0, 0);
            check(st == NJ_OK, "a solid point renders with the eleven-argument ABI");
            check(pget(vm, img, 128, 128) == 0xFFFF0000u,
                  "default parallel unity is 512: world (1024,1024) is pixel (128,128)");
            check(pget(vm, img, 129, 128) == 0xFFFFFFFFu,
                  "a point paints one pixel, not a triangle-strip segment");

            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, 10, -20);
            check(st == NJ_OK, "a point renders with non-zero x/y drawing offsets");
            check(pget(vm, img, 138, 108) == 0xFFFF0000u,
                  "the x/y arguments add to the layout centre instead of being culled");
        }

        /*
         * The identity AffineTrans is implicit until Java asks for it: the
         * getter materialises one instance, the renderer sees its mutations,
         * and setAffineTrans(null) resets to the same no-allocation state.
         */
        {
            nj_ref xf;
            nj_s4 pv[3] = { 0, 0, 0 };
            nj_s4 pc[1] = { 0x0000FF00 };

            check(call(vm, fl, FL, "getAffineTrans", "()L" XF ";", NULL, &ret) &&
                  ret.r, "getAffineTrans() works on a default layout");
            xf = ret.r;
            set_int(vm, xf, "m03", 64);
            set_int(vm, xf, "m13", 64);
            verts = ints(vm, pv, 3);
            colors = ints(vm, pc, 1);
            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, 0, 0);
            /* default layout scale is 512, so a translated world (0,0) at
             * (64,64) lands on pixel (8,8); the earlier red pixel stays put. */
            check(st == NJ_OK && pget(vm, img, 8, 8) == 0xFF00FF00u &&
                  pget(vm, img, 128, 128) == 0xFFFF0000u,
                  "the matrix returned by getAffineTrans is the one the renderer uses");
            a[0].r = 0;
            (void)call(vm, fl, FL, "setAffineTrans", "(L" XF ";)V", a, NULL);
            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, 0, 0);
            check(st == NJ_OK && pget(vm, img, 0, 0) == 0xFF00FF00u &&
                  ref_field(vm, fl, FL, "_xf", "L" XF ";") == 0,
                  "setAffineTrans(null) resets to the implicit identity, no object kept");
        }

        /*
         * Extreme but legal int inputs.  The reference runs this transform in
         * Java int, so every product and the sum wrap modulo 2^32: with the
         * layout at 1:1, INT_MIN * INT_MIN is 0 after the wrap, and +2048,
         * >>12 and the translation column behave exactly like the reference
         * (old 64-bit math would have produced ~2^50 and a culled point).
         * These checks assert the Java-32 pixels, so they also fail if the
         * math is widened again.
         */
        {
            nj_ref imge, ge, xfe;
            nj_s4 onev[3] = { -2147483647 - 1, 0, 0 };
            nj_s4 sumv[3] = { -2147483647 - 1, -2147483647 - 1, 0 };
            nj_s4 trv[3] = { 4096, 0, 0 };
            nj_s4 ec[1] = { 0x0000FFFF };
            const nj_u4 cyan = 0xFF00FFFFu;

            check(target(vm, 32, 32, &imge, &ge),
                  "a fresh 32x32 target for the Java-32 transform extremes");
            a[0].r = ge;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            a[0].i = ONE;
            a[1].i = ONE;
            (void)call(vm, fl, FL, "setScale", "(II)V", a, NULL);
            a[0].i = 0;
            a[1].i = 0;
            (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);
            check(call(vm, fl, FL, "getAffineTrans", "()L" XF ";", NULL, &ret) &&
                  ret.r, "getAffineTrans gives the matrix to stress");
            xfe = ret.r;

            /* INT_MIN * INT_MIN == 0 (mod 2^32), >>12 is 0: pixel (0,0). */
            set_int(vm, xfe, "m00", -2147483647 - 1);
            verts = ints(vm, onev, 3);
            colors = ints(vm, ec, 1);
            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, 0, 0);
            check(st == NJ_OK && pget(vm, imge, 0, 0) == cyan,
                  "Java-32 wrap: INT_MIN * INT_MIN transforms to the origin pixel");

            /* Two such products still wrap the sum to 0; m03=7 moves it. */
            set_int(vm, xfe, "m01", -2147483647 - 1);
            set_int(vm, xfe, "m03", 7);
            verts = ints(vm, sumv, 3);
            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, 0, 0);
            check(st == NJ_OK && pget(vm, imge, 7, 0) == cyan,
                  "Java-32 wrap: a three-product sum lands on the wrapped value plus m03");

            /* Translation addition wraps in int too: 1 + INT_MAX -> INT_MIN,
             * so the point is culled instead of turning into a huge positive. */
            set_int(vm, xfe, "m00", 1);
            set_int(vm, xfe, "m01", 0);
            set_int(vm, xfe, "m03", 2147483647);
            verts = ints(vm, trv, 3);
            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, 0, 0);
            check(st == NJ_OK && pget(vm, imge, 1, 0) == 0xFFFFFFFFu,
                  "Java-32 wrap: 1 + INT_MAX translation wraps to INT_MIN and is culled");

            a[0].r = 0;
            (void)call(vm, fl, FL, "setAffineTrans", "(L" XF ";)V", a, NULL);
        }

        /*
         * Extreme centres wrap like Java int addition; signed overflow in C
         * would be undefined.  The wrapped centre lands off-screen, so the
         * point is culled - the observable Java result, not a trap.
         */
        {
            nj_ref imgw, gw;
            nj_s4 wv[3] = { 0, 0, 0 };
            nj_s4 wc[1] = { 0x00FF00FF };

            check(target(vm, 64, 64, &imgw, &gw),
                  "a fresh 64x64 target for centre overflow");
            a[0].r = gw;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            a[0].i = ONE;
            a[1].i = ONE;
            (void)call(vm, fl, FL, "setScale", "(II)V", a, NULL);
            a[0].i = 2147483647;
            a[1].i = 0;
            (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);
            verts = ints(vm, wv, 3);
            colors = ints(vm, wc, 1);
            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, 1, 0);
            check(st == NJ_OK && pget(vm, imgw, 0, 0) == 0xFFFFFFFFu,
                  "centre INT_MAX + 1 wraps to INT_MIN and is culled, not UB");
            a[0].i = -2147483647 - 1;
            a[1].i = 0;
            (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);
            st = render(vm, g3, fl, fx, 0x01000400, 1, verts, 0, 0, colors, -1, 0);
            check(st == NJ_OK && pget(vm, imgw, 0, 0) == 0xFFFFFFFFu,
                  "centre INT_MIN - 1 wraps to INT_MAX and is culled");
        }

        /* Scale 4096 and centre (32,32) make the expected pixels easy to
         * state.  The batch load is read-only, so the bound Graphics must
         * keep its colour afterwards (the old single-triangle bridge stored
         * it back and leaked the batch colour into Java state). */
        a[0].i = ONE;
        a[1].i = ONE;
        (void)call(vm, fl, FL, "setScale", "(II)V", a, NULL);
        a[0].i = 32;
        a[1].i = 32;
        (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);

        {
            nj_ref img2, g2;
            nj_s4 tv[9] = { 0, 0, 0, 20, 0, 0, 0, 20, 0 };
            nj_s4 tc[1] = { 0x0000FF00 };

            check(target(vm, 64, 64, &img2, &g2), "a fresh 64x64 target for polygons");
            a[0].r = g2;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            verts = ints(vm, tv, 9);
            colors = ints(vm, tc, 1);
            {
                nj_s4 gc_before =
                    field(vm, g, "javax/microedition/lcdui/Graphics", "gColor");

                nj_vm_get_stats(vm, &before);
                st = render(vm, g3, fl, fx, 0x03000400, 1, verts, 0, 0, colors, 0, 0);
                nj_vm_get_stats(vm, &after);
                check(st == NJ_OK,
                      "one independent triangle renders from a primitive count of one");
                check(after.tris == before.tris + 1,
                      "one triangle increments vm->tris once");
                check(pget(vm, img2, 38, 34) == 0xFF00FF00u,
                      "the triangle interior carries the command colour");
                check(pget(vm, img2, 55, 55) == 0xFFFFFFFFu,
                      "the background outside the triangle survives");
                check(field(vm, g, "javax/microedition/lcdui/Graphics", "gColor") ==
                      gc_before,
                      "a render batch leaves the bound Graphics colour untouched");
            }
        }

        {
            nj_ref img3, g3b;
            nj_s4 qv[12] = { 0, 0, 0, 20, 0, 0, 20, 20, 0, 0, 20, 0 };
            nj_s4 qc[1] = { 0x000000FF };

            check(target(vm, 64, 64, &img3, &g3b), "a fresh 64x64 target for a quad");
            a[0].r = g3b;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            verts = ints(vm, qv, 12);
            colors = ints(vm, qc, 1);
            st = render(vm, g3, fl, fx, 0x04000400, 1, verts, 0, 0, colors, 0, 0);
            check(st == NJ_OK, "one quad renders from a primitive count of one");
            check(pget(vm, img3, 36, 36) == 0xFF0000FFu,
                  "the quad's (0,1,3) triangle covers the upper-left half");
            check(pget(vm, img3, 46, 46) == 0xFF0000FFu,
                  "the quad's (1,2,3) triangle covers the lower-right half");
            check(pget(vm, img3, 58, 36) == 0xFFFFFFFFu,
                  "the (0,1,3)/(1,2,3) split leaves the top-right corner empty");
        }

        {
            nj_ref img4, g4;
            nj_s4 pv[18] = {
                0, 0, 0, 10, 0, 0, 10, 10, 0,
                0, 20, 0, 10, 20, 0, 10, 30, 0
            };
            nj_s4 pc[2] = { 0x00FF0000, 0x000000FF };

            check(target(vm, 64, 64, &img4, &g4),
                  "a fresh 64x64 target for per-face colours");
            a[0].r = g4;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            verts = ints(vm, pv, 18);
            colors = ints(vm, pc, 2);
            {
                nj_vm_stats lo_before, lo_after;

                nj_vm_get_stats(vm, &lo_before);
                st = render(vm, g3, fl, fx, 0x03000800, 2, verts, 0, 0, colors, 0, 0);
                nj_vm_get_stats(vm, &lo_after);
                check(st == NJ_OK, "two triangles render with PDATA_COLOR_PER_FACE");
                check(lo_after.gfx_calls == lo_before.gfx_calls + 1,
                      "a whole batch costs one Graphics load, not one load per triangle");
            }
            check(pget(vm, img4, 38, 34) == 0xFFFF0000u,
                  "the first face takes colors[0]");
            check(pget(vm, img4, 36, 54) == 0xFF0000FFu,
                  "the second face takes colors[1], not a repeated colors[0]");
        }

        {
            nj_ref img5, g5;
            nj_s4 lv[6] = { 0, 0, 0, 20, 0, 0 };
            nj_s4 lc[1] = { 0x00FF00FF };

            check(target(vm, 64, 64, &img5, &g5), "a fresh 64x64 target for a line");
            a[0].r = g5;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            verts = ints(vm, lv, 6);
            colors = ints(vm, lc, 1);
            st = render(vm, g3, fl, fx, 0x02000400, 1, verts, 0, 0, colors, 0, 0);
            check(st == NJ_OK, "one line renders");
            check(pget(vm, img5, 41, 32) == 0xFFFF00FFu,
                  "the line pixel is the command colour");
        }

        {
            nj_ref imgc, gc;
            nj_s4 cv[3] = { 0, 0, 0 };
            nj_s4 cc[1] = { 0x00FF0000 };
            nj_ref vp;

            nj_value clip4[4];

            check(target(vm, 64, 64, &imgc, &gc),
                  "a fresh 64x64 target for clip semantics");
            clip4[0].i = 0;
            clip4[1].i = 0;
            clip4[2].i = 8;
            clip4[3].i = 8;
            (void)call(vm, gc, "javax/microedition/lcdui/Graphics", "setClip",
                       "(IIII)V", clip4, NULL);
            a[0].r = gc;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            vp = ints(vm, cv, 3);
            colors = ints(vm, cc, 1);
            st = render(vm, g3, fl, fx, 0x01000400, 1, vp, 0, 0, colors, 0, 0);
            check(st == NJ_OK && pget(vm, imgc, 32, 32) == 0xFFFF0000u,
                  "Micro3D ignores the bound Graphics clip: it paints framebuffer pixels");
        }

        /* Perspective: near=64, far=1000, angle=512 (a half turn), so
         * scale = 0.5/tan(angle/4096*pi) = 1.2071 and kx = 77 for a 64-wide
         * buffer.  Behind-camera and beyond-far points are culled, never
         * clamped to z=1 (which would paint enormous wrong triangles). */
        {
            nj_ref img6, g6;
            nj_s4 nearv[3] = { 0, 0, 512 };
            nj_s4 rightv[3] = { 64, 0, 512 };
            nj_s4 behind[3] = { 0, 0, 32 };
            nj_s4 farv[3] = { 0, 0, 2000 };
            nj_s4 green[1] = { 0x0000FF00 };
            nj_s4 blue[1] = { 0x000000FF };
            nj_s4 red[1] = { 0x00FF0000 };
            nj_ref vn, vr, vb, vf, cg, cb, cr;

            check(target(vm, 64, 64, &img6, &g6),
                  "a fresh 64x64 target for perspective");
            a[0].r = g6;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            a[0].i = 64;
            a[1].i = 1000;
            a[2].i = 512;
            (void)call(vm, fl, FL, "setPerspective", "(III)V", a, NULL);
            a[0].i = 32;
            a[1].i = 32;
            (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);

            vn = ints(vm, nearv, 3);
            vr = ints(vm, rightv, 3);
            vb = ints(vm, behind, 3);
            vf = ints(vm, farv, 3);
            cg = ints(vm, green, 1);
            cb = ints(vm, blue, 1);
            cr = ints(vm, red, 1);
            check(render(vm, g3, fl, fx, 0x01000400, 1, vn, 0, 0, cg, 0, 0) == NJ_OK,
                  "a point at the perspective centre renders");
            check(pget(vm, img6, 32, 32) == 0xFF00FF00u,
                  "z=512 with near=64,far=1000,angle=512 projects to the centre");
            check(render(vm, g3, fl, fx, 0x01000400, 1, vr, 0, 0, cb, 0, 0) == NJ_OK,
                  "a point to the right renders");
            check(pget(vm, img6, 41, 32) == 0xFF0000FFu,
                  "FOV scale is (int)(64*0.5/tan(pi/8))=77: x=(64*77)/512+32=41");
            check(render(vm, g3, fl, fx, 0x01000400, 1, vb, 0, 0, cr, 0, 0) == NJ_OK,
                  "a near-plane point is culled without faulting");
            check(pget(vm, img6, 32, 32) == 0xFF00FF00u,
                  "the culled near point did not overwrite the centre");
            check(render(vm, g3, fl, fx, 0x01000400, 1, vf, 0, 0, cr, 0, 0) == NJ_OK,
                  "a far-plane point is culled without faulting");
            check(pget(vm, img6, 32, 32) == 0xFF00FF00u,
                  "the culled far point did not overwrite the centre");
        }

        {
            nj_ref img7, g7;
            nj_s4 lv[6] = { 0, 0, 32, 0, 0, 512 };
            nj_s4 lc[1] = { 0x00FF00FF };

            check(target(vm, 64, 64, &img7, &g7),
                  "a fresh 64x64 target for near-plane clipping");
            a[0].r = g7;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            verts = ints(vm, lv, 6);
            colors = ints(vm, lc, 1);
            st = render(vm, g3, fl, fx, 0x02000400, 1, verts, 0, 0, colors, 0, 0);
            check(st == NJ_OK, "a line crossing the near plane renders");
            check(pget(vm, img7, 32, 32) == 0xFFFF00FFu,
                  "the near plane clips to a point at the centre instead of exploding at z=1");
        }

        /*
         * A quad with vertex 0 behind the near plane.  The reference splits
         * the quad into (0,1,3) and (1,2,3) and clips each triangle on its
         * own.  Clipping the quad as one polygon and fanning the result
         * would move the diagonal, which HALF blend makes visible on seam
         * pixels - so rendering the quad must be pixel-identical to
         * rendering those two triangles explicitly.
         */
        {
            nj_ref iq, gq, it, gt2, qv, av, bv, qc, got_q, got_t;

            check(target(vm, 64, 64, &iq, &gq) &&
                  target(vm, 64, 64, &it, &gt2),
                  "two 64x64 targets for the clipped-quad comparison");
            a[0].i = 64;
            a[1].i = 1000;
            a[2].i = 512;
            (void)call(vm, fl, FL, "setPerspective", "(III)V", a, NULL);
            a[0].i = 32;
            a[1].i = 32;
            (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);
            a[0].r = gq;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            a[0].i = 1;
            (void)call(vm, fx, FX, "setSemiTransparentEnabled", "(Z)V", a, NULL);

            qv = ints(vm, (const nj_s4[]){ 0, 0, 32, 64, 0, 512,
                                           64, 64, 512, 0, 64, 512 }, 12);
            av = ints(vm, (const nj_s4[]){ 0, 0, 32, 64, 0, 512,
                                           0, 64, 512 }, 9);
            bv = ints(vm, (const nj_s4[]){ 64, 0, 512, 64, 64, 512,
                                           0, 64, 512 }, 9);
            qc = ints(vm, (const nj_s4[]){ 0x0000FF00 }, 1);
            st = render(vm, g3, fl, fx, 0x04000420, 1, qv, 0, 0, qc, 0, 0);
            check(st == NJ_OK, "a near-clipped quad renders through HALF blend");
            got_q = grab(vm, iq, 64, 64);

            a[0].r = gt2;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            st = render(vm, g3, fl, fx, 0x03000420, 1, av, 0, 0, qc, 0, 0);
            check(st == NJ_OK, "its (0,1,3) triangle renders");
            st = render(vm, g3, fl, fx, 0x03000420, 1, bv, 0, 0, qc, 0, 0);
            check(st == NJ_OK, "its (1,2,3) triangle renders");
            got_t = grab(vm, it, 64, 64);
            a[0].i = 0;
            (void)call(vm, fx, FX, "setSemiTransparentEnabled", "(Z)V", a, NULL);

            check(got_q && got_t &&
                  memcmp(nj_array_data(got_q), nj_array_data(got_t),
                         (size_t)(64 * 64 * 4)) == 0,
                  "clipping keeps the quad's (0,1,3)/(1,2,3) diagonal pixel for pixel");
        }

        {
            nj_ref img8, g8, tex, uvs;
            nj_s4 uv[6] = { 0, 0, 4096, 0, 0, 4096 };
            nj_s4 tv[9] = { 0, 0, 0, 20, 0, 0, 0, 20, 0 };
            nj_s4 tc[1] = { 0x0000FF00 };

            check(target(vm, 64, 64, &img8, &g8),
                  "a fresh 64x64 target for a textured command");
            a[0].r = g8;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            tex = make(vm, TEX);
            a[0].i = 8;
            a[1].i = 8;
            (void)call(vm, tex, TEX, "<init>", "(II)V", a, NULL);
            verts = ints(vm, tv, 9);
            colors = ints(vm, tc, 1);
            uvs = ints(vm, uv, 6);
            nj_vm_get_stats(vm, &before);
            st = render_ex(vm, g3, tex, fl, fx, 0x03003400, 1, verts, 0, uvs,
                           colors, 0, 0);
            nj_vm_get_stats(vm, &after);
            check(st == NJ_OK, "a textured triangle is validated");
            check(after.tris == before.tris &&
                  pget(vm, img8, 32, 32) == 0xFFFFFFFFu,
                  "no invented grey: a texture command draws nothing until a sampler exists");
        }

        {
            nj_ref imgl, gl, lig, fxl, verts9, normals9, cl;
            nj_s4 ltv[9] = { 0, 0, 0, 20, 0, 0, 0, 20, 0 };
            nj_s4 lnv[9] = { 0, 0, 4096, 0, 0, 4096, 0, 0, 4096 };
            nj_s4 ltc[1] = { 0x0000FF00 };
            nj_ref dir;
            nj_value a4[4];

            check(target(vm, 64, 64, &imgl, &gl),
                  "a fresh 64x64 target for lighting");
            a[0].r = gl;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            a[0].i = ONE;
            a[1].i = ONE;
            (void)call(vm, fl, FL, "setScale", "(II)V", a, NULL);
            a[0].i = 32;
            a[1].i = 32;
            (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);
            dir = vec(vm, 0, 0, ONE);
            lig = make(vm, LIG);
            a[0].r = dir;
            a[1].i = 255;
            a[2].i = 64;
            (void)call(vm, lig, LIG, "<init>", "(L" V3 ";II)V", a, NULL);
            fxl = make(vm, FX);
            a4[0].r = lig;
            a4[1].i = 255;
            a4[2].i = 0;
            a4[3].r = 0;
            (void)call(vm, fxl, FX, "<init>", "(L" LIG ";IZL" TEX ";)V", a4, NULL);
            verts9 = ints(vm, ltv, 9);
            normals9 = ints(vm, lnv, 9);
            cl = ints(vm, ltc, 1);

            st = render(vm, g3, fl, fxl, 0x03000601, 1, verts9, normals9, 0, cl,
                        0, 0);
            check(st == NJ_OK && pget(vm, imgl, 38, 34) == 0xFFFFFFFFu,
                  "lighting requested: nothing drawn, no flat-colour lie");
            st = render(vm, g3, fl, fxl, 0x03000600, 1, verts9, normals9, 0, cl,
                        0, 0);
            check(st == NJ_OK && pget(vm, imgl, 38, 34) == 0xFF00FF00u,
                  "normals without the lighting flag draw flat, as the reference's unlit path does");
        }

        /* Malformed input must be rejected with the reference's exception,
         * not silently accepted: the old code returned OK for every one of
         * these calls. */
        {
            nj_ref g3u = make(vm, G3D);
            nj_s4 tv[9] = { 0, 0, 0, 20, 0, 0, 0, 20, 0 };
            nj_s4 pc[1] = { 0x00FF0000 };
            nj_s4 uv[6] = { 0, 0, 4096, 0, 0, 4096 };
            nj_s4 big[2304];
            nj_ref v9, v2, c1, u6, tex;
            int i;

            for (i = 0; i < 2304; i++) {
                big[i] = (nj_s4)i;
            }
            v9 = ints(vm, tv, 9);
            v2 = ints(vm, tv, 2);
            c1 = ints(vm, pc, 1);
            u6 = ints(vm, uv, 6);
            verts = ints(vm, big, 2304);
            tex = make(vm, TEX);
            a[0].i = 8;
            a[1].i = 8;
            (void)call(vm, tex, TEX, "<init>", "(II)V", a, NULL);

            st = render(vm, g3, fl, fx, 0x01000400, 0, v9, 0, 0, c1, 0, 0);
            expect_throw(vm, "a primitive count of zero is IllegalArgumentException",
                         st, "IllegalArgumentException");
            st = render(vm, g3, fl, fx, 0x01000400, 256, verts, 0, 0, c1, 0, 0);
            expect_throw(vm, "a primitive count of 256 is IllegalArgumentException",
                         st, "IllegalArgumentException");
            st = render(vm, g3, fl, fx, 0x03010400, 1, v9, 0, 0, c1, 0, 0);
            expect_throw(vm, "a command with a reserved bit set is IllegalArgumentException",
                         st, "IllegalArgumentException");
            st = render(vm, g3, fl, fx, 0x06000400, 1, v9, 0, 0, c1, 0, 0);
            expect_throw(vm, "an unknown primitive type is IllegalArgumentException",
                         st, "IllegalArgumentException");
            st = render(vm, g3, fl, fx, 0x03000C00, 1, v9, 0, 0, c1, 0, 0);
            expect_throw(vm, "PDATA_COLOR_INVALID is IllegalArgumentException",
                         st, "IllegalArgumentException");
            {
                nj_s4 nv[9];
                nj_ref n9;

                for (i = 0; i < 9; i++) {
                    nv[i] = 0;
                }
                n9 = ints(vm, nv, 9);
                st = render(vm, g3, fl, fx, 0x03000500, 1, v9, n9, 0, c1, 0, 0);
                expect_throw(vm, "PDATA_NORMAL_INVALID is IllegalArgumentException",
                             st, "IllegalArgumentException");
            }
            st = render(vm, g3, 0, fx, 0x01000400, 1, v9, 0, 0, c1, 0, 0);
            expect_throw(vm, "a null FigureLayout is NullPointerException",
                         st, "NullPointerException");
            st = render(vm, g3, fl, 0, 0x01000400, 1, v9, 0, 0, c1, 0, 0);
            expect_throw(vm, "a null Effect3D is NullPointerException",
                         st, "NullPointerException");
            st = render(vm, g3, fl, fx, 0x03000800, 1, v9, 0, 0, 0, 0, 0);
            expect_throw(vm, "PDATA_COLOR_PER_FACE without colors is NullPointerException",
                         st, "NullPointerException");
            st = render(vm, g3, fl, fx, 0x01000400, 1, 0, 0, 0, c1, 0, 0);
            expect_throw(vm, "a null vertex array is NullPointerException",
                         st, "NullPointerException");
            st = render(vm, g3, fl, fx, 0x03000400, 1, v2, 0, 0, c1, 0, 0);
            expect_throw(vm, "a vertex array too short for the count is ArrayIndexOutOfBoundsException",
                         st, "ArrayIndexOutOfBoundsException");
            st = render(vm, g3u, fl, fx, 0x01000400, 1, v9, 0, 0, c1, 0, 0);
            expect_throw(vm, "rendering with no bound Graphics is IllegalStateException",
                         st, "IllegalStateException");
            st = render_ex(vm, g3, 0, fl, fx, 0x03003400, 1, v9, 0, u6, c1, 0, 0);
            expect_throw(vm, "a textured command without a Texture is NullPointerException",
                         st, "NullPointerException");
            st = render_ex(vm, g3, tex, fl, fx, 0x03003400, 1, v9, 0, 0, c1, 0, 0);
            expect_throw(vm, "a textured command without texture coordinates is NullPointerException",
                         st, "NullPointerException");
        }

        /* The half-blend material bit is honoured as 128/256 source-over,
         * and the modes nj_gfx cannot express are refused instead of drawn
         * opaque.  The old code painted every one of these opaque. */
        {
            nj_ref img9, g9;
            nj_s4 v[3] = { 128, 128, 0 };
            nj_s4 c[1] = { 0x00FF0000 };
            nj_ref vp;

            check(target(vm, 256, 256, &img9, &g9),
                  "a fresh 256x256 target for blend modes");
            a[0].r = g9;
            (void)call(vm, g3, G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V",
                       a, NULL);
            a[0].i = ONE;
            a[1].i = ONE;
            (void)call(vm, fl, FL, "setScale", "(II)V", a, NULL);
            a[0].i = 0;
            a[1].i = 0;
            (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);
            a[0].i = 1;
            (void)call(vm, fx, FX, "setSemiTransparentEnabled", "(Z)V", a, NULL);
            vp = ints(vm, v, 3);
            colors = ints(vm, c, 1);

            st = render(vm, g3, fl, fx, 0x01000420, 1, vp, 0, 0, colors, 0, 0);
            check(st == NJ_OK, "a HALF-blend point renders");
            check(pget(vm, img9, 128, 128) == 0xFFFF7F7Fu,
                  "HALF draws 128/256 source-over red over white (reference rounds exactly half)");
            st = render(vm, g3, fl, fx, 0x01000440, 1, vp, 0, 0, colors, 0, 0);
            check(st == NJ_OK && pget(vm, img9, 40, 40) == 0xFFFFFFFFu,
                  "ADD blend with transparency enabled draws nothing instead of an opaque lie");
            st = render(vm, g3, fl, fx, 0x01000460, 1, vp, 0, 0, colors, 0, 0);
            check(st == NJ_OK && pget(vm, img9, 40, 40) == 0xFFFFFFFFu,
                  "SUB blend with transparency enabled draws nothing instead of an opaque lie");
            /* Without the effect's transparency flag the blend bits are
             * ignored, exactly as in the reference material conversion. */
            a[0].i = 0;
            (void)call(vm, fx, FX, "setSemiTransparentEnabled", "(Z)V", a, NULL);
            st = render(vm, g3, fl, fx, 0x01000440, 1, vp, 0, 0, colors, 40, 40);
            check(st == NJ_OK, "an ADD command with transparency disabled renders");
            check(pget(vm, img9, 168, 168) == 0xFFFF0000u,
                  "transparency disabled draws the ADD command opaque");
        }

    }

    /*
     * The control: prove that a lookup can fail, or every check above could be
     * passing because lookups always succeed.
     */
    check(nj_object_new(vm, "com/mascotcapsule/micro3d/v3/NoSuchClassAtAll") == 0,
          "control: allocating a class that does not exist returns nothing");
    check(make(vm, V3) != 0, "the classes in this package really are registered");

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

#endif /* NJ_ENABLE_MASCOT */
