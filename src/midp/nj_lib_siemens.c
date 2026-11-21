/*
 * nano_j2me - src/midp/nj_lib_siemens.c
 *
 * Siemens' `com.siemens.mp.color_game` and `com.siemens.mp.game` classes.
 *
 * WHY THIS EXISTS
 * ---------------
 * The `resource:` Connector scheme fix let Wizardry load its data files, and
 * it then died on `NoClassDefFoundError: com/siemens/mp/game/Light`.  The
 * title renders its screens through Siemens' layer manager: Layers and
 * Sprites composited by a LayerManager, with ExtendedImage for the direct
 * screen blits and Light for the screen backlight.  A device that has no
 * light sensor is a normal device, but a title that asks for this API should
 * get a working compositor, not a linkage error that aborts its game thread.
 *
 * WHAT IS IMPLEMENTED
 * -------------------
 * The subset the corpus references (checked with javap over every class of
 * the three Siemens titles): Layer, LayerManager, Sprite, ExtendedImage and
 * Light.  LayerManager reproduces the reference paint order (last layer
 * first, visible layers only) and the reference clip/translate bookkeeping.
 * Sprite reproduces the reference frame grid: `Sprite(Image)` treats the
 * whole image as one frame, `Sprite(Image,w,h)` splits it into a
 * `(image.width/w) x (image.height/h)` grid, `setFrame()` selects one.
 * ExtendedImage.blitToScreen() draws into the current Canvas' backing buffer
 * (which is what the frame pump presents) and asks for a repaint;
 * ExtendedImage.clear() fills the wrapped image white for 0 and black
 * otherwise, matching the reference.  The screen backlight cannot be
 * controlled here, so Light's two static calls are documented no-ops.
 *
 * What is missing from the reference is deliberately limited: Sprite frame
 * sequences, collision rectangles and transforms are not referenced by the
 * corpus and would only add untested surface.  A title that calls them gets
 * NoSuchMethodError at its own call site, which is visible in a log rather
 * than an aborting linkage error at load time.
 */
#include "nj_midp.h"

#define NJFN(name) NJ_NATIVE_FN(name)

#define NJ_C_SILAYER  "com/siemens/mp/color_game/Layer"
#define NJ_C_SILMGR   "com/siemens/mp/color_game/LayerManager"
#define NJ_C_SISPRITE "com/siemens/mp/color_game/Sprite"
#define NJ_C_SIXIMG   "com/siemens/mp/game/ExtendedImage"
#define NJ_C_SILIGHT  "com/siemens/mp/game/Light"
#define NJ_C_IMAGE    "javax/microedition/lcdui/Image"
#define NJ_C_GRAPHICS "javax/microedition/lcdui/Graphics"
#define NJ_C_VECTOR   "java/util/Vector"
#define NJ_C_CANVAS   "javax/microedition/lcdui/Canvas"

/* Reference values: Graphics.TOP and Graphics.LEFT. */
#define SIEMENS_ANCHOR_TOP_LEFT 20

static nj_field *siemens_f(nj_vm *vm, const char *cls, const char *name,
                           const char *desc, int is_static)
{
    return nj_field_lookup(vm, cls, name, desc, is_static);
}

static nj_ref siemens_vector_new(nj_vm *vm)
{
    nj_u4 mark;
    nj_ref v;

    mark = nj_root_mark(vm);
    v = nj_root(vm, nj_object_new(vm, NJ_C_VECTOR));
    if (!v) {
        nj_root_release(vm, mark);
        return NULL;
    }
    if (nj_call_special(vm, v, NJ_C_VECTOR, "<init>", "()V", NULL, NULL)
        != NJ_OK) {
        nj_root_release(vm, mark);
        return NULL;
    }
    nj_root_release(vm, mark);
    return v;
}

/* ------------------------------------------------------------------ */
/* Layer                                                               */
/* ------------------------------------------------------------------ */
static void layer_set_int(nj_vm *vm, nj_ref self, const char *name, nj_s4 v)
{
    nj_field *f = siemens_f(vm, NJ_C_SILAYER, name, "I", 0);
    if (f) {
        nj_iput(vm, self, f, v);
    }
}

static nj_s4 layer_get_int(nj_vm *vm, nj_ref self, const char *name)
{
    nj_field *f = siemens_f(vm, NJ_C_SILAYER, name, "I", 0);
    return f ? nj_iget(vm, self, f) : 0;
}

static void layer_set_visible(nj_vm *vm, nj_ref self, int on)
{
    nj_field *f = siemens_f(vm, NJ_C_SILAYER, "visible", "Z", 0);
    if (f) {
        nj_iput(vm, self, f, on ? 1 : 0);
    }
}

