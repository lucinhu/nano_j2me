/*
 * nano_j2me - tests/test_game.c
 *
 * Behavioural test for javax.microedition.lcdui.game, driving the builtin
 * classes exactly the way the interpreter does: nj_object_new + nj_call /
 * nj_call_special, with real Images, real Graphics state and the real
 * rasterizer.
 *
 * What it proves, section by section:
 *   1. Sprite frame grids, frame sequences, validation
 *   2. the reference-pixel arithmetic for all eight transforms (table driven)
 *   3. Sprite.paint() lands each frame pixel where that table says it does
 *   4. pixel-level collision, including the exact transition column
 *   5. TiledLayer cells, animated tiles, clipping to the visible cells
 *   6. LayerManager ordering, z-order, view-window translation and culling
 *   7. GameCanvas buffer, getKeyStates() latching, flushGraphics() blitting
 *
 * It also enforces the project's memory contract: the whole suite runs three
 * times, each time in a brand new VM, and nj_vm_destroy() must report the same
 * number of released host blocks every round - so create -> run -> destroy ->
 * create leaves nothing behind.
 *
 * Usage:  test_game
 */
#include "nj2me/nj.h"
#include "nj_internal.h"   /* the VM struct: repaint_pending, screen surface   */
#include "nj_midp.h"       /* the frozen class / field ABI of the MIDP modules */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Assertions                                                          */
/* ------------------------------------------------------------------ */
static int g_checks;
static int g_failures;

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_failures++;                                                 \
            fprintf(stderr, "FAIL line %d: ", __LINE__);                  \
            fprintf(stderr, __VA_ARGS__);                                 \
            fputc('\n', stderr);                                          \
        }                                                                 \
    } while (0)

/* ------------------------------------------------------------------ */
/* Descriptors and colours                                             */
/* ------------------------------------------------------------------ */
#define D_INT   "I"
#define D_BOOL  "Z"
#define D_IMAGE "Ljavax/microedition/lcdui/Image;"
#define D_GFX   "Ljavax/microedition/lcdui/Graphics;"
#define D_LAYER "Ljavax/microedition/lcdui/game/Layer;"
#define D_SPR   "Ljavax/microedition/lcdui/game/Sprite;"
#define D_TILED "Ljavax/microedition/lcdui/game/TiledLayer;"

/*
 * Colours whose components survive an RGB565 round trip exactly, so the same
 * expectations hold whether the port's panel is RGB565 or ARGB8888.
 */
#define C_RED   0xFFFF0000u
#define C_GREEN 0xFF00FF00u
#define C_BLUE  0xFF0000FFu
#define C_WHITE 0xFFFFFFFFu
#define C_SENT  0xFF402060u   /* untouched-pixel sentinel */
#define C_TRANS 0x00000000u

/* ------------------------------------------------------------------ */
/* Field access and calling into Java                                  */
/* ------------------------------------------------------------------ */
static nj_s4 iget(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                  const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void iput(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                 const char *desc, nj_s4 v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static nj_ref rget(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                   const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void rput(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                 const char *desc, nj_ref v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

/* A call that was expected to succeed: report the exception if it did not. */
static void report_call(nj_vm *vm, const char *what)
{
    const char *d = nj_exception_describe(vm);
    g_checks++;
    g_failures++;
    fprintf(stderr, "FAIL: %s raised %s\n", what, d ? d : "(nothing)");
    nj_clear_exception(vm);
}

static nj_s4 i0(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                const char *desc)
{
    nj_value r;
    r.i = 0;
    if (nj_call(vm, o, cls, name, desc, NULL, &r) != NJ_OK) {
        report_call(vm, name);
        return 0;
    }
    return r.i;
}

static nj_s4 i1(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                const char *desc, nj_s4 a0)
{
    nj_value a[1], r;
    a[0].i = a0;
    r.i = 0;
    if (nj_call(vm, o, cls, name, desc, a, &r) != NJ_OK) {
        report_call(vm, name);
        return 0;
    }
    return r.i;
}

static nj_s4 i2(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                const char *desc, nj_s4 a0, nj_s4 a1)
{
    nj_value a[2], r;
    a[0].i = a0;
    a[1].i = a1;
    r.i = 0;
    if (nj_call(vm, o, cls, name, desc, a, &r) != NJ_OK) {
        report_call(vm, name);
        return 0;
    }
    return r.i;
}

static nj_ref ref1(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                   const char *desc, nj_s4 a0)
{
    nj_value a[1], r;
    a[0].i = a0;
    r.r = NULL;
    if (nj_call(vm, o, cls, name, desc, a, &r) != NJ_OK) {
        report_call(vm, name);
        return NULL;
    }
    return r.r;
}

static void v0(nj_vm *vm, nj_ref o, const char *cls, const char *name,
               const char *desc)
{
    if (nj_call(vm, o, cls, name, desc, NULL, NULL) != NJ_OK) {
        report_call(vm, name);
    }
}

static void v1(nj_vm *vm, nj_ref o, const char *cls, const char *name,
               const char *desc, nj_s4 a0)
{
    nj_value a[1];
    a[0].i = a0;
    if (nj_call(vm, o, cls, name, desc, a, NULL) != NJ_OK) {
        report_call(vm, name);
    }
}

static void v2(nj_vm *vm, nj_ref o, const char *cls, const char *name,
               const char *desc, nj_s4 a0, nj_s4 a1)
{
    nj_value a[2];
    a[0].i = a0; a[1].i = a1;
    if (nj_call(vm, o, cls, name, desc, a, NULL) != NJ_OK) {
        report_call(vm, name);
    }
}

static void v3(nj_vm *vm, nj_ref o, const char *cls, const char *name,
               const char *desc, nj_s4 a0, nj_s4 a1, nj_s4 a2)
{
    nj_value a[3];
    a[0].i = a0; a[1].i = a1; a[2].i = a2;
    if (nj_call(vm, o, cls, name, desc, a, NULL) != NJ_OK) {
        report_call(vm, name);
    }
}

static void v4(nj_vm *vm, nj_ref o, const char *cls, const char *name,
               const char *desc, nj_s4 a0, nj_s4 a1, nj_s4 a2, nj_s4 a3)
{
    nj_value a[4];
    a[0].i = a0; a[1].i = a1; a[2].i = a2; a[3].i = a3;
    if (nj_call(vm, o, cls, name, desc, a, NULL) != NJ_OK) {
        report_call(vm, name);
    }
}

/* Constructor with one object argument: <init>(LImage;)V and friends. */
static nj_ref ctor_ref1(nj_vm *vm, nj_ref obj, const char *cls,
                        const char *desc, nj_ref arg)
{
    nj_value a[1];
    a[0].r = arg;
    /*
     * A constructor returns void: the VM does not hand `this` back, so the
     * object the caller created is what comes out.  (Reading the return slot
     * here would be reading a union whose upper half the VM does not clear.)
     */
    if (nj_call_special(vm, obj, cls, "<init>", desc, a, NULL) != NJ_OK) {
        report_call(vm, "<init>");
        return NULL;
    }
    return obj;
}

/* Expect a Java exception whose description mentions `cls_part`. */
static void expect_throw(nj_vm *vm, nj_status st, const char *cls_part,
                         const char *what)
{
    const char *d;

    g_checks++;
    if (st == NJ_OK) {
        g_failures++;
        fprintf(stderr, "FAIL: %s did not throw (expected %s)\n", what,
                cls_part);
        nj_clear_exception(vm);
        return;
    }
    d = nj_exception_describe(vm);
    if (!d || !strstr(d, cls_part)) {
        g_failures++;
        fprintf(stderr, "FAIL: %s threw \"%s\", expected %s\n", what,
                d ? d : "(none)", cls_part);
    }
    nj_clear_exception(vm);
}

/* Calls that are expected to fail: the status is handed to expect_throw. */
static nj_status try_init_image(nj_vm *vm, const char *cls, const char *img_desc,
                                nj_ref img)
{
    nj_ref o = nj_object_new(vm, cls);
    nj_value a[1];
    a[0].r = img;
    if (!o) {
        return NJ_ERR;
    }
    return nj_call_special(vm, o, cls, "<init>", img_desc, a, NULL);
}

static nj_status try_sprite_frames(nj_vm *vm, nj_ref img, int fw, int fh)
{
    nj_ref o = nj_object_new(vm, NJ_C_SPRITE);
    nj_value a[3];
    a[0].r = img; a[1].i = fw; a[2].i = fh;
    if (!o) {
        return NJ_ERR;
    }
    return nj_call_special(vm, o, NJ_C_SPRITE, "<init>", "(" D_IMAGE "II)V",
                           a, NULL);
}

static nj_status try_tiled(nj_vm *vm, int cols, int rows, nj_ref img, int tw,
                           int th)
{
    nj_ref o = nj_object_new(vm, NJ_C_TILEDLAYER);
    nj_value a[5];
    if (!o) {
        return NJ_ERR;
    }
    a[0].i = cols; a[1].i = rows; a[2].r = img; a[3].i = tw; a[4].i = th;
    return nj_call_special(vm, o, NJ_C_TILEDLAYER, "<init>",
                           "(II" D_IMAGE "II)V", a, NULL);
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */
static nj_ref mk_image_fill(nj_vm *vm, int w, int h, nj_u4 argb)
{
    nj_u4 *px = (nj_u4 *)malloc((size_t)w * (size_t)h * sizeof(nj_u4));
    nj_ref img;
    int i;

    if (!px) {
        return NULL;
    }
    for (i = 0; i < w * h; i++) {
        px[i] = argb;
    }
    img = nj_midp_image_adopt(vm, px, w, h);
    free(px);
    return img;
}

/* A 4x2 grid of 16x16 frames in a 64x32 image; frame k is flat coloured. */
static nj_ref mk_frame_grid(nj_vm *vm)
{
    nj_u4 *px = (nj_u4 *)malloc(64u * 32u * sizeof(nj_u4));
    nj_ref img;
    int x, y;

    if (!px) {
        return NULL;
    }
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 64; x++) {
            int k = (y / 16) * 4 + (x / 16);
            px[y * 64 + x] = 0xFF000000u | ((nj_u4)(k + 1) * 0x00111111u);
        }
    }
    img = nj_midp_image_adopt(vm, px, 64, 32);
    free(px);
    return img;
}

/* A transparent image with a single opaque pixel at (ox,oy). */
static nj_ref mk_dot_image(nj_vm *vm, int w, int h, int ox, int oy, nj_u4 argb)
{
    nj_u4 *px = (nj_u4 *)malloc((size_t)w * (size_t)h * sizeof(nj_u4));
    nj_ref img;
    int i;

    if (!px) {
        return NULL;
    }
    for (i = 0; i < w * h; i++) {
        px[i] = C_TRANS;
    }
    px[oy * w + ox] = argb;
    img = nj_midp_image_adopt(vm, px, w, h);
    free(px);
    return img;
}

/* A 16x8 atlas of two opaque 8x8 tiles: tile 1 red, tile 2 blue. */
static nj_ref mk_tile_atlas(nj_vm *vm)
{
    nj_u4 *px = (nj_u4 *)malloc(16u * 8u * sizeof(nj_u4));
    nj_ref img;
    int x, y;

    if (!px) {
        return NULL;
    }
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 16; x++) {
            px[y * 16 + x] = (x < 8) ? C_RED : C_BLUE;
        }
    }
    img = nj_midp_image_adopt(vm, px, 16, 8);
    free(px);
    return img;
}

static nj_u4 *img_px(nj_vm *vm, nj_ref img)
{
    return nj_midp_image_pixels(vm, img);
}

static nj_u4 px_at(nj_vm *vm, nj_ref img, int x, int y)
{
    nj_u4 *p = img_px(vm, img);
    int w = 0, h = 0;

    if (!p || !nj_midp_image_size(vm, img, &w, &h)) {
        return 0u;
    }
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return 0u;
    }
    return p[(nj_size)y * w + x];
}

