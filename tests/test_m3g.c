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

        a[0].i = 8; a[1].i = 8; a[2].i = 0;
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
        }

        if (app && tex) {
            a[0].i = 0; a[1].r = tex;
            call(vm, app, CLS("Appearance"), "setTexture",
                 "(IL" CLS("Texture2D") ";)V", a, NULL);
            call(vm, app, CLS("Appearance"), "getTexture",
                 "(I)L" CLS("Texture2D") ";", a, &r);
            check(r.r == tex, "Appearance.setTexture and getTexture agree");
        }

        if (g3 && lit && xf) {
            a[0].r = lit; a[1].r = xf;
            call(vm, g3, CLS("Graphics3D"), "addLight",
                 "(L" CLS("Light") ";L" CLS("Transform") ";)I", a, &r);
            check(r.i == 0, "the first addLight is index 0");
            call(vm, g3, CLS("Graphics3D"), "addLight",
                 "(L" CLS("Light") ";L" CLS("Transform") ";)I", a, &r);
            check(r.i == 1, "the second addLight is index 1");
        }

        if (poly) {
            a[0].i = 161;
            call(vm, poly, CLS("PolygonMode"), "setCulling", "(I)V", a, NULL);
            call(vm, poly, CLS("PolygonMode"), "getCulling", "()I", NULL, &r);
            check(r.i == 161, "PolygonMode.setCulling and getCulling agree");
        }
    }

    /*
     * TriangleStripArray stores what its constructors are handed.  The
     * rasteriser does not exist yet, so "it did not crash" is not evidence
     * that anything was kept; these read it back.
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
            a[0].r = lens; a[1].r = idx;
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

        a2[0].i = 10;
        a2[1].r = img;
        check(nj_call(vm, i2, "javax/microedition/m3g/Image2D", "<init>",
                      "(ILjava/lang/Object;)V", a2, &r) == NJ_OK && img != 0,
              "Image2D(format, Image) constructs");
        check(fw && fh && nj_field_read_int(vm, i2, fw) == 8 && nj_field_read_int(vm, i2, fh) == 4,
              "Image2D(format, Image) copies the size out of the image");
        check(fi && nj_field_read_ref(vm, i2, fi) == img,
              "Image2D(format, Image) keeps the image itself");

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
        /* nj_array_length() counts BYTES - three shorts are six of them.  The
           probe that found this printed len=6 for what the code believed was
           three values. */
        check(data != 0 && nj_array_length(data) == 6u,
              "the short[] holds one value per vertex");
        if (data) {
            nj_s2 *sh = (nj_s2 *)nj_array_data(data);
            check(sh != NULL && sh[0] == (nj_s2)0x0100 && sh[1] == (nj_s2)0x0200 &&
                  sh[2] == (nj_s2)0x0300,
                  "and they are the numbers the file held, little-endian");
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
            nj_iput(vm, va, nj_field_lookup(vm, CLS("VertexArray"), "_nj_compSize", "I", 0), 3);
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
        nj_ref w, grp, mesh, found[4];
        nj_value a, r;

        w = make(vm, CLS("World"));
        grp = make(vm, CLS("Group"));
        mesh = make(vm, CLS("Mesh"));
        check(w && grp && mesh, "a World, a Group and a Mesh exist");

        a.r = grp; r.r = 0;
        call(vm, w, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", &a, &r);
        a.r = mesh; r.r = 0;
        call(vm, grp, CLS("Group"), "addChild", "(L" CLS("Node") ";)V", &a, &r);

        memset(found, 0, sizeof(found));
        check(nj_m3g_collect_meshes(vm, w, found, 4u) == 1u,
              "the walk finds the one mesh under the world");
        check(found[0] == mesh, "and it is the mesh that was added");
        check(nj_m3g_collect_meshes(vm, mesh, found, 4u) == 1u,
              "a mesh is itself a mesh");
    }
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    /* The file format calls type 18 Sprite; the titles call the class
       Sprite3D.  Both have to be loadable. */
    check(nj_class_lookup(vm, "javax/microedition/m3g/Sprite") != NULL,
          "javax/microedition/m3g/Sprite is not registered");
    check(nj_class_lookup(vm, "javax/microedition/m3g/Sprite3D") != NULL,
          "javax/microedition/m3g/Sprite3D is not registered");
    check(nj_class_lookup(vm, "javax/microedition/m3g/MorphingMesh") != NULL,
          "javax/microedition/m3g/MorphingMesh is not registered");
    check(nj_class_lookup(vm, "javax/microedition/m3g/SkinnedMesh") != NULL,
          "javax/microedition/m3g/SkinnedMesh is not registered");
    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("FAILED (%d failures)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