NJFN(siemens_layer_init)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 w = NJ_ARG_INT(args, 0), h = NJ_ARG_INT(args, 1);

    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative layer size");
    }
    layer_set_int(vm, self, "x", 0);
    layer_set_int(vm, self, "y", 0);
    layer_set_int(vm, self, "width", w);
    layer_set_int(vm, self, "height", h);
    layer_set_visible(vm, self, 1);
    return NJ_OK;
}

NJFN(siemens_layer_set_position)
{
    nj_ref self = NJ_THIS(args);
    layer_set_int(vm, self, "x", NJ_ARG_INT(args, 0));
    layer_set_int(vm, self, "y", NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(siemens_layer_move)
{
    nj_ref self = NJ_THIS(args);
    layer_set_int(vm, self, "x",
                  layer_get_int(vm, self, "x") + NJ_ARG_INT(args, 0));
    layer_set_int(vm, self, "y",
                  layer_get_int(vm, self, "y") + NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(siemens_layer_get_x)
{
    ret->i = layer_get_int(vm, NJ_THIS(args), "x");
    return NJ_OK;
}

NJFN(siemens_layer_get_y)
{
    ret->i = layer_get_int(vm, NJ_THIS(args), "y");
    return NJ_OK;
}

NJFN(siemens_layer_get_width)
{
    ret->i = layer_get_int(vm, NJ_THIS(args), "width");
    return NJ_OK;
}

NJFN(siemens_layer_get_height)
{
    ret->i = layer_get_int(vm, NJ_THIS(args), "height");
    return NJ_OK;
}

NJFN(siemens_layer_set_visible)
{
    layer_set_visible(vm, NJ_THIS(args), NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(siemens_layer_is_visible)
{
    nj_field *f = siemens_f(vm, NJ_C_SILAYER, "visible", "Z", 0);
    ret->i = f ? (nj_iget(vm, NJ_THIS(args), f) != 0) : 0;
    return NJ_OK;
}

NJFN(siemens_layer_set_width)
{
    nj_s4 w = NJ_ARG_INT(args, 0);
    if (w < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative layer width");
    }
    layer_set_int(vm, NJ_THIS(args), "width", w);
    return NJ_OK;
}

NJFN(siemens_layer_set_height)
{
    nj_s4 h = NJ_ARG_INT(args, 0);
    if (h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative layer height");
    }
    layer_set_int(vm, NJ_THIS(args), "height", h);
    return NJ_OK;
}

/* Layer declares paint() abstract in the reference API.  Nothing in the
 * corpus calls it on a bare Layer, but a Java subclass that forgets to
 * override it should get a clear Java exception rather than a crash. */
NJFN(siemens_layer_paint)
{
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "Layer.paint is abstract");
}

/* ------------------------------------------------------------------ */
/* LayerManager                                                        */
/* ------------------------------------------------------------------ */
static nj_ref lm_layers(nj_vm *vm, nj_ref self, int create)
{
    nj_field *f = siemens_f(vm, NJ_C_SILMGR, "layers", "L" NJ_C_VECTOR ";", 0);
    nj_ref v;

    if (!f) {
        return NULL;
    }
    v = nj_field_read_ref(vm, self, f);
    if (!v && create) {
        v = siemens_vector_new(vm);
        if (v) {
            nj_field_write_ref(vm, self, f, v);
        }
    }
    return v;
}

static nj_s4 lm_get_int(nj_vm *vm, nj_ref self, const char *name)
{
    nj_field *f = siemens_f(vm, NJ_C_SILMGR, name, "I", 0);
    return f ? nj_iget(vm, self, f) : 0;
}

static void lm_set_int(nj_vm *vm, nj_ref self, const char *name, nj_s4 v)
{
    nj_field *f = siemens_f(vm, NJ_C_SILMGR, name, "I", 0);
    if (f) {
        nj_iput(vm, self, f, v);
    }
}

NJFN(siemens_lm_init)
{
    nj_ref self = NJ_THIS(args);

    lm_set_int(vm, self, "viewX", 0);
    lm_set_int(vm, self, "viewY", 0);
    lm_set_int(vm, self, "viewWidth", 0x7FFFFFFF);
    lm_set_int(vm, self, "viewHeight", 0x7FFFFFFF);
    return NJ_OK;
}

static nj_status lm_remove_impl(nj_vm *vm, nj_ref self, nj_ref layer)
{
    nj_ref vec;
    nj_value a[1];
    nj_s4 index;
    nj_u4 mark;

    if (!layer) {
        return nj_throw(vm, "java/lang/NullPointerException", "layer");
    }
    vec = lm_layers(vm, self, 0);
    if (!vec) {
        return NJ_OK;
    }
    mark = nj_root_mark(vm);
    vec = nj_root(vm, vec);
    layer = nj_root(vm, layer);
    a[0].r = layer;
    index = nj_call_int(vm, vec, NJ_C_VECTOR, "indexOf",
                        "(Ljava/lang/Object;)I", a);
    if (index >= 0 && !nj_has_exception(vm)) {
        a[0].i = index;
        (void)nj_call(vm, vec, NJ_C_VECTOR, "removeElementAt", "(I)V", a, NULL);
    }
    nj_root_release(vm, mark);
    return nj_has_exception(vm) ? NJ_ERR : NJ_OK;
}

NJFN(siemens_lm_append)
{
    nj_ref self = NJ_THIS(args), layer = NJ_ARG_REF(args, 0);
    nj_ref vec;
    nj_value a[1];
    nj_u4 mark;
    nj_status st;

    st = lm_remove_impl(vm, self, layer);
    if (st != NJ_OK) {
        return st;
    }
    vec = lm_layers(vm, self, 1);
    if (!vec) {
        return NJ_ERR;
    }
    mark = nj_root_mark(vm);
    vec = nj_root(vm, vec);
    layer = nj_root(vm, layer);
    a[0].r = layer;
    (void)nj_call(vm, vec, NJ_C_VECTOR, "addElement",
                  "(Ljava/lang/Object;)V", a, NULL);
    nj_root_release(vm, mark);
    return nj_has_exception(vm) ? NJ_ERR : NJ_OK;
}

NJFN(siemens_lm_insert)
{
    nj_ref self = NJ_THIS(args), layer = NJ_ARG_REF(args, 0);
    nj_s4 index = NJ_ARG_INT(args, 1);
    nj_ref vec;
    nj_value a[2];
    nj_u4 mark;
    nj_s4 n;
    nj_status st;

    /* Reference semantics: the index must be in [0, size]. */
    vec = lm_layers(vm, self, 0);
    n = vec ? nj_call_int(vm, vec, NJ_C_VECTOR, "size", "()I", NULL) : 0;
    if (vec && nj_has_exception(vm)) {
        return NJ_ERR;
    }
    if (index < 0 || index > n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException",
                        "layer index");
    }
    st = lm_remove_impl(vm, self, layer);
    if (st != NJ_OK) {
        return st;
    }
    vec = lm_layers(vm, self, 1);
    if (!vec) {
        return NJ_ERR;
    }
    mark = nj_root_mark(vm);
    vec = nj_root(vm, vec);
    layer = nj_root(vm, layer);
    a[0].i = index;
    a[1].r = layer;
    (void)nj_call(vm, vec, NJ_C_VECTOR, "insertElementAt",
                  "(Ljava/lang/Object;I)V", a, NULL);
    nj_root_release(vm, mark);
    return nj_has_exception(vm) ? NJ_ERR : NJ_OK;
}

NJFN(siemens_lm_get_layer)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 index = NJ_ARG_INT(args, 0);
    nj_ref vec = lm_layers(vm, self, 0);
    nj_value a[1];
    nj_s4 n = 0;

    if (vec) {
        n = nj_call_int(vm, vec, NJ_C_VECTOR, "size", "()I", NULL);
        if (nj_has_exception(vm)) {
            return NJ_ERR;
        }
    }
    if (index < 0 || index >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException",
                        "layer index");
    }
    a[0].i = index;
    ret->r = nj_call_ref(vm, vec, NJ_C_VECTOR, "elementAt",
                         "(I)Ljava/lang/Object;", a);
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(siemens_lm_get_size)
{
    nj_ref vec = lm_layers(vm, NJ_THIS(args), 0);
    ret->i = vec ? nj_call_int(vm, vec, NJ_C_VECTOR, "size", "()I", NULL) : 0;
    return nj_has_exception(vm) ? NJ_ERR : NJ_OK;
}

NJFN(siemens_lm_remove)
{
    return lm_remove_impl(vm, NJ_THIS(args), NJ_ARG_REF(args, 0));
}

/* Graphics helpers used by the reference paint algorithm. */
static nj_status g_call_int(nj_vm *vm, nj_ref g, const char *name,
                            nj_s4 *out)
{
    nj_s4 v = nj_call_int(vm, g, NJ_C_GRAPHICS, name, "()I", NULL);
    if (nj_has_exception(vm)) {
        return NJ_ERR;
    }
    *out = v;
    return NJ_OK;
}

static nj_status g_call4(nj_vm *vm, nj_ref g, const char *name,
                         nj_s4 a0, nj_s4 a1, nj_s4 a2, nj_s4 a3)
{
    nj_value a[4];

    a[0].i = a0;
    a[1].i = a1;
    a[2].i = a2;
    a[3].i = a3;
    (void)nj_call(vm, g, NJ_C_GRAPHICS, name, "(IIII)V", a, NULL);
    return nj_has_exception(vm) ? NJ_ERR : NJ_OK;
}

static nj_status g_call2(nj_vm *vm, nj_ref g, const char *name,
                         nj_s4 a0, nj_s4 a1)
{
    nj_value a[2];

    a[0].i = a0;
    a[1].i = a1;
    (void)nj_call(vm, g, NJ_C_GRAPHICS, name, "(II)V", a, NULL);
    return nj_has_exception(vm) ? NJ_ERR : NJ_OK;
}

NJFN(siemens_lm_paint)
{
    nj_ref self = NJ_THIS(args), g = NJ_ARG_REF(args, 0);
    nj_ref vec;
    nj_s4 px = NJ_ARG_INT(args, 1), py = NJ_ARG_INT(args, 2);
    nj_s4 clipX = 0, clipY = 0, clipW = 0, clipH = 0;
    nj_s4 viewX, viewY, viewW, viewH, n = 0, i;
    nj_u4 mark;
    nj_status st = NJ_OK;

    if (!g) {
        return nj_throw(vm, "java/lang/NullPointerException", "graphics");
    }
    vec = lm_layers(vm, self, 0);
    if (vec) {
        n = nj_call_int(vm, vec, NJ_C_VECTOR, "size", "()I", NULL);
        if (nj_has_exception(vm)) {
            return NJ_ERR;
        }
    }
    viewX = lm_get_int(vm, self, "viewX");
    viewY = lm_get_int(vm, self, "viewY");
    viewW = lm_get_int(vm, self, "viewWidth");
    viewH = lm_get_int(vm, self, "viewHeight");

    mark = nj_root_mark(vm);
    self = nj_root(vm, self);
    g = nj_root(vm, g);
    if (vec) {
        vec = nj_root(vm, vec);
    }

    /* Reference save/restore: translate by (x - viewX, y - viewY), clip to
     * the view window, draw last-to-first, then restore both. */
    if (g_call_int(vm, g, "getClipX", &clipX) != NJ_OK ||
        g_call_int(vm, g, "getClipY", &clipY) != NJ_OK ||
        g_call_int(vm, g, "getClipWidth", &clipW) != NJ_OK ||
        g_call_int(vm, g, "getClipHeight", &clipH) != NJ_OK ||
        g_call2(vm, g, "translate", px - viewX, py - viewY) != NJ_OK ||
        g_call4(vm, g, "clipRect", viewX, viewY, viewW, viewH) != NJ_OK) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }

    for (i = n; --i >= 0;) {
        nj_u4 m2 = nj_root_mark(vm);
        nj_value a[1];
        nj_ref layer;
        nj_field *vis;
        int visible = 0;

        if (!vec) {
            break;
        }
        a[0].i = i;
        layer = nj_call_ref(vm, vec, NJ_C_VECTOR, "elementAt",
                            "(I)Ljava/lang/Object;", a);
        if (!layer || nj_has_exception(vm)) {
            nj_root_release(vm, m2);
            st = NJ_ERR;
            break;
        }
        layer = nj_root(vm, layer);
        vis = siemens_f(vm, NJ_C_SILAYER, "visible", "Z", 0);
        if (vis) {
            visible = (nj_iget(vm, layer, vis) != 0);
        }
        if (visible) {
            nj_value ga[1];
            ga[0].r = g;
            (void)nj_call(vm, layer, nj_ref_class_name(layer), "paint",
                          "(L" NJ_C_GRAPHICS ";)V", ga, NULL);
            if (nj_has_exception(vm)) {
                nj_root_release(vm, m2);
                st = NJ_ERR;
                break;
            }
        }
        nj_root_release(vm, m2);
    }

    if (st == NJ_OK) {
        if (g_call2(vm, g, "translate", -px + viewX, -py + viewY) != NJ_OK ||
            g_call4(vm, g, "setClip", clipX, clipY, clipW, clipH) != NJ_OK) {
            st = NJ_ERR;
        }
    }
    nj_root_release(vm, mark);
    return st;
}

NJFN(siemens_lm_set_view)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 w = NJ_ARG_INT(args, 2), h = NJ_ARG_INT(args, 3);

    if (w < 0 || h < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative view window");
    }
    lm_set_int(vm, self, "viewX", NJ_ARG_INT(args, 0));
    lm_set_int(vm, self, "viewY", NJ_ARG_INT(args, 1));
    lm_set_int(vm, self, "viewWidth", w);
    lm_set_int(vm, self, "viewHeight", h);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Sprite                                                              */
/* ------------------------------------------------------------------ */
static nj_s4 sprite_get_int(nj_vm *vm, nj_ref self, const char *name)
{
    nj_field *f = siemens_f(vm, NJ_C_SISPRITE, name, "I", 0);
    return f ? nj_iget(vm, self, f) : 0;
}

static void sprite_set_int(nj_vm *vm, nj_ref self, const char *name, nj_s4 v)
{
    nj_field *f = siemens_f(vm, NJ_C_SISPRITE, name, "I", 0);
    if (f) {
        nj_iput(vm, self, f, v);
    }
}

static nj_ref sprite_image(nj_vm *vm, nj_ref self)
{
    nj_field *f = siemens_f(vm, NJ_C_SISPRITE, "sourceImage",
                            "L" NJ_C_IMAGE ";", 0);
    return f ? nj_field_read_ref(vm, self, f) : NULL;
}

static nj_status sprite_init(nj_vm *vm, nj_ref self, nj_ref img,
                             nj_s4 fw, nj_s4 fh)
{
    int iw = 0, ih = 0;
    nj_s4 frames;
    nj_u4 mark;

    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image");
    }
    if (!nj_midp_image_size(vm, img, &iw, &ih) || iw <= 0 || ih <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "sprite image has no pixels");
    }
    if (fw < 1 || fh < 1 || iw % fw != 0 || ih % fh != 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bad sprite frame size");
    }
    frames = (nj_s4)((iw / fw) * (ih / fh));
    mark = nj_root_mark(vm);
    self = nj_root(vm, self);
    img = nj_root(vm, img);
    nj_field_write_ref(vm, self,
                       siemens_f(vm, NJ_C_SISPRITE, "sourceImage",
                                 "L" NJ_C_IMAGE ";", 0), img);
    sprite_set_int(vm, self, "numberFrames", frames);
    sprite_set_int(vm, self, "srcFrameWidth", fw);
    sprite_set_int(vm, self, "srcFrameHeight", fh);
    sprite_set_int(vm, self, "sequenceIndex", 0);
    layer_set_int(vm, self, "width", fw);
    layer_set_int(vm, self, "height", fh);
    layer_set_int(vm, self, "x", 0);
    layer_set_int(vm, self, "y", 0);
    layer_set_visible(vm, self, 1);
    nj_root_release(vm, mark);
    return NJ_OK;
}