static void px_set(nj_vm *vm, nj_ref img, int x, int y, nj_u4 argb)
{
    nj_u4 *p = img_px(vm, img);
    int w = 0, h = 0;

    if (!p || !nj_midp_image_size(vm, img, &w, &h)) {
        return;
    }
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return;
    }
    p[(nj_size)y * w + x] = argb;
}

static void img_wipe(nj_vm *vm, nj_ref img, nj_u4 argb)
{
    nj_u4 *p = img_px(vm, img);
    int w = 0, h = 0, i;

    if (!p || !nj_midp_image_size(vm, img, &w, &h)) {
        return;
    }
    for (i = 0; i < w * h; i++) {
        p[i] = argb;
    }
}

/* Pixels that are neither the sentinel nor fully transparent. */
static int count_painted(nj_vm *vm, nj_ref img)
{
    nj_u4 *p = img_px(vm, img);
    int w = 0, h = 0, i, n = 0;

    if (!p || !nj_midp_image_size(vm, img, &w, &h)) {
        return -1;
    }
    for (i = 0; i < w * h; i++) {
        if (p[i] != C_SENT && ((p[i] >> 24) & 0xFFu) != 0u) {
            n++;
        }
    }
    return n;
}

/* A Graphics with complete Java state (the LCDUI layout, built by hand). */
static nj_ref mk_graphics(nj_vm *vm, nj_ref target, int tx, int ty,
                          int cx, int cy, int cw, int ch)
{
    nj_ref g = nj_object_new(vm, NJ_C_GRAPHICS);

    if (!g) {
        return NULL;
    }
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT, tx);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT, ty);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT, cx);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT, cy);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT, cw);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT, ch);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_COLOR, D_INT, (nj_s4)0xFF000000u);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_BLEND, D_INT, NJ_BLEND_SRC_OVER);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_STROKE, D_INT, NJ_STROKE_SOLID);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_XOR, D_INT, 0);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_XORCOLOR, D_INT, 0);
    if (target) {
        rput(vm, g, NJ_C_GRAPHICS, NJ_F_G_TARGET, D_IMAGE, target);
    }
    return g;
}

static nj_ref mk_sprite(nj_vm *vm, nj_ref img)
{
    nj_ref s = nj_object_new(vm, NJ_C_SPRITE);

    if (!s) {
        return NULL;
    }
    if (!ctor_ref1(vm, s, NJ_C_SPRITE, "(" D_IMAGE ")V", img)) {
        return NULL;
    }
    return s;
}

static nj_ref mk_sprite_frames(nj_vm *vm, nj_ref img, int fw, int fh)
{
    nj_ref s = nj_object_new(vm, NJ_C_SPRITE);
    nj_value a[3], r;

    if (!s) {
        return NULL;
    }
    a[0].r = img; a[1].i = fw; a[2].i = fh;
    r.r = NULL;
    if (nj_call_special(vm, s, NJ_C_SPRITE, "<init>", "(" D_IMAGE "II)V",
                        a, &r) != NJ_OK) {
        report_call(vm, "Sprite(Image,int,int)");
        return NULL;
    }
    return s;
}

static nj_ref mk_tiled(nj_vm *vm, int cols, int rows, nj_ref img, int tw, int th)
{
    nj_ref t = nj_object_new(vm, NJ_C_TILEDLAYER);
    nj_value a[5], r;

    if (!t) {
        return NULL;
    }
    a[0].i = cols; a[1].i = rows; a[2].r = img; a[3].i = tw; a[4].i = th;
    r.r = NULL;
    if (nj_call_special(vm, t, NJ_C_TILEDLAYER, "<init>", "(II" D_IMAGE "II)V",
                        a, &r) != NJ_OK) {
        report_call(vm, "TiledLayer(...)");
        return NULL;
    }
    return t;
}

static nj_ref mk_manager(nj_vm *vm)
{
    nj_ref m = nj_object_new(vm, NJ_C_LAYERMANAGER);

    if (!m) {
        return NULL;
    }
    if (nj_call_special(vm, m, NJ_C_LAYERMANAGER, "<init>", "()V", NULL, NULL)
        != NJ_OK) {
        report_call(vm, "LayerManager()");
        return NULL;
    }
    return m;
}

static nj_ref mk_gamecanvas(nj_vm *vm, int suppress)
{
    nj_ref c = nj_object_new(vm, NJ_C_GAMECANVAS);
    nj_value a[1];

    if (!c) {
        return NULL;
    }
    a[0].i = suppress;
    if (nj_call_special(vm, c, NJ_C_GAMECANVAS, "<init>", "(Z)V", a, NULL)
        != NJ_OK) {
        report_call(vm, "GameCanvas(boolean)");
        return NULL;
    }
    return c;
}

/* Layered call: manager.paint(graphics, x, y). */
static void lm_paint_at(nj_vm *vm, nj_ref m, nj_ref g, int x, int y)
{
    nj_value a[3];
    a[0].r = g; a[1].i = x; a[2].i = y;
    if (nj_call(vm, m, NJ_C_LAYERMANAGER, "paint", "(" D_GFX "II)V", a, NULL)
        != NJ_OK) {
        report_call(vm, "LayerManager.paint");
    }
}

/* Layered call: sprite.paint(graphics). */
static void sprite_paint(nj_vm *vm, nj_ref s, nj_ref g)
{
    nj_value a[1];
    a[0].r = g;
    if (nj_call(vm, s, NJ_C_SPRITE, "paint", "(" D_GFX ")V", a, NULL)
        != NJ_OK) {
        report_call(vm, "Sprite.paint");
    }
}

/* Layered call: tiled.paint(graphics). */
static void tiled_paint(nj_vm *vm, nj_ref t, nj_ref g)
{
    nj_value a[1];
    a[0].r = g;
    if (nj_call(vm, t, NJ_C_TILEDLAYER, "paint", "(" D_GFX ")V", a, NULL)
        != NJ_OK) {
        report_call(vm, "TiledLayer.paint");
    }
}

/* ------------------------------------------------------------------ */
/* The transform expectation table                                     */
/* ------------------------------------------------------------------ */
/*
 * For each transform: where untransformed frame pixel (3,2) appears in the
 * transformed frame, and where reference pixel (5,2) appears in a 16x16 frame.
 * Derived from the MIDP definition ("mirror about the vertical centre, then
 * rotate clockwise") independently of the implementation.
 */
typedef struct trans_case {
    int trans;
    int vx, vy;
    int rx, ry;
} trans_case;

static const trans_case TRANS_TABLE[] = {
    { NJ_TRANS_NONE,           3,  2,  5,  2 },
    { NJ_TRANS_ROT90,         13,  3, 13,  5 },
    { NJ_TRANS_ROT180,        12, 13, 10, 13 },
    { NJ_TRANS_ROT270,         2, 12,  2, 10 },
    { NJ_TRANS_MIRROR,        12,  2, 10,  2 },
    { NJ_TRANS_MIRROR_ROT90,  13, 12, 13, 10 },
    { NJ_TRANS_MIRROR_ROT180,  3, 13,  5, 13 },
    { NJ_TRANS_MIRROR_ROT270,  2,  3,  2,  5 }
};
#define NTRANS ((int)(sizeof(TRANS_TABLE) / sizeof(TRANS_TABLE[0])))

/* ------------------------------------------------------------------ */
/* Field lookup by content, not by pointer                             */
/* ------------------------------------------------------------------ */
static void test_field_lookup_memo(nj_vm *vm)
{
    /*
     * nj_field_lookup() has to answer by comparing the STRINGS, even though
     * nj_class_find() remembers classes by the address of the name a call site
     * hands it (that cache verifies with strcmp for exactly this reason).  A
     * name built at run time, equal but somewhere else in memory, must find the
     * same field; a different descriptor must not; and a static request must
     * not be answered with an instance field.
     */
    char name[8];
    char desc[4];
    nj_field *lit = nj_field_lookup(vm, "java/lang/Integer", "value", "I", 0);
    nj_field *built;

    CHECK(lit != NULL, "the literal lookup finds Integer.value");
    memcpy(name, "value", 6u);
    memcpy(desc, "I", 2u);
    built = nj_field_lookup(vm, "java/lang/Integer", name, desc, 0);
    CHECK(built == lit, "a name built at run time finds the same field");
    CHECK(nj_field_lookup(vm, "java/lang/Integer", "value", "I", 0) == lit,
          "the second lookup answers the field the linker resolved");
    CHECK(nj_field_lookup(vm, "java/lang/Integer", "value", "J", 0) == NULL,
          "a different descriptor finds no field");
    CHECK(nj_field_lookup(vm, "java/lang/Integer", "value", "I", 1) == NULL,
          "a static request is not answered with an instance field");
}

