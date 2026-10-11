/*
 * nano_j2me - nj_lib_lcdui.c
 *
 * javax.microedition.lcdui - the whole user-interface toolkit.
 *
 * ============================================================================
 *  HOW THIS MODULE IS PUT TOGETHER
 * ============================================================================
 *
 * In real MIDP every widget is a Java class with fields and methods; here each
 * one is an ordinary VM object whose fields are declared in the builtin class
 * tables below.  That gives three things this port needs:
 *
 *   - the GC owns the UI.  There is no side table to free and no lifetime to
 *     get wrong: an Image *is* an int[] of ARGB pixels, a Graphics *is* a set
 *     of transform fields, a Form *is* an Item[].
 *   - STATE PLACEMENT.  Every byte of mutable state lives in a Java field or in
 *     the VM (vm->screen, vm->fonts, vm->midp_flags, ...).  The module has no
 *     file-scope variables at all, so create -> run -> destroy -> create leaves
 *     nothing behind (tools/check_no_statics.sh enforces this).
 *   - the field names are the ABI other MIDP modules (the game package, the
 *     MIDlet lifecycle, the runner) code against, exactly as fixed by
 *     src/midp/nj_midp.h.
 *
 * DRAWING.  A Java Graphics is a *serialised* nj_gfx: every call rebuilds an
 * nj_gfx on the C stack from the transform fields, points it at
 * nj_midp_surface(target), runs one nj_gfx primitive and writes the mutated
 * fields back.  Nothing is cached, so a Graphics stays valid across GC moves,
 * class reloads and a complete VM teardown.
 *
 * PAINTING.  nj_midp_paint() owns the frame: it makes sure the current
 * Displayable has a backing store, gets the application to draw into it
 * (paint() for a Canvas, a built-in widget renderer for Form/List/TextBox/
 * Alert) and blits the result into the VM framebuffer, which is then handed to
 * the port with display_present().
 */
#include "nj_midp.h"
#include "nj2me/nj_gfx.h"
#include "nj2me/nj_png.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

/* ------------------------------------------------------------------ */
/* Descriptors used constantly                                         */
/* ------------------------------------------------------------------ */
#define D_INT     "I"
#define D_BOOL    "Z"
#define D_STR     "Ljava/lang/String;"
#define D_OBJ     "Ljava/lang/Object;"
#define D_IMAGE   "Ljavax/microedition/lcdui/Image;"
#define D_FONT    "Ljavax/microedition/lcdui/Font;"
#define D_GFX     "Ljavax/microedition/lcdui/Graphics;"
#define D_CMD     "Ljavax/microedition/lcdui/Command;"
#define D_TICKER  "Ljavax/microedition/lcdui/Ticker;"
#define D_ITEM    "Ljavax/microedition/lcdui/Item;"
#define D_ITEMARR "[Ljavax/microedition/lcdui/Item;"
#define D_IMGARR  "[Ljavax/microedition/lcdui/Image;"
#define D_STRARR  "[Ljava/lang/String;"
#define D_INTARR  "[I"
#define D_CHARR   "[C"
#define D_BARR    "[B"
#define D_OBJARR  "[Ljava/lang/Object;"
#define D_VECTOR  "Ljava/util/Vector;"
#define D_FORM    "Ljavax/microedition/lcdui/Form;"
#define D_CL      "Ljavax/microedition/lcdui/CommandListener;"
#define D_ICL     "Ljavax/microedition/lcdui/ItemCommandListener;"
#define D_ISL     "Ljavax/microedition/lcdui/ItemStateListener;"
#define D_RUN     "Ljava/lang/Runnable;"

/*
 * Field names come straight from nj_midp.h, which is the ABI between MIDP
 * modules: an Image is { imgWidth, imgHeight, imgPixels, imgMutable } and the
 * game package looks the very same names up.
 */
#define F_IMG_W    NJ_F_IMG_WIDTH
#define F_IMG_H    NJ_F_IMG_HEIGHT
#define F_IMG_PX   NJ_F_IMG_PIXELS
#define F_IMG_MUT  NJ_F_IMG_MUTABLE

#define F_G_TGT    NJ_F_G_TARGET
#define F_G_TX     NJ_F_G_TX
#define F_G_TY     NJ_F_G_TY
#define F_G_CX     NJ_F_G_CX
#define F_G_CY     NJ_F_G_CY
#define F_G_CW     NJ_F_G_CW
#define F_G_CH     NJ_F_G_CH
#define F_G_COLOR  NJ_F_G_COLOR
#define F_G_BLEND  NJ_F_G_BLEND
#define F_G_STROKE NJ_F_G_STROKE
#define F_G_FONT   NJ_F_G_FONT
#define F_G_XOR    NJ_F_G_XOR
#define F_G_XORC   NJ_F_G_XORCOLOR

#define F_FONT_FACE  NJ_F_FONT_FACE
#define F_FONT_STYLE NJ_F_FONT_STYLE
#define F_FONT_SIZE  NJ_F_FONT_SIZE

#define F_D_TITLE    NJ_F_D_TITLE
#define F_D_TICKER   NJ_F_D_TICKER
#define F_D_LISTENER NJ_F_D_LISTENER
#define F_D_COMMANDS NJ_F_D_COMMANDS
#define F_D_FULL     NJ_F_D_FULLSCRN

#define F_C_BUFFER   NJ_F_C_BUFFER
#define F_C_WIDTH    NJ_F_C_WIDTH
#define F_C_HEIGHT   NJ_F_C_HEIGHT
#define F_C_KEYS     NJ_F_C_KEYS
#define F_C_VISIBLE  NJ_F_C_VISIBLE
#define F_C_PRESSED  NJ_F_C_PRESSED

#define F_GC_SUPPRESS NJ_F_GC_SUPPRESS
#define F_GC_FULL     NJ_F_GC_FULL

#define F_DISP_CUR   NJ_F_DISP_CUR
#define F_DISP_ALERT NJ_F_DISP_ALERT

#define F_AL_TIMEOUT NJ_F_AL_TIMEOUT
#define F_AL_TYPE    NJ_F_AL_TYPE
#define F_AL_STRING  NJ_F_AL_STRING
#define F_AL_IMAGE   NJ_F_AL_IMAGE
#define F_AL_INDEF   NJ_F_AL_INDEF

/* ------------------------------------------------------------------ */
/* Small field helpers                                                 */
/* ------------------------------------------------------------------ */
static nj_field *fld(nj_vm *vm, const char *cls, const char *name,
                     const char *desc, int is_static)
{
    return nj_field_lookup(vm, cls, name, desc, is_static);
}

