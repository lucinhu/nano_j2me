/*
 * Mascot Capsule Micro3D v3: the arithmetic, and the state the classes keep.
 *
 * There is no rasterizer in that module, so this test does not look at pixels.
 * It checks the two things that ARE real and that a game can observe through
 * its own fields: the fixed-point maths (rotations, concatenation, look-at,
 * normalisation, the integer square root) and the values the remaining classes
 * store and hand back.  A module that accepted every call and stored nothing
 * would fail almost every assertion below.
 *
 * With -DNJ_ENABLE_MASCOT=0 the module is trimmed out and this prints a loud
 * SKIP rather than passing silently: a silent pass would hide the module
 * missing from a build that was supposed to have it.
 */

#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_vm.h"
#include "nj2me/nj_config.h"

#include <stdio.h>
#include <string.h>

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
    nj_ref a, b, r;
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
        args[0].i = 60; args[1].i = 100; args[2].i = 1000;
        (void)call(vm, fl, FL, "setPerspective", "(III)V", args, &ret);
        check(field(vm, fl, FL, "_p") == 60 && field(vm, fl, FL, "_near") == 100 &&
              field(vm, fl, FL, "_far") == 1000, "FigureLayout keeps its perspective");

        args[0].i = 120; args[1].i = 160;
        (void)call(vm, fl, FL, "setCenter", "(II)V", args, &ret);
        (void)call(vm, fl, FL, "setScale", "(II)V", args, &ret);
        check(field(vm, fl, FL, "_cx") == 120 && field(vm, fl, FL, "_cy") == 160 &&
              field(vm, fl, FL, "_sx") == 120 && field(vm, fl, FL, "_sy") == 160,
              "FigureLayout keeps its center and scale");

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