NJFN(siemens_sprite_init1)
{
    nj_ref self = NJ_THIS(args), img = NJ_ARG_REF(args, 0);
    int w = 0, h = 0;

    if (!img || !nj_midp_image_size(vm, img, &w, &h)) {
        return nj_throw(vm, "java/lang/NullPointerException", "image");
    }
    return sprite_init(vm, self, img, w, h);
}

NJFN(siemens_sprite_init3)
{
    return sprite_init(vm, NJ_THIS(args), NJ_ARG_REF(args, 0),
                       NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2));
}

NJFN(siemens_sprite_set_frame)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 index = NJ_ARG_INT(args, 0);
    nj_s4 n = sprite_get_int(vm, self, "numberFrames");

    if (index < 0 || index >= n) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException",
                        "sprite frame");
    }
    sprite_set_int(vm, self, "sequenceIndex", index);
    return NJ_OK;
}

NJFN(siemens_sprite_get_frame)
{
    ret->i = sprite_get_int(vm, NJ_THIS(args), "sequenceIndex");
    return NJ_OK;
}

NJFN(siemens_sprite_frame_count)
{
    ret->i = sprite_get_int(vm, NJ_THIS(args), "numberFrames");
    return NJ_OK;
}

NJFN(siemens_sprite_seq_length)
{
    ret->i = sprite_get_int(vm, NJ_THIS(args), "numberFrames");
    return NJ_OK;
}

