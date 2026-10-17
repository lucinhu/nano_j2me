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
#include <stdlib.h>
#include "nj2me/nj_gfx.h"
#include "nj2me/nj_png.h"
#include "nj2me/nj_codec.h"

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
#define D_DISPLAY "Ljavax/microedition/lcdui/Displayable;"
#define D_DISPLAYS "Ljavax/microedition/lcdui/Display;"
#define D_CL      "Ljavax/microedition/lcdui/CommandListener;"
#define D_ICL     "Ljavax/microedition/lcdui/ItemCommandListener;"
#define D_ISL     "Ljavax/microedition/lcdui/ItemStateListener;"
#define D_RUN     "Ljava/lang/Runnable;"

/*
 * Field names come straight from nj_midp.h, which is the ABI between MIDP
 * modules: an Image is { imgWidth, imgHeight, imgPixels, imgMutable } and the
 * game package looks the very same names up.
 */
/*
 * javax.microedition.lcdui.Choice modes, shared by ChoiceGroup and List.
 * EXCLUSIVE and MULTIPLE are deliberately in MIDP's order, not the intuitive
 * one - mixing them up silently turns radio buttons into check boxes.
 */
#define NJ_CHOICE_EXCLUSIVE 1
#define NJ_CHOICE_MULTIPLE  2
#define NJ_CHOICE_IMPLICIT  3

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

/* Defined with the Display natives, used by the frame hook above them. */
static void display_drain_queue(nj_vm *vm);

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

/* A NULL object from nj_object_new() already carries either
 * NoClassDefFoundError(<class>) or OutOfMemoryError.  Only add the OOM when the
 * call failed silently, so a failed MIDlet start always names the class that
 * could not be loaded rather than reporting a bare error. */
static nj_status new_failed(nj_vm *vm)
{
    return vm->exception ? NJ_ERR : nj_throw_oom(vm);
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
    nj_u4 mark;

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
    /*
     * The pixel array is w*h ints: a 295x279 Image asks for 329 KB, which is
     * enough that allocating it runs a collection.  The half-built Image
     * exists only in this C local until it is returned, so it has to be
     * rooted across that call - otherwise the sweep frees it and writes a
     * free-list link over its first word, and the caller gets a dangling
     * Image whose `cls` is not an nj_class at all.  The symptom is nowhere
     * near here: storing that Image into an Image[] throws
     * ArrayStoreException (the assignability test dereferences the bogus
     * cls), and a game that catches its own loader failure then paints a
     * null Image every frame - 火影忍者-终极格斗_6604 lost its first frame
     * that way, at e.paint+376.
     */
    mark = nj_root_mark(vm);
    img = nj_object_new(vm, NJ_C_IMAGE);
    if (!img) {
        nj_root_release(vm, mark);
        return NULL;
    }
    img = nj_root(vm, img);
    px = nj_int_array_new(vm, (nj_u4)w * (nj_u4)h);
    if (!px) {
        nj_root_release(vm, mark);
        return NULL;
    }
    px = nj_root(vm, px);
    if (mutable) {
        /*
         * MIDP 2.0, Image.createImage(int width, int height): "Every pixel
         * within the newly created image is white", and of the class: "Every
         * pixel within a mutable image is always fully opaque."  A Java int[]
         * arrives zeroed, which is transparent BLACK.  A title builds its
         * scenery in one of these: it draws a background into part of it,
         * blends sprites over it, then blits it - and against black the
         * sprites come out dark while the pixels it never touched see through
         * to whatever is behind the image.  合金弹头2013BT版_678 re-blends
         * into the same buffer every frame, so that read as scenery that
         * flickers between the two.
         */
        nj_u4 *mp = (nj_u4 *)px->u.bytes;
        nj_size n = (nj_size)w * (nj_size)h, k;
        for (k = 0; k < n; k++) {
            mp[k] = 0xFFFFFFFFu;
        }
    }
    nj_field_write_int(vm, img, fw, w);
    nj_field_write_int(vm, img, fh, h);
    nj_field_write_int(vm, img, fm, mutable ? 1 : 0);
    nj_field_write_ref(vm, img, fp, px);
    nj_root_release(vm, mark);
    return img;
}

/* Build a stack nj_image view over an Image for the blitters. */
static int img_view(nj_vm *vm, nj_ref img, nj_image *out)
{
    int w = 0, h = 0;
    nj_u4 *px;

    if (!img || !img_dims(vm, img, &w, &h) || w <= 0 || h <= 0) {
        return 0;
    }
    px = nj_midp_image_pixels(vm, img);
    if (!px) {
        return 0;
    }
    out->width = w;
    out->height = h;
    out->format = NJ_PIXFMT_ARGB8888;
    out->pixels = px;
    /* NOT measured.  The rasteriser decides per pixel whether to copy or to
       blend, so the only thing a pre-scan buys is the scan itself - and for a
       full-screen image that is 76,800 reads per blit, every frame (23% of the
       sampled time in a 1200-frame run of 合金弹头4(完美汉化版)_4053, whose
       whole screen is one opaque image).  Nothing reads this field. */
    out->has_alpha = 0;
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
    /*
     * Already up?  Ask the SURFACE, not the ownership flag.
     *
     * The original guard was `screen_pixels && screen.pixels == screen_pixels`,
     * which is only true when the runtime allocated the buffer itself.  With a
     * port-owned framebuffer screen_pixels is NULL, so the guard never fired:
     * the second caller fell through, skipped the w<=0 block (screen_w was now
     * set), and allocated a fresh private buffer over the top.  The first
     * caller got the port's memory and everyone after got a private copy, so
     * the runtime rendered into one buffer while the port presented the other -
     * a permanently black screen with no error anywhere.
     */
    if (vm->screen.pixels && vm->screen_w > 0 && vm->screen_h > 0) {
        return &vm->screen;
    }
    w = (int)vm->screen_w;
    h = (int)vm->screen_h;
    if (w <= 0 || h <= 0) {
        int pw = 0, ph = 0, pstride = 0;
        void *ppixels = NULL;
        if (vm->port && vm->port->display_init) {
            int pf = vm->port->display_init(vm->port->ud, &pw, &ph, &pstride,
                                            &ppixels);
            if (pf >= 0) {
                fmt = pf;
            }
        }
        w = (pw > 0) ? pw : NJ_SCREEN_WIDTH;
        h = (ph > 0) ? ph : NJ_SCREEN_HEIGHT;

        /*
         * USE THE PORT'S BUFFER.
         *
         * display_init() is not a way to ask for the screen size - it is how a
         * port publishes the framebuffer it wants drawn into, and the port
         * presents from exactly that memory (the headless port writes its PPM
         * frames from it, an ESP32 DMAs it to the panel, an Android port copies
         * it into the window).
         *
         * Allocating a second, private buffer here looks harmless and is the
         * worst possible failure: the runtime renders correctly into one
         * buffer, the port faithfully presents the other, and the screen is
         * black forever with no error anywhere.  It survived a long time
         * because every unit test reads the framebuffer through this function
         * rather than through the port.
         *
         * The port owns this memory and releases it in its shutdown hook, so
         * screen_pixels stays NULL to mark it as "not ours to free".
         */
        if (ppixels && pstride > 0) {
            int bpp0 = (fmt == NJ_PIXFMT_ARGB8888) ? 4
                       : ((fmt == NJ_PIXFMT_RGB888) ? 3 : 2);
            vm->screen_pixels = NULL;
            vm->screen.pixels = ppixels;
            vm->screen.width = w;
            vm->screen.height = h;
            vm->screen.stride = pstride;
            vm->screen.format = fmt;
            vm->screen.bpp = bpp0;
            vm->screen_w = (nj_u2)w;
            vm->screen_h = (nj_u2)h;
            goto screen_ready;
        }
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

screen_ready:
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

void nj_midp_present(nj_vm *vm)
{

    if (!vm) {
        return;
    }
    {
        /*
         * Mirror the current Canvas' buffer onto the screen HERE, at the frame
         * boundary, rather than at the end of nj_midp_paint().  A title may
         * keep the Graphics that paint() handed it and go on drawing with it
         * after returning; a blit taken at the end of paint() can never show
         * that drawing, and the pixels it wrote are then overwritten by the
         * next paint.  合金弹头2013BT版_678 loses its foreground scenery layer
         * on alternate frames for exactly that reason.
         *
         * canvas_ensure_buffer() is deliberately not called: a present must not
         * change the canvas' geometry, it only shows what is already there.
         */
        nj_ref cur = nj_midp_current(vm);

        if (cur && isa(vm, cur, NJ_C_CANVAS) && !isa(vm, cur, NJ_C_GAMECANVAS)
            && vm->screen.pixels) {
            nj_surface *scr = nj_midp_screen(vm);
            nj_ref buf = rget(vm, cur, NJ_C_CANVAS, F_C_BUFFER, D_IMAGE);
            nj_image view;

            if (scr && buf && img_view(vm, buf, &view)) {
                nj_gfx blit;
                nj_gfx_init(&blit, scr);
                nj_gfx_draw_image(&blit, &view, 0, 0, 0, 0, view.width,
                                  view.height);
            }
        }
    }
    if (vm->port && vm->port->display_present && vm->screen.pixels) {
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
    /*
     * Every Graphics method - draw and query alike - comes through here, so
     * this is the one place a call count can be taken without touching the
     * pixel path.  A frame that was painted but is empty shows up as
     * paints rising while this does not.
     */
    vm->gfx_calls++;
    nj_gfx_init(g, s);

    tx = (int)fget(vm, g_obj, G_CLS, F_G_TX, D_INT);
    ty = (int)fget(vm, g_obj, G_CLS, F_G_TY, D_INT);
    g->translate_x = tx;
    g->translate_y = ty;

    cx = (int)fget(vm, g_obj, G_CLS, F_G_CX, D_INT);
    cy = (int)fget(vm, g_obj, G_CLS, F_G_CY, D_INT);
    cw = (int)fget(vm, g_obj, G_CLS, F_G_CW, D_INT);
    ch = (int)fget(vm, g_obj, G_CLS, F_G_CH, D_INT);
    /*
     * No fallback here on purpose.  A non-positive size is an EMPTY clip, which
     * is what setClip() asked for, and gfx_new() has already written the whole
     * target into these fields for a Graphics nobody has clipped yet - so the
     * two cases are told apart at construction instead of being guessed at on
     * every load.  Reinstating a "cw <= 0 means the whole surface" fallback
     * here reintroduces the bug that tests/test_clip.c exists for.
     */
    /*
     * The clip fields (NJ_F_G_CX/CY/CW/CH) hold USER-space coordinates, exactly
     * like Graphics.getClipX(), and nj_gfx_set_clip() applies the translate
     * itself - so the stored values are passed through unchanged.  Adding the
     * translate here would count it twice and the clip would drift by (tx,ty)
     * between setClip() and every following draw.
     */
    nj_gfx_set_clip(g, cx, cy, cw, ch);

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

/*
 * See nj_midp.h.  The colour is forced opaque for the same reason
 * Graphics.setColor(int) does it: a triangle with alpha 0 would be invisible
 * and the caller would have no way to tell that from a projection that put it
 * off screen.
 */
int nj_midp_gfx_triangle(nj_vm *vm, nj_ref g_obj,
                         nj_s4 x1, nj_s4 y1, nj_s4 x2, nj_s4 y2,
                         nj_s4 x3, nj_s4 y3, nj_u4 argb)
{
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, g_obj, &g, &sc)) {
        return 0;
    }
    nj_gfx_set_color(&g, argb | 0xFF000000u);
    nj_gfx_triangle(&g, x1, y1, x2, y2, x3, y3, 1);
    gfx_store(vm, g_obj, &g);
    return 1;
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
 * The no-argument constructor: a Graphics whose target is the display itself.
 * nj_gfx_load() answers with nj_midp_screen() for a null target, which is what
 * a Canvas subclass drawing outside paint() expects.
 */
NJFN(g_init_noarg)
{
    return g_init(vm, th, (nj_value[]){ { .r = NULL } }, ret);
}

/*
 * Build a Graphics over `target` (NULL = the display) and hand it to the
 * caller.  Used by Image.getGraphics() and by the paint machinery.
 */
/*
 * A brand new Graphics clips to its whole target.  gfx_load() reconstructs the
 * clip from the object's own fields, so those fields have to SAY so: otherwise
 * "the clip is empty" and "no clip has ever been set" are both (0,0,0,0), and
 * gfx_load() ends up guessing.  It guessed "whole target", which turned
 * setClip(x, y, 0, h) - a game clipping a strip away - into a licence to draw
 * over the entire screen.  See tests/test_clip.c.
 */
static void gfx_seed_clip(nj_vm *vm, nj_ref g, nj_ref target)
{
    nj_value args[1];
    nj_value ret;
    nj_s4 w = 0, h = 0;
    nj_field *f;

    args[0].bits = 0u;
    args[0].r = target;
    ret.bits = 0u;
    if (target && nj_call(vm, target, NJ_C_IMAGE, "getWidth", "()I", NULL, &ret)
        == NJ_OK) {
        w = ret.i;
    }
    ret.bits = 0u;
    if (target && nj_call(vm, target, NJ_C_IMAGE, "getHeight", "()I", NULL, &ret)
        == NJ_OK) {
        h = ret.i;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    f = fld(vm, G_CLS, F_G_CX, D_INT, 0);
    if (f) { nj_field_write_int(vm, g, f, 0); }
    f = fld(vm, G_CLS, F_G_CY, D_INT, 0);
    if (f) { nj_field_write_int(vm, g, f, 0); }
    f = fld(vm, G_CLS, F_G_CW, D_INT, 0);
    if (f) { nj_field_write_int(vm, g, f, w); }
    f = fld(vm, G_CLS, F_G_CH, D_INT, 0);
    if (f) { nj_field_write_int(vm, g, f, h); }
    (void)args;
}

static nj_ref gfx_new(nj_vm *vm, nj_ref target)
{
    nj_ref g;
    nj_value a1[1];

    /*
     * <init> is Java: it allocates, and the object being constructed is held
     * only by this C local, which the collector cannot see.  Without the root
     * the <init> itself could free its own receiver.  Rooting here is not
     * enough on its own - the reference has to stay rooted until paint() has
     * run, which is what canvas_call_paint_rooted() below is for.
     */
    nj_u4 mark = nj_root_mark(vm);

    g = nj_object_new(vm, G_CLS);
    if (!g) {
        nj_root_release(vm, mark);
        return NULL;
    }
    g = nj_root(vm, g);
    a1[0].r = target;
    if (nj_call_special(vm, g, G_CLS, "<init>", "(L" NJ_C_IMAGE ";)V",
                        a1, NULL) != NJ_OK) {
        nj_root_release(vm, mark);
        return NULL;
    }
    gfx_seed_clip(vm, g, target);
    nj_root_release(vm, mark);
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

    /*
     * A negative width or height is not an error: MIDlets routinely compute
     * w = width - x with x past the edge and expect an empty clip.
     * 傲世三国志－群雄争霸_5633 and 穿越火线-韩国正版_6874 did exactly that
     * once per frame (~1600 lines between them), and the throw aborted paint().
     * nj_gfx_set_clip() already turns a non-positive size into an empty clip.
     */
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    /* User space in, user space stored: nj_gfx_set_clip() does the offset. */
    nj_gfx_set_clip(&g, (int)x, (int)y, (int)w, (int)h);
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
    /* MIDP defines setColor(int) as OPAQUE: only setAlpha may lower the alpha,
         * so a game writing 0x00FF0000 must still get a visible red.  Passing
         * the raw 0x00RRGGBB through leaves alpha at zero and makes every Java
         * drawing call invisible - the single most confusing possible failure,
         * because nothing errors and the screen is simply blank. */
        nj_gfx_set_color(&g, (nj_u4)NJ_ARG_INT(args, 0) | 0xFF000000u);
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

/* Graphics.getDisplayColor(int): the nearest colour the display can show for
 * the requested one.  Every pixel format this runtime has is true colour (see
 * disp_isColor), so the argument is already displayable. */
NJFN(g_getDisplayColor)
{
    ret->i = NJ_ARG_INT(args, 0);
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

/* getGrayScale(): the luminance of the current colour. */
NJFN(g_getGrayScale)
{
    nj_u4 c = (nj_u4)fget(vm, NJ_THIS(args), G_CLS, F_G_COLOR, D_INT);
    int r = (int)((c >> 16) & 0xFFu);
    int g_ = (int)((c >> 8) & 0xFFu);
    int b = (int)(c & 0xFFu);
    ret->i = (r * 77 + g_ * 151 + b * 28) >> 8;
    return NJ_OK;
}

/* setXORMode(boolean): true starts XOR drawing against white, false stops it. */
NJFN(g_setXORModeFlag)
{
    nj_ref self = NJ_THIS(args);
    if (NJ_ARG_INT(args, 0)) {
        fput(vm, self, G_CLS, F_G_XOR, D_INT, 1);
        fput(vm, self, G_CLS, F_G_XORC, D_INT, 0x00FFFFFF);
    } else {
        fput(vm, self, G_CLS, F_G_XOR, D_INT, 0);
    }
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
/*
 * javax.microedition.lcdui.Graphics' anchor constants.  These ARE the
 * renderer's NJ_ANCHOR_* bits -- the same numbers, deliberately -- because a
 * MIDlet's anchor travels from Graphics.LEFT through the native call straight
 * into nj_gfx.  The empirical proof is recorded in include/nj2me/nj_gfx.h:
 * 12476 compiled classes put TOP|LEFT (20) at the top of the anchor histogram,
 * and 20 is not even legal for text under any other reading.
 */
#define MIDP_ANCHOR_HCENTER   1
#define MIDP_ANCHOR_VCENTER   2
#define MIDP_ANCHOR_LEFT      4
#define MIDP_ANCHOR_RIGHT     8
#define MIDP_ANCHOR_TOP      16
#define MIDP_ANCHOR_BOTTOM   32
#define MIDP_ANCHOR_BASELINE 64

/*
 * The horizontal half is therefore a straight copy.  The vertical half is not,
 * because the renderer has one more case than MIDP does: its "no vertical bit
 * at all" already means BASELINE, a text blitter needing some default.  MIDP
 * instead defines a missing vertical component as TOP:
 *
 *   MIDP 2.0, Graphics, "Anchor Points":
 *   "Zero may also be used as the value of an anchor point.  Using zero
 *    for the anchor point value gives results identical to using TOP | LEFT."
 *
 * So BASELINE is the value that has to translate to *nothing*, and TOP has to
 * be spelled out explicitly.  Without that final `else`, every
 * drawString(s, x, y, 0) -- and the bare three-argument drawString, which is
 * the same call -- drew its line `ascent` pixels too high.  On the 仙剑奇侠传
 * dialogue that is a line of speech sitting on the top border of its own
 * dialogue frame.
 */
static int midp_anchor(nj_s4 anchor)
{
    int out = (int)anchor & (NJ_ANCHOR_LEFT | NJ_ANCHOR_HCENTER |
                             NJ_ANCHOR_RIGHT);

    if (anchor & MIDP_ANCHOR_TOP)           { out |= NJ_ANCHOR_TOP; }
    else if (anchor & MIDP_ANCHOR_BOTTOM)   { out |= NJ_ANCHOR_BOTTOM; }
    else if (anchor & MIDP_ANCHOR_BASELINE) { /* nothing: the blitter's default */ }
    else                                    { out |= NJ_ANCHOR_TOP; }

    return out;
}

/*
 * Images.  Same rule, but the vertical set differs: an image has no baseline,
 * so MIDP allows VCENTER (which text may not use) and forbids BASELINE (which
 * text may).  A missing vertical component still means TOP.
 */
static int midp_anchor_image(nj_s4 anchor)
{
    int out = (int)anchor & (NJ_ANCHOR_LEFT | NJ_ANCHOR_HCENTER |
                             NJ_ANCHOR_RIGHT);

    if (anchor & MIDP_ANCHOR_TOP)          { out |= NJ_ANCHOR_TOP; }
    else if (anchor & MIDP_ANCHOR_VCENTER) { out |= NJ_ANCHOR_VCENTER; }
    else if (anchor & MIDP_ANCHOR_BOTTOM)  { out |= NJ_ANCHOR_BOTTOM; }
    else                                   { out |= NJ_ANCHOR_TOP; }

    return out;
}

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
                     (int)x, (int)y, midp_anchor(anchor));
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
                     midp_anchor(anchor));
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
                     (int)x, (int)y, midp_anchor(anchor));
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
    nj_gfx_draw_text(&g, &ch, 1, (int)x, (int)y, midp_anchor(anchor));
    gfx_store(vm, self, &g);
    return NJ_OK;
}

/* ---- images ---- */
/*
 * drawImage(Image img, int x, int y, int anchor)
 *
 * NOTE: this is NOT the same argument list as drawRegion().  drawImage blits
 * the WHOLE image with an anchor; drawRegion blits a sub-rectangle with a
 * transform and no anchor translation beyond the anchor itself.  Sharing one
 * helper between them reads past the end of the argument slots and rejects
 * perfectly ordinary calls with "negative source region".
 */
/*
 * Is this anchor illegal?  MIDP's rule is about CONFLICTS, not omissions: a
 * missing horizontal component means LEFT and a missing vertical one means
 * TOP, and 0 is a perfectly legal anchor meaning TOP|LEFT.  The old test
 * demanded a horizontal bit, so any caller that passed 0 - or Graphics.TOP on
 * its own - got "bad horizontal anchor" on every single frame and the MIDlet
 * painted nothing at all.  That is exactly how 植物大战僵尸2013完美升级版 was
 * failing: an exception per frame, no picture, and nothing in the log that
 * named the anchor value.
 *
 * Two bits set in the same group is a genuine conflict and still throws.
 */
static int anchor_is_bad(nj_s4 anchor)
{
    int h = (int)anchor & (MIDP_ANCHOR_LEFT | MIDP_ANCHOR_HCENTER |
                           MIDP_ANCHOR_RIGHT);
    int v = (int)anchor & (MIDP_ANCHOR_TOP | MIDP_ANCHOR_VCENTER |
                           MIDP_ANCHOR_BOTTOM | MIDP_ANCHOR_BASELINE);

    /* For a power-of-two group, h & (h - 1) is nonzero iff two bits are set. */
    return (h != 0 && (h & (h - 1)) != 0) || (v != 0 && (v & (v - 1)) != 0);
}


NJFN(g_drawImage)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    nj_s4 x = NJ_ARG_INT(args, 1);
    nj_s4 y = NJ_ARG_INT(args, 2);
    nj_s4 anchor = NJ_ARG_INT(args, 3);
    nj_image view;
    nj_gfx g;
    nj_surface sc;

    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image is null");
    }
    if (anchor != -1 &&
        anchor_is_bad(anchor)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bad horizontal anchor");
    }
    if (!img_view(vm, img, &view)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "image has no pixels");
    }
    if (anchor != -1) {
        int a = midp_anchor_image(anchor);

        if (a & NJ_ANCHOR_HCENTER) { x -= view.width / 2; }
        else if (a & NJ_ANCHOR_RIGHT) { x -= view.width; }
        if (a & NJ_ANCHOR_VCENTER) { y -= view.height / 2; }
        else if (a & NJ_ANCHOR_BOTTOM) { y -= view.height; }
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_draw_region(&g, &view, 0, 0, view.width, view.height,
                       NJ_TRANS_NONE, g.use_xor, g.xor_color, NULL,
                       (int)x, (int)y, NJ_ROP_PAINT);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(g_drawRegion)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    nj_s4 sx = NJ_ARG_INT(args, 1);
    nj_s4 sy = NJ_ARG_INT(args, 2);
    nj_s4 sw = NJ_ARG_INT(args, 3);
    nj_s4 sh = NJ_ARG_INT(args, 4);
    nj_s4 transform = NJ_ARG_INT(args, 5);
    nj_s4 x = NJ_ARG_INT(args, 6);
    nj_s4 y = NJ_ARG_INT(args, 7);
    nj_s4 anchor = NJ_ARG_INT(args, 8);
    nj_image view;
    nj_gfx g;
    nj_surface sc;
    nj_s4 dw, dh;

    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image is null");
    }
    if (sw < 0 || sh < 0 || sx < 0 || sy < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative source region");
    }
    if (anchor != -1 &&
        anchor_is_bad(anchor)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bad horizontal anchor");
    }
    if (!img_view(vm, img, &view)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "image has no pixels");
    }
    if (sx + sw > view.width || sy + sh > view.height) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "source region outside the image");
    }
    switch ((int)transform) {
    case NJ_TRANS_ROT90: case NJ_TRANS_ROT270:
    case NJ_TRANS_MIRROR_ROT90: case NJ_TRANS_MIRROR_ROT270:
        dw = sh;
        dh = sw;
        break;
    default:
        dw = sw;
        dh = sh;
        break;
    }
    if (anchor != -1) {
        int a = midp_anchor_image(anchor);

        if (a & NJ_ANCHOR_HCENTER) { x -= dw / 2; }
        else if (a & NJ_ANCHOR_RIGHT) { x -= dw; }
        if (a & NJ_ANCHOR_VCENTER) { y -= dh / 2; }
        else if (a & NJ_ANCHOR_BOTTOM) { y -= dh; }
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_draw_region(&g, &view, (int)sx, (int)sy, (int)sw, (int)sh,
                       (int)transform, g.use_xor, g.xor_color, NULL,
                       (int)x, (int)y, NJ_ROP_PAINT);
    gfx_store(vm, self, &g);
    return NJ_OK;
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
    nj_u4 *rgb;
    nj_size n;
    nj_image view;
    nj_gfx g;
    nj_surface sc;
    nj_status st;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", "rgb is null");
    }
    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "negative size");
    }
    if (w == 0 || h == 0) {
        return NJ_OK;
    }
    {
        nj_s8 first = offset;
        nj_s8 last = first + (nj_s8)(h - 1) * scan;
        nj_s8 low = first < last ? first : last;
        nj_s8 high = (first > last ? first : last) + w;
        if (low < 0 || high > arr->length) {
            return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                            "rgb data too short");
        }
        if ((nj_size)w > (nj_size)-1 / sizeof(nj_u4) / (nj_size)h) {
            return nj_throw_oom(vm);
        }
    }

    /* Retain ARGB: dropping alpha here turns transparent sprite backgrounds
     * (for example Asphalt 6's 0x00FF00FF colour key) into solid magenta. */
    n = (nj_size)w * (nj_size)h * sizeof(nj_u4);
    rgb = (nj_u4 *)nj_host_alloc(vm, n);
    if (!rgb) {
        return nj_throw_oom(vm);
    }
    {
        nj_s4 row, col;
        for (row = 0; row < h; row++) {
            for (col = 0; col < w; col++) {
                nj_u4 p = (nj_u4)nj_array_get_int(arr, (nj_u4)(offset + row * scan + col));
                rgb[(nj_size)row * (nj_size)w + (nj_size)col] =
                    alpha ? p : (p | 0xFF000000u);
            }
        }
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        nj_host_free(vm, rgb);
        return NJ_ERR;
    }
    st = NJ_OK;
    view.width = (int)w;
    view.height = (int)h;
    view.format = NJ_PIXFMT_ARGB8888;
    view.pixels = rgb;
    view.has_alpha = alpha != 0;
    nj_gfx_draw_region(&g, &view, 0, 0, (int)w, (int)h,
                       NJ_TRANS_NONE, 0, 0, NULL,
                       (int)x, (int)y, NJ_ROP_PAINT);
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
        anchor_is_bad(anchor)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bad horizontal anchor");
    }
    if (w == 0 || h == 0) {
        return NJ_OK;
    }
    if (anchor != -1) {
        int a = midp_anchor_image(anchor);

        if (a & NJ_ANCHOR_HCENTER) { xd -= w / 2; }
        else if (a & NJ_ANCHOR_RIGHT) { xd -= w; }
        if (a & NJ_ANCHOR_VCENTER) { yd -= h / 2; }
        else if (a & NJ_ANCHOR_BOTTOM) { yd -= h; }
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
/*
 * Image has no public constructor in MIDP: every image comes from a
 * createImage() factory, which fills the fields directly.  This exists only
 * because the compile stubs synthesise a no-arg constructor, and it says so
 * rather than failing with a link error.
 */
NJFN(no_public_constructor)
{
    (void)args;
    (void)ret;
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "use a factory method: Image.createImage(), "
                    "Font.getFont() or Display.getDisplay()");
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

static nj_status img_createImage_img(nj_vm *vm, nj_thread *th, nj_value *args,
                                     nj_value *ret);

/*
 * The compile stubs declare an all-int createImage(int,int,int,int,int,int),
 * which cannot express the source image the real MIDP 2.0 signature requires
 * (createImage(Image, int, int, int, int, int)).  It is bound so that a link
 * failure cannot happen, and it says clearly what to call instead.
 */
NJFN(img_createImage_sixints)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return nj_throw(vm, "java/lang/IllegalArgumentException",
                    "use createImage(Image source, int x, int y, int width, "
                    "int height, int transform)");
}

/* createImage(Image source): an immutable copy of the whole image. */
NJFN(img_createImage_copy)
{
    nj_value a[6];
    a[0] = args[0];
    a[1].i = 0;
    a[2].i = 0;
    a[3].i = 0;
    a[4].i = 0;
    a[5].i = 0;
    {
        int w = 0, h = 0;
        if (!img_dims(vm, NJ_ARG_REF(args, 0), &w, &h)) {
            return nj_throw(vm, "java/lang/NullPointerException", "source is null");
        }
        a[3].i = w;
        a[4].i = h;
    }
    return img_createImage_img(vm, th, a, ret);
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
    nj_s4 dw, dh;
    int rotated;

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
    /*
     * The quarter turns swap the dimensions: createImage(src, x, y, w, h,
     * TRANS_ROT90) produces an image h wide and w high, because every pixel of
     * the w x h source region is rotated into it.  Allocating w x h for those
     * transforms was wrong twice over - the caller got the wrong size, and the
     * copy below indexed the source with a coordinate that ran off the row
     * (tests/games/三国赤壁小乔秘史_7976.jar asked for TRANS_ROT270, and the read
     * at ux = w - 1 - j went negative on the first destination row, which is a
     * segmentation fault rather than an exception).
     */
    rotated = (transform == NJ_TRANS_ROT90 ||
               transform == NJ_TRANS_MIRROR_ROT90 ||
               transform == NJ_TRANS_ROT270 ||
               transform == NJ_TRANS_MIRROR_ROT270);
    dw = rotated ? h : w;
    dh = rotated ? w : h;
    out = img_alloc(vm, (int)dw, (int)dh, 0);
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
    for (j = 0; j < dh; j++) {
        for (i = 0; i < dw; i++) {
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
            dst[(nj_size)j * (nj_size)dw + (nj_size)i] = p;
        }
    }
    ret->r = out;
    return NJ_OK;
}

/*
 * Three tiny formatters.  This file has no stdio - core and midp code is
 * expected to work without libc - and the error paths that need to explain
 * themselves are not hot, so a dozen lines beat pulling in a printf.
 */
static int fmt_put(char *buf, int at, const char *s)
{
    while (*s) {
        buf[at++] = *s++;
    }
    return at;
}

/*
 * Like fmt_put, but stops before cap.  The strings this carries are the
 * provider's - a port's own decoder writes them - so the copy is bounded by
 * the buffer here rather than by the caller's promise.
 */
static int fmt_put_max(char *buf, int at, int cap, const char *s)
{
    while (*s && at < cap) {
        buf[at++] = *s++;
    }
    return at;
}

static int fmt_u4(char *buf, int at, nj_u4 v)
{
    char tmp[12];
    int n = 0;

    do {
        tmp[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0u && n < (int)sizeof(tmp));
    while (n > 0) {
        buf[at++] = tmp[--n];
    }
    return at;
}

static int fmt_hex2(char *buf, int at, nj_u4 b)
{
    static const char hex[] = "0123456789abcdef";

    buf[at++] = hex[(b >> 4) & 0xFu];
    buf[at++] = hex[b & 0xFu];
    return at;
}

/*
 * Byte-array form: decode PNG (or JPEG) through the per-VM codec registry.
 *
 * The registry hands back a host-allocated nj_image; the pixels are copied
 * into a Java int[] (which the GC then owns) and the host block is released, so
 * nothing outside the Java heap outlives the call.
 */
static nj_status img_decode_into(nj_vm *vm, nj_ref arr, nj_s4 off, nj_s4 len,
                                 nj_ref *out)
{
    nj_image *dec;
    nj_ref img;
    const char *why;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", "data is null");
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > arr->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "no image data");
    }
    why = NULL;
    dec = nj_image_decode_mem(vm, arr->u.bytes + off, (nj_u4)len, &why);
    if (!dec || !dec->pixels || dec->width <= 0 || dec->height <= 0) {
        char msg[256];
        int n;
        int i;
        const nj_u1 *p = (const nj_u1 *)arr->u.bytes + off;

        if (dec) {
            nj_image_release(vm, dec);
        }
        /*
         * Say what was offered.  "cannot decode image" with no clue sent a
         * whole debugging round into the decoder, while the bytes turned out
         * not to be an image at all - a game that encrypts its own resources
         * hands its ciphertext to this function and expects the phone to cope.
         * The length and the first four bytes settle that question at once.
         */
        n = fmt_put(msg, 0, "cannot decode image: ");
        n = fmt_u4(msg, n, (nj_u4)len);
        n = fmt_put(msg, n, " bytes starting ");
        n = fmt_hex2(msg, n, len > 0 ? p[0] : 0u);
        for (i = 1; i < 4; i++) {
            n = fmt_put(msg, n, " ");
            n = fmt_hex2(msg, n, (nj_u4)len > (nj_u4)i ? p[i] : 0u);
        }
        msg[n] = '\0';
        /*
         * And what the decoder said, when a provider said anything.  Without
         * this the MIDlet sees "cannot decode image" for both "this build has
         * no PNG decoder" and "those bytes are not a PNG", which are the two
         * answers a port needs to tell apart.
         */
        if (why != NULL && why[0] != '\0') {
            n = fmt_put(msg, n, ": ");
            n = fmt_put_max(msg, n, (int)sizeof(msg) - 1, why);
            msg[n] = '\0';
        }
        return nj_throw(vm, "java/io/IOException", msg);
    }
    img = nj_midp_image_adopt(vm, dec->pixels, dec->width, dec->height);
    nj_image_release(vm, dec);
    if (!img) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    *out = img;
    return NJ_OK;
}

/*
 * Image.createImage(InputStream).
 *
 * Reads the stream to its end and hands the bytes to the array form, so
 * there is one decoder rather than two.  The buffer grows from 8 KiB
 * instead of being preallocated at a guess: a fixed 4 MiB buffer once hung
 * a MIDlet on a 4 MiB heap, and a sprite sheet is usually far smaller.
 *
 * The array is rooted because every read() may allocate, and img_decode_into
 * runs before the root is released for the same reason.
 */
#define NJ_IMG_STREAM_CHUNK 8192u

NJFN(img_createImage_stream)
{
    nj_ref in = NJ_ARG_REF(args, 0);
    nj_ref bytes;
    nj_ref out = NULL;
    nj_u4 cap = NJ_IMG_STREAM_CHUNK;
    nj_u4 len = 0u;
    nj_u4 mark;
    nj_status st;

    if (!in) {
        (void)nj_throw(vm, "java/lang/NullPointerException", "stream is null");
        return NJ_ERR;
    }
    bytes = nj_byte_array_new(vm, cap);
    if (!bytes) {
        return NJ_ERR;
    }
    mark = nj_root_mark(vm);
    bytes = nj_root(vm, bytes);
    for (;;) {
        nj_value r;
        nj_s4 b;

        st = nj_call(vm, in, "java/io/InputStream", "read", "()I", NULL, &r);
        if (st != NJ_OK) {
            nj_root_release(vm, mark);
            return st;
        }
        b = r.i;
        if (b < 0) {
            break;
        }
        if (len == cap) {
            nj_ref bigger = nj_byte_array_new(vm, cap * 2u);

            if (!bigger) {
                nj_root_release(vm, mark);
                return NJ_ERR;
            }
            memcpy(bigger->u.bytes, bytes->u.bytes, len);
            bytes = nj_root(vm, bigger);
            cap *= 2u;
        }
        ((unsigned char *)bytes->u.bytes)[len++] = (unsigned char)(nj_u4)b;
    }
    st = img_decode_into(vm, bytes, 0, (nj_s4)len, &out);
    nj_root_release(vm, mark);
    if (st != NJ_OK) {
        return st;
    }
    ret->r = out;
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
    const char *why;
    void *data;
    nj_u4 len = 0u;
    nj_ref out = NULL;
    nj_image *dec;

    if (!name) {
        return nj_throw(vm, "java/lang/NullPointerException", "name is null");
    }
    if (!app) {
        return nj_throw(vm, "java/io/IOException", "no application is loaded");
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
    why = NULL;
    dec = nj_image_decode_mem(vm, data, len, &why);
    nj_host_free(vm, data);
    if (!dec || !dec->pixels || dec->width <= 0 || dec->height <= 0) {
        if (dec) {
            nj_image_release(vm, dec);
        }
        /* The resource name alone says nothing about why it would not decode. */
        if (why != NULL && why[0] != '\0') {
            char msg[256];
            int n = fmt_put_max(msg, 0, (int)sizeof(msg) - 1, cname);
            n = fmt_put(msg, n, ": ");
            n = fmt_put_max(msg, n, (int)sizeof(msg) - 1, why);
            msg[n] = '\0';
            return nj_throw(vm, "java/io/IOException", msg);
        }
        return nj_throw(vm, "java/io/IOException", cname);
    }
    out = nj_midp_image_adopt(vm, dec->pixels, dec->width, dec->height);
    nj_image_release(vm, dec);
    if (!out) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
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
        return new_failed(vm);
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

/* Draw a whole String, positioned by the renderer's own NJ_ANCHOR_* bits.
 * Every caller here is internal and already speaks that encoding; a value that
 * came from a MIDlet must go through midp_anchor() first. */
static void text_draw(nj_gfx *g, nj_ref str, int x, int y, int anchor)
{
    if (!str) {
        return;
    }
    nj_gfx_draw_text(g, nj_string_chars(str), (int)nj_string_len(str), x, y,
                     anchor);
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
    /* getFont(int style) - the face and size default to SYSTEM / MEDIUM. */
    nj_ref f = font_intern(vm, 0, NJ_ARG_INT(args, 0), 0);
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
/*
 * Command(String label, int commandType, int priority)
 *   args: [0] label, [1] commandType, [2] priority
 */
NJFN(cmd_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref label = NJ_ARG_REF(args, 0);

    if (!label) {
        return nj_throw(vm, "java/lang/NullPointerException", "label is null");
    }
    fput(vm, self, NJ_C_COMMAND, "cmdType", D_INT, NJ_ARG_INT(args, 1));
    rput(vm, self, NJ_C_COMMAND, "cmdLabel", D_STR, label);
    fput(vm, self, NJ_C_COMMAND, "cmdPriority", D_INT, NJ_ARG_INT(args, 2));
    fput(vm, self, NJ_C_COMMAND, "cmdLongLabel", D_INT, 0);
    rput(vm, self, NJ_C_COMMAND, "cmdLong", D_STR, NULL);
    fput(vm, self, NJ_C_COMMAND, "cmdMnemonic", D_INT, 0);
    return NJ_OK;
}

/*
 * Command(String longLabel, int commandType, int priority, int mnemonic)
 *   args: [0] longLabel, [1] commandType, [2] priority, [3] mnemonic
 * There is no separate short label in this overload, so getLabel() answers with
 * the long one, exactly as a device does.
 */
NJFN(cmd_init_long)
{
    nj_ref self = NJ_THIS(args);
    nj_ref longl = NJ_ARG_REF(args, 0);

    if (!longl) {
        return nj_throw(vm, "java/lang/NullPointerException", "label is null");
    }
    fput(vm, self, NJ_C_COMMAND, "cmdType", D_INT, NJ_ARG_INT(args, 1));
    rput(vm, self, NJ_C_COMMAND, "cmdLabel", D_STR, longl);
    fput(vm, self, NJ_C_COMMAND, "cmdPriority", D_INT, NJ_ARG_INT(args, 2));
    fput(vm, self, NJ_C_COMMAND, "cmdLongLabel", D_INT, 1);
    rput(vm, self, NJ_C_COMMAND, "cmdLong", D_STR, longl);
    fput(vm, self, NJ_C_COMMAND, "cmdMnemonic", D_INT, NJ_ARG_INT(args, 3));
    return NJ_OK;
}

/*
 * Command(String label, String longLabel, int commandType, int priority)
 *   args: [0] label, [1] longLabel, [2] commandType, [3] priority
 * Both labels are kept: getLabel() answers with the short one and
 * getLongLabel() with the long one.  A null longLabel means "no long form" and
 * leaves getLongLabel() falling back to the short label.
 */
NJFN(cmd_init_labels)
{
    nj_ref self = NJ_THIS(args);
    nj_ref label = NJ_ARG_REF(args, 0);
    nj_ref longl = NJ_ARG_REF(args, 1);

    if (!label) {
        return nj_throw(vm, "java/lang/NullPointerException", "label is null");
    }
    fput(vm, self, NJ_C_COMMAND, "cmdType", D_INT, NJ_ARG_INT(args, 2));
    rput(vm, self, NJ_C_COMMAND, "cmdLabel", D_STR, label);
    fput(vm, self, NJ_C_COMMAND, "cmdPriority", D_INT, NJ_ARG_INT(args, 3));
    fput(vm, self, NJ_C_COMMAND, "cmdLongLabel", D_INT, longl ? 1 : 0);
    rput(vm, self, NJ_C_COMMAND, "cmdLong", D_STR, longl);
    fput(vm, self, NJ_C_COMMAND, "cmdMnemonic", D_INT, 0);
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
/*
 * Activate a Canvas's command from a soft key.
 *
 * A Canvas may carry Commands and a CommandListener - it is legal MIDP, and
 * the second third-party game in the corpus does exactly that for its start
 * screen.  Delivering the soft keys to keyPressed() instead leaves the game's
 * own menu unreachable by ANY key, which is what happened: the title screen
 * drew and nothing could get past it.
 *
 * SOFT_LEFT takes the default command.  SOFT_RIGHT takes the SECOND one when
 * there is one, because that is the convention a device with two soft keys
 * uses; when there is only one command, either soft key activates it, because
 * a lone command that no key can reach is strictly worse than one reachable
 * from two.
 */
static int canvas_command_activate(nj_vm *vm, nj_ref d, int key)
{
    int n = 0;
    nj_ref arr = cmd_snapshot(vm, d, &n);
    nj_ref c = NULL;

    if (!arr || n <= 0) {
        return 0;
    }
    if (key == NJ_KEY_SOFT_RIGHT && n > 1) {
        int def = -1;
        (void)cmd_default(vm, d, &def);
        /* The first command that is not the default one. */
        {
            int i;
            for (i = 0; i < n; i++) {
                if (i != def) {
                    c = nj_array_get_ref(arr, (nj_u4)i);
                    break;
                }
            }
        }
    }
    if (!c) {
        c = cmd_default(vm, d, NULL);
    }
    if (!c) {
        return 0;
    }
    return command_dispatch(vm, d, c);
}

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
/*
 * The core takes the receiver explicitly.  A native reads its receiver as
 * args[-1], which only exists for a call the VM made; code that builds a
 * synthetic argument array on the stack must NOT go through the native form,
 * or args[-1] reads off the end of that array.
 */
static void d_init_on(nj_vm *vm, nj_ref self)
{
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TITLE, D_STR, NULL);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TICKER, D_TICKER, NULL);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_LISTENER, D_CL, NULL);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_COMMANDS, D_VECTOR, NULL);
    fput(vm, self, NJ_C_DISPLAYABLE, F_D_FULL, D_BOOL, 0);
}

NJFN(d_init)
{
    d_init_on(vm, NJ_THIS(args));
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
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown key code");
    }
    ret->r = nj_string_new(vm, name);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* ================================================================== */
/* Canvas                                                              */
/* ================================================================== */
/*
 * Map between the port's NJ_KEY_* codes (the nj_key enum in nj_port.h, which
 * is what a port puts in nj_event.key and what --keys parses) and the MIDP key
 * codes a MIDlet sees.  This is the ONLY translation: everything downstream of
 * nj_midp_key() speaks MIDP codes.
 */
static int nj_key_to_midp(int key)
{
    switch (key) {
    case NJ_KEY_0: case NJ_KEY_1: case NJ_KEY_2: case NJ_KEY_3:
    case NJ_KEY_4: case NJ_KEY_5: case NJ_KEY_6: case NJ_KEY_7:
    case NJ_KEY_8: case NJ_KEY_9:
        return 48 + (key - NJ_KEY_0);
    case NJ_KEY_STAR:  return NJ_MIDP_KEY_STAR;
    case NJ_KEY_POUND: return NJ_MIDP_KEY_POUND;
    case NJ_KEY_UP:    return NJ_MIDP_KEY_UP;
    case NJ_KEY_DOWN:  return NJ_MIDP_KEY_DOWN;
    case NJ_KEY_LEFT:  return NJ_MIDP_KEY_LEFT;
    case NJ_KEY_RIGHT: return NJ_MIDP_KEY_RIGHT;
    case NJ_KEY_FIRE:  return NJ_MIDP_KEY_FIRE;
    case NJ_KEY_SOFT_LEFT:  return NJ_MIDP_KEY_SOFT1;
    case NJ_KEY_SOFT_RIGHT: return NJ_MIDP_KEY_SOFT2;
    case NJ_KEY_CLEAR:      return NJ_MIDP_KEY_CLEAR;
    case NJ_KEY_GAME_A: return NJ_MIDP_KEY_GAME_A;
    case NJ_KEY_GAME_B: return NJ_MIDP_KEY_GAME_B;
    case NJ_KEY_GAME_C: return NJ_MIDP_KEY_GAME_C;
    case NJ_KEY_GAME_D: return NJ_MIDP_KEY_GAME_D;
    /*
     * Volume, for a pad key that carries one.  The MIDlet is told the code the
     * platform's own volume key would have had, which is the only sensible
     * answer for a key MIDP itself never had.
     */
    case NJ_KEY_VOLUP:   return NJ_MIDP_KEY_VOLUP;
    case NJ_KEY_VOLDOWN: return NJ_MIDP_KEY_VOLDOWN;
    default: return 0;
    }
}

/*
 * Map a MIDP key code to the *game action* constant Canvas.getGameAction()
 * returns: UP=1, LEFT=2, RIGHT=5, DOWN=6, FIRE=8, GAME_A..GAME_D=9..12.
 * These are a different namespace from the NJ_GAME_* key-state bits that
 * GameCanvas.getKeyStates() reports - confusing the two makes every game read
 * "UP" as 2, so they are kept in separate functions on purpose.
 */
static int midp_game_action(nj_s4 code)
{
    switch ((int)code) {
    case NJ_MIDP_KEY_UP:    return 1;    /* Canvas.UP     */
    case NJ_MIDP_KEY_LEFT:  return 2;    /* Canvas.LEFT   */
    case NJ_MIDP_KEY_RIGHT: return 5;    /* Canvas.RIGHT  */
    case NJ_MIDP_KEY_DOWN:  return 6;    /* Canvas.DOWN   */
    case NJ_MIDP_KEY_FIRE:  return 8;    /* Canvas.FIRE   */
    case NJ_MIDP_KEY_GAME_A: return 9;
    case NJ_MIDP_KEY_GAME_B: return 10;
    case NJ_MIDP_KEY_GAME_C: return 11;
    case NJ_MIDP_KEY_GAME_D: return 12;
    case NJ_MIDP_KEY_NUM2: return 1;    /* KEY_NUM2 -> UP    */
    case NJ_MIDP_KEY_NUM8: return 6;    /* KEY_NUM8 -> DOWN  */
    case NJ_MIDP_KEY_NUM4: return 2;    /* KEY_NUM4 -> LEFT  */
    case NJ_MIDP_KEY_NUM6: return 5;    /* KEY_NUM6 -> RIGHT */
    case NJ_MIDP_KEY_NUM5: return 8;    /* KEY_NUM5 -> FIRE  */
    default: return 0;
    }
}

/* The key-state bit (GameCanvas.getKeyStates namespace) for a MIDP key code. */
static int midp_key_state_bit(nj_s4 code)
{
    switch ((int)code) {
    case NJ_MIDP_KEY_UP:    return NJ_GAME_UP;
    case NJ_MIDP_KEY_DOWN:  return NJ_GAME_DOWN;
    case NJ_MIDP_KEY_LEFT:  return NJ_GAME_LEFT;
    case NJ_MIDP_KEY_RIGHT: return NJ_GAME_RIGHT;
    case NJ_MIDP_KEY_FIRE:  return NJ_GAME_FIRE;
    case NJ_MIDP_KEY_GAME_A: return NJ_GAME_A;
    case NJ_MIDP_KEY_GAME_B: return NJ_GAME_B;
    case NJ_MIDP_KEY_GAME_C: return NJ_GAME_C;
    case NJ_MIDP_KEY_GAME_D: return NJ_GAME_D;
    case NJ_MIDP_KEY_NUM2: return NJ_GAME_UP;
    case NJ_MIDP_KEY_NUM8: return NJ_GAME_DOWN;
    case NJ_MIDP_KEY_NUM4: return NJ_GAME_LEFT;
    case NJ_MIDP_KEY_NUM6: return NJ_GAME_RIGHT;
    case NJ_MIDP_KEY_NUM5: return NJ_GAME_FIRE;
    default: return 0;
    }
}

static int game_action_to_key(int action)
{
    switch (action) {
    case 1:  return NJ_MIDP_KEY_UP;
    case 6:  return NJ_MIDP_KEY_DOWN;
    case 2:  return NJ_MIDP_KEY_LEFT;
    case 5:  return NJ_MIDP_KEY_RIGHT;
    case 8:  return NJ_MIDP_KEY_FIRE;
    case 9:  return NJ_MIDP_KEY_GAME_A;
    case 10: return NJ_MIDP_KEY_GAME_B;
    case 11: return NJ_MIDP_KEY_GAME_C;
    case 12: return NJ_MIDP_KEY_GAME_D;
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
    fput(vm, self, NJ_C_CANVAS, NJ_F_C_HELD, D_INT, 0);
    fput(vm, self, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL, 0);
    fput(vm, self, NJ_C_CANVAS, F_C_PRESSED, D_BOOL, 0);
    return vm->exception ? NJ_ERR : NJ_OK;
}

/*
 * Canvas.showNotify()/hideNotify()/drawNotify() are plain hooks a subclass may
 * override; the default implementations do nothing.  They must be callable, so
 * they are bound rather than left to a missing native.
 */
NJFN(c_visibility_notify)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

/* The default input callbacks do nothing, which is what MIDP specifies. */
NJFN(canvas_noop_i)  { (void)args; (void)ret; return NJ_OK; }
NJFN(canvas_noop_ii) { (void)args; (void)ret; return NJ_OK; }

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

    /*
     * MIDP clips the requested rectangle to the Canvas and does nothing when
     * the result is empty - it does NOT reject a negative width or height.
     * Games rely on that: a sprite that has moved off the left edge produces a
     * negative x, and a computed width can go negative on the frame it leaves.
     * Throwing here fails a game that is behaving correctly, and it fails it in
     * startApp(), where the developer has no idea which call is at fault.
     *
     * x/y are unused because this runtime repaints the whole displayable every
     * frame rather than tracking damage rectangles; the clipping below is what
     * makes the semantics right, not the tracking.
     */
    NJ_UNUSED(x);
    NJ_UNUSED(y);
    if (w <= 0 || h <= 0) {
        return NJ_OK;   /* empty region: no repaint is requested */
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

    /*
     * Canvas.repaint() with no arguments asks for the WHOLE canvas.  It must
     * not be routed through the four-argument form: that one reads x/y/w/h out
     * of the native argument slots, and repaint() has no arguments, so it was
     * reading whatever the caller's frame happened to hold - typically zeros,
     * and a width or height of zero means "empty region, no repaint is
     * requested".  The pending request was then never set, so the matching
     * serviceRepaints() had nothing to service.
     *
     * That is not a harmless no-op.  The common MIDP idiom is to publish the
     * paint state in a field, repaint, force the paint with serviceRepaints()
     * and clear the field afterwards:
     *
     *     f = state; repaint(); serviceRepaints(); f = 0;
     *
     * With no repaint pending, serviceRepaints() returned immediately, the
     * field was cleared, and the runner's own per-frame paint then found the
     * cleared field and drew nothing: a title that rendered its map from
     * paint() stayed black for the whole run while the runtime reported a
     * paint for every frame (仙魔飞行棋_417's mobile/k.e(I)V).
     */
    nj_ref self = NJ_THIS(args);
    nj_ref cur = nj_midp_current(vm);

    NJ_UNUSED(th);
    NJ_UNUSED(ret);
    if (cur != self) {
        /* MIDP silently ignores repaint() on a Displayable that is not shown,
         * and it must NOT become visible as a side effect. */
        return NJ_OK;
    }
    nj_midp_repaint(vm);
    return NJ_OK;
}

NJFN(c_serviceRepaints)
{

    /*
     * MIDP: "serviceRepaints() forces any pending repaints to be serviced ...
     * it blocks until all pending repaint requests have been processed."
     *
     * This has to be synchronous, and the reason is not theoretical.  The
     * common MIDlet idiom is
     *
     *     visible = true; repaint(); serviceRepaints(); visible = false;
     *
     * with paint() starting `if (!visible) return;`.  When this was a no-op,
     * the runner's per-frame paint arrived after `visible` was already false,
     * so paint() returned at its first instruction: the runtime reported a
     * paint for every frame while the application made no Graphics call at
     * all.  One title sat at zero drawn pixels for its whole run that way.
     *
     * Painting here also presents, which is what the caller asked for.  The
     * guard keeps a callSerially() runnable drained inside the paint from
     * re-entering it.
     */
    if (vm->repaint_pending && !vm->in_paint) {
        vm->in_paint = 1u;
        (void)nj_midp_paint(vm);
        vm->in_paint = 0u;
    }
    return NJ_OK;
}

NJFN(c_setFullScreenMode)
{
    return d_setFullScreenMode(vm, th, args, ret);
}

/* ================================================================== */
/* The built-in widget renderer                                        */
/* ================================================================== */
/*
 * A MIDlet that never draws anything itself still has to see its Form, List,
 * TextBox and Alert: those screens are the device's UI, not the game's.  This
 * section renders them with the nj_gfx primitives.
 *
 * Legibility beats beauty: a title bar, a scrollable content area, a selection
 * highlight, soft-key command labels and a ticker.  Everything is laid out
 * from the font metrics, so it adapts to any screen size.
 */
#define UI_PAD      3
#define UI_BAR_H    20
#define UI_ROW_GAP  2
#define UI_ITEM_H   18

/* Palette. */
#define UI_BG       0xFFF0F0F0u
#define UI_TEXT     0xFF000000u
#define UI_DIM      0xFF606060u
#define UI_TITLE_BG 0xFF305080u
#define UI_TITLE_FG 0xFFFFFFFFu
#define UI_SEL_BG   0xFF3060C0u
#define UI_SEL_FG   0xFFFFFFFFu
#define UI_BORDER   0xFF808080u
#define UI_SOFT_BG  0xFFD0D0D0u
#define UI_GAUGE_BG 0xFFFFFFFFu
#define UI_GAUGE_FG 0xFF2050A0u
#define UI_ALERT_BG 0xFFFFF0C0u

/* The per-frame UI state: what the content area scrolls to and which font
 * metrics it is using.  A stack local, never a global. */
typedef struct ui_frame {
    int scroll;      /* first visible content line/row */
    int content_y;   /* device y of the top of the content area */
    int content_h;   /* height of the content area */
    int row_h;       /* one text row */
    int small_h;     /* one small-text row */
    nj_ref font;
    nj_ref small;
} ui_frame;

/*
 * Draw one line of UI text.  The rasterizer clips natively, so long strings
 * simply run under the next widget rather than corrupting the frame; callers
 * pass the width they reserved.
 */
/*
 * Draw a Java string, WRAPPING it at maxw.
 *
 * This used to take maxw and ignore it - the text was not clipped at the panel
 * edge, it was drawn straight past it, which is why a real game's error Alert
 * showed "java.lang.Except..." and stopped.  MIDP wraps.
 *
 * Every built-in renderer goes through here (Alert, Form, List, TextBox), so
 * the wrap belongs here too rather than in each of them.
 *
 * The break is at the last space that fits, falling back to a hard break inside
 * a word that is longer than the line - a URL or a class name has no spaces, and
 * running it off the edge is the behaviour being fixed.
 */
static void ui_text(nj_vm *vm, nj_gfx *g, nj_ref font, nj_ref s, int x, int y,
                    int maxw)
{
    const nj_u2 *ch;
    const nj_font *f;
    int len;
    int i, start, last_space, w, line_h, cy;

    NJ_UNUSED(vm);
    NJ_UNUSED(font);
    if (!s || !g) {
        return;
    }
    ch = nj_string_chars(s);
    len = (int)nj_string_len(s);
    if (!ch || len <= 0) {
        return;
    }
    f = g->font;
    if (maxw <= 0 || !f || f->height <= 0) {
        nj_gfx_draw_text(g, ch, len, x, y, NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
        return;
    }

    line_h = f->height;
    cy = y;
    start = 0;
    last_space = -1;
    w = 0;

    for (i = 0; i < len; i++) {
        int cw = nj_font_char_width(f, ch[i]);

        if (ch[i] == (nj_u2)' ') {
            last_space = i;
        }
        if (w + cw > maxw && i > start) {
            int brk = (last_space > start) ? last_space : i;
            nj_gfx_draw_text(g, ch + start, brk - start, x, cy,
                             NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
            cy += line_h;
            start = (ch[brk] == (nj_u2)' ') ? brk + 1 : brk;
            i = start - 1;
            last_space = -1;
            w = 0;
            continue;
        }
        w += cw;
    }
    if (start < len) {
        nj_gfx_draw_text(g, ch + start, len - start, x, cy,
                         NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
    }
}

/* Standard "row" background: a highlight when selected. */
static void ui_row_bg(nj_gfx *g, int x, int y, int w, int h, int selected)
{
    nj_gfx_set_color(g, selected ? UI_SEL_BG : UI_BG);
    nj_gfx_fill_rect(g, x, y, w, h);
}

static void ui_row_text(nj_vm *vm, nj_gfx *g, nj_ref font, nj_ref s, int x,
                        int y, int maxw, int selected)
{
    nj_gfx_set_color(g, selected ? UI_SEL_FG : UI_TEXT);
    ui_text(vm, g, font, s, x, y, maxw);
}

/* ------------------------------------------------------------------ */
/* Items                                                               */
/* ------------------------------------------------------------------ */
/*
 * Measure an Item.  Items answer through an abstract njHeight()/njText()
 * method pair, so a widget the game defines itself renders through the same
 * path as the built-in ones and a missing override raises
 * AbstractMethodError instead of drawing nothing.
 */
static int item_measure(nj_vm *vm, nj_ref item, const ui_frame *uf, int avail)
{
    nj_value a[1];
    nj_s4 h;

    if (!item) {
        return 0;
    }
    a[0].i = avail;
    h = nj_call_int(vm, item, NJ_C_ITEM, "njHeight", "(I)I", a);
    if (vm->exception) {
        nj_clear_exception(vm);
        return UI_ITEM_H;
    }
    if (h < 0) {
        h = 0;
    }
    if (h > 0) {
        nj_ref label = rget(vm, item, NJ_C_ITEM, "itLabel", D_STR);
        if (label && nj_string_len(label) > 0u) {
            h += uf->small_h;
        }
    }
    return (int)h;
}

/* The text an Item currently shows, or NULL. */
static nj_ref item_text(nj_vm *vm, nj_ref item)
{
    nj_ref s;

    if (!item) {
        return NULL;
    }
    s = nj_call_ref(vm, item, NJ_C_ITEM, "njText", "()Ljava/lang/String;", NULL);
    if (vm->exception) {
        nj_clear_exception(vm);
        return NULL;
    }
    return s;
}

/*
 * The default Item renderer: a label in small type, then the item's text, or
 * its bitmap if it has one.  Used for every Item whose class does not override
 * njDraw().
 */
static void item_draw_default(nj_vm *vm, nj_gfx *g, nj_ref item, const ui_frame *uf,
                              int x, int y, int w, int selected)
{
    nj_ref label = rget(vm, item, NJ_C_ITEM, "itLabel", D_STR);
    int ty = y;
    nj_ref img = rget(vm, item, NJ_C_ITEM, "itImage", D_IMAGE);

    if (label && nj_string_len(label) > 0u) {
        nj_gfx_set_color(g, selected ? UI_SEL_FG : UI_DIM);
        ui_text(vm, g, uf->small, label, x, ty, w);
        ty += uf->small_h;
    }
    if (img) {
        nj_image view;
        if (img_view(vm, img, &view)) {
            nj_gfx_draw_image(g, &view, x, ty, 0, 0, view.width, view.height);
            ty += view.height;
        }
        return;
    }
    ui_row_text(vm, g, uf->font, item_text(vm, item), x, ty, w, selected);
}

/*
 * Render one Item and return the height it consumed.  A class may override
 * njDraw(Graphics, int, int, int, boolean) -> int; the default implementation
 * below handles every built-in widget through the abstract njText()/njHeight()
 * contract.
 */
static int item_draw(nj_vm *vm, nj_gfx *g, nj_ref item, const ui_frame *uf,
                     int x, int y, int w, int selected)
{
    int h;

    if (!item) {
        return 0;
    }
    h = item_measure(vm, item, uf, w);
    item_draw_default(vm, g, item, uf, x, y, w, selected);
    return (h > 0) ? h : UI_ITEM_H;
}

/* Defined with DateField, used by the Form key handler above it. */
static nj_s8 df_millis(nj_vm *vm, nj_ref self);
static void df_set_millis(nj_vm *vm, nj_ref self, nj_s8 t);

/* ------------------------------------------------------------------ */
/* Screen chrome                                                       */
/* ------------------------------------------------------------------ */
static void ui_title(nj_vm *vm, nj_gfx *g, nj_ref title, int w, int scroll)
{
    int x = UI_PAD;

    nj_gfx_set_color(g, UI_TITLE_BG);
    nj_gfx_fill_rect(g, 0, 0, w, UI_BAR_H);
    nj_gfx_set_color(g, UI_TITLE_FG);
    if (title && nj_string_len(title) > 0u) {
        ui_text(vm, g, NULL, title, x, (UI_BAR_H - 11) / 2 + 1, w - 2 * UI_PAD);
    }
    (void)scroll;
}

/* A soft-key bar: the commands the user can reach, with their labels. */
static void ui_softkeys(nj_vm *vm, nj_gfx *g, nj_ref displayable, int w,
                        const ui_frame *uf)
{
    int n = 0;
    nj_ref arr = cmd_snapshot(vm, displayable, &n);
    int i;
    int y = g->surf->height - UI_BAR_H;
    int col = 0;

    nj_gfx_set_color(g, UI_SOFT_BG);
    nj_gfx_fill_rect(g, 0, y, w, UI_BAR_H);
    nj_gfx_set_color(g, UI_BORDER);
    nj_gfx_line(g, 0, y, w - 1, y);
    if (!arr || n <= 0) {
        return;
    }
    for (i = 0; i < n && col < 3; i++) {
        nj_ref c = nj_array_get_ref(arr, (nj_u4)i);
        nj_ref label;
        int lw;
        int x;

        if (!c) {
            continue;
        }
        label = rget(vm, c, NJ_C_COMMAND, "cmdLabel", D_STR);
        if (!label) {
            continue;
        }
        lw = text_width(vm, uf->small, label);
        if (lw > w / 2 - 4) {
            break;
        }
        x = (col == 0) ? UI_PAD : (w - UI_PAD - lw);
        nj_gfx_set_color(g, UI_TEXT);
        text_draw(g, label, x, y + (UI_BAR_H - uf->small_h) / 2 + 1,
                  NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
        if (col == 0) {
            col = 1;
        } else {
            col = 2;
        }
    }
}

/* The ticker: a marquee strip the game can set on any screen. */
static void ui_ticker(nj_vm *vm, nj_gfx *g, nj_ref ticker, int w, int y,
                      int *phase)
{
    nj_ref s = rget(vm, ticker, NJ_C_TICKER, "tkString", D_STR);
    int tw;

    if (!s || nj_string_len(s) == 0u) {
        return;
    }
    nj_gfx_set_color(g, UI_DIM);
    nj_gfx_fill_rect(g, 0, y, w, 13);
    tw = text_width(vm, NULL, s);
    if (tw <= w - 2 * UI_PAD) {
        nj_gfx_set_color(g, UI_TEXT);
        text_draw(g, s, UI_PAD, y + 1, NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
        return;
    }
    /* Scrolling marquee: the phase advances one pixel per frame. */
    *phase = (*phase + 1) % (tw + 8);
    nj_gfx_set_color(g, UI_TEXT);
    text_draw(g, s, UI_PAD + w - *phase, y + 1, NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
}

/* ------------------------------------------------------------------ */
/* Form / List / TextBox                                               */
/* ------------------------------------------------------------------ */
/*
 * A Form is an Item[] plus a count.  The renderer walks it from the current
 * scroll offset, drawing until the content area is full and reporting the
 * total height so the caller knows whether a scroll bar is needed.
 */
static int form_line_height(nj_vm *vm, nj_ref item, const ui_frame *uf)
{
    int h;

    if (!item) {
        return 0;
    }
    h = item_measure(vm, item, uf, uf->content_h > 0 ? uf->content_h : 100);
    if (h <= 0) {
        h = UI_ITEM_H;
    }
    return h + UI_ROW_GAP;
}

static int form_total_height(nj_vm *vm, nj_ref form, const ui_frame *uf)
{
    nj_s4 n = fget(vm, form, NJ_C_FORM, "fmCount", D_INT);
    nj_ref items = rget(vm, form, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 i;
    int total = 0;

    if (!items || n <= 0) {
        return 0;
    }
    for (i = 0; i < n && (nj_u4)i < items->length; i++) {
        total += form_line_height(vm, nj_array_get_ref(items, (nj_u4)i), uf);
    }
    return total;
}

static void form_render(nj_vm *vm, nj_gfx *g, nj_ref form, ui_frame *uf,
                        int focus)
{
    nj_ref items = rget(vm, form, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, form, NJ_C_FORM, "fmCount", D_INT);
    nj_s4 i;
    int y = uf->content_y;
    int bottom = uf->content_y + uf->content_h;
    int total = form_total_height(vm, form, uf);
    int maxscroll = total - uf->content_h;

    if (maxscroll < 0) {
        maxscroll = 0;
    }
    if (uf->scroll > maxscroll) {
        uf->scroll = maxscroll;
    }
    if (uf->scroll < 0) {
        uf->scroll = 0;
    }
    if (!items || n <= 0) {
        nj_gfx_set_color(g, UI_DIM);
        {
            nj_ref empty = nj_string_new(vm, "(empty)");
            if (empty) {
                ui_text(vm, g, uf->small, empty, UI_PAD, y, g->surf->width);
            }
        }
        return;
    }
    for (i = 0; i < n && (nj_u4)i < items->length; i++) {
        nj_ref it = nj_array_get_ref(items, (nj_u4)i);
        int h = form_line_height(vm, it, uf);
        int sel = (focus && (int)i == fget(vm, form, NJ_C_FORM, "fmIndex", D_INT));
        if (y + h < uf->content_y) {
            y += h;
            continue;
        }
        if (y >= bottom) {
            break;
        }
        if (sel) {
            ui_row_bg(g, 0, y, g->surf->width, h - UI_ROW_GAP, 1);
        }
        (void)item_draw(vm, g, it, uf, UI_PAD, y, g->surf->width - 2 * UI_PAD,
                        sel);
        y += h;
    }
    /* Scroll indicator: a 2px bar on the right when the content overflows. */
    if (maxscroll > 0) {
        int track = uf->content_h;
        int thumb = track * track / total;
        int pos = (uf->scroll * (track - thumb)) / maxscroll;
        nj_gfx_set_color(g, UI_BORDER);
        nj_gfx_fill_rect(g, g->surf->width - 2, uf->content_y + pos, 2, thumb);
    }
}

/*
 * A List is a screen of its own (it is not a Form in MIDP 2.0), so it has its
 * own renderer: rows of strings with an optional image, the selected one
 * highlighted, and - in IMPLICIT mode - a "Select" soft key.
 */
static void list_render(nj_vm *vm, nj_gfx *g, nj_ref list, ui_frame *uf)
{
    nj_ref strs = rget(vm, list, NJ_C_LIST, "lsStrings", D_STRARR);
    nj_ref imgs = rget(vm, list, NJ_C_LIST, "lsImages", D_IMGARR);
    nj_s4 n = fget(vm, list, NJ_C_LIST, "lsCount", D_INT);
    nj_s4 sel = fget(vm, list, NJ_C_LIST, "lsIndex", D_INT);
    nj_s4 i;
    int y = uf->content_y;
    int bottom = uf->content_y + uf->content_h;
    int row = uf->row_h + UI_ROW_GAP;
    int maxrows = uf->content_h / (row > 0 ? row : 1);
    int maxscroll = (int)n - maxrows;

    if (maxscroll < 0) {
        maxscroll = 0;
    }
    uf->scroll = (int)sel - maxrows / 2;
    if (uf->scroll > maxscroll) { uf->scroll = maxscroll; }
    if (uf->scroll < 0) { uf->scroll = 0; }

    if (!strs || n <= 0) {
        nj_gfx_set_color(g, UI_DIM);
        {
            nj_ref empty = nj_string_new(vm, "(empty list)");
            if (empty) {
                ui_text(vm, g, uf->small, empty, UI_PAD, y, g->surf->width);
            }
        }
        return;
    }
    for (i = (nj_s4)uf->scroll; i < n; i++) {
        int sy = y + (int)(i - uf->scroll) * row;
        int is_sel = (i == sel);
        int tx = UI_PAD;
        nj_ref s;

        if (sy + row > bottom) {
            break;
        }
        if ((nj_u4)i >= strs->length) {
            break;
        }
        s = nj_array_get_ref(strs, (nj_u4)i);
        ui_row_bg(g, 0, sy, g->surf->width, row - UI_ROW_GAP, is_sel);
        if (imgs && (nj_u4)i < imgs->length) {
            nj_ref im = nj_array_get_ref(imgs, (nj_u4)i);
            nj_image view;
            if (im && img_view(vm, im, &view)) {
                nj_gfx_draw_image(g, &view, tx, sy + 1, 0, 0, view.width,
                                  view.height);
                tx += view.width + 2;
            }
        }
        ui_row_text(vm, g, uf->font, s, tx, sy + 1,
                    g->surf->width - tx - UI_PAD, is_sel);
    }
}

static void textbox_render(nj_vm *vm, nj_gfx *g, nj_ref tb, ui_frame *uf)
{
    nj_ref s = rget(vm, tb, NJ_C_TEXTBOX, "tbText", D_STR);
    int cx = UI_PAD;
    int cy = uf->content_y;

    if (s) {
        nj_gfx_set_color(g, UI_TEXT);
        ui_text(vm, g, uf->font, s, cx, cy + 1, g->surf->width - 2 * UI_PAD);
    }
    /* A caret makes it obvious the screen is editable. */
    {
        int w = s ? text_width(vm, uf->font, s) : 0;
        if (w > g->surf->width - 2 * UI_PAD) {
            w = g->surf->width - 2 * UI_PAD;
        }
        nj_gfx_set_color(g, UI_TEXT);
        nj_gfx_fill_rect(g, cx + w + 1, cy + 1, 1, uf->row_h);
    }
}

static void alert_render(nj_vm *vm, nj_gfx *g, nj_ref alert, ui_frame *uf)
{
    nj_ref s = rget(vm, alert, NJ_C_ALERT, F_AL_STRING, D_STR);
    nj_ref img = rget(vm, alert, NJ_C_ALERT, F_AL_IMAGE, D_IMAGE);
    nj_s4 type = 0;
    nj_ref tobj = rget(vm, alert, NJ_C_ALERT, F_AL_TYPE,
                       "Ljavax/microedition/lcdui/AlertType;");
    int y = uf->content_y;

    if (tobj) {
        type = fget(vm, tobj, NJ_C_ALERTTYPE, "atType", D_INT);
    }
    nj_gfx_set_color(g, UI_ALERT_BG);
    nj_gfx_fill_rect(g, 0, uf->content_y - uf->small_h, g->surf->width,
                     uf->content_h + uf->small_h);
    nj_gfx_set_color(g, UI_BORDER);
    nj_gfx_rect(g, 0, uf->content_y - uf->small_h, g->surf->width,
                uf->content_h + uf->small_h);
    /* The alert kind is part of the message the user sees. */
    {
        const char *kind = "INFO";
        switch ((int)type) {
        case 1: kind = "WARNING"; break;
        case 2: kind = "ERROR"; break;
        case 3: kind = "ALARM"; break;
        case 4: kind = "CONFIRMATION"; break;
        default: break;
        }
        nj_gfx_set_color(g, UI_DIM);
        {
            nj_ref ks = nj_string_new(vm, kind);
            if (ks) {
                ui_text(vm, g, uf->small, ks, UI_PAD, y, g->surf->width);
            }
        }
        y += uf->small_h + 1;
    }
    if (img) {
        nj_image view;
        if (img_view(vm, img, &view)) {
            nj_gfx_draw_image(g, &view, UI_PAD, y, 0, 0, view.width, view.height);
            y += view.height + 2;
        }
    }
    if (s) {
        nj_gfx_set_color(g, UI_TEXT);
        ui_text(vm, g, uf->font, s, UI_PAD, y, g->surf->width - 2 * UI_PAD);
    }
    /* The timeout is shown so a test (and a user) can see it counts down. */
    {
        nj_s4 t = fget(vm, alert, NJ_C_ALERT, F_AL_TIMEOUT, D_INT);
        nj_ref ts;
        char buf[32];
        int n = 0;
        buf[n++] = '-';
        buf[n++] = '>';
        buf[n++] = ' ';
        if (t < 0) {
            buf[n++] = 'i'; buf[n++] = 'n'; buf[n++] = 'f';
        } else {
            nj_s4 v = t;
            char tmp[16];
            int k = 0;
            if (v == 0) { tmp[k++] = '0'; }
            while (v > 0 && k < 15) { tmp[k++] = (char)('0' + (v % 10)); v /= 10; }
            while (k > 0) { buf[n++] = tmp[--k]; }
            buf[n++] = 'm'; buf[n++] = 's';
        }
        buf[n] = '\0';
        ts = nj_string_new(vm, buf);
        if (ts) {
            nj_gfx_set_color(g, UI_DIM);
            ui_text(vm, g, uf->small, ts, UI_PAD,
                    uf->content_y + uf->content_h - uf->small_h, g->surf->width);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The frame                                                           */
/* ------------------------------------------------------------------ */
static void ui_setup(nj_vm *vm, nj_gfx *g, nj_ref displayable, ui_frame *uf)
{
    nj_ref ticker = rget(vm, displayable, NJ_C_DISPLAYABLE, F_D_TICKER,
                         D_TICKER);
    const nj_font *f = nj_midp_font_for(vm, NULL);
    int top = UI_BAR_H;
    int bottom = g->surf->height - UI_BAR_H;

    uf->scroll = 0;
    uf->font = NULL;
    uf->small = NULL;
    uf->row_h = f ? f->height + 2 : 12;
    uf->small_h = f ? (f->height > 4 ? f->height - 2 : f->height) : 9;
    if (ticker) {
        top += 13;
    }
    uf->content_y = top + 1;
    uf->content_h = bottom - top - 2;
    if (uf->content_h < 0) {
        uf->content_h = 0;
    }
    (void)displayable;
}

/* Render any non-Canvas Displayable into the current surface. */
static void displayable_render(nj_vm *vm, nj_gfx *g, nj_ref d)
{
    ui_frame uf;
    nj_ref title = rget(vm, d, NJ_C_DISPLAYABLE, F_D_TITLE, D_STR);
    nj_ref ticker = rget(vm, d, NJ_C_DISPLAYABLE, F_D_TICKER, D_TICKER);

    ui_setup(vm, g, d, &uf);
    nj_gfx_set_color(g, UI_BG);
    nj_gfx_fill_rect(g, 0, 0, g->surf->width, g->surf->height);

    if (isa(vm, d, NJ_C_ALERT)) {
        alert_render(vm, g, d, &uf);
    } else if (isa(vm, d, NJ_C_LIST)) {
        list_render(vm, g, d, &uf);
    } else if (isa(vm, d, NJ_C_TEXTBOX)) {
        textbox_render(vm, g, d, &uf);
    } else if (isa(vm, d, NJ_C_FORM)) {
        form_render(vm, g, d, &uf, 1);
    } else {
        nj_gfx_set_color(g, UI_DIM);
        {
            nj_ref msg = nj_string_new(vm, "(unsupported screen)");
            if (msg) {
                ui_text(vm, g, uf.small, msg, UI_PAD, uf.content_y,
                        g->surf->width);
            }
        }
    }
    if (ticker) {
        int phase = fget(vm, ticker, NJ_C_TICKER, "tkPhase", D_INT);
        ui_ticker(vm, g, ticker, g->surf->width, UI_BAR_H, &phase);
        fput(vm, ticker, NJ_C_TICKER, "tkPhase", D_INT, phase);
    }
    ui_title(vm, g, title, g->surf->width, 0);
    ui_softkeys(vm, g, d, g->surf->width, &uf);
}

/* ================================================================== */
/* Input dispatch into the built-in widget screens                     */
/* ================================================================== */
/*
 * Every branch below returns 1 when it consumed the event.  These drive the
 * built-in UI only: a Canvas sees its events through its own methods, and the
 * built-in screens are the fallback the runner (and a test) uses to poke a
 * Form, List, TextBox or Alert.
 */
static int ui_is_up(int action)
{
    return (action == NJ_MIDP_KEY_UP || action == NJ_GAME_UP) ? 1 : 0;
}

static int ui_is_down(int action)
{
    return (action == NJ_MIDP_KEY_DOWN || action == NJ_GAME_DOWN) ? 1 : 0;
}

static int ui_is_fire(int action)
{
    return (action == NJ_MIDP_KEY_FIRE || action == NJ_GAME_FIRE ||
            action == NJ_MIDP_KEY_SOFT2) ? 1 : 0;
}

static int screen_move(nj_vm *vm, nj_ref d, int delta)
{
    if (isa(vm, d, NJ_C_LIST)) {
        nj_s4 n = fget(vm, d, NJ_C_LIST, "lsCount", D_INT);
        nj_s4 i = fget(vm, d, NJ_C_LIST, "lsIndex", D_INT);
        if (n <= 0) {
            return 0;
        }
        i += delta;
        if (i < 0) { i = 0; }
        if (i >= n) { i = n - 1; }
        fput(vm, d, NJ_C_LIST, "lsIndex", D_INT, i);
        nj_midp_repaint(vm);
        return 1;
    }
    if (isa(vm, d, NJ_C_FORM)) {
        nj_s4 n = fget(vm, d, NJ_C_FORM, "fmCount", D_INT);
        nj_s4 i = fget(vm, d, NJ_C_FORM, "fmIndex", D_INT);
        if (n <= 0) {
            return 0;
        }
        i += delta;
        if (i < 0) { i = 0; }
        if (i >= n) { i = n - 1; }
        fput(vm, d, NJ_C_FORM, "fmIndex", D_INT, i);
        nj_midp_repaint(vm);
        return 1;
    }
    return 0;
}

/* Move the value of the item that currently has the Form's focus. */
static int form_item_key(nj_vm *vm, nj_ref form, int delta)
{
    nj_ref items = rget(vm, form, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 idx = fget(vm, form, NJ_C_FORM, "fmIndex", D_INT);
    nj_ref it;

    if (!items || idx < 0 || (nj_u4)idx >= items->length) {
        return 0;
    }
    it = nj_array_get_ref(items, (nj_u4)idx);
    if (!it) {
        return 0;
    }
    if (isa(vm, it, NJ_C_CHOICEGROUP)) {
        nj_s4 n = fget(vm, it, NJ_C_CHOICEGROUP, "cgCount", D_INT);
        nj_s4 sel = fget(vm, it, NJ_C_CHOICEGROUP, "cgIndex", D_INT);
        if (n <= 0) {
            return 0;
        }
        sel += delta;
        if (sel < 0) { sel = 0; }
        if (sel >= n) { sel = n - 1; }
        fput(vm, it, NJ_C_CHOICEGROUP, "cgIndex", D_INT, sel);
        nj_midp_repaint(vm);
        return 1;
    }
    if (isa(vm, it, NJ_C_GAUGE)) {
        nj_s4 v = fget(vm, it, NJ_C_GAUGE, "gaValue", D_INT);
        nj_s4 max = fget(vm, it, NJ_C_GAUGE, "gaMax", D_INT);
        v += delta * 5;
        if (v < 0) { v = 0; }
        if (v > max) { v = max; }
        fput(vm, it, NJ_C_GAUGE, "gaValue", D_INT, v);
        item_state_changed(vm, it);
        nj_midp_repaint(vm);
        return 1;
    }
    if (isa(vm, it, NJ_C_TEXTFIELD)) {
        /* Up/down traverses Form items. Native editor left/right owns the
         * caret; trapping traversal here made a focused field inescapable. */
        return 0;
    }
    if (isa(vm, it, NJ_C_DATEFIELD)) {
        nj_s8 t = df_millis(vm, it);
        nj_s4 mode = fget(vm, it, NJ_C_DATEFIELD, "dfMode", D_INT);
        nj_s8 step = (mode == 2) ? 60000 : (nj_s8)86400000;   /* TIME vs DATE */
        t += (nj_s8)delta * step;
        df_set_millis(vm, it, t);
        item_state_changed(vm, it);
        nj_midp_repaint(vm);
        return 1;
    }
    return 0;
}

/* Activate the item that currently has the Form's focus. */
static int form_item_activate(nj_vm *vm, nj_ref form)
{
    nj_ref items = rget(vm, form, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 idx = fget(vm, form, NJ_C_FORM, "fmIndex", D_INT);
    nj_ref it;

    if (!items || idx < 0 || (nj_u4)idx >= items->length) {
        return 0;
    }
    it = nj_array_get_ref(items, (nj_u4)idx);
    if (!it) {
        return 0;
    }
    if (isa(vm, it, NJ_C_CHOICEGROUP)) {
        nj_s4 sel = fget(vm, it, NJ_C_CHOICEGROUP, "cgIndex", D_INT);
        nj_s4 mode = fget(vm, it, NJ_C_CHOICEGROUP, "cgType", D_INT);
        (void)mode;
        if (sel >= 0) {
            /* Toggle the element under the cursor (MULTIPLE) or select it
             * exclusively (EXCLUSIVE / IMPLICIT). */
            nj_field *fa = fld(vm, NJ_C_CHOICEGROUP, "cgSelected", "[Z", 0);
            nj_ref arr = fa ? nj_field_read_ref(vm, it, fa) : NULL;
            nj_s4 mode2 = fget(vm, it, NJ_C_CHOICEGROUP, "cgType", D_INT);
            if (arr && (nj_u4)sel < arr->length) {
                if (mode2 == 1) {
                    nj_u4 k;
                    for (k = 0u; k < arr->length; k++) {
                        arr->u.bytes[k] = 0;
                    }
                    arr->u.bytes[sel] = 1;
                } else {
                    arr->u.bytes[sel] = (nj_u1)(arr->u.bytes[sel] ? 0 : 1);
                }
            }
            item_state_changed(vm, it);
            nj_midp_repaint(vm);
            return 1;
        }
    }
    if (isa(vm, it, NJ_C_GAUGE)) {
        /* An interactive gauge ignores a plain activation. */
        return 0;
    }
    /* Anything else may carry its own default command. */
    {
        nj_ref c = rget(vm, it, NJ_C_ITEM, "itDefaultCmd", D_CMD);
        if (c) {
            return item_command_dispatch(vm, it, c);
        }
    }
    return 0;
}

static int screen_activate(nj_vm *vm, nj_ref d)
{
    if (isa(vm, d, NJ_C_LIST)) {
        /* For an IMPLICIT List the select action is the List's own command. */
        nj_ref sel = rgets(vm, NJ_C_LIST, "SELECT_COMMAND", D_CMD);
        nj_ref listener = rget(vm, d, NJ_C_DISPLAYABLE, F_D_LISTENER, D_CL);
        if (sel && listener) {
            return command_dispatch(vm, d, sel);
        }
        return 0;
    }
    if (isa(vm, d, NJ_C_FORM)) {
        if (form_item_activate(vm, d)) {
            return 1;
        }
    }
    if (isa(vm, d, NJ_C_ALERT)) {
        /* Dismissing an Alert returns to the screen that raised it. */
        nj_ref sel = rgets(vm, NJ_C_ALERT, "DISMISS_COMMAND", D_CMD);
        nj_ref listener = rget(vm, d, NJ_C_DISPLAYABLE, F_D_LISTENER, D_CL);
        if (sel && listener) {
            return command_dispatch(vm, d, sel);
        }
        return 0;
    }
    return command_default_activate(vm, d);
}

/*
 * Deliver a key to the current non-Canvas Displayable.  Returns 1 when the
 * screen consumed it.
 */
static int displayable_key(nj_vm *vm, nj_ref d, int action, int down)
{
    if (!down) {
        return 0;   /* the built-in widgets act on key-down */
    }
    if (!d) {
        return 0;
    }
    if (ui_is_up(action)) {
        if (isa(vm, d, NJ_C_FORM)) {
            if (form_item_key(vm, d, -1)) {
                return 1;
            }
        }
        return screen_move(vm, d, -1);
    }
    if (ui_is_down(action)) {
        if (isa(vm, d, NJ_C_FORM)) {
            if (form_item_key(vm, d, 1)) {
                return 1;
            }
        }
        return screen_move(vm, d, 1);
    }
    if (ui_is_fire(action)) {
        return screen_activate(vm, d);
    }
    if (action == NJ_MIDP_KEY_SOFT1) {
        return command_default_activate(vm, d);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Canvas event delivery                                               */
/* ------------------------------------------------------------------ */
/*
 * The Canvas event methods are ABSTRACT on Canvas, exactly as MIDP requires:
 * a subclass that forgets to override paint() must fail loudly instead of
 * leaving a blank screen.
 *
 * nj_call() runs the resolved method directly, so an abstract method with no
 * implementation would surface as UnsatisfiedLinkError (there is no native to
 * bind).  The receiver's own vtable is therefore checked first: if the method
 * that would run is still abstract, that is an AbstractMethodError, which is
 * what a device reports and what the MIDlet can actually catch.
 */
static int canvas_call_abstract(nj_vm *vm, nj_ref canvas, const char *name,
                                const char *desc, nj_value *a, int *why)
{
    nj_method *m;
    nj_status st;

    if (why) {
        *why = 0;
    }
    if (!canvas || !canvas->cls) {
        if (why) { *why = 1; }
        return 0;
    }
    m = nj_method_lookup(canvas->cls, name, desc);
    if (m && !m->code && (m->flags & NJ_ACC_ABSTRACT) != 0u) {
        (void)nj_throw(vm, "java/lang/AbstractMethodError", name);
        if (why) { *why = 2; }
        return 0;
    }
    st = nj_call(vm, canvas, NJ_C_CANVAS, name, desc, a, NULL);
    if (why) {
        *why = (st == NJ_OK) ? 0 : (int)st;
    }
    return st == NJ_OK;
}

static int canvas_call(nj_vm *vm, nj_ref canvas, const char *name, int argc,
                       nj_value *a)
{
    const char *desc;

    switch (argc) {
    case 1:  desc = "(I)V"; break;
    case 2:  desc = "(II)V"; break;
    case 0:  desc = "()V"; break;
    default: return 0;
    }
    return canvas_call_abstract(vm, canvas, name, desc, a, NULL);
}

/* paint() takes a Graphics, so it needs its own descriptor. */
static int canvas_call_paint(nj_vm *vm, nj_ref canvas, nj_ref g)
{
    nj_value a[1];
    a[0].r = g;
    int why = 0;
    return canvas_call_abstract(vm, canvas, "paint",
                                "(L" NJ_C_GRAPHICS ";)V", a, &why);
}

/*
 * Run the MIDlet's paint() with its Graphics rooted.
 *
 * paint() is Java: it allocates, so a collection can happen inside it.  The
 * only reference to this Graphics is a C local in the caller, which the
 * collector cannot see, so without a root it frees the object - and the
 * title's own `aload_1; invokevirtual drawImage` then dispatches on the freed
 * block.  That was a SIGSEGV inside nj_method_lookup on a receiver reported as
 * "idx=-1 super=(nil) mcount=1 flags=0": not one of this VM's classes.
 *
 * src/core/nj_internal.h states the rule this call site was breaking.
 */
static int canvas_visible(nj_vm *vm, nj_ref canvas);

/*
 * Paint the canvas with its Graphics rooted - and with showNotify()/
 * sizeChanged() inside the root too.
 *
 * Both halves are Java and both allocate, and the Graphics exists only in the
 * caller's C local.  Rooting around paint() alone was not enough and did not
 * fix anything: showNotify() runs first and the collection it triggers freed
 * the Graphics before paint() was reached, so a title's own
 * `aload_1; invokevirtual drawImage` dispatched on the freed block - SIGSEGV
 * inside nj_method_lookup on a receiver that was not one of this VM's classes
 * (idx=-1, super=nil).  It looked like a bad class table; it was a live
 * reference nobody was holding.
 */
static int canvas_call_paint_rooted(nj_vm *vm, nj_ref canvas, nj_ref g)
{
    nj_u4 mark = nj_root_mark(vm);
    int ok;

    g = nj_root(vm, g);
    (void)canvas_visible(vm, canvas);
    ok = canvas_call_paint(vm, canvas, g);
    nj_root_release(vm, mark);
    return ok;
}

static int canvas_visible(nj_vm *vm, nj_ref canvas)
{
    /*
     * sizeChanged(w, h) first, then showNotify() - the order MIDP specifies.
     *
     * This was never called at all, and a game that computes its layout there
     * drew nothing: Game15MIDlet's paint() uses offsetX/offsetY/cellSize, all
     * set in sizeChanged(), so every grid line was drawn at (0,0) and the whole
     * puzzle came out as one black dot in the corner.
     *
     * Delivered when the canvas has no buffer yet or the buffer no longer
     * matches the screen, which covers both the first show and a resize
     * without needing a field to remember the last size delivered.
     */
    if (isa(vm, canvas, NJ_C_CANVAS)) {
        nj_ref buf = rget(vm, canvas, NJ_C_CANVAS, F_C_BUFFER, D_IMAGE);
        nj_surface *scr = nj_midp_screen(vm);
        int bw = 0, bh = 0;
        int need = 0;

        if (scr) {
            /*
             * On the FIRST show as well as on a resize.  Testing only "is
             * there a buffer of the right size" is not enough: the frame
             * painter creates the buffer at screen size before the canvas is
             * ever shown, so that test is false exactly when sizeChanged()
             * matters most.
             */
            if (!fget(vm, canvas, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL)) {
                need = 1;
            } else if (!buf) {
                need = 1;
            } else if (nj_midp_image_size(vm, buf, &bw, &bh)) {
                need = (bw != scr->width || bh != scr->height);
            }
            if (need) {
                nj_value a[2];
                a[0].i = scr->width;
                a[1].i = scr->height;
                (void)canvas_call(vm, canvas, "sizeChanged", 2, a);
                if (vm->exception) {
                    return 0;
                }
            }
        }
    }
    if (!fget(vm, canvas, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL)) {
        (void)canvas_call(vm, canvas, "showNotify", 0, NULL);
        fput(vm, canvas, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL, 1);
        if (vm->exception) {
            return 0;
        }
    }
    if (!fget(vm, canvas, NJ_C_CANVAS, F_C_PRESSED, D_BOOL)) {
        (void)canvas_call(vm, canvas, "drawNotify", 0, NULL);
        fput(vm, canvas, NJ_C_CANVAS, F_C_PRESSED, D_BOOL, 1);
    }
    return 1;
}

/*
 * Keep the sticky key bitmap in step: GameCanvas.getKeyStates() reads it.
 *
 * Keep two bitmaps: current holds and presses latched since the last poll.
 * A release clears only the held bitmap; getKeyStates() consumes only the
 * latch. A quick tap remains visible once, and a hold survives every poll. */

static void canvas_keys_update(nj_vm *vm, nj_ref canvas, int code, int down)
{
    int mask;

    nj_s4 held;

    if (!canvas) {
        return;
    }
    mask = midp_key_state_bit(code);
    if (mask == 0) {
        return;
    }
    held = fget(vm, canvas, NJ_C_CANVAS, NJ_F_C_HELD, D_INT);
    fput(vm, canvas, NJ_C_CANVAS, NJ_F_C_HELD, D_INT,
         down ? held | mask : held & ~mask);
    if (down) {
        nj_s4 keys = fget(vm, canvas, NJ_C_CANVAS, F_C_KEYS, D_INT);
        fput(vm, canvas, NJ_C_CANVAS, F_C_KEYS, D_INT, keys | mask);
    }
}

int nj_midp_key(nj_vm *vm, int key, int down)
{
    nj_ref d;
    int code;
    int consumed = 0;
    nj_value a[1];

    if (!vm) {
        return 0;
    }
    d = nj_midp_current(vm);
    if (!d) {
        return 0;
    }
    code = nj_key_to_midp(key);
    if (code == 0 && key != 0) {
        /* Not a key we know: pass the raw value on so a Canvas can still see
         * it through getKeyCode()/getGameAction(). */
        code = (key > 0 && key < 0x10000) ? key : 0;
    }

    if (isa(vm, d, NJ_C_CANVAS)) {
        int suppressed = isa(vm, d, NJ_C_GAMECANVAS) &&
                         fget(vm, d, NJ_C_GAMECANVAS, F_GC_SUPPRESS, D_BOOL);
        canvas_keys_update(vm, d, code, down);

        /*
         * A soft key belongs to the COMMANDS when the Canvas has any.  The
         * check is on key-down only: a soft key that fired on release as well
         * would run the command twice for one press.
         */
        if (down && (key == NJ_KEY_SOFT_LEFT || key == NJ_KEY_SOFT_RIGHT)
            && cmd_count(vm, d) > 0) {
            (void)canvas_visible(vm, d);
            if (canvas_command_activate(vm, d, key)) {
                return 1;
            }
        }

        if (code != 0) {
            a[0].i = code;
            if (!suppressed) {
                (void)canvas_visible(vm, d);
                (void)canvas_call(vm, d, down ? "keyPressed" : "keyReleased", 1,
                                  a);
            }
            consumed = 1;
        }
        /*
         * An exception raised by keyPressed()/keyReleased() - including the
         * AbstractMethodError for an unimplemented hook - is left pending so
         * the caller sees it.
         */
        if (vm->exception) {
            return consumed;
        }
    }
    if (!consumed) {
        /*
         * A built-in screen owns the keyboard entirely: a MIDlet never sees a
         * key while a Form/List/TextBox/Alert is showing, so the event counts
         * as consumed whether or not it moved the selection.  Returning 0 here
         * would invite the runner to act on a key the application is using as
         * a menu.
         */
        (void)displayable_key(vm, d, code, down);
        consumed = 1;
    }
    if (vm->exception) {
        return consumed;
    }
    if (consumed) {
        nj_midp_repaint(vm);
    }
    return consumed;
}

int nj_midp_pointer(nj_vm *vm, int x, int y, int kind)
{
    nj_ref d;
    int cx = x, cy = y;
    nj_value a[2];

    if (!vm) {
        return 0;
    }
    d = nj_midp_current(vm);
    if (!d) {
        return 0;
    }
    if (isa(vm, d, NJ_C_CANVAS)) {
        int cw = fget(vm, d, NJ_C_CANVAS, F_C_WIDTH, D_INT);
        int ch = fget(vm, d, NJ_C_CANVAS, F_C_HEIGHT, D_INT);
        nj_surface *s = nj_midp_screen(vm);
        const char *method;

        if (s && cw > 0 && ch > 0 && (cw != s->width || ch != s->height)) {
            cx = x * cw / s->width;
            cy = y * ch / s->height;
        }
        /*
         * A move is NOT a press.
         *
         * This used to be `down ? "pointerPressed" : "pointerReleased"`, and
         * NJ_EV_POINTER_MOVE arrives as 2 - truthy - so every drag reported
         * another press: a MIDlet following a finger got a stream of
         * pointerPressed() at the new coordinates and never one
         * pointerDragged(), while F_C_PRESSED was rewritten and the key state
         * was disturbed on every move.  pointerDragged is in the Canvas
         * contract and was simply unreachable.
         */
        if (kind == NJ_POINTER_MOVE) {
            method = "pointerDragged";
        } else {
            method = (kind == NJ_POINTER_DOWN) ? "pointerPressed" : "pointerReleased";
            canvas_keys_update(vm, d, 0, (kind == NJ_POINTER_DOWN));
        }
        a[0].i = cx;
        a[1].i = cy;
        if (!canvas_visible(vm, d)) {
            nj_clear_exception(vm);
            return 1;
        }
        (void)canvas_call(vm, d, method, 2, a);
        if (vm->exception) {
            return 1;   /* the MIDlet's exception is the caller's to handle */
        }
        /* The pressed flag means "a pointer is down", which a move does not
         * change: clearing it here would make a drag look like a release to
         * every Canvas that reads isPointerPressed(). */
        if (kind != NJ_POINTER_MOVE) {
            fput(vm, d, NJ_C_CANVAS, F_C_PRESSED, D_BOOL,
                 (kind == NJ_POINTER_DOWN) ? 1 : 0);
        }
        nj_midp_repaint(vm);
        return 1;
    }
    /*
     * A non-Canvas Displayable has no pointer methods, so a tap can only mean
     * one thing there: activate the focused item, exactly as the fire key
     * does.  A drag on a menu is not an activation.
     */
    if (kind == NJ_POINTER_DOWN) {
        int consumed = screen_activate(vm, d);
        nj_midp_repaint(vm);
        return consumed;
    }
    return 0;
}

/* ================================================================== */
/* nj_midp_paint - one frame                                           */
/* ================================================================== */
nj_status nj_midp_paint(nj_vm *vm)
{
    if (!vm) {
        return NJ_ERR;
    }
    vm->paints++;
    /* Remember that this frame has been painted: the runner paints on top of
     * whatever the application did only when the application did not paint
     * itself (see the frame hook in nj_lib_midlet.c). */
    if (vm->in_paint) {
        vm->app_painted = 1u;   /* serviceRepaints() asked for this one */
        vm->app_painted_frame = 1u;
    }
    vm->painted_in_frame = 1u;
    nj_ref d;
    nj_surface *scr;
    nj_ref buf = NULL;
    nj_surface buf_surf;
    int w, h;

    /*
     * MIDP's callSerially() contract is "run before the next paint", so the
     * queue is drained here, at the top of the frame.
     */
    display_drain_queue(vm);
    scr = nj_midp_screen(vm);
    if (!scr) {
        return NJ_ERR;
    }
    d = nj_midp_current(vm);
    if (!d) {
        nj_surface_fill(scr, 0xFF000000u);
        vm->repaint_pending = 0u;
        return NJ_OK;
    }
    w = scr->width;
    h = scr->height;

    if (isa(vm, d, NJ_C_CANVAS) && !isa(vm, d, NJ_C_GAMECANVAS)) {
        nj_ref g;
        buf = canvas_ensure_buffer(vm, d, w, h);
        if (!buf) {
            nj_clear_exception(vm);
            return NJ_ERR;
        }
        /*
         * The application draws into its own buffer through a Graphics that
         * targets it; paint() is abstract on Canvas, so a subclass that does
         * not implement it gets AbstractMethodError rather than a silent blank
         * frame.
         */
        g = gfx_new(vm, buf);
        if (!g) {
            return NJ_ERR;   /* names the class; not a silent blank frame */
        }
        if (!canvas_call_paint_rooted(vm, d, g)) {
            /*
             * Leave a real exception pending if there is one: paint() is
             * abstract on Canvas, so "method not implemented" is exactly what
             * the caller must see.
             *
             * If there is NO exception, do not invent one.  The old code
             * fabricated "AbstractMethodError: paint" here for any failure,
             * including ones that have nothing to do with abstractness - which
             * sent a whole debugging session after the wrong bug.  Say what is
             * actually known instead.
             */
            if (!vm->exception) {
                char mbuf[160];
                nj_u4 n = 0u;
                const char *q = "paint() on ";
                const char *c = (d && d->cls) ? d->cls->name : "?";
                const char *t = " failed without raising an exception";
                while (*q && n + 1u < sizeof(mbuf)) { mbuf[n++] = *q++; }
                while (*c && n + 1u < sizeof(mbuf)) { mbuf[n++] = *c++; }
                while (*t && n + 1u < sizeof(mbuf)) { mbuf[n++] = *t++; }
                mbuf[n] = '\0';
                (void)nj_throw(vm, "java/lang/RuntimeException", mbuf);
            }
            return NJ_ERR;
        }
        /*
         * No blit here.  The buffer is mirrored onto the screen by
         * nj_midp_present() at the frame boundary, because a title may keep
         * drawing through the Graphics paint() was given after it returns.
         */
    } else if (isa(vm, d, NJ_C_GAMECANVAS)) {
        /*
         * The game package owns GameCanvas; this is only the fallback path for
         * a GameCanvas subclass that implements paint() too.
         */
        buf = canvas_ensure_buffer(vm, d, w, h);
        if (buf) {
            nj_ref g = gfx_new(vm, buf);
            if (g) {
                (void)canvas_call_paint_rooted(vm, d, g);
                nj_clear_exception(vm);
            }
            if (nj_midp_surface(vm, buf, &buf_surf)) {
                nj_gfx blit;
                nj_image view;
                if (img_view(vm, buf, &view)) {
                    nj_gfx_init(&blit, scr);
                    nj_gfx_draw_image(&blit, &view, 0, 0, 0, 0, view.width,
                                      view.height);
                }
            }
        } else {
            nj_clear_exception(vm);
        }
    } else {
        /* An Image is a legitimate paint target for the built-in renderers. */
        buf = canvas_ensure_buffer(vm, d, w, h);
        if (!buf) {
            nj_clear_exception(vm);
            return NJ_ERR;
        }
        if (nj_midp_surface(vm, buf, &buf_surf)) {
            nj_gfx g;
            nj_image view;
            nj_gfx_init(&g, &buf_surf);
            displayable_render(vm, &g, d);
            /* A Screen has no buffer field of its own - canvas_ensure_buffer
             * returns a fresh image for it - so the screen must be updated
             * here, before the temporary is dropped. */
            if (img_view(vm, buf, &view)) {
                nj_gfx blit;
                nj_gfx_init(&blit, scr);
                nj_gfx_draw_image(&blit, &view, 0, 0, 0, 0, view.width,
                                  view.height);
            }
        }
    }
    vm->repaint_pending = 0u;
    return NJ_OK;
}

/* ================================================================== */
/* Display                                                             */
/* ================================================================== */
nj_ref nj_midp_display(nj_vm *vm)
{
    nj_ref d;

    if (!vm) {
        return NULL;
    }
    if (vm->display) {
        return vm->display;
    }
    d = nj_object_new(vm, NJ_C_DISPLAY);
    if (!d) {
        return NULL;   /* the pending exception names the class */
    }
    rput(vm, d, NJ_C_DISPLAY, F_DISP_CUR, D_DISPLAY, NULL);
    rput(vm, d, NJ_C_DISPLAY, F_DISP_ALERT, D_DISPLAY, NULL);
    rputs(vm, NJ_C_DISPLAY, "sDisplay", D_DISPLAYS, d);
    vm->display = d;
    return d;
}

nj_ref nj_midp_current(nj_vm *vm)
{
    nj_ref d;

    if (!vm) {
        return NULL;
    }
    if (vm->current_displayable) {
        return vm->current_displayable;
    }
    d = nj_midp_display(vm);
    if (!d) {
        return NULL;
    }
    return rget(vm, d, NJ_C_DISPLAY, F_DISP_CUR, D_DISPLAY);
}

/* Tell the outgoing Canvas it is no longer visible. */
static void displayable_hide(nj_vm *vm, nj_ref d)
{
    if (!d) {
        return;
    }
    if (isa(vm, d, NJ_C_CANVAS) &&
        fget(vm, d, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL)) {
        (void)canvas_call(vm, d, "hideNotify", 0, NULL);
        fput(vm, d, NJ_C_CANVAS, F_C_VISIBLE, D_BOOL, 0);
        fput(vm, d, NJ_C_CANVAS, F_C_PRESSED, D_BOOL, 0);
        if (vm->exception) {
            nj_clear_exception(vm);
        }
    }
}

nj_status nj_midp_set_current(nj_vm *vm, nj_ref d)
{
    nj_ref disp;

    if (!vm) {
        return NJ_ERR;
    }
    disp = nj_midp_display(vm);
    if (!disp) {
        return NJ_ERR;
    }
    {
        nj_ref old = nj_midp_current(vm);
        if (old && old != d) {
            displayable_hide(vm, old);
        }
    }
    rput(vm, disp, NJ_C_DISPLAY, F_DISP_CUR, D_DISPLAY, d);
    vm->current_displayable = d;
    /*
     * showNotify() is deliberately NOT delivered here.
     *
     * On a real device setCurrent() returns before the Displayable is shown:
     * showNotify() arrives later, on the event thread, and the first paint
     * follows it.  Delivering it synchronously made the Canvas "visible" the
     * instant setCurrent() returned, which changes what the MIDlet is allowed
     * to do next - Displayable.setFullScreenMode() must throw
     * IllegalStateException while the Displayable is visible, so
     *
     *     Display.getDisplay(this).setCurrent(canvas);
     *     canvas.setFullScreenMode(true);
     *
     * threw during construction for 开心农场.  That sequence is legal on the
     * phones these titles were written for, and there is no other way to ask
     * for full screen before the first frame.
     *
     * The paint path calls canvas_visible() before paint(), so showNotify()
     * still runs first - it just runs on the frame that shows the Canvas
     * rather than inside setCurrent().
     */
    nj_midp_repaint(vm);
    return NJ_OK;
}

NJFN(disp_getDisplay)
{
    nj_ref d = nj_midp_display(vm);
    if (!d) {
        return new_failed(vm);
    }
    ret->r = d;
    return NJ_OK;
}

NJFN(disp_setCurrent)
{
    nj_status st = nj_midp_set_current(vm, NJ_ARG_REF(args, 0));
    return st;
}

NJFN(disp_setCurrent_alert)
{
    nj_ref alert = NJ_ARG_REF(args, 0);
    nj_ref next = NJ_ARG_REF(args, 1);
    nj_ref disp = nj_midp_display(vm);
    nj_ref d;

    if (!disp) {
        return NJ_ERR;
    }
    if (next) {
        rput(vm, disp, NJ_C_DISPLAY, F_DISP_ALERT, D_DISPLAY, next);
    } else {
        d = nj_midp_current(vm);
        if (alert != d) {
            rput(vm, disp, NJ_C_DISPLAY, F_DISP_ALERT, D_DISPLAY, d);
        }
    }
    return nj_midp_set_current(vm, alert);
}

/*
 * setCurrentItem(Item): the screen the item belongs to becomes current and the
 * built-in renderer focuses that row.  It is a hint, so an item that is not on
 * any screen is simply ignored.
 */
NJFN(disp_setCurrentItem)
{
    nj_ref item = NJ_ARG_REF(args, 0);
    nj_ref owner;

    if (!item) {
        return nj_throw(vm, "java/lang/NullPointerException", "item is null");
    }
    owner = rget(vm, item, NJ_C_ITEM, "itOwner", D_FORM);
    if (!owner) {
        owner = rget(vm, item, NJ_C_ITEM, "itOwner", NJ_C_DISPLAYABLE);
    }
    if (!owner) {
        return NJ_OK;
    }
    {
        nj_ref items = rget(vm, owner, NJ_C_FORM, "fmItems", D_ITEMARR);
        nj_s4 n = fget(vm, owner, NJ_C_FORM, "fmCount", D_INT);
        nj_s4 i;
        for (i = 0; i < n && items && (nj_u4)i < items->length; i++) {
            if (nj_array_get_ref(items, (nj_u4)i) == item) {
                fput(vm, owner, NJ_C_FORM, "fmIndex", D_INT, i);
                break;
            }
        }
    }
    return nj_midp_set_current(vm, owner);
}

NJFN(disp_getCurrent)
{
    ret->r = nj_midp_current(vm);
    return NJ_OK;
}

NJFN(disp_isColor)
{
    /*
     * Every pixel format this runtime supports is a colour format.  RGB565 has
     * 65536 of them; calling it "not colour" is exactly backwards, and it is
     * the kind of backwards that only shows up when a game gates on it:
     *
     *     if (!myDisplay.isColor() || myDisplay.numColors() < 256)
     *         throw new Exception("game requires full-color screen");
     *
     * which is how three of the corpus titles came to refuse to start on a
     * 16-bit display that is perfectly capable of running them.
     *
     * There is no monochrome format to test for.  If one is ever added, this is
     * where the test goes.
     */
    ret->i = 1;
    return NJ_OK;
}

NJFN(disp_numColors)
{
    ret->i = (vm->screen.format == NJ_PIXFMT_RGB565) ? 65536 : 16777216;
    return NJ_OK;
}

NJFN(disp_numAlphaLevels)
{
    ret->i = (vm->midp_flags & NJ_MIDP_SUPPORTS_ALPHA) ? 256 : 1;
    return NJ_OK;
}

NJFN(disp_flashBacklight)
{
    NJ_UNUSED(args);
    return NJ_OK;   /* no backlight on a desktop port */
}

NJFN(disp_vibrate)
{
    nj_s4 ms = NJ_ARG_INT(args, 0);
    if (ms < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative duration");
    }
    if (vm->port && vm->port->vibrate) {
        vm->port->vibrate(vm->port->ud, (int)ms);
    }
    return NJ_OK;
}

NJFN(disp_getBestImageWidth)
{
    nj_s4 lvl = NJ_ARG_INT(args, 0);
    nj_surface *s = nj_midp_screen(vm);
    ret->i = s ? s->width : (nj_s4)NJ_SCREEN_WIDTH;
    NJ_UNUSED(lvl);
    return NJ_OK;
}

NJFN(disp_getBestImageHeight)
{
    nj_s4 lvl = NJ_ARG_INT(args, 0);
    nj_surface *s = nj_midp_screen(vm);
    ret->i = s ? s->height : (nj_s4)NJ_SCREEN_HEIGHT;
    NJ_UNUSED(lvl);
    return NJ_OK;
}

/*
 * getColor(int colorSpecifier): the colour the device paints each of the six
 * documented screen parts with.  The palette below IS the one the runtime
 * paints with, so a game that themes itself from these values matches the
 * system screens instead of guessing.
 */
static const nj_u4 disp_palette[6] = {
    UI_BG,      /* COLOR_BACKGROUND             */
    UI_TEXT,    /* COLOR_FOREGROUND             */
    UI_SEL_BG,  /* COLOR_HIGHLIGHTED_BACKGROUND */
    UI_SEL_FG,  /* COLOR_HIGHLIGHTED_FOREGROUND */
    UI_BORDER,  /* COLOR_BORDER                 */
    UI_BORDER   /* COLOR_HIGHLIGHTED_BORDER     */
};

NJFN(disp_getColor)
{
    nj_s4 spec = NJ_ARG_INT(args, 0);

    if (spec < 0 || spec > 5) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "invalid color specifier");
    }
    ret->i = (nj_s4)(disp_palette[spec] & 0x00FFFFFFu);   /* 0x00RRGGBB */
    return NJ_OK;
}

/* The boolean spelling of the same query, which some compile stubs declare:
 * "does the device support this colour".  Every specifier above is supported,
 * and an invalid one throws exactly as the int form does. */
NJFN(disp_getColor_bool)
{
    nj_s4 spec = NJ_ARG_INT(args, 0);

    if (spec < 0 || spec > 5) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "invalid color specifier");
    }
    ret->i = 1;
    return NJ_OK;
}

/* getBorderStyle(boolean highlighted): this runtime draws a solid border in
 * both states (Graphics.SOLID is 0), so that is the honest answer. */
NJFN(disp_getBorderStyle)
{
    NJ_UNUSED(args);
    ret->i = 0;   /* Graphics.SOLID */
    return NJ_OK;
}

/*
 * callSerially(Runnable): the runnable is queued in a Java Vector and executed
 * at the start of the next frame, which is exactly the guarantee MIDP gives -
 * "after the current event has been processed, before the next paint".
 */
NJFN(disp_callSerially)
{
    nj_ref self = NJ_THIS(args);
    nj_ref r = NJ_ARG_REF(args, 0);
    nj_ref q;
    nj_value a[1];

    if (!r) {
        return nj_throw(vm, "java/lang/NullPointerException", "runnable is null");
    }
    q = rget(vm, self, NJ_C_DISPLAY, "dpQueue", D_VECTOR);
    if (!q) {
        q = nj_object_new(vm, "java/util/Vector");
        if (!q) {
            return nj_throw_oom(vm);
        }
        if (nj_call_special(vm, q, "java/util/Vector", "<init>", "()V", NULL,
                            NULL) != NJ_OK) {
            return NJ_ERR;
        }
        rput(vm, self, NJ_C_DISPLAY, "dpQueue", D_VECTOR, q);
    }
    a[0].r = r;
    (void)nj_call(vm, q, "java/util/Vector", "addElement",
                  "(Ljava/lang/Object;)V", a, NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

/* Run and clear the pending callSerially queue.  Called once per frame by the
 * frame pump; harmless when empty. */
static void display_drain_queue(nj_vm *vm)
{
    nj_ref disp = nj_midp_display(vm);
    nj_ref q;
    int guard = 0;

    if (!disp) {
        return;
    }
    q = rget(vm, disp, NJ_C_DISPLAY, "dpQueue", D_VECTOR);
    while (q && guard++ < 64) {
        nj_s4 n = nj_call_int(vm, q, "java/util/Vector", "size", "()I", NULL);
        nj_ref r;
        if (n <= 0) {
            break;
        }
        r = nj_call_ref(vm, q, "java/util/Vector", "elementAt",
                        "(I)Ljava/lang/Object;", &(nj_value){ .i = 0 });
        (void)nj_call(vm, q, "java/util/Vector", "removeElementAt", "(I)V",
                      &(nj_value){ .i = 0 }, NULL);
        if (!r) {
            break;
        }
        (void)nj_call(vm, r, "java/lang/Runnable", "run", "()V", NULL, NULL);
        if (vm->exception) {
            nj_clear_exception(vm);
            break;
        }
    }
}

/* ================================================================== */
/* Item and the widgets                                                */
/* ================================================================== */

static void item_init_on(nj_vm *vm, nj_ref self)
{
    rput(vm, self, NJ_C_ITEM, "itLabel", D_STR, NULL);
    fput(vm, self, NJ_C_ITEM, "itLayout", D_INT, -1);
    fput(vm, self, NJ_C_ITEM, "itPrefW", D_INT, -1);
    fput(vm, self, NJ_C_ITEM, "itPrefH", D_INT, -1);
    fput(vm, self, NJ_C_ITEM, "itCmdType", D_INT, -1);
    rput(vm, self, NJ_C_ITEM, "itDefaultCmd", D_CMD, NULL);
    rput(vm, self, NJ_C_ITEM, "itCmdListener", D_ICL, NULL);
    rput(vm, self, NJ_C_ITEM, "itOwner", NULL, NULL);
    rput(vm, self, NJ_C_ITEM, "itImage", D_IMAGE, NULL);
}

NJFN(item_init)
{
    item_init_on(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(item_getLabel)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_ITEM, "itLabel", D_STR);
    return NJ_OK;
}

NJFN(item_setLabel)
{
    rput(vm, NJ_THIS(args), NJ_C_ITEM, "itLabel", D_STR, NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(item_getLayout)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_ITEM, "itLayout", D_INT);
    return NJ_OK;
}

NJFN(item_setLayout)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 l = NJ_ARG_INT(args, 0);

    if (l != -1 && (l & ~(nj_s4)0xFF) != 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown layout directives");
    }
    fput(vm, self, NJ_C_ITEM, "itLayout", D_INT, l);
    if (l != -1) {
        fput(vm, self, NJ_C_ITEM, "itPrefW", D_INT, -1);
        fput(vm, self, NJ_C_ITEM, "itPrefH", D_INT, -1);
    }
    return NJ_OK;
}

NJFN(item_addCommand)
{
    nj_ref self = NJ_THIS(args);
    nj_ref c = NJ_ARG_REF(args, 0);
    nj_s4 t;

    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", "command is null");
    }
    t = fget(vm, c, NJ_C_COMMAND, "cmdType", D_INT);
    /* MIDP recommends ITEM commands, but permits other command types. */
    fput(vm, self, NJ_C_ITEM, "itCmdType", D_INT, t);
    return NJ_OK;
}

NJFN(item_removeCommand)
{
    NJ_UNUSED(args);
    return NJ_OK;
}

NJFN(item_setItemCommandListener)
{
    rput(vm, NJ_THIS(args), NJ_C_ITEM, "itCmdListener", D_ICL,
         NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(item_setDefaultCommand)
{
    nj_ref self = NJ_THIS(args);
    nj_ref c = NJ_ARG_REF(args, 0);

    rput(vm, self, NJ_C_ITEM, "itDefaultCmd", D_CMD, c);
    return NJ_OK;
}

NJFN(item_getDefaultCommand)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_ITEM, "itDefaultCmd", D_CMD);
    return NJ_OK;
}

NJFN(item_setPreferredSize)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 w = NJ_ARG_INT(args, 0);
    nj_s4 h = NJ_ARG_INT(args, 1);

    if (w < -1 || h < -1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "preferred size must be -1 or positive");
    }
    fput(vm, self, NJ_C_ITEM, "itPrefW", D_INT, w);
    fput(vm, self, NJ_C_ITEM, "itPrefH", D_INT, h);
    return NJ_OK;
}

NJFN(item_getMinimumWidth)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 w = fget(vm, self, NJ_C_ITEM, "itPrefW", D_INT);
    ret->i = (w < 0) ? 0 : w;
    return NJ_OK;
}

NJFN(item_getMinimumHeight)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 h = fget(vm, self, NJ_C_ITEM, "itPrefH", D_INT);
    ret->i = (h < 0) ? 0 : h;
    return NJ_OK;
}

NJFN(item_getPreferredWidth)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 w = fget(vm, self, NJ_C_ITEM, "itPrefW", D_INT);
    if (w < 0) {
        w = 0;
    }
    if (w == 0) {
        w = 64;   /* the native UI's natural width for an unsized Item */
    }
    ret->i = w;
    return NJ_OK;
}

NJFN(item_getPreferredHeight)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 h = fget(vm, self, NJ_C_ITEM, "itPrefH", D_INT);
    if (h < 0) {
        h = UI_ITEM_H;
    }
    ret->i = h;
    return NJ_OK;
}

NJFN(item_notifyStateChanged)
{
    item_state_changed(vm, NJ_THIS(args));
    return NJ_OK;
}

/* The default Item renderer asks the item for its text and height. */
NJFN(item_njText)
{
    ret->r = NULL;
    return NJ_OK;
}

NJFN(item_njHeight)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 w = NJ_ARG_INT(args, 0);
    nj_s4 h = fget(vm, self, NJ_C_ITEM, "itPrefH", D_INT);
    NJ_UNUSED(w);
    ret->i = (h > 0) ? h : UI_ITEM_H;
    return NJ_OK;
}

NJFN(item_njSelected)
{
    NJ_UNUSED(args);
    ret->r = NULL;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* StringItem / Spacer                                                 */
/* ------------------------------------------------------------------ */
/* PLAIN / HYPERLINK / BUTTON, mirroring StringItem's appearance modes. */
static const char *string_item_appearance(nj_s4 mode)
{
    switch ((int)mode) {
    case 1: return "> ";
    case 2: return "[ ";
    default: return "";
    }
}

/* StringItem(String label, String text[, int appearanceMode]) */
static nj_status si_init_common(nj_vm *vm, nj_ref self, nj_value *args,
                                nj_s4 mode)
{
    item_init_on(vm, self);
    if (mode < 0 || mode > 2) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown appearance mode");
    }
    rput(vm, self, NJ_C_ITEM, "itLabel", D_STR, NJ_ARG_REF(args, 0));
    rput(vm, self, NJ_C_STRINGITEM, "siText", D_STR, NJ_ARG_REF(args, 1));
    fput(vm, self, NJ_C_STRINGITEM, "siMode", D_INT, mode);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(si_init)
{
    return si_init_common(vm, NJ_THIS(args), args, 0);
}

NJFN(si_init_mode)
{
    return si_init_common(vm, NJ_THIS(args), args, NJ_ARG_INT(args, 2));
}

NJFN(si_getFont)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_STRINGITEM, "siFont", D_FONT);
    return NJ_OK;
}

NJFN(si_setFont)
{
    nj_ref self = NJ_THIS(args);
    nj_ref f = NJ_ARG_REF(args, 0);
    if (!f) {
        return nj_throw(vm, "java/lang/NullPointerException", "font is null");
    }
    rput(vm, self, NJ_C_STRINGITEM, "siFont", D_FONT, f);
    return NJ_OK;
}

NJFN(si_getText)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_STRINGITEM, "siText", D_STR);
    return NJ_OK;
}

NJFN(si_setText)
{
    rput(vm, NJ_THIS(args), NJ_C_STRINGITEM, "siText", D_STR,
         NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(si_getAppearanceMode)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_STRINGITEM, "siMode", D_INT);
    return NJ_OK;
}

NJFN(si_addCommand)
{
    nj_ref self = NJ_THIS(args);
    nj_ref c = NJ_ARG_REF(args, 0);

    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", "command is null");
    }
    rput(vm, self, NJ_C_STRINGITEM, "siCmd", D_CMD, c);
    return NJ_OK;
}

NJFN(si_removeCommand)
{
    rput(vm, NJ_THIS(args), NJ_C_STRINGITEM, "siCmd", D_CMD, NULL);
    return NJ_OK;
}

NJFN(si_setItemCommandListener)
{
    rput(vm, NJ_THIS(args), NJ_C_STRINGITEM, "siCmdListener", D_ICL,
         NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(si_njText)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = rget(vm, self, NJ_C_STRINGITEM, "siText", D_STR);
    nj_ref cmd = rget(vm, self, NJ_C_STRINGITEM, "siCmd", D_CMD);
    nj_s4 mode = fget(vm, self, NJ_C_STRINGITEM, "siMode", D_INT);
    const char *prefix = string_item_appearance(mode);

    if (cmd && mode != 0) {
        nj_ref label = rget(vm, cmd, NJ_C_COMMAND, "cmdLabel", D_STR);
        nj_ref out;
        nj_u2 buf[128];
        int n = 0;
        const char *p = prefix;
        while (*p && n < 120) { buf[n++] = (nj_u2)(nj_u1)*p++; }
        if (s) {
            int i;
            nj_u4 sn = nj_string_len(s);
            for (i = 0; (nj_u4)i < sn && n < 120; i++) {
                buf[n++] = nj_string_chars(s)[i];
            }
        }
        if (label) {
            int i;
            nj_u4 ln = nj_string_len(label);
            buf[n++] = ' ';
            for (i = 0; (nj_u4)i < ln && n < 126; i++) {
                buf[n++] = nj_string_chars(label)[i];
            }
        }
        out = nj_new_string_utf16(vm, buf, n);
        if (!out) {
            return NJ_ERR;
        }
        ret->r = out;
        return NJ_OK;
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(si_njHeight)
{
    const nj_font *f = nj_midp_font_for(vm, NULL);
    NJ_UNUSED(args);
    ret->i = (f ? f->height : 11) + 2;
    return NJ_OK;
}

NJFN(spacer_init)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 minw = NJ_ARG_INT(args, 0);
    nj_s4 minh = NJ_ARG_INT(args, 1);

    (void)item_init(vm, th, args, ret);
    if (minw < 0 || minh < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative spacer size");
    }
    fput(vm, self, NJ_C_SPACER, "spW", D_INT, minw);
    fput(vm, self, NJ_C_SPACER, "spH", D_INT, minh);
    fput(vm, self, NJ_C_ITEM, "itPrefW", D_INT, minw);
    fput(vm, self, NJ_C_ITEM, "itPrefH", D_INT, minh);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(spacer_setMinimumSize)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 w = NJ_ARG_INT(args, 0);
    nj_s4 h = NJ_ARG_INT(args, 1);

    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative spacer size");
    }
    fput(vm, self, NJ_C_SPACER, "spW", D_INT, w);
    fput(vm, self, NJ_C_SPACER, "spH", D_INT, h);
    fput(vm, self, NJ_C_ITEM, "itPrefW", D_INT, w);
    fput(vm, self, NJ_C_ITEM, "itPrefH", D_INT, h);
    return NJ_OK;
}

NJFN(spacer_njHeight)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 h = fget(vm, self, NJ_C_SPACER, "spH", D_INT);
    ret->i = h;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* ImageItem                                                           */
/* ------------------------------------------------------------------ */
/* ImageItem(String label, Image img, int layout, String altText) */
NJFN(ii_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref label = NJ_ARG_REF(args, 0);
    nj_ref img = NJ_ARG_REF(args, 1);
    nj_s4 layout = NJ_ARG_INT(args, 2);
    nj_ref alt = NJ_ARG_REF(args, 3);

    (void)item_init(vm, th, args, ret);
    if (!alt) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "altText is null");
    }
    rput(vm, self, NJ_C_ITEM, "itLabel", D_STR, label);
    rput(vm, self, NJ_C_IMAGEITEM, "iiImage", D_IMAGE, img);
    rput(vm, self, NJ_C_IMAGEITEM, "iiAlt", D_STR, alt);
    fput(vm, self, NJ_C_IMAGEITEM, "iiLayout", D_INT, layout);
    rput(vm, self, NJ_C_IMAGEITEM, "iiAlt2", D_STR, NULL);
    /* -1: no explicit appearance mode, so getAppearanceMode() derives it from
     * the layout directives the way it always has. */
    fput(vm, self, NJ_C_IMAGEITEM, "iiAppearance", D_INT, -1);
    rput(vm, self, NJ_C_ITEM, "itImage", D_IMAGE, img);
    return vm->exception ? NJ_ERR : NJ_OK;
}

/*
 * ImageItem(String label, Image img, int layout, String altText,
 *           int appearanceMode)
 *   args: [0] label, [1] img, [2] layout, [3] altText, [4] appearanceMode
 * PLAIN / HYPERLINK / BUTTON (= 0 / 1 / 2, the values StringItem declares).
 */
NJFN(ii_init_app)
{
    nj_ref self = NJ_THIS(args);
    nj_ref label = NJ_ARG_REF(args, 0);
    nj_ref img = NJ_ARG_REF(args, 1);
    nj_s4 layout = NJ_ARG_INT(args, 2);
    nj_ref alt = NJ_ARG_REF(args, 3);
    nj_s4 mode = NJ_ARG_INT(args, 4);

    (void)item_init(vm, th, args, ret);
    if (!alt) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "altText is null");
    }
    if (mode < 0 || mode > 2) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown appearance mode");
    }
    rput(vm, self, NJ_C_ITEM, "itLabel", D_STR, label);
    rput(vm, self, NJ_C_IMAGEITEM, "iiImage", D_IMAGE, img);
    rput(vm, self, NJ_C_IMAGEITEM, "iiAlt", D_STR, alt);
    fput(vm, self, NJ_C_IMAGEITEM, "iiLayout", D_INT, layout);
    rput(vm, self, NJ_C_IMAGEITEM, "iiAlt2", D_STR, NULL);
    fput(vm, self, NJ_C_IMAGEITEM, "iiAppearance", D_INT, mode);
    rput(vm, self, NJ_C_ITEM, "itImage", D_IMAGE, img);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(ii_getImage)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_IMAGEITEM, "iiImage", D_IMAGE);
    return NJ_OK;
}

NJFN(ii_setImage)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    rput(vm, self, NJ_C_IMAGEITEM, "iiImage", D_IMAGE, img);
    rput(vm, self, NJ_C_ITEM, "itImage", D_IMAGE, img);
    return NJ_OK;
}

NJFN(ii_getAltText)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_IMAGEITEM, "iiAlt", D_STR);
    return NJ_OK;
}

NJFN(ii_setAltText)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "altText is null");
    }
    rput(vm, self, NJ_C_IMAGEITEM, "iiAlt", D_STR, s);
    return NJ_OK;
}

NJFN(ii_setLayout)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 l = NJ_ARG_INT(args, 0);
    if (l == -1 || (l & ~(nj_s4)0xFF) != 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown layout directives");
    }
    fput(vm, self, NJ_C_IMAGEITEM, "iiLayout", D_INT, l);
    fput(vm, self, NJ_C_ITEM, "itLayout", D_INT, l);
    return NJ_OK;
}

NJFN(ii_getAppearanceMode)
{
    nj_s4 a = fget(vm, NJ_THIS(args), NJ_C_IMAGEITEM, "iiAppearance", D_INT);

    if (a >= 0) {
        ret->i = a;
        return NJ_OK;
    }
    /* Built through the four-argument constructor: the appearance follows the
     * layout directives, which is what this method has always answered. */
    a = fget(vm, NJ_THIS(args), NJ_C_IMAGEITEM, "iiLayout", D_INT);
    ret->i = (a == 1) ? 1 : ((a == 2) ? 2 : 0);
    return NJ_OK;
}

NJFN(ii_njText)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_IMAGEITEM, "iiAlt", D_STR);
    return NJ_OK;
}

NJFN(ii_njHeight)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = rget(vm, self, NJ_C_IMAGEITEM, "iiImage", D_IMAGE);
    int h = 0;
    const nj_font *f = nj_midp_font_for(vm, NULL);

    if (img) {
        (void)img_dims(vm, img, NULL, &h);
    }
    if (h <= 0) {
        h = f ? f->height + 2 : UI_ITEM_H;
    }
    ret->i = h;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Gauge                                                               */
/* ------------------------------------------------------------------ */
NJFN(gauge_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref label = NJ_ARG_REF(args, 0);
    nj_s4 interactive = NJ_ARG_INT(args, 1);
    nj_s4 max = NJ_ARG_INT(args, 2);
    nj_s4 initial = NJ_ARG_INT(args, 3);

    (void)item_init(vm, th, args, ret);
    if (max <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "maxValue must be positive");
    }
    if (initial < 0 || initial > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "initial value out of range");
    }
    rput(vm, self, NJ_C_GAUGE, "gaLabel", D_STR, label);
    fput(vm, self, NJ_C_GAUGE, "gaInteractive", D_BOOL, interactive ? 1 : 0);
    fput(vm, self, NJ_C_GAUGE, "gaMax", D_INT, max);
    fput(vm, self, NJ_C_GAUGE, "gaValue", D_INT, interactive ? 0 : initial);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(gauge_setValue)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 v = NJ_ARG_INT(args, 0);

    if (!fget(vm, self, NJ_C_GAUGE, "gaInteractive", D_BOOL)) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "gauge is not interactive");
    }
    if (v < 0 || v > fget(vm, self, NJ_C_GAUGE, "gaMax", D_INT)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "value out of range");
    }
    fput(vm, self, NJ_C_GAUGE, "gaValue", D_INT, v);
    item_state_changed(vm, self);
    return NJ_OK;
}

NJFN(gauge_getValue)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_GAUGE, "gaValue", D_INT);
    return NJ_OK;
}

NJFN(gauge_setMaxValue)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 max = NJ_ARG_INT(args, 0);

    if (max <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "maxValue must be positive");
    }
    if (!fget(vm, self, NJ_C_GAUGE, "gaInteractive", D_BOOL)) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "gauge is not interactive");
    }
    fput(vm, self, NJ_C_GAUGE, "gaMax", D_INT, max);
    if (fget(vm, self, NJ_C_GAUGE, "gaValue", D_INT) > max) {
        fput(vm, self, NJ_C_GAUGE, "gaValue", D_INT, max);
    }
    item_state_changed(vm, self);
    return NJ_OK;
}

NJFN(gauge_getMaxValue)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_GAUGE, "gaMax", D_INT);
    return NJ_OK;
}

NJFN(gauge_isInteractive)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_GAUGE, "gaInteractive", D_BOOL) ? 1 : 0;
    return NJ_OK;
}

NJFN(gauge_njText)
{
    nj_ref self = NJ_THIS(args);
    nj_ref label = rget(vm, self, NJ_C_GAUGE, "gaLabel", D_STR);
    nj_s4 v = fget(vm, self, NJ_C_GAUGE, "gaValue", D_INT);
    nj_s4 max = fget(vm, self, NJ_C_GAUGE, "gaMax", D_INT);
    char buf[48];
    int n = 0;
    if (label && nj_string_len(label) == 0u) {
        label = NULL;
    }
    if (label) {
        int i;
        nj_u4 ln = nj_string_len(label);
        for (i = 0; (nj_u4)i < ln && n < 30; i++) {
            buf[n++] = (char)(nj_string_chars(label)[i] & 0x7F);
        }
        buf[n++] = ' ';
    }
    buf[n++] = '[';
    {
        nj_s4 i;
        int cells = 10;
        nj_s4 filled = (max > 0) ? (v * cells / max) : 0;
        for (i = 0; i < cells && n < 44; i++) {
            buf[n++] = (i < filled) ? '#' : '-';
        }
    }
    buf[n++] = ']';
    buf[n] = '\0';
    ret->r = nj_string_new_n(vm, buf, n);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(gauge_njHeight)
{
    const nj_font *f = nj_midp_font_for(vm, NULL);
    NJ_UNUSED(args);
    ret->i = (f ? f->height : 11) + 2;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* TextField                                                           */
/* ------------------------------------------------------------------ */
/*
 * The MIDP constraint *values* are not in the order the names are usually
 * listed: ANY=0, EMAILADDR=1, NUMERIC=2, PHONENUMBER=3, URL=4, DECIMAL=5.
 * Getting this order wrong silently swaps URL and PHONENUMBER, which rejects
 * perfectly ordinary text.
 */
static int tf_is_valid_char(nj_s4 constraints, nj_u2 ch)
{
    nj_s4 kind = constraints & 0xFFFF;
    int ascii_printable = (ch >= 0x20u && ch < 0x7Fu);

    switch ((int)kind) {
    case 1:   /* EMAILADDR */
    case 4:   /* URL */
        return ascii_printable && ch != ' ';
    case 2:   /* NUMERIC */
        return (ch >= '0' && ch <= '9');
    case 3:   /* PHONENUMBER */
        return (ch >= '0' && ch <= '9') || ch == '+' || ch == '*' ||
               ch == '#' || ch == 'p' || ch == 'w' || ch == ' ';
    case 5:   /* DECIMAL */
        return (ch >= '0' && ch <= '9') || ch == '.' || ch == '-';
    default:  /* ANY and the flag bits */
        return 1;
    }
}

static int tf_allowed(nj_s4 constraints, const nj_u2 *s, int n)
{
    int i, dots = 0;
    int kind = constraints & 0xFFFF;

    for (i = 0; i < n; i++) {
        if (!tf_is_valid_char(constraints, s[i])) { return 0; }
        if ((kind == 2 || kind == 5) && s[i] == '-' && i != 0) { return 0; }
        if (kind == 5 && s[i] == '.' && ++dots > 1) { return 0; }
    }
    return 1;
}

/* Height in pixels for `n` characters at `width` pixels per line. */
static int text_height_for(nj_vm *vm, nj_ref str, int width, const nj_font *f)
{
    int lh = f ? f->height + 2 : UI_ITEM_H;
    NJ_UNUSED(vm);
    if (!str || width <= 0) {
        return lh;
    }
    {
        int lines = 1;
        int w = 0;
        nj_u4 i;
        for (i = 0u; i < nj_string_len(str); i++) {
            int cw = nj_font_char_width(f, nj_string_chars(str)[i]);
            if (w + cw > width) {
                lines++;
                w = 0;
            }
            w += cw;
        }
        return lines * lh;
    }
}

/*
 * MIDP 2.0 documents the initial contents of both TextBox and TextField as
 * "the text content, or null if the text is empty".  三国杀2．2美化版_1347
 * builds its user-creation TextBox with a null text and used to die with
 * NullPointerException in lk.<init> before the first frame was painted.
 */
static nj_ref text_or_empty(nj_vm *vm, nj_ref text)
{
    if (text) {
        return text;
    }
    return nj_string_new_n(vm, "", 0);
}

NJFN(tf_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref label = NJ_ARG_REF(args, 0);
    nj_ref text = NJ_ARG_REF(args, 1);
    nj_s4 max = NJ_ARG_INT(args, 2);
    nj_s4 constraints = NJ_ARG_INT(args, 3);
    nj_ref chars;

    (void)item_init(vm, th, args, ret);
    text = text_or_empty(vm, text);
    if (!text) {
        return nj_throw_oom(vm);
    }
    if (max <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "maxSize must be positive");
    }
    if ((constraints & 0xFFFF) > 5) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown constraints");
    }
    if (nj_string_len(text) > (nj_u4)max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "initial text longer than maxSize");
    }
    if (!tf_allowed(constraints, nj_string_chars(text),
                    (int)nj_string_len(text))) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "initial text violates the constraints");
    }
    chars = nj_char_array_new(vm, (nj_u4)max);
    if (!chars) {
        return nj_throw_oom(vm);
    }
    if (nj_string_len(text) > 0u) {
        memcpy(chars->u.bytes, nj_string_chars(text),
               (nj_size)nj_string_len(text) * sizeof(nj_u2));
    }
    rput(vm, self, NJ_C_TEXTFIELD, "tfLabel", D_STR, label);
    rput(vm, self, NJ_C_TEXTFIELD, "tfText", D_STR, text);
    rput(vm, self, NJ_C_TEXTFIELD, "tfChars", D_CHARR, chars);
    fput(vm, self, NJ_C_TEXTFIELD, "tfMax", D_INT, max);
    fput(vm, self, NJ_C_TEXTFIELD, "tfConstraints", D_INT, constraints);
    fput(vm, self, NJ_C_TEXTFIELD, "tfCursor", D_INT,
         (nj_s4)nj_string_len(text));
    fput(vm, self, NJ_C_TEXTFIELD, "tfInputMode", D_INT, -1);
    rput(vm, self, NJ_C_TEXTFIELD, "tfInputModeName", D_STR, NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

/* Rebuild tfText/tfChars after any edit. */
static nj_status tf_sync(nj_vm *vm, nj_ref self, const nj_u2 *s, int n)
{
    nj_ref chars = rget(vm, self, NJ_C_TEXTFIELD, "tfChars", D_CHARR);
    nj_ref str;

    if (!chars || (nj_u4)n > chars->length) {
        return NJ_OK;
    }
    if (n > 0) {
        memcpy(chars->u.bytes, s, (nj_size)n * sizeof(nj_u2));
    }
    if ((nj_u4)n < chars->length) {
        memset((nj_u2 *)chars->u.bytes + n, 0,
               (nj_size)(chars->length - (nj_u4)n) * sizeof(nj_u2));
    }
    str = nj_new_string_utf16(vm, s, n);
    if (!str) {
        return nj_throw_oom(vm);
    }
    rput(vm, self, NJ_C_TEXTFIELD, "tfText", D_STR, str);
    return NJ_OK;
}

NJFN(tf_getString)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_TEXTFIELD, "tfText", D_STR);
    return NJ_OK;
}

NJFN(tf_setString)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_s4 max = fget(vm, self, NJ_C_TEXTFIELD, "tfMax", D_INT);

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    if ((nj_s4)nj_string_len(s) > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text longer than maxSize");
    }
    if (!tf_allowed(fget(vm, self, NJ_C_TEXTFIELD, "tfConstraints", D_INT),
                    nj_string_chars(s), (int)nj_string_len(s))) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text violates the constraints");
    }
    if (tf_sync(vm, self, nj_string_chars(s), (int)nj_string_len(s)) != NJ_OK) {
        return NJ_ERR;
    }
    fput(vm, self, NJ_C_TEXTFIELD, "tfCursor", D_INT,
         (nj_s4)nj_string_len(s));
    item_state_changed(vm, self);
    return NJ_OK;
}

/* getChars(char[]) returns the number of characters copied (MIDP). */
NJFN(tf_getChars)
{
    nj_ref self = NJ_THIS(args);
    nj_ref dst = NJ_ARG_REF(args, 0);
    nj_ref src = rget(vm, self, NJ_C_TEXTFIELD, "tfChars", D_CHARR);
    nj_ref text = rget(vm, self, NJ_C_TEXTFIELD, "tfText", D_STR);
    nj_u4 n = 0u;

    if (!dst) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (src && text) {
        n = nj_string_len(text);
        if (n > dst->length) {
            n = dst->length;
        }
        if (n > 0u) {
            memcpy(dst->u.bytes, src->u.bytes, (nj_size)n * sizeof(nj_u2));
        }
    }
    ret->i = (nj_s4)n;
    return NJ_OK;
}

NJFN(tf_setChars)
{
    nj_ref self = NJ_THIS(args);
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 max = fget(vm, self, NJ_C_TEXTFIELD, "tfMax", D_INT);

    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > src->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (len > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text longer than maxSize");
    }
    if (!tf_allowed(fget(vm, self, NJ_C_TEXTFIELD, "tfConstraints", D_INT),
                    (const nj_u2 *)src->u.bytes + off, (int)len)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text violates the constraints");
    }
    if (tf_sync(vm, self, (const nj_u2 *)src->u.bytes + off, (int)len) != NJ_OK) {
        return NJ_ERR;
    }
    fput(vm, self, NJ_C_TEXTFIELD, "tfCursor", D_INT, len);
    item_state_changed(vm, self);
    return NJ_OK;
}

NJFN(tf_size)
{
    nj_ref s = rget(vm, NJ_THIS(args), NJ_C_TEXTFIELD, "tfText", D_STR);
    ret->i = s ? (nj_s4)nj_string_len(s) : 0;
    return NJ_OK;
}

NJFN(tf_getMaxSize)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_TEXTFIELD, "tfMax", D_INT);
    return NJ_OK;
}

NJFN(tf_setMaxSize)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 max = NJ_ARG_INT(args, 0);
    nj_ref chars;
    nj_ref text = rget(vm, self, NJ_C_TEXTFIELD, "tfText", D_STR);

    if (max <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "maxSize must be positive");
    }
    if (text && (nj_s4)nj_string_len(text) > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text longer than the new maxSize");
    }
    chars = nj_char_array_new(vm, (nj_u4)max);
    if (!chars) {
        return nj_throw_oom(vm);
    }
    if (text && nj_string_len(text) > 0u) {
        memcpy(chars->u.bytes, nj_string_chars(text),
               (nj_size)nj_string_len(text) * sizeof(nj_u2));
    }
    rput(vm, self, NJ_C_TEXTFIELD, "tfChars", D_CHARR, chars);
    fput(vm, self, NJ_C_TEXTFIELD, "tfMax", D_INT, max);
    return NJ_OK;
}

NJFN(tf_getConstraints)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_TEXTFIELD, "tfConstraints", D_INT);
    return NJ_OK;
}

NJFN(tf_setConstraints)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 c = NJ_ARG_INT(args, 0);
    nj_ref text = rget(vm, self, NJ_C_TEXTFIELD, "tfText", D_STR);

    if ((c & 0xFFFF) > 5) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown constraints");
    }
    if (text && !tf_allowed(c, nj_string_chars(text),
                            (int)nj_string_len(text))) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "current text violates the new constraints");
    }
    fput(vm, self, NJ_C_TEXTFIELD, "tfConstraints", D_INT, c);
    return NJ_OK;
}

NJFN(tf_setInitialInputMode)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "input mode is null");
    }
    rput(vm, self, NJ_C_TEXTFIELD, "tfInputModeName", D_STR, s);
    return NJ_OK;
}

NJFN(tf_getInitialInputMode)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_TEXTFIELD, "tfInputModeName", D_STR);
    return NJ_OK;
}

static nj_status tf_insert_on(nj_vm *vm, nj_ref self, nj_value *args)
{
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 at = NJ_ARG_INT(args, 3);
    nj_ref text = rget(vm, self, NJ_C_TEXTFIELD, "tfText", D_STR);
    const nj_u2 *srcp = (const nj_u2 *)src->u.bytes;
    nj_s4 cur = text ? (nj_s4)nj_string_len(text) : 0;
    nj_s4 max = fget(vm, self, NJ_C_TEXTFIELD, "tfMax", D_INT);
    nj_u2 *buf;

    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (src->kind == NJ_OBJ_STRING) {
        srcp = nj_string_chars(src);
        if (off < 0 || len < 0 || (nj_u4)(off + len) > nj_string_len(src)) {
            return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
        }
    } else if (off < 0 || len < 0 || (nj_u4)(off + len) > src->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (at < 0 || at > cur) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (cur + len > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text longer than maxSize");
    }
    if (!tf_allowed(fget(vm, self, NJ_C_TEXTFIELD, "tfConstraints", D_INT),
                    srcp + off, (int)len)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text violates the constraints");
    }
    buf = (nj_u2 *)nj_host_alloc(vm, ((nj_size)cur + (nj_size)len + 1u) *
                                     sizeof(nj_u2));
    if (!buf) {
        return nj_throw_oom(vm);
    }
    if (cur > 0) {
        memcpy(buf, nj_string_chars(text), (nj_size)at * sizeof(nj_u2));
    }
    memcpy(buf + at, srcp + off, (nj_size)len * sizeof(nj_u2));
    if (cur > at) {
        memcpy(buf + at + len, nj_string_chars(text) + at,
               (nj_size)(cur - at) * sizeof(nj_u2));
    }
    {
        nj_status st = tf_sync(vm, self, buf, (int)(cur + len));
        nj_host_free(vm, buf);
        if (st != NJ_OK) {
            return st;
        }
    }
    fput(vm, self, NJ_C_TEXTFIELD, "tfCursor", D_INT, at + len);
    return NJ_OK;
}

NJFN(tf_insert)
{
    return tf_insert_on(vm, NJ_THIS(args), args);
}

/* insert(String src, int position) - the MIDP 2.0 String overload. */
NJFN(tf_insert_str)
{
    nj_ref str = NJ_ARG_REF(args, 0);
    nj_s4 at = NJ_ARG_INT(args, 1);
    nj_value a[4];

    if (!str) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    a[0].r = str;
    a[1].i = 0;
    a[2].i = (nj_s4)nj_string_len(str);
    a[3].i = at;
    return tf_insert_on(vm, NJ_THIS(args), a);
}

NJFN(tf_delete)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 off = NJ_ARG_INT(args, 0);
    nj_s4 len = NJ_ARG_INT(args, 1);
    nj_ref text = rget(vm, self, NJ_C_TEXTFIELD, "tfText", D_STR);
    nj_s4 cur = text ? (nj_s4)nj_string_len(text) : 0;
    nj_u2 *buf;

    if (off < 0 || len < 0 || off + len > cur) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        return NJ_OK;
    }
    buf = (nj_u2 *)nj_host_alloc(vm, ((nj_size)cur + 1u) * sizeof(nj_u2));
    if (!buf) {
        return nj_throw_oom(vm);
    }
    if (off > 0) {
        memcpy(buf, nj_string_chars(text), (nj_size)off * sizeof(nj_u2));
    }
    if (cur > off + len) {
        memcpy(buf + off, nj_string_chars(text) + off + len,
               (nj_size)(cur - off - len) * sizeof(nj_u2));
    }
    {
        nj_status st = tf_sync(vm, self, buf, (int)(cur - len));
        nj_host_free(vm, buf);
        if (st != NJ_OK) {
            return st;
        }
    }
    if (fget(vm, self, NJ_C_TEXTFIELD, "tfCursor", D_INT) > off) {
        fput(vm, self, NJ_C_TEXTFIELD, "tfCursor", D_INT, off);
    }
    return NJ_OK;
}

NJFN(tf_njText)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_TEXTFIELD, "tfText", D_STR);
    return NJ_OK;
}

/* TextField.getCaretPosition(): the same insertion point, from tfCursor. */
NJFN(tf_getCaretPosition)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_TEXTFIELD, "tfCursor", D_INT);
    return NJ_OK;
}

NJFN(tf_njHeight)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 w = NJ_ARG_INT(args, 0);
    nj_ref text = rget(vm, self, NJ_C_TEXTFIELD, "tfText", D_STR);
    nj_s4 pref = fget(vm, self, NJ_C_ITEM, "itPrefH", D_INT);
    const nj_font *f = nj_midp_font_for(vm, NULL);

    if (pref > 0) {
        ret->i = pref;
        return NJ_OK;
    }
    ret->i = text_height_for(vm, text, (int)w, f);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* ChoiceGroup                                                         */
/* ------------------------------------------------------------------ */
/*
 * A ChoiceGroup keeps three parallel arrays (text, image, selected flag) plus
 * the element count and the cursor.  The contract of nj_midp.h says nothing
 * about them, so the names are local to this file - but they are ordinary Java
 * fields, so the GC keeps them alive and there is no side table.
 */
static nj_status cg_append_common(nj_vm *vm, nj_ref self, nj_ref str,
                                  nj_ref img);

/* ChoiceGroup(String label, int choiceType) */
static nj_status cg_init_common(nj_vm *vm, nj_ref self, nj_value *args,
                                nj_s4 type)
{
    nj_ref label = NJ_ARG_REF(args, 0);

    item_init_on(vm, self);
    if (type < NJ_CHOICE_EXCLUSIVE || type > NJ_CHOICE_IMPLICIT) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "choice type must be EXCLUSIVE, MULTIPLE or IMPLICIT");
    }
    rput(vm, self, NJ_C_ITEM, "itLabel", D_STR, label);
    fput(vm, self, NJ_C_CHOICEGROUP, "cgType", D_INT, type);
    rput(vm, self, NJ_C_CHOICEGROUP, "cgLabel", D_STR, label);
    rput(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR, NULL);
    rput(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR, NULL);
    rput(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z", NULL);
    rput(vm, self, NJ_C_CHOICEGROUP, "cgFont", D_FONT, NULL);
    fput(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT, 0);
    fput(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT, -1);
    fput(vm, self, NJ_C_CHOICEGROUP, "cgFit", D_INT, 0);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(cg_init)
{
    return cg_init_common(vm, NJ_THIS(args), args, NJ_ARG_INT(args, 1));
}

/* ChoiceGroup(String label, int type, String[] strings, Image[] images) */
NJFN(cg_init_arrays)
{
    nj_ref self = NJ_THIS(args);
    nj_ref strs = NJ_ARG_REF(args, 2);
    nj_ref imgs = NJ_ARG_REF(args, 3);
    nj_u4 i;

    if (cg_init_common(vm, self, args, NJ_ARG_INT(args, 1)) != NJ_OK) {
        return NJ_ERR;
    }
    if (!strs || !imgs) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "strings and images must not be null");
    }
    if (strs->length != imgs->length) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "strings and images differ in length");
    }
    for (i = 0u; i < strs->length; i++) {
        nj_ref str = nj_array_get_ref(strs, i);
        if (!str) {
            return nj_throw(vm, "java/lang/NullPointerException",
                            "element is null");
        }
        if (cg_append_common(vm, self, str, nj_array_get_ref(imgs, i)) != NJ_OK) {
            return NJ_ERR;
        }
    }
    return NJ_OK;
}

/* Grow the three parallel arrays so that `need` elements fit. */
static nj_status cg_ensure(nj_vm *vm, nj_ref self, int need)
{
    nj_ref strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    nj_ref imgs = rget(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR);
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_u4 cur = strs ? strs->length : 0u;

    if ((nj_u4)need <= cur && strs && imgs && sels) {
        return NJ_OK;
    }
    {
        nj_u4 cap = cur ? cur : 4u;
        nj_ref ns, ni, nf;
        nj_u4 i;
        while (cap < (nj_u4)need) {
            cap *= 2u;
        }
        ns = nj_object_array_new(vm, "java/lang/String", cap);
        ni = nj_object_array_new(vm, NJ_C_IMAGE, cap);
        nf = nj_new_array(vm, NULL, cap, NJ_ARRAY_T_BOOLEAN);
        if (!ns || !ni || !nf) {
            return nj_throw_oom(vm);
        }
        for (i = 0u; i < cur; i++) {
            if (strs) { nj_array_set_ref(ns, i, nj_array_get_ref(strs, i)); }
            if (imgs) { nj_array_set_ref(ni, i, nj_array_get_ref(imgs, i)); }
            if (sels) { nf->u.bytes[i] = sels->u.bytes[i]; }
        }
        rput(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR, ns);
        rput(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR, ni);
        rput(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z", nf);
    }
    return NJ_OK;
}

/* Move every element from `from` to the end one slot right (or left). */
static void cg_shift(nj_vm *vm, nj_ref self, int from, int to)
{
    nj_ref strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    nj_ref imgs = rget(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR);
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);
    int i;

    if (!strs || !imgs || !sels || from == to) {
        return;
    }
    if (to > from) {
        for (i = n - 1; i >= from; i--) {
            nj_array_set_ref(strs, (nj_u4)(i + 1), nj_array_get_ref(strs, (nj_u4)i));
            nj_array_set_ref(imgs, (nj_u4)(i + 1), nj_array_get_ref(imgs, (nj_u4)i));
            sels->u.bytes[i + 1] = sels->u.bytes[i];
        }
    } else {
        for (i = from; i < n; i++) {
            nj_array_set_ref(strs, (nj_u4)(i - 1), nj_array_get_ref(strs, (nj_u4)i));
            nj_array_set_ref(imgs, (nj_u4)(i - 1), nj_array_get_ref(imgs, (nj_u4)i));
            sels->u.bytes[i - 1] = sels->u.bytes[i];
        }
        sels->u.bytes[n - 1] = 0;
    }
}

static nj_status cg_append_common(nj_vm *vm, nj_ref self, nj_ref str,
                                  nj_ref img)
{
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);
    nj_ref strs;
    nj_ref imgs;

    if (!str) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    if (img && !fget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z")) {
        /* nothing: ensure() allocates below */
    }
    if (img && fget(vm, self, NJ_C_CHOICEGROUP, "cgType", D_INT) == 3) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "IMPLICIT ChoiceGroup cannot hold images");
    }
    if (cg_ensure(vm, self, (int)n + 1) != NJ_OK) {
        return NJ_ERR;
    }
    strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    imgs = rget(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR);
    nj_array_set_ref(strs, (nj_u4)n, str);
    nj_array_set_ref(imgs, (nj_u4)n, img);
    fput(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT, n + 1);
    if (fget(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT) < 0) {
        fput(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT, 0);
    }
    /* MIDP: in an EXCLUSIVE or IMPLICIT group the first element is selected. */
    if (n == 0 && fget(vm, self, NJ_C_CHOICEGROUP, "cgType", D_INT) !=
                  NJ_CHOICE_MULTIPLE) {
        nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
        if (sels && sels->length > 0u) {
            sels->u.bytes[0] = 1;
        }
        fput(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT, 0);
    }
    return NJ_OK;
}

static nj_status cg_append_result(nj_vm *vm, nj_ref self, nj_ref str,
                                 nj_ref img, nj_value *ret)
{
    nj_status st = cg_append_common(vm, self, str, img);
    if (st != NJ_OK) {
        return st;
    }
    ret->i = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT) - 1;
    return NJ_OK;
}

NJFN(cg_append)
{
    return cg_append_result(vm, NJ_THIS(args), NJ_ARG_REF(args, 0), NULL, ret);
}

/* append(String, Image) exists with an int result (MIDP) and a void result
 * (the compile stubs); both are declared and bound to this native. */
NJFN(cg_append_img)
{
    return cg_append_result(vm, NJ_THIS(args), NJ_ARG_REF(args, 0),
                            NJ_ARG_REF(args, 1), ret);
}

static nj_status cg_insert_common(nj_vm *vm, nj_ref self, nj_s4 at, nj_ref str,
                                  nj_ref img)
{
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);
    nj_ref strs;
    nj_ref imgs;

    if (!str) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    if (at < 0 || at > n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    if (img && fget(vm, self, NJ_C_CHOICEGROUP, "cgType", D_INT) == 3) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "IMPLICIT ChoiceGroup cannot hold images");
    }
    if (cg_ensure(vm, self, (int)n + 1) != NJ_OK) {
        return NJ_ERR;
    }
    cg_shift(vm, self, (int)at, (int)at + 1);
    strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    imgs = rget(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR);
    nj_array_set_ref(strs, (nj_u4)at, str);
    nj_array_set_ref(imgs, (nj_u4)at, img);
    fput(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT, n + 1);
    if (fget(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT) < 0) {
        fput(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT, 0);
    }
    return NJ_OK;
}

NJFN(cg_insert)
{
    return cg_insert_common(vm, NJ_THIS(args), NJ_ARG_INT(args, 0),
                            NJ_ARG_REF(args, 1), NULL);
}

NJFN(cg_insert_img)
{
    return cg_insert_common(vm, NJ_THIS(args), NJ_ARG_INT(args, 0),
                            NJ_ARG_REF(args, 1), NJ_ARG_REF(args, 2));
}

/* set(int index, String text, Image image) */
NJFN(cg_set)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref str = NJ_ARG_REF(args, 1);
    nj_ref img = NJ_ARG_REF(args, 2);
    nj_ref strs;
    nj_ref imgs;
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (!str) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    if (at < 0 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    if (img && fget(vm, self, NJ_C_CHOICEGROUP, "cgType", D_INT) ==
               NJ_CHOICE_IMPLICIT) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "IMPLICIT ChoiceGroup cannot hold images");
    }
    strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    imgs = rget(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR);
    if (!strs || !imgs || (nj_u4)at >= strs->length) {
        return NJ_OK;
    }
    nj_array_set_ref(strs, (nj_u4)at, str);
    nj_array_set_ref(imgs, (nj_u4)at, img);
    return NJ_OK;
}

NJFN(cg_delete)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (at < 0 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    cg_shift(vm, self, (int)at + 1, (int)at);
    fput(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT, n - 1);
    if (fget(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT) >= n - 1) {
        fput(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT, n - 2);
    }
    return NJ_OK;
}

NJFN(cg_deleteAll)
{
    nj_ref self = NJ_THIS(args);
    nj_ref strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    nj_ref imgs = rget(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR);
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_u4 i;

    if (strs) {
        for (i = 0u; i < strs->length; i++) {
            nj_array_set_ref(strs, i, NULL);
        }
    }
    if (imgs) {
        for (i = 0u; i < imgs->length; i++) {
            nj_array_set_ref(imgs, i, NULL);
        }
    }
    if (sels) {
        memset(sels->u.bytes, 0, sels->length);
    }
    fput(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT, 0);
    fput(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT, -1);
    return NJ_OK;
}

NJFN(cg_size)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_CHOICEGROUP, "cgCount", D_INT);
    return NJ_OK;
}

NJFN(cg_getString)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (at < 0 || at >= n || !strs || (nj_u4)at >= strs->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->r = nj_array_get_ref(strs, (nj_u4)at);
    return NJ_OK;
}

NJFN(cg_setString)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref s = NJ_ARG_REF(args, 1);
    nj_ref strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    if (at < 0 || at >= n || !strs || (nj_u4)at >= strs->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    nj_array_set_ref(strs, (nj_u4)at, s);
    return NJ_OK;
}

NJFN(cg_getImage)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref imgs = rget(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR);
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (at < 0 || at >= n || !imgs || (nj_u4)at >= imgs->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->r = nj_array_get_ref(imgs, (nj_u4)at);
    return NJ_OK;
}

NJFN(cg_setImage)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref img = NJ_ARG_REF(args, 1);
    nj_ref imgs = rget(vm, self, NJ_C_CHOICEGROUP, "cgImages", D_IMGARR);
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (at < 0 || at >= n || !imgs || (nj_u4)at >= imgs->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    nj_array_set_ref(imgs, (nj_u4)at, img);
    return NJ_OK;
}

NJFN(cg_isSelected)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (at < 0 || at >= n || !sels || (nj_u4)at >= sels->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->i = sels->u.bytes[at] ? 1 : 0;
    return NJ_OK;
}

static nj_status cg_set_selected(nj_vm *vm, nj_ref self, nj_s4 at, int on)
{
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);
    nj_s4 type;

    if (at < 0 || at >= n || !sels || (nj_u4)at >= sels->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    type = fget(vm, self, NJ_C_CHOICEGROUP, "cgType", D_INT);
    if (type == NJ_CHOICE_EXCLUSIVE) {
        nj_u4 i;
        for (i = 0u; i < sels->length; i++) {
            sels->u.bytes[i] = 0;
        }
        sels->u.bytes[at] = 1;
    } else if (type == NJ_CHOICE_IMPLICIT && !on) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "cannot deselect an IMPLICIT element");
    } else {
        sels->u.bytes[at] = on ? 1 : 0;
    }
    fput(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT, at);
    return NJ_OK;
}

NJFN(cg_setSelectedIndex)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_status st = cg_set_selected(vm, self, at, 1);
    if (st != NJ_OK) {
        return st;
    }
    item_state_changed(vm, self);
    return NJ_OK;
}

/* setSelectedIndex(int index, boolean selected) */
NJFN(cg_setSelectedIndex2)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_s4 on = NJ_ARG_INT(args, 1) ? 1 : 0;
    nj_status st = cg_set_selected(vm, self, at, on);
    if (st != NJ_OK) {
        return st;
    }
    item_state_changed(vm, self);
    return NJ_OK;
}

NJFN(cg_setSelectedFlags)
{
    nj_ref self = NJ_THIS(args);
    nj_ref flags = NJ_ARG_REF(args, 0);
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);
    nj_s4 type = fget(vm, self, NJ_C_CHOICEGROUP, "cgType", D_INT);
    nj_u4 i;
    int first = -1;

    if (!flags) {
        return nj_throw(vm, "java/lang/NullPointerException", "flags is null");
    }
    if (!sels) {
        return NJ_OK;
    }
    for (i = 0u; i < (nj_u4)n && i < sels->length; i++) {
        int on = (i < flags->length) ? (flags->u.bytes[i] ? 1 : 0) : 0;
        sels->u.bytes[i] = (nj_u1)on;
        if (on && first < 0) {
            first = (int)i;
        }
        if (on && type == NJ_CHOICE_EXCLUSIVE) {
            nj_u4 k;
            for (k = 0u; k < sels->length; k++) {
                sels->u.bytes[k] = 0;
            }
            sels->u.bytes[i] = 1;
            first = (int)i;
            break;
        }
        if (on && type == NJ_CHOICE_IMPLICIT) {
            nj_u4 k;
            for (k = 0u; k < sels->length; k++) {
                sels->u.bytes[k] = 0;
            }
            sels->u.bytes[i] = 1;
            first = (int)i;
            break;
        }
    }
    fput(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT, first);
    return NJ_OK;
}

NJFN(cg_getSelectedFlags)
{
    nj_ref self = NJ_THIS(args);
    nj_ref flags = NJ_ARG_REF(args, 0);
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);
    nj_s4 i;
    int count = 0;

    if (!flags) {
        return nj_throw(vm, "java/lang/NullPointerException", "flags is null");
    }
    for (i = 0; i < n; i++) {
        int on = 0;
        if (sels && (nj_u4)i < sels->length) {
            on = sels->u.bytes[i] ? 1 : 0;
        }
        if ((nj_u4)i < flags->length) {
            flags->u.bytes[i] = (nj_u1)on;
        }
        count += on;
    }
    ret->i = count;
    return NJ_OK;
}

NJFN(cg_getSelectedIndex)
{
    nj_ref self = NJ_THIS(args);
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);
    nj_s4 i;

    if (fget(vm, self, NJ_C_CHOICEGROUP, "cgType", D_INT) == NJ_CHOICE_EXCLUSIVE
        && sels) {
        int count = 0, last = -1;
        for (i = 0; i < n && (nj_u4)i < sels->length; i++) {
            if (sels->u.bytes[i]) { count++; last = (int)i; }
        }
        ret->i = (count == 1) ? last : -1;
        return NJ_OK;
    }
    ret->i = fget(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT);
    return NJ_OK;
}

NJFN(cg_getFitPolicy)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_CHOICEGROUP, "cgFit", D_INT);
    return NJ_OK;
}

NJFN(cg_setFitPolicy)
{
    fput(vm, NJ_THIS(args), NJ_C_CHOICEGROUP, "cgFit", D_INT,
         NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(cg_setFont)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref f = NJ_ARG_REF(args, 1);
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (!f) {
        return nj_throw(vm, "java/lang/NullPointerException", "font is null");
    }
    if (at < -1 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    rput(vm, self, NJ_C_CHOICEGROUP, "cgFont", D_FONT, f);
    return NJ_OK;
}

NJFN(cg_getFont)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);

    if (at < -1 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->r = rget(vm, self, NJ_C_CHOICEGROUP, "cgFont", D_FONT);
    return NJ_OK;
}

NJFN(cg_njText)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 n = fget(vm, self, NJ_C_CHOICEGROUP, "cgCount", D_INT);
    nj_ref strs = rget(vm, self, NJ_C_CHOICEGROUP, "cgStrings", D_STRARR);
    nj_ref sels = rget(vm, self, NJ_C_CHOICEGROUP, "cgSelected", "[Z");
    nj_s4 idx = fget(vm, self, NJ_C_CHOICEGROUP, "cgIndex", D_INT);
    nj_ref out;
    nj_u2 buf[96];
    int k = 0;
    nj_s4 i;

    if (n <= 0 || !strs) {
        ret->r = NULL;
        return NJ_OK;
    }
    for (i = 0; i < n && k < 80; i++) {
        nj_ref s;
        int mark = 0;
        if ((nj_u4)i >= strs->length) {
            break;
        }
        if (sels && (nj_u4)i < sels->length && sels->u.bytes[i]) {
            mark = 1;
        }
        if (i == idx) {
            buf[k++] = '[';
            buf[k++] = '>';
        } else {
            buf[k++] = ' ';
        }
        buf[k++] = mark ? 'x' : 'o';
        buf[k++] = ' ';
        s = nj_array_get_ref(strs, (nj_u4)i);
        if (s) {
            nj_u4 j;
            nj_u4 sn = nj_string_len(s);
            for (j = 0u; j < sn && k < 80; j++) {
                buf[k++] = nj_string_chars(s)[j];
            }
        }
        if (i != n - 1 && k < 79) {
            buf[k++] = ' ';
            buf[k++] = '|';
            buf[k++] = ' ';
        }
    }
    out = nj_new_string_utf16(vm, buf, k);
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(cg_njHeight)
{
    const nj_font *f = nj_midp_font_for(vm, NULL);
    NJ_UNUSED(args);
    ret->i = (f ? f->height : 11) + 2;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* DateField                                                           */
/* ------------------------------------------------------------------ */
/*
 * Fields of a DateField:  dfTime (J, milliseconds since the epoch),
 * dfMode (I: DATE / TIME / DATE_TIME), dfInputMode (I).
 * The textual value is produced here rather than by java.util.Calendar so that
 * the UI keeps working even when a trimmed build leaves Calendar out.
 */
static void df_format(nj_s8 t, nj_s4 mode, char *out, int cap)
{
    nj_s4 days, rem, hh, mm;
    int year = 1970, month = 0, day = 0;
    int n = 0;
    static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (cap < 24) {
        out[0] = '\0';
        return;
    }
    days = (nj_s4)(t / 86400000);
    rem = (nj_s4)(t % 86400000);
    if (rem < 0) {
        rem += 86400000;
        days -= 1;
    }
    hh = rem / 3600000;
    mm = (rem % 3600000) / 60000;
    while (days < 0) {
        year -= 1;
        days += 365 + ((year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0);
    }
    for (;;) {
        int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
        int len = 365 + leap;
        if (days < len) {
            break;
        }
        days -= len;
        year++;
    }
    for (month = 0; month < 12; month++) {
        int len = mdays[month] + ((month == 1) ? ((year % 4 == 0 &&
                  (year % 100 != 0 || year % 400 == 0)) ? 1 : 0) : 0);
        if (days < len) {
            break;
        }
        days -= len;
    }
    day = days + 1;
    /* Format YYYY-MM-DD HH:MM (or the part the mode asks for). */
    {
        char tmp[8];
        int k = 0, y = year;
        if (y == 0) { tmp[k++] = '0'; }
        while (y > 0 && k < 6) { tmp[k++] = (char)('0' + y % 10); y /= 10; }
        while (k > 0 && n < cap - 1) { out[n++] = tmp[--k]; }
    }
    if (mode != 1) {
        out[n++] = '-';
        out[n++] = (char)('0' + (month + 1) / 10);
        out[n++] = (char)('0' + (month + 1) % 10);
        out[n++] = '-';
        out[n++] = (char)('0' + day / 10);
        out[n++] = (char)('0' + day % 10);
    }
    if (mode != 0) {
        out[n++] = ' ';
        out[n++] = (char)('0' + (hh / 10) % 10);
        out[n++] = (char)('0' + hh % 10);
        out[n++] = ':';
        out[n++] = (char)('0' + (mm / 10) % 10);
        out[n++] = (char)('0' + mm % 10);
    }
    out[n] = '\0';
}

/* The stored millisecond value (<init> arguments are Date / TimeZone refs). */
static nj_s8 df_millis(nj_vm *vm, nj_ref self)
{
    nj_field *f = fld(vm, NJ_C_DATEFIELD, "dfTime", "J", 0);
    return f ? nj_field_read_long(vm, self, f) : 0;
}

static void df_set_millis(nj_vm *vm, nj_ref self, nj_s8 t)
{
    nj_field *f = fld(vm, NJ_C_DATEFIELD, "dfTime", "J", 0);
    if (f) {
        nj_field_write_long(vm, self, f, t);
    }
}

/* DateField(String label, int mode[, TimeZone zone]) */
static nj_status df_init_common(nj_vm *vm, nj_ref self, nj_value *args,
                                int with_zone)
{
    nj_s4 mode = NJ_ARG_INT(args, 1);

    item_init_on(vm, self);
    if (mode < 1 || mode > 3) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown date field mode");
    }
    rput(vm, self, NJ_C_ITEM, "itLabel", D_STR, NJ_ARG_REF(args, 0));
    rput(vm, self, NJ_C_DATEFIELD, "dfLabel", D_STR, NJ_ARG_REF(args, 0));
    /* MIDP starts a DateField at "now". */
    df_set_millis(vm, self, nj_now_ms(vm));
    fput(vm, self, NJ_C_DATEFIELD, "dfMode", D_INT, mode);
    fput(vm, self, NJ_C_DATEFIELD, "dfInputMode", D_INT, -1);
    rput(vm, self, NJ_C_DATEFIELD, "dfZone", "Ljava/util/TimeZone;",
         with_zone ? NJ_ARG_REF(args, 2) : NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(df_init)
{
    return df_init_common(vm, NJ_THIS(args), args, 0);
}

NJFN(df_init_zone)
{
    return df_init_common(vm, NJ_THIS(args), args, 1);
}

/* getDate()/setDate() speak java.util.Date, as MIDP specifies. */
NJFN(df_getDate)
{
    nj_ref self = NJ_THIS(args);
    nj_ref d = nj_object_new(vm, "java/util/Date");
    nj_value a[2];

    if (!d) {
        return new_failed(vm);
    }
    a[0].j = df_millis(vm, self);
    a[1].bits = 0u;
    if (nj_call_special(vm, d, "java/util/Date", "<init>", "(J)V", a,
                        NULL) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = d;
    return NJ_OK;
}

NJFN(df_setDate)
{
    nj_ref self = NJ_THIS(args);
    nj_ref d = NJ_ARG_REF(args, 0);
    nj_s8 t = 0;

    if (d) {
        nj_value r;
        r.bits = 0u;
        if (nj_call(vm, d, "java/util/Date", "getTime", "()J", NULL,
                    &r) != NJ_OK) {
            return NJ_ERR;
        }
        t = r.j;
    }
    df_set_millis(vm, self, t);
    item_state_changed(vm, self);
    return NJ_OK;
}

NJFN(df_getTimeZone)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_DATEFIELD, "dfZone",
                  "Ljava/util/TimeZone;");
    return NJ_OK;
}

NJFN(df_setTimeZone)
{
    nj_ref self = NJ_THIS(args);
    rput(vm, self, NJ_C_DATEFIELD, "dfZone", "Ljava/util/TimeZone;",
         NJ_ARG_REF(args, 0));
    item_state_changed(vm, self);
    return NJ_OK;
}

NJFN(df_setInputMode)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 t = NJ_ARG_INT(args, 0);
    if (t != -1) {
        switch (t) {
        case 0: case 1: case 2: case 3: case 4: case 5:
        case 6: case 7: case 8: case 9: case 10:
            break;
        default:
            return nj_throw(vm, "java/lang/IllegalArgumentException",
                            "unknown input mode");
        }
    }
    fput(vm, self, NJ_C_DATEFIELD, "dfInputMode", D_INT, t);
    return NJ_OK;
}

NJFN(df_getInputMode)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_DATEFIELD, "dfInputMode", D_INT);
    return NJ_OK;
}

NJFN(df_njText)
{
    nj_ref self = NJ_THIS(args);
    nj_s8 t = df_millis(vm, self);
    char buf[40];
    df_format(t, fget(vm, self, NJ_C_DATEFIELD, "dfMode", D_INT), buf,
              (int)sizeof(buf));
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(df_njHeight)
{
    const nj_font *f = nj_midp_font_for(vm, NULL);
    NJ_UNUSED(args);
    ret->i = (f ? f->height : 11) + 2;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Form (List derives from it)                                         */
/* ------------------------------------------------------------------ */
/*
 * A Form is an Item[] plus a count.  Item.s itOwner points back at the Form so
 * that notifyStateChanged() can reach the ItemStateListener.
 */
/* Forward: both Form and List append through the same code path. */
static nj_status form_add_item(nj_vm *vm, nj_ref form, nj_ref item, nj_s4 at);

NJFN(form_init)
{
    nj_ref self = NJ_THIS(args);

    d_init_on(vm, self);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TITLE, D_STR, NJ_ARG_REF(args, 0));
    rput(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR, NULL);
    fput(vm, self, NJ_C_FORM, "fmCount", D_INT, 0);
    fput(vm, self, NJ_C_FORM, "fmIndex", D_INT, 0);
    rput(vm, self, NJ_C_FORM, "fmItemListener", D_ISL, NULL);
    rput(vm, self, NJ_C_FORM, "fmBuffer", D_IMAGE, NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(form_init_items)
{
    nj_ref self = NJ_THIS(args);
    nj_ref items = NJ_ARG_REF(args, 1);
    nj_u4 i;

    if (form_init(vm, th, args, ret) != NJ_OK) {
        return NJ_ERR;
    }
    if (!items) {
        return nj_throw(vm, "java/lang/NullPointerException", "items is null");
    }
    for (i = 0u; i < items->length; i++) {
        nj_ref it = nj_array_get_ref(items, i);
        if (!it) {
            return nj_throw(vm, "java/lang/NullPointerException",
                            "item is null");
        }
        if (form_add_item(vm, self, it, (nj_s4)i) != NJ_OK) {
            return NJ_ERR;
        }
    }
    return NJ_OK;
}

static nj_status form_add_item(nj_vm *vm, nj_ref form, nj_ref item, nj_s4 at)
{
    nj_ref items = rget(vm, form, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, form, NJ_C_FORM, "fmCount", D_INT);
    nj_s4 i;

    if (!item) {
        return nj_throw(vm, "java/lang/NullPointerException", "item is null");
    }
    if (at < 0 || at > n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    if (!items || (nj_u4)n >= items->length) {
        nj_u4 cap = items ? items->length * 2u : 8u;
        nj_ref ni;
        while (cap < (nj_u4)n + 1u) {
            cap *= 2u;
        }
        ni = nj_object_array_new(vm, NJ_C_ITEM, cap);
        if (!ni) {
            return nj_throw_oom(vm);
        }
        for (i = 0; i < n && items && (nj_u4)i < items->length; i++) {
            nj_array_set_ref(ni, (nj_u4)i, nj_array_get_ref(items, (nj_u4)i));
        }
        rput(vm, form, NJ_C_FORM, "fmItems", D_ITEMARR, ni);
        items = ni;
    }
    for (i = n; i > at; i--) {
        nj_array_set_ref(items, (nj_u4)i, nj_array_get_ref(items, (nj_u4)(i - 1)));
    }
    nj_array_set_ref(items, (nj_u4)at, item);
    rput(vm, item, NJ_C_ITEM, "itOwner", D_FORM, form);
    rput(vm, item, NJ_C_ITEM, "itGraphics", D_GFX,
         rget(vm, form, NJ_C_FORM, "fmBuffer", D_IMAGE)
             ? rget(vm, form, NJ_C_FORM, "fmBuffer", D_IMAGE) : NULL);
    fput(vm, form, NJ_C_FORM, "fmCount", D_INT, n + 1);
    return NJ_OK;
}

NJFN(form_append_str)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_ref item;
    nj_status st;
    nj_value a[3];

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    item = nj_object_new(vm, NJ_C_STRINGITEM);
    if (!item) {
        return new_failed(vm);
    }
    a[0].r = NULL;      /* label */
    a[1].r = s;         /* text  */
    a[2].i = 0;         /* PLAIN */
    st = nj_call_special(vm, item, NJ_C_STRINGITEM, "<init>",
                         "(Ljava/lang/String;Ljava/lang/String;I)V", a, NULL);
    if (st != NJ_OK) {
        return st;
    }
    st = form_add_item(vm, self, item, fget(vm, self, NJ_C_FORM, "fmCount", D_INT));
    if (st != NJ_OK) {
        return st;
    }
    ret->i = fget(vm, self, NJ_C_FORM, "fmCount", D_INT) - 1;
    return NJ_OK;
}

NJFN(form_append_img)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    nj_ref item;
    nj_status st;
    nj_value a[4];

    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image is null");
    }
    item = nj_object_new(vm, NJ_C_IMAGEITEM);
    if (!item) {
        return new_failed(vm);
    }
    a[0].r = NULL;                      /* label  */
    a[1].r = img;                       /* image  */
    a[2].i = 0;                         /* layout */
    a[3].r = nj_string_new(vm, "");     /* alt    */
    st = nj_call_special(vm, item, NJ_C_IMAGEITEM, "<init>",
                         "(Ljava/lang/String;L" NJ_C_IMAGE ";ILjava/lang/String;)V",
                         a, NULL);
    if (st != NJ_OK) {
        return st;
    }
    st = form_add_item(vm, self, item, fget(vm, self, NJ_C_FORM, "fmCount", D_INT));
    if (st != NJ_OK) {
        return st;
    }
    ret->i = fget(vm, self, NJ_C_FORM, "fmCount", D_INT) - 1;
    return NJ_OK;
}

NJFN(form_append_item)
{
    nj_ref self = NJ_THIS(args);
    nj_status st = form_add_item(vm, self, NJ_ARG_REF(args, 0),
                                 fget(vm, self, NJ_C_FORM, "fmCount", D_INT));
    if (st != NJ_OK) {
        return st;
    }
    ret->i = fget(vm, self, NJ_C_FORM, "fmCount", D_INT) - 1;
    return NJ_OK;
}

NJFN(form_insert_str)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref s = NJ_ARG_REF(args, 1);
    nj_ref item;
    nj_status st;
    nj_value a[3];

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    item = nj_object_new(vm, NJ_C_STRINGITEM);
    if (!item) {
        return nj_throw_oom(vm);
    }
    a[0].r = NULL;
    a[1].r = s;
    a[2].i = 0;
    st = nj_call_special(vm, item, NJ_C_STRINGITEM, "<init>",
                         "(Ljava/lang/String;Ljava/lang/String;I)V", a, NULL);
    if (st != NJ_OK) {
        return st;
    }
    return form_add_item(vm, self, item, at);
}

NJFN(form_insert_img)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref img = NJ_ARG_REF(args, 1);
    nj_ref item;
    nj_status st;
    nj_value a[4];

    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image is null");
    }
    item = nj_object_new(vm, NJ_C_IMAGEITEM);
    if (!item) {
        return new_failed(vm);
    }
    a[0].r = NULL;                      /* label  */
    a[1].r = img;                       /* image  */
    a[2].i = 0;                         /* layout */
    a[3].r = nj_string_new(vm, "");     /* alt    */
    st = nj_call_special(vm, item, NJ_C_IMAGEITEM, "<init>",
                         "(Ljava/lang/String;L" NJ_C_IMAGE ";ILjava/lang/String;)V",
                         a, NULL);
    if (st != NJ_OK) {
        return st;
    }
    return form_add_item(vm, self, item, at);
}

NJFN(form_insert_item)
{
    return form_add_item(vm, NJ_THIS(args), NJ_ARG_REF(args, 1),
                         NJ_ARG_INT(args, 0));
}

NJFN(form_delete)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref items = rget(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, self, NJ_C_FORM, "fmCount", D_INT);
    nj_s4 i;

    if (at < 0 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    if (items) {
        for (i = at; i < n - 1; i++) {
            nj_array_set_ref(items, (nj_u4)i,
                             nj_array_get_ref(items, (nj_u4)(i + 1)));
        }
        nj_array_set_ref(items, (nj_u4)(n - 1), NULL);
    }
    fput(vm, self, NJ_C_FORM, "fmCount", D_INT, n - 1);
    if (fget(vm, self, NJ_C_FORM, "fmIndex", D_INT) >= n - 1) {
        fput(vm, self, NJ_C_FORM, "fmIndex", D_INT, n - 2);
    }
    return NJ_OK;
}

NJFN(form_deleteAll)
{
    nj_ref self = NJ_THIS(args);
    nj_ref items = rget(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, self, NJ_C_FORM, "fmCount", D_INT);
    nj_s4 i;

    if (items) {
        for (i = 0; i < n && (nj_u4)i < items->length; i++) {
            nj_array_set_ref(items, (nj_u4)i, NULL);
        }
    }
    fput(vm, self, NJ_C_FORM, "fmCount", D_INT, 0);
    fput(vm, self, NJ_C_FORM, "fmIndex", D_INT, 0);
    return NJ_OK;
}

NJFN(form_size)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_FORM, "fmCount", D_INT);
    return NJ_OK;
}

NJFN(form_get)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref items = rget(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, self, NJ_C_FORM, "fmCount", D_INT);

    if (at < 0 || at >= n || !items || (nj_u4)at >= items->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->r = nj_array_get_ref(items, (nj_u4)at);
    return NJ_OK;
}

NJFN(form_set)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref item = NJ_ARG_REF(args, 1);
    nj_ref items = rget(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, self, NJ_C_FORM, "fmCount", D_INT);

    if (!item) {
        return nj_throw(vm, "java/lang/NullPointerException", "item is null");
    }
    if (at < 0 || at >= n || !items || (nj_u4)at >= items->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    nj_array_set_ref(items, (nj_u4)at, item);
    rput(vm, item, NJ_C_ITEM, "itOwner", D_FORM, self);
    return NJ_OK;
}

NJFN(form_setItemStateListener)
{
    rput(vm, NJ_THIS(args), NJ_C_FORM, "fmItemListener", D_ISL,
         NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* List                                                                */
/* ------------------------------------------------------------------ */
/*
 * A List keeps StringItem objects in the Item[] it inherits from Form, so the
 * inherited layout machinery works unchanged; the parallel String/Image arrays
 * are views rebuilt after every mutation.  list_wrap() builds one element.
 */
static void list_sync(nj_vm *vm, nj_ref self);

static nj_ref list_wrap(nj_vm *vm, nj_ref s, nj_ref img)
{
    nj_ref item = nj_object_new(vm, NJ_C_STRINGITEM);
    nj_value a[3];

    if (!item) {
        return NULL;   /* the pending exception names the class */
    }
    a[0].r = NULL;
    a[1].r = s;
    a[2].i = 0;
    if (nj_call_special(vm, item, NJ_C_STRINGITEM, "<init>",
                        "(Ljava/lang/String;Ljava/lang/String;I)V", a,
                        NULL) != NJ_OK) {
        return NULL;
    }
    if (img) {
        rput(vm, item, NJ_C_ITEM, "itImage", D_IMAGE, img);
    }
    return item;
}

NJFN(list_init)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 type = NJ_ARG_INT(args, 1);

    d_init_on(vm, self);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TITLE, D_STR, NJ_ARG_REF(args, 0));
    rput(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR, NULL);
    fput(vm, self, NJ_C_FORM, "fmCount", D_INT, 0);
    fput(vm, self, NJ_C_FORM, "fmIndex", D_INT, 0);
    rput(vm, self, NJ_C_FORM, "fmItemListener", D_ISL, NULL);
    rput(vm, self, NJ_C_FORM, "fmBuffer", D_IMAGE, NULL);
    if (type < NJ_CHOICE_EXCLUSIVE || type > NJ_CHOICE_IMPLICIT) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "list type must be EXCLUSIVE, MULTIPLE or IMPLICIT");
    }
    fput(vm, self, NJ_C_LIST, "lsType", D_INT, type);
    rput(vm, self, NJ_C_LIST, "lsStrings", D_STRARR, NULL);
    rput(vm, self, NJ_C_LIST, "lsImages", D_IMGARR, NULL);
    fput(vm, self, NJ_C_LIST, "lsCount", D_INT, 0);
    fput(vm, self, NJ_C_LIST, "lsIndex", D_INT, 0);
    fput(vm, self, NJ_C_LIST, "lsFit", D_INT, 0);
    rput(vm, self, NJ_C_LIST, "lsFont", D_FONT, NULL);
    if (type == NJ_CHOICE_IMPLICIT) {
        rput(vm, self, NJ_C_LIST, "lsSelect", D_CMD,
             rgets(vm, NJ_C_LIST, "SELECT_COMMAND", D_CMD));
    }
    return NJ_OK;
}

NJFN(list_init_arrays)
{
    nj_ref self = NJ_THIS(args);
    nj_ref strs = NJ_ARG_REF(args, 2);
    nj_ref imgs = NJ_ARG_REF(args, 3);
    nj_u4 i;

    if (list_init(vm, th, args, ret) != NJ_OK) {
        return NJ_ERR;
    }
    if (!strs || !imgs) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "strings and images must not be null");
    }
    if (strs->length != imgs->length) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "strings and images differ in length");
    }
    for (i = 0u; i < strs->length; i++) {
        nj_ref s = nj_array_get_ref(strs, i);
        nj_ref it;
        if (!s) {
            return nj_throw(vm, "java/lang/NullPointerException",
                            "list element is null");
        }
        it = list_wrap(vm, s, nj_array_get_ref(imgs, i));
        if (!it) {
            return vm->exception ? NJ_ERR : nj_throw_oom(vm);
        }
        if (form_add_item(vm, self, it, (nj_s4)i) != NJ_OK) {
            return NJ_ERR;
        }
    }
    rput(vm, self, NJ_C_LIST, "lsStrings", D_STRARR, strs);
    rput(vm, self, NJ_C_LIST, "lsImages", D_IMGARR, imgs);
    fput(vm, self, NJ_C_LIST, "lsCount", D_INT, (nj_s4)strs->length);
    fput(vm, self, NJ_C_LIST, "lsIndex", D_INT, 0);
    return NJ_OK;
}

NJFN(list_getType)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_LIST, "lsType", D_INT);
    return NJ_OK;
}

/* Append or insert one element, keeping both representations in step. */
static nj_status list_add(nj_vm *vm, nj_ref self, nj_ref s, nj_ref img,
                          nj_s4 at)
{
    nj_ref item;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    item = list_wrap(vm, s, img);
    if (!item) {
        return vm->exception ? NJ_ERR : nj_throw_oom(vm);
    }
    return form_add_item(vm, self, item, at);
}

/* List.append(String) returns the index of the new element (MIDP 2.0). */
NJFN(list_append_str)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);
    nj_status st = list_add(vm, self, NJ_ARG_REF(args, 0), NULL, at);
    if (st != NJ_OK) {
        return st;
    }
    list_sync(vm, self);
    ret->i = fget(vm, self, NJ_C_LIST, "lsCount", D_INT) - 1;
    return NJ_OK;
}

NJFN(list_append_img)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);
    nj_status st = list_add(vm, self, NJ_ARG_REF(args, 0), NJ_ARG_REF(args, 1),
                            at);
    if (st != NJ_OK) {
        return st;
    }
    list_sync(vm, self);
    ret->i = fget(vm, self, NJ_C_LIST, "lsCount", D_INT) - 1;
    return NJ_OK;
}

NJFN(list_insert_str)
{
    nj_ref self = NJ_THIS(args);
    nj_status st = list_add(vm, self, NJ_ARG_REF(args, 1), NULL,
                            NJ_ARG_INT(args, 0));
    if (st != NJ_OK) {
        return st;
    }
    list_sync(vm, self);
    return NJ_OK;
}

NJFN(list_insert_img)
{
    nj_ref self = NJ_THIS(args);
    nj_status st = list_add(vm, self, NJ_ARG_REF(args, 1), NJ_ARG_REF(args, 2),
                            NJ_ARG_INT(args, 0));
    if (st != NJ_OK) {
        return st;
    }
    list_sync(vm, self);
    return NJ_OK;
}

/*
 * List keeps its Item[] (inherited from Form) as the source of truth; the
 * parallel String/Image arrays are views rebuilt after every mutation so the
 * native renderer can walk either representation.
 */
static void list_sync(nj_vm *vm, nj_ref self)
{
    nj_ref items = rget(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, self, NJ_C_FORM, "fmCount", D_INT);
    nj_ref strs;
    nj_ref imgs;
    nj_s4 i;

    if (!items) {
        return;
    }
    strs = nj_object_array_new(vm, "java/lang/String", (nj_u4)n);
    imgs = nj_object_array_new(vm, NJ_C_IMAGE, (nj_u4)n);
    if (!strs || !imgs) {
        return;
    }
    for (i = 0; i < n && (nj_u4)i < items->length; i++) {
        nj_ref it = nj_array_get_ref(items, (nj_u4)i);
        nj_ref s = NULL;
        nj_ref im = NULL;
        if (it) {
            s = rget(vm, it, NJ_C_STRINGITEM, "siText", D_STR);
            im = rget(vm, it, NJ_C_ITEM, "itImage", D_IMAGE);
        }
        nj_array_set_ref(strs, (nj_u4)i, s);
        nj_array_set_ref(imgs, (nj_u4)i, im);
    }
    rput(vm, self, NJ_C_LIST, "lsStrings", D_STRARR, strs);
    rput(vm, self, NJ_C_LIST, "lsImages", D_IMGARR, imgs);
    fput(vm, self, NJ_C_LIST, "lsCount", D_INT, n);
}

NJFN(list_delete)
{
    nj_status st = form_delete(vm, th, args, ret);
    if (st != NJ_OK) {
        return st;
    }
    list_sync(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(list_deleteAll)
{
    nj_status st = form_deleteAll(vm, th, args, ret);
    if (st != NJ_OK) {
        return st;
    }
    list_sync(vm, NJ_THIS(args));
    fput(vm, NJ_THIS(args), NJ_C_LIST, "lsIndex", D_INT, 0);
    return NJ_OK;
}

NJFN(list_size)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_LIST, "lsCount", D_INT);
    return NJ_OK;
}

/*
 * List keeps its elements as StringItems in the Screen item array, and
 * list_sync() maintains lsStrings/lsImages as views of those.  These four
 * accessors therefore work on the List's own arrays: forwarding to the
 * ChoiceGroup ones would look up ChoiceGroup's fields on a List object, which
 * is a different class with a different layout.
 */
NJFN(list_getString)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref strs = rget(vm, self, NJ_C_LIST, "lsStrings", D_STRARR);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);

    if (at < 0 || at >= n || !strs || (nj_u4)at >= strs->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->r = nj_array_get_ref(strs, (nj_u4)at);
    return NJ_OK;
}

NJFN(list_setString)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref str = NJ_ARG_REF(args, 1);
    nj_ref items = rget(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);
    nj_ref it;

    if (!str) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    if (at < 0 || at >= n || !items || (nj_u4)at >= items->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    it = nj_array_get_ref(items, (nj_u4)at);
    if (it) {
        rput(vm, it, NJ_C_STRINGITEM, "siText", D_STR, str);
    }
    list_sync(vm, self);
    return NJ_OK;
}

NJFN(list_getImage)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref imgs = rget(vm, self, NJ_C_LIST, "lsImages", D_IMGARR);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);

    if (at < 0 || at >= n || !imgs || (nj_u4)at >= imgs->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->r = nj_array_get_ref(imgs, (nj_u4)at);
    return NJ_OK;
}

NJFN(list_setImage)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref img = NJ_ARG_REF(args, 1);
    nj_ref items = rget(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);
    nj_ref it;

    if (at < 0 || at >= n || !items || (nj_u4)at >= items->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    it = nj_array_get_ref(items, (nj_u4)at);
    if (it) {
        rput(vm, it, NJ_C_ITEM, "itImage", D_IMAGE, img);
    }
    list_sync(vm, self);
    return NJ_OK;
}

NJFN(list_getSelectedIndex)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_LIST, "lsIndex", D_INT);
    return NJ_OK;
}

NJFN(list_setSelectedIndex)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);

    if (at < 0 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    fput(vm, self, NJ_C_LIST, "lsIndex", D_INT, at);
    return NJ_OK;
}

NJFN(list_isSelected)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);

    if (at < 0 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    /* List items carry no per-element flag of their own; the cursor is the
     * selection (MIDP keeps this consistent for all three list types). */
    ret->i = (at == fget(vm, self, NJ_C_LIST, "lsIndex", D_INT)) ? 1 : 0;
    return NJ_OK;
}

/* List.setSelectedFlags(boolean[]) is void in MIDP; a List has one cursor. */
NJFN(list_setSelectedFlags)
{
    nj_ref self = NJ_THIS(args);
    nj_ref flags = NJ_ARG_REF(args, 0);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);
    nj_s4 i;

    if (!flags) {
        return nj_throw(vm, "java/lang/NullPointerException", "flags is null");
    }
    for (i = 0; i < n && (nj_u4)i < flags->length; i++) {
        if (flags->u.bytes[i]) {
            fput(vm, self, NJ_C_LIST, "lsIndex", D_INT, i);
            return NJ_OK;
        }
    }
    return NJ_OK;
}

/* setSelectedIndex(int index, boolean selected): a List cursor cannot be
 * deselected, so `false` on the current row is a legal no-op. */
NJFN(list_setSelectedIndex2)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_s4 on = NJ_ARG_INT(args, 1);

    if (!on) {
        return NJ_OK;
    }
    if (at < 0 || at >= fget(vm, self, NJ_C_LIST, "lsCount", D_INT)) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    fput(vm, self, NJ_C_LIST, "lsIndex", D_INT, at);
    return NJ_OK;
}

/* set(int index, String text, Image image) */
NJFN(list_set)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref str = NJ_ARG_REF(args, 1);
    nj_ref img = NJ_ARG_REF(args, 2);
    nj_ref items = rget(vm, self, NJ_C_FORM, "fmItems", D_ITEMARR);
    nj_ref it;

    if (!str) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    if (at < 0 || at >= fget(vm, self, NJ_C_LIST, "lsCount", D_INT) ||
        !items || (nj_u4)at >= items->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    it = nj_array_get_ref(items, (nj_u4)at);
    if (it) {
        rput(vm, it, NJ_C_STRINGITEM, "siText", D_STR, str);
        rput(vm, it, NJ_C_ITEM, "itImage", D_IMAGE, img);
    }
    list_sync(vm, self);
    return NJ_OK;
}

/* setSelectCommand(Command): only an IMPLICIT List has a select action. */
NJFN(list_setSelectCommand)
{
    nj_ref self = NJ_THIS(args);
    nj_ref c = NJ_ARG_REF(args, 0);

    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", "command is null");
    }
    if (fget(vm, self, NJ_C_LIST, "lsType", D_INT) != NJ_CHOICE_IMPLICIT) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "only an IMPLICIT List has a select command");
    }
    /*
     * The type is NOT checked.  JSR 118 says the select command should be of
     * type ITEM, but a real phone accepted 贪吃蛇经典版_4515's Command of type
     * OK here, and refusing it killed the MIDlet in its constructor - three
     * frames and a dead run, which is a certain failure, against a soft key
     * that shows a slightly odd label, which is cosmetic.  When those are the
     * two outcomes, take the cosmetic one.
     */
    rput(vm, self, NJ_C_LIST, "lsSelect", D_CMD, c);
    return NJ_OK;
}

NJFN(list_getSelectedFlags)
{
    nj_ref self = NJ_THIS(args);
    nj_ref flags = NJ_ARG_REF(args, 0);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);
    nj_s4 i, count = 0;

    if (!flags) {
        return nj_throw(vm, "java/lang/NullPointerException", "flags is null");
    }
    for (i = 0; i < n && (nj_u4)i < flags->length; i++) {
        int on = (i == fget(vm, self, NJ_C_LIST, "lsIndex", D_INT)) ? 1 : 0;
        flags->u.bytes[i] = (nj_u1)on;
        count += on;
    }
    ret->i = count;
    return NJ_OK;
}

NJFN(list_setFitPolicy)
{
    fput(vm, NJ_THIS(args), NJ_C_LIST, "lsFit", D_INT,
         NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(list_getFitPolicy)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_LIST, "lsFit", D_INT);
    return NJ_OK;
}

NJFN(list_setFont)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref f = NJ_ARG_REF(args, 1);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);

    if (!f) {
        return nj_throw(vm, "java/lang/NullPointerException", "font is null");
    }
    if (at < -1 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    rput(vm, self, NJ_C_LIST, "lsFont", D_FONT, f);
    return NJ_OK;
}

NJFN(list_getFont)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_s4 n = fget(vm, self, NJ_C_LIST, "lsCount", D_INT);

    if (at < -1 || at >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->r = rget(vm, self, NJ_C_LIST, "lsFont", D_FONT);
    return NJ_OK;
}

NJFN(list_getSelectCommand)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_LIST, "lsSelect", D_CMD);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* TextBox                                                             */
/* ------------------------------------------------------------------ */
NJFN(tb_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref title = NJ_ARG_REF(args, 0);
    nj_ref text = NJ_ARG_REF(args, 1);
    nj_s4 max = NJ_ARG_INT(args, 2);
    nj_s4 constraints = NJ_ARG_INT(args, 3);
    nj_ref chars;

    (void)d_init(vm, th, args, ret);
    text = text_or_empty(vm, text);
    if (!text) {
        return nj_throw_oom(vm);
    }
    if (max <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "maxSize must be positive");
    }
    if ((constraints & 0xFFFF) > 5) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown constraints");
    }
    if (nj_string_len(text) > (nj_u4)max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "initial text longer than maxSize");
    }
    if (!tf_allowed(constraints, nj_string_chars(text),
                    (int)nj_string_len(text))) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "initial text violates the constraints");
    }
    chars = nj_char_array_new(vm, (nj_u4)max);
    if (!chars) {
        return nj_throw_oom(vm);
    }
    if (nj_string_len(text) > 0u) {
        memcpy(chars->u.bytes, nj_string_chars(text),
               (nj_size)nj_string_len(text) * sizeof(nj_u2));
    }
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TITLE, D_STR, title);
    rput(vm, self, NJ_C_TEXTBOX, "tbText", D_STR, text);
    rput(vm, self, NJ_C_TEXTBOX, "tbChars", D_CHARR, chars);
    fput(vm, self, NJ_C_TEXTBOX, "tbMax", D_INT, max);
    fput(vm, self, NJ_C_TEXTBOX, "tbConstraints", D_INT, constraints);
    fput(vm, self, NJ_C_TEXTBOX, "tbCursor", D_INT, (nj_s4)nj_string_len(text));
    fput(vm, self, NJ_C_TEXTBOX, "tbInputMode", D_INT, -1);
    rput(vm, self, NJ_C_TEXTBOX, "tbInputModeName", D_STR, NULL);
    rput(vm, self, NJ_C_TEXTBOX, "tbBuffer", D_IMAGE, NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

static nj_status tb_sync(nj_vm *vm, nj_ref self, const nj_u2 *s, int n)
{
    nj_ref chars = rget(vm, self, NJ_C_TEXTBOX, "tbChars", D_CHARR);
    nj_ref str;

    if (!chars || (nj_u4)n > chars->length) {
        return NJ_OK;
    }
    if (n > 0) {
        memcpy(chars->u.bytes, s, (nj_size)n * sizeof(nj_u2));
    }
    if ((nj_u4)n < chars->length) {
        memset((nj_u2 *)chars->u.bytes + n, 0,
               (nj_size)(chars->length - (nj_u4)n) * sizeof(nj_u2));
    }
    str = nj_new_string_utf16(vm, s, n);
    if (!str) {
        return nj_throw_oom(vm);
    }
    rput(vm, self, NJ_C_TEXTBOX, "tbText", D_STR, str);
    return NJ_OK;
}

/* Resolve focus on every event: a native editor never retains a Java object. */
static nj_ref text_focus(nj_vm *vm, int *box)
{
    nj_ref d = nj_midp_current(vm);
    *box = 0;
    if (isa(vm, d, NJ_C_TEXTBOX)) { *box = 1; return d; }
    if (isa(vm, d, NJ_C_FORM)) {
        nj_ref items = rget(vm, d, NJ_C_FORM, "fmItems", D_ITEMARR);
        nj_s4 at = fget(vm, d, NJ_C_FORM, "fmIndex", D_INT);
        if (items && at >= 0 && (nj_u4)at < items->length) {
            nj_ref it = nj_array_get_ref(items, (nj_u4)at);
            if (isa(vm, it, NJ_C_TEXTFIELD)) { return it; }
        }
    }
    return NULL;
}

void nj_midp_text_input_sync(nj_vm *vm)
{
    int box;
    nj_ref it, disp, str;
    const char *cls;
    nj_text_input_state state;
    if (!vm || !vm->port || !vm->port->text_input) { return; }
    memset(&state, 0, sizeof(state));
    disp = nj_midp_display(vm);
    if (!disp) { return; }
    it = text_focus(vm, &box);
    cls = box ? NJ_C_TEXTBOX : NJ_C_TEXTFIELD;
    if (it) {
        state.constraints = (nj_u4)fget(vm, it, cls,
                                 box ? "tbConstraints" : "tfConstraints", D_INT);
        state.max_length = fget(vm, it, cls, box ? "tbMax" : "tfMax", D_INT);
        if (state.constraints & 0x20000u) { it = NULL; }
    }
    state.session = (nj_u4)fget(vm, disp, NJ_C_DISPLAY, "dpImeSession", D_INT);
    if (it != rget(vm, disp, NJ_C_DISPLAY, "dpImeFocus", D_ITEM) ||
        state.constraints != (nj_u4)fget(vm, disp, NJ_C_DISPLAY, "dpImeConstraints", D_INT) ||
        state.max_length != fget(vm, disp, NJ_C_DISPLAY, "dpImeMax", D_INT)) {
        if (++state.session == 0u) { ++state.session; }
        rput(vm, disp, NJ_C_DISPLAY, "dpImeFocus", D_ITEM, it);
        fput(vm, disp, NJ_C_DISPLAY, "dpImeSession", D_INT, (nj_s4)state.session);
        fput(vm, disp, NJ_C_DISPLAY, "dpImeSequence", D_INT, 0);
        fput(vm, disp, NJ_C_DISPLAY, "dpImeConstraints", D_INT, (nj_s4)state.constraints);
        fput(vm, disp, NJ_C_DISPLAY, "dpImeMax", D_INT, state.max_length);
    }
    state.active = it != NULL;
    state.multiline = box;
    state.sequence = (nj_u4)fget(vm, disp, NJ_C_DISPLAY, "dpImeSequence", D_INT);
    str = it ? rget(vm, it, cls, box ? "tbText" : "tfText", D_STR) : NULL;
    state.length = str ? (int)nj_string_len(str) : 0;
    state.text = str ? nj_string_chars(str) : NULL;
    state.cursor = it ? fget(vm, it, cls, box ? "tbCursor" : "tfCursor", D_INT) : 0;
    vm->port->text_input(vm->port->ud, &state);
}

static int text_high(nj_u2 c) { return c >= 0xD800u && c <= 0xDBFFu; }
static int text_low(nj_u2 c) { return c >= 0xDC00u && c <= 0xDFFFu; }

/* Strict standard UTF-8, unlike the modified UTF-8 used by Java class files.
 * Reject the entire commit if malformed; never expose a partial composition. */
static int text_decode(const char *s, nj_u2 *out)
{
    const unsigned char *p = (const unsigned char *)s;
    int n = 0, bytes = 0;
    if (!s) { return -1; }
    while (*p) {
        nj_u4 cp, min;
        int more, k;
        unsigned char c = *p++;
        if (++bytes >= NJ_TEXT_INPUT_BYTES) { return -1; }
        if (c < 0x80u) { cp = c; min = 0; more = 0; }
        else if (c >= 0xC2u && c <= 0xDFu) { cp = c & 31u; min = 0x80u; more = 1; }
        else if (c >= 0xE0u && c <= 0xEFu) { cp = c & 15u; min = 0x800u; more = 2; }
        else if (c >= 0xF0u && c <= 0xF4u) { cp = c & 7u; min = 0x10000u; more = 3; }
        else { return -1; }
        for (k = 0; k < more; k++) {
            if ((*p & 0xC0u) != 0x80u || ++bytes >= NJ_TEXT_INPUT_BYTES) { return -1; }
            cp = (cp << 6) | (*p++ & 63u);
        }
        if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) { return -1; }
        if (cp < 32u && cp != '\n') { return -1; }
        if (cp <= 0xFFFFu) { out[n++] = (nj_u2)cp; }
        else {
            cp -= 0x10000u;
            out[n++] = (nj_u2)(0xD800u | (cp >> 10));
            out[n++] = (nj_u2)(0xDC00u | (cp & 1023u));
        }
    }
    return n;
}

static int text_change(nj_vm *vm, const nj_u2 *insert, int count, int edit)
{
    int box, len, at, begin, end, newlen;
    nj_ref it, str;
    const char *cls, *cursor;
    nj_s4 constraints, max;
    const nj_u2 *old;
    nj_u2 *buf;
    nj_status st;
    nj_u4 mark;
    if (!vm) { return 0; }
    it = text_focus(vm, &box);
    if (!it) { return 0; }
    cls = box ? NJ_C_TEXTBOX : NJ_C_TEXTFIELD;
    cursor = box ? "tbCursor" : "tfCursor";
    constraints = fget(vm, it, cls, box ? "tbConstraints" : "tfConstraints", D_INT);
    if (constraints & 0x20000) { return 0; }
    str = rget(vm, it, cls, box ? "tbText" : "tfText", D_STR);
    len = str ? (int)nj_string_len(str) : 0;
    old = str ? nj_string_chars(str) : NULL;
    at = fget(vm, it, cls, cursor, D_INT);
    if (at < 0) { at = 0; }
    if (at > len) { at = len; }
    if (at > 0 && at < len && text_high(old[at-1]) && text_low(old[at])) { at--; }
    begin = end = at;
    switch (edit) {
    case NJ_TEXT_LEFT:
    case NJ_TEXT_BACKSPACE:
        if (begin > 0) {
            begin--;
            if (begin > 0 && text_low(old[begin]) && text_high(old[begin-1])) { begin--; }
        }
        at = begin;
        break;
    case NJ_TEXT_RIGHT:
    case NJ_TEXT_DELETE:
        if (end < len) {
            end++;
            if (end < len && text_high(old[end-1]) && text_low(old[end])) { end++; }
        }
        if (edit == NJ_TEXT_RIGHT) { at = end; }
        break;
    case NJ_TEXT_HOME: at = 0; break;
    case NJ_TEXT_END: at = len; break;
    case -1: begin = 0; end = len; break; /* complete native editor replacement */
    case 0: break;
    default: return 0;
    }
    if (edit > 0 && edit != NJ_TEXT_BACKSPACE && edit != NJ_TEXT_DELETE) {
        fput(vm, it, cls, cursor, D_INT, at);
        nj_midp_repaint(vm);
        return 1;
    }
    max = fget(vm, it, cls, box ? "tbMax" : "tfMax", D_INT);
    if (count > max - (len - (end - begin))) { return 0; }
    newlen = len - (end - begin) + count;
    if (newlen == len && !count && begin == end) { return 1; }
    buf = (nj_u2 *)nj_host_alloc(vm, (nj_size)(newlen + 1) * sizeof(nj_u2));
    if (!buf) { (void)nj_throw_oom(vm); return 0; }
    if (begin) { memcpy(buf, old, (nj_size)begin * sizeof(nj_u2)); }
    if (count) { memcpy(buf + begin, insert, (nj_size)count * sizeof(nj_u2)); }
    if (len > end) { memcpy(buf + begin + count, old + end, (nj_size)(len-end) * sizeof(nj_u2)); }
    if (!tf_allowed(constraints, buf, newlen)) { nj_host_free(vm, buf); return 0; }
    if ((constraints & 0xFFFF) == 5) {
        int i, dots = 0;
        for (i = 0; i < newlen; i++) {
            if ((buf[i] == '-' && i != 0) || (buf[i] == '.' && ++dots > 1)) {
                nj_host_free(vm, buf); return 0;
            }
        }
    }
    mark = nj_root_mark(vm);
    nj_root(vm, it);
    st = box ? tb_sync(vm, it, buf, newlen) : tf_sync(vm, it, buf, newlen);
    nj_host_free(vm, buf);
    if (st == NJ_OK) {
        fput(vm, it, cls, cursor, D_INT, begin + count);
        if (!box) { item_state_changed(vm, it); }
        nj_midp_repaint(vm);
    }
    nj_root_release(vm, mark);
    return st == NJ_OK;
}

int nj_midp_text(nj_vm *vm, const char *utf8)
{
    nj_u2 chars[NJ_TEXT_INPUT_BYTES];
    int n = text_decode(utf8, chars);
    return n > 0 ? text_change(vm, chars, n, 0) : 0;
}

int nj_midp_text_edit(nj_vm *vm, int edit)
{
    return edit > 0 ? text_change(vm, NULL, 0, edit) : 0;
}

int nj_midp_text_event(nj_vm *vm, const nj_event *event)
{
    nj_ref disp, it;
    int box, i, ok;
    nj_u4 session, sequence;
    if (!vm || !event) { return 0; }
    disp = nj_midp_display(vm);
    it = text_focus(vm, &box);
    if (!disp || !it || it != rget(vm, disp, NJ_C_DISPLAY, "dpImeFocus", D_ITEM)) { return 0; }
    session = (nj_u4)fget(vm, disp, NJ_C_DISPLAY, "dpImeSession", D_INT);
    if (!event->text_session || event->text_session != session) { return 0; }
    if (event->type == NJ_EV_TEXT_INPUT) { return nj_midp_text(vm, event->text); }
    if (event->type == NJ_EV_TEXT_EDIT) { return nj_midp_text_edit(vm, event->key); }
    if (event->type != NJ_EV_TEXT_REPLACE) { return 0; }
    sequence = (nj_u4)fget(vm, disp, NJ_C_DISPLAY, "dpImeSequence", D_INT);
    if (!event->text_sequence || event->text_sequence <= sequence) { return 0; }
    /* Acknowledge rejected input as well, so Android restores the valid value. */
    fput(vm, disp, NJ_C_DISPLAY, "dpImeSequence", D_INT, (nj_s4)event->text_sequence);
    if (event->text_length < 0 || (event->text_length && !event->text_utf16) ||
        event->text_cursor < 0 || event->text_cursor > event->text_length) { return 0; }
    for (i = 0; i < event->text_length; i++) {
        nj_u2 c = event->text_utf16[i];
        if ((c < 32u && c != '\n') || text_low(c)) { return 0; }
        if (text_high(c) && (++i >= event->text_length || !text_low(event->text_utf16[i]))) { return 0; }
    }
    i = event->text_cursor;
    if (i > 0 && i < event->text_length && text_high(event->text_utf16[i-1]) &&
        text_low(event->text_utf16[i])) { return 0; }
    ok = text_change(vm, event->text_utf16, event->text_length, -1);
    /* ItemStateListener may have moved focus: never update the new widget. */
    if (ok && it == text_focus(vm, &box)) {
        fput(vm, it, box ? NJ_C_TEXTBOX : NJ_C_TEXTFIELD,
             box ? "tbCursor" : "tfCursor", D_INT, event->text_cursor);
    }
    return ok;
}

NJFN(tb_getString)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_TEXTBOX, "tbText", D_STR);
    return NJ_OK;
}

NJFN(tb_setString)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_s4 max = fget(vm, self, NJ_C_TEXTBOX, "tbMax", D_INT);

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    if ((nj_s4)nj_string_len(s) > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text longer than maxSize");
    }
    if (!tf_allowed(fget(vm, self, NJ_C_TEXTBOX, "tbConstraints", D_INT),
                    nj_string_chars(s), (int)nj_string_len(s))) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text violates the constraints");
    }
    if (tb_sync(vm, self, nj_string_chars(s), (int)nj_string_len(s)) != NJ_OK) {
        return NJ_ERR;
    }
    fput(vm, self, NJ_C_TEXTBOX, "tbCursor", D_INT, (nj_s4)nj_string_len(s));
    return NJ_OK;
}

/* getChars(char[]) returns the number of characters copied (MIDP). */
NJFN(tb_getChars)
{
    nj_ref self = NJ_THIS(args);
    nj_ref dst = NJ_ARG_REF(args, 0);
    nj_ref src = rget(vm, self, NJ_C_TEXTBOX, "tbChars", D_CHARR);
    nj_ref text = rget(vm, self, NJ_C_TEXTBOX, "tbText", D_STR);
    nj_u4 n = 0u;

    if (!dst) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (src && text) {
        n = nj_string_len(text);
        if (n > dst->length) {
            n = dst->length;
        }
        if (n > 0u) {
            memcpy(dst->u.bytes, src->u.bytes, (nj_size)n * sizeof(nj_u2));
        }
    }
    ret->i = (nj_s4)n;
    return NJ_OK;
}

NJFN(tb_setChars)
{
    nj_ref self = NJ_THIS(args);
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 max = fget(vm, self, NJ_C_TEXTBOX, "tbMax", D_INT);

    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > src->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (len > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text longer than maxSize");
    }
    if (!tf_allowed(fget(vm, self, NJ_C_TEXTBOX, "tbConstraints", D_INT),
                    (const nj_u2 *)src->u.bytes + off, (int)len)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text violates the constraints");
    }
    if (tb_sync(vm, self, (const nj_u2 *)src->u.bytes + off, (int)len) != NJ_OK) {
        return NJ_ERR;
    }
    fput(vm, self, NJ_C_TEXTBOX, "tbCursor", D_INT, len);
    return NJ_OK;
}

NJFN(tb_size)
{
    nj_ref s = rget(vm, NJ_THIS(args), NJ_C_TEXTBOX, "tbText", D_STR);
    ret->i = s ? (nj_s4)nj_string_len(s) : 0;
    return NJ_OK;
}

NJFN(tb_getMaxSize)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_TEXTBOX, "tbMax", D_INT);
    return NJ_OK;
}

NJFN(tb_setMaxSize)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 max = NJ_ARG_INT(args, 0);
    nj_ref text = rget(vm, self, NJ_C_TEXTBOX, "tbText", D_STR);
    nj_ref chars;

    if (max <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "maxSize must be positive");
    }
    if (text && (nj_s4)nj_string_len(text) > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text longer than the new maxSize");
    }
    chars = nj_char_array_new(vm, (nj_u4)max);
    if (!chars) {
        return nj_throw_oom(vm);
    }
    if (text && nj_string_len(text) > 0u) {
        memcpy(chars->u.bytes, nj_string_chars(text),
               (nj_size)nj_string_len(text) * sizeof(nj_u2));
    }
    rput(vm, self, NJ_C_TEXTBOX, "tbChars", D_CHARR, chars);
    fput(vm, self, NJ_C_TEXTBOX, "tbMax", D_INT, max);
    return NJ_OK;
}

NJFN(tb_getConstraints)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_TEXTBOX, "tbConstraints", D_INT);
    return NJ_OK;
}

NJFN(tb_setConstraints)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 c = NJ_ARG_INT(args, 0);
    nj_ref text = rget(vm, self, NJ_C_TEXTBOX, "tbText", D_STR);

    if ((c & 0xFFFF) > 5) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown constraints");
    }
    if (text && !tf_allowed(c, nj_string_chars(text),
                            (int)nj_string_len(text))) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "current text violates the new constraints");
    }
    fput(vm, self, NJ_C_TEXTBOX, "tbConstraints", D_INT, c);
    return NJ_OK;
}

NJFN(tb_setInitialInputMode)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "input mode is null");
    }
    rput(vm, self, NJ_C_TEXTBOX, "tbInputModeName", D_STR, s);
    return NJ_OK;
}

static nj_status tb_insert_on(nj_vm *vm, nj_ref self, nj_value *args)
{
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 at = NJ_ARG_INT(args, 3);
    nj_ref text = rget(vm, self, NJ_C_TEXTBOX, "tbText", D_STR);
    const nj_u2 *srcp = (const nj_u2 *)src->u.bytes;
    nj_s4 cur = text ? (nj_s4)nj_string_len(text) : 0;
    nj_s4 max = fget(vm, self, NJ_C_TEXTBOX, "tbMax", D_INT);
    nj_u2 *buf;

    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > src->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (at < 0 || at > cur) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (cur + len > max) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text longer than maxSize");
    }
    if (!tf_allowed(fget(vm, self, NJ_C_TEXTBOX, "tbConstraints", D_INT),
                    (const nj_u2 *)src->u.bytes + off, (int)len)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "text violates the constraints");
    }
    buf = (nj_u2 *)nj_host_alloc(vm, ((nj_size)cur + (nj_size)len + 1u) *
                                     sizeof(nj_u2));
    if (!buf) {
        return nj_throw_oom(vm);
    }
    if (at > 0) {
        memcpy(buf, nj_string_chars(text), (nj_size)at * sizeof(nj_u2));
    }
    memcpy(buf + at, srcp + off, (nj_size)len * sizeof(nj_u2));
    if (cur > at) {
        memcpy(buf + at + len, nj_string_chars(text) + at,
               (nj_size)(cur - at) * sizeof(nj_u2));
    }
    {
        nj_status st = tb_sync(vm, self, buf, (int)(cur + len));
        nj_host_free(vm, buf);
        if (st != NJ_OK) {
            return st;
        }
    }
    fput(vm, self, NJ_C_TEXTBOX, "tbCursor", D_INT, at + len);
    return NJ_OK;
}

NJFN(tb_insert)
{
    return tb_insert_on(vm, NJ_THIS(args), args);
}

/* insert(String src, int position) - the MIDP 2.0 String overload. */
NJFN(tb_insert_str)
{
    nj_ref str = NJ_ARG_REF(args, 0);
    nj_s4 at = NJ_ARG_INT(args, 1);
    nj_value a[4];

    if (!str) {
        return nj_throw(vm, "java/lang/NullPointerException", "text is null");
    }
    a[0].r = str;
    a[1].i = 0;
    a[2].i = (nj_s4)nj_string_len(str);
    a[3].i = at;
    return tb_insert_on(vm, NJ_THIS(args), a);
}

NJFN(tb_delete)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 off = NJ_ARG_INT(args, 0);
    nj_s4 len = NJ_ARG_INT(args, 1);
    nj_ref text = rget(vm, self, NJ_C_TEXTBOX, "tbText", D_STR);
    nj_s4 cur = text ? (nj_s4)nj_string_len(text) : 0;
    nj_u2 *buf;

    if (off < 0 || len < 0 || off + len > cur) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        return NJ_OK;
    }
    buf = (nj_u2 *)nj_host_alloc(vm, ((nj_size)cur + 1u) * sizeof(nj_u2));
    if (!buf) {
        return nj_throw_oom(vm);
    }
    if (off > 0) {
        memcpy(buf, nj_string_chars(text), (nj_size)off * sizeof(nj_u2));
    }
    if (cur > off + len) {
        memcpy(buf + off, nj_string_chars(text) + off + len,
               (nj_size)(cur - off - len) * sizeof(nj_u2));
    }
    {
        nj_status st = tb_sync(vm, self, buf, (int)(cur - len));
        nj_host_free(vm, buf);
        if (st != NJ_OK) {
            return st;
        }
    }
    if (fget(vm, self, NJ_C_TEXTBOX, "tbCursor", D_INT) > off) {
        fput(vm, self, NJ_C_TEXTBOX, "tbCursor", D_INT, off);
    }
    return NJ_OK;
}

NJFN(tb_setTicker)
{
    return d_setTicker(vm, th, args, ret);
}

NJFN(tb_getTicker)
{
    return d_getTicker(vm, th, args, ret);
}

/* TextBox.getCaretPosition(): the insertion point the text machinery keeps in
 * tbCursor, or 0 when the field was never written. */
NJFN(tb_getCaretPosition)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_TEXTBOX, "tbCursor", D_INT);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Alert and AlertType                                                 */
/* ------------------------------------------------------------------ */
static nj_status alert_init_on(nj_vm *vm, nj_ref self, nj_value *args)
{
    nj_ref title = NJ_ARG_REF(args, 0);
    nj_ref text = NJ_ARG_REF(args, 1);
    nj_ref img = NJ_ARG_REF(args, 2);
    nj_ref type = NJ_ARG_REF(args, 3);

    d_init_on(vm, self);
    rput(vm, self, NJ_C_DISPLAYABLE, F_D_TITLE, D_STR, title);
    rput(vm, self, NJ_C_ALERT, F_AL_STRING, D_STR, text);
    rput(vm, self, NJ_C_ALERT, F_AL_IMAGE, D_IMAGE, img);
    rput(vm, self, NJ_C_ALERT, F_AL_TYPE,
         "Ljavax/microedition/lcdui/AlertType;", type);
    fput(vm, self, NJ_C_ALERT, F_AL_TIMEOUT, D_INT, 2000);
    fput(vm, self, NJ_C_ALERT, F_AL_INDEF, D_BOOL, 0);
    rput(vm, self, NJ_C_ALERT, "alBuffer", D_IMAGE, NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(alert_init1)
{
    nj_value a[4];
    a[0].r = NJ_ARG_REF(args, 0);
    a[1].r = NULL;
    a[2].r = NULL;
    a[3].r = NULL;
    return alert_init_on(vm, NJ_THIS(args), a);
}

NJFN(alert_init4)
{
    return alert_init_on(vm, NJ_THIS(args), args);
}

NJFN(alert_getType)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_ALERT, F_AL_TYPE,
                  "Ljavax/microedition/lcdui/AlertType;");
    return NJ_OK;
}

NJFN(alert_setType)
{
    nj_ref self = NJ_THIS(args);
    nj_ref t = NJ_ARG_REF(args, 0);

    if (!t) {
        return nj_throw(vm, "java/lang/NullPointerException", "type is null");
    }
    rput(vm, self, NJ_C_ALERT, F_AL_TYPE,
         "Ljavax/microedition/lcdui/AlertType;", t);
    return NJ_OK;
}

NJFN(alert_getString)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_ALERT, F_AL_STRING, D_STR);
    return NJ_OK;
}

NJFN(alert_setString)
{
    rput(vm, NJ_THIS(args), NJ_C_ALERT, F_AL_STRING, D_STR,
         NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(alert_getImage)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_ALERT, F_AL_IMAGE, D_IMAGE);
    return NJ_OK;
}

NJFN(alert_setImage)
{
    rput(vm, NJ_THIS(args), NJ_C_ALERT, F_AL_IMAGE, D_IMAGE,
         NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(alert_getTimeout)
{
    ret->i = fget(vm, NJ_THIS(args), NJ_C_ALERT, F_AL_TIMEOUT, D_INT);
    return NJ_OK;
}

NJFN(alert_setTimeout)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 t = NJ_ARG_INT(args, 0);

    if (t == 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "timeout must not be zero");
    }
    fput(vm, self, NJ_C_ALERT, F_AL_TIMEOUT, D_INT, t);
    fput(vm, self, NJ_C_ALERT, F_AL_INDEF, D_BOOL, (t < 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(alert_getDefaultTimeout)
{
    ret->i = sget(vm, NJ_C_ALERT, "sDefaultTimeout", D_INT);
    return NJ_OK;
}

NJFN(alert_setDefaultTimeout)
{
    nj_s4 t = NJ_ARG_INT(args, 0);
    /* -2 and below are reserved; -1 and 0.. are accepted, as on a device. */
    if (t < -1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "illegal default timeout");
    }
    sput(vm, NJ_C_ALERT, "sDefaultTimeout", D_INT, t);
    return NJ_OK;
}

NJFN(alert_setIndicator)
{
    nj_ref self = NJ_THIS(args);
    nj_ref g = NJ_ARG_REF(args, 0);

    if (g) {
        if (!isa(vm, g, NJ_C_GAUGE)) {
            return nj_throw(vm, "java/lang/IllegalArgumentException",
                            "indicator must be a Gauge");
        }
        if (fget(vm, g, NJ_C_GAUGE, "gaInteractive", D_BOOL)) {
            return nj_throw(vm, "java/lang/IllegalArgumentException",
                            "indicator must be a non-interactive Gauge");
        }
    }
    rput(vm, self, NJ_C_ALERT, "alIndicator", NJ_C_GAUGE, g);
    return NJ_OK;
}

NJFN(alert_getIndicator)
{
    ret->r = rget(vm, NJ_THIS(args), NJ_C_ALERT, "alIndicator", NJ_C_GAUGE);
    return NJ_OK;
}

NJFN(alerttype_init)
{
    nj_ref self = NJ_THIS(args);
    fput(vm, self, NJ_C_ALERTTYPE, "atType", D_INT, NJ_ARG_INT(args, 0));
    return NJ_OK;
}

/* The no-argument form the compile stubs synthesise: an INFO alert type. */
NJFN(alerttype_init0)
{
    nj_ref self = NJ_THIS(args);
    fput(vm, self, NJ_C_ALERTTYPE, "atType", D_INT, 0);
    return NJ_OK;
}

NJFN(alerttype_getTypeName)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 t;

    /* The MIDP 2.0 form is static and takes the type; the instance form is
     * kept because the squash-court renderer and older MIDlets use it. */
    if (NJ_ARG_REF(args, 0)) {
        self = NJ_ARG_REF(args, 0);
    }
    t = fget(vm, self, NJ_C_ALERTTYPE, "atType", D_INT);
    {
        const char *name0;

    switch ((int)t) {
    case 1: name0 = "WARNING"; break;
    case 2: name0 = "ERROR"; break;
    case 3: name0 = "ALARM"; break;
    case 4: name0 = "CONFIRMATION"; break;
    default: name0 = "INFO"; break;
    }
    ret->r = nj_string_new(vm, name0);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
    }
}

/*
 * AlertType.playSound(Display) must never fail: a device without audio simply
 * hears nothing.  When the port offers tone playback the alert gets a short
 * pattern, and a missing port is not an error.
 */
NJFN(alerttype_playSound)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 t = fget(vm, self, NJ_C_ALERTTYPE, "atType", D_INT);
    nj_audio_tone tones[3];

    NJ_UNUSED(args);
    if (!vm->port || !vm->port->tone_play) {
        return NJ_OK;
    }
    switch ((int)t) {
    case 1:   /* WARNING */
        tones[0].freq_hz = 440; tones[0].duration_ms = 120;
        tones[1].freq_hz = 0;   tones[1].duration_ms = 60;
        tones[2].freq_hz = 440; tones[2].duration_ms = 120;
        return (vm->port->tone_play(vm->port->ud, tones, 3) == 0) ? NJ_OK : NJ_OK;
    case 2:   /* ERROR */
        tones[0].freq_hz = 220; tones[0].duration_ms = 400;
        return (vm->port->tone_play(vm->port->ud, tones, 1) == 0) ? NJ_OK : NJ_OK;
    case 3:   /* ALARM */
        tones[0].freq_hz = 880; tones[0].duration_ms = 200;
        tones[1].freq_hz = 0;   tones[1].duration_ms = 100;
        tones[2].freq_hz = 880; tones[2].duration_ms = 200;
        return (vm->port->tone_play(vm->port->ud, tones, 3) == 0) ? NJ_OK : NJ_OK;
    case 4:   /* CONFIRMATION */
        tones[0].freq_hz = 660; tones[0].duration_ms = 80;
        tones[1].freq_hz = 990; tones[1].duration_ms = 120;
        return (vm->port->tone_play(vm->port->ud, tones, 2) == 0) ? NJ_OK : NJ_OK;
    default:  /* INFO */
        tones[0].freq_hz = 1046; tones[0].duration_ms = 100;
        return (vm->port->tone_play(vm->port->ud, tones, 1) == 0) ? NJ_OK : NJ_OK;
    }
}

/* ================================================================== */
/* Class tables                                                        */
/* ================================================================== */
/* ------------------------------------------------------------------ */
/* com.nokia.mid.ui.DirectGraphics                                     */
/* ------------------------------------------------------------------ */
/*
 * Nokia's DirectGraphics is an INTERFACE and twelve of the titles collected
 * under tests/games call it.  The interface itself is declared next door in
 * nj_lib_nokia.c; the METHODS live here for two reasons.  This is the file
 * that can reach the nj_gfx primitives, and invokeinterface dispatches on the
 * receiver's runtime class - so the object DirectUtils.getDirectGraphics()
 * hands back is the Graphics itself, and for the call to resolve the methods
 * have to be on Graphics.
 *
 * Only these seven exist, because only these seven are used anywhere in the
 * corpus.  Inventing the rest of Nokia's API would be guesswork.
 */
#define NJ_DG_PIXFMT_RGB565 10   /* Nokia's TYPE_USHORT_565_RGB */

NJFN(dg_setARGBColor)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    /*
     * The name says ARGB, but a zero alpha would draw nothing at all on a
     * surface, and a game that writes 0x00FF0000 means red - the same trap
     * setColor already documents.  Opaque it is.
     */
    nj_gfx_set_color(&g, (nj_u4)NJ_ARG_INT(args, 0) | 0xFF000000u);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(dg_getNativePixelFormat)
{
    ret->i = NJ_DG_PIXFMT_RGB565;
    return NJ_OK;
}

/*
 * drawImage(Image, x, y, anchor, manipulation).  The manipulation argument is
 * accepted and ignored: this port draws through the same code as the MIDP
 * method, which has no rotation.  Saying so here is better than pretending,
 * and a game that asks for ROTATE_90 gets an unrotated image rather than an
 * exception it cannot catch.
 */
NJFN(dg_drawImage)
{
    /*
     * drawImage(Image, x, y, anchor, manipulation) - the Nokia form, which
     * adds a manipulation argument to the standard method.
     *
     * This forwarded through nj_call with a FOUR-slot argument array.  The
     * callee takes five (receiver, image, x, y, anchor), so it read one slot
     * past the end: a corpus title that called it ended up dispatching on a
     * receiver that was really the caller's own stack word, and the emulator
     * segfaulted three frames later inside nj_method_lookup.  Call the
     * implementation with the array it actually wants, and with the receiver
     * and the parameters in the slots it expects.
     */
    nj_value a[5];

    a[0] = args[0];     /* the Graphics */
    a[1] = args[1];     /* the Image */
    a[2] = args[2];     /* x */
    a[3] = args[3];     /* y */
    a[4] = args[4];     /* anchor; args[5], the manipulation, is ignored */
    return nj_call(vm, NJ_THIS(args), NJ_C_GRAPHICS, "drawImage",
                   "(L" NJ_C_IMAGE ";III)V", a, NULL);
}

NJFN(dg_fillTriangle)
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

/*
 * A polygon, filled a row at a time.
 *
 * nj_gfx has no polygon fill and does not need one: a scanline that calls
 * nj_gfx_line once per row inherits the existing clipping, colour and blend
 * rules instead of reimplementing them.  Slow and obvious is the right trade
 * for something a game calls a handful of times a frame.
 */
static void dg_fill_poly(nj_gfx *g,
                         const nj_s4 *xs, const nj_s4 *ys, nj_s4 n)
{
    nj_s4 y, ymin, ymax;

    if (n < 3) {
        return;
    }
    ymin = ymax = ys[0];
    for (y = 1; y < n; y++) {
        if (ys[y] < ymin) { ymin = ys[y]; }
        if (ys[y] > ymax) { ymax = ys[y]; }
    }
    for (y = ymin; y <= ymax; y++) {
        nj_s4 x0 = 0, x1 = 0;
        int have = 0, i, j;

        for (i = 0, j = n - 1; i < n; j = i++) {
            nj_s4 ya = ys[j], yb = ys[i];
            if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
                nj_s4 x = xs[j] + (nj_s4)(((nj_s8)(y - ya) *
                                           (xs[i] - xs[j])) / (yb - ya));
                if (!have) { x0 = x1 = x; have = 1; }
                else if (x < x0) { x0 = x; }
                else if (x > x1) { x1 = x; }
            }
        }
        if (have && x1 >= x0) {
            nj_gfx_line(g, x0, y, x1, y);
        }
    }
}

/* Shared argument marshalling for the two polygon calls. */
static int dg_poly_args(nj_vm *vm, nj_value *args, nj_gfx *g,
                        nj_s4 **xs_out, nj_s4 **ys_out, nj_s4 *n_out)
{
    nj_ref xa = NJ_ARG_REF(args, 0);
    nj_s4 xoff = NJ_ARG_INT(args, 1);
    nj_ref ya = NJ_ARG_REF(args, 2);
    nj_s4 yoff = NJ_ARG_INT(args, 3);
    nj_s4 n = NJ_ARG_INT(args, 4);
    nj_s4 xlen = xa ? (nj_s4)xa->length : 0;
    nj_s4 ylen = ya ? (nj_s4)ya->length : 0;

    if (!xa || !ya) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL) == NJ_OK;
    }
    if (xoff < 0 || yoff < 0 || n < 0 || xoff + n > xlen || yoff + n > ylen) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                        NULL) == NJ_OK;
    }
    *xs_out = (nj_s4 *)xa->u.bytes + xoff;
    *ys_out = (nj_s4 *)ya->u.bytes + yoff;
    *n_out = n;
    (void)g;
    return 1;
}

NJFN(dg_fillPolygon)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;
    nj_s4 *xs = NULL, *ys = NULL, n = 0;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    if (!dg_poly_args(vm, args, &g, &xs, &ys, &n)) {
        return NJ_ERR;
    }
    nj_gfx_set_color(&g, (nj_u4)NJ_ARG_INT(args, 5) | 0xFF000000u);
    dg_fill_poly(&g, xs, ys, n);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

NJFN(dg_drawPolygon)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;
    nj_s4 *xs = NULL, *ys = NULL, n = 0;
    nj_s4 i;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    if (!dg_poly_args(vm, args, &g, &xs, &ys, &n)) {
        return NJ_ERR;
    }
    nj_gfx_set_color(&g, (nj_u4)NJ_ARG_INT(args, 5) | 0xFF000000u);
    for (i = 0; i < n; i++) {
        nj_s4 j = (i + 1 < n) ? i + 1 : 0;
        nj_gfx_line(&g, xs[i], ys[i], xs[j], ys[j]);
    }
    gfx_store(vm, self, &g);
    return NJ_OK;
}

/*
 * drawPixels(short[], transparency, offset, scanlength, x, y, w, h,
 *            manipulation, format).  One nj_gfx_pixel per pixel: a game that
 * gets a correct picture slowly beats one that gets an exception, and this is
 * not on the hot path for anything in the corpus.
 */
NJFN(dg_drawPixels)
{
    nj_ref self = NJ_THIS(args);
    nj_ref px = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 2);
    nj_s4 scan = NJ_ARG_INT(args, 3);
    nj_s4 x = NJ_ARG_INT(args, 4);
    nj_s4 y = NJ_ARG_INT(args, 5);
    nj_s4 w = NJ_ARG_INT(args, 6);
    nj_s4 h = NJ_ARG_INT(args, 7);
    nj_gfx g;
    nj_surface sc;
    nj_s4 row, col;
    const nj_u2 *pix;

    if (!px) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    pix = (const nj_u2 *)px->u.bytes;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            nj_s4 idx = off + row * scan + col;
            nj_u4 v;
            if (idx < 0 || (nj_u4)idx >= px->length) {
                continue;
            }
            v = pix[idx];
            /* 565 in, 888 out: the same expansion RGB565 images already use. */
            nj_gfx_set_color(&g,
                             0xFF000000u |
                             (((v >> 11) & 0x1Fu) << 19) |
                             (((v >> 5) & 0x3Fu) << 10) |
                             ((v & 0x1Fu) << 3));
            nj_gfx_pixel(&g, x + col, y + row);
        }
    }
    gfx_store(vm, self, &g);
    return NJ_OK;
}

/*
 * getPixels is the one method here that CANNOT be honest yet: reading pixels
 * back needs a surface read primitive that nj_gfx does not expose.  Leaving
 * the caller's array untouched is better than throwing into a game's paint()
 * where it cannot be caught, but it is a real gap and it is written down here
 * rather than hidden.
 */
/*
 * DirectGraphics.getPixels() - reading the target back.
 *
 * This used to be a deliberate no-op, because nj_gfx had no way to read a
 * surface.  nj_surface_get()/nj_surface_format_color() exist, so it is a
 * no-op no longer: a game that reads the framebuffer to detect a collision or
 * to fade out gets the pixels it drew.
 *
 * `format` follows Nokia: TYPE_INT_8888_ARGB (12) and TYPE_INT_888_RGB (11)
 * are honoured, TYPE_USHORT_565_RGB (10) and anything else get the packed
 * 565 value, which is what the surface holds anyway.
 */
#define NJ_DG_TYPE_INT_888_RGB    11
#define NJ_DG_TYPE_INT_8888_ARGB  12

/* elem is 2 for the short[] overload and 4 for the int[] one. */
static nj_status dg_get_pixels(nj_vm *vm, nj_value *args, nj_value *ret,
                               int elem)
{
    nj_ref self = NJ_THIS(args);
    nj_ref dest = NJ_ARG_REF(args, 0);
    nj_s4 offset = NJ_ARG_INT(args, 1);
    nj_s4 scan = NJ_ARG_INT(args, 2);
    nj_s4 x = NJ_ARG_INT(args, 3);
    nj_s4 y = NJ_ARG_INT(args, 4);
    nj_s4 w = NJ_ARG_INT(args, 5);
    nj_s4 h = NJ_ARG_INT(args, 6);
    nj_s4 fmt = NJ_ARG_INT(args, 7);
    nj_gfx g;
    nj_surface sc;
    nj_s4 j, i;

    (void)ret;
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    if (!dest) {
        NJ_THROW(vm, "java/lang/NullPointerException",
                 "the destination array is null");
    }
    if (w <= 0 || h <= 0) {
        return NJ_OK;
    }
    if (offset < 0 || scan < w) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "offset and scanlength are too small for the pixels asked for");
    }
    /* The last element written is offset + (h-1)*scan + (w-1). */
    if ((nj_s8)offset + (nj_s8)(h - 1) * scan + (w - 1)
        >= (nj_s8)dest->length) {
        NJ_THROW(vm, "java/lang/ArrayIndexOutOfBoundsException",
                 "the destination array is too small for the pixels asked for");
    }
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            nj_u4 argb = nj_surface_format_color(sc.format,
                                                 nj_surface_get(&sc, x + i,
                                                                y + j));
            nj_s4 v;
            nj_s4 at = offset + j * scan + i;

            if (fmt == NJ_DG_TYPE_INT_8888_ARGB) {
                v = (nj_s4)argb;
            } else if (fmt == NJ_DG_TYPE_INT_888_RGB) {
                v = (nj_s4)(argb & 0xFFFFFFu);
            } else {
                v = (nj_s4)(((argb >> 19) & 0x1Fu) << 11
                            | ((argb >> 10) & 0x3Fu) << 5
                            | ((argb >> 3) & 0x1Fu));
            }
            if (elem == 2) {
                ((nj_u2 *)dest->u.bytes)[at] = (nj_u2)v;
            } else {
                ((nj_s4 *)dest->u.bytes)[at] = v;
            }
        }
    }
    return NJ_OK;
}

NJFN(dg_getPixels)
{
    return dg_get_pixels(vm, args, ret, 2);
}

NJFN(dg_getPixelsInt)
{
    return dg_get_pixels(vm, args, ret, 4);
}

/*
 * drawTriangle(x1, y1, x2, y2, x3, y3, argb): the outline of a triangle in the
 * direct colour.  Three lines, because that is what the picture is - the filled
 * form is fillTriangle() above.
 */
NJFN(dg_drawTriangle)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 x1 = NJ_ARG_INT(args, 0);
    nj_s4 y1 = NJ_ARG_INT(args, 1);
    nj_s4 x2 = NJ_ARG_INT(args, 2);
    nj_s4 y2 = NJ_ARG_INT(args, 3);
    nj_s4 x3 = NJ_ARG_INT(args, 4);
    nj_s4 y3 = NJ_ARG_INT(args, 5);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    nj_gfx_set_color(&g, (nj_u4)NJ_ARG_INT(args, 6));
    nj_gfx_line(&g, x1, y1, x2, y2);
    nj_gfx_line(&g, x2, y2, x3, y3);
    nj_gfx_line(&g, x3, y3, x1, y1);
    gfx_store(vm, self, &g);
    return NJ_OK;
}

/*
 * drawPixels(int[], boolean, offset, scanlength, x, y, w, h, manipulation,
 * format): the int[] overload of the call above.  The format says how to read
 * each int - 8888 is ARGB, 888 is RGB, anything else is a packed 565 value,
 * which is what the surface holds anyway.
 */
NJFN(dg_drawPixelsInt)
{
    nj_ref self = NJ_THIS(args);
    nj_ref px = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 2);
    nj_s4 scan = NJ_ARG_INT(args, 3);
    nj_s4 x = NJ_ARG_INT(args, 4);
    nj_s4 y = NJ_ARG_INT(args, 5);
    nj_s4 w = NJ_ARG_INT(args, 6);
    nj_s4 h = NJ_ARG_INT(args, 7);
    nj_s4 fmt = NJ_ARG_INT(args, 9);
    nj_gfx g;
    nj_surface sc;
    nj_s4 row, col;
    const nj_s4 *pix;

    if (!px) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    pix = (const nj_s4 *)px->u.bytes;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            nj_s4 idx = off + row * scan + col;
            nj_u4 v;
            if (idx < 0 || (nj_u4)idx >= px->length) {
                continue;
            }
            v = (nj_u4)pix[idx];
            if (fmt == NJ_DG_TYPE_INT_8888_ARGB) {
                /* as given */
            } else if (fmt == NJ_DG_TYPE_INT_888_RGB) {
                v |= 0xFF000000u;
            } else {
                v = 0xFF000000u
                    | (((v >> 11) & 0x1Fu) << 19)
                    | (((v >> 5) & 0x3Fu) << 10)
                    | ((v & 0x1Fu) << 3);
            }
            nj_gfx_set_color(&g, v);
            nj_gfx_pixel(&g, x + col, y + row);
        }
    }
    gfx_store(vm, self, &g);
    return NJ_OK;
}

/*
 * drawPixels(byte[] pixels, byte[] transparency, offset, scanlength, x, y, w,
 * h, manipulation, format): the same call with one byte per pixel.  Nokia's
 * byte formats are grey scales and palettes, and this runtime keeps no palette
 * state, so a byte is drawn as the grey level of the same value: exact for the
 * grey formats, visibly wrong for a palette - the same trade the short[]
 * overload above makes, and better than an exception in the middle of a frame.
 */
NJFN(dg_drawPixelsBytes)
{
    nj_ref self = NJ_THIS(args);
    nj_ref px = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 2);
    nj_s4 scan = NJ_ARG_INT(args, 3);
    nj_s4 x = NJ_ARG_INT(args, 4);
    nj_s4 y = NJ_ARG_INT(args, 5);
    nj_s4 w = NJ_ARG_INT(args, 6);
    nj_s4 h = NJ_ARG_INT(args, 7);
    nj_gfx g;
    nj_surface sc;
    nj_s4 row, col;
    const nj_u1 *pix;

    if (!px) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    pix = (const nj_u1 *)px->u.bytes;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            nj_s4 idx = off + row * scan + col;
            nj_u1 v;
            if (idx < 0 || (nj_u4)idx >= px->length) {
                continue;
            }
            v = pix[idx];
            nj_gfx_set_color(&g, 0xFF000000u | ((nj_u4)v << 16)
                             | ((nj_u4)v << 8) | (nj_u4)v);
            nj_gfx_pixel(&g, x + col, y + row);
        }
    }
    gfx_store(vm, self, &g);
    return NJ_OK;
}

/*
 * getPixels(byte[] pixels, byte[] transparency, offset, scanlength, x, y, w,
 * h, format): the read side of the byte overload, answering the grey level of
 * each pixel (Rec. 601 weights), which is what a byte-format title asked for.
 */
static nj_status dg_get_pixels_bytes(nj_vm *vm, nj_value *args)
{
    nj_ref self = NJ_THIS(args);
    nj_ref dest = NJ_ARG_REF(args, 0);
    nj_s4 offset = NJ_ARG_INT(args, 2);
    nj_s4 scan = NJ_ARG_INT(args, 3);
    nj_s4 x = NJ_ARG_INT(args, 4);
    nj_s4 y = NJ_ARG_INT(args, 5);
    nj_s4 w = NJ_ARG_INT(args, 6);
    nj_s4 h = NJ_ARG_INT(args, 7);
    nj_gfx g;
    nj_surface sc;
    nj_s4 j, i;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    if (!dest) {
        NJ_THROW(vm, "java/lang/NullPointerException",
                 "the destination array is null");
    }
    if (w <= 0 || h <= 0) {
        return NJ_OK;
    }
    if (offset < 0 || scan < w) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "offset and scanlength are too small for the pixels asked for");
    }
    if ((nj_s8)offset + (nj_s8)(h - 1) * scan + (w - 1)
        >= (nj_s8)dest->length) {
        NJ_THROW(vm, "java/lang/ArrayIndexOutOfBoundsException",
                 "the destination array is too small for the pixels asked for");
    }
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            nj_u4 argb = nj_surface_format_color(sc.format,
                                                 nj_surface_get(&sc, x + i,
                                                                y + j));
            nj_u4 grey = ((argb >> 16) & 0xFFu) * 77u
                       + ((argb >> 8) & 0xFFu) * 151u
                       + (argb & 0xFFu) * 28u;
            ((nj_u1 *)dest->u.bytes)[offset + j * scan + i] = (nj_u1)(grey >> 8);
        }
    }
    return NJ_OK;
}

NJFN(dg_getPixelsBytes)
{
    return dg_get_pixels_bytes(vm, args);
}

/*
 * getAlphaComponent(): the alpha channel the direct colour draws with.  The
 * DirectGraphics colour is ARGB, so this is its top byte - a game saves the
 * blend it had and puts it back afterwards.
 */
NJFN(dg_getAlphaComponent)
{
    nj_ref self = NJ_THIS(args);
    nj_gfx g;
    nj_surface sc;

    if (!gfx_load(vm, self, &g, &sc)) {
        return NJ_ERR;
    }
    ret->i = (nj_s4)((nj_gfx_get_color(&g) >> 24) & 0xFFu);
    return NJ_OK;
}

#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

/* ---- Image ---- */
static const nj_builtin_field image_fields[] = {
    FLD(F_IMG_W,    D_INT,    0),
    FLD(F_IMG_H,    D_INT,    0),
    FLD(F_IMG_PX,   D_INTARR, 0),
    FLD(F_IMG_MUT,  D_BOOL,   0)
};

static const nj_builtin_method image_methods[] = {
    M("<init>", "()V", 0),
    M("createImage", "(II)L" NJ_C_IMAGE ";",                   NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("createImage", "([BII)L" NJ_C_IMAGE ";",                 NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("createImage", "([BIII)L" NJ_C_IMAGE ";",                NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("createImage", "(Ljava/lang/String;)L" NJ_C_IMAGE ";",   NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("createImage", "(L" NJ_C_IMAGE ";)L" NJ_C_IMAGE ";", NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("createImage", "(Ljava/io/InputStream;)L" NJ_C_IMAGE ";", NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("createImage", "(IIIIII)L" NJ_C_IMAGE ";", NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("createImage", "(L" NJ_C_IMAGE ";IIIII)L" NJ_C_IMAGE ";", NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("createRGBImage", "([IIIZ)L" NJ_C_IMAGE ";",             NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("getWidth",  "()I",  0),
    M("getHeight", "()I",  0),
    M("isMutable", "()Z",  0),
    M("getGraphics", "()L" NJ_C_GRAPHICS ";", 0),
    M("getRGB", "([IIIIIII)V", 0)
};

/* ---- Font ---- */
static const nj_builtin_field font_fields[] = {
    FLD("sCache",   D_OBJARR, NJ_ACC_STATIC),
    FLD("sDefault", D_FONT,   NJ_ACC_STATIC),
    FLD(F_FONT_FACE,  D_INT,  0),
    FLD(F_FONT_STYLE, D_INT,  0),
    FLD(F_FONT_SIZE,  D_INT,  0),
    /* The MIDP face/style/size constants are static finals, filled in by
     * nj_lcdui_late_init() because a builtin class has no ConstantValue. */
    FLD("FACE_SYSTEM",       D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("FACE_MONOSPACED",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("FACE_MONOSPACE",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("FACE_PROPORTIONAL", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("STYLE_PLAIN",       D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("STYLE_BOLD",        D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("STYLE_ITALIC",      D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("STYLE_UNDERLINED",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("SIZE_SMALL",        D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("SIZE_MEDIUM",       D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("SIZE_LARGE",        D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method font_methods[] = {
    M("<init>", "()V", 0),
    M("getFont",          "(I)L" NJ_C_FONT ";",         NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("getFont",          "(III)L" NJ_C_FONT ";",       NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("getDefaultFont",   "()L" NJ_C_FONT ";",          NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("getFace",          "()I", 0),
    M("getStyle",         "()I", 0),
    M("getSize",          "()I", 0),
    M("isPlain",          "()Z", 0),
    M("isBold",           "()Z", 0),
    M("isItalic",         "()Z", 0),
    M("isUnderlined",     "()Z", 0),
    M("getHeight",        "()I", 0),
    M("getBaselinePosition", "()I", 0),
    M("getAscent",        "()I", 0),
    M("getDescent",       "()I", 0),
    M("charWidth",        "(C)I", 0),
    M("charsWidth",       "([CII)I", 0),
    M("stringWidth",      "(Ljava/lang/String;)I", 0),
    M("substringWidth",   "(Ljava/lang/String;II)I", 0)
};

/* ---- Graphics ---- */
static const nj_builtin_field graphics_fields[] = {
    FLD(F_G_TGT,    D_IMAGE, 0),
    FLD(F_G_TX,     D_INT, 0),
    FLD(F_G_TY,     D_INT, 0),
    FLD(F_G_CX,     D_INT, 0),
    FLD(F_G_CY,     D_INT, 0),
    FLD(F_G_CW,     D_INT, 0),
    FLD(F_G_CH,     D_INT, 0),
    FLD(F_G_COLOR,  D_INT, 0),
    FLD(F_G_BLEND,  D_INT, 0),
    FLD(F_G_STROKE, D_INT, 0),
    FLD(F_G_FONT,   D_FONT, 0),
    FLD(F_G_XOR,    D_INT, 0),
    FLD(F_G_XORC,   D_INT, 0),
    /* MIDP 2.0 constants. */
    FLD("HCENTER",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("VCENTER",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LEFT",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("RIGHT",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TOP",      D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("BOTTOM",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("BASELINE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("SOLID",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("DOTTED",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method graphics_methods[] = {
    /*
     * Two constructors.  (Image) is the one Image.getGraphics() and the paint
     * machinery use.  ()V exists because a Graphics is a serialised nj_gfx and
     * the game package builds one field by field for a Canvas buffer; it leaves
     * gTarget null, which means "the display".
     */
    M("<init>", "(L" NJ_C_IMAGE ";)V", 0),
    M("<init>", "()V", 0),
    M("translate",      "(II)V", 0),
    M("getTranslateX",  "()I", 0),
    M("getTranslateY",  "()I", 0),
    M("setClip",        "(IIII)V", 0),
    M("clipRect",       "(IIII)V", 0),
    M("getClipX",       "()I", 0),
    M("getClipY",       "()I", 0),
    M("getClipWidth",   "()I", 0),
    M("getClipHeight",  "()I", 0),
    M("setColor",       "(I)V", 0),
    M("setColor",       "(III)V", 0),
    M("getColor",       "()I", 0),
    M("getRedComponent",   "()I", 0),
    M("getGreenComponent", "()I", 0),
    M("getBlueComponent",  "()I", 0),
    M("getAlphaComponent", "()I", 0),
    M("setAlpha",       "(I)V", 0),
    M("getAlpha",       "()I", 0),
    M("getGrayScale",   "()I", 0),
    M("setGrayScale",   "(I)V", 0),
    M("setBlendMode",   "(I)V", 0),
    M("getBlendMode",   "()I", 0),
    M("setStrokeStyle", "(I)V", 0),
    M("getStrokeStyle", "()I", 0),
    M("setFont",        "(L" NJ_C_FONT ";)V", 0),
    M("getFont",        "()L" NJ_C_FONT ";", 0),
    M("setXORMode",     "(Z)V", 0),
    M("setXORMode",     "(L" NJ_C_GRAPHICS ";)V", 0),
    M("drawLine",       "(IIII)V", 0),
    M("drawRect",       "(IIII)V", 0),
    M("fillRect",       "(IIII)V", 0),
    M("drawRoundRect",  "(IIIIII)V", 0),
    M("fillRoundRect",  "(IIIIII)V", 0),
    M("drawArc",        "(IIIIII)V", 0),
    M("fillArc",        "(IIIIII)V", 0),
    M("drawTriangle",   "(IIIIII)V", 0),
    M("fillTriangle",   "(IIIIII)V", 0),
    M("drawString",     "(Ljava/lang/String;III)V", 0),
    M("drawString",     "(Ljava/lang/String;II)V", 0),
    M("drawSubstring",  "(Ljava/lang/String;IIIII)V", 0),
    M("drawChars",      "([CIIIII)V", 0),
    M("drawChar",       "(CIII)V", 0),
    M("drawImage",      "(L" NJ_C_IMAGE ";III)V", 0),
    /* MIDP 2.0: drawRegion(Image, x_src, y_src, w, h, transform, x_dest,
     * y_dest, anchor).  The compile stubs also declare a 10-argument variant;
     * it is accepted and treated as the same operation. */
    M("drawRegion",     "(L" NJ_C_IMAGE ";IIIIIIII)V", 0),
    M("drawRegion",     "(L" NJ_C_IMAGE ";IIIIIIIII)V", 0),
    M("drawRGB",        "([IIIIIIIZ)V", 0),
    M("copyArea",       "(IIIIIII)V", 0),
    M("getDisplayColor", "(I)I", 0),

    /* com.nokia.mid.ui.DirectGraphics, reached through the interface. */
    M("setARGBColor",         "(I)V",   NJ_ACC_PUBLIC),
    M("getNativePixelFormat", "()I",    NJ_ACC_PUBLIC),
    M("drawImage",  "(L" NJ_C_IMAGE ";IIII)V", NJ_ACC_PUBLIC),
    M("fillTriangle", "(IIIIIII)V",     NJ_ACC_PUBLIC),
    M("drawTriangle", "(IIIIIII)V",     NJ_ACC_PUBLIC),
    M("drawPolygon", "([II[IIII)V",     NJ_ACC_PUBLIC),
    M("fillPolygon", "([II[IIII)V",     NJ_ACC_PUBLIC),
    M("drawPixels",  "([SZIIIIIIII)V",  NJ_ACC_PUBLIC),
    M("getPixels",   "([SIIIIIII)V",    NJ_ACC_PUBLIC),
    M("getPixels",      "([IIIIIIII)V", NJ_ACC_PUBLIC),
    M("drawPixels",        "([IZIIIIIIII)V", NJ_ACC_PUBLIC),
    M("drawPixels",        "([B[BIIIIIIII)V", NJ_ACC_PUBLIC),
    M("getPixels",         "([B[BIIIIIII)V",  NJ_ACC_PUBLIC),
    M("getAlphaComponent", "()I",              NJ_ACC_PUBLIC),
};

/* ---- Displayable / Canvas ---- */
/*
 * The paint-target storage lives on DISPLAYABLE, not on Canvas.
 *
 * The frame painter needs somewhere to compose a Form, List, TextBox or Alert
 * before blitting it to the screen, and it used to call the Canvas-scoped
 * accessor for all of them.  On anything that is not a Canvas the field lookup
 * fails, the write goes nowhere, and the painter allocates a fresh
 * screen-sized image EVERY FRAME - 307 KB per frame at 240x320x4.  The heap
 * thrashes, img_alloc eventually fails, nj_midp_paint returns an error and the
 * displayable is never presented at all: a blank screen with no exception
 * anywhere.  That is exactly how maze.jar (whose first screen is a List) failed
 * while every unit test, which renders once or twice, passed.
 */
static const nj_builtin_field displayable_fields[] = {
    FLD(F_C_BUFFER,   D_IMAGE,  0),
    FLD(F_C_WIDTH,    D_INT,    0),
    FLD(F_C_HEIGHT,   D_INT,    0),
    FLD(F_D_TITLE,    D_STR,    0),
    FLD(F_D_TICKER,   D_TICKER, 0),
    FLD(F_D_LISTENER, D_CL,     0),
    FLD(F_D_COMMANDS, D_VECTOR, 0),
    FLD(F_D_FULL,     D_BOOL,   0)
};

static const nj_builtin_method displayable_methods[] = {
    M("<init>",           "()V", 0),
    M("getWidth",         "()I", 0),
    M("getHeight",        "()I", 0),
    M("getTitle",         "()Ljava/lang/String;", 0),
    M("setTitle",         "(Ljava/lang/String;)V", 0),
    M("getTicker",        "()L" NJ_C_TICKER ";", 0),
    M("setTicker",        "(L" NJ_C_TICKER ";)V", 0),
    M("addCommand",       "(L" NJ_C_COMMAND ";)V", 0),
    M("removeCommand",    "(L" NJ_C_COMMAND ";)V", 0),
    M("setCommandListener", "(L" NJ_C_COMMANDLISTENER ";)V", 0),
    M("getCommandListener", "()L" NJ_C_COMMANDLISTENER ";", 0),
    M("isShown",          "()Z", 0),
    M("sizeChanged",      "(II)V", 0),
    M("getKeyName",       "(I)Ljava/lang/String;", 0),
    M("setFullScreenMode", "(Z)V", 0),
    M("isFullScreenMode", "()Z", 0)
};

static const nj_builtin_field canvas_fields[] = {
    FLD(F_C_KEYS,    D_INT,   0),
    FLD(NJ_F_C_HELD, D_INT,   0),
    FLD(F_C_VISIBLE, D_BOOL,  0),
    FLD(F_C_PRESSED, D_BOOL,  0),
    FLD("KEY_NUM0",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM1",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM2",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM3",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM4",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM5",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM6",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM7",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM8",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_NUM9",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_STAR",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_POUND",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_UP",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_DOWN",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_LEFT",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_RIGHT",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_FIRE",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_SOFTKEY1", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_SOFTKEY2", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEY_SOFTKEY3", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("UP",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("DOWN",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LEFT",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("RIGHT", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("FIRE",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("GAME_A", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("GAME_B", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("GAME_C", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("GAME_D", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

/*
 * Canvas declares the event methods ABSTRACT, exactly as MIDP specifies.  A
 * game's subclass overrides them; a Canvas whose paint() is never overridden
 * raises AbstractMethodError instead of quietly drawing nothing.
 */
static const nj_builtin_method canvas_methods[] = {
    M("<init>",            "()V", 0),
    M("paint",             "(L" NJ_C_GRAPHICS ";)V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    /*
     * The input callbacks are CONCRETE and empty in MIDP, not abstract.
     *
     * Only paint() is abstract on Canvas.  Declaring these abstract makes a
     * game that handles only, say, keyPressed fail with AbstractMethodError the
     * first time the user presses a key - which is what happened to the second
     * third-party game in the corpus.  A game overriding none of them is
     * perfectly legal and simply ignores input.
     */
    M("keyPressed",        "(I)V", NJ_ACC_PUBLIC),
    M("keyReleased",       "(I)V", NJ_ACC_PUBLIC),
    M("keyRepeated",       "(I)V", NJ_ACC_PUBLIC),
    M("pointerPressed",    "(II)V", NJ_ACC_PUBLIC),
    M("pointerReleased",   "(II)V", NJ_ACC_PUBLIC),
    M("pointerDragged",    "(II)V", NJ_ACC_PUBLIC),
    M("showNotify",        "()V", 0),
    M("hideNotify",        "()V", 0),
    M("drawNotify",        "()V", 0),
    M("getWidth",          "()I", 0),
    M("getHeight",         "()I", 0),
    M("isDoubleBuffered",  "()Z", 0),
    M("hasPointerEvents",  "()Z", 0),
    M("hasPointerMotionEvents", "()Z", 0),
    M("hasRepeatEvents",   "()Z", 0),
    /* MIDP instance methods: consume the receiver and skip it when passing
     * arguments to their native implementations. */
    M("getGameAction",     "(I)I", 0),
    M("getKeyCode",        "(I)I", 0),
    M("getKeyName",        "(I)Ljava/lang/String;", 0),
    M("repaint",           "()V", 0),
    M("repaint",           "(IIII)V", 0),
    M("serviceRepaints",   "()V", 0),
    M("setFullScreenMode", "(Z)V", 0)
};

/* ---- Command / Ticker ---- */
static const nj_builtin_field command_fields[] = {
    FLD("cmdType",     D_INT, 0),
    FLD("cmdLabel",    D_STR, 0),
    FLD("cmdPriority", D_INT, 0),
    FLD("cmdLongLabel", D_INT, 0),
    FLD("cmdLong",     D_STR, 0),
    FLD("cmdMnemonic", D_INT, 0),
    FLD("SCREEN", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("BACK",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("CANCEL", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("OK",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("HELP",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("STOP",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("EXIT",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("ITEM",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("PRIORITY_1", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("PRIORITY_2", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method command_methods[] = {
    M("<init>", "(Ljava/lang/String;II)V", 0),
    M("<init>", "(Ljava/lang/String;III)V", 0),
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;II)V", 0),
    M("getCommandType", "()I", 0),
    M("getLabel",       "()Ljava/lang/String;", 0),
    M("getLongLabel",   "()Ljava/lang/String;", 0),
    M("getPriority",    "()I", 0),
    M("compareTo",      "(L" NJ_C_COMMAND ";)I", 0)
};

static const nj_builtin_field ticker_fields[] = {
    FLD("tkString", D_STR, 0),
    FLD("tkPhase",  D_INT, 0)
};

static const nj_builtin_method ticker_methods[] = {
    M("<init>",    "(Ljava/lang/String;)V", 0),
    M("getString", "()Ljava/lang/String;", 0),
    M("setString", "(Ljava/lang/String;)V", 0)
};

/* ---- Item and friends ---- */
static const nj_builtin_field item_fields[] = {
    FLD("itLabel",       D_STR,   0),
    FLD("itLayout",      D_INT,   0),
    FLD("itPrefW",       D_INT,   0),
    FLD("itPrefH",       D_INT,   0),
    FLD("itCmdType",     D_INT,   0),
    FLD("itDefaultCmd",  D_CMD,   0),
    FLD("itCmdListener", D_ICL,   0),
    FLD("itOwner",       D_FORM,  0),
    FLD("itImage",       D_IMAGE, 0),
    FLD("itGraphics",    D_GFX,   0),
    FLD("LAYOUT_DEFAULT",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_LEFT",       D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_RIGHT",      D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_CENTER",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_TOP",        D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_BOTTOM",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_VCENTER",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_NEWLINE_BEFORE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_NEWLINE_AFTER",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_SHRINK",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_VSHRINK",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_EXPAND",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_VEXPAND",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_2",          D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method item_methods[] = {
    M("<init>", "()V", 0),
    M("getLabel",       "()Ljava/lang/String;", 0),
    M("setLabel",       "(Ljava/lang/String;)V", 0),
    M("getLayout",      "()I", 0),
    M("setLayout",      "(I)V", 0),
    M("addCommand",     "(L" NJ_C_COMMAND ";)V", 0),
    M("removeCommand",  "(L" NJ_C_COMMAND ";)V", 0),
    M("setItemCommandListener", "(L" NJ_C_ITEMCOMMANDLISTENER ";)V", 0),
    M("setPreferredSize", "(II)V", 0),
    M("getMinimumWidth",  "()I", 0),
    M("getMinimumHeight", "()I", 0),
    M("getPreferredWidth",  "()I", 0),
    M("getPreferredHeight", "()I", 0),
    M("notifyStateChanged", "()V", 0),
    M("setDefaultCommand",  "(L" NJ_C_COMMAND ";)V", 0),
    M("getDefaultCommand",  "()L" NJ_C_COMMAND ";", 0),
    /* Rendering contract: abstract, so every Item answers. */
    M("njText",   "()Ljava/lang/String;", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("njHeight", "(I)I",                 NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("njSelected", "(I)Ljava/lang/String;", 0)
};

static const nj_builtin_field stringitem_fields[] = {
    FLD("siText", D_STR, 0),
    FLD("siMode", D_INT, 0),
    FLD("siFont", D_FONT, 0),
    FLD("siCmd",  D_CMD, 0),
    FLD("siCmdListener", D_ICL, 0),
    FLD("PLAIN",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("HYPERLINK", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("BUTTON",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method stringitem_methods[] = {
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;)V", 0),
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;I)V", 0),
    M("getText", "()Ljava/lang/String;", 0),
    M("getFont", "()L" NJ_C_FONT ";", 0),
    M("setFont", "(L" NJ_C_FONT ";)V", 0),
    M("setText", "(Ljava/lang/String;)V", 0),
    M("getAppearanceMode", "()I", 0),
    M("addCommand",    "(L" NJ_C_COMMAND ";)V", 0),
    M("removeCommand", "(L" NJ_C_COMMAND ";)V", 0),
    M("setItemCommandListener", "(L" NJ_C_ITEMCOMMANDLISTENER ";)V", 0),
    M("njText",   "()Ljava/lang/String;", 0),
    M("njHeight", "(I)I", 0)
};

static const nj_builtin_field imageitem_fields[] = {
    FLD("iiImage",  D_IMAGE, 0),
    FLD("iiAlt",    D_STR,   0),
    FLD("iiAlt2",   D_STR,   0),
    FLD("iiLayout", D_INT,   0),
    FLD("iiAppearance", D_INT, 0),
    FLD("LAYOUT_DEFAULT", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_LEFT",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_RIGHT",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_CENTER",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_NEWLINE_BEFORE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LAYOUT_NEWLINE_AFTER",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method imageitem_methods[] = {
    M("<init>", "(Ljava/lang/String;L" NJ_C_IMAGE ";ILjava/lang/String;)V", 0),
    M("<init>", "(Ljava/lang/String;L" NJ_C_IMAGE ";ILjava/lang/String;I)V", 0),
    M("getImage",   "()L" NJ_C_IMAGE ";", 0),
    M("setImage",   "(L" NJ_C_IMAGE ";)V", 0),
    M("getAltText", "()Ljava/lang/String;", 0),
    M("setAltText", "(Ljava/lang/String;)V", 0),
    M("setLayout",  "(I)V", 0),
    M("getAppearanceMode", "()I", 0),
    M("njText",   "()Ljava/lang/String;", 0),
    M("njHeight", "(I)I", 0)
};

static const nj_builtin_field gauge_fields[] = {
    FLD("gaLabel",       D_STR, 0),
    FLD("gaValue",       D_INT, 0),
    FLD("gaMax",         D_INT, 0),
    FLD("gaInteractive", D_BOOL, 0),
    FLD("INDEFINITE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("CONTINUOUS_IDLE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("INCREMENTAL_IDLE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("CONTINUOUS_RUNNING", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("INCREMENTAL_UPDATING", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method gauge_methods[] = {
    M("<init>", "(Ljava/lang/String;ZII)V", 0),
    M("setValue",    "(I)V", 0),
    M("getValue",    "()I", 0),
    M("setMaxValue", "(I)V", 0),
    M("getMaxValue", "()I", 0),
    M("isInteractive", "()Z", 0),
    M("njText",   "()Ljava/lang/String;", 0),
    M("njHeight", "(I)I", 0)
};

static const nj_builtin_field choicegroup_fields[] = {
    FLD("cgLabel",    D_STR,    0),
    FLD("cgType",     D_INT,    0),
    FLD("cgStrings",  D_STRARR, 0),
    FLD("cgImages",   D_IMGARR, 0),
    FLD("cgSelected", "[Z",     0),
    FLD("cgFont",     D_FONT,   0),
    FLD("cgCount",    D_INT,    0),
    FLD("cgIndex",    D_INT,    0),
    FLD("cgFit",      D_INT,    0),
    FLD("EXCLUSIVE",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("MULTIPLE",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("IMPLICIT",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_DEFAULT", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_ON",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_OFF", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method choicegroup_methods[] = {
    M("<init>", "(Ljava/lang/String;I)V", 0),
    M("<init>", "(Ljava/lang/String;I[Ljava/lang/String;[L" NJ_C_IMAGE ";)V", 0),
    M("append", "(Ljava/lang/String;)I", 0),
    M("append", "(Ljava/lang/String;L" NJ_C_IMAGE ";)I", 0),
    M("append", "(Ljava/lang/String;L" NJ_C_IMAGE ";)V", 0),
    M("insert", "(ILjava/lang/String;)V", 0),
    M("insert", "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", 0),
    M("set",    "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", 0),
    M("delete",    "(I)V", 0),
    M("deleteAll", "()V", 0),
    M("size",      "()I", 0),
    M("getString", "(I)Ljava/lang/String;", 0),
    M("setString", "(ILjava/lang/String;)V", 0),
    M("getImage",  "(I)L" NJ_C_IMAGE ";", 0),
    M("setImage",  "(IL" NJ_C_IMAGE ";)V", 0),
    M("isSelected", "(I)Z", 0),
    M("setSelectedIndex",  "(I)V", 0),
    M("setSelectedIndex",  "(IZ)V", 0),
    M("getSelectedIndex",  "()I", 0),
    M("setSelectedFlags",  "([Z)V", 0),
    M("getSelectedFlags",  "([Z)I", 0),
    M("setFitPolicy", "(I)V", 0),
    M("getFitPolicy", "()I", 0),
    M("setFont", "(IL" NJ_C_FONT ";)V", 0),
    M("getFont", "(I)L" NJ_C_FONT ";", 0),
    M("njText",   "()Ljava/lang/String;", 0),
    M("njHeight", "(I)I", 0)
};

static const nj_builtin_field textfield_fields[] = {
    FLD("tfLabel",       D_STR,   0),
    FLD("tfText",        D_STR,   0),
    FLD("tfChars",       D_CHARR, 0),
    FLD("tfMax",         D_INT,   0),
    FLD("tfConstraints", D_INT,   0),
    FLD("tfCursor",      D_INT,   0),
    FLD("tfInputMode",   D_INT,   0),
    FLD("tfInputModeName", D_STR, 0),
    FLD("ANY",         D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("EMAILADDR",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("NUMERIC",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("PHONENUMBER", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("URL",         D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("DECIMAL",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("PASSWORD",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("UNEDITABLE",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("SENSITIVE",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("NON_PREDICTIVE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("INITIAL_CAPS_WORD", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("INITIAL_CAPS_SENTENCE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
    /* Declared TWICE here until the constant test caught it.  The lookup
     * returns the first match, so the second got its own static slot and was
     * never written - a field that reads 0 and looks fine. */
};

/*
 * javax.microedition.lcdui.Choice: an INTERFACE whose constants a MIDlet reads
 * directly (Choice.IMPLICIT), which is why it has to exist as a class even
 * though nothing implements it here - List and ChoiceGroup are separate
 * builtins with their own copies of the same numbers.
 */
static const nj_builtin_field choice_fields[] = {
    FLD("EXCLUSIVE",        D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("MULTIPLE",         D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("IMPLICIT",         D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_DEFAULT", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_ON",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_OFF",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method textfield_methods[] = {
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;II)V", 0),
    M("getString",       "()Ljava/lang/String;", 0),
    M("setString",       "(Ljava/lang/String;)V", 0),
    M("getChars",        "([C)I", 0),
    M("setChars",        "([CII)V", 0),
    M("insert",          "([CIII)V", 0),
    M("insert",          "(Ljava/lang/String;I)V", 0),
    M("delete",          "(II)V", 0),
    M("size",            "()I", 0),
    M("getMaxSize",      "()I", 0),
    M("setMaxSize",      "(I)V", 0),
    M("getConstraints",  "()I", 0),
    M("setConstraints",  "(I)V", 0),
    M("setInitialInputMode", "(Ljava/lang/String;)V", 0),
    M("getInitialInputMode", "()Ljava/lang/String;", 0),
    M("njText",   "()Ljava/lang/String;", 0),
    M("njHeight", "(I)I", 0),
    M("getCaretPosition", "()I", 0)
};

static const nj_builtin_field datefield_fields[] = {
    FLD("dfLabel",     D_STR, 0),
    FLD("dfTime",      "J",   0),
    FLD("dfZone",      "Ljava/util/TimeZone;", 0),
    FLD("dfMode",      D_INT, 0),
    FLD("dfInputMode", D_INT, 0),
    FLD("DATE",      D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TIME",      D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("DATE_TIME", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method datefield_methods[] = {
    M("<init>", "(Ljava/lang/String;I)V", 0),
    M("<init>", "(Ljava/lang/String;ILjava/util/TimeZone;)V", 0),
    M("getDate",  "()Ljava/util/Date;", 0),
    M("setDate",  "(Ljava/util/Date;)V", 0),
    M("getTimeZone", "()Ljava/util/TimeZone;", 0),
    M("setTimeZone", "(Ljava/util/TimeZone;)V", 0),
    M("setInputMode", "(I)V", 0),
    M("getInputMode", "()I", 0),
    M("njText",   "()Ljava/lang/String;", 0),
    M("njHeight", "(I)I", 0)
};

static const nj_builtin_field spacer_fields[] = {
    FLD("spW", D_INT, 0),
    FLD("spH", D_INT, 0)
};

static const nj_builtin_method spacer_methods[] = {
    M("<init>", "(II)V", 0),
    M("setMinimumSize", "(II)V", 0),
    M("njText",   "()Ljava/lang/String;", 0),
    M("njHeight", "(I)I", 0)
};

/* ---- Form / List / TextBox ---- */
static const nj_builtin_method form_methods[] = {
    M("<init>", "(Ljava/lang/String;)V", 0),
    M("<init>", "(Ljava/lang/String;[L" NJ_C_ITEM ";)V", 0),
    M("append", "(Ljava/lang/String;)I", 0),
    M("append", "(L" NJ_C_IMAGE ";)I", 0),
    M("append", "(L" NJ_C_ITEM ";)I", 0),
    M("insert", "(ILjava/lang/String;)V", 0),
    M("insert", "(IL" NJ_C_IMAGE ";)V", 0),
    M("insert", "(IL" NJ_C_ITEM ";)V", 0),
    M("delete",    "(I)V", 0),
    M("deleteAll", "()V", 0),
    M("size",      "()I", 0),
    M("get",       "(I)L" NJ_C_ITEM ";", 0),
    M("set",       "(IL" NJ_C_ITEM ";)V", 0),
    M("setItemStateListener", "(L" NJ_C_ITEMSTATELISTENER ";)V", 0)
};

static const nj_builtin_field list_fields[] = {
    FLD("lsType",    D_INT,    0),
    FLD("lsStrings", D_STRARR, 0),
    FLD("lsImages",  D_IMGARR, 0),
    FLD("lsCount",   D_INT,    0),
    FLD("lsIndex",   D_INT,    0),
    FLD("lsFit",     D_INT,    0),
    FLD("lsFont",    D_FONT,   0),
    FLD("lsSelect",  D_CMD,    0),
    FLD("SELECT_COMMAND", D_CMD, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("EXCLUSIVE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("MULTIPLE",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("IMPLICIT",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_DEFAULT", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_ON",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_WRAP_OFF", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method list_methods[] = {
    M("<init>", "(Ljava/lang/String;I)V", 0),
    M("<init>", "(Ljava/lang/String;I[Ljava/lang/String;[L" NJ_C_IMAGE ";)V", 0),
    M("getType", "()I", 0),
    M("append", "(Ljava/lang/String;)I", 0),
    M("append", "(Ljava/lang/String;L" NJ_C_IMAGE ";)I", 0),
    M("insert", "(ILjava/lang/String;)V", 0),
    M("insert", "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", 0),
    M("delete",    "(I)V", 0),
    M("deleteAll", "()V", 0),
    M("size",      "()I", 0),
    M("getString", "(I)Ljava/lang/String;", 0),
    M("setString", "(ILjava/lang/String;)V", 0),
    M("getImage",  "(I)L" NJ_C_IMAGE ";", 0),
    M("setImage",  "(IL" NJ_C_IMAGE ";)V", 0),
    M("getSelectedIndex", "()I", 0),
    M("setSelectedIndex", "(I)V", 0),
    M("setSelectedIndex", "(IZ)V", 0),
    M("isSelected",       "(I)Z", 0),
    M("setSelectedFlags", "([Z)V", 0),
    M("getSelectedFlags", "([Z)I", 0),
    M("set",              "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", 0),
    M("setFitPolicy", "(I)V", 0),
    M("getFitPolicy", "()I", 0),
    M("setFont", "(IL" NJ_C_FONT ";)V", 0),
    M("getFont", "(I)L" NJ_C_FONT ";", 0),
    M("setSelectCommand", "(L" NJ_C_COMMAND ";)V", 0),
    M("getSelectCommand", "()L" NJ_C_COMMAND ";", 0)
};

static const nj_builtin_field textbox_fields[] = {
    FLD("tbText",        D_STR,   0),
    FLD("tbChars",       D_CHARR, 0),
    FLD("tbMax",         D_INT,   0),
    FLD("tbConstraints", D_INT,   0),
    FLD("tbCursor",      D_INT,   0),
    FLD("tbInputMode",   D_INT,   0),
    FLD("tbInputModeName", D_STR, 0),
    FLD("tbBuffer",      D_IMAGE, 0),
    FLD("ANY",         D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("EMAILADDR",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("NUMERIC",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("PHONENUMBER", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("URL",         D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("DECIMAL",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("PASSWORD",    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("UNEDITABLE",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("SENSITIVE",   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("NON_PREDICTIVE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("INITIAL_CAPS_WORD", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("INITIAL_CAPS_SENTENCE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method textbox_methods[] = {
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;II)V", 0),
    M("getString",       "()Ljava/lang/String;", 0),
    M("setString",       "(Ljava/lang/String;)V", 0),
    M("getChars",        "([C)I", 0),
    M("setChars",        "([CII)V", 0),
    M("insert",          "([CIII)V", 0),
    M("insert",          "(Ljava/lang/String;I)V", 0),
    M("delete",          "(II)V", 0),
    M("size",            "()I", 0),
    M("getMaxSize",      "()I", 0),
    M("setMaxSize",      "(I)V", 0),
    M("getConstraints",  "()I", 0),
    M("setConstraints",  "(I)V", 0),
    M("setInitialInputMode", "(Ljava/lang/String;)V", 0),
    M("setTicker",       "(L" NJ_C_TICKER ";)V", 0),
    M("getTicker",       "()L" NJ_C_TICKER ";", 0),
    M("getCaretPosition", "()I", 0)
};

/* ---- Alert / AlertType ---- */
static const nj_builtin_field alert_fields[] = {
    FLD(F_AL_TYPE,    "L" NJ_C_ALERTTYPE ";", 0),
    FLD(F_AL_STRING,  D_STR,   0),
    FLD(F_AL_IMAGE,   D_IMAGE, 0),
    FLD(F_AL_TIMEOUT, D_INT,   0),
    FLD(F_AL_INDEF,   D_BOOL,  0),
    FLD("alIndicator", NJ_C_GAUGE, 0),
    FLD("alBuffer",   D_IMAGE, 0),
    FLD("sDefaultTimeout", D_INT, NJ_ACC_STATIC),
    FLD("FOREVER", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("DISMISS_COMMAND", D_CMD, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method alert_methods[] = {
    M("<init>", "(Ljava/lang/String;)V", 0),
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;L" NJ_C_IMAGE ";L" NJ_C_ALERTTYPE ";)V", 0),
    M("getType",   "()L" NJ_C_ALERTTYPE ";", 0),
    M("setType",   "(L" NJ_C_ALERTTYPE ";)V", 0),
    M("getString", "()Ljava/lang/String;", 0),
    M("setString", "(Ljava/lang/String;)V", 0),
    M("getImage",  "()L" NJ_C_IMAGE ";", 0),
    M("setImage",  "(L" NJ_C_IMAGE ";)V", 0),
    M("getTimeout", "()I", 0),
    M("setTimeout", "(I)V", 0),
    M("getDefaultTimeout", "()I", NJ_ACC_STATIC),
    M("setDefaultTimeout", "(I)V", NJ_ACC_STATIC),
    M("setIndicator", "(L" NJ_C_GAUGE ";)V", 0),
    M("getIndicator", "()L" NJ_C_GAUGE ";", 0)
};

static const nj_builtin_field alerttype_fields[] = {
    FLD("atType", D_INT, 0),
    FLD("INFO",         "L" NJ_C_ALERTTYPE ";", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("WARNING",      "L" NJ_C_ALERTTYPE ";", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("ERROR",        "L" NJ_C_ALERTTYPE ";", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("ALARM",        "L" NJ_C_ALERTTYPE ";", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("CONFIRMATION", "L" NJ_C_ALERTTYPE ";", NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method alerttype_methods[] = {
    M("<init>", "()V", 0),
    M("<init>", "(I)V", 0),
    M("playSound", "(L" NJ_C_DISPLAY ";)Z", 0),
    M("getTypeName", "(L" NJ_C_ALERTTYPE ";)Ljava/lang/String;", NJ_ACC_STATIC),
    M("getTypeName", "()Ljava/lang/String;", 0)
};

/* ---- Display ---- */
static const nj_builtin_field display_fields[] = {
    FLD(F_DISP_CUR,   D_DISPLAY,  0),
    FLD(F_DISP_ALERT, D_DISPLAY,  0),
    FLD("dpQueue",    D_VECTOR,   0),
    FLD("dpImeFocus", D_ITEM, 0),
    FLD("dpImeSession", D_INT, 0),
    FLD("dpImeSequence", D_INT, 0),
    FLD("dpImeConstraints", D_INT, 0),
    FLD("dpImeMax", D_INT, 0),
    FLD("sDisplay",   D_DISPLAYS, NJ_ACC_STATIC),
    FLD("LIST_ELEMENT",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TEXT_ELEMENT",  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("IMAGE_ELEMENT", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    /* The six parts of the screen getColor() can be asked about. */
    FLD("COLOR_BACKGROUND",             D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("COLOR_FOREGROUND",             D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("COLOR_HIGHLIGHTED_BACKGROUND", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("COLOR_HIGHLIGHTED_FOREGROUND", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("COLOR_BORDER",                 D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("COLOR_HIGHLIGHTED_BORDER",     D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method display_methods[] = {
    M("<init>", "()V", 0),
    M("getDisplay", "(Ljavax/microedition/midlet/MIDlet;)L" NJ_C_DISPLAY ";",
      NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("setCurrent", "(L" NJ_C_DISPLAYABLE ";)V", 0),
    M("setCurrent", "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", 0),
    M("setCurrentItem", "(L" NJ_C_ITEM ";)V", 0),
    M("getCurrent", "()L" NJ_C_DISPLAYABLE ";", 0),
    M("callSerially", "(Ljava/lang/Runnable;)V", 0),
    /* static (MIDP) and instance (the compile stubs) spellings of the same
     * device queries; the VM resolves whichever form the caller emitted. */
    M("isColor",     "()Z", NJ_ACC_STATIC),
    M("isColor",     "()Z", 0),
    M("numColors",   "()I", NJ_ACC_STATIC),
    M("numColors",   "()I", 0),
    M("numAlphaLevels", "()I", NJ_ACC_STATIC),
    M("numAlphaLevels", "()I", 0),
    M("flashBacklight", "(I)Z", NJ_ACC_STATIC),
    M("flashBacklight", "(I)V", 0),
    M("vibrate",     "(I)Z", NJ_ACC_STATIC),
    M("vibrate",     "(I)Z", 0),
    M("getBestImageWidth",  "(I)I", 0),
    M("getBestImageHeight", "(I)I", 0),
    /* MIDP 2.0 colour queries.  getColor is also declared in the boolean shape
     * some stubs emit, so the VM resolves whichever form the caller wrote. */
    M("getColor",       "(I)I", 0),
    M("getColor",       "(I)Z", 0),
    M("getBorderStyle", "(Z)I", 0)
};

/* ---- Screen: the abstract parent of the "content" Displayables ---- */
/*
 * The item storage lives on Screen rather than on Form, because Form, List and
 * TextBox all need it and List is NOT a Form in MIDP.  Declaring it here means
 * one field object, one offset, and a Form-relative lookup that finds the same
 * slot for every kind of screen.
 */
static const nj_builtin_field screen_fields[] = {
    FLD("fmItems",   D_ITEMARR, 0),
    FLD("fmCount",   D_INT,     0),
    FLD("fmIndex",   D_INT,     0),
    FLD("fmItemListener", D_ISL, 0),
    FLD("fmBuffer",  D_IMAGE,   0)
};

static const nj_builtin_method screen_methods[] = {
    M("<init>", "()V", 0)
};

NJFN(screen_init)
{
    return d_init(vm, th, args, ret);
}

/* ---- interfaces: declared abstract, no natives ---- */
static const nj_builtin_method commandlistener_methods[] = {
    M("commandAction",
      "(L" NJ_C_COMMAND ";L" NJ_C_DISPLAYABLE ";)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_method itemcommandlistener_methods[] = {
    M("commandAction", "(L" NJ_C_COMMAND ";L" NJ_C_ITEM ";)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_method itemstatelistener_methods[] = {
    M("itemStateChanged", "(L" NJ_C_ITEM ";)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

#define NJ_LCDUI_N(a) (nj_u2)(sizeof(a) / sizeof((a)[0]))

const nj_builtin_class nj_lib_lcdui_classes[] = {
    { "javax/microedition/lcdui/Choice", NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      choice_fields,
      (nj_u2)(sizeof(choice_fields) / sizeof(choice_fields[0])),
      NULL, 0u },
    { NJ_C_DISPLAYABLE, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      displayable_fields, NJ_LCDUI_N(displayable_fields),
      displayable_methods, NJ_LCDUI_N(displayable_methods) },
    { "javax/microedition/lcdui/Screen", NJ_C_DISPLAYABLE,
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      screen_fields, NJ_LCDUI_N(screen_fields),
      screen_methods, NJ_LCDUI_N(screen_methods) },
    { NJ_C_CANVAS, NJ_C_DISPLAYABLE, NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      canvas_fields, NJ_LCDUI_N(canvas_fields),
      canvas_methods, NJ_LCDUI_N(canvas_methods) },
    { NJ_C_GRAPHICS, "java/lang/Object", NJ_ACC_PUBLIC,
      graphics_fields, NJ_LCDUI_N(graphics_fields),
      graphics_methods, NJ_LCDUI_N(graphics_methods) },
    { NJ_C_IMAGE, "java/lang/Object", NJ_ACC_PUBLIC,
      image_fields, NJ_LCDUI_N(image_fields),
      image_methods, NJ_LCDUI_N(image_methods) },
    { NJ_C_FONT, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      font_fields, NJ_LCDUI_N(font_fields),
      font_methods, NJ_LCDUI_N(font_methods) },
    { NJ_C_COMMAND, "java/lang/Object", NJ_ACC_PUBLIC,
      command_fields, NJ_LCDUI_N(command_fields),
      command_methods, NJ_LCDUI_N(command_methods) },
    { NJ_C_TICKER, "java/lang/Object", NJ_ACC_PUBLIC,
      ticker_fields, NJ_LCDUI_N(ticker_fields),
      ticker_methods, NJ_LCDUI_N(ticker_methods) },
    { NJ_C_ALERT, "javax/microedition/lcdui/Screen", NJ_ACC_PUBLIC,
      alert_fields, NJ_LCDUI_N(alert_fields),
      alert_methods, NJ_LCDUI_N(alert_methods) },
    { NJ_C_ALERTTYPE, "java/lang/Object", NJ_ACC_PUBLIC,
      alerttype_fields, NJ_LCDUI_N(alerttype_fields),
      alerttype_methods, NJ_LCDUI_N(alerttype_methods) },
    { NJ_C_FORM, "javax/microedition/lcdui/Screen", NJ_ACC_PUBLIC,
      NULL, 0u, form_methods, NJ_LCDUI_N(form_methods) },
    { NJ_C_LIST, "javax/microedition/lcdui/Screen", NJ_ACC_PUBLIC,
      list_fields, NJ_LCDUI_N(list_fields),
      list_methods, NJ_LCDUI_N(list_methods) },
    { NJ_C_TEXTBOX, "javax/microedition/lcdui/Screen", NJ_ACC_PUBLIC,
      textbox_fields, NJ_LCDUI_N(textbox_fields),
      textbox_methods, NJ_LCDUI_N(textbox_methods) },
    { NJ_C_ITEM, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      item_fields, NJ_LCDUI_N(item_fields),
      item_methods, NJ_LCDUI_N(item_methods) },
    { NJ_C_STRINGITEM, NJ_C_ITEM, NJ_ACC_PUBLIC,
      stringitem_fields, NJ_LCDUI_N(stringitem_fields),
      stringitem_methods, NJ_LCDUI_N(stringitem_methods) },
    { NJ_C_IMAGEITEM, NJ_C_ITEM, NJ_ACC_PUBLIC,
      imageitem_fields, NJ_LCDUI_N(imageitem_fields),
      imageitem_methods, NJ_LCDUI_N(imageitem_methods) },
    { NJ_C_GAUGE, NJ_C_ITEM, NJ_ACC_PUBLIC,
      gauge_fields, NJ_LCDUI_N(gauge_fields),
      gauge_methods, NJ_LCDUI_N(gauge_methods) },
    { NJ_C_CHOICEGROUP, NJ_C_ITEM, NJ_ACC_PUBLIC,
      choicegroup_fields, NJ_LCDUI_N(choicegroup_fields),
      choicegroup_methods, NJ_LCDUI_N(choicegroup_methods) },
    { NJ_C_TEXTFIELD, NJ_C_ITEM, NJ_ACC_PUBLIC,
      textfield_fields, NJ_LCDUI_N(textfield_fields),
      textfield_methods, NJ_LCDUI_N(textfield_methods) },
    { NJ_C_DATEFIELD, NJ_C_ITEM, NJ_ACC_PUBLIC,
      datefield_fields, NJ_LCDUI_N(datefield_fields),
      datefield_methods, NJ_LCDUI_N(datefield_methods) },
    { NJ_C_SPACER, NJ_C_ITEM, NJ_ACC_PUBLIC,
      spacer_fields, NJ_LCDUI_N(spacer_fields),
      spacer_methods, NJ_LCDUI_N(spacer_methods) },
    { NJ_C_DISPLAY, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      display_fields, NJ_LCDUI_N(display_fields),
      display_methods, NJ_LCDUI_N(display_methods) },
    { NJ_C_COMMANDLISTENER, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, commandlistener_methods, NJ_LCDUI_N(commandlistener_methods) },
    { NJ_C_ITEMCOMMANDLISTENER, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, itemcommandlistener_methods, NJ_LCDUI_N(itemcommandlistener_methods) },
    { NJ_C_ITEMSTATELISTENER, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, itemstatelistener_methods, NJ_LCDUI_N(itemstatelistener_methods) }
};
const int nj_lib_lcdui_class_count =
    (int)(sizeof(nj_lib_lcdui_classes) / sizeof(nj_lib_lcdui_classes[0]));

const nj_native_entry nj_lib_lcdui_table[] = {
    { "javax/microedition/lcdui/Screen", "<init>", "()V", screen_init },
    { NJ_C_IMAGE, "<init>", "()V", no_public_constructor },
    { NJ_C_IMAGE, "createImage", "(II)L" NJ_C_IMAGE ";", img_createImage_wh },
    { NJ_C_IMAGE, "createImage", "(L" NJ_C_IMAGE ";)L" NJ_C_IMAGE ";", img_createImage_copy },
    { NJ_C_IMAGE, "createImage", "(IIIIII)L" NJ_C_IMAGE ";", img_createImage_sixints },
    { NJ_C_IMAGE, "createImage", "(L" NJ_C_IMAGE ";IIIII)L" NJ_C_IMAGE ";", img_createImage_img },
    { NJ_C_IMAGE, "createImage", "([BII)L" NJ_C_IMAGE ";", img_createImage_bytes3 },
    { NJ_C_IMAGE, "createImage", "(Ljava/io/InputStream;)L" NJ_C_IMAGE ";", img_createImage_stream },
    { NJ_C_IMAGE, "createImage", "([BIII)L" NJ_C_IMAGE ";", img_createImage_bytes },
    { NJ_C_IMAGE, "createImage", "(Ljava/lang/String;)L" NJ_C_IMAGE ";", img_createImage_name },
    { NJ_C_IMAGE, "createRGBImage", "([IIIZ)L" NJ_C_IMAGE ";", img_createRGBImage },
    { NJ_C_IMAGE, "getWidth", "()I", img_getWidth },
    { NJ_C_IMAGE, "getHeight", "()I", img_getHeight },
    { NJ_C_IMAGE, "isMutable", "()Z", img_isMutable },
    { NJ_C_IMAGE, "getGraphics", "()L" NJ_C_GRAPHICS ";", img_getGraphics },
    { NJ_C_IMAGE, "getRGB", "([IIIIIII)V", img_getRGB },

    { NJ_C_FONT, "<init>", "()V", no_public_constructor },
    { NJ_C_FONT, "getFont", "(I)L" NJ_C_FONT ";", font_getFontStatic1 },
    { NJ_C_FONT, "getFont", "(III)L" NJ_C_FONT ";", font_getFontStatic3 },
    { NJ_C_FONT, "getDefaultFont", "()L" NJ_C_FONT ";", font_getDefaultFont },
    { NJ_C_FONT, "getFace", "()I", font_getFace },
    { NJ_C_FONT, "getStyle", "()I", font_getStyle },
    { NJ_C_FONT, "getSize", "()I", font_getSize },
    { NJ_C_FONT, "isPlain", "()Z", font_isPlain },
    { NJ_C_FONT, "isBold", "()Z", font_isBold },
    { NJ_C_FONT, "isItalic", "()Z", font_isItalic },
    { NJ_C_FONT, "isUnderlined", "()Z", font_isUnderlined },
    { NJ_C_FONT, "getHeight", "()I", font_getHeight },
    { NJ_C_FONT, "getBaselinePosition", "()I", font_getBaselinePosition },
    { NJ_C_FONT, "getAscent", "()I", font_getAscent },
    { NJ_C_FONT, "getDescent", "()I", font_getDescent },
    { NJ_C_FONT, "charWidth", "(C)I", font_charWidth },
    { NJ_C_FONT, "charsWidth", "([CII)I", font_charsWidth },
    { NJ_C_FONT, "stringWidth", "(Ljava/lang/String;)I", font_stringWidth },
    { NJ_C_FONT, "substringWidth", "(Ljava/lang/String;II)I", font_substringWidth },

    { NJ_C_GRAPHICS, "<init>", "(L" NJ_C_IMAGE ";)V", g_init },
    { NJ_C_GRAPHICS, "<init>", "()V", g_init_noarg },
    { NJ_C_GRAPHICS, "translate", "(II)V", g_translate },
    { NJ_C_GRAPHICS, "getTranslateX", "()I", g_getTranslateX },
    { NJ_C_GRAPHICS, "getTranslateY", "()I", g_getTranslateY },
    { NJ_C_GRAPHICS, "setClip", "(IIII)V", g_setClip },
    { NJ_C_GRAPHICS, "clipRect", "(IIII)V", g_clipRect },
    { NJ_C_GRAPHICS, "getClipX", "()I", g_getClipX },
    { NJ_C_GRAPHICS, "getClipY", "()I", g_getClipY },
    { NJ_C_GRAPHICS, "getClipWidth", "()I", g_getClipWidth },
    { NJ_C_GRAPHICS, "getClipHeight", "()I", g_getClipHeight },
    { NJ_C_GRAPHICS, "setColor", "(I)V", g_setColor },
    { NJ_C_GRAPHICS, "setColor", "(III)V", g_setColorRGB },
    { NJ_C_GRAPHICS, "getColor", "()I", g_getColor },
    { NJ_C_GRAPHICS, "getDisplayColor", "(I)I", g_getDisplayColor },
    { NJ_C_GRAPHICS, "getRedComponent", "()I", g_getRedComponent },
    { NJ_C_GRAPHICS, "getGreenComponent", "()I", g_getGreenComponent },
    { NJ_C_GRAPHICS, "getBlueComponent", "()I", g_getBlueComponent },
    { NJ_C_GRAPHICS, "getAlphaComponent", "()I", g_getAlphaComponent },
    { NJ_C_GRAPHICS, "setAlpha", "(I)V", g_setAlpha },
    { NJ_C_GRAPHICS, "getAlpha", "()I", g_getAlpha },
    { NJ_C_GRAPHICS, "getGrayScale", "()I", g_getGrayScale },
    { NJ_C_GRAPHICS, "setGrayScale", "(I)V", g_setGrayScale },
    { NJ_C_GRAPHICS, "setBlendMode", "(I)V", g_setBlendMode },
    { NJ_C_GRAPHICS, "getBlendMode", "()I", g_getBlendMode },
    { NJ_C_GRAPHICS, "setStrokeStyle", "(I)V", g_setStrokeStyle },
    { NJ_C_GRAPHICS, "getStrokeStyle", "()I", g_getStrokeStyle },
    { NJ_C_GRAPHICS, "setFont", "(L" NJ_C_FONT ";)V", g_setFont },
    { NJ_C_GRAPHICS, "getFont", "()L" NJ_C_FONT ";", g_getFont },
    { NJ_C_GRAPHICS, "setXORMode", "(Z)V", g_setXORModeFlag },
    { NJ_C_GRAPHICS, "setXORMode", "(L" NJ_C_GRAPHICS ";)V", g_setXORMode },
    { NJ_C_GRAPHICS, "drawLine", "(IIII)V", g_drawLine },
    { NJ_C_GRAPHICS, "drawRect", "(IIII)V", g_drawRect },
    { NJ_C_GRAPHICS, "fillRect", "(IIII)V", g_fillRect },
    { NJ_C_GRAPHICS, "setARGBColor",         "(I)V",   dg_setARGBColor },
    { NJ_C_GRAPHICS, "getNativePixelFormat", "()I",    dg_getNativePixelFormat },
    { NJ_C_GRAPHICS, "drawImage", "(L" NJ_C_IMAGE ";IIII)V", dg_drawImage },
    { NJ_C_GRAPHICS, "fillTriangle", "(IIIIIII)V", dg_fillTriangle },
    { NJ_C_GRAPHICS, "drawTriangle", "(IIIIIII)V", dg_drawTriangle },
    { NJ_C_GRAPHICS, "drawPolygon", "([II[IIII)V", dg_drawPolygon },
    { NJ_C_GRAPHICS, "fillPolygon", "([II[IIII)V", dg_fillPolygon },
    { NJ_C_GRAPHICS, "drawPixels",  "([SZIIIIIIII)V", dg_drawPixels },
    { NJ_C_GRAPHICS, "getPixels",   "([SIIIIIII)V",   dg_getPixels },
    { NJ_C_GRAPHICS, "getPixels",   "([IIIIIIII)V",   dg_getPixelsInt },
    { NJ_C_GRAPHICS, "drawPixels",  "([IZIIIIIIII)V", dg_drawPixelsInt },
    { NJ_C_GRAPHICS, "drawPixels",  "([B[BIIIIIIII)V", dg_drawPixelsBytes },
    { NJ_C_GRAPHICS, "getPixels",   "([B[BIIIIIII)V",  dg_getPixelsBytes },
    { NJ_C_GRAPHICS, "getAlphaComponent", "()I",       dg_getAlphaComponent },
    { NJ_C_GRAPHICS, "drawRoundRect", "(IIIIII)V", g_drawRoundRect },
    { NJ_C_GRAPHICS, "fillRoundRect", "(IIIIII)V", g_fillRoundRect },
    { NJ_C_GRAPHICS, "drawArc", "(IIIIII)V", g_drawArc },
    { NJ_C_GRAPHICS, "fillArc", "(IIIIII)V", g_fillArc },
    { NJ_C_GRAPHICS, "drawTriangle", "(IIIIII)V", g_drawTriangle },
    { NJ_C_GRAPHICS, "fillTriangle", "(IIIIII)V", g_fillTriangle },
    { NJ_C_GRAPHICS, "drawString", "(Ljava/lang/String;III)V", g_drawString },
    { NJ_C_GRAPHICS, "drawString", "(Ljava/lang/String;II)V", g_drawString3 },
    { NJ_C_GRAPHICS, "drawSubstring", "(Ljava/lang/String;IIIII)V", g_drawSubstring },
    { NJ_C_GRAPHICS, "drawChars", "([CIIIII)V", g_drawChars },
    { NJ_C_GRAPHICS, "drawChar", "(CIII)V", g_drawChar },
    { NJ_C_GRAPHICS, "drawImage", "(L" NJ_C_IMAGE ";III)V", g_drawImage },
    { NJ_C_GRAPHICS, "drawRegion", "(L" NJ_C_IMAGE ";IIIIIIII)V", g_drawRegion },
    { NJ_C_GRAPHICS, "drawRegion", "(L" NJ_C_IMAGE ";IIIIIIIII)V", g_drawRegion },
    { NJ_C_GRAPHICS, "drawRGB", "([IIIIIIIZ)V", g_drawRGB },
    { NJ_C_GRAPHICS, "copyArea", "(IIIIIII)V", g_copyArea },

    { NJ_C_DISPLAYABLE, "<init>", "()V", d_init },
    { NJ_C_DISPLAYABLE, "getWidth", "()I", d_getWidth },
    { NJ_C_DISPLAYABLE, "getHeight", "()I", d_getHeight },
    { NJ_C_DISPLAYABLE, "getTitle", "()Ljava/lang/String;", d_getTitle },
    { NJ_C_DISPLAYABLE, "setTitle", "(Ljava/lang/String;)V", d_setTitle },
    { NJ_C_DISPLAYABLE, "getTicker", "()L" NJ_C_TICKER ";", d_getTicker },
    { NJ_C_DISPLAYABLE, "setTicker", "(L" NJ_C_TICKER ";)V", d_setTicker },
    { NJ_C_DISPLAYABLE, "addCommand", "(L" NJ_C_COMMAND ";)V", d_addCommand },
    { NJ_C_DISPLAYABLE, "removeCommand", "(L" NJ_C_COMMAND ";)V", d_removeCommand },
    { NJ_C_DISPLAYABLE, "setCommandListener", "(L" NJ_C_COMMANDLISTENER ";)V", d_setCommandListener },
    { NJ_C_DISPLAYABLE, "getCommandListener", "()L" NJ_C_COMMANDLISTENER ";", d_getCommandListener },
    { NJ_C_DISPLAYABLE, "isShown", "()Z", d_isShown },
    { NJ_C_DISPLAYABLE, "sizeChanged", "(II)V", d_sizeChanged },
    { NJ_C_DISPLAYABLE, "getKeyName", "(I)Ljava/lang/String;", d_getKeyName },
    { NJ_C_DISPLAYABLE, "setFullScreenMode", "(Z)V", d_setFullScreenMode },
    { NJ_C_DISPLAYABLE, "isFullScreenMode", "()Z", d_isFullScreenMode },

    { NJ_C_CANVAS, "<init>", "()V", c_init },
    { NJ_C_CANVAS, "showNotify", "()V", c_visibility_notify },
    { NJ_C_CANVAS, "hideNotify", "()V", c_visibility_notify },
    { NJ_C_CANVAS, "drawNotify", "()V", c_visibility_notify },
    { NJ_C_CANVAS, "keyPressed",      "(I)V",  canvas_noop_i },
    { NJ_C_CANVAS, "keyReleased",     "(I)V",  canvas_noop_i },
    { NJ_C_CANVAS, "keyRepeated",     "(I)V",  canvas_noop_i },
    { NJ_C_CANVAS, "pointerPressed",  "(II)V", canvas_noop_ii },
    { NJ_C_CANVAS, "pointerReleased", "(II)V", canvas_noop_ii },
    { NJ_C_CANVAS, "pointerDragged",  "(II)V", canvas_noop_ii },
    { NJ_C_CANVAS, "getWidth", "()I", c_getWidth },
    { NJ_C_CANVAS, "getHeight", "()I", c_getHeight },
    { NJ_C_CANVAS, "isDoubleBuffered", "()Z", c_isDoubleBuffered },
    { NJ_C_CANVAS, "hasPointerEvents", "()Z", c_hasPointerEvents },
    { NJ_C_CANVAS, "hasPointerMotionEvents", "()Z", c_hasPointerMotionEvents },
    { NJ_C_CANVAS, "hasRepeatEvents", "()Z", c_hasRepeatEvents },
    { NJ_C_CANVAS, "getGameAction", "(I)I", c_getGameAction },
    { NJ_C_CANVAS, "getKeyCode", "(I)I", c_getKeyCode },
    { NJ_C_CANVAS, "getKeyName", "(I)Ljava/lang/String;", c_getKeyName },
    { NJ_C_CANVAS, "repaint", "()V", c_repaint_all },
    { NJ_C_CANVAS, "repaint", "(IIII)V", c_repaint },
    { NJ_C_CANVAS, "serviceRepaints", "()V", c_serviceRepaints },
    { NJ_C_CANVAS, "setFullScreenMode", "(Z)V", c_setFullScreenMode },

    { NJ_C_COMMAND, "<init>", "(Ljava/lang/String;II)V", cmd_init },
    { NJ_C_COMMAND, "<init>", "(Ljava/lang/String;III)V", cmd_init_long },
    { NJ_C_COMMAND, "getCommandType", "()I", cmd_getCommandType },
    { NJ_C_COMMAND, "getLabel", "()Ljava/lang/String;", cmd_getLabel },
    { NJ_C_COMMAND, "getLongLabel", "()Ljava/lang/String;", cmd_getLongLabel },
    { NJ_C_COMMAND, "getPriority", "()I", cmd_getPriority },
    { NJ_C_COMMAND, "compareTo", "(L" NJ_C_COMMAND ";)I", cmd_compareTo },
    { NJ_C_COMMAND, "<init>", "(Ljava/lang/String;Ljava/lang/String;II)V", cmd_init_labels },

    { NJ_C_TICKER, "<init>", "(Ljava/lang/String;)V", ticker_init },
    { NJ_C_TICKER, "getString", "()Ljava/lang/String;", ticker_getString },
    { NJ_C_TICKER, "setString", "(Ljava/lang/String;)V", ticker_setString },

    { NJ_C_ITEM, "<init>", "()V", item_init },
    { NJ_C_ITEM, "getLabel", "()Ljava/lang/String;", item_getLabel },
    { NJ_C_ITEM, "setLabel", "(Ljava/lang/String;)V", item_setLabel },
    { NJ_C_ITEM, "getLayout", "()I", item_getLayout },
    { NJ_C_ITEM, "setLayout", "(I)V", item_setLayout },
    { NJ_C_ITEM, "addCommand", "(L" NJ_C_COMMAND ";)V", item_addCommand },
    { NJ_C_ITEM, "removeCommand", "(L" NJ_C_COMMAND ";)V", item_removeCommand },
    { NJ_C_ITEM, "setItemCommandListener", "(L" NJ_C_ITEMCOMMANDLISTENER ";)V", item_setItemCommandListener },
    { NJ_C_ITEM, "setPreferredSize", "(II)V", item_setPreferredSize },
    { NJ_C_ITEM, "getMinimumWidth", "()I", item_getMinimumWidth },
    { NJ_C_ITEM, "getMinimumHeight", "()I", item_getMinimumHeight },
    { NJ_C_ITEM, "getPreferredWidth", "()I", item_getPreferredWidth },
    { NJ_C_ITEM, "getPreferredHeight", "()I", item_getPreferredHeight },
    { NJ_C_ITEM, "notifyStateChanged", "()V", item_notifyStateChanged },
    { NJ_C_ITEM, "setDefaultCommand", "(L" NJ_C_COMMAND ";)V", item_setDefaultCommand },
    { NJ_C_ITEM, "getDefaultCommand", "()L" NJ_C_COMMAND ";", item_getDefaultCommand },
    { NJ_C_ITEM, "njText", "()Ljava/lang/String;", item_njText },
    { NJ_C_ITEM, "njHeight", "(I)I", item_njHeight },
    { NJ_C_ITEM, "njSelected", "(I)Ljava/lang/String;", item_njSelected },

    { NJ_C_STRINGITEM, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V", si_init },
    { NJ_C_STRINGITEM, "<init>", "(Ljava/lang/String;Ljava/lang/String;I)V", si_init_mode },
    { NJ_C_STRINGITEM, "getFont", "()L" NJ_C_FONT ";", si_getFont },
    { NJ_C_STRINGITEM, "setFont", "(L" NJ_C_FONT ";)V", si_setFont },
    { NJ_C_STRINGITEM, "getText", "()Ljava/lang/String;", si_getText },
    { NJ_C_STRINGITEM, "setText", "(Ljava/lang/String;)V", si_setText },
    { NJ_C_STRINGITEM, "getAppearanceMode", "()I", si_getAppearanceMode },
    { NJ_C_STRINGITEM, "addCommand", "(L" NJ_C_COMMAND ";)V", si_addCommand },
    { NJ_C_STRINGITEM, "removeCommand", "(L" NJ_C_COMMAND ";)V", si_removeCommand },
    { NJ_C_STRINGITEM, "setItemCommandListener", "(L" NJ_C_ITEMCOMMANDLISTENER ";)V", si_setItemCommandListener },
    { NJ_C_STRINGITEM, "njText", "()Ljava/lang/String;", si_njText },
    { NJ_C_STRINGITEM, "njHeight", "(I)I", si_njHeight },

    { NJ_C_IMAGEITEM, "<init>", "(Ljava/lang/String;L" NJ_C_IMAGE ";ILjava/lang/String;)V", ii_init },
    { NJ_C_IMAGEITEM, "getImage", "()L" NJ_C_IMAGE ";", ii_getImage },
    { NJ_C_IMAGEITEM, "setImage", "(L" NJ_C_IMAGE ";)V", ii_setImage },
    { NJ_C_IMAGEITEM, "getAltText", "()Ljava/lang/String;", ii_getAltText },
    { NJ_C_IMAGEITEM, "setAltText", "(Ljava/lang/String;)V", ii_setAltText },
    { NJ_C_IMAGEITEM, "setLayout", "(I)V", ii_setLayout },
    { NJ_C_IMAGEITEM, "getAppearanceMode", "()I", ii_getAppearanceMode },
    { NJ_C_IMAGEITEM, "njText", "()Ljava/lang/String;", ii_njText },
    { NJ_C_IMAGEITEM, "njHeight", "(I)I", ii_njHeight },
    { NJ_C_IMAGEITEM, "<init>", "(Ljava/lang/String;L" NJ_C_IMAGE ";ILjava/lang/String;I)V", ii_init_app },

    { NJ_C_GAUGE, "<init>", "(Ljava/lang/String;ZII)V", gauge_init },
    { NJ_C_GAUGE, "setValue", "(I)V", gauge_setValue },
    { NJ_C_GAUGE, "getValue", "()I", gauge_getValue },
    { NJ_C_GAUGE, "setMaxValue", "(I)V", gauge_setMaxValue },
    { NJ_C_GAUGE, "getMaxValue", "()I", gauge_getMaxValue },
    { NJ_C_GAUGE, "isInteractive", "()Z", gauge_isInteractive },
    { NJ_C_GAUGE, "njText", "()Ljava/lang/String;", gauge_njText },
    { NJ_C_GAUGE, "njHeight", "(I)I", gauge_njHeight },

    { NJ_C_CHOICEGROUP, "<init>", "(Ljava/lang/String;I)V", cg_init },
    { NJ_C_CHOICEGROUP, "<init>", "(Ljava/lang/String;I[Ljava/lang/String;[L" NJ_C_IMAGE ";)V", cg_init_arrays },
    { NJ_C_CHOICEGROUP, "set", "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", cg_set },
    { NJ_C_CHOICEGROUP, "setSelectedIndex", "(IZ)V", cg_setSelectedIndex2 },
    { NJ_C_CHOICEGROUP, "append", "(Ljava/lang/String;)I", cg_append },
    { NJ_C_CHOICEGROUP, "append", "(Ljava/lang/String;L" NJ_C_IMAGE ";)I", cg_append_img },
    { NJ_C_CHOICEGROUP, "append", "(Ljava/lang/String;L" NJ_C_IMAGE ";)V", cg_append_img },
    { NJ_C_CHOICEGROUP, "insert", "(ILjava/lang/String;)V", cg_insert },
    { NJ_C_CHOICEGROUP, "insert", "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", cg_insert_img },
    { NJ_C_CHOICEGROUP, "delete", "(I)V", cg_delete },
    { NJ_C_CHOICEGROUP, "deleteAll", "()V", cg_deleteAll },
    { NJ_C_CHOICEGROUP, "size", "()I", cg_size },
    { NJ_C_CHOICEGROUP, "getString", "(I)Ljava/lang/String;", cg_getString },
    { NJ_C_CHOICEGROUP, "setString", "(ILjava/lang/String;)V", cg_setString },
    { NJ_C_CHOICEGROUP, "getImage", "(I)L" NJ_C_IMAGE ";", cg_getImage },
    { NJ_C_CHOICEGROUP, "setImage", "(IL" NJ_C_IMAGE ";)V", cg_setImage },
    { NJ_C_CHOICEGROUP, "isSelected", "(I)Z", cg_isSelected },
    { NJ_C_CHOICEGROUP, "setSelectedIndex", "(I)V", cg_setSelectedIndex },
    { NJ_C_CHOICEGROUP, "getSelectedIndex", "()I", cg_getSelectedIndex },
    { NJ_C_CHOICEGROUP, "setSelectedFlags", "([Z)V", cg_setSelectedFlags },
    { NJ_C_CHOICEGROUP, "getSelectedFlags", "([Z)I", cg_getSelectedFlags },
    { NJ_C_CHOICEGROUP, "setFitPolicy", "(I)V", cg_setFitPolicy },
    { NJ_C_CHOICEGROUP, "getFitPolicy", "()I", cg_getFitPolicy },
    { NJ_C_CHOICEGROUP, "setFont", "(IL" NJ_C_FONT ";)V", cg_setFont },
    { NJ_C_CHOICEGROUP, "getFont", "(I)L" NJ_C_FONT ";", cg_getFont },
    { NJ_C_CHOICEGROUP, "njText", "()Ljava/lang/String;", cg_njText },
    { NJ_C_CHOICEGROUP, "njHeight", "(I)I", cg_njHeight },

    { NJ_C_TEXTFIELD, "<init>", "(Ljava/lang/String;Ljava/lang/String;II)V", tf_init },
    { NJ_C_TEXTFIELD, "getString", "()Ljava/lang/String;", tf_getString },
    { NJ_C_TEXTFIELD, "setString", "(Ljava/lang/String;)V", tf_setString },
    { NJ_C_TEXTFIELD, "getChars", "([C)I", tf_getChars },
    { NJ_C_TEXTFIELD, "insert", "(Ljava/lang/String;I)V", tf_insert_str },
    { NJ_C_TEXTFIELD, "setChars", "([CII)V", tf_setChars },
    { NJ_C_TEXTFIELD, "insert", "([CIII)V", tf_insert },
    { NJ_C_TEXTFIELD, "delete", "(II)V", tf_delete },
    { NJ_C_TEXTFIELD, "size", "()I", tf_size },
    { NJ_C_TEXTFIELD, "getMaxSize", "()I", tf_getMaxSize },
    { NJ_C_TEXTFIELD, "setMaxSize", "(I)V", tf_setMaxSize },
    { NJ_C_TEXTFIELD, "getConstraints", "()I", tf_getConstraints },
    { NJ_C_TEXTFIELD, "setConstraints", "(I)V", tf_setConstraints },
    { NJ_C_TEXTFIELD, "setInitialInputMode", "(Ljava/lang/String;)V", tf_setInitialInputMode },
    { NJ_C_TEXTFIELD, "getInitialInputMode", "()Ljava/lang/String;", tf_getInitialInputMode },
    { NJ_C_TEXTFIELD, "njText", "()Ljava/lang/String;", tf_njText },
    { NJ_C_TEXTFIELD, "njHeight", "(I)I", tf_njHeight },
    { NJ_C_TEXTFIELD, "getCaretPosition", "()I", tf_getCaretPosition },

    { NJ_C_DATEFIELD, "<init>", "(Ljava/lang/String;I)V", df_init },
    { NJ_C_DATEFIELD, "<init>", "(Ljava/lang/String;ILjava/util/TimeZone;)V", df_init_zone },
    { NJ_C_DATEFIELD, "getDate", "()Ljava/util/Date;", df_getDate },
    { NJ_C_DATEFIELD, "setDate", "(Ljava/util/Date;)V", df_setDate },
    { NJ_C_DATEFIELD, "getTimeZone", "()Ljava/util/TimeZone;", df_getTimeZone },
    { NJ_C_DATEFIELD, "setTimeZone", "(Ljava/util/TimeZone;)V", df_setTimeZone },
    { NJ_C_DATEFIELD, "setInputMode", "(I)V", df_setInputMode },
    { NJ_C_DATEFIELD, "getInputMode", "()I", df_getInputMode },
    { NJ_C_DATEFIELD, "njText", "()Ljava/lang/String;", df_njText },
    { NJ_C_DATEFIELD, "njHeight", "(I)I", df_njHeight },

    { NJ_C_SPACER, "<init>", "(II)V", spacer_init },
    { NJ_C_SPACER, "setMinimumSize", "(II)V", spacer_setMinimumSize },
    { NJ_C_SPACER, "njText", "()Ljava/lang/String;", item_njText },
    { NJ_C_SPACER, "njHeight", "(I)I", spacer_njHeight },

    { NJ_C_FORM, "<init>", "(Ljava/lang/String;)V", form_init },
    { NJ_C_FORM, "<init>", "(Ljava/lang/String;[L" NJ_C_ITEM ";)V", form_init_items },
    { NJ_C_FORM, "append", "(Ljava/lang/String;)I", form_append_str },
    { NJ_C_FORM, "append", "(L" NJ_C_IMAGE ";)I", form_append_img },
    { NJ_C_FORM, "append", "(L" NJ_C_ITEM ";)I", form_append_item },
    { NJ_C_FORM, "insert", "(ILjava/lang/String;)V", form_insert_str },
    { NJ_C_FORM, "insert", "(IL" NJ_C_IMAGE ";)V", form_insert_img },
    { NJ_C_FORM, "insert", "(IL" NJ_C_ITEM ";)V", form_insert_item },
    { NJ_C_FORM, "delete", "(I)V", form_delete },
    { NJ_C_FORM, "deleteAll", "()V", form_deleteAll },
    { NJ_C_FORM, "size", "()I", form_size },
    { NJ_C_FORM, "get", "(I)L" NJ_C_ITEM ";", form_get },
    { NJ_C_FORM, "set", "(IL" NJ_C_ITEM ";)V", form_set },
    { NJ_C_FORM, "setItemStateListener", "(L" NJ_C_ITEMSTATELISTENER ";)V", form_setItemStateListener },

    { NJ_C_LIST, "<init>", "(Ljava/lang/String;I)V", list_init },
    { NJ_C_LIST, "<init>", "(Ljava/lang/String;I[Ljava/lang/String;[L" NJ_C_IMAGE ";)V", list_init_arrays },
    { NJ_C_LIST, "getType", "()I", list_getType },
    { NJ_C_LIST, "append", "(Ljava/lang/String;)I", list_append_str },
    { NJ_C_LIST, "append", "(Ljava/lang/String;L" NJ_C_IMAGE ";)I", list_append_img },
    { NJ_C_LIST, "insert", "(ILjava/lang/String;)V", list_insert_str },
    { NJ_C_LIST, "insert", "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", list_insert_img },
    { NJ_C_LIST, "delete", "(I)V", list_delete },
    { NJ_C_LIST, "deleteAll", "()V", list_deleteAll },
    { NJ_C_LIST, "size", "()I", list_size },
    { NJ_C_LIST, "getString", "(I)Ljava/lang/String;", list_getString },
    { NJ_C_LIST, "setString", "(ILjava/lang/String;)V", list_setString },
    { NJ_C_LIST, "getImage", "(I)L" NJ_C_IMAGE ";", list_getImage },
    { NJ_C_LIST, "setImage", "(IL" NJ_C_IMAGE ";)V", list_setImage },
    { NJ_C_LIST, "getSelectedIndex", "()I", list_getSelectedIndex },
    { NJ_C_LIST, "setSelectedIndex", "(I)V", list_setSelectedIndex },
    { NJ_C_LIST, "setSelectedIndex", "(IZ)V", list_setSelectedIndex2 },
    { NJ_C_LIST, "isSelected", "(I)Z", list_isSelected },
    { NJ_C_LIST, "setSelectedFlags", "([Z)V", list_setSelectedFlags },
    { NJ_C_LIST, "set", "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", list_set },
    { NJ_C_LIST, "setSelectCommand", "(L" NJ_C_COMMAND ";)V", list_setSelectCommand },
    { NJ_C_LIST, "getSelectedFlags", "([Z)I", list_getSelectedFlags },
    { NJ_C_LIST, "setFitPolicy", "(I)V", list_setFitPolicy },
    { NJ_C_LIST, "getFitPolicy", "()I", list_getFitPolicy },
    { NJ_C_LIST, "setFont", "(IL" NJ_C_FONT ";)V", list_setFont },
    { NJ_C_LIST, "getFont", "(I)L" NJ_C_FONT ";", list_getFont },
    { NJ_C_LIST, "getSelectCommand", "()L" NJ_C_COMMAND ";", list_getSelectCommand },

    { NJ_C_TEXTBOX, "<init>", "(Ljava/lang/String;Ljava/lang/String;II)V", tb_init },
    { NJ_C_TEXTBOX, "getString", "()Ljava/lang/String;", tb_getString },
    { NJ_C_TEXTBOX, "setString", "(Ljava/lang/String;)V", tb_setString },
    { NJ_C_TEXTBOX, "getChars", "([C)I", tb_getChars },
    { NJ_C_TEXTBOX, "insert", "(Ljava/lang/String;I)V", tb_insert_str },
    { NJ_C_TEXTBOX, "setChars", "([CII)V", tb_setChars },
    { NJ_C_TEXTBOX, "insert", "([CIII)V", tb_insert },
    { NJ_C_TEXTBOX, "delete", "(II)V", tb_delete },
    { NJ_C_TEXTBOX, "size", "()I", tb_size },
    { NJ_C_TEXTBOX, "getMaxSize", "()I", tb_getMaxSize },
    { NJ_C_TEXTBOX, "setMaxSize", "(I)V", tb_setMaxSize },
    { NJ_C_TEXTBOX, "getConstraints", "()I", tb_getConstraints },
    { NJ_C_TEXTBOX, "setConstraints", "(I)V", tb_setConstraints },
    { NJ_C_TEXTBOX, "setInitialInputMode", "(Ljava/lang/String;)V", tb_setInitialInputMode },
    { NJ_C_TEXTBOX, "setTicker", "(L" NJ_C_TICKER ";)V", tb_setTicker },
    { NJ_C_TEXTBOX, "getTicker", "()L" NJ_C_TICKER ";", tb_getTicker },
    { NJ_C_TEXTBOX, "getCaretPosition", "()I", tb_getCaretPosition },

    { NJ_C_ALERT, "<init>", "(Ljava/lang/String;)V", alert_init1 },
    { NJ_C_ALERT, "<init>", "(Ljava/lang/String;Ljava/lang/String;L" NJ_C_IMAGE ";L" NJ_C_ALERTTYPE ";)V", alert_init4 },
    { NJ_C_ALERT, "getType", "()L" NJ_C_ALERTTYPE ";", alert_getType },
    { NJ_C_ALERT, "setType", "(L" NJ_C_ALERTTYPE ";)V", alert_setType },
    { NJ_C_ALERT, "getString", "()Ljava/lang/String;", alert_getString },
    { NJ_C_ALERT, "setString", "(Ljava/lang/String;)V", alert_setString },
    { NJ_C_ALERT, "getImage", "()L" NJ_C_IMAGE ";", alert_getImage },
    { NJ_C_ALERT, "setImage", "(L" NJ_C_IMAGE ";)V", alert_setImage },
    { NJ_C_ALERT, "getTimeout", "()I", alert_getTimeout },
    { NJ_C_ALERT, "setTimeout", "(I)V", alert_setTimeout },
    { NJ_C_ALERT, "getDefaultTimeout", "()I", alert_getDefaultTimeout },
    { NJ_C_ALERT, "setDefaultTimeout", "(I)V", alert_setDefaultTimeout },
    { NJ_C_ALERT, "addCommand", "(L" NJ_C_COMMAND ";)V", d_addCommand },
    { NJ_C_ALERT, "removeCommand", "(L" NJ_C_COMMAND ";)V", d_removeCommand },
    { NJ_C_ALERT, "setCommandListener", "(L" NJ_C_COMMANDLISTENER ";)V", d_setCommandListener },
    { NJ_C_ALERT, "setIndicator", "(L" NJ_C_GAUGE ";)V", alert_setIndicator },
    { NJ_C_ALERT, "getIndicator", "()L" NJ_C_GAUGE ";", alert_getIndicator },

    { NJ_C_ALERTTYPE, "<init>", "()V", alerttype_init0 },
    { NJ_C_ALERTTYPE, "<init>", "(I)V", alerttype_init },
    { NJ_C_ALERTTYPE, "playSound", "(L" NJ_C_DISPLAY ";)Z", alerttype_playSound },
    { NJ_C_ALERTTYPE, "getTypeName", "(L" NJ_C_ALERTTYPE ";)Ljava/lang/String;", alerttype_getTypeName },
    { NJ_C_ALERTTYPE, "getTypeName", "()Ljava/lang/String;", alerttype_getTypeName },

    { NJ_C_DISPLAY, "<init>", "()V", no_public_constructor },
    { NJ_C_DISPLAY, "getDisplay", "(Ljavax/microedition/midlet/MIDlet;)L" NJ_C_DISPLAY ";", disp_getDisplay },
    { NJ_C_DISPLAY, "setCurrent", "(L" NJ_C_DISPLAYABLE ";)V", disp_setCurrent },
    { NJ_C_DISPLAY, "setCurrent", "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", disp_setCurrent_alert },
    { NJ_C_DISPLAY, "setCurrentItem", "(L" NJ_C_ITEM ";)V", disp_setCurrentItem },
    { NJ_C_DISPLAY, "getCurrent", "()L" NJ_C_DISPLAYABLE ";", disp_getCurrent },
    { NJ_C_DISPLAY, "callSerially", "(Ljava/lang/Runnable;)V", disp_callSerially },
    { NJ_C_DISPLAY, "isColor", "()Z", disp_isColor },
    { NJ_C_DISPLAY, "numColors", "()I", disp_numColors },
    { NJ_C_DISPLAY, "numAlphaLevels", "()I", disp_numAlphaLevels },
    { NJ_C_DISPLAY, "flashBacklight", "(I)Z", disp_flashBacklight },
    { NJ_C_DISPLAY, "flashBacklight", "(I)V", disp_flashBacklight },
    { NJ_C_DISPLAY, "isColor", "()Z", disp_isColor },
    { NJ_C_DISPLAY, "numColors", "()I", disp_numColors },
    { NJ_C_DISPLAY, "numAlphaLevels", "()I", disp_numAlphaLevels },
    { NJ_C_DISPLAY, "vibrate", "(I)Z", disp_vibrate },
    { NJ_C_DISPLAY, "vibrate", "(I)Z", disp_vibrate },
    { NJ_C_DISPLAY, "getBestImageWidth", "(I)I", disp_getBestImageWidth },
    { NJ_C_DISPLAY, "getBestImageHeight", "(I)I", disp_getBestImageHeight },
    { NJ_C_DISPLAY, "getColor", "(I)I", disp_getColor },
    { NJ_C_DISPLAY, "getColor", "(I)Z", disp_getColor_bool },
    { NJ_C_DISPLAY, "getBorderStyle", "(Z)I", disp_getBorderStyle }
};
const int nj_lib_lcdui_count =
    (int)(sizeof(nj_lib_lcdui_table) / sizeof(nj_lib_lcdui_table[0]));

/* ================================================================== */
/* Late static initialisation                                          */
/* ================================================================== */
/*
 * A builtin class has no bytecode and therefore no <clinit> and no
 * ConstantValue attributes, so everything that would be `static final int X = n`
 * in Java is written into the static slot here.  This runs from
 * nj_lib_late_init() once every builtin class has been created and linked, and
 * because the slots live in vm->statics nothing survives nj_vm_destroy().
 */
static void lcdui_const_int(nj_vm *vm, const char *cls, const char *name,
                            nj_s4 value)
{
    nj_field *f = nj_field_lookup(vm, cls, name, D_INT, 1);
    if (f) {
        nj_field_write_int(vm, NULL, f, value);
    }
}

/*
 * Build one AlertType singleton and store it in its static field.
 *
 * NOTE: late init runs before the VM is marked running, so nj_call() cannot be
 * used here - it would return NJ_ERR_STATE and the singleton would silently stay
 * null.  The objects are therefore filled in field by field, which is what the
 * constructor native does anyway.
 */
static void lcdui_alert_type(nj_vm *vm, const char *field, nj_s4 kind)
{
    nj_ref t = nj_object_new(vm, NJ_C_ALERTTYPE);

    if (!t) {
        nj_clear_exception(vm);
        return;
    }
    fput(vm, t, NJ_C_ALERTTYPE, "atType", D_INT, kind);
    rputs(vm, NJ_C_ALERTTYPE, field, "L" NJ_C_ALERTTYPE ";", t);
}

/* Build the List.SELECT_COMMAND singleton. */
/* Fill in a Command directly (see the note on lcdui_alert_type). */
static nj_ref lcdui_make_command(nj_vm *vm, const char *label_text, nj_s4 type,
                                 nj_s4 priority)
{
    nj_ref c = nj_object_new(vm, NJ_C_COMMAND);
    nj_ref label;

    if (!c) {
        nj_clear_exception(vm);
        return NULL;
    }
    label = nj_string_new(vm, label_text);
    if (!label) {
        nj_clear_exception(vm);
        return NULL;
    }
    fput(vm, c, NJ_C_COMMAND, "cmdType", D_INT, type);
    rput(vm, c, NJ_C_COMMAND, "cmdLabel", D_STR, label);
    fput(vm, c, NJ_C_COMMAND, "cmdPriority", D_INT, priority);
    fput(vm, c, NJ_C_COMMAND, "cmdLongLabel", D_INT, 0);
    rput(vm, c, NJ_C_COMMAND, "cmdLong", D_STR, NULL);
    fput(vm, c, NJ_C_COMMAND, "cmdMnemonic", D_INT, 0);
    return c;
}

static void lcdui_select_command(nj_vm *vm)
{
    nj_ref c = lcdui_make_command(vm, "Select", 8 /* Command.ITEM */, 0);
    if (c) {
        rputs(vm, NJ_C_LIST, "SELECT_COMMAND", D_CMD, c);
    }
}

/*
 * The LCDUI half of nj_lib_late_init().  Exported so nj_lib_system.c can call
 * it with one line; it is deliberately idempotent and safe to call before any
 * MIDlet exists.
 */
nj_status nj_lcdui_late_init(nj_vm *vm)
{
    if (!vm) {
        return NJ_ERR;
    }

    /* ---- Font ---- */
    lcdui_const_int(vm, NJ_C_FONT, "FACE_SYSTEM",       0);
    lcdui_const_int(vm, NJ_C_FONT, "FACE_MONOSPACED",   32);
    lcdui_const_int(vm, NJ_C_FONT, "FACE_MONOSPACE",    32);
    lcdui_const_int(vm, NJ_C_FONT, "FACE_PROPORTIONAL", 64);
    lcdui_const_int(vm, NJ_C_FONT, "STYLE_PLAIN",       0);
    lcdui_const_int(vm, NJ_C_FONT, "STYLE_BOLD",        1);
    lcdui_const_int(vm, NJ_C_FONT, "STYLE_ITALIC",      2);
    lcdui_const_int(vm, NJ_C_FONT, "STYLE_UNDERLINED",  4);
    lcdui_const_int(vm, NJ_C_FONT, "SIZE_SMALL",        8);
    lcdui_const_int(vm, NJ_C_FONT, "SIZE_MEDIUM",       0);
    lcdui_const_int(vm, NJ_C_FONT, "SIZE_LARGE",        16);

    /* ---- Graphics anchors and styles ---- */
    /*
     * These are the values MIDP 2.0 defines, and they are NOT free choices: a
     * MIDlet compiles Graphics.LEFT into a literal, so a runtime that calls it
     * something else answers a different question than the caller asked.
     *
     * They are also, deliberately, the same numbers as NJ_ANCHOR_* in
     * include/nj2me/nj_gfx.h, so no translation is needed for the horizontal
     * half.  The evidence is a scan of 12476 compiled classes from the 395
     * titles in tests/games/, collecting the constant that precedes every
     * drawString/drawImage/drawRegion call: 20 (TOP|LEFT) leads with 11342
     * occurrences, then 0, 17 (TOP|HCENTER), 3 (HCENTER|VCENTER), 36
     * (BOTTOM|LEFT), 33, 40 (BOTTOM|RIGHT), 24 (TOP|RIGHT), 68
     * (BASELINE|LEFT).  Every one is a legal MIDP anchor under THIS numbering
     * and several are illegal under any other -- 20 would be RIGHT|VCENTER,
     * which MIDP forbids for text.
     *
     * A commit once "corrected" these to LEFT 1 / HCENTER 2 / RIGHT 4 / TOP 8
     * with a matching translation, on the theory that the renderer's numbering
     * was private.  It made things worse: MIDlets inlined the real values while
     * the validator rejected them (HCENTER|VCENTER became LEFT|HCENTER,
     * "contradictory"), so titles threw IllegalArgumentException on every
     * frame instead of drawing.
     *
     * Whatever these numbers are, tools/midp_stubs.py must say the same thing,
     * because javac inlines the stub's value into the MIDlet.
     */
    lcdui_const_int(vm, NJ_C_GRAPHICS, "HCENTER",  1);
    lcdui_const_int(vm, NJ_C_GRAPHICS, "VCENTER",  2);
    lcdui_const_int(vm, NJ_C_GRAPHICS, "LEFT",     4);
    lcdui_const_int(vm, NJ_C_GRAPHICS, "RIGHT",    8);
    lcdui_const_int(vm, NJ_C_GRAPHICS, "TOP",      16);
    lcdui_const_int(vm, NJ_C_GRAPHICS, "BOTTOM",   32);
    lcdui_const_int(vm, NJ_C_GRAPHICS, "BASELINE", 64);
    lcdui_const_int(vm, NJ_C_GRAPHICS, "SOLID",    0);
    lcdui_const_int(vm, NJ_C_GRAPHICS, "DOTTED",   1);

    /* ---- Canvas key codes and game actions ---- */
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM0",  48);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM1",  49);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM2",  50);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM3",  51);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM4",  52);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM5",  53);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM6",  54);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM7",  55);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM8",  56);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_NUM9",  57);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_STAR",  42);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_POUND", 35);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_UP",    -1);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_DOWN",  -2);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_LEFT",  -3);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_RIGHT", -4);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_FIRE",  -5);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_SOFTKEY1", -6);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_SOFTKEY2", -7);
    lcdui_const_int(vm, NJ_C_CANVAS, "KEY_SOFTKEY3", -8);
    lcdui_const_int(vm, NJ_C_CANVAS, "UP",    1);
    lcdui_const_int(vm, NJ_C_CANVAS, "DOWN",  6);
    lcdui_const_int(vm, NJ_C_CANVAS, "LEFT",  2);
    lcdui_const_int(vm, NJ_C_CANVAS, "RIGHT", 5);
    lcdui_const_int(vm, NJ_C_CANVAS, "FIRE",  8);
    lcdui_const_int(vm, NJ_C_CANVAS, "GAME_A", 9);
    lcdui_const_int(vm, NJ_C_CANVAS, "GAME_B", 10);
    lcdui_const_int(vm, NJ_C_CANVAS, "GAME_C", 11);
    lcdui_const_int(vm, NJ_C_CANVAS, "GAME_D", 12);

    /* ---- Command ---- */
    lcdui_const_int(vm, NJ_C_COMMAND, "SCREEN", 1);
    lcdui_const_int(vm, NJ_C_COMMAND, "BACK",   2);
    lcdui_const_int(vm, NJ_C_COMMAND, "CANCEL", 3);
    lcdui_const_int(vm, NJ_C_COMMAND, "OK",     4);
    lcdui_const_int(vm, NJ_C_COMMAND, "HELP",   5);
    lcdui_const_int(vm, NJ_C_COMMAND, "STOP",   6);
    lcdui_const_int(vm, NJ_C_COMMAND, "EXIT",   7);
    lcdui_const_int(vm, NJ_C_COMMAND, "ITEM",   8);
    lcdui_const_int(vm, NJ_C_COMMAND, "PRIORITY_1", 1);
    lcdui_const_int(vm, NJ_C_COMMAND, "PRIORITY_2", 2);

    /* ---- Item layout ---- */
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_DEFAULT",         0);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_LEFT",            1);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_RIGHT",           2);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_CENTER",          3);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_TOP",             16);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_BOTTOM",          32);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_VCENTER",         48);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_NEWLINE_BEFORE",  256);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_NEWLINE_AFTER",   512);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_SHRINK",          1024);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_VSHRINK",         2048);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_EXPAND",          4096);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_VEXPAND",         8192);
    lcdui_const_int(vm, NJ_C_ITEM, "LAYOUT_2",               16384);

    /* ---- StringItem / ImageItem ---- */
    lcdui_const_int(vm, NJ_C_STRINGITEM, "PLAIN",     0);
    lcdui_const_int(vm, NJ_C_STRINGITEM, "HYPERLINK", 1);
    lcdui_const_int(vm, NJ_C_STRINGITEM, "BUTTON",    2);
    lcdui_const_int(vm, NJ_C_IMAGEITEM, "LAYOUT_DEFAULT", 0);
    lcdui_const_int(vm, NJ_C_IMAGEITEM, "LAYOUT_LEFT",    1);
    lcdui_const_int(vm, NJ_C_IMAGEITEM, "LAYOUT_RIGHT",   2);
    lcdui_const_int(vm, NJ_C_IMAGEITEM, "LAYOUT_CENTER",  3);
    lcdui_const_int(vm, NJ_C_IMAGEITEM, "LAYOUT_NEWLINE_BEFORE", 256);
    lcdui_const_int(vm, NJ_C_IMAGEITEM, "LAYOUT_NEWLINE_AFTER",  512);

    /* ---- Gauge ---- */
    lcdui_const_int(vm, NJ_C_GAUGE, "INDEFINITE",             -1);
    lcdui_const_int(vm, NJ_C_GAUGE, "CONTINUOUS_IDLE",         0);
    lcdui_const_int(vm, NJ_C_GAUGE, "INCREMENTAL_IDLE",        1);
    lcdui_const_int(vm, NJ_C_GAUGE, "CONTINUOUS_RUNNING",      2);
    lcdui_const_int(vm, NJ_C_GAUGE, "INCREMENTAL_UPDATING",    3);

    /* ---- ChoiceGroup / List ---- */
    lcdui_const_int(vm, NJ_C_CHOICEGROUP, "EXCLUSIVE", NJ_CHOICE_EXCLUSIVE);
    lcdui_const_int(vm, NJ_C_CHOICEGROUP, "MULTIPLE",  NJ_CHOICE_MULTIPLE);
    lcdui_const_int(vm, NJ_C_CHOICEGROUP, "IMPLICIT",  NJ_CHOICE_IMPLICIT);
    lcdui_const_int(vm, NJ_C_CHOICEGROUP, "TEXT_WRAP_DEFAULT", 0);
    lcdui_const_int(vm, NJ_C_CHOICEGROUP, "TEXT_WRAP_ON",      1);
    lcdui_const_int(vm, NJ_C_CHOICEGROUP, "TEXT_WRAP_OFF",     2);
    lcdui_const_int(vm, "javax/microedition/lcdui/Choice", "EXCLUSIVE", 1);
    lcdui_const_int(vm, "javax/microedition/lcdui/Choice", "MULTIPLE", 2);
    lcdui_const_int(vm, "javax/microedition/lcdui/Choice", "IMPLICIT", 3);
    lcdui_const_int(vm, "javax/microedition/lcdui/Choice", "TEXT_WRAP_DEFAULT", 0);
    lcdui_const_int(vm, "javax/microedition/lcdui/Choice", "TEXT_WRAP_ON", 1);
    lcdui_const_int(vm, "javax/microedition/lcdui/Choice", "TEXT_WRAP_OFF", 2);
    lcdui_const_int(vm, NJ_C_ALERT, "FOREVER", -2);
    lcdui_const_int(vm, NJ_C_LIST, "EXCLUSIVE", NJ_CHOICE_EXCLUSIVE);
    lcdui_const_int(vm, NJ_C_LIST, "MULTIPLE",  NJ_CHOICE_MULTIPLE);
    lcdui_const_int(vm, NJ_C_LIST, "IMPLICIT",  NJ_CHOICE_IMPLICIT);

    lcdui_const_int(vm, NJ_C_LIST, "TEXT_WRAP_DEFAULT", 0);
    lcdui_const_int(vm, NJ_C_LIST, "TEXT_WRAP_ON",      1);
    lcdui_const_int(vm, NJ_C_LIST, "TEXT_WRAP_OFF",     2);

    /* ---- TextField / TextBox ---- */
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "ANY",         0);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "EMAILADDR",   1);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "NUMERIC",     2);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "PHONENUMBER", 3);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "URL",         4);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "DECIMAL",     5);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "PASSWORD",    0x10000);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "UNEDITABLE",  0x20000);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "SENSITIVE",   0x40000);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "NON_PREDICTIVE", 0x80000);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "INITIAL_CAPS_WORD", 0x100000);
    lcdui_const_int(vm, NJ_C_TEXTFIELD, "INITIAL_CAPS_SENTENCE", 0x200000);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "ANY",         0);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "EMAILADDR",   1);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "NUMERIC",     2);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "PHONENUMBER", 3);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "URL",         4);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "DECIMAL",     5);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "PASSWORD",    0x10000);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "UNEDITABLE",  0x20000);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "SENSITIVE",   0x40000);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "NON_PREDICTIVE", 0x80000);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "INITIAL_CAPS_WORD", 0x100000);
    lcdui_const_int(vm, NJ_C_TEXTBOX, "INITIAL_CAPS_SENTENCE", 0x200000);

    /* ---- DateField ---- */
    lcdui_const_int(vm, NJ_C_DATEFIELD, "DATE",      1);
    lcdui_const_int(vm, NJ_C_DATEFIELD, "TIME",      2);
    lcdui_const_int(vm, NJ_C_DATEFIELD, "DATE_TIME", 3);

    /* ---- Display ---- */
    lcdui_const_int(vm, NJ_C_DISPLAY, "LIST_ELEMENT",  0);
    lcdui_const_int(vm, NJ_C_DISPLAY, "TEXT_ELEMENT",  1);
    lcdui_const_int(vm, NJ_C_DISPLAY, "IMAGE_ELEMENT", 2);
    lcdui_const_int(vm, NJ_C_DISPLAY, "COLOR_BACKGROUND",             0);
    lcdui_const_int(vm, NJ_C_DISPLAY, "COLOR_FOREGROUND",             1);
    lcdui_const_int(vm, NJ_C_DISPLAY, "COLOR_HIGHLIGHTED_BACKGROUND", 2);
    lcdui_const_int(vm, NJ_C_DISPLAY, "COLOR_HIGHLIGHTED_FOREGROUND", 3);
    lcdui_const_int(vm, NJ_C_DISPLAY, "COLOR_BORDER",                 4);
    lcdui_const_int(vm, NJ_C_DISPLAY, "COLOR_HIGHLIGHTED_BORDER",     5);

    /* ---- the AlertType singletons and the List select command ---- */
    lcdui_alert_type(vm, "INFO",         0);
    lcdui_alert_type(vm, "WARNING",      1);
    lcdui_alert_type(vm, "ERROR",        2);
    lcdui_alert_type(vm, "ALARM",        3);
    lcdui_alert_type(vm, "CONFIRMATION", 4);
    lcdui_select_command(vm);
    sput(vm, NJ_C_ALERT, "sDefaultTimeout", D_INT, 2000);

    /* The default Alert type is INFO, and DISMISS_COMMAND is what an Alert
     * with OK/BACK semantics delivers; both are singletons. */
    {
        nj_ref dismiss = lcdui_make_command(vm, "Dismiss", 4 /* Command.OK */, 0);
        if (dismiss) {
            rputs(vm, NJ_C_ALERT, "DISMISS_COMMAND", D_CMD, dismiss);
        }
    }

    /* Make sure the font registry exists so the first paint has metrics. */
    if (!vm->fonts.initialized) {
        nj_fontset_init(&vm->fonts);
    }
    return NJ_OK;
}