static nj_s4 fget(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                  const char *desc)
{
    nj_field *f = fld(vm, cls, name, desc, 0);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void fput(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                 const char *desc, nj_s4 v)
{
    nj_field *f = fld(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static nj_ref rget(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                   const char *desc)
{
    nj_field *f = fld(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void rput(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                 const char *desc, nj_ref v)
{
    nj_field *f = fld(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

static nj_ref rgets(nj_vm *vm, const char *cls, const char *name,
                    const char *desc)
{
    nj_field *f = fld(vm, cls, name, desc, 1);
    return f ? nj_field_read_ref(vm, NULL, f) : NULL;
}

static void rputs(nj_vm *vm, const char *cls, const char *name,
                  const char *desc, nj_ref v)
{
    nj_field *f = fld(vm, cls, name, desc, 1);
    if (f) {
        nj_field_write_ref(vm, NULL, f, v);
    }
}

static nj_s4 sget(nj_vm *vm, const char *cls, const char *name,
                   const char *desc)
{
    nj_field *f = fld(vm, cls, name, desc, 1);
    return f ? nj_field_read_int(vm, NULL, f) : 0;
}

static void sput(nj_vm *vm, const char *cls, const char *name,
                  const char *desc, nj_s4 v)
{
    nj_field *f = fld(vm, cls, name, desc, 1);
    if (f) {
        nj_field_write_int(vm, NULL, f, v);
    }
}

/* Is `o` an instance of `cls` (i.e. would `o instanceof cls` be true)? */
static int isa(nj_vm *vm, nj_ref o, const char *cls)
{
    nj_class *c;

    if (!o || !o->cls) {
        return 0;
    }
    c = nj_class_lookup(vm, cls);
    return c ? nj_class_is_subclass_of(o->cls, c) : 0;
}

/* ------------------------------------------------------------------ */
/* Image                                                               */
/* ------------------------------------------------------------------ */
/* An Image is { imgWidth, imgHeight, imgPixels ([I, ARGB8888), imgMutable }. */

static int img_dims(nj_vm *vm, nj_ref img, int *w, int *h)
{
    nj_field *fw, *fh;

    if (!img) {
        return 0;
    }
    fw = fld(vm, NJ_C_IMAGE, F_IMG_W, D_INT, 0);
    fh = fld(vm, NJ_C_IMAGE, F_IMG_H, D_INT, 0);
    if (!fw || !fh) {
        return 0;
    }
    if (w) { *w = nj_field_read_int(vm, img, fw); }
    if (h) { *h = nj_field_read_int(vm, img, fh); }
    return 1;
}

/* Allocate a transparent-black Image of w x h, or NULL with an exception. */
static nj_ref img_alloc(nj_vm *vm, int w, int h, int mutable)
{
    nj_ref img;
    nj_ref px;
    nj_field *fw, *fh, *fp, *fm;

    if (!vm) {
        return NULL;
    }
    if (w <= 0 || h <= 0) {
        (void)nj_throw(vm, "java/lang/IllegalArgumentException",
                       "image dimensions must be positive");
        return NULL;
    }
    fw = fld(vm, NJ_C_IMAGE, F_IMG_W, D_INT, 0);
    fh = fld(vm, NJ_C_IMAGE, F_IMG_H, D_INT, 0);
    fp = fld(vm, NJ_C_IMAGE, F_IMG_PX, D_INTARR, 0);
    fm = fld(vm, NJ_C_IMAGE, F_IMG_MUT, D_BOOL, 0);
    if (!fw || !fh || !fp || !fm) {
        (void)nj_throw(vm, "java/lang/InternalError", "Image layout");
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
    nj_field_write_int(vm, img, fw, w);
    nj_field_write_int(vm, img, fh, h);
    nj_field_write_int(vm, img, fm, mutable ? 1 : 0);
    nj_field_write_ref(vm, img, fp, px);
    return img;
}

/* Build a stack nj_image view over an Image for the blitters. */
static int img_view(nj_vm *vm, nj_ref img, nj_image *out)
{
    int w = 0, h = 0;
    nj_u4 *px;
    nj_size i, n;

    if (!img || !img_dims(vm, img, &w, &h) || w <= 0 || h <= 0) {
        return 0;
    }
    px = nj_midp_image_pixels(vm, img);
    if (!px) {
        return 0;
    }
    n = (nj_size)w * (nj_size)h;
    out->width = w;
    out->height = h;
    out->format = NJ_PIXFMT_ARGB8888;
    out->pixels = px;
    out->has_alpha = 0;
    for (i = 0; i < n; i++) {
        if ((px[i] >> 24) != 0xFFu) {
            out->has_alpha = 1;
            break;
        }
    }
    return 1;
}

nj_ref nj_midp_image_new(nj_vm *vm, int w, int h, int mutable)
{
    return img_alloc(vm, w, h, mutable);
}

nj_ref nj_midp_image_adopt(nj_vm *vm, nj_u4 *pixels, int w, int h)
{
    nj_ref img;
    nj_ref px;
    nj_field *fp;

    if (!vm || !pixels || w <= 0 || h <= 0) {
        return NULL;
    }
    img = img_alloc(vm, w, h, 0);
    if (!img) {
        return NULL;
    }
    fp = fld(vm, NJ_C_IMAGE, F_IMG_PX, D_INTARR, 0);
    px = fp ? nj_field_read_ref(vm, img, fp) : NULL;
    if (!px) {
        return NULL;
    }
    memcpy(px->u.bytes, pixels, (nj_size)w * (nj_size)h * sizeof(nj_u4));
    return img;
}

int nj_midp_image_size(nj_vm *vm, nj_ref image_obj, int *w, int *h)
{
    int rw = 0, rh = 0;

    if (!img_dims(vm, image_obj, &rw, &rh)) {
        return 0;
    }
    if (w) { *w = rw; }
    if (h) { *h = rh; }
    return 1;
}

nj_u4 *nj_midp_image_pixels(nj_vm *vm, nj_ref image_obj)
{
    nj_ref px;

    if (!image_obj) {
        return NULL;
    }
    px = rget(vm, image_obj, NJ_C_IMAGE, F_IMG_PX, D_INTARR);
    if (!px || px->kind != NJ_OBJ_ARRAY || px->aflags != NJ_ARRAY_T_INT) {
        return NULL;
    }
    return (nj_u4 *)px->u.bytes;
}

/*
 * Expose an Image's Java int[] as an nj_surface without copying: the
 * rasterizer writes straight into the Java heap, which is what makes
 * double-buffered MIDlets cheap.  Images are always ARGB8888 internally.
 */
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
    if (!img_dims(vm, image_obj, &w, &h)) {
        (void)nj_throw(vm, "java/lang/NullPointerException", "image is null");
        return NULL;
    }
    px = nj_midp_image_pixels(vm, image_obj);
    if (!px) {
        (void)nj_throw(vm, "java/lang/IllegalStateException",
                       "image has no pixel store");
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

/* ================================================================== */
/* The framebuffer                                                     */
/* ================================================================== */
/*
 * The screen buffer belongs to the VM (host allocation) so that
 * nj_vm_destroy() releases it, and vm->screen views it.  nj_midp_screen()
 * creates it lazily: nothing is allocated during nj_vm_create() that a MIDlet
 * without a user interface would have to pay for.
 */
nj_surface *nj_midp_screen(nj_vm *vm)
{
    int w, h, fmt = NJ_PIXFMT;
    nj_u1 *buf;
    int bpp;

    if (!vm) {
        return NULL;
    }
    if (vm->screen_pixels && vm->screen.pixels == vm->screen_pixels) {
        return &vm->screen;
    }
    w = (int)vm->screen_w;
    h = (int)vm->screen_h;
    if (w <= 0 || h <= 0) {
        int pw = 0, ph = 0;
        if (vm->port && vm->port->display_init) {
            int stride = 0;
            void *pixels = NULL;
            int pf = vm->port->display_init(vm->port->ud, &pw, &ph, &stride,
                                            &pixels);
            if (pf >= 0) {
                fmt = pf;
            }
        }
        w = (pw > 0) ? pw : NJ_SCREEN_WIDTH;
        h = (ph > 0) ? ph : NJ_SCREEN_HEIGHT;
    }
    if (vm->port) {
        if (vm->port->width > 0)  { w = vm->port->width; }
        if (vm->port->height > 0) { h = vm->port->height; }
    }
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

    /*
     * What can this device do?  Ports describe themselves through the port
     * struct and the pixel format; Canvas.hasPointerEvents() and friends read
     * the answer back from these bits.
     */
    vm->midp_flags = 0u;
    if (vm->port && vm->port->input_poll) {
        vm->midp_flags |= NJ_MIDP_HAS_POINTER;
    }
    vm->midp_flags |= NJ_MIDP_HAS_SOFTKEYS;
    if (fmt == NJ_PIXFMT_ARGB8888) {
        vm->midp_flags |= NJ_MIDP_SUPPORTS_ALPHA;
    }
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

static void screen_present(nj_vm *vm)
{
    if (vm && vm->port && vm->port->display_present && vm->screen.pixels) {
        vm->port->display_present(vm->port->ud, 0, 0, vm->screen.width,
                                  vm->screen.height);
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
    int style, size;

    if (!vm) {
        return NULL;
    }
    if (!vm->fonts.initialized) {
        nj_fontset_init(&vm->fonts);
    }
    if (!font_obj) {
        return nj_fontset_get(&vm->fonts, NJ_FONT_SIZE_MEDIUM,
                              NJ_FONT_STYLE_PLAIN);
    }
    style = (int)fget(vm, font_obj, NJ_C_FONT, F_FONT_STYLE, D_INT);
    size = (int)fget(vm, font_obj, NJ_C_FONT, F_FONT_SIZE, D_INT);
    return nj_fontset_get(&vm->fonts, size, style);
}

/* ================================================================== */
/* Graphics                                                            */
/* ================================================================== */
/*
 * A Java Graphics is the *serialised* form of an nj_gfx.  Every native below
 * follows the same three steps:
 *
 *   1. nj_gfx_load()  : build an nj_gfx on the C stack from the Java fields
 *   2. call exactly one nj_gfx_* primitive
 *   3. nj_gfx_store() : write the mutated fields back
 *
 * Nothing is cached between calls because the Java object is the single source
 * of truth: it survives a GC (which may move nothing today but is allowed to),
 * it can be handed to another thread, and it needs no teardown.
 */
#define G_CLS NJ_C_GRAPHICS

static const nj_font *gfx_font_of(nj_vm *vm, nj_ref g_obj)
{
    nj_ref fo = rget(vm, g_obj, G_CLS, F_G_FONT, D_FONT);
    return nj_midp_font_for(vm, fo);
}

/*
 * Load the Java state into `g`.  `surf_scratch` must outlive the call; it
 * receives the target Image's pixels when the Graphics is bound to one.
 */
static int gfx_load(nj_vm *vm, nj_ref g_obj, nj_gfx *g, nj_surface *surf_scratch)
{
    nj_ref target;
    nj_surface *s;
    int tx, ty, cx, cy, cw, ch;

    if (!vm || !g_obj) {
        return 0;
    }
    target = rget(vm, g_obj, G_CLS, F_G_TGT, D_IMAGE);
    s = nj_midp_surface(vm, target, surf_scratch);
    if (!s) {
        return 0;
    }
    nj_gfx_init(g, s);

    tx = (int)fget(vm, g_obj, G_CLS, F_G_TX, D_INT);
    ty = (int)fget(vm, g_obj, G_CLS, F_G_TY, D_INT);
    g->translate_x = tx;
    g->translate_y = ty;

    cx = (int)fget(vm, g_obj, G_CLS, F_G_CX, D_INT);
    cy = (int)fget(vm, g_obj, G_CLS, F_G_CY, D_INT);
    cw = (int)fget(vm, g_obj, G_CLS, F_G_CW, D_INT);
    ch = (int)fget(vm, g_obj, G_CLS, F_G_CH, D_INT);
    if (cw <= 0 || ch <= 0) {
        /* A freshly built Graphics clips to its whole target. */
        cx = 0;
        cy = 0;
        cw = s->width;
        ch = s->height;
    }
    nj_gfx_set_clip(g, cx + tx, cy + ty, cw, ch);

    nj_gfx_set_color(g, (nj_u4)fget(vm, g_obj, G_CLS, F_G_COLOR, D_INT));
    g->blend = (int)fget(vm, g_obj, G_CLS, F_G_BLEND, D_INT);
    g->stroke = (int)fget(vm, g_obj, G_CLS, F_G_STROKE, D_INT);
    g->font = gfx_font_of(vm, g_obj);
    if (fget(vm, g_obj, G_CLS, F_G_XOR, D_INT) != 0) {
        g->use_xor = 1;
        g->xor_color = (nj_u4)fget(vm, g_obj, G_CLS, F_G_XORC, D_INT);
    }
    return 1;
}

/* Write the mutated nj_gfx state back into the Java object. */
static void gfx_store(nj_vm *vm, nj_ref g_obj, const nj_gfx *g)
{
    nj_field *f;

    if (!vm || !g_obj || !g) {
        return;
    }
    f = fld(vm, G_CLS, F_G_TX, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->translate_x); }
    f = fld(vm, G_CLS, F_G_TY, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->translate_y); }
    /* The clip is kept in *user* space, so getClipX() reports what the caller
     * set even after a translate(). */
    f = fld(vm, G_CLS, F_G_CX, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->clip_x - g->translate_x); }
    f = fld(vm, G_CLS, F_G_CY, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->clip_y - g->translate_y); }
    f = fld(vm, G_CLS, F_G_CW, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->clip_w); }
    f = fld(vm, G_CLS, F_G_CH, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->clip_h); }
    f = fld(vm, G_CLS, F_G_COLOR, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, (nj_s4)g->color); }
    f = fld(vm, G_CLS, F_G_BLEND, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->blend); }
    f = fld(vm, G_CLS, F_G_STROKE, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->stroke); }
    f = fld(vm, G_CLS, F_G_XOR, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, g->use_xor ? 1 : 0); }
    f = fld(vm, G_CLS, F_G_XORC, D_INT, 0);
    if (f) { nj_field_write_int(vm, g_obj, f, (nj_s4)g->xor_color); }
}

/* Font metrics for a Graphics, in its own coordinate space. */
static const nj_font *gfx_font(nj_vm *vm, nj_ref g_obj)
{
    return gfx_font_of(vm, g_obj);
}

/* ---- construction ---- */
NJFN(g_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref target = NJ_ARG_REF(args, 0);

    rput(vm, self, G_CLS, F_G_TGT, D_IMAGE, target);
    {
        int w = 0, h = 0;
        nj_surface *s = nj_midp_surface(vm, target, &(nj_surface){0});
        if (!s) {
            return NJ_ERR;
        }
        w = s->width;
        h = s->height;
        fput(vm, self, G_CLS, F_G_TX, D_INT, 0);
        fput(vm, self, G_CLS, F_G_TY, D_INT, 0);
        fput(vm, self, G_CLS, F_G_CX, D_INT, 0);
        fput(vm, self, G_CLS, F_G_CY, D_INT, 0);
        fput(vm, self, G_CLS, F_G_CW, D_INT, w);
        fput(vm, self, G_CLS, F_G_CH, D_INT, h);
        fput(vm, self, G_CLS, F_G_COLOR, D_INT, (nj_s4)0xFF000000u);
        fput(vm, self, G_CLS, F_G_BLEND, D_INT, NJ_BLEND_SRC_OVER);
        fput(vm, self, G_CLS, F_G_STROKE, D_INT, NJ_STROKE_SOLID);
        fput(vm, self, G_CLS, F_G_XOR, D_INT, 0);
        fput(vm, self, G_CLS, F_G_XORC, D_INT, 0);
        rput(vm, self, G_CLS, F_G_FONT, D_FONT, NULL);
    }
    return NJ_OK;
}

/*
 * Build a Graphics over `target` (NULL = the display) and hand it to the
 * caller.  Used by Image.getGraphics() and by the paint machinery.
 */
static nj_ref gfx_new(nj_vm *vm, nj_ref target)
{
    nj_ref g;
    nj_value a1[1];

    g = nj_object_new(vm, G_CLS);
    if (!g) {
        return NULL;
    }
    a1[0].r = target;
    if (nj_call_special(vm, g, G_CLS, "<init>", "(L" NJ_C_IMAGE ";)V",
                        a1, NULL) != NJ_OK) {
        return NULL;
    }
    return g;
}

/* ---- transform ---- */
NJFN(g_translate)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_translate(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_getTranslateX)
{
    ret->i = fget(vm, NJ_THIS(args), G_CLS, F_G_TX, D_INT);
    return NJ_OK;
}

NJFN(g_getTranslateY)
{
    ret->i = fget(vm, NJ_THIS(args), G_CLS, F_G_TY, D_INT);
    return NJ_OK;
}

/* ---- clipping ---- */
NJFN(g_setClip)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 x = NJ_ARG_INT(args, 0);
    nj_s4 y = NJ_ARG_INT(args, 1);
    nj_s4 w = NJ_ARG_INT(args, 2);
    nj_s4 h = NJ_ARG_INT(args, 3);
    nj_gfx g;
    nj_surface sc;

    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "clip dimensions must not be negative");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_set_clip(&g, (int)x + g.translate_x, (int)y + g.translate_y,
                    (int)w, (int)h);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_clipRect)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_clip_rect(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                     NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_getClipX) { ret->i = fget(vm, NJ_THIS(args), G_CLS, F_G_CX, D_INT); return NJ_OK; }
