/*
 * Mascot Capsule Micro3D v3: Graphics3D.drawCommandList() pixel and error
 * behaviour.
 *
 * Galaxy on Fire's only command list is: version word, ONE LINES command
 * with per-face colours, and END; the queued lines are painted by the next
 * explicit Graphics3D.flush().  This test builds exactly that list, calls
 * both public overloads (Texture[] and single Texture), and reads the pixels
 * back with Image.getRGB().  It also pins the reference's observable error
 * behaviour: NullPointerException for null layout/effect/list,
 * IllegalStateException when nothing is bound, IllegalArgumentException for
 * a wrong version or an unknown command word, and
 * ArrayIndexOutOfBoundsException for a truncated payload.  Commands the
 * first implementation does not draw yet must fail loudly with
 * UnsupportedOperationException instead of painting a wrong picture.
 *
 * An independent control pins the codec against REAL Galaxy on Fire input:1)
 * a 73-word list produced by the game's own class t builder (captured with a
 * reference-JVM harness that drives t.a(bz,Graphics3D,Effect3D)), together
 * with the reference's own decoded line queue, so the offsets/colours/z are
 * not self-confirming.  Second, the queue cap is exercised at its exact
 * boundary (the RAM-switch acceptance): cap primitives queue, the next one
 * throws OutOfMemoryError, flush and release clear the queue.
 *
 * With -DNJ_ENABLE_MASCOT=0 the module is not compiled in; the test prints a
 * loud SKIP rather than passing silently.  With NJ_MASCOT_MAX_QUEUED_TRIANGLES=0
 * (queue disabled builds) it SKIPs as well.
 */

#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_vm.h"
#include "nj2me/nj_config.h"
#include "nj_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !NJ_ENABLE_MASCOT

int main(void)
{
    printf("tests/test_mascot_commands: SKIPPED, this build has NJ_ENABLE_MASCOT=0\n");
    return 0;
}

#else

#define FL  "com/mascotcapsule/micro3d/v3/FigureLayout"
#define TEX "com/mascotcapsule/micro3d/v3/Texture"
#define FX  "com/mascotcapsule/micro3d/v3/Effect3D"
#define G3D "com/mascotcapsule/micro3d/v3/Graphics3D"

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