NJFN(siemens_sprite_next)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 n = sprite_get_int(vm, self, "numberFrames");
    nj_s4 i = sprite_get_int(vm, self, "sequenceIndex");

    if (n > 0) {
        sprite_set_int(vm, self, "sequenceIndex", (i + 1) % n);
    }
    return NJ_OK;
}

NJFN(siemens_sprite_prev)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 n = sprite_get_int(vm, self, "numberFrames");
    nj_s4 i = sprite_get_int(vm, self, "sequenceIndex");

    if (n > 0) {
        sprite_set_int(vm, self, "sequenceIndex", i == 0 ? n - 1 : i - 1);
    }
    return NJ_OK;
}

NJFN(siemens_sprite_paint)
{
    nj_ref self = NJ_THIS(args), g = NJ_ARG_REF(args, 0);
    nj_ref img = sprite_image(vm, self);
    nj_s4 fw = sprite_get_int(vm, self, "srcFrameWidth");
    nj_s4 fh = sprite_get_int(vm, self, "srcFrameHeight");
    nj_s4 n = sprite_get_int(vm, self, "numberFrames");
    nj_s4 idx = sprite_get_int(vm, self, "sequenceIndex");
    nj_s4 x = layer_get_int(vm, self, "x");
    nj_s4 y = layer_get_int(vm, self, "y");
    int iw = 0, ih = 0, cols = 1;
    nj_value a[9];
    nj_u4 mark;

    if (!g) {
        return nj_throw(vm, "java/lang/NullPointerException", "graphics");
    }
    if (!img || !nj_midp_image_size(vm, img, &iw, &ih)) {
        return NJ_OK;
    }
    if (fw < 1 || fh < 1) {
        return NJ_OK;
    }
    if (idx < 0 || idx >= n) {
        idx = 0;
    }
    cols = iw / fw;
    if (cols < 1) {
        cols = 1;
    }
    a[0].r = img;
    a[1].i = (idx % cols) * fw;
    a[2].i = (idx / cols) * fh;
    a[3].i = fw;
    a[4].i = fh;
    a[5].i = 0;                       /* TRANS_NONE */
    a[6].i = x;
    a[7].i = y;
    a[8].i = SIEMENS_ANCHOR_TOP_LEFT;

    mark = nj_root_mark(vm);
    img = nj_root(vm, img);
    g = nj_root(vm, g);
    (void)nj_call(vm, g, NJ_C_GRAPHICS, "drawRegion",
                  "(L" NJ_C_IMAGE ";IIIIIIII)V", a, NULL);
    nj_root_release(vm, mark);
    return nj_has_exception(vm) ? NJ_ERR : NJ_OK;
}