/* ------------------------------------------------------------------ */
/* 1. Sprite frames and sequences                                      */
/* ------------------------------------------------------------------ */
static void test_sprite_frames(nj_vm *vm)
{
    nj_ref grid = mk_frame_grid(vm);
    nj_ref s, s2, copy;

    CHECK(grid != NULL, "frame grid image");
    if (!grid) {
        return;
    }

    /* Sprite(Image): one frame the size of the whole image. */
    s = mk_sprite(vm, grid);
    CHECK(s != NULL, "Sprite(Image)");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getRawFrameCount", "()I") == 1,
          "Sprite(Image).getRawFrameCount() == 1");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 0,
          "Sprite(Image).getFrame() == 0");
    CHECK(i0(vm, s, NJ_C_LAYER, "getWidth", "()I") == 64,
          "Sprite(Image).getWidth() == 64");
    CHECK(i0(vm, s, NJ_C_LAYER, "getHeight", "()I") == 32,
          "Sprite(Image).getHeight() == 32");
    CHECK(rget(vm, s, NJ_C_SPRITE, NJ_F_SP_IMAGE, D_IMAGE) == grid,
          "getImage() returns the source image");
    expect_throw(vm, try_init_image(vm, NJ_C_SPRITE, "(" D_IMAGE ")V", NULL),
                 "NullPointerException", "Sprite(null)");

    /* Sprite(Image,16,16) over the 4x2 grid. */
    s = mk_sprite_frames(vm, grid, 16, 16);
    CHECK(s != NULL, "Sprite(Image,16,16)");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getRawFrameCount", "()I") == 8,
          "raw frame count == 8 (4 columns x 2 rows)");
    CHECK(iget(vm, s, NJ_C_SPRITE, NJ_F_SP_COLS, D_INT) == 4,
          "frame columns == 4");
    CHECK(i0(vm, s, NJ_C_LAYER, "getWidth", "()I") == 16, "frame width 16");
    CHECK(i0(vm, s, NJ_C_LAYER, "getHeight", "()I") == 16, "frame height 16");
    CHECK(i0(vm, s, NJ_C_LAYER, "getX", "()I") == 0, "initial x == 0");
    CHECK(i0(vm, s, NJ_C_LAYER, "isVisible", "()Z") == 1, "visible by default");

    /* setFrame / getFrame. */
    v1(vm, s, NJ_C_SPRITE, "setFrame", "(I)V", 5);
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 5, "setFrame(5)");
    CHECK(iget(vm, s, NJ_C_SPRITE, NJ_F_SP_FRAME, D_INT) == 5,
          "the raw frame follows the default sequence");

    /* nextFrame / prevFrame wrap around. */
    v0(vm, s, NJ_C_SPRITE, "nextFrame", "()V");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 6, "nextFrame -> 6");
    v1(vm, s, NJ_C_SPRITE, "setFrame", "(I)V", 7);
    v0(vm, s, NJ_C_SPRITE, "nextFrame", "()V");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 0,
          "nextFrame wraps 7 -> 0");
    v0(vm, s, NJ_C_SPRITE, "prevFrame", "()V");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 7,
          "prevFrame wraps 0 -> 7");

    /* A custom frame sequence: the sequence index is not the frame index. */
    {
        static const nj_s4 seq[4] = { 3, 1, 3, 0 };
        nj_ref arr = nj_int_array_new(vm, 4u);
        nj_value a[1];
        int k;
        for (k = 0; k < 4; k++) {
            nj_array_set_int(arr, (nj_u4)k, seq[k]);
        }
        a[0].r = arr;
        if (nj_call(vm, s, NJ_C_SPRITE, "setFrameSequence", "([I)V", a, NULL)
            != NJ_OK) {
            report_call(vm, "setFrameSequence");
        }
    }
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrameSequenceLength", "()I") == 4,
          "custom sequence length == 4");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getRawFrameCount", "()I") == 8,
          "the raw frame count is unaffected by the sequence");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 0,
          "setFrameSequence resets the sequence index");
    v0(vm, s, NJ_C_SPRITE, "nextFrame", "()V");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 1,
          "sequence index after nextFrame");
    CHECK(iget(vm, s, NJ_C_SPRITE, NJ_F_SP_FRAME, D_INT) == 1,
          "displayed raw frame == sequence[1]");
    v1(vm, s, NJ_C_SPRITE, "setFrame", "(I)V", 2);
    CHECK(iget(vm, s, NJ_C_SPRITE, NJ_F_SP_FRAME, D_INT) == 3,
          "displayed raw frame == sequence[2] == 3");
    v0(vm, s, NJ_C_SPRITE, "nextFrame", "()V");
    v0(vm, s, NJ_C_SPRITE, "nextFrame", "()V");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 0,
          "the custom sequence wraps around");

    /* Sequence validation and reversion to the default sequence. */
    {
        nj_value a[1];
        nj_ref bad = nj_int_array_new(vm, 2u);
        nj_status st;
        nj_array_set_int(bad, 0u, 0);
        nj_array_set_int(bad, 1u, 8);
        a[0].r = bad;
        st = nj_call(vm, s, NJ_C_SPRITE, "setFrameSequence", "([I)V", a, NULL);
        expect_throw(vm, st, "ArrayIndexOutOfBoundsException",
                     "setFrameSequence with frame 8 of 8");
        a[0].r = nj_int_array_new(vm, 0u);
        st = nj_call(vm, s, NJ_C_SPRITE, "setFrameSequence", "([I)V", a, NULL);
        expect_throw(vm, st, "IllegalArgumentException", "empty sequence");
        a[0].r = NULL;
        st = nj_call(vm, s, NJ_C_SPRITE, "setFrameSequence", "([I)V", a, NULL);
        CHECK(st == NJ_OK, "setFrameSequence(null) reverts to the default");
        CHECK(i0(vm, s, NJ_C_SPRITE, "getFrameSequenceLength", "()I") == 8,
              "default sequence length == raw frame count");
        CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 0,
              "the default sequence starts at 0");
    }

    /* The additive int-returning twins live next to the MIDP void versions. */
    v1(vm, s, NJ_C_SPRITE, "setFrame", "(I)V", 0);
    CHECK(i0(vm, s, NJ_C_SPRITE, "nextFrame", "()I") == 1,
          "nextFrame()I returns the new sequence index");
    CHECK(i0(vm, s, NJ_C_SPRITE, "prevFrame", "()I") == 0,
          "prevFrame()I returns the new sequence index");
    CHECK(i0(vm, s, NJ_C_SPRITE, "getFrame", "()I") == 0,
          "the int twins leave the same state as the void ones");

    /* Out-of-range frame indices. */
    expect_throw(vm, nj_call(vm, s, NJ_C_SPRITE, "setFrame", "(I)V",
                             &(nj_value){ .i = 9 }, NULL),
                 "IndexOutOfBoundsException", "setFrame(9)");
    expect_throw(vm, nj_call(vm, s, NJ_C_SPRITE, "setFrame", "(I)V",
                             &(nj_value){ .i = -1 }, NULL),
                 "IndexOutOfBoundsException", "setFrame(-1)");

    /* The image must divide evenly into frames. */
    expect_throw(vm, try_sprite_frames(vm, grid, 10, 16),
                 "IllegalArgumentException", "Sprite(Image,10,16)");
    expect_throw(vm, try_sprite_frames(vm, grid, 16, 0),
                 "IllegalArgumentException", "Sprite(Image,16,0)");
    expect_throw(vm, try_init_image(vm, NJ_C_SPRITE, "(" D_IMAGE ")V", NULL),
                 "NullPointerException", "Sprite(null image)");

    /* The copy constructor duplicates every attribute. */
    s2 = mk_sprite_frames(vm, grid, 16, 16);
    v1(vm, s2, NJ_C_SPRITE, "setFrame", "(I)V", 6);
    v2(vm, s2, NJ_C_LAYER, "setPosition", "(II)V", 11, 13);
    v1(vm, s2, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_ROT90);
    v4(vm, s2, NJ_C_SPRITE, "defineCollisionRectangle", "(IIII)V", 2, 1, 9, 7);
    copy = nj_object_new(vm, NJ_C_SPRITE);
    CHECK(ctor_ref1(vm, copy, NJ_C_SPRITE, "(" D_SPR ")V", s2) != NULL,
          "Sprite(Sprite)");
    CHECK(i0(vm, copy, NJ_C_SPRITE, "getRawFrameCount", "()I") == 8,
          "copy: raw frame count");
    CHECK(i0(vm, copy, NJ_C_SPRITE, "getFrame", "()I") == 6, "copy: frame");
    CHECK(i0(vm, copy, NJ_C_SPRITE, "getTransform", "()I") == NJ_TRANS_ROT90,
          "copy: transform");
    CHECK(i0(vm, copy, NJ_C_LAYER, "getX", "()I") ==
          i0(vm, s2, NJ_C_LAYER, "getX", "()I"), "copy: x");
    CHECK(i0(vm, copy, NJ_C_LAYER, "getY", "()I") ==
          i0(vm, s2, NJ_C_LAYER, "getY", "()I"), "copy: y");
    CHECK(rget(vm, copy, NJ_C_SPRITE, NJ_F_SP_IMAGE, D_IMAGE) == grid,
          "copy: image");
    CHECK(iget(vm, copy, NJ_C_SPRITE, "spColW", D_INT) == 9,
          "copy: collision rectangle");
    expect_throw(vm, try_init_image(vm, NJ_C_SPRITE, "(" D_SPR ")V", NULL),
                 "NullPointerException", "Sprite((Sprite)null)");
}