NJFN(g_getClipY) { ret->i = fget(vm, NJ_THIS(args), G_CLS, F_G_CY, D_INT); return NJ_OK; }
NJFN(g_getClipWidth)
{
    ret->i = fget(vm, NJ_THIS(args), G_CLS, F_G_CW, D_INT);
    return NJ_OK;
}
NJFN(g_getClipHeight)
{
    ret->i = fget(vm, NJ_THIS(args), G_CLS, F_G_CH, D_INT);
    return NJ_OK;
}

/* ---- colour ---- */
NJFN(g_setColor)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_set_color(&g, (nj_u4)NJ_ARG_INT(args, 0));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_setColorRGB)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 r = NJ_ARG_INT(args, 0);
    nj_s4 gg = NJ_ARG_INT(args, 1);
    nj_s4 b = NJ_ARG_INT(args, 2);
    nj_gfx g;
    nj_surface sc;

    if (r < 0 || r > 255 || gg < 0 || gg > 255 || b < 0 || b > 255) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "colour component out of range");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_set_color_rgb(&g, (int)r, (int)gg, (int)b);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_getColor)
{
    ret->i = (nj_s4)((nj_u4)fget(vm, NJ_THIS(args), G_CLS, F_G_COLOR, D_INT) &
                     0x00FFFFFFu);
    return NJ_OK;
}

NJFN(g_getRedComponent)
{
    ret->i = (nj_s4)(((nj_u4)fget(vm, NJ_THIS(args), G_CLS, F_G_COLOR, D_INT)
                      >> 16) & 0xFFu);
    return NJ_OK;
}

NJFN(g_getGreenComponent)
{
    ret->i = (nj_s4)(((nj_u4)fget(vm, NJ_THIS(args), G_CLS, F_G_COLOR, D_INT)
                      >> 8) & 0xFFu);
    return NJ_OK;
}

NJFN(g_getBlueComponent)
{
    ret->i = (nj_s4)((nj_u4)fget(vm, NJ_THIS(args), G_CLS, F_G_COLOR, D_INT)
                     & 0xFFu);
    return NJ_OK;
}

NJFN(g_getAlphaComponent)
{
    ret->i = (nj_s4)(((nj_u4)fget(vm, NJ_THIS(args), G_CLS, F_G_COLOR, D_INT)
                      >> 24) & 0xFFu);
    return NJ_OK;
}

NJFN(g_setAlpha)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 a = NJ_ARG_INT(args, 0);
    nj_gfx g;
    nj_surface sc;

    if (a < 0 || a > 255) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "alpha out of range");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_set_alpha(&g, (int)a);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_getAlpha)
{
    /* The alpha is part of the colour field, so this needs no pixel access. */
    nj_s4 c = fget(vm, NJ_THIS(args), G_CLS, F_G_COLOR, D_INT);
    ret->i = (c >> 24) & 0xFF;
    return NJ_OK;
}

NJFN(g_setGrayScale)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 v = NJ_ARG_INT(args, 0);
    nj_gfx g;
    nj_surface sc;

    if (v < 0 || v > 255) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "grayscale value out of range");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_set_color_rgb(&g, (int)v, (int)v, (int)v);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_setBlendMode)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 mode = NJ_ARG_INT(args, 0);
    nj_gfx g;
    nj_surface sc;

    /* MIDP 2.0: 0 = SRC_OVER, 1 = SRC. */
    if (mode != 0 && mode != 1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown blend mode");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_set_blend(&g, (mode == 0) ? NJ_BLEND_SRC_OVER : NJ_BLEND_NONE);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_getBlendMode)
{
    nj_s4 b = fget(vm, NJ_THIS(args), G_CLS, F_G_BLEND, D_INT);
    ret->i = (b == NJ_BLEND_NONE) ? 1 : 0;
    return NJ_OK;
}

NJFN(g_setStrokeStyle)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 s = NJ_ARG_INT(args, 0);
    nj_gfx g;
    nj_surface sc;

    if (s != 0 && s != 1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown stroke style");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_set_stroke(&g, (s == 0) ? NJ_STROKE_SOLID : NJ_STROKE_DOTTED);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_getStrokeStyle)
{
    nj_s4 s = fget(vm, NJ_THIS(args), G_CLS, F_G_STROKE, D_INT);
    ret->i = (s == NJ_STROKE_DOTTED) ? 1 : 0;
    return NJ_OK;
}

NJFN(g_setFont)
{
    nj_ref self = NJ_THIS(args);
    nj_ref f = NJ_ARG_REF(args, 0);

    if (!f) {
        return nj_throw(vm, "java/lang/NullPointerException", "font is null");
    }
    rput(vm, self, G_CLS, F_G_FONT, D_FONT, f);
    return NJ_OK;
}

NJFN(g_getFont)
{
    nj_ref self = NJ_THIS(args);
    nj_ref f = rget(vm, self, G_CLS, F_G_FONT, D_FONT);

    if (!f) {
        f = rgets(vm, NJ_C_FONT, "sDefault", D_FONT);
        if (!f) {
            nj_value a1[3];
            a1[0].i = 0;
            a1[1].i = 0;
            a1[2].i = 0;
            f = nj_call_ref(vm, NULL, NJ_C_FONT, "getFont",
                            "(III)L" NJ_C_FONT ";", a1);
        }
        if (!f) {
            ret->r = NULL;
            return vm->exception ? NJ_ERR : NJ_OK;
        }
        rput(vm, self, G_CLS, F_G_FONT, D_FONT, f);
    }
    ret->r = f;
    return NJ_OK;
}

NJFN(g_setXORMode)
{
    nj_ref self = NJ_THIS(args);
    nj_ref c = NJ_ARG_REF(args, 0);
    nj_s4 argb = 0x00FFFFFF;

    if (c) {
        argb = fget(vm, c, G_CLS, F_G_COLOR, D_INT);
    }
    fput(vm, self, G_CLS, F_G_XOR, D_INT, 1);
    fput(vm, self, G_CLS, F_G_XORC, D_INT, argb);
    return NJ_OK;
}

/* ---- primitives ---- */
NJFN(g_drawLine)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_line(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawRect)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_rect(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_fillRect)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_fill_rect(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                     NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawRoundRect)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_round_rect(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                      NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3),
                      NJ_ARG_INT(args, 4), NJ_ARG_INT(args, 5));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_fillRoundRect)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_fill_round_rect(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                           NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3),
                           NJ_ARG_INT(args, 4), NJ_ARG_INT(args, 5));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawArc)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_arc(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
               NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3),
               NJ_ARG_INT(args, 4), NJ_ARG_INT(args, 5));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_fillArc)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_fill_arc(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                    NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3),
                    NJ_ARG_INT(args, 4), NJ_ARG_INT(args, 5));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawTriangle)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_triangle(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                    NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3),
                    NJ_ARG_INT(args, 4), NJ_ARG_INT(args, 5), 0);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_fillTriangle)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_triangle(&g, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                    NJ_ARG_INT(args, 2), NJ_ARG_INT(args, 3),
                    NJ_ARG_INT(args, 4), NJ_ARG_INT(args, 5), 1);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