/* ------------------------------------------------------------------ */
/* ExtendedImage                                                       */
/* ------------------------------------------------------------------ */
static nj_ref ximg_image(nj_vm *vm, nj_ref self)
{
    nj_field *f = siemens_f(vm, NJ_C_SIXIMG, "image", "L" NJ_C_IMAGE ";", 0);
    return f ? nj_field_read_ref(vm, self, f) : NULL;
}

NJFN(siemens_ximg_init)
{
    nj_ref self = NJ_THIS(args), img = NJ_ARG_REF(args, 0);
    int w = 0, h = 0;
    nj_field *f;

    if (!img || !nj_midp_image_size(vm, img, &w, &h)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "ExtendedImage needs an image");
    }
    if (w % 8 != 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "ExtendedImage width is not divisible by 8");
    }
    f = siemens_f(vm, NJ_C_SIXIMG, "image", "L" NJ_C_IMAGE ";", 0);
    if (f) {
        nj_field_write_ref(vm, self, f, img);
    }
    return NJ_OK;
}

NJFN(siemens_ximg_get_image)
{
    ret->r = ximg_image(vm, NJ_THIS(args));
    return NJ_OK;
}

/* Fill the wrapped image: 0 is white, anything else black (reference). */
NJFN(siemens_ximg_clear)
{
    nj_ref self = NJ_THIS(args), img = ximg_image(vm, self);
    nj_u4 fill = NJ_ARG_INT(args, 0) == 0 ? 0xFFFFFFFFu : 0xFF000000u;
    nj_u4 *px;
    int w = 0, h = 0, n, k;

    if (!img || !nj_midp_image_size(vm, img, &w, &h)) {
        return NJ_OK;
    }
    px = nj_midp_image_pixels(vm, img);
    if (!px) {
        return NJ_OK;
    }
    n = w * h;
    for (k = 0; k < n; k++) {
        px[k] = fill;
    }
    return NJ_OK;
}