/* ------------------------------------------------------------------ */
/* 2. Reference pixel arithmetic, all eight transforms                 */
/* ------------------------------------------------------------------ */
static void test_reference_pixel(nj_vm *vm)
{
    nj_ref grid = mk_frame_grid(vm);
    nj_ref s = mk_sprite_frames(vm, grid, 16, 16);
    int k;

    if (!s) {
        CHECK(0, "sprite for the reference pixel test");
        return;
    }

    /*
     * setTransform -> defineReferencePixel -> setPosition: the painter-space
     * location of the reference pixel is the position plus the transformed
     * offset of the defined pixel.
     */
    for (k = 0; k < NTRANS; k++) {
        const trans_case *t = &TRANS_TABLE[k];
        int rx, ry;

        v1(vm, s, NJ_C_SPRITE, "setTransform", "(I)V", t->trans);
        v2(vm, s, NJ_C_SPRITE, "defineReferencePixel", "(II)V", 5, 2);
        v2(vm, s, NJ_C_LAYER, "setPosition", "(II)V", 100, 50);

        rx = i0(vm, s, NJ_C_SPRITE, "getRefPixelX", "()I");
        ry = i0(vm, s, NJ_C_SPRITE, "getRefPixelY", "()I");
        CHECK(rx == 100 + t->rx, "transform %d: getRefPixelX %d != %d",
              t->trans, rx, 100 + t->rx);
        CHECK(ry == 50 + t->ry, "transform %d: getRefPixelY %d != %d",
              t->trans, ry, 50 + t->ry);
        CHECK(iget(vm, s, NJ_C_SPRITE, NJ_F_SP_REFX, D_INT) == t->rx,
              "transform %d: spRefX is the rotated-space offset", t->trans);
        CHECK(iget(vm, s, NJ_C_SPRITE, NJ_F_SP_REFY, D_INT) == t->ry,
              "transform %d: spRefY is the rotated-space offset", t->trans);

        /* setPosition() positions the visual bounds, not the reference pixel. */
        CHECK(i0(vm, s, NJ_C_LAYER, "getX", "()I") == 100,
              "transform %d: setPosition sets getX", t->trans);
        CHECK(i0(vm, s, NJ_C_LAYER, "getY", "()I") == 50,
              "transform %d: setPosition sets getY", t->trans);

        /* setRefPixelPosition() is the inverse: it moves the layer. */
        v2(vm, s, NJ_C_SPRITE, "setRefPixelPosition", "(II)V", 7, 9);
        CHECK(i0(vm, s, NJ_C_SPRITE, "getRefPixelX", "()I") == 7,
              "transform %d: setRefPixelPosition x", t->trans);
        CHECK(i0(vm, s, NJ_C_SPRITE, "getRefPixelY", "()I") == 9,
              "transform %d: setRefPixelPosition y", t->trans);
        CHECK(i0(vm, s, NJ_C_LAYER, "getX", "()I") == 7 - t->rx,
              "transform %d: layer x follows the reference pixel", t->trans);
        CHECK(i0(vm, s, NJ_C_LAYER, "getY", "()I") == 9 - t->ry,
              "transform %d: layer y follows the reference pixel", t->trans);

        /* move() shifts the reference pixel with the sprite. */
        v2(vm, s, NJ_C_LAYER, "move", "(II)V", 2, 3);
        CHECK(i0(vm, s, NJ_C_SPRITE, "getRefPixelX", "()I") == 9,
              "transform %d: move x", t->trans);
        CHECK(i0(vm, s, NJ_C_SPRITE, "getRefPixelY", "()I") == 12,
              "transform %d: move y", t->trans);
    }

    /*
     * Changing the transform must leave the reference pixel where it is on
     * screen ("the reference pixel effectively becomes the center of the
     * transform"); getX/getY may change.
     */
    v1(vm, s, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_NONE);
    v2(vm, s, NJ_C_SPRITE, "defineReferencePixel", "(II)V", 5, 2);
    v2(vm, s, NJ_C_LAYER, "setPosition", "(II)V", 100, 50);
    CHECK(i0(vm, s, NJ_C_SPRITE, "getRefPixelX", "()I") == 105,
          "reference pixel before the transform sweep");
    for (k = 0; k < NTRANS; k++) {
        const trans_case *t = &TRANS_TABLE[k];
        v1(vm, s, NJ_C_SPRITE, "setTransform", "(I)V", t->trans);
        CHECK(i0(vm, s, NJ_C_SPRITE, "getRefPixelX", "()I") == 105,
              "transform %d keeps the reference pixel x", t->trans);
        CHECK(i0(vm, s, NJ_C_SPRITE, "getRefPixelY", "()I") == 52,
              "transform %d keeps the reference pixel y", t->trans);
        CHECK(i0(vm, s, NJ_C_LAYER, "getX", "()I") == 105 - t->rx,
              "transform %d repositions getX", t->trans);
        CHECK(i0(vm, s, NJ_C_LAYER, "getY", "()I") == 52 - t->ry,
              "transform %d repositions getY", t->trans);
    }

    /* Defining the reference pixel must not move the sprite. */
    v1(vm, s, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_ROT90);
    v2(vm, s, NJ_C_LAYER, "setPosition", "(II)V", 40, 40);
    v2(vm, s, NJ_C_SPRITE, "defineReferencePixel", "(II)V", 1, 1);
    CHECK(i0(vm, s, NJ_C_LAYER, "getX", "()I") == 40 &&
          i0(vm, s, NJ_C_LAYER, "getY", "()I") == 40,
          "defineReferencePixel leaves getX/getY alone");

    /* Invalid transforms are rejected. */
    expect_throw(vm, nj_call(vm, s, NJ_C_SPRITE, "setTransform", "(I)V",
                             &(nj_value){ .i = 9 }, NULL),
                 "IllegalArgumentException", "setTransform(9)");

    /* getWidth/getHeight swap for the 90/270 rotations. */
    {
        nj_ref narrow = mk_image_fill(vm, 32, 16, C_RED);
        nj_ref ns = mk_sprite_frames(vm, narrow, 16, 8);
        CHECK(i0(vm, ns, NJ_C_LAYER, "getWidth", "()I") == 16,
              "16x8 frame width");
        CHECK(i0(vm, ns, NJ_C_LAYER, "getHeight", "()I") == 8,
              "16x8 frame height");
        v1(vm, ns, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_ROT90);
        CHECK(i0(vm, ns, NJ_C_LAYER, "getWidth", "()I") == 8,
              "ROT90 swaps width");
        CHECK(i0(vm, ns, NJ_C_LAYER, "getHeight", "()I") == 16,
              "ROT90 swaps height");
        v1(vm, ns, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_MIRROR_ROT270);
        CHECK(i0(vm, ns, NJ_C_LAYER, "getWidth", "()I") == 8,
              "MIRROR_ROT270 swaps width");
        v1(vm, ns, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_MIRROR);
        CHECK(i0(vm, ns, NJ_C_LAYER, "getWidth", "()I") == 16,
              "MIRROR keeps the width");
    }
}

/* ------------------------------------------------------------------ */
/* 3. Sprite.paint() places pixels exactly where the transform says    */
/* ------------------------------------------------------------------ */
static void test_sprite_paint(nj_vm *vm)
{
    nj_ref dot = mk_dot_image(vm, 16, 16, 3, 2, C_RED);
    nj_ref s = mk_sprite_frames(vm, dot, 16, 16);
    nj_ref target = mk_image_fill(vm, 40, 60, C_SENT);
    nj_ref g = mk_graphics(vm, target, 0, 0, 0, 0, 40, 60);
    int k;

    if (!s || !target || !g) {
        CHECK(0, "sprite paint fixtures");
        return;
    }

    for (k = 0; k < NTRANS; k++) {
        const trans_case *t = &TRANS_TABLE[k];
        nj_u4 got;

        img_wipe(vm, target, C_SENT);
        v1(vm, s, NJ_C_SPRITE, "setTransform", "(I)V", t->trans);
        v2(vm, s, NJ_C_LAYER, "setPosition", "(II)V", 20, 30);
        sprite_paint(vm, s, g);

        got = px_at(vm, target, 20 + t->vx, 30 + t->vy);
        CHECK(got == C_RED,
              "transform %d: frame pixel (3,2) must land at (%d,%d), got %08lx",
              t->trans, 20 + t->vx, 30 + t->vy, (unsigned long)got);
        CHECK(count_painted(vm, target) == 1,
              "transform %d: exactly one pixel painted, got %d", t->trans,
              count_painted(vm, target));
        CHECK(px_at(vm, target, 20, 30) == C_SENT,
              "transform %d: the visual origin pixel is untouched", t->trans);
    }

    /* The Graphics translate shifts the sprite. */
    img_wipe(vm, target, C_SENT);
    v1(vm, s, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_NONE);
    v2(vm, s, NJ_C_LAYER, "setPosition", "(II)V", 20, 30);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT, 5);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT, 7);
    sprite_paint(vm, s, g);
    CHECK(px_at(vm, target, 28, 39) == C_RED,
          "paint honours the Graphics translate");
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT, 0);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT, 0);

    /* The Graphics clip (stored in user space) cuts the sprite off. */
    img_wipe(vm, target, C_SENT);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT, 0);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT, 0);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT, 25);   /* excludes x >= 25 */
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT, 60);
    sprite_paint(vm, s, g);
    CHECK(px_at(vm, target, 23, 32) == C_RED, "the clip keeps the pixel inside");
    CHECK(count_painted(vm, target) == 1, "the clip does not leak");
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT, 40);

    /* An invisible sprite paints nothing. */
    img_wipe(vm, target, C_SENT);
    v1(vm, s, NJ_C_LAYER, "setVisible", "(Z)V", 0);
    sprite_paint(vm, s, g);
    CHECK(count_painted(vm, target) == 0, "an invisible sprite paints nothing");
    v1(vm, s, NJ_C_LAYER, "setVisible", "(Z)V", 1);

    /* paint(null) is a NullPointerException. */
    expect_throw(vm, nj_call(vm, s, NJ_C_SPRITE, "paint", "(" D_GFX ")V",
                             &(nj_value){ .r = NULL }, NULL),
                 "NullPointerException", "Sprite.paint(null)");

    /*
     * Layer.paint is abstract and has no native binding: a subclass that
     * forgets to override it must fail loudly instead of drawing nothing.
     */
    {
        nj_ref bare = nj_object_new(vm, NJ_C_LAYER);
        nj_value a[1];
        nj_status st;
        const char *d;
        a[0].r = g;
        st = nj_call(vm, bare, NJ_C_LAYER, "paint", "(" D_GFX ")V", a, NULL);
        g_checks++;
        if (st == NJ_OK) {
            g_failures++;
            fprintf(stderr, "FAIL: Layer.paint() returned normally\n");
        } else {
            d = nj_exception_describe(vm);
            if (!d || !strstr(d, "Error")) {
                g_failures++;
                fprintf(stderr, "FAIL: Layer.paint() raised \"%s\"\n",
                        d ? d : "(none)");
            }
            nj_clear_exception(vm);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 4. Pixel-level collision                                            */
/* ------------------------------------------------------------------ */
static void test_collision(nj_vm *vm)
{
    nj_ref img_a = mk_dot_image(vm, 16, 16, 0, 0, C_RED);
    nj_ref img_b = mk_dot_image(vm, 16, 16, 15, 15, C_BLUE);
    nj_ref a = mk_sprite_frames(vm, img_a, 16, 16);
    nj_ref b = mk_sprite_frames(vm, img_b, 16, 16);
    nj_value ar[2], r;
    int d;

    if (!a || !b) {
        CHECK(0, "collision fixtures");
        return;
    }
    v2(vm, a, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);

    /* Bounding boxes overlap, opaque pixels do not. */
    v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
    ar[0].r = b; ar[1].i = 0;
    r.i = 0;
    CHECK(nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "Z)Z",
                  ar, &r) == NJ_OK && r.i == 1,
          "non-pixel collision uses the bounding boxes");
    ar[1].i = 1;
    r.i = 0;
    CHECK(nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "Z)Z",
                  ar, &r) == NJ_OK && r.i == 0,
          "pixel collision: overlapping boxes, disjoint pixels");

    /*
     * Slide B along x with y fixed at -15: B's only opaque pixel is at frame
     * (15,15), A's at (0,0), so the first pixel-level hit is exactly
     * B == (-15,-15).
     */
    for (d = 0; d >= -16; d--) {
        int expect = (d == -15) ? 1 : 0;
        ar[1].i = 1;
        v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", d, -15);
        r.i = -1;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "Z)Z",
                      ar, &r);
        CHECK(r.i == expect, "collision at B=(%d,-15) is %d, expected %d",
              d, r.i, expect);
    }
    /* The same on the vertical axis. */
    for (d = 0; d >= -16; d--) {
        int expect = (d == -15) ? 1 : 0;
        ar[1].i = 1;
        v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", -15, d);
        r.i = -1;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "Z)Z",
                      ar, &r);
        CHECK(r.i == expect, "collision at B=(-15,%d) is %d, expected %d",
              d, r.i, expect);
    }

    /* The transform is applied before the pixel comparison: ROT180 moves
     * B's opaque pixel from frame (15,15) to visual (0,0). */
    v1(vm, b, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_ROT180);
    v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
    ar[1].i = 1;
    r.i = 0;
    (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "Z)Z", ar, &r);
    CHECK(r.i == 1, "ROT180 moves B's opaque pixel onto A's");
    v1(vm, b, NJ_C_SPRITE, "setTransform", "(I)V", NJ_TRANS_NONE);

    /* The collision rectangle restricts which pixels count. */
    v4(vm, a, NJ_C_SPRITE, "defineCollisionRectangle", "(IIII)V", 4, 4, 8, 8);
    v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", 2, 2);
    ar[1].i = 1;
    r.i = 1;
    (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "Z)Z", ar, &r);
    CHECK(r.i == 0, "the collision rectangle excludes A's only opaque pixel");
    ar[1].i = 0;
    r.i = 0;
    (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "Z)Z", ar, &r);
    CHECK(r.i == 1, "the non-pixel test still uses the rectangles");
    v4(vm, a, NJ_C_SPRITE, "defineCollisionRectangle", "(IIII)V", 0, 0, 16, 16);
    expect_throw(vm, nj_call(vm, a, NJ_C_SPRITE, "defineCollisionRectangle",
                             "(IIII)V",
                             (nj_value[]){ {.i = 0}, {.i = 0}, {.i = -1},
                                           {.i = 4} }, NULL),
                 "IllegalArgumentException", "negative collision rectangle");

    /* Visibility gates the test. */
    v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", -15, -15);
    v1(vm, b, NJ_C_LAYER, "setVisible", "(Z)V", 0);
    ar[1].i = 1;
    r.i = 1;
    (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "Z)Z", ar, &r);
    CHECK(r.i == 0, "an invisible sprite never collides");
    v1(vm, b, NJ_C_LAYER, "setVisible", "(Z)V", 1);

    /* collidesWith(Image,x,y,pixelLevel). */
    {
        nj_ref probe = mk_dot_image(vm, 4, 4, 3, 3, C_GREEN);
        nj_value ia[4];
        v2(vm, a, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
        ia[0].r = probe; ia[1].i = -3; ia[2].i = -3; ia[3].i = 1;
        r.i = 0;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_IMAGE "IIZ)Z",
                      ia, &r);
        CHECK(r.i == 1, "collidesWith(Image) at the pixel hit");
        ia[1].i = -2;
        r.i = 1;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_IMAGE "IIZ)Z",
                      ia, &r);
        CHECK(r.i == 0, "collidesWith(Image) one pixel away is a miss");
        ia[1].i = -3; ia[3].i = 0;
        r.i = 0;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_IMAGE "IIZ)Z",
                      ia, &r);
        CHECK(r.i == 1, "collidesWith(Image) bounds test");
    }

    /*
     * The additive reference-pixel overload: B's reference pixel is its dot,
     * so lining the two reference pixels up makes the dots coincide.
     */
    {
        nj_value ra[3];
        v2(vm, a, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
        v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", 40, 40);
        v2(vm, b, NJ_C_SPRITE, "defineReferencePixel", "(II)V", 15, 15);
        ra[0].r = b; ra[1].i = 1; ra[2].i = 1;
        r.i = 0;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "ZZ)Z",
                      ra, &r);
        CHECK(r.i == 1,
              "collidesWith(Sprite,true,true) aligns the reference pixels");
        ra[1].i = 0;
        r.i = 0;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "ZZ)Z",
                      ra, &r);
        CHECK(r.i == 1, "aligned bounding boxes overlap");
        ra[2].i = 0;
        r.i = 1;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_SPR "ZZ)Z",
                      ra, &r);
        CHECK(r.i == 0, "without alignment the sprites are far apart");
    }

    /* The additive generic-Layer dispatch, in both arities. */
    {
        nj_value la[2], lz[3];
        la[0].r = b; la[1].i = 1;
        v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", -15, -15);
        r.i = 0;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_LAYER "Z)Z",
                      la, &r);
        CHECK(r.i == 1, "collidesWith(Layer,true) dispatches to the Sprite test");
        lz[0].r = b; lz[1].i = 1; lz[2].i = 0;
        r.i = 0;
        (void)nj_call(vm, a, NJ_C_SPRITE, "collidesWith", "(" D_LAYER "ZZ)Z",
                      lz, &r);
        CHECK(r.i == 1, "collidesWith(Layer,true,false) dispatches too");
    }
}