static int call(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                const char *desc, nj_value *args, nj_value *ret)
{
    nj_status st = nj_call(vm, o, cls, name, desc, args, ret);

    if (st != NJ_OK) {
        printf("  FAIL  %s.%s%s returned status %d\n", cls, name, desc,
               (int)st);
        failures++;
        checks++;
        return 0;
    }
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

/* A white mutable Image plus its Graphics. */
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

static nj_ref ints(nj_vm *vm, const nj_s4 *v, int n)
{
    nj_ref a = nj_int_array_new(vm, (nj_u4)n);

    if (a && v) {
        memcpy(nj_array_data(a), v, (size_t)n * sizeof(nj_s4));
    }
    return a;
}

/* Mirror of the scene queue record declared in src/midp/nj_mascot_scene.inc.
 * The test reads Graphics3D._queue through its raw byte[] payload so the
 * decoded command-list primitives can be compared with the reference
 * implementation's own decoded queue (see the REAL GOF INPUT block). */
typedef struct t_qv {
    nj_s2 x, y;
    nj_u2 u, v;
    nj_s2 light, eu, ev;
} t_qv;
typedef struct t_qprim {
    t_qv v[3];
    nj_s4 z;
    nj_u4 color, toon;
    nj_u2 material, texture, environment;
    nj_u1 kind, pad;
} t_qprim;
typedef char t_qprim_size_check[(sizeof(t_qprim) == 64u) ? 1 : -1];

static int queue_count(nj_vm *vm, nj_ref g3)
{
    nj_field *f = nj_field_lookup_in(g3->cls, "_count", "I", 0);

    return f ? (int)nj_field_read_int_fast(vm, g3, f) : -1;
}

static const unsigned char *queue_bytes(nj_ref g3)
{
    nj_field *f = nj_field_lookup_in(g3->cls, "_queue", "[B", 0);
    nj_ref a;

    if (!f) {
        return NULL;
    }
    a = nj_field_read_ref_fast(NULL, g3, f);
    return a ? (const unsigned char *)nj_array_data(a) : NULL;
}

static void bind_g(nj_vm *vm, nj_ref g3, nj_ref gfx)
{
    nj_value a[1];

    a[0].r = gfx;
    (void)call(vm, g3, G3D, "bind",
               "(Ljavax/microedition/lcdui/Graphics;)V", a, NULL);
}

static void unbind_g(nj_vm *vm, nj_ref g3, nj_ref gfx)
{
    nj_value a[1];

    a[0].r = gfx;
    (void)call(vm, g3, G3D, "release",
               "(Ljavax/microedition/lcdui/Graphics;)V", a, NULL);
}

/* A FigureLayout at 1:1 parallel scale centred at (32,32): a world-space
 * point is then exactly a framebuffer pixel offset from that centre. */
static nj_ref layout_1to1(nj_vm *vm)
{
    nj_ref fl = make(vm, FL);
    nj_value a[2];

    if (!fl) {
        return 0;
    }
    (void)call(vm, fl, FL, "<init>", "()V", NULL, NULL);
    a[0].i = 32;
    a[1].i = 32;
    (void)call(vm, fl, FL, "setCenter", "(II)V", a, NULL);
    a[0].i = ONE;
    a[1].i = ONE;
    (void)call(vm, fl, FL, "setScale", "(II)V", a, NULL);
    return fl;
}

static nj_ref effect_default(nj_vm *vm)
{
    nj_ref fx = make(vm, FX);

    if (fx) {
        (void)call(vm, fx, FX, "<init>", "()V", NULL, NULL);
    }
    return fx;
}

/* Graphics3D.drawCommandList([LTexture;IILFigureLayout;LEffect3D;[I)V */
static nj_status draw_array(nj_vm *vm, nj_ref g3, nj_ref textures, nj_s4 x,
                            nj_s4 y, nj_ref fl, nj_ref fx, nj_ref list)
{
    nj_value a[6];

    memset(a, 0, sizeof a);
    a[0].r = textures;
    a[1].i = x;
    a[2].i = y;
    a[3].r = fl;
    a[4].r = fx;
    a[5].r = list;
    return nj_call(vm, g3, G3D, "drawCommandList",
                   "([L" TEX ";IIL" FL ";L" FX ";[I)V", a, NULL);
}

/* Graphics3D.drawCommandList(LTexture;IILFigureLayout;LEffect3D;[I)V */
static nj_status draw_single(nj_vm *vm, nj_ref g3, nj_ref texture, nj_s4 x,
                             nj_s4 y, nj_ref fl, nj_ref fx, nj_ref list)
{
    nj_value a[6];

    memset(a, 0, sizeof a);
    a[0].r = texture;
    a[1].i = x;
    a[2].i = y;
    a[3].r = fl;
    a[4].r = fx;
    a[5].r = list;
    return nj_call(vm, g3, G3D, "drawCommandList",
                   "(L" TEX ";IIL" FL ";L" FX ";[I)V", a, NULL);
}

static nj_status flush(nj_vm *vm, nj_ref g3)
{
    return nj_call(vm, g3, G3D, "flush", "()V", NULL, NULL);
}

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

#define LINES_CMD(num) \
    ((nj_s4)(0x02000000u | ((nj_u4)(num) << 16) | 0x0800u))

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref img, gfx, g3, fl, fx, list, list2;
    nj_s4 v[24];
    int i;
    nj_status st;

    if (NJ_MASCOT_MAX_QUEUED_TRIANGLES == 0u) {
        printf("tests/test_mascot_commands: SKIPPED, NJ_MASCOT_MAX_QUEUED_TRIANGLES=0 (queue disabled)\n");
        return 0;
    }

    nj_port_headless(&port);
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    if (!vm) {
        printf("tests/test_mascot_commands: could not create a VM\n");
        return 1;
    }

    fl = layout_1to1(vm);
    fx = effect_default(vm);
    g3 = make(vm, G3D);
    if (!fl || !fx || !g3) {
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
        return 1;
    }
    (void)call(vm, g3, G3D, "<init>", "()V", NULL, NULL);
    if (!target(vm, 64, 64, &img, &gfx)) {
        printf("tests/test_mascot_commands: could not create a target Image\n");
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
        return 1;
    }
    {
        nj_value a[1];

        a[0].r = gfx;
        (void)call(vm, g3, G3D, "bind",
                   "(Ljavax/microedition/lcdui/Graphics;)V", a, NULL);
    }

    /*
     * The Galaxy on Fire form: version, two lines with per-face colours
     * (red and green), END.  Per the reference primitive layout there is NO
     * stored offset word: the word right after the command is x0 of the
     * first vertex (Graphics3D.java: "int vtxOffset = idx++;" only steps the
     * cursor past that first word).  Payload is 2*2*3 vertex ints followed
     * by 2 colour ints, so the whole list is 17 words (Galaxy on Fire builds
     * 73 words for 10 lines).  Layout is 1:1 with centre (32,32), so the
     * lines are y=32 and y=42 from x=32 to x=52.
     */
    v[0] = (nj_s4)0xFE000001u;
    v[1] = LINES_CMD(2);
    v[2] = 0;
    v[3] = 0;
    v[4] = 0;
    v[5] = 20;
    v[6] = 0;
    v[7] = 0;
    v[8] = 0;
    v[9] = 10;
    v[10] = 0;
    v[11] = 20;
    v[12] = 10;
    v[13] = 0;
    v[14] = 0x00FF0000; /* red, low 24 bits are the colour */
    v[15] = 0x0000FF00; /* green */
    v[16] = (nj_s4)0x80000000u;
    list = ints(vm, v, 17);
    check(list != 0, "a Galaxy-on-Fire-style command list can be allocated");

    st = draw_array(vm, g3, 0, 0, 0, fl, fx, list);
    if (st != NJ_OK) {
        printf("  DEBUG first draw status %d exception: %s\n", (int)st,
               nj_exception_describe(vm));
    }
    check(st == NJ_OK, "drawCommandList(Texture[],...) accepts the GoF list (null Texture[])");
    check(pget(vm, img, 40, 32) == 0xFFFFFFFFu,
          "END does not flush: nothing is painted before Graphics3D.flush()");
    st = flush(vm, g3);
    check(st == NJ_OK, "the queued command-list lines flush");
    check(pget(vm, img, 40, 32) == 0xFFFF0000u,
          "the first line's per-face red colour reaches the framebuffer");
    check(pget(vm, img, 45, 42) == 0xFF00FF00u,
          "the second line's per-face green colour reaches the framebuffer");
    check(pget(vm, img, 40, 52) == 0xFFFFFFFFu,
          "untouched pixels stay white (no invented geometry)");

    /* The single-Texture overload must interpret the same list identically. */
    {
        nj_value a[1];

        a[0].r = gfx;
        (void)call(vm, g3, G3D, "release",
                   "(Ljavax/microedition/lcdui/Graphics;)V", a, NULL);
        if (!target(vm, 64, 64, &img, &gfx)) {
            check(0, "second target Image");
        } else {
            a[0].r = gfx;
            (void)call(vm, g3, G3D, "bind",
                       "(Ljavax/microedition/lcdui/Graphics;)V", a, NULL);
        }
    }
    st = draw_single(vm, g3, 0, 0, 0, fl, fx, list);
    check(st == NJ_OK, "drawCommandList(Texture,...) accepts the same list (null Texture)");
    st = flush(vm, g3);
    check(st == NJ_OK && pget(vm, img, 40, 32) == 0xFFFF0000u,
          "the single-Texture overload paints the same red line");

    /*
     * REAL GOF INPUT / INDEPENDENT REFERENCE CONTROL
     * ----------------------------------------------
     * Captured from the real game: a JVM harness loads class t from
     * tests/games/浴火银河_10189.jar, runs its own t.a(bz,Graphics3D,Effect3D)
     * builder against an instrumented FreeJ2ME-Plus Graphics3D and records
     * both the produced int[] and the reference's decoded line queue
     * (primData: sortZ, colour<<8|header, x1,y1,x2,y2).  The array below is
     * therefore GoF's own output: 0xFE000001, LINES/PER_FACE with 10 lines,
     * 60 vertex words, 10 colours 0x00FFFF19, END.  Harness + log live in
     * .local-tools/debug-runs/micro3d-round7-20261022/commands/oracle/
     * (CaptureT.java, harness/run/capture.log).  The expected pixels use the
     * reference's default FigureLayout: parallel scale 512/512, centre 0,0,
     * so x_fb = x_world/8 (integer floor) and y_fb = y_world/8.
     */
    if (NJ_MASCOT_MAX_QUEUED_TRIANGLES >= 10u) {
        static const nj_s4 gof[73] = {
            (nj_s4)0xFE000001u, (nj_s4)0x020A0800u,
            40, 20, 0, 40, 20, 0,
            40, 20, 0, 48, 24, 0,
            40, 20, 0, 56, 28, 0,
            40, 20, 0, 64, 32, 0,
            40, 20, 0, 72, 36, 0,
            40, 20, 0, 80, 40, 0,
            40, 20, 0, 88, 44, 0,
            40, 20, 0, 96, 48, 0,
            40, 20, 0, 104, 52, 0,
            40, 20, 0, 112, 56, 0,
            16776985, 16776985, 16776985, 16776985, 16776985,
            16776985, 16776985, 16776985, 16776985, 16776985,
            (nj_s4)0x80000000u
        };
        /* Reference decoded queue from the capture, line order preserved. */
        static const nj_s4 refxy[10][4] = {
            { 5, 2, 5, 2 }, { 5, 2, 6, 3 }, { 5, 2, 7, 3 },
            { 5, 2, 8, 4 }, { 5, 2, 9, 4 }, { 5, 2, 10, 5 },
            { 5, 2, 11, 5 }, { 5, 2, 12, 6 }, { 5, 2, 13, 6 },
            { 5, 2, 14, 7 }
        };
        nj_ref gof_list = ints(vm, gof, 73);
        nj_ref dflt = make(vm, FL);
        const unsigned char *qb;
        int qok = 1, i2;

        (void)call(vm, dflt, FL, "<init>", "()V", NULL, NULL);
        unbind_g(vm, g3, gfx);
        if (!target(vm, 64, 64, &img, &gfx)) {
            check(0, "real-GoF target Image");
        } else {
            bind_g(vm, g3, gfx);
        }
        st = draw_array(vm, g3, 0, 0, 0, dflt, fx, gof_list);
        check(st == NJ_OK, "the real GoF command list is accepted");
        check(queue_count(vm, g3) == 10,
              "the real GoF list queues exactly its 10 lines");
        check(pget(vm, img, 5, 2) == 0xFFFFFFFFu,
              "the real GoF list does not paint before flush");
        qb = queue_bytes(g3);
        if (!qb) {
            qok = 0;
        } else {
            for (i2 = 0; i2 < 10; i2++) {
                t_qprim q;

                memcpy(&q, qb + (size_t)i2 * sizeof q, sizeof q);
                if (q.kind != 2 || q.z != 0 ||
                    q.color != 0xFFFFFF19u ||
                    q.v[0].x != refxy[i2][0] || q.v[0].y != refxy[i2][1] ||
                    q.v[1].x != refxy[i2][2] || q.v[1].y != refxy[i2][3]) {
                    qok = 0;
                }
            }
        }
        check(qok, "decoded lines match the reference queue (x,y,z,colour,kind)");
        st = flush(vm, g3);
        check(st == NJ_OK, "the real GoF lines flush");
        check(pget(vm, img, 5, 2) == 0xFFFFFF19u,
              "real GoF line start pixel has the game's colour");
        check(pget(vm, img, 14, 7) == 0xFFFFFF19u,
              "real GoF line end pixel has the game's colour");
        check(pget(vm, img, 40, 40) == 0xFFFFFFFFu,
              "the real GoF list paints nothing outside its ten lines");
    } else {
        printf("  note  queue cap < 10: skipping the real-GoF 10-line control\n");
    }

    /*
     * Validation that a game can observe.  Those calls must fail without
     * painting anything, and without leaving an exception behind for the
     * next check.
     */
    {
        nj_s4 bad_version[2] = { 0x12345678, (nj_s4)0x80000000u };
        nj_ref bad = ints(vm, bad_version, 2);

        expect_throw(vm, "wrong command-list version is IllegalArgumentException",
                     draw_array(vm, g3, 0, 0, 0, fl, fx, bad),
                     "IllegalArgumentException");
    }
    expect_throw(vm, "null FigureLayout is NullPointerException",
                 draw_array(vm, g3, 0, 0, 0, 0, fx, list),
                 "NullPointerException");
    expect_throw(vm, "null Effect3D is NullPointerException",
                 draw_array(vm, g3, 0, 0, 0, fl, 0, list),
                 "NullPointerException");
    expect_throw(vm, "null command list is NullPointerException",
                 draw_array(vm, g3, 0, 0, 0, fl, fx, 0),
                 "NullPointerException");
    {
        nj_ref unbound = make(vm, G3D);

        (void)call(vm, unbound, G3D, "<init>", "()V", NULL, NULL);
        expect_throw(vm, "drawCommandList without a bound Graphics is IllegalStateException",
                     draw_array(vm, unbound, 0, 0, 0, fl, fx, list),
                     "IllegalStateException");
    }
    {
        nj_s4 unknown[2] = { (nj_s4)0xFE000001u, 0x00000000 };

        expect_throw(vm, "an unknown command word is IllegalArgumentException",
                     draw_array(vm, g3, 0, 0, 0, fl, fx,
                                ints(vm, unknown, 2)),
                     "IllegalArgumentException");
    }
    {
        nj_s4 unsupported[2] = { (nj_s4)0xFE000001u, (nj_s4)0x85000000u };

        expect_throw(vm, "a known but unimplemented command fails loudly",
                     draw_array(vm, g3, 0, 0, 0, fl, fx,
                                ints(vm, unsupported, 2)),
                     "UnsupportedOperationException");
    }
    {
        /* LINES claiming 2 lines but the list ends before the payload. */
        nj_s4 short_list[5] = { (nj_s4)0xFE000001u, LINES_CMD(2), 0, 0, 0 };

        expect_throw(vm, "a truncated primitive payload is ArrayIndexOutOfBoundsException",
                     draw_array(vm, g3, 0, 0, 0, fl, fx,
                                ints(vm, short_list, 5)),
                     "ArrayIndexOutOfBoundsException");
    }
    {
        /* No colour data at all: the reference submitLines() draws nothing.
         * Real layout: version, LINES(1), 6 vertex ints, END (no colours). */
        nj_s4 nocolor[9];

        nocolor[0] = (nj_s4)0xFE000001u;
        nocolor[1] = (nj_s4)(0x02000000u | (1u << 16));
        for (i = 2; i < 8; i++) {
            nocolor[i] = 0;
        }
        nocolor[8] = (nj_s4)0x80000000u;
        list2 = ints(vm, nocolor, 9);
        st = draw_array(vm, g3, 0, 0, 0, fl, fx, list2);
        check(st == NJ_OK, "a LINES command without colour data is accepted");
        (void)flush(vm, g3);
        check(pget(vm, img, 40, 52) == 0xFFFFFFFFu &&
              pget(vm, img, 60, 60) == 0xFFFFFFFFu,
              "no colour means no draw (no extra pixels appear)");
    }
    {
        /* Invalid colour type 0x0C00 is IllegalArgumentException. */
        nj_s4 bad_color[4] = { (nj_s4)0xFE000001u,
                               (nj_s4)(0x02000000u | (1u << 16) | 0x0C00u),
                               0, 0 };

        expect_throw(vm, "an invalid colour type is IllegalArgumentException",
                     draw_array(vm, g3, 0, 0, 0, fl, fx,
                                ints(vm, bad_color, 4)),
                     "IllegalArgumentException");
    }

    /*
     * QUEUE CAP RAM-SWITCH ACCEPTANCE
     * -------------------------------
     * Fill the Graphics3D queue with legal LINES batches up to exactly
     * NJ_MASCOT_MAX_QUEUED_TRIANGLES primitives: every one of those calls
     * must succeed.  The next queued line must throw OutOfMemoryError
     * without corrupting the queue (count unchanged), and flush/release must
     * clear it.  Batched at 255 lines per command (the maximum a command
     * word can carry) so a large default cap does not mean thousands of
     * Java calls.
     */
    {
        const int cap = (int)NJ_MASCOT_MAX_QUEUED_TRIANGLES;
        nj_s4 one[10];
        nj_s4 *batch;
        int left = cap, all_ok = 1;
        nj_ref cap_list;

        /* one degenerate line: version, LINES(1), xyz*2, colour, END */
        one[0] = (nj_s4)0xFE000001u;
        one[1] = LINES_CMD(1);
        one[2] = one[3] = one[4] = one[5] = one[6] = one[7] = 0;
        one[8] = 0x00FFFFFF;
        one[9] = (nj_s4)0x80000000u;
        cap_list = ints(vm, one, 10);
        batch = (nj_s4 *)malloc(1788u * sizeof(nj_s4));

        if (!cap_list || !batch) {
            check(0, "queue-cap test buffers");
        } else {
            while (left > 0 && all_ok) {
                int n = left > 255 ? 255 : left;

                batch[0] = (nj_s4)0xFE000001u;
                batch[1] = LINES_CMD(n);
                {
                    int w = 2, k;

                    for (k = 0; k < n; k++) {
                        batch[w++] = 0;
                        batch[w++] = 0;
                        batch[w++] = 0;
                        batch[w++] = 0;
                        batch[w++] = 0;
                        batch[w++] = 0;
                    }
                    for (k = 0; k < n; k++) {
                        batch[w++] = 0x00FFFFFF;
                    }
                    batch[w++] = (nj_s4)0x80000000u;
                    cap_list = ints(vm, batch, w);
                }
                if (!cap_list) {
                    all_ok = 0;
                    break;
                }
                if (draw_array(vm, g3, 0, 0, 0, fl, fx, cap_list) != NJ_OK) {
                    all_ok = 0;
                }
                left -= n;
            }
            free(batch);
            check(all_ok, "exactly NJ_MASCOT_MAX_QUEUED_TRIANGLES primitives can be queued");
            check(queue_count(vm, g3) == cap,
                  "the queue holds exactly the configured cap");
            if (all_ok) {
                nj_ref again = ints(vm, one, 10);

                expect_throw(vm, "the primitive past the queue cap is OutOfMemoryError",
                             draw_array(vm, g3, 0, 0, 0, fl, fx, again),
                             "OutOfMemoryError");
                check(queue_count(vm, g3) == cap,
                      "a rejected primitive does not overrun or corrupt the queue");
                st = flush(vm, g3);
                check(st == NJ_OK, "flushing a completely full queue succeeds");
                check(queue_count(vm, g3) == 0,
                      "flush clears the queue");
                st = draw_array(vm, g3, 0, 0, 0, fl, fx, again);
                check(st == NJ_OK && queue_count(vm, g3) == 1,
                      "the queue accepts new primitives after a flush");
                unbind_g(vm, g3, gfx);
                check(queue_count(vm, g3) == 0,
                      "release clears the queue");
            }
        }
    }

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

#endif /* NJ_ENABLE_MASCOT */