/* Draw the wrapped image at (x,y) into the current Canvas' backing buffer,
 * which the frame pump presents; fall back to the port screen surface. */
NJFN(siemens_ximg_blit)
{
    nj_ref self = NJ_THIS(args), img = ximg_image(vm, self);
    nj_ref cur, buf = NULL, g;
    nj_s4 x = NJ_ARG_INT(args, 0), y = NJ_ARG_INT(args, 1);
    nj_u4 mark;
    nj_value a[4];
    nj_field *f;

    if (!img) {
        return NJ_OK;
    }
    cur = nj_midp_current(vm);
    if (!cur) {
        return NJ_OK;
    }
    f = siemens_f(vm, NJ_C_CANVAS, "cBuffer", "L" NJ_C_IMAGE ";", 0);
    if (f) {
        buf = nj_field_read_ref(vm, cur, f);
    }
    if (!buf) {
        return NJ_OK;    /* no backing buffer to composite into */
    }
    g = nj_midp_image_graphics(vm, buf);
    if (!g) {
        nj_clear_exception(vm);
        return NJ_OK;
    }
    mark = nj_root_mark(vm);
    img = nj_root(vm, img);
    g = nj_root(vm, g);
    a[0].r = img;
    a[1].i = x;
    a[2].i = y;
    a[3].i = SIEMENS_ANCHOR_TOP_LEFT;
    (void)nj_call(vm, g, NJ_C_GRAPHICS, "drawImage", "(L" NJ_C_IMAGE ";III)V",
                  a, NULL);
    nj_root_release(vm, mark);
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
        return NJ_OK;
    }
    nj_midp_repaint(vm);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Light: the screen backlight cannot be controlled from here.          */
/* ------------------------------------------------------------------ */
NJFN(siemens_light_on)  { (void)args; (void)ret; return NJ_OK; }
NJFN(siemens_light_off) { (void)args; (void)ret; return NJ_OK; }

/* ------------------------------------------------------------------ */
/* Class and native tables                                             */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_field layer_fields[] = {
    { "x",      "I", 0 },
    { "y",      "I", 0 },
    { "width",  "I", 0 },
    { "height", "I", 0 },
    { "visible", "Z", 0 }
};