/* ---- text ---- */
NJFN(g_drawString)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_s4 x = NJ_ARG_INT(args, 1);
    nj_s4 y = NJ_ARG_INT(args, 2);
    nj_s4 anchor = NJ_ARG_INT(args, 3);
    nj_gfx g;
    nj_surface sc;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "string is null");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_draw_text(&g, nj_string_chars(s), (int)nj_string_len(s),
                     (int)x, (int)y, (int)anchor);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawString3)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_s4 x = NJ_ARG_INT(args, 1);
    nj_s4 y = NJ_ARG_INT(args, 2);
    nj_gfx g;
    nj_surface sc;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "string is null");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_draw_text(&g, nj_string_chars(s), (int)nj_string_len(s),
                     (int)x, (int)y, NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawSubstring)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 x = NJ_ARG_INT(args, 3);
    nj_s4 y = NJ_ARG_INT(args, 4);
    nj_s4 anchor = NJ_ARG_INT(args, 5);
    nj_gfx g;
    nj_surface sc;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "string is null");
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > nj_string_len(s)) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_draw_text(&g, nj_string_chars(s) + off, (int)len, (int)x, (int)y,
                     (int)anchor);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawChars)
{
    nj_ref self = NJ_THIS(args);
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 x = NJ_ARG_INT(args, 3);
    nj_s4 y = NJ_ARG_INT(args, 4);
    nj_s4 anchor = NJ_ARG_INT(args, 5);
    nj_gfx g;
    nj_surface sc;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", "chars is null");
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > arr->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_draw_text(&g, (const nj_u2 *)arr->u.bytes + off, (int)len,
                     (int)x, (int)y, (int)anchor);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawChar)
{
    nj_ref self = NJ_THIS(args);
    nj_u2 ch = (nj_u2)NJ_ARG_INT(args, 0);
    nj_s4 x = NJ_ARG_INT(args, 1);
    nj_s4 y = NJ_ARG_INT(args, 2);
    nj_s4 anchor = NJ_ARG_INT(args, 3);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_draw_text(&g, &ch, 1, (int)x, (int)y, (int)anchor);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

/* ---- images ---- */
static nj_status gfx_draw_image_common(nj_vm *vm, nj_value *args, int rop,
                                       int transform, int has_clip)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    nj_s4 sx = NJ_ARG_INT(args, 1);
    nj_s4 sy = NJ_ARG_INT(args, 2);
    nj_s4 sw = NJ_ARG_INT(args, 3);
    nj_s4 sh = NJ_ARG_INT(args, 4);
    nj_s4 tx = NJ_ARG_INT(args, 5);
    nj_s4 ty = NJ_ARG_INT(args, 6);
    nj_ref clip = NULL;
    nj_image view;
    nj_gfx g;
    nj_surface sc;

    if (has_clip) {
        clip = NJ_ARG_REF(args, 7);
        if (!clip) {
            return nj_throw(vm, "java/lang/NullPointerException",
                            "clip image is null");
        }
    }
    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image is null");
    }
    if (sw < 0 || sh < 0 || sx < 0 || sy < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative source region");
    }
    if (!img_view(vm, img, &view)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "image has no pixels");
    }
    if (sx + sw > view.width || sy + sh > view.height) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "source region outside the image");
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    if (has_clip) {
        nj_image cview;
        if (!img_view(vm, clip, &cview)) {
            return nj_throw(vm, "java/lang/IllegalArgumentException",
                            "clip image has no pixels");
        }
        nj_gfx_draw_region(&g, &view, (int)sx, (int)sy, (int)sw, (int)sh,
                           transform, g.use_xor, g.xor_color, &cview,
                           (int)tx, (int)ty, rop);
    } else {
        nj_gfx_draw_region(&g, &view, (int)sx, (int)sy, (int)sw, (int)sh,
                           transform, g.use_xor, g.xor_color, NULL,
                           (int)tx, (int)ty, rop);
    }
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawImage)
{
    return gfx_draw_image_common(vm, args, NJ_ROP_PAINT, NJ_TRANS_NONE, 0);
}

NJFN(g_drawRegion4)
{
    /* drawRegion(Image, int x, int y, int transform, int anchor) */
    nj_value a[8];
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    nj_s4 x = NJ_ARG_INT(args, 1);
    nj_s4 y = NJ_ARG_INT(args, 2);
    nj_s4 transform = NJ_ARG_INT(args, 3);
    nj_s4 anchor = NJ_ARG_INT(args, 4);
    nj_image view;
    nj_s4 w, h;
    nj_ref imgs;
    nj_gfx g;
    nj_surface sc;

    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image is null");
    }
    if (anchor != -1 &&
        (anchor & (NJ_ANCHOR_LEFT | NJ_ANCHOR_HCENTER | NJ_ANCHOR_RIGHT)) == 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bad horizontal anchor");
    }
    if (!img_view(vm, img, &view)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "image has no pixels");
    }
    switch ((int)transform) {
    case NJ_TRANS_ROT90: case NJ_TRANS_ROT270:
    case NJ_TRANS_MIRROR_ROT90: case NJ_TRANS_MIRROR_ROT270:
        w = view.height;
        h = view.width;
        break;
    default:
        w = view.width;
        h = view.height;
        break;
    }
    if (anchor != -1) {
        if (anchor & NJ_ANCHOR_HCENTER) { x -= w / 2; }
        else if (anchor & NJ_ANCHOR_RIGHT) { x -= w; }
        if (anchor & NJ_ANCHOR_VCENTER) { y -= h / 2; }
        else if (anchor & NJ_ANCHOR_BOTTOM) { y -= h; }
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_draw_region(&g, &view, 0, 0, view.width, view.height,
                       (int)transform, g.use_xor, g.xor_color, NULL,
                       (int)x, (int)y, NJ_ROP_PAINT);
    gfx_store(vm, self, &g);
    NJ_UNUSED(a);
    NJ_UNUSED(imgs);
    return NJ_OK;
}

NJFN(g_drawRegion8)
{
    return gfx_draw_image_common(vm, args, NJ_ROP_PAINT, NJ_ARG_INT(args, 6), 0);
}

NJFN(g_drawRegion9)
{
    return gfx_draw_image_common(vm, args, NJ_ROP_PAINT, NJ_ARG_INT(args, 6), 1);
}

NJFN(g_drawRGB)
{
    /* drawRGB(int[] rgbData, int offset, int scanlength, int x, int y,
     *         int width, int height, boolean processAlpha) */
    nj_ref self = NJ_THIS(args);
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 offset = NJ_ARG_INT(args, 1);
    nj_s4 scan = NJ_ARG_INT(args, 2);
    nj_s4 x = NJ_ARG_INT(args, 3);
    nj_s4 y = NJ_ARG_INT(args, 4);
    nj_s4 w = NJ_ARG_INT(args, 5);
    nj_s4 h = NJ_ARG_INT(args, 6);
    nj_s4 alpha = NJ_ARG_INT(args, 7);
    nj_u1 *rgb;
    nj_size n;
    nj_gfx g;
    nj_surface sc;
    nj_status st;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", "rgb is null");
    }
    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "negative size");
    }
    if (scan != 0 && w > 0) {
        nj_s4 rows = (h > 0) ? (h - 1) : 0;
        nj_s4 need = rows * scan + w;
        if (scan > 0) {
            if (offset < 0 || (nj_u4)(offset + need) > arr->length) {
                return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                                "rgb data too short");
            }
        } else {
            if (offset < 0 || (nj_u4)(offset + need) > arr->length) {
                return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                                "rgb data too short");
            }
        }
    } else if (offset < 0 || (nj_u4)offset > arr->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (w == 0 || h == 0) {
        return NJ_OK;
    }

    /*
     * nj_gfx takes a tightly packed RGB byte buffer, so repack the Java int[]
     * into one.  The buffer is a host allocation and is released on every exit
     * path, including the error paths below.
     */
    n = (nj_size)w * (nj_size)h * 3u;
    rgb = (nj_u1 *)nj_host_alloc(vm, n);
    if (!rgb) {
        return nj_throw_oom(vm);
    }
    {
        nj_s4 row, col;
        for (row = 0; row < h; row++) {
            for (col = 0; col < w; col++) {
                nj_u4 p = (nj_u4)nj_array_get_int(arr, (nj_u4)(offset + row * scan + col));
                nj_u1 *d = rgb + ((nj_size)row * (nj_size)w + (nj_size)col) * 3u;
                d[0] = (nj_u1)((p >> 16) & 0xFFu);
                d[1] = (nj_u1)((p >> 8) & 0xFFu);
                d[2] = (nj_u1)(p & 0xFFu);
            }
        }
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        nj_host_free(vm, rgb);
        return NJ_ERR;
    }
    st = NJ_OK;
    {
        int rop = NJ_ROP_SET;
        if (g.blend == NJ_BLEND_SRC_OVER || alpha) {
            rop = NJ_ROP_PAINT;
        }
        nj_gfx_draw_rgb(&g, rgb, (int)w * 3, alpha ? 0xFF : 0xFF,
                        0, 0, 0, 0, (int)w, (int)h, rop, NJ_TRANS_NONE);
        (void)x;
        (void)y;
    }
    gfx_store(vm, self, &g);
    nj_host_free(vm, rgb);
    return st;
}

NJFN(g_copyArea)
{
    /* copyArea(int x_src, int y_src, int width, int height, int x_dest,
     *          int y_dest, int anchor) */
    nj_ref self = NJ_THIS(args);
    nj_s4 xs = NJ_ARG_INT(args, 0);
    nj_s4 ys = NJ_ARG_INT(args, 1);
    nj_s4 w = NJ_ARG_INT(args, 2);
    nj_s4 h = NJ_ARG_INT(args, 3);
    nj_s4 xd = NJ_ARG_INT(args, 4);
    nj_s4 yd = NJ_ARG_INT(args, 5);
    nj_s4 anchor = NJ_ARG_INT(args, 6);
    nj_gfx g;
    nj_surface sc;
    nj_u4 *tmp;
    nj_status st = NJ_OK;

    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative size");
    }
    if (anchor != -1 &&
        (anchor & (NJ_ANCHOR_LEFT | NJ_ANCHOR_HCENTER | NJ_ANCHOR_RIGHT)) == 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bad horizontal anchor");
    }
    if (w == 0 || h == 0) {
        return NJ_OK;
    }
    if (anchor != -1) {
        if (anchor & NJ_ANCHOR_HCENTER) { xd -= w / 2; }
        else if (anchor & NJ_ANCHOR_RIGHT) { xd -= w; }
        if (anchor & NJ_ANCHOR_VCENTER) { yd -= h / 2; }
        else if (anchor & NJ_ANCHOR_BOTTOM) { yd -= h; }
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    /*
     * The source and destination overlap (that is the whole point of
     * copyArea), so the region is staged through a scratch buffer.  It is
     * allocated from the host registry and freed on every path out.
     */
    tmp = (nj_u4 *)nj_host_alloc(vm, (nj_size)w * (nj_size)h * sizeof(nj_u4));
    if (!tmp) {
        return nj_throw_oom(vm);
    }
    {
        int row, col;
        for (row = 0; row < h; row++) {
            for (col = 0; col < w; col++) {
                int sxp = (int)xs + col + g.translate_x;
                int syp = (int)ys + row + g.translate_y;
                tmp[(nj_size)row * (nj_size)w + (nj_size)col] =
                    nj_surface_get(g.surf, sxp, syp);
            }
        }
    }
    {
        nj_image view;
        view.width = (int)w;
        view.height = (int)h;
        view.format = NJ_PIXFMT_ARGB8888;
        view.pixels = tmp;
        view.has_alpha = 1;
        nj_gfx_draw_image(&g, &view, (int)xd, (int)yd, 0, 0, (int)w, (int)h);
    }
    gfx_store(vm, self, &g);
    nj_host_free(vm, tmp);
    return st;
}