/* ------------------------------------------------------------------ */
/* 5. TiledLayer                                                       */
/* ------------------------------------------------------------------ */
static void test_tiledlayer(nj_vm *vm)
{
    nj_ref atlas = mk_tile_atlas(vm);
    nj_ref t = mk_tiled(vm, 3, 2, atlas, 8, 8);
    nj_ref target, g;
    int anim;

    if (!t) {
        CHECK(0, "TiledLayer fixtures");
        return;
    }
    CHECK(i0(vm, t, NJ_C_TILEDLAYER, "getColumns", "()I") == 3, "columns");
    CHECK(i0(vm, t, NJ_C_TILEDLAYER, "getRows", "()I") == 2, "rows");
    CHECK(i0(vm, t, NJ_C_TILEDLAYER, "getCellWidth", "()I") == 8, "cell width");
    CHECK(i0(vm, t, NJ_C_TILEDLAYER, "getCellHeight", "()I") == 8, "cell height");
    CHECK(i0(vm, t, NJ_C_LAYER, "getWidth", "()I") == 24, "layer width");
    CHECK(i0(vm, t, NJ_C_LAYER, "getHeight", "()I") == 16, "layer height");
    CHECK(i2(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I", 1, 1) == 0,
          "cells start empty");

    /* Validation of the constructor. */
    expect_throw(vm, try_tiled(vm, 0, 2, atlas, 8, 8),
                 "IllegalArgumentException", "TiledLayer with 0 columns");
    expect_throw(vm, try_tiled(vm, 3, 2, atlas, 6, 8),
                 "IllegalArgumentException", "tile width 6 does not divide 16");
    expect_throw(vm, try_tiled(vm, 3, 2, NULL, 8, 8),
                 "NullPointerException", "TiledLayer(null image)");

    /* Cells. */
    v3(vm, t, NJ_C_TILEDLAYER, "setCell", "(III)V", 0, 0, 2);
    CHECK(i2(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I", 0, 0) == 2,
          "setCell/getCell");
    expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "setCell", "(III)V",
                             (nj_value[]){ {.i = 3}, {.i = 0}, {.i = 1} }, NULL),
                 "IndexOutOfBoundsException", "setCell column 3");
    expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I",
                             (nj_value[]){ {.i = 0}, {.i = 2} }, NULL),
                 "IndexOutOfBoundsException", "getCell row 2");
    expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "setCell", "(III)V",
                             (nj_value[]){ {.i = 0}, {.i = 0}, {.i = 7} }, NULL),
                 "IndexOutOfBoundsException", "setCell with tile index 7 of 2");

    /* Animated tiles: MIDP numbers them -1, -2, ... and returns that index. */
    anim = i1(vm, t, NJ_C_TILEDLAYER, "createAnimatedTile", "(I)I", 1);
    CHECK(anim == -1, "createAnimatedTile returns -1, got %d", anim);
    CHECK(i1(vm, t, NJ_C_TILEDLAYER, "createAnimatedTile", "(I)I", 2) == -2,
          "the second animated tile is -2");
    CHECK(i1(vm, t, NJ_C_TILEDLAYER, "getAnimatedTile", "(I)I", -1) == 1,
          "getAnimatedTile(-1) starts at its static tile");
    v2(vm, t, NJ_C_TILEDLAYER, "setAnimatedTile", "(II)V", -1, 2);
    CHECK(i1(vm, t, NJ_C_TILEDLAYER, "getAnimatedTile", "(I)I", -1) == 2,
          "setAnimatedTile retargets");
    CHECK(i1(vm, t, NJ_C_TILEDLAYER, "getAnimatedTile", "(I)I", 3) == 2,
          "the positive alias nStatic+1 addresses the same animated tile");
    v3(vm, t, NJ_C_TILEDLAYER, "setCell", "(III)V", 2, 1, -1);
    CHECK(i2(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I", 2, 1) == -1,
          "a cell can hold an animated tile index");
    expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "getAnimatedTile", "(I)I",
                             &(nj_value){ .i = -3 }, NULL),
                 "IndexOutOfBoundsException", "getAnimatedTile(-3)");
    expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "createAnimatedTile", "(I)I",
                             &(nj_value){ .i = 99 }, NULL),
                 "IndexOutOfBoundsException", "createAnimatedTile(99)");

    /* fillCells, both the MIDP form and the additive clearing form. */
    v4(vm, t, NJ_C_TILEDLAYER, "fillCells", "(IIII)V", 0, 1, 3, 1);
    CHECK(i2(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I", 2, 1) == 0,
          "4-arg fillCells clears the row (including the animated cell)");
    {
        nj_value a[5];
        a[0].i = 0; a[1].i = 1; a[2].i = 3; a[3].i = 1; a[4].i = 1;
        (void)nj_call(vm, t, NJ_C_TILEDLAYER, "fillCells", "(IIIII)V", a, NULL);
    }
    CHECK(i2(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I", 0, 1) == 1 &&
          i2(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I", 1, 1) == 1 &&
          i2(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I", 2, 1) == 1,
          "fillCells(col,row,numCols,numRows,tile)");
    expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "fillCells", "(IIIII)V",
                             (nj_value[]){ {.i = 2}, {.i = 0}, {.i = 2},
                                           {.i = 1}, {.i = 1} }, NULL),
                 "IndexOutOfBoundsException",
                 "fillCells region outside the grid");
    expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "fillCells", "(IIIII)V",
                             (nj_value[]){ {.i = 0}, {.i = 0}, {.i = -1},
                                           {.i = 1}, {.i = 1} }, NULL),
                 "IllegalArgumentException", "negative fillCells width");

    /* ---- paint(): the loop only visits the visible cells ---- */
    target = mk_image_fill(vm, 24, 24, C_SENT);
    g = mk_graphics(vm, target, 0, 0, 0, 0, 8, 24);   /* only column 0 */
    {
        nj_value a[5];
        a[0].i = 0; a[1].i = 0; a[2].i = 3; a[3].i = 2; a[4].i = 1;  /* red */
        (void)nj_call(vm, t, NJ_C_TILEDLAYER, "fillCells", "(IIIII)V", a, NULL);
        a[0].i = 0; a[1].i = 0; a[2].i = 1; a[3].i = 1; a[4].i = 2;  /* blue */
        (void)nj_call(vm, t, NJ_C_TILEDLAYER, "fillCells", "(IIIII)V", a, NULL);
    }
    v2(vm, t, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
    tiled_paint(vm, t, g);
    CHECK(px_at(vm, target, 4, 4) == C_BLUE,
          "clipped paint: cell (0,0) is blue inside the clip");
    CHECK(px_at(vm, target, 7, 4) == C_BLUE, "clipped paint: still cell (0,0)");
    CHECK(px_at(vm, target, 8, 4) == C_SENT,
          "clipped paint: column 1 is outside the clip and untouched");
    CHECK(px_at(vm, target, 4, 12) == C_RED,
          "clipped paint: cell (0,1) is red");
    CHECK(px_at(vm, target, 4, 20) == C_SENT,
          "clipped paint: below the 2-row grid nothing is drawn");

    /* ---- paint(): the layer's position, whole grid ---- */
    img_wipe(vm, target, C_SENT);
    iput(vm, g, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT, 24);
    v2(vm, t, NJ_C_LAYER, "setPosition", "(II)V", 4, 4);
    tiled_paint(vm, t, g);
    CHECK(px_at(vm, target, 4, 4) == C_BLUE, "cell (0,0) at the layer origin");
    CHECK(px_at(vm, target, 12, 4) == C_RED, "cell (1,0) red");
    CHECK(px_at(vm, target, 4, 12) == C_RED, "cell (0,1) red");
    CHECK(px_at(vm, target, 20, 12) == C_RED, "cell (2,1) red");
    CHECK(px_at(vm, target, 3, 3) == C_SENT, "outside the layer: untouched");
    CHECK(px_at(vm, target, 23, 23) == C_SENT,
          "the grid ends at (20,20): the rest is untouched");

    /* Animated tiles are followed by paint(). */
    img_wipe(vm, target, C_SENT);
    v2(vm, t, NJ_C_TILEDLAYER, "setAnimatedTile", "(II)V", -1, 2);
    v3(vm, t, NJ_C_TILEDLAYER, "setCell", "(III)V", 1, 0, -1);
    tiled_paint(vm, t, g);
    CHECK(px_at(vm, target, 12, 4) == C_BLUE,
          "an animated cell paints its current static tile");

    /* Sprite <-> TiledLayer collision, both arities (pixel and bounds). */
    {
        nj_ref dot = mk_dot_image(vm, 16, 16, 0, 0, C_GREEN);
        nj_ref sp = mk_sprite_frames(vm, dot, 16, 16);
        nj_value ta[2], tz[3], r;

        v2(vm, t, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
        v4(vm, t, NJ_C_TILEDLAYER, "fillCells", "(IIII)V", 0, 0, 3, 2);
        {
            nj_value fa[5];
            fa[0].i = 1; fa[1].i = 1; fa[2].i = 1; fa[3].i = 1; fa[4].i = 1;
            (void)nj_call(vm, t, NJ_C_TILEDLAYER, "fillCells", "(IIIII)V", fa,
                          NULL);
        }
        v2(vm, sp, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
        ta[0].r = t; ta[1].i = 0;
        r.i = 0;
        (void)nj_call(vm, sp, NJ_C_SPRITE, "collidesWith", "(" D_TILED "Z)Z",
                      ta, &r);
        CHECK(r.i == 1, "collidesWith(TiledLayer,false) over an empty cell");
        ta[1].i = 1;
        r.i = 0;
        (void)nj_call(vm, sp, NJ_C_SPRITE, "collidesWith", "(" D_TILED "Z)Z",
                      ta, &r);
        CHECK(r.i == 0, "pixel collision misses the empty cell (0,0)");
        v2(vm, sp, NJ_C_LAYER, "setPosition", "(II)V", 8, 8);
        ta[1].i = 0;
        r.i = 0;
        (void)nj_call(vm, sp, NJ_C_SPRITE, "collidesWith", "(" D_TILED "Z)Z",
                      ta, &r);
        CHECK(r.i == 1, "bounds collision over the red cell");
        ta[1].i = 1;
        r.i = 0;
        (void)nj_call(vm, sp, NJ_C_SPRITE, "collidesWith", "(" D_TILED "Z)Z",
                      ta, &r);
        CHECK(r.i == 1, "pixel collision inside the red cell");
        tz[0].r = t; tz[1].i = 1; tz[2].i = 0;
        r.i = 0;
        (void)nj_call(vm, sp, NJ_C_SPRITE, "collidesWith", "(" D_TILED "ZZ)Z",
                      tz, &r);
        CHECK(r.i == 1, "collidesWith(TiledLayer,true,false) is accepted");
    }

    /* setStaticTileSet with fewer tiles clears the grid and drops the anims. */
    {
        nj_ref small = mk_image_fill(vm, 8, 8, C_GREEN);
        nj_value a[3];
        a[0].r = small; a[1].i = 8; a[2].i = 8;
        if (nj_call(vm, t, NJ_C_TILEDLAYER, "setStaticTileSet",
                    "(" D_IMAGE "II)V", a, NULL) != NJ_OK) {
            report_call(vm, "setStaticTileSet");
        }
        CHECK(i2(vm, t, NJ_C_TILEDLAYER, "getCell", "(II)I", 1, 0) == 0,
              "a smaller static tile set clears the cells");
        expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "getAnimatedTile",
                                 "(I)I", &(nj_value){ .i = -1 }, NULL),
                     "IndexOutOfBoundsException",
                     "animated tiles are dropped with a smaller set");
        expect_throw(vm, nj_call(vm, t, NJ_C_TILEDLAYER, "setStaticTileSet",
                                 "(" D_IMAGE "II)V",
                                 (nj_value[]){ {.r = atlas}, {.i = 6},
                                               {.i = 8} }, NULL),
                     "IllegalArgumentException",
                     "tile width 6 does not divide 16");
    }
}