static const nj_builtin_method layer_methods[] = {
    M("<init>",      "(II)V", NJ_ACC_PUBLIC),
    M("setPosition", "(II)V", NJ_ACC_PUBLIC),
    M("move",        "(II)V", NJ_ACC_PUBLIC),
    M("getX",        "()I",   NJ_ACC_PUBLIC),
    M("getY",        "()I",   NJ_ACC_PUBLIC),
    M("getWidth",    "()I",   NJ_ACC_PUBLIC),
    M("getHeight",   "()I",   NJ_ACC_PUBLIC),
    M("setVisible",  "(Z)V",  NJ_ACC_PUBLIC),
    M("isVisible",   "()Z",   NJ_ACC_PUBLIC),
    M("setWidth",    "(I)V",  NJ_ACC_PUBLIC),
    M("setHeight",   "(I)V",  NJ_ACC_PUBLIC),
    M("paint",       "(L" NJ_C_GRAPHICS ";)V", NJ_ACC_PUBLIC)
};

static const nj_builtin_field lm_fields[] = {
    { "layers",     "L" NJ_C_VECTOR ";", 0 },
    { "viewX",      "I", 0 },
    { "viewY",      "I", 0 },
    { "viewWidth",  "I", 0 },
    { "viewHeight", "I", 0 }
};

static const nj_builtin_method lm_methods[] = {
    M("<init>",      "()V", NJ_ACC_PUBLIC),
    M("append",      "(L" NJ_C_SILAYER ";)V", NJ_ACC_PUBLIC),
    M("insert",      "(L" NJ_C_SILAYER ";I)V", NJ_ACC_PUBLIC),
    M("getLayerAt",  "(I)L" NJ_C_SILAYER ";", NJ_ACC_PUBLIC),
    M("getSize",     "()I", NJ_ACC_PUBLIC),
    M("remove",      "(L" NJ_C_SILAYER ";)V", NJ_ACC_PUBLIC),
    M("paint",       "(L" NJ_C_GRAPHICS ";II)V", NJ_ACC_PUBLIC),
    M("setViewWindow", "(IIII)V", NJ_ACC_PUBLIC)
};

static const nj_builtin_field sprite_fields[] = {
    { "sourceImage",   "L" NJ_C_IMAGE ";", 0 },
    { "numberFrames",  "I", 0 },
    { "srcFrameWidth",  "I", 0 },
    { "srcFrameHeight", "I", 0 },
    { "sequenceIndex", "I", 0 }
};

static const nj_builtin_method sprite_methods[] = {
    M("<init>",   "(L" NJ_C_IMAGE ";)V", NJ_ACC_PUBLIC),
    M("<init>",   "(L" NJ_C_IMAGE ";II)V", NJ_ACC_PUBLIC),
    M("setFrame", "(I)V", NJ_ACC_PUBLIC),
    M("getFrame", "()I",  NJ_ACC_PUBLIC),
    M("getRawFrameCount", "()I", NJ_ACC_PUBLIC),
    M("getFrameSequenceLength", "()I", NJ_ACC_PUBLIC),
    M("nextFrame", "()V", NJ_ACC_PUBLIC),
    M("prevFrame", "()V", NJ_ACC_PUBLIC),
    M("paint",     "(L" NJ_C_GRAPHICS ";)V", NJ_ACC_PUBLIC)
};

static const nj_builtin_field ximg_fields[] = {
    { "image", "L" NJ_C_IMAGE ";", 0 }
};

static const nj_builtin_method ximg_methods[] = {
    M("<init>",       "(L" NJ_C_IMAGE ";)V", NJ_ACC_PUBLIC),
    M("blitToScreen", "(II)V", NJ_ACC_PUBLIC),
    M("clear",        "(B)V",  NJ_ACC_PUBLIC),
    M("getImage",     "()L" NJ_C_IMAGE ";", NJ_ACC_PUBLIC)
};

