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

#include <stdio.h>
#include <string.h>
#include <math.h>

static int checks;
static int failures;

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
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
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
            check(n.i == 0, "no projection yet: getProjection returns 0");

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
         * The static constants are looked up but not read back: this test has
         * no safe way to read a static field, and doing it the obvious way
         * (nj_field_read_int with a NULL receiver) dereferences obj + offset
         * and dies.
         */
        check(nj_field_lookup(vm, CLS("Light"), "SPOT", "I", 1) != NULL,
              "Light.SPOT is declared as a static field");
    }

    nj_vm_destroy(vm);
    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("FAILED (%d failures)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
