/*
 * tests/tmp_game_lcdui.c
 *
 * ============================================================================
 * THROWAWAY STAND-IN -- NOT PART OF THE RUNTIME.  DELETE WHEN UNUSED.
 * ============================================================================
 *
 * src/midp/nj_lib_lcdui.c is owned by another module and was being rewritten
 * while the game package was verified (it is ~280 KB and occasionally does not
 * compile for a few minutes at a time).  This file implements just enough of
 * the LCDUI contract - Image, Graphics, Displayable, Canvas and the
 * nj_midp_* helpers of src/midp/nj_midp.h - that src/midp/nj_lib_game.c can be
 * built and tested on its own.
 *
 * It deliberately implements NOTHING of LCDUI's own behaviour: no widgets, no
 * drawing primitives, no key dispatch to Java.  The real rasterizer in
 * src/gfx/ is used unchanged, so everything the game package draws goes through
 * the production blitter.
 *
 * The point of the stand-in is reproducibility: the game package's own
 * behaviour must not depend on another module's edit-in-progress state.  The
 * authoritative run links the real src/midp/nj_lib_lcdui.c instead.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define D_INT   "I"
#define D_BOOL  "Z"
#define D_IMAGE "Ljavax/microedition/lcdui/Image;"

/* ------------------------------------------------------------------ */
/* Field helpers                                                       */
/* ------------------------------------------------------------------ */
static nj_field *fld(nj_vm *vm, const char *cls, const char *name,
                     const char *desc)
{
    return nj_field_lookup(vm, cls, name, desc, 0);
}