static const nj_builtin_method light_methods[] = {
    M("setLightOn",  "()V", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("setLightOff", "()V", NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

const nj_builtin_class nj_lib_siemens_classes[] = {
    { NJ_C_SILAYER, "java/lang/Object", NJ_ACC_PUBLIC,
      layer_fields,
      (nj_u2)(sizeof(layer_fields) / sizeof(layer_fields[0])),
      layer_methods,
      (nj_u2)(sizeof(layer_methods) / sizeof(layer_methods[0])) },
    { NJ_C_SILMGR, "java/lang/Object", NJ_ACC_PUBLIC,
      lm_fields,
      (nj_u2)(sizeof(lm_fields) / sizeof(lm_fields[0])),
      lm_methods,
      (nj_u2)(sizeof(lm_methods) / sizeof(lm_methods[0])) },
    { NJ_C_SISPRITE, NJ_C_SILAYER, NJ_ACC_PUBLIC,
      sprite_fields,
      (nj_u2)(sizeof(sprite_fields) / sizeof(sprite_fields[0])),
      sprite_methods,
      (nj_u2)(sizeof(sprite_methods) / sizeof(sprite_methods[0])) },
    { NJ_C_SIXIMG, "java/lang/Object", NJ_ACC_PUBLIC,
      ximg_fields,
      (nj_u2)(sizeof(ximg_fields) / sizeof(ximg_fields[0])),
      ximg_methods,
      (nj_u2)(sizeof(ximg_methods) / sizeof(ximg_methods[0])) },
    { NJ_C_SILIGHT, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u,
      light_methods,
      (nj_u2)(sizeof(light_methods) / sizeof(light_methods[0])) }
};
const int nj_lib_siemens_class_count =
    (int)(sizeof(nj_lib_siemens_classes) / sizeof(nj_lib_siemens_classes[0]));

const nj_native_entry nj_lib_siemens_table[] = {
    { NJ_C_SILAYER, "<init>", "(II)V", siemens_layer_init },
    { NJ_C_SILAYER, "setPosition", "(II)V", siemens_layer_set_position },
    { NJ_C_SILAYER, "move", "(II)V", siemens_layer_move },
    { NJ_C_SILAYER, "getX", "()I", siemens_layer_get_x },
    { NJ_C_SILAYER, "getY", "()I", siemens_layer_get_y },
    { NJ_C_SILAYER, "getWidth", "()I", siemens_layer_get_width },
    { NJ_C_SILAYER, "getHeight", "()I", siemens_layer_get_height },
    { NJ_C_SILAYER, "setVisible", "(Z)V", siemens_layer_set_visible },
    { NJ_C_SILAYER, "isVisible", "()Z", siemens_layer_is_visible },
    { NJ_C_SILAYER, "setWidth", "(I)V", siemens_layer_set_width },
    { NJ_C_SILAYER, "setHeight", "(I)V", siemens_layer_set_height },
    { NJ_C_SILAYER, "paint", "(L" NJ_C_GRAPHICS ";)V", siemens_layer_paint },
    { NJ_C_SILMGR, "<init>", "()V", siemens_lm_init },
    { NJ_C_SILMGR, "append", "(L" NJ_C_SILAYER ";)V", siemens_lm_append },
    { NJ_C_SILMGR, "insert", "(L" NJ_C_SILAYER ";I)V", siemens_lm_insert },
    { NJ_C_SILMGR, "getLayerAt", "(I)L" NJ_C_SILAYER ";", siemens_lm_get_layer },
    { NJ_C_SILMGR, "getSize", "()I", siemens_lm_get_size },
    { NJ_C_SILMGR, "remove", "(L" NJ_C_SILAYER ";)V", siemens_lm_remove },
    { NJ_C_SILMGR, "paint", "(L" NJ_C_GRAPHICS ";II)V", siemens_lm_paint },
    { NJ_C_SILMGR, "setViewWindow", "(IIII)V", siemens_lm_set_view },
    { NJ_C_SISPRITE, "<init>", "(L" NJ_C_IMAGE ";)V", siemens_sprite_init1 },
    { NJ_C_SISPRITE, "<init>", "(L" NJ_C_IMAGE ";II)V", siemens_sprite_init3 },
    { NJ_C_SISPRITE, "setFrame", "(I)V", siemens_sprite_set_frame },
    { NJ_C_SISPRITE, "getFrame", "()I", siemens_sprite_get_frame },
    { NJ_C_SISPRITE, "getRawFrameCount", "()I", siemens_sprite_frame_count },
    { NJ_C_SISPRITE, "getFrameSequenceLength", "()I", siemens_sprite_seq_length },
    { NJ_C_SISPRITE, "nextFrame", "()V", siemens_sprite_next },
    { NJ_C_SISPRITE, "prevFrame", "()V", siemens_sprite_prev },
    { NJ_C_SISPRITE, "paint", "(L" NJ_C_GRAPHICS ";)V", siemens_sprite_paint },
    { NJ_C_SIXIMG, "<init>", "(L" NJ_C_IMAGE ";)V", siemens_ximg_init },
    { NJ_C_SIXIMG, "blitToScreen", "(II)V", siemens_ximg_blit },
    { NJ_C_SIXIMG, "clear", "(B)V", siemens_ximg_clear },
    { NJ_C_SIXIMG, "getImage", "()L" NJ_C_IMAGE ";", siemens_ximg_get_image },
    { NJ_C_SILIGHT, "setLightOn", "()V", siemens_light_on },
    { NJ_C_SILIGHT, "setLightOff", "()V", siemens_light_off }
};
const int nj_lib_siemens_count =
    (int)(sizeof(nj_lib_siemens_table) / sizeof(nj_lib_siemens_table[0]));