/* ================================================================== */
/* Image natives                                                       */
/* ================================================================== */
NJFN(img_init_wh)
{
    (void)th;
    (void)ret;
    NJ_UNUSED(args);
    return NJ_OK;   /* the factory calls img_alloc directly */
}

NJFN(img_createImage_wh)
{
    nj_ref img = img_alloc(vm, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1), 1);
    if (!img) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    ret->r = img;
    return NJ_OK;
}

NJFN(img_createImage_img)
{
    /* createImage(Image source, int x, int y, int width, int height,
     *             int transform) */
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_s4 x = NJ_ARG_INT(args, 1);
    nj_s4 y = NJ_ARG_INT(args, 2);
    nj_s4 w = NJ_ARG_INT(args, 3);
    nj_s4 h = NJ_ARG_INT(args, 4);
    nj_s4 transform = NJ_ARG_INT(args, 5);
    nj_image view;
    nj_ref out;
    nj_u4 *dst;
    nj_s4 i, j;

    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", "source is null");
    }
    if (w <= 0 || h <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "image dimensions must be positive");
    }
    if (x < 0 || y < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative source origin");
    }
    if (!img_view(vm, src, &view)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "source image has no pixels");
    }
    if (x + w > view.width || y + h > view.height) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "source region outside the image");
    }
    out = img_alloc(vm, (int)w, (int)h, 0);
    if (!out) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    dst = nj_midp_image_pixels(vm, out);
    if (!dst) {
        return nj_throw(vm, "java/lang/InternalError",
                        "created image has no pixel store");
    }
    /*
     * For the rotated transforms the *destination* is (h x w) and (i,j) runs
     * over that rotated space; untransform() maps it back into the source
     * region exactly like the rasterizer's blitter does.
     */
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            nj_s4 ux = i, uy = j;
            nj_u4 p;
            switch ((int)transform) {
            case NJ_TRANS_ROT90:         ux = j;              uy = h - 1 - i; break;
            case NJ_TRANS_ROT180:        ux = w - 1 - i;      uy = h - 1 - j; break;
            case NJ_TRANS_ROT270:        ux = w - 1 - j;      uy = i;         break;
            case NJ_TRANS_MIRROR:        ux = w - 1 - i;      uy = j;         break;
            case NJ_TRANS_MIRROR_ROT90:  ux = w - 1 - j;      uy = h - 1 - i; break;
            case NJ_TRANS_MIRROR_ROT180: ux = i;              uy = h - 1 - j; break;
            case NJ_TRANS_MIRROR_ROT270: ux = j;              uy = i;         break;
            default:                     ux = i;              uy = j;         break;
            }
            p = view.pixels[(nj_size)(y + uy) * (nj_size)view.width +
                            (nj_size)(x + ux)];
            dst[(nj_size)j * (nj_size)w + (nj_size)i] = p;
        }
    }
    ret->r = out;
    return NJ_OK;
}

/* Byte-array form: decode PNG (or JPEG) through the image loader. */
static nj_status img_decode_into(nj_vm *vm, nj_ref arr, nj_s4 off, nj_s4 len,
                                 nj_ref *out)
{
    nj_image *dec;
    nj_ref img;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", "data is null");
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > arr->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "no image data");
    }
    dec = nj_image_load(arr->u.bytes + off, (nj_u4)len, NULL, NULL, NULL, NULL);
    if (!dec) {
        return nj_throw(vm, "java/io/IOException", "cannot decode image");
    }
    img = nj_midp_image_adopt(vm, dec->pixels, dec->width, dec->height);
    /*
     * nj_image_load() allocated with malloc and handed us the pixels, so the
     * ARGB buffer itself must not be freed by nj_image_free; detach it first.
     */
    {
        void *px = dec->pixels;
        dec->pixels = NULL;
        nj_image_free(dec, NULL, NULL);
        if (!img) {
            (void)px;
            return nj_throw_oom(vm);
        }
    }
    *out = img;
    return NJ_OK;
}

NJFN(img_createImage_bytes)
{
    nj_ref out = NULL;
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_status st;

    if (len == 0) {
        len = (nj_s4)(NJ_ARG_REF(args, 0) ? NJ_ARG_REF(args, 0)->length : 0u) - off;
    }
    st = img_decode_into(vm, NJ_ARG_REF(args, 0), off, len, &out);
    if (st != NJ_OK) {
        return st;
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(img_createImage_bytes3)
{
    nj_ref out = NULL;
    nj_status st = img_decode_into(vm, NJ_ARG_REF(args, 0), NJ_ARG_INT(args, 1),
                                   NJ_ARG_INT(args, 2), &out);
    if (st != NJ_OK) {
        return st;
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(img_createImage_name)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    nj_app *app = (nj_app *)vm->loader_ud;
    char cname[256];
    void *data;
    nj_u4 len = 0u;
    nj_ref out = NULL;
    nj_status st;

    if (!name) {
        return nj_throw(vm, "java/lang/NullPointerException", "name is null");
    }
    if (!app) {
        return nj_throw(vm, "java/io/IOException",
                        "no application is loaded");
    }
    if (nj_string_to_utf8(name, cname, (int)sizeof(cname)) <= 0) {
        return nj_throw(vm, "java/io/IOException", "bad resource name");
    }
    data = nj_app_resource(vm, app, cname, &len);
    if (!data || len == 0u) {
        if (data) {
            nj_host_free(vm, data);
        }
        return nj_throw(vm, "java/io/IOException", cname);
    }
    st = img_decode_into(vm, NULL, 0, 0, &out);
    if (st == NJ_OK) {
        nj_host_free(vm, data);
        ret->r = out;
        return NJ_OK;
    }
    /* Decode straight from the resource buffer. */
    nj_clear_exception(vm);
    {
        nj_image *dec = nj_image_load(data, len, NULL, NULL, NULL, NULL);
        if (!dec) {
            nj_host_free(vm, data);
            return nj_throw(vm, "java/io/IOException", cname);
        }
        out = nj_midp_image_adopt(vm, dec->pixels, dec->width, dec->height);
        dec->pixels = NULL;
        nj_image_free(dec, NULL, NULL);
        nj_host_free(vm, data);
    }
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(img_createRGBImage)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 w = NJ_ARG_INT(args, 1);
    nj_s4 h = NJ_ARG_INT(args, 2);
    nj_s4 process_alpha = NJ_ARG_INT(args, 3);
    nj_ref out;
    nj_u4 *dst;
    nj_s4 i, n;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", "rgb is null");
    }
    if (w <= 0 || h <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "image dimensions must be positive");
    }
    n = w * h;
    if ((nj_u4)n > arr->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    out = img_alloc(vm, (int)w, (int)h, 0);
    if (!out) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    dst = nj_midp_image_pixels(vm, out);
    if (!dst) {
        return nj_throw(vm, "java/lang/InternalError", "created image has no pixels");
    }
    for (i = 0; i < n; i++) {
        nj_u4 p = (nj_u4)nj_array_get_int(arr, (nj_u4)i);
        nj_u4 a = process_alpha ? ((p >> 24) & 0xFFu) : 0xFFu;
        dst[i] = (a << 24) | (p & 0x00FFFFFFu);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(img_getWidth)
{
    int w = 0;
    if (!img_dims(vm, NJ_THIS(args), &w, NULL)) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    ret->i = w;
    return NJ_OK;
}

NJFN(img_getHeight)
{
    int h = 0;
    if (!img_dims(vm, NJ_THIS(args), NULL, &h)) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    ret->i = h;
    return NJ_OK;
}

NJFN(img_isMutable)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_IMAGE, F_IMG_MUT, D_BOOL) ? 1 : 0;
    return NJ_OK;
}

NJFN(img_getGraphics)
{
    nj_ref self = NJ_THIS(args);
    nj_ref g;

    if (!fget(vm, self, NJ_C_IMAGE, F_IMG_MUT, D_BOOL)) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "image is immutable");
    }
    g = gfx_new(vm, self);
    if (!g) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    ret->r = g;
    return NJ_OK;
}

NJFN(img_getRGB)
{
    nj_ref self = NJ_THIS(args);
    nj_ref dst = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 scan = NJ_ARG_INT(args, 2);
    nj_s4 x = NJ_ARG_INT(args, 3);
    nj_s4 y = NJ_ARG_INT(args, 4);
    nj_s4 w = NJ_ARG_INT(args, 5);
    nj_s4 h = NJ_ARG_INT(args, 6);
    nj_u4 *px;
    int iw = 0, ih = 0;
    nj_s4 row, col;

    if (!dst) {
        return nj_throw(vm, "java/lang/NullPointerException", "rgb is null");
    }
    if (!img_dims(vm, self, &iw, &ih)) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "negative size");
    }
    if (x < 0 || y < 0) {
        x = 0;
        y = 0;
    }
    if (x + w > iw) { w = iw - x; }
    if (y + h > ih) { h = ih - y; }
    if (w < 0) { w = 0; }
    if (h < 0) { h = 0; }
    px = nj_midp_image_pixels(vm, self);
    if (!px) {
        return nj_throw(vm, "java/lang/IllegalStateException", "image has no pixels");
    }
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            nj_s4 di = off + row * scan + col;
            if (di < 0 || (nj_u4)di >= dst->length) {
                return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
            }
            nj_array_set_int(dst, (nj_u4)di,
                             (nj_s4)px[(nj_size)(y + row) * (nj_size)iw +
                                       (nj_size)(x + col)]);
        }
    }
    return NJ_OK;
}

/* ================================================================== */
/* Text helpers                                                        */
/* ================================================================== */
/*
 * Drawing text means taking a Java char[] callback.  These helpers centralise
 * the awkward parts: measuring, anchoring, and the UTF-16 array conversion the
 * Font/Gauge widgets need.
 */
static int text_width(nj_vm *vm, nj_ref font_obj, nj_ref str)
{
    const nj_font *f;

    if (!str) {
        return 0;
    }
    f = nj_midp_font_for(vm, font_obj);
    if (!f) {
        return 0;
    }
    return nj_font_string_width(f, nj_string_chars(str), (int)nj_string_len(str));
}

/* Draw a whole String with a MIDP anchor. */
static void text_draw(nj_gfx *g, nj_ref str, int x, int y, int anchor)
{
    if (!str) {
        return;
    }
    nj_gfx_draw_text(g, nj_string_chars(str), (int)nj_string_len(str), x, y,
                     anchor);
}

/* Draw a Java char[] slice with a MIDP anchor. */
static void text_draw_chars(nj_gfx *g, nj_ref chars, int off, int len,
                            int x, int y, int anchor)
{
    if (!chars || len <= 0) {
        return;
    }
    nj_gfx_draw_text(g, (const nj_u2 *)chars->u.bytes + off, len, x, y, anchor);
}