static nj_s4 fgeti(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                   const char *desc)
{
    nj_field *f = fld(vm, cls, name, desc);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void fputi(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                  const char *desc, nj_s4 v)
{
    nj_field *f = fld(vm, cls, name, desc);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static nj_ref fgetr(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                    const char *desc)
{
    nj_field *f = fld(vm, cls, name, desc);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

/* ------------------------------------------------------------------ */
/* Image                                                               */
/* ------------------------------------------------------------------ */
int nj_midp_image_size(nj_vm *vm, nj_ref image_obj, int *w, int *h)
{
    int rw, rh;

    if (!vm || !image_obj) {
        return 0;
    }
    rw = (int)fgeti(vm, image_obj, NJ_C_IMAGE, NJ_F_IMG_WIDTH, D_INT);
    rh = (int)fgeti(vm, image_obj, NJ_C_IMAGE, NJ_F_IMG_HEIGHT, D_INT);
    if (rw <= 0 || rh <= 0) {
        return 0;
    }
    if (w) { *w = rw; }
    if (h) { *h = rh; }
    return 1;
}

nj_u4 *nj_midp_image_pixels(nj_vm *vm, nj_ref image_obj)
{
    nj_ref px;

    if (!vm || !image_obj) {
        return NULL;
    }
    px = fgetr(vm, image_obj, NJ_C_IMAGE, NJ_F_IMG_PIXELS, "[I");
    if (!px || px->kind != NJ_OBJ_ARRAY || px->aflags != NJ_ARRAY_T_INT) {
        return NULL;
    }
    return (nj_u4 *)px->u.bytes;
}

nj_ref nj_midp_image_new(nj_vm *vm, int w, int h, int mutable)
{
    nj_ref img, px;

    if (!vm || w <= 0 || h <= 0) {
        return NULL;
    }
    img = nj_object_new(vm, NJ_C_IMAGE);
    if (!img) {
        return NULL;
    }
    px = nj_int_array_new(vm, (nj_u4)w * (nj_u4)h);
    if (!px) {
        return NULL;
    }
    fputi(vm, img, NJ_C_IMAGE, NJ_F_IMG_WIDTH, D_INT, w);
    fputi(vm, img, NJ_C_IMAGE, NJ_F_IMG_HEIGHT, D_INT, h);
    fputi(vm, img, NJ_C_IMAGE, NJ_F_IMG_MUTABLE, D_BOOL, mutable ? 1 : 0);
    {
        nj_field *f = fld(vm, NJ_C_IMAGE, NJ_F_IMG_PIXELS, "[I");
        if (f) {
            nj_field_write_ref(vm, img, f, px);
        }
    }
    return img;
}

nj_ref nj_midp_image_adopt(nj_vm *vm, nj_u4 *pixels, int w, int h)
{
    nj_ref img = nj_midp_image_new(vm, w, h, 0);
    nj_u4 *dst;

    if (!img || !pixels) {
        return NULL;
    }
    dst = nj_midp_image_pixels(vm, img);
    if (!dst) {
        return NULL;
    }
    memcpy(dst, pixels, (nj_size)w * (nj_size)h * sizeof(nj_u4));
    return img;
}

nj_surface *nj_midp_surface(nj_vm *vm, nj_ref image_obj, nj_surface *scratch)
{
    nj_u4 *px;
    int w = 0, h = 0;

    if (!vm || !scratch) {
        return NULL;
    }
    if (!image_obj) {
        return nj_midp_screen(vm);
    }
    if (!nj_midp_image_size(vm, image_obj, &w, &h)) {
        (void)nj_throw(vm, "java/lang/NullPointerException", "image is null");
        return NULL;
    }
    px = nj_midp_image_pixels(vm, image_obj);
    if (!px) {
        return NULL;
    }
    scratch->pixels = px;
    scratch->width = w;
    scratch->height = h;
    scratch->stride = w * 4;
    scratch->format = NJ_PIXFMT_ARGB8888;
    scratch->bpp = 4;
    return scratch;
}

/* ------------------------------------------------------------------ */
/* Display surface                                                     */
/* ------------------------------------------------------------------ */
nj_surface *nj_midp_screen(nj_vm *vm)
{
    int w, h, fmt = NJ_PIXFMT, bpp;
    nj_u1 *buf;

    if (!vm) {
        return NULL;
    }
    if (vm->screen_pixels) {
        return &vm->screen;
    }
    w = (vm->port && vm->port->width > 0) ? vm->port->width : NJ_SCREEN_WIDTH;
    h = (vm->port && vm->port->height > 0) ? vm->port->height : NJ_SCREEN_HEIGHT;
    bpp = (fmt == NJ_PIXFMT_ARGB8888) ? 4 : ((fmt == NJ_PIXFMT_RGB888) ? 3 : 2);
    buf = (nj_u1 *)nj_host_alloc(vm, (nj_size)w * (nj_size)h * (nj_size)bpp);
    if (!buf) {
        return NULL;
    }
    vm->screen_pixels = buf;
    vm->screen.pixels = buf;
    vm->screen.width = w;
    vm->screen.height = h;
    vm->screen.stride = w * bpp;
    vm->screen.format = fmt;
    vm->screen.bpp = bpp;
    vm->screen_w = (nj_u2)w;
    vm->screen_h = (nj_u2)h;
    vm->midp_flags = NJ_MIDP_HAS_SOFTKEYS;
    if (!vm->fonts.initialized) {
        nj_fontset_init(&vm->fonts);
    }
    return &vm->screen;
}

void nj_midp_repaint(nj_vm *vm)
{
    if (vm) {
        vm->repaint_pending = 1u;
    }
}

nj_fontset *nj_midp_fonts(nj_vm *vm)
{
    if (!vm) {
        return NULL;
    }
    if (!vm->fonts.initialized) {
        nj_fontset_init(&vm->fonts);
    }
    return &vm->fonts;
}

const nj_font *nj_midp_font_for(nj_vm *vm, nj_ref font_obj)
{
    (void)font_obj;
    if (!vm) {
        return NULL;
    }
    if (!vm->fonts.initialized) {
        nj_fontset_init(&vm->fonts);
    }
    return nj_fontset_get(&vm->fonts, NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);
}

/* ------------------------------------------------------------------ */
/* Display / input                                                     */
/* ------------------------------------------------------------------ */
nj_ref nj_midp_display(nj_vm *vm)
{
    return vm ? vm->display : NULL;
}

nj_ref nj_midp_current(nj_vm *vm)
{
    return vm ? vm->current_displayable : NULL;
}

nj_status nj_midp_set_current(nj_vm *vm, nj_ref d)
{
    if (!vm) {
        return NJ_ERR;
    }
    vm->current_displayable = d;
    vm->repaint_pending = 1u;
    return NJ_OK;
}

/* Sticky game-key bits, exactly as the real module maintains them. */
int nj_midp_key(nj_vm *vm, int key, int down)
{
    nj_ref d;
    int mask;

    if (!vm) {
        return 0;
    }
    d = nj_midp_current(vm);
    if (!d) {
        return 0;
    }
    switch (key) {
    case NJ_KEY_UP:      mask = NJ_GAME_UP; break;
    case NJ_KEY_DOWN:    mask = NJ_GAME_DOWN; break;
    case NJ_KEY_LEFT:    mask = NJ_GAME_LEFT; break;
    case NJ_KEY_RIGHT:   mask = NJ_GAME_RIGHT; break;
    case NJ_KEY_FIRE:    mask = NJ_GAME_FIRE; break;
    case NJ_KEY_GAME_A:  mask = NJ_GAME_A; break;
    case NJ_KEY_GAME_B:  mask = NJ_GAME_B; break;
    case NJ_KEY_GAME_C:  mask = NJ_GAME_C; break;
    case NJ_KEY_GAME_D:  mask = NJ_GAME_D; break;
    default:             mask = 0; break;
    }
    if (mask != 0) {
        nj_s4 keys = fgeti(vm, d, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT);
        keys = down ? (keys | mask) : (keys & ~mask);
        fputi(vm, d, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT, keys);
    }
    return 1;
}

int nj_midp_pointer(nj_vm *vm, int x, int y, int down)
{
    (void)vm; (void)x; (void)y; (void)down;
    return 0;
}

nj_status nj_midp_paint(nj_vm *vm)
{
    nj_surface *s = nj_midp_screen(vm);

    if (!s) {
        return NJ_ERR;
    }
    nj_surface_fill(s, 0xFF000000u);
    vm->repaint_pending = 0u;
    return NJ_OK;
}

/* Called by nj_lib_late_init(): this stand-in owns no static state. */
nj_status nj_lcdui_late_init(nj_vm *vm)
{
    if (!vm) {
        return NJ_ERR;
    }
    if (!vm->fonts.initialized) {
        nj_fontset_init(&vm->fonts);
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Classes and tables                                                  */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define F(name, desc, flags) { name, desc, flags }
#define N(a) ((nj_u2)(sizeof(a) / sizeof((a)[0])))

static const nj_builtin_field displayable_fields[] = {
    F(NJ_F_D_TITLE, "Ljava/lang/String;", 0),
    F(NJ_F_D_TICKER, "Ljavax/microedition/lcdui/Ticker;", 0),
    F(NJ_F_D_LISTENER, "Ljavax/microedition/lcdui/CommandListener;", 0),
    F(NJ_F_D_COMMANDS, "Ljava/util/Vector;", 0),
    F(NJ_F_D_FULLSCRN, "Z", 0)
};

static const nj_builtin_method displayable_methods[] = {
    M("<init>", "()V", NJ_ACC_PROTECTED)
};

static const nj_builtin_field canvas_fields[] = {
    F(NJ_F_C_BUFFER, D_IMAGE, 0),
    F(NJ_F_C_WIDTH, "I", 0),
    F(NJ_F_C_HEIGHT, "I", 0),
    F(NJ_F_C_KEYS, "I", 0),
    F(NJ_F_C_VISIBLE, "Z", 0),
    F(NJ_F_C_PRESSED, "Z", 0)
};

static const nj_builtin_method canvas_methods[] = {
    M("<init>", "()V", NJ_ACC_PROTECTED),
    /* MIDP 2.0 also gives Canvas a (Z) constructor; GameCanvas chains to it. */
    M("<init>", "(Z)V", NJ_ACC_PROTECTED),
    M("paint", "(Ljavax/microedition/lcdui/Graphics;)V", NJ_ACC_PUBLIC)
};

static const nj_builtin_field graphics_fields[] = {
    F(NJ_F_G_TARGET, D_IMAGE, 0),
    F(NJ_F_G_TX, "I", 0),
    F(NJ_F_G_TY, "I", 0),
    F(NJ_F_G_CX, "I", 0),
    F(NJ_F_G_CY, "I", 0),
    F(NJ_F_G_CW, "I", 0),
    F(NJ_F_G_CH, "I", 0),
    F(NJ_F_G_COLOR, "I", 0),
    F(NJ_F_G_BLEND, "I", 0),
    F(NJ_F_G_STROKE, "I", 0),
    F(NJ_F_G_FONT, "Ljavax/microedition/lcdui/Font;", 0),
    F(NJ_F_G_XOR, "I", 0),
    F(NJ_F_G_XORCOLOR, "I", 0)
};

static const nj_builtin_method graphics_methods[] = {
    M("<init>", "()V", NJ_ACC_PUBLIC)
};

static const nj_builtin_field image_fields[] = {
    F(NJ_F_IMG_WIDTH, "I", 0),
    F(NJ_F_IMG_HEIGHT, "I", 0),
    F(NJ_F_IMG_PIXELS, "[I", 0),
    F(NJ_F_IMG_MUTABLE, "Z", 0)
};

static const nj_builtin_method image_methods[] = {
    M("<init>", "()V", NJ_ACC_PUBLIC)
};

/* ---- natives that fill in a freshly created object ---- */
NJ_NATIVE_FN(t_canvas_init)
{
    nj_ref self = NJ_THIS(args);
    (void)th;
    fputi(vm, self, NJ_C_CANVAS, NJ_F_C_BUFFER, D_IMAGE, 0);
    (void)fld(vm, NJ_C_CANVAS, NJ_F_C_BUFFER, D_IMAGE);
    {
        nj_field *f = fld(vm, NJ_C_CANVAS, NJ_F_C_BUFFER, D_IMAGE);
        if (f) {
            nj_field_write_ref(vm, self, f, NULL);
        }
    }
    fputi(vm, self, NJ_C_CANVAS, NJ_F_C_WIDTH, D_INT, 0);
    fputi(vm, self, NJ_C_CANVAS, NJ_F_C_HEIGHT, D_INT, 0);
    fputi(vm, self, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT, 0);
    fputi(vm, self, NJ_C_CANVAS, NJ_F_C_VISIBLE, D_BOOL, 0);
    fputi(vm, self, NJ_C_CANVAS, NJ_F_C_PRESSED, D_BOOL, 0);
    return NJ_OK;
}

NJ_NATIVE_FN(t_graphics_init)
{
    nj_ref self = NJ_THIS(args);
    (void)th;
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT, 0);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT, 0);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT, 0);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT, 0);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT, 0);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT, 0);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_COLOR, D_INT, (nj_s4)0xFF000000u);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_BLEND, D_INT, NJ_BLEND_SRC_OVER);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_STROKE, D_INT, NJ_STROKE_SOLID);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_XOR, D_INT, 0);
    fputi(vm, self, NJ_C_GRAPHICS, NJ_F_G_XORCOLOR, D_INT, 0);
    return NJ_OK;
}