/* ------------------------------------------------------------------ */
/* 6. LayerManager                                                     */
/* ------------------------------------------------------------------ */
static void test_layermanager(nj_vm *vm)
{
    nj_ref dot = mk_dot_image(vm, 16, 16, 0, 0, C_RED);
    nj_ref dot2 = mk_dot_image(vm, 16, 16, 0, 0, C_BLUE);
    nj_ref dot3 = mk_dot_image(vm, 16, 16, 0, 0, C_GREEN);
    nj_ref a = mk_sprite_frames(vm, dot, 16, 16);
    nj_ref b = mk_sprite_frames(vm, dot2, 16, 16);
    nj_ref c = mk_sprite_frames(vm, dot3, 16, 16);
    nj_ref m = mk_manager(vm);
    nj_ref target = mk_image_fill(vm, 24, 24, C_SENT);
    nj_ref g = mk_graphics(vm, target, 0, 0, 0, 0, 24, 24);
    nj_s4 tx0, ty0, cw0;

    if (!a || !b || !c || !m || !g) {
        CHECK(0, "LayerManager fixtures");
        return;
    }

    {
        nj_value la[1];
        la[0].r = a;
        (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "append", "(" D_LAYER ")V",
                      la, NULL);
        la[0].r = b;
        (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "append", "(" D_LAYER ")V",
                      la, NULL);
        la[0].r = c;
        (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "append", "(" D_LAYER ")V",
                      la, NULL);
    }
    CHECK(i0(vm, m, NJ_C_LAYERMANAGER, "getSize", "()I") == 3, "size == 3");
    CHECK(ref1(vm, m, NJ_C_LAYERMANAGER, "getLayerAt", "(I)" D_LAYER, 0) == a,
          "getLayerAt(0) is the first appended layer");
    CHECK(ref1(vm, m, NJ_C_LAYERMANAGER, "getLayerAt", "(I)" D_LAYER, 2) == c,
          "getLayerAt(2) is the last appended layer");

    /* insert() of a layer already present moves it (MIDP removes it first). */
    {
        nj_value la[2];
        la[0].r = c; la[1].i = 0;
        (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "insert", "(" D_LAYER "I)V",
                      la, NULL);
    }
    CHECK(i0(vm, m, NJ_C_LAYERMANAGER, "getSize", "()I") == 3,
          "inserting an existing layer keeps the size");
    CHECK(ref1(vm, m, NJ_C_LAYERMANAGER, "getLayerAt", "(I)" D_LAYER, 0) == c,
          "insert(layer,0) puts it first");
    CHECK(ref1(vm, m, NJ_C_LAYERMANAGER, "getLayerAt", "(I)" D_LAYER, 1) == a,
          "the other layers shift up");

    {
        nj_value la[1];
        la[0].r = c;
        (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "remove", "(" D_LAYER ")V",
                      la, NULL);
    }
    CHECK(i0(vm, m, NJ_C_LAYERMANAGER, "getSize", "()I") == 2,
          "size after remove");
    CHECK(ref1(vm, m, NJ_C_LAYERMANAGER, "getLayerAt", "(I)" D_LAYER, 0) == a,
          "remove shifts the indices");
    {
        nj_value la[1];
        la[0].r = c;
        (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "remove", "(" D_LAYER ")V",
                      la, NULL);
    }
    CHECK(i0(vm, m, NJ_C_LAYERMANAGER, "getSize", "()I") == 2,
          "removing a layer that is not there is a no-op");
    expect_throw(vm, nj_call(vm, m, NJ_C_LAYERMANAGER, "getLayerAt",
                             "(I)" D_LAYER, &(nj_value){ .i = 2 }, NULL),
                 "IndexOutOfBoundsException", "getLayerAt(2)");
    expect_throw(vm, nj_call(vm, m, NJ_C_LAYERMANAGER, "insert",
                             "(" D_LAYER "I)V",
                             (nj_value[]){ {.r = c}, {.i = 5} }, NULL),
                 "IndexOutOfBoundsException", "insert(layer,5)");
    expect_throw(vm, nj_call(vm, m, NJ_C_LAYERMANAGER, "append",
                             "(" D_LAYER ")V", &(nj_value){ .r = NULL }, NULL),
                 "NullPointerException", "append(null)");
    expect_throw(vm, nj_call(vm, m, NJ_C_LAYERMANAGER, "setViewWindow",
                             "(IIII)V",
                             (nj_value[]){ {.i = 0}, {.i = 0}, {.i = -1},
                                           {.i = 4} }, NULL),
                 "IllegalArgumentException", "negative view window");

    /*
     * z-order: index 0 is closest to the user, so it is painted last and wins
     * where the layers overlap.
     */
    v2(vm, a, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
    v2(vm, b, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
    v2(vm, c, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
    {
        /* Two SOLID sprites make the whole overlap comparable pixel by pixel. */
        nj_ref solid_red = mk_sprite_frames(vm, mk_image_fill(vm, 16, 16, C_RED),
                                            16, 16);
        nj_ref solid_blue = mk_sprite_frames(vm,
                                             mk_image_fill(vm, 16, 16, C_BLUE),
                                             16, 16);
        nj_ref arr[2];
        int i;

        /* start from an empty manager */
        arr[0] = a;
        arr[1] = b;
        for (i = 0; i < 2; i++) {
            nj_value la[1];
            la[0].r = arr[i];
            (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "remove",
                          "(" D_LAYER ")V", la, NULL);
        }
        v2(vm, solid_red, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
        v2(vm, solid_blue, NJ_C_LAYER, "setPosition", "(II)V", 0, 0);
        arr[0] = solid_red;
        arr[1] = solid_blue;
        for (i = 0; i < 2; i++) {
            nj_value la[1];
            la[0].r = arr[i];
            (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "append",
                          "(" D_LAYER ")V", la, NULL);
        }
        img_wipe(vm, target, C_SENT);
        lm_paint_at(vm, m, g, 0, 0);
        CHECK(px_at(vm, target, 0, 0) == C_RED,
              "the layer at index 0 is painted last (on top)");
        CHECK(px_at(vm, target, 5, 5) == C_RED,
              "the whole overlapping area belongs to the top layer");
        CHECK(px_at(vm, target, 15, 15) == C_RED,
              "the top layer covers its full bounds");
        for (i = 0; i < 2; i++) {
            nj_value la[1];
            la[0].r = arr[i];
            (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "remove",
                          "(" D_LAYER ")V", la, NULL);
        }
    }
    CHECK(i0(vm, m, NJ_C_LAYERMANAGER, "getSize", "()I") == 0, "empty again");

    /* View window translation, and the Graphics state is restored. */
    v2(vm, a, NJ_C_LAYER, "setPosition", "(II)V", 4, 4);
    {
        nj_value la[1];
        la[0].r = a;
        (void)nj_call(vm, m, NJ_C_LAYERMANAGER, "append", "(" D_LAYER ")V",
                      la, NULL);
    }
    v4(vm, m, NJ_C_LAYERMANAGER, "setViewWindow", "(IIII)V", 4, 4, 16, 16);
    CHECK(i0(vm, m, NJ_C_LAYERMANAGER, "getViewX", "()I") == 4, "getViewX");
    CHECK(i0(vm, m, NJ_C_LAYERMANAGER, "getViewY", "()I") == 4, "getViewY");
    img_wipe(vm, target, C_SENT);
    tx0 = iget(vm, g, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT);
    ty0 = iget(vm, g, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT);
    cw0 = iget(vm, g, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT);
    lm_paint_at(vm, m, g, 0, 0);
    CHECK(px_at(vm, target, 0, 0) == C_RED,
          "view window (4,4) puts the sprite pixel at the origin");
    CHECK(px_at(vm, target, 15, 15) == C_SENT,
          "the view window is 16x16, so nothing appears beyond it");
    CHECK(iget(vm, g, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT) == tx0 &&
          iget(vm, g, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT) == ty0 &&
          iget(vm, g, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT) == cw0,
          "paint restores the Graphics translate and clip");

    /* Rendering the view window at (2,3) offsets everything by (2,3). */
    img_wipe(vm, target, C_SENT);
    lm_paint_at(vm, m, g, 2, 3);
    CHECK(px_at(vm, target, 2, 3) == C_RED,
          "the view window is rendered at the requested location");

    /* A layer outside the view window does not appear; widen the window and
     * the very same layer does. */
    v2(vm, a, NJ_C_LAYER, "setPosition", "(II)V", 60, 60);
    img_wipe(vm, target, C_SENT);
    lm_paint_at(vm, m, g, 0, 0);
    CHECK(count_painted(vm, target) == 0,
          "a layer outside the view window is skipped");
    v4(vm, m, NJ_C_LAYERMANAGER, "setViewWindow", "(IIII)V", 60, 60, 16, 16);
    lm_paint_at(vm, m, g, 0, 0);
    CHECK(px_at(vm, target, 0, 0) == C_RED,
          "the same layer appears once the view window reaches it");

    /* An invisible layer is skipped even inside the window. */
    img_wipe(vm, target, C_SENT);
    v1(vm, a, NJ_C_LAYER, "setVisible", "(Z)V", 0);
    lm_paint_at(vm, m, g, 0, 0);
    CHECK(count_painted(vm, target) == 0, "an invisible layer is skipped");
    v1(vm, a, NJ_C_LAYER, "setVisible", "(Z)V", 1);
}

/* ------------------------------------------------------------------ */
/* 7. GameCanvas                                                       */
/* ------------------------------------------------------------------ */
static void test_gamecanvas(nj_vm *vm)
{
    nj_ref c = mk_gamecanvas(vm, 0);
    nj_ref buf, g1, g2;
    nj_surface *scr;
    nj_u4 q_sent = 0u, q_green = 0u, q_blue = 0u, q_red = 0u;

    if (!c) {
        CHECK(0, "GameCanvas fixture");
        return;
    }
    buf = rget(vm, c, NJ_C_CANVAS, NJ_F_C_BUFFER, D_IMAGE);
    CHECK(buf != NULL, "the constructor creates the off-screen buffer");
    CHECK(px_at(vm, buf, 0, 0) == C_WHITE, "the buffer starts white");

    /* getGraphics(): a fresh Graphics over the buffer, full clip, black pen. */
    g1 = NULL;
    {
        nj_value r;
        r.r = NULL;
        if (nj_call(vm, c, NJ_C_GAMECANVAS, "getGraphics", "()" D_GFX, NULL,
                    &r) != NJ_OK) {
            report_call(vm, "GameCanvas.getGraphics");
        }
        g1 = r.r;
    }
    CHECK(g1 != NULL, "getGraphics() returns a Graphics");
    CHECK(rget(vm, g1, NJ_C_GRAPHICS, NJ_F_G_TARGET, D_IMAGE) == buf,
          "getGraphics() targets the off-screen buffer");
    CHECK(iget(vm, g1, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT) == 0 &&
          iget(vm, g1, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT) == 0,
          "getGraphics() starts untranslated");
    CHECK(iget(vm, g1, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT) == 0 &&
          iget(vm, g1, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT) == 0,
          "getGraphics() clip origin");
    {
        int bw = 0, bh = 0;
        (void)nj_midp_image_size(vm, buf, &bw, &bh);
        CHECK(iget(vm, g1, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT) == bw &&
              iget(vm, g1, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT) == bh,
              "getGraphics() clip covers the buffer");
    }
    CHECK((nj_u4)iget(vm, g1, NJ_C_GRAPHICS, NJ_F_G_COLOR, D_INT) ==
          0xFF000000u, "getGraphics() starts with a black pen");
    {
        nj_value r;
        r.r = NULL;
        (void)nj_call(vm, c, NJ_C_GAMECANVAS, "getGraphics", "()" D_GFX, NULL,
                      &r);
        g2 = r.r;
    }
    CHECK(g2 != NULL && g2 != g1,
          "each getGraphics() call returns a new Graphics object");

    /* ---- flushGraphics() ---- */
    scr = nj_midp_screen(vm);
    CHECK(scr != NULL, "the display surface exists");
    if (!scr) {
        return;
    }
    px_set(vm, buf, 2, 3, C_GREEN);
    px_set(vm, buf, 10, 10, C_BLUE);

    /*
     * A device panel is usually RGB565, so a colour written to the screen and
     * read back is quantised.  Ask the surface what each colour becomes and
     * compare against that, so the assertions hold for any pixel format.
     */
    {
        nj_surface_set(scr, 0, 0, C_SENT);  q_sent = nj_surface_get(scr, 0, 0);
        nj_surface_set(scr, 0, 0, C_GREEN); q_green = nj_surface_get(scr, 0, 0);
        nj_surface_set(scr, 0, 0, C_BLUE);  q_blue = nj_surface_get(scr, 0, 0);
        nj_surface_set(scr, 0, 0, C_RED);   q_red = nj_surface_get(scr, 0, 0);
    }

    nj_surface_fill(scr, C_SENT);
    v0(vm, c, NJ_C_GAMECANVAS, "flushGraphics", "()V");
    CHECK(nj_surface_get(scr, 2, 3) == q_green,
          "flushGraphics() copies the buffer to the display");
    CHECK(nj_surface_get(scr, 10, 10) == q_blue,
          "flushGraphics() copies all of it");
    CHECK(px_at(vm, buf, 2, 3) == C_GREEN,
          "flushing does not clear the buffer");
    CHECK(vm->repaint_pending != 0u,
          "flushGraphics() asks the runner for a frame");

    /* The sub-rectangle form. */
    nj_surface_fill(scr, C_SENT);
    v4(vm, c, NJ_C_GAMECANVAS, "flushGraphics", "(IIII)V", 2, 3, 1, 1);
    CHECK(nj_surface_get(scr, 2, 3) == q_green,
          "flushGraphics(2,3,1,1) copies that pixel");
    CHECK(nj_surface_get(scr, 10, 10) == q_sent,
          "flushGraphics(2,3,1,1) leaves the rest alone");

    /* Zero-sized requests flush nothing. */
    nj_surface_fill(scr, C_SENT);
    v4(vm, c, NJ_C_GAMECANVAS, "flushGraphics", "(IIII)V", 0, 0, 0, 0);
    CHECK(nj_surface_get(scr, 2, 3) == q_sent, "a 0x0 flush does nothing");

    /* A region that starts outside the canvas is clipped to it. */
    px_set(vm, buf, 1, 1, C_RED);
    nj_surface_fill(scr, C_SENT);
    v4(vm, c, NJ_C_GAMECANVAS, "flushGraphics", "(IIII)V", -5, -5, 10, 10);
    CHECK(nj_surface_get(scr, 1, 1) == q_red,
          "a negative flush origin is clipped, not wrapped");

    /* paint(Graphics): MIDP's GameCanvas.paint renders the buffer at (0,0). */
    nj_surface_fill(scr, C_SENT);
    {
        nj_ref gscreen = mk_graphics(vm, NULL, 0, 0, 0, 0,
                                     scr->width, scr->height);
        nj_value pa[1];
        pa[0].r = gscreen;
        if (nj_call(vm, c, NJ_C_GAMECANVAS, "paint", "(" D_GFX ")V", pa, NULL)
            != NJ_OK) {
            report_call(vm, "GameCanvas.paint");
        }
    }
    CHECK(nj_surface_get(scr, 2, 3) == q_green,
          "paint(Graphics) renders the off-screen buffer at (0,0)");
    expect_throw(vm, nj_call(vm, c, NJ_C_GAMECANVAS, "paint", "(" D_GFX ")V",
                             &(nj_value){ .r = NULL }, NULL),
                 "NullPointerException", "GameCanvas.paint(null)");

    /* ---- getKeyStates() ---- */
    iput(vm, c, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT, NJ_GAME_LEFT | NJ_GAME_FIRE);
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0x0104,
          "getKeyStates maps NJ_GAME_* to the MIDP FIRE|LEFT bits");
    CHECK(iget(vm, c, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT) == 0,
          "reading the key states consumes the latch");
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0,
          "a second read reports no new presses");
    iput(vm, c, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT,
          NJ_GAME_UP | NJ_GAME_DOWN | NJ_GAME_RIGHT | NJ_GAME_A |
          NJ_GAME_B | NJ_GAME_C | NJ_GAME_D);
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") ==
          (0x0002 | 0x0040 | 0x0020 | 0x0200 | 0x0400 | 0x0800 | 0x1000),
          "every game key maps to its documented MIDP bit");

    /* Drive the common port event path, including simultaneous directions. */
    CHECK(nj_midp_set_current(vm, c) == NJ_OK, "show polling canvas");
    nj_midp_key(vm, NJ_KEY_UP, 1);
    nj_midp_key(vm, NJ_KEY_RIGHT, 1);
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0x0022,
          "held directions appear together");
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0x0022,
          "held directions survive repeated polls without OS repeat");
    nj_midp_key(vm, NJ_KEY_UP, 0);
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0x0020,
          "releasing one direction preserves the other");
    nj_midp_key(vm, NJ_KEY_RIGHT, 0);
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0,
          "release stops movement");
    nj_midp_key(vm, NJ_KEY_FIRE, 1);
    nj_midp_key(vm, NJ_KEY_FIRE, 0);
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0x0100,
          "quick tap between polls is latched once");
    CHECK(i0(vm, c, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0,
          "quick tap is consumed on the first poll");

    /* Callback suppression does not change polling semantics. */
    {
        nj_ref cs = mk_gamecanvas(vm, 1);
        CHECK(cs != NULL, "GameCanvas(true)");
        iput(vm, cs, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT,
             NJ_GAME_UP | NJ_GAME_FIRE);
        CHECK(i0(vm, cs, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0x0102,
              "a suppressed canvas reports UP|FIRE");
        CHECK(i0(vm, cs, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0,
              "suppressed canvas also consumes its latch");
        CHECK(nj_midp_set_current(vm, cs) == NJ_OK, "show suppressed canvas");
        nj_midp_key(vm, NJ_KEY_DOWN, 1);
        CHECK(i0(vm, cs, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0x0040,
              "suppressed canvas polls held keys");
        CHECK(i0(vm, cs, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0x0040,
              "suppressed canvas keeps held keys across polls");
        nj_midp_key(vm, NJ_KEY_DOWN, 0);
        CHECK(i0(vm, cs, NJ_C_GAMECANVAS, "getKeyStates", "()I") == 0,
              "suppressed canvas stops on release");
    }
}

/* A GameCanvas may override paint() and start its timer in showNotify().
 * Exercise the runner's paint entry without a key event delivering visibility
 * first, and collect during showNotify to check the pending Graphics root. */
static int gc_visible_phase, gc_size_calls, gc_show_calls, gc_paint_calls;

static nj_status gc_test_size(nj_vm *vm, nj_thread *th,
                              nj_value *args, nj_value *ret)
{
    nj_surface *scr = nj_midp_screen(vm);
    (void)th; (void)ret;
    CHECK(gc_visible_phase == 0, "sizeChanged precedes showNotify");
    CHECK(scr && args[0].i == scr->width && args[1].i == scr->height,
          "GameCanvas receives the display size");
    gc_visible_phase = 1;
    gc_size_calls++;
    return NJ_OK;
}

static nj_status gc_test_show(nj_vm *vm, nj_thread *th,
                              nj_value *args, nj_value *ret)
{
    (void)th; (void)args; (void)ret;
    CHECK(gc_visible_phase == 1, "showNotify follows sizeChanged");
    gc_visible_phase = 2;
    gc_show_calls++;
    (void)nj_gc_collect(vm);
    return NJ_OK;
}

static nj_status gc_test_paint(nj_vm *vm, nj_thread *th,
                               nj_value *args, nj_value *ret)
{
    nj_ref g = args[0].r;
    (void)th; (void)ret;
    CHECK(gc_visible_phase >= 2, "GameCanvas is shown before paint");
    CHECK(nj_heap_owns_object(vm, g), "Graphics survives showNotify collection");
    if (nj_heap_owns_object(vm, g)) {
        v1(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", 0x00FF00);
        v4(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", 0, 0, 1, 1);
    }
    gc_visible_phase = 3;
    gc_paint_calls++;
    return NJ_OK;
}

static void test_gamecanvas_visibility(nj_vm *vm)
{
    static const nj_builtin_method methods[] = {
        { "sizeChanged", "(II)V", NJ_ACC_PROTECTED },
        { "showNotify", "()V", NJ_ACC_PROTECTED },
        { "paint", "(" D_GFX ")V", NJ_ACC_PUBLIC }
    };
    static const nj_builtin_class fixture = {
        "test/VisibleGameCanvas", NJ_C_GAMECANVAS, NJ_ACC_PUBLIC,
        NULL, 0u, methods, 3u
    };
    nj_class *cls = nj_class_create_builtin(vm, &fixture);
    nj_u4 mark = nj_root_mark(vm);
    nj_ref c;
    nj_value a;
    CHECK(cls != NULL, "GameCanvas visibility fixture class");
    if (!cls) { return; }
    cls->methods[0].native = gc_test_size;
    cls->methods[1].native = gc_test_show;
    cls->methods[2].native = gc_test_paint;
    c = nj_root(vm, nj_object_new(vm, fixture.name));
    CHECK(c != NULL, "GameCanvas visibility fixture object");
    if (!c) { nj_root_release(vm, mark); return; }
    a.i = 0;
    CHECK(nj_call_special(vm, c, NJ_C_GAMECANVAS, "<init>", "(Z)V", &a,
                          NULL) == NJ_OK, "initialize visibility canvas");
    gc_visible_phase = gc_size_calls = gc_show_calls = gc_paint_calls = 0;
    CHECK(nj_midp_set_current(vm, c) == NJ_OK, "select visibility canvas");
    CHECK(gc_show_calls == 0, "setCurrent defers showNotify to event delivery");
    CHECK(nj_midp_paint(vm) == NJ_OK, "paint newly selected GameCanvas");
    CHECK(gc_size_calls == 1 && gc_show_calls == 1 && gc_paint_calls == 1,
          "first paint delivers sizeChanged, showNotify and paint once");
    CHECK((nj_surface_get(nj_midp_screen(vm), 0, 0) & 0x00FFFFFFu) == 0x00FF00u,
          "GameCanvas override paints the display after showNotify");
    CHECK(nj_midp_paint(vm) == NJ_OK, "paint visible GameCanvas again");
    CHECK(gc_size_calls == 1 && gc_show_calls == 1 && gc_paint_calls == 2,
          "subsequent paint does not repeat visibility notifications");
    CHECK(!nj_has_exception(vm), "visibility callbacks finish without exception");
    nj_root_release(vm, mark);
}

/* ------------------------------------------------------------------ */
/* The suite                                                           */
/* ------------------------------------------------------------------ */
static void run_suite(nj_vm *vm)
{
    test_field_lookup_memo(vm);
    test_sprite_frames(vm);
    test_reference_pixel(vm);
    test_sprite_paint(vm);
    test_collision(vm);
    test_tiledlayer(vm);
    test_layermanager(vm);
    test_gamecanvas(vm);
    test_gamecanvas_visibility(vm);
}

/* ------------------------------------------------------------------ */
/* main: three create -> run -> destroy rounds                         */
/* ------------------------------------------------------------------ */
static nj_u4 run_round(int round, int *checks, int *failures, int verbose)
{
    nj_port port;
    nj_vm *vm;
    nj_u4 released;
    int c0 = g_checks, f0 = g_failures;

    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "port init failed\n");
        exit(2);
    }
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "vm create failed\n");
        nj_port_shutdown(&port);
        exit(2);
    }
    run_suite(vm);
    *checks = g_checks - c0;
    *failures = g_failures - f0;
    if (verbose) {
        printf("round %d: %d assertions, %d failures\n", round, *checks,
               *failures);
    }
    /* Everything the VM owns is released here; the count is the leak
     * assertion for the whole round. */
    released = nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    return released;
}

int main(void)
{
    nj_u4 r1, r2, r3;
    int c1 = 0, c2 = 0, c3 = 0, f1 = 0, f2 = 0, f3 = 0;

    printf("== nano_j2me javax.microedition.lcdui.game test ==\n");
    r1 = run_round(1, &c1, &f1, 1);
    r2 = run_round(2, &c2, &f2, 1);
    r3 = run_round(3, &c3, &f3, 1);

    printf("assertions: %d + %d + %d = %d, failures: %d\n",
           c1, c2, c3, c1 + c2 + c3, f1 + f2 + f3);

    if (r1 == 0u) {
        printf("FAIL: nj_vm_destroy() released nothing, accounting is broken\n");
        return 1;
    }
    if (r2 != r1 || r3 != r1) {
        printf("FAIL: teardown drifted between rounds (%u / %u / %u)\n",
               (unsigned)r1, (unsigned)r2, (unsigned)r3);
        return 1;
    }
    printf("host blocks released by nj_vm_destroy(): %u, %u, %u (identical)\n",
           (unsigned)r1, (unsigned)r2, (unsigned)r3);

    if (f1 + f2 + f3 != 0) {
        printf("\nFAIL: %d of %d assertions failed\n", f1 + f2 + f3,
               c1 + c2 + c3);
        return 1;
    }
    printf("\nPASS: %d assertions (x3 rounds), no residue\n", c1);
    return 0;
}