/* Copy up to `cap` characters of a String into a caller buffer. */
static int text_to_chars(nj_ref str, nj_u2 *out, int cap)
{
    int n;

    if (!str) {
        return 0;
    }
    n = (int)nj_string_len(str);
    if (n > cap) {
        n = cap;
    }
    if (n > 0) {
        memcpy(out, nj_string_chars(str), (nj_size)n * sizeof(nj_u2));
    }
    return n;
}

/* ================================================================== */
/* Font                                                                */
/* ================================================================== */
/*
 * Font objects are interned: NFont.getFont() must return the *same* object for
 * equal parameters, because games compare Fonts with == and use them as
 * hashtable keys.  The registry is a Java Object[] in a static field of Font,
 * so it belongs to the VM and dies with it.
 *
 * The index encodes the request:  (faceGroup * 8 + style) * 3 + sizeGroup,
 * stored at array slot index+1 (slot 0 stays null).
 */
#define FONT_FACES 3
#define FONT_STYLES 8
#define FONT_SIZES 3

static int font_size_group(nj_s4 size)
{
    switch ((int)size) {
    case 8:  return 0;   /* SIZE_SMALL  */
    case 16: return 2;   /* SIZE_LARGE  */
    default: return 1;   /* SIZE_MEDIUM */
    }
}

static int font_face_group(nj_s4 face)
{
    switch ((int)face) {
    case 32: return 1;   /* FACE_MONOSPACED   */
    case 64: return 2;   /* FACE_PROPORTIONAL */
    default: return 0;   /* FACE_SYSTEM       */
    }
}

/* Look up (and if necessary build) the interned Font for these parameters. */
static nj_ref font_intern(nj_vm *vm, nj_s4 face, nj_s4 style, nj_s4 size)
{
    int idx;
    nj_ref cache;
    nj_ref f;

    if ((style & ~7) != 0) {
        (void)nj_throw(vm, "java/lang/IllegalArgumentException",
                       "illegal font style");
        return NULL;
    }
    idx = (font_face_group(face) * FONT_STYLES + (int)(style & 7)) * FONT_SIZES +
          font_size_group(size);
    cache = rgets(vm, NJ_C_FONT, "sCache", D_OBJARR);
    if (!cache || (nj_s4)cache->length <= idx + 1) {
        nj_ref nc = nj_object_array_new(vm, "java/lang/Object",
                                        (nj_u4)(idx + 1 + 8));
        nj_u4 i;
        if (!nc) {
            return NULL;
        }
        if (cache) {
            for (i = 0u; i < cache->length; i++) {
                nj_array_set_ref(nc, i, nj_array_get_ref(cache, i));
            }
        }
        rputs(vm, NJ_C_FONT, "sCache", D_OBJARR, nc);
        cache = nc;
    }
    f = nj_array_get_ref(cache, (nj_u4)(idx + 1));
    if (f) {
        return f;
    }
    f = nj_object_new(vm, NJ_C_FONT);
    if (!f) {
        return NULL;
    }
    fput(vm, f, NJ_C_FONT, F_FONT_FACE, D_INT, face);
    fput(vm, f, NJ_C_FONT, F_FONT_STYLE, D_INT, style);
    fput(vm, f, NJ_C_FONT, F_FONT_SIZE, D_INT, size);
    nj_array_set_ref(cache, (nj_u4)(idx + 1), f);
    return f;
}

NJFN(font_getFontStatic1)
{
    nj_ref f = font_intern(vm, 0, 0, 0);
    if (!f) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    rputs(vm, NJ_C_FONT, "sDefault", D_FONT, f);
    ret->r = f;
    return NJ_OK;
}

NJFN(font_getDefaultFont)
{
    nj_ref f = rgets(vm, NJ_C_FONT, "sDefault", D_FONT);
    if (!f) {
        f = font_intern(vm, 0, 0, 0);
        if (!f) {
            return vm->exception ? NJ_ERR : nj_throw_oom(vm);
        }
        rputs(vm, NJ_C_FONT, "sDefault", D_FONT, f);
    }
    ret->r = f;
    return NJ_OK;
}

NJFN(font_getFontStatic3)
{
    nj_ref f = font_intern(vm, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                           NJ_ARG_INT(args, 2));
    if (!f) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    ret->r = f;
    return NJ_OK;
}

NJFN(font_getFace)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_FONT, F_FONT_FACE, D_INT);
    return NJ_OK;
}

NJFN(font_getStyle)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_FONT, F_FONT_STYLE, D_INT);
    return NJ_OK;
}

NJFN(font_getSize)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_FONT, F_FONT_SIZE, D_INT);
    return NJ_OK;
}

NJFN(font_isPlain)
{
    ret->i = (fget(vm, NJ_THIS(args), NJ_C_FONT, F_FONT_STYLE, D_INT) == 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(font_isBold)
{
    ret->i = (fget(vm, NJ_THIS(args), NJ_C_FONT, F_FONT_STYLE, D_INT) & 1) ? 1 : 0;
    return NJ_OK;
}

NJFN(font_isItalic)
{
    ret->i = (fget(vm, NJ_THIS(args), NJ_C_FONT, F_FONT_STYLE, D_INT) & 2) ? 1 : 0;
    return NJ_OK;
}

NJFN(font_isUnderlined)
{
    ret->i = (fget(vm, NJ_THIS(args), NJ_C_FONT, F_FONT_STYLE, D_INT) & 4) ? 1 : 0;
    return NJ_OK;
}

NJFN(font_getHeight)
{
    const nj_font *f = nj_midp_font_for(vm, NJ_THIS(args));
    ret->i = f ? f->height : 0;
    return NJ_OK;
}

NJFN(font_getBaselinePosition)
{
    const nj_font *f = nj_midp_font_for(vm, NJ_THIS(args));
    ret->i = f ? f->ascent : 0;
    return NJ_OK;
}

NJFN(font_getAscent)
{
    const nj_font *f = nj_midp_font_for(vm, NJ_THIS(args));
    ret->i = f ? f->ascent : 0;
    return NJ_OK;
}

NJFN(font_getDescent)
{
    const nj_font *f = nj_midp_font_for(vm, NJ_THIS(args));
    ret->i = f ? f->descent : 0;
    return NJ_OK;
}

NJFN(font_charWidth)
{
    const nj_font *f = nj_midp_font_for(vm, NJ_THIS(args));
    ret->i = f ? nj_font_char_width(f, (nj_u2)NJ_ARG_INT(args, 0)) : 0;
    return NJ_OK;
}

NJFN(font_charsWidth)
{
    const nj_font *f = nj_midp_font_for(vm, NJ_THIS(args));
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > arr->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    ret->i = f ? nj_font_string_width(f, (const nj_u2 *)arr->u.bytes + off,
                                      (int)len) : 0;
    return NJ_OK;
}

NJFN(font_stringWidth)
{
    const nj_font *f = nj_midp_font_for(vm, NJ_THIS(args));
    nj_ref s = NJ_ARG_REF(args, 0);

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    ret->i = f ? nj_font_string_width(f, nj_string_chars(s),
                                      (int)nj_string_len(s)) : 0;
    return NJ_OK;
}

NJFN(font_substringWidth)
{
    const nj_font *f = nj_midp_font_for(vm, NJ_THIS(args));
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > nj_string_len(s)) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    ret->i = f ? nj_font_string_width(f, nj_string_chars(s) + off, (int)len) : 0;
    return NJ_OK;
}

/* ================================================================== */
/* Command / Ticker                                                    */
/* ================================================================== */
NJFN(cmd_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref label = NJ_ARG_REF(args, 1);

    if (!label) {
        return nj_throw(vm, "java/lang/NullPointerException", "label is null");
    }
    fput(vm, self, NJ_C_COMMAND, "cmdType", D_INT, NJ_ARG_INT(args, 0));
    rput(vm, self, NJ_C_COMMAND, "cmdLabel", D_STR, label);
    fput(vm, self, NJ_C_COMMAND, "cmdPriority", D_INT, NJ_ARG_INT(args, 2));
    fput(vm, self, NJ_C_COMMAND, "cmdLongLabel", D_INT, 0);
    return NJ_OK;
}

NJFN(cmd_init_long)
{
    nj_ref self = NJ_THIS(args);
    nj_ref shortl = NJ_ARG_REF(args, 1);
    nj_ref longl = NJ_ARG_REF(args, 2);

    if (!shortl || !longl) {
        return nj_throw(vm, "java/lang/NullPointerException", "label is null");
    }
    fput(vm, self, NJ_C_COMMAND, "cmdType", D_INT, NJ_ARG_INT(args, 0));
    rput(vm, self, NJ_C_COMMAND, "cmdLabel", D_STR, shortl);
    fput(vm, self, NJ_C_COMMAND, "cmdPriority", D_INT, NJ_ARG_INT(args, 3));
    fput(vm, self, NJ_C_COMMAND, "cmdLongLabel", D_INT, 1);
    rput(vm, self, NJ_C_COMMAND, "cmdLong", D_STR, longl);
    return NJ_OK;
}

NJFN(cmd_getCommandType)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_COMMAND, "cmdType", D_INT);
    return NJ_OK;
}

NJFN(cmd_getPriority)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_COMMAND, "cmdPriority", D_INT);
    return NJ_OK;
}

NJFN(cmd_getLabel)
{
    nj_ref l = rget(vm, NJ_THIS(args), NJ_C_COMMAND, "cmdLabel", D_STR);
    if (l && !fget(vm, NJ_THIS(args), NJ_C_COMMAND, "cmdLongLabel", D_INT)) {
        ret->r = l;
        return NJ_OK;
    }
    /* The UI passes the full label; a long-labelled command still answers with
     * its short form from getLabel(). */
    ret->r = l;
    return NJ_OK;
}

NJFN(cmd_getLongLabel)
{
    nj_ref self = NJ_THIS(args);
    nj_ref l = rget(vm, self, NJ_C_COMMAND, "cmdLong", D_STR);
    if (!l) {
        l = rget(vm, self, NJ_C_COMMAND, "cmdLabel", D_STR);
    }
    ret->r = l;
    return NJ_OK;
}

/*
 * compareTo(Command) - not in MIDP, but games sort their own command lists and
 * expect the same ordering the device uses: by type, then priority, then
 * insertion order.
 */