const nj_builtin_class nj_lib_lcdui_classes[] = {
    { NJ_C_DISPLAYABLE, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      displayable_fields, N(displayable_fields),
      displayable_methods, N(displayable_methods) },
    { NJ_C_CANVAS, NJ_C_DISPLAYABLE, NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      canvas_fields, N(canvas_fields),
      canvas_methods, N(canvas_methods) },
    { NJ_C_GRAPHICS, "java/lang/Object", NJ_ACC_PUBLIC,
      graphics_fields, N(graphics_fields),
      graphics_methods, N(graphics_methods) },
    { NJ_C_IMAGE, "java/lang/Object", NJ_ACC_PUBLIC,
      image_fields, N(image_fields),
      image_methods, N(image_methods) }
};
const int nj_lib_lcdui_class_count =
    (int)(sizeof(nj_lib_lcdui_classes) / sizeof(nj_lib_lcdui_classes[0]));

const nj_native_entry nj_lib_lcdui_table[] = {
    { NJ_C_CANVAS, "<init>", "()V", t_canvas_init },
    { NJ_C_CANVAS, "<init>", "(Z)V", t_canvas_init },
    { NJ_C_GRAPHICS, "<init>", "()V", t_graphics_init }
};
const int nj_lib_lcdui_count =
    (int)(sizeof(nj_lib_lcdui_table) / sizeof(nj_lib_lcdui_table[0]));