NJFN(cmd_compareTo)
{
    nj_ref self = NJ_THIS(args);
    nj_ref other = NJ_ARG_REF(args, 0);
    nj_s4 t1, t2, p1, p2;

    if (!other) {
        return nj_throw(vm, "java/lang/NullPointerException", "command is null");
    }
    t1 = fget(vm, self, NJ_C_COMMAND, "cmdType", D_INT);
    t2 = fget(vm, other, NJ_C_COMMAND, "cmdType", D_INT);
    p1 = fget(vm, self, NJ_C_COMMAND, "cmdPriority", D_INT);
    p2 = fget(vm, other, NJ_C_COMMAND, "cmdPriority", D_INT);
    if (p1 != p2) {
        ret->i = (p1 < p2) ? -1 : 1;
        return NJ_OK;
    }
    if (t1 != t2) {
        ret->i = (t1 < t2) ? -1 : 1;
        return NJ_OK;
    }
    ret->i = 0;
    return NJ_OK;
}

NJFN(ticker_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    rput(vm, self, NJ_C_TICKER, "tkString", D_STR, s);
    fput(vm, self, NJ_C_TICKER, "tkPhase", D_INT, 0);
    return NJ_OK;
}

NJFN(ticker_getString)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_TICKER, "tkString", D_STR);
    return NJ_OK;
}

NJFN(ticker_setString)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    rput(vm, self, NJ_C_TICKER, "tkString", D_STR, s);
    return NJ_OK;
}

/* ================================================================== */
/* Command dispatch                                                    */
/* ================================================================== */
/*
 * Commands live in a java.util.Vector (dCommands) exactly as nj_midp.h fixes
 * it.  The helpers below build a plain Object[] snapshot for the duration of a
 * dispatch so that dispatching cannot be disturbed by a listener that adds or
 * removes commands - the classic "modify while iterating" bug in a UI toolkit.
 */
static nj_ref cmd_vector(nj_vm *vm, nj_ref displayable)
{
    return rget(vm, displayable, NJ_C_DISPLAYABLE, F_D_COMMANDS, D_VECTOR);
}

static int cmd_count(nj_vm *vm, nj_ref displayable)
{
    nj_ref v = cmd_vector(vm, displayable);
    nj_s4 n;

    if (!v) {
        return 0;
    }
    n = nj_call_int(vm, v, "java/util/Vector", "size", "()I", NULL);
    if (vm->exception) {
        nj_clear_exception(vm);
        return 0;
    }
    return (int)n;
}

static nj_ref cmd_at(nj_vm *vm, nj_ref displayable, int index)
{
    nj_ref v = cmd_vector(vm, displayable);
    nj_value a[1];

    if (!v) {
        return NULL;
    }
    a[0].i = index;
    return nj_call_ref(vm, v, "java/util/Vector", "elementAt",
                       "(I)Ljava/lang/Object;", a);
}

/* Snapshot of the commands, or NULL when there are none. */
static nj_ref cmd_snapshot(nj_vm *vm, nj_ref displayable, int *out_count)
{
    int n = cmd_count(vm, displayable);
    int i;
    nj_ref arr;

    if (out_count) {
        *out_count = 0;
    }
    if (n <= 0) {
        return NULL;
    }
    if (n > 64) {
        n = 64;
    }
    arr = nj_object_array_new(vm, "java/lang/Object", (nj_u4)n);
    if (!arr) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        nj_ref c = cmd_at(vm, displayable, i);
        if (!c) {
            continue;
        }
        nj_array_set_ref(arr, (nj_u4)i, c);
    }
    if (out_count) {
        *out_count = n;
    }
    return arr;
}

/* The command that the left soft key should invoke: highest priority first. */
static nj_ref cmd_default(nj_vm *vm, nj_ref displayable, int *out_index)
{
    nj_ref arr;
    int n = 0, i;
    int best = -1;
    nj_s4 best_pri = 0;
    nj_s4 best_type = 0;

    if (out_index) {
        *out_index = -1;
    }
    if (!displayable) {
        return NULL;
    }
    arr = cmd_snapshot(vm, displayable, &n);
    if (!arr) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        nj_ref c = nj_array_get_ref(arr, (nj_u4)i);
        nj_s4 pri, type;
        if (!c) {
            continue;
        }
        pri = fget(vm, c, NJ_C_COMMAND, "cmdPriority", D_INT);
        type = fget(vm, c, NJ_C_COMMAND, "cmdType", D_INT);
        if (best < 0 || pri < best_pri ||
            (pri == best_pri && type < best_type)) {
            best = i;
            best_pri = pri;
            best_type = type;
        }
    }
    if (best < 0) {
        return NULL;
    }
    if (out_index) {
        *out_index = best;
    }
    return nj_array_get_ref(arr, (nj_u4)best);
}

/* Deliver commandAction(Command, Displayable) to the listener. */
static int command_dispatch(nj_vm *vm, nj_ref displayable, nj_ref command)
{
    nj_ref listener;
    nj_value a[2];

    if (!displayable || !command) {
        return 0;
    }
    listener = rget(vm, displayable, NJ_C_DISPLAYABLE, F_D_LISTENER, D_CL);
    if (!listener) {
        return 0;
    }
    a[0].r = command;
    a[1].r = displayable;
    (void)nj_call(vm, listener, NJ_C_COMMANDLISTENER, "commandAction",
                  "(L" NJ_C_COMMAND ";L" NJ_C_DISPLAYABLE ";)V", a, NULL);
    return 1;
}

/* Activate the default command, if there is one. */
static int command_default_activate(nj_vm *vm, nj_ref displayable)
{
    nj_ref c = cmd_default(vm, displayable, NULL);
    if (!c) {
        return 0;
    }
    return command_dispatch(vm, displayable, c);
}

/* Deliver itemCommandAction(Command, Item). */
static int item_command_dispatch(nj_vm *vm, nj_ref item, nj_ref command)
{
    nj_ref listener;
    nj_value a[2];

    if (!item || !command) {
        return 0;
    }
    listener = rget(vm, item, NJ_C_ITEM, "itCmdListener", D_ICL);
    if (!listener) {
        return 0;
    }
    a[0].r = command;
    a[1].r = item;
    (void)nj_call(vm, listener, NJ_C_ITEMCOMMANDLISTENER, "commandAction",
                  "(L" NJ_C_COMMAND ";L" NJ_C_ITEM ";)V", a, NULL);
    return 1;
}

/* Notify the ItemStateListener of the enclosing screen, if any. */
static void item_state_changed(nj_vm *vm, nj_ref item)
{
    nj_ref screen;
    nj_ref listener;
    nj_value a[1];

    if (!item) {
        return;
    }
    screen = rget(vm, item, NJ_C_ITEM, "itOwner", D_FORM);
    if (!screen) {
        screen = rget(vm, item, NJ_C_ITEM, "itOwner", NJ_C_DISPLAYABLE);
    }
    if (!screen) {
        return;
    }
    listener = rget(vm, screen, NJ_C_FORM, "fmItemListener", D_ISL);
    if (!listener) {
        return;
    }
    a[0].r = item;
    (void)nj_call(vm, listener, NJ_C_ITEMSTATELISTENER, "itemStateChanged",
                  "(L" NJ_C_ITEM ";)V", a, NULL);
}

/* ================================================================== */
/* Displayable                                                         */
/* ================================================================== */
NJFN(d_init)
{
    nj_ref self = NJ_THIS(args);

    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TITLE, D_STR, NULL);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TICKER, D_TICKER, NULL);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_LISTENER, D_CL, NULL);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_COMMANDS, D_VECTOR, NULL);
    fput(vm, self, NJ_C_DISPLAYABLE, F_D_FULL, D_BOOL, 0);
    return NJ_OK;
}

NJFN(d_getTitle)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_DISPLAYABLE, F_D_TITLE, D_STR);
    return NJ_OK;
}

NJFN(d_setTitle)
{
    nj_ref self = NJ_THIS(args);
    nj_ref t = NJ_ARG_REF(args, 0);
    if (!t) {
        return nj_throw(vm, "java/lang/NullPointerException", "title is null");
    }
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TITLE, D_STR, t);
    return NJ_OK;
}

NJFN(d_getTicker)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_DISPLAYABLE, F_D_TICKER, D_TICKER);
    return NJ_OK;
}

NJFN(d_setTicker)
{
    rput(vm, NJ_THIS(args), NJ_C_DISPLAYABLE, F_D_TICKER, D_TICKER,
         NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/* Lazily create the command Vector. */
static nj_ref cmd_vector_for(nj_vm *vm, nj_ref displayable)
{
    nj_ref v = cmd_vector(vm, displayable);

    if (v) {
        return v;
    }
    v = nj_object_new(vm, "java/util/Vector");
    if (!v) {
        return NULL;
    }
    if (nj_call_special(vm, v, "java/util/Vector", "<init>", "()V", NULL,
                        NULL) != NJ_OK) {
        return NULL;
    }
    rput(vm, displayable, NJ_C_DISPLAYABLE, F_D_COMMANDS, D_VECTOR, v);
    return v;
}

NJFN(d_addCommand)
{
    nj_ref self = NJ_THIS(args);
    nj_ref c = NJ_ARG_REF(args, 0);
    nj_ref v;
    nj_value a[1];

    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", "command is null");
    }
    v = cmd_vector_for(vm, self);
    if (!v) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    a[0].r = c;
    (void)nj_call(vm, v, "java/util/Vector", "addElement",
                  "(Ljava/lang/Object;)V", a, NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(d_removeCommand)
{
    nj_ref self = NJ_THIS(args);
    nj_ref c = NJ_ARG_REF(args, 0);
    nj_ref v = cmd_vector(vm, self);
    nj_value a[1];

    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", "command is null");
    }
    if (!v) {
        return NJ_OK;
    }
    a[0].r = c;
    (void)nj_call(vm, v, "java/util/Vector", "removeElement",
                  "(Ljava/lang/Object;)Z", a, NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(d_setCommandListener)
{
    rput(vm, NJ_THIS(args), NJ_C_DISPLAYABLE, F_D_LISTENER, D_CL,
         NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(d_getCommandListener)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_DISPLAYABLE, F_D_LISTENER, D_CL);
    return NJ_OK;
}

NJFN(d_getWidth)
{
    nj_ref self = NJ_THIS(args);
    if (isa(vm, self, NJ_C_CANVAS)) {
        ret->i = fget(vm, self, NJ_C_CANVAS, F_C_WIDTH, D_INT);
    } else {
        ret->i = 0;
    }
    return NJ_OK;
}

NJFN(d_getHeight)
{
    nj_ref self = NJ_THIS(args);
    if (isa(vm, self, NJ_C_CANVAS)) {
        ret->i = fget(vm, self, NJ_C_CANVAS, F_C_HEIGHT, D_INT);
    } else {
        ret->i = 0;
    }
    return NJ_OK;
}

NJFN(d_isShown)
{
    nj_ref self = NJ_THIS(args);
    nj_ref cur = nj_midp_current(vm);

    if (!cur || cur != self) {
        ret->i = 0;
        return NJ_OK;
    }
    if (isa(vm, self, NJ_C_CANVAS)) {
        ret->i = fget(vm, self, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL) ? 1 : 0;
    } else {
        ret->i = 1;
    }
    return NJ_OK;
}

NJFN(d_setFullScreenMode)
{
    nj_ref self = NJ_THIS(args);
    nj_ref cur = nj_midp_current(vm);
    nj_s4 on = NJ_ARG_INT(args, 0) ? 1 : 0;

    if (cur == self && isa(vm, self, NJ_C_CANVAS) &&
        (fget(vm, self, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL) != 0) &&
        (fget(vm, self, NJ_C_DISPLAYABLE, F_D_FULL, D_BOOL) != on)) {
        fput(vm, self, NJ_C_DISPLAYABLE, F_D_FULL, D_BOOL, on);
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "setFullScreenMode() on a visible Displayable");
    }
    fput(vm, self, NJ_C_DISPLAYABLE, F_D_FULL, D_BOOL, on);
    if (isa(vm, self, NJ_C_GAMECANVAS)) {
        fput(vm, self, NJ_C_GAMECANVAS, F_GC_FULL, D_BOOL, on);
    }
    return NJ_OK;
}

NJFN(d_isFullScreenMode)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_DISPLAYABLE, F_D_FULL, D_BOOL) ? 1 : 0;
    return NJ_OK;
}

NJFN(d_sizeChanged)
{
    nj_ref self = NJ_THIS(args);
    nj_ref cur = nj_midp_current(vm);

    if (cur == self) {
        nj_midp_repaint(vm);
    }
    return NJ_OK;
}

NJFN(d_getKeyName)
{
    nj_s4 code = NJ_ARG_INT(args, 0);
    const char *name = NULL;
    char buf[8];

    switch ((int)code) {
    case -1: name = "UP"; break;
    case -2: name = "DOWN"; break;
    case -3: name = "LEFT"; break;
    case -4: name = "RIGHT"; break;
    case -5: name = "SELECT"; break;
    case -6: name = "SOFTKEY1"; break;
    case -7: name = "SOFTKEY2"; break;
    case -21: name = "SOFTKEY3"; break;
    case 42: name = "*"; break;
    case 35: name = "#"; break;
    default:
        if (code >= 48 && code <= 57) {
            buf[0] = (char)code;
            buf[1] = '\0';
            name = buf;
        } else if (code > 0) {
            name = "KEY";
        }
        break;
    }
    if (!name) {
        ret->r = NULL;
        return NJ_OK;
    }
    ret->r = nj_string_new(vm, name);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* ================================================================== */
/* Canvas                                                              */
/* ================================================================== */
/* Map between the port's NJ_KEY_* codes and the MIDP key codes. */
static int nj_key_to_midp(int key)
{
    switch (key) {
    case NJ_KEY_0: case NJ_KEY_1: case NJ_KEY_2: case NJ_KEY_3:
    case NJ_KEY_4: case NJ_KEY_5: case NJ_KEY_6: case NJ_KEY_7:
    case NJ_KEY_8: case NJ_KEY_9:
        return 48 + (key - NJ_KEY_0);
    case NJ_KEY_STAR:  return NJ_KEY_STAR;
    case NJ_KEY_POUND: return NJ_KEY_POUND;
    case NJ_KEY_UP:    return NJ_KEY_UP;
    case NJ_KEY_DOWN:  return NJ_KEY_DOWN;
    case NJ_KEY_LEFT:  return NJ_KEY_LEFT;
    case NJ_KEY_RIGHT: return NJ_KEY_RIGHT;
    case NJ_KEY_FIRE:  return NJ_KEY_FIRE;
    case NJ_KEY_SOFT_LEFT:  return -6;
    case NJ_KEY_SOFT_RIGHT: return -7;
    case NJ_KEY_GAME_A: return NJ_KEY_GAME_A;
    case NJ_KEY_GAME_B: return NJ_KEY_GAME_B;
    case NJ_KEY_GAME_C: return NJ_KEY_GAME_C;
    case NJ_KEY_GAME_D: return NJ_KEY_GAME_D;
    default: return 0;
    }
}

/* Map a MIDP key code to the game action it implies. */
static int midp_game_action(nj_s4 code)
{
    switch ((int)code) {
    case NJ_KEY_UP:    return NJ_GAME_UP;
    case NJ_KEY_DOWN:  return NJ_GAME_DOWN;
    case NJ_KEY_LEFT:  return NJ_GAME_LEFT;
    case NJ_KEY_RIGHT: return NJ_GAME_RIGHT;
    case NJ_KEY_FIRE:  return NJ_GAME_FIRE;
    case NJ_KEY_GAME_A: return NJ_GAME_A;
    case NJ_KEY_GAME_B: return NJ_GAME_B;
    case NJ_KEY_GAME_C: return NJ_GAME_C;
    case NJ_KEY_GAME_D: return NJ_GAME_D;
    case 50: return NJ_GAME_UP;      /* KEY_NUM2 */
    case 56: return NJ_GAME_DOWN;    /* KEY_NUM8 */
    case 52: return NJ_GAME_LEFT;    /* KEY_NUM4 */
    case 54: return NJ_GAME_RIGHT;   /* KEY_NUM6 */
    case 53: return NJ_GAME_FIRE;    /* KEY_NUM5 */
    default: return 0;
    }
}

static int game_action_to_key(int action)
{
    switch (action) {
    case NJ_GAME_UP:    return NJ_KEY_UP;
    case NJ_GAME_DOWN:  return NJ_KEY_DOWN;
    case NJ_GAME_LEFT:  return NJ_KEY_LEFT;
    case NJ_GAME_RIGHT: return NJ_KEY_RIGHT;
    case NJ_GAME_FIRE:  return NJ_KEY_FIRE;
    case NJ_GAME_A:     return NJ_KEY_GAME_A;
    case NJ_GAME_B:     return NJ_KEY_GAME_B;
    case NJ_GAME_C:     return NJ_KEY_GAME_C;
    case NJ_GAME_D:     return NJ_KEY_GAME_D;
    default:            return 0;
    }
}

/* Make sure cBuffer is an Image of exactly w x h and hand it back. */
static nj_ref canvas_ensure_buffer(nj_vm *vm, nj_ref canvas, int w, int h)
{
    nj_ref buf = rget(vm, canvas, NJ_C_CANVAS, F_C_BUFFER, D_IMAGE);
    int bw = 0, bh = 0;

    fput(vm, canvas, NJ_C_CANVAS, F_C_WIDTH, D_INT, w);
    fput(vm, canvas, NJ_C_CANVAS, F_C_HEIGHT, D_INT, h);
    if (buf && img_dims(vm, buf, &bw, &bh) && bw == w && bh == h) {
        return buf;
    }
    buf = img_alloc(vm, w, h, 0);
    if (!buf) {
        return NULL;
    }
    rput(vm, canvas, NJ_C_CANVAS, F_C_BUFFER, D_IMAGE, buf);
    return buf;
}

NJFN(c_init)
{
    nj_ref self = NJ_THIS(args);

    (void)d_init(vm, th, args, ret);
    rput(vm, self, NJ_C_CANVAS, F_C_BUFFER, D_IMAGE, NULL);
    fput(vm, self, NJ_C_CANVAS, F_C_WIDTH, D_INT, 0);
    fput(vm, self, NJ_C_CANVAS, F_C_HEIGHT, D_INT, 0);
    fput(vm, self, NJ_C_CANVAS, F_C_KEYS, D_INT, 0);
    fput(vm, self, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL, 0);
    fput(vm, self, NJ_C_CANVAS, F_C_PRESSED, D_BOOL, 0);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(c_getWidth)
{
    nj_ref self = NJ_THIS(args);
    if (fget(vm, self, NJ_C_CANVAS, F_C_WIDTH, D_INT) <= 0) {
        nj_surface *s = nj_midp_screen(vm);
        if (s) {
            fput(vm, self, NJ_C_CANVAS, F_C_WIDTH, D_INT, s->width);
            fput(vm, self, NJ_C_CANVAS, F_C_HEIGHT, D_INT, s->height);
        }
    }
    ret->i = fget(vm, self, NJ_C_CANVAS, F_C_WIDTH, D_INT);
    return NJ_OK;
}

NJFN(c_getHeight)
{
    nj_ref self = NJ_THIS(args);
    if (fget(vm, self, NJ_C_CANVAS, F_C_HEIGHT, D_INT) <= 0) {
        nj_surface *s = nj_midp_screen(vm);
        if (s) {
            fput(vm, self, NJ_C_CANVAS, F_C_WIDTH, D_INT, s->width);
            fput(vm, self, NJ_C_CANVAS, F_C_HEIGHT, D_INT, s->height);
        }
    }
    ret->i = fget(vm, self, NJ_C_CANVAS, F_C_HEIGHT, D_INT);
    return NJ_OK;
}

NJFN(c_isDoubleBuffered)
{
    ret->i = 1;
    return NJ_OK;
}

NJFN(c_hasPointerEvents)
{
    ret->i = (vm->midp_flags & NJ_MIDP_HAS_POINTER) ? 1 : 0;
    return NJ_OK;
}

NJFN(c_hasPointerMotionEvents)
{
    ret->i = (vm->midp_flags & NJ_MIDP_HAS_POINTER) ? 1 : 0;
    return NJ_OK;
}

NJFN(c_hasRepeatEvents)
{
    ret->i = 0;
    return NJ_OK;
}

NJFN(c_getGameAction)
{
    ret->i = midp_game_action(NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(c_getKeyCode)
{
    ret->i = game_action_to_key(NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(c_getKeyName)
{
    return d_getKeyName(vm, th, args, ret);
}

NJFN(c_repaint)
{
    nj_ref self = NJ_THIS(args);
    nj_ref cur = nj_midp_current(vm);
    nj_s4 x = NJ_ARG_INT(args, 0);
    nj_s4 y = NJ_ARG_INT(args, 1);
    nj_s4 w = NJ_ARG_INT(args, 2);
    nj_s4 h = NJ_ARG_INT(args, 3);

    NJ_UNUSED(x);
    NJ_UNUSED(y);
    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative repaint rectangle");
    }
    if (cur != self) {
        /* MIDP silently ignores repaint() on a Displayable that is not shown;
         * it must NOT become visible as a side effect. */
        return NJ_OK;
    }
    nj_midp_repaint(vm);
    return NJ_OK;
}

NJFN(c_repaint_all)
{
    return c_repaint(vm, th, args, ret);
}

NJFN(c_serviceRepaints)
{
    /* The runner repaints every frame; there is no separate event loop to
     * flush, so this is deliberately a no-op. */
    return NJ_OK;
}

NJFN(c_setFullScreenMode)
{
    return d_setFullScreenMode(vm, th, args, ret);
}
