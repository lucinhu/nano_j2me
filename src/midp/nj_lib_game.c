/*
 * nano_j2me - nj_lib_game.c
 *
 * javax.microedition.lcdui.game - the MIDP 2.0 game API.
 *
 * ============================================================================
 *  WHAT THIS FILE OWNS
 * ============================================================================
 * Five builtin classes, declared exactly once, here:
 *
 *   javax/microedition/lcdui/game/GameCanvas    (extends Canvas)
 *   javax/microedition/lcdui/game/Layer         (abstract)
 *   javax/microedition/lcdui/game/Sprite        (extends Layer)
 *   javax/microedition/lcdui/game/TiledLayer    (extends Layer)
 *   javax/microedition/lcdui/game/LayerManager
 *
 * Nothing in javax/microedition/lcdui/ is declared here: Image, Graphics and
 * Canvas belong to nj_lib_lcdui.c.  This module only *uses* them, through the
 * frozen field names of src/midp/nj_midp.h.
 *
 * ============================================================================
 *  STATE PLACEMENT
 * ============================================================================
 * Every byte of mutable state lives in a Java field of one of the classes
 * above; there is not a single file-scope variable in this file (enforced by
 * tools/check_no_statics.sh).  A MIDlet can therefore be started, torn down and
 * restarted without residue, and two VMs can coexist.
 *
 * The fields of nj_midp.h are used verbatim.  A few ADDITIVE fields are
 * declared where the frozen set has no home for MIDP state; they carry the
 * same prefix and are never read by another module:
 *
 *   Sprite       spRefDX/spRefDY  the reference pixel as *defined*, i.e.
 *                                 relative to the untransformed frame
 *                spSequence/spSeqIndex  custom frame sequence + its index
 *                spColX/Y/W/H     collision rectangle (frame space)
 *   TiledLayer   tlAnimCap        capacity of the animated-tile table
 *   LayerManager lmCount          number of live entries in lmLayers
 *                lmViewW/lmViewH   the view window's size
 *   GameCanvas   -                (gcSuppress/gcFullScreen are enough)
 *
 * ============================================================================
 *  LAYERMANAGER STORAGE: Object[] + count, not java/util/Vector
 * ============================================================================
 * NJ_F_LM_LAYERS ("lmLayers") holds the layers, but as a
 * "[Ljava/lang/Object;" plus an int count (lmCount) rather than a
 * java/util/Vector.  Reasons:
 *
 *   - the game package is then linkable and testable on its own.  Vector is a
 *     separate module that was still a placeholder while this one was written,
 *     and a builtin field whose type cannot be resolved makes linking fail,
 *     which would have taken all five game classes down with it;
 *   - paint() walks the list once per layer per frame: an array read is a load,
 *     a Vector.get() is a full Java call through nj_call;
 *   - the list is private to LayerManager - no Java code can observe it - so
 *     the change is invisible to MIDlets.
 * The array grows by doubling; unused slots stay NULL.
 *
 * ============================================================================
 *  THE TRANSFORM / REFERENCE PIXEL ARITHMETIC  (the subtle part)
 * ============================================================================
 * nj_gfx_draw_region(g, src, sx,sy,sw,sh, transform, ...) draws the source
 * rectangle TRANSFORMED, and places the transformed rectangle's bounding box
 * with its upper-left corner at (dx,dy) in the Graphics' user space.  Writing T
 * for the map from an untransformed frame pixel (ux,uy) to its place in the
 * transformed frame (vx,vy), the eight transforms are:
 *
 *   TRANS_NONE          vx = ux            vy = uy
 *   TRANS_ROT90         vx = fh-1-uy       vy = ux
 *   TRANS_ROT180        vx = fw-1-ux       vy = fh-1-uy
 *   TRANS_ROT270        vx = uy            vy = fw-1-ux
 *   TRANS_MIRROR        vx = fw-1-ux       vy = uy
 *   TRANS_MIRROR_ROT90  vx = fh-1-uy       vy = fw-1-ux
 *   TRANS_MIRROR_ROT180 vx = ux            vy = fh-1-uy
 *   TRANS_MIRROR_ROT270 vx = uy            vy = ux
 *
 * (fw,fh = frame size; the transformed frame is (fh,fw) for the 90/270
 * rotations).  T is exactly the inverse of the un-transform the rasterizer
 * performs pixel by pixel, which is what makes drawing and collision agree:
 * collision never re-implements a different notion of "where the sprite is".
 *
 * The Sprite stores:
 *   lX,lY            the position of the sprite's VISUAL BOUNDS, i.e. of the
 *                    transformed frame's bounding box, in the painter's
 *                    coordinate system.  paint() passes (lX,lY) straight to
 *                    nj_gfx_draw_region as its (dx,dy).
 *   spRefDX,spRefDY  the reference pixel as defineReferencePixel() defines it:
 *                    relative to the upper-left corner of the UNTRANSFORMED
 *                    frame (it may lie outside the frame).
 *   spRefX,spRefY    the same pixel expressed in the transformed frame, i.e.
 *                    T(spRefDX,spRefDY) - "rotated space", as nj_midp.h calls
 *                    it.  Then, and only then,
 *                        getRefPixelX() = getX() + spRefX
 *                        getRefPixelY() = getY() + spRefY
 *                    holds for every transform, and
 *                        setRefPixelPosition(x,y) => lX = x - spRefX
 *                                                    lY = y - spRefY.
 * setTransform() recomputes spRefX/spRefY for the new transform and adds the
 * difference to lX/lY, so the reference pixel does not move on screen ("the
 * reference pixel effectively becomes the center of the transform"):
 *
 *   lX += oldRefX - newRefX;   lY += oldRefY - newRefY;
 *
 * ============================================================================
 *  DEVIATIONS FROM THE TASK TEXT, AND WHY  (all deliberate)
 * ============================================================================
 *  1. getKeyStates() translates the sticky bits.  A MIDlet's javac inlined its
 *     constants, so the returned value MUST use the documented MIDP bit values
 *     (UP 0x0002, LEFT 0x0004, RIGHT 0x0020, DOWN 0x0040, FIRE 0x0100,
 *     GAME_A 0x0200 ... GAME_D 0x1000).  NJ_F_C_KEYS ("cKeys") carries the
 *     project's own NJ_GAME_* bits from nj_midp.h, which differ for everything
 *     except UP and LEFT, so this method maps them across.  Without the
 *     mapping RIGHT/DOWN/FIRE would never reach a real game.
 *  2. TiledLayer's animated tile indices are NEGATIVE (-1, -2, ...), as MIDP
 *     2.0 requires, not "> nStatic".  For tolerance the positive alias
 *     (nStatic+1, nStatic+2, ...) is accepted wherever an animated tile index
 *     is expected; createAnimatedTile() returns the spec-conformant negative.
 *  3. paint(Graphics) on GameCanvas renders the off-screen buffer at (0,0),
 *     which is what MIDP documents ("By default, this method renders the
 *     off-screen buffer at (0,0)").  It is final and cannot be overridden, so
 *     an empty implementation would leave the display blank on every
 *     system-initiated repaint, and flushGraphics() alone would not help.
 *  4. nextFrame()/prevFrame() return void (MIDP's signature).  getFrame()
 *     returns the FRAME SEQUENCE index; getFrameSequenceIndex() is the
 *     additive alias.  The displayed raw frame lives in a separate additive
 *     field kept in sync, so paint() never re-resolves the sequence.
 *  5. Additive extensions, none of which conflict with a MIDP signature:
 *     Sprite.getImage(), Sprite.setImage(Image), Sprite.getTransform(),
 *     Sprite.getFrameSequenceIndex(), Sprite.collidesWith(Layer,boolean),
 *     Sprite.collidesWith(Sprite,boolean,boolean) (third argument: line the two
 *     reference pixels up before testing), TiledLayer.fillCells(int,int,int,int)
 *     (clear a region), LayerManager.getViewX()/getViewY()/getViewW()/getViewH().
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

/* ------------------------------------------------------------------ */
/* Descriptors                                                         */
/* ------------------------------------------------------------------ */
#define D_INT     "I"
#define D_BOOL    "Z"
#define D_IMAGE   "Ljavax/microedition/lcdui/Image;"
#define D_GFX     "Ljavax/microedition/lcdui/Graphics;"
#define D_LAYER   "Ljavax/microedition/lcdui/game/Layer;"
#define D_SPRITE  "Ljavax/microedition/lcdui/game/Sprite;"
#define D_TILED   "Ljavax/microedition/lcdui/game/TiledLayer;"
#define D_INTARR  "[I"
#define D_OBJARR  "[Ljava/lang/Object;"

/* Additive field names (see the header comment). */
#define SP_REFDX   "spRefDX"
#define SP_REFDY   "spRefDY"
#define SP_SEQ     "spSequence"
#define SP_SEQIDX  "spSeqIndex"
#define SP_COLX    "spColX"
#define SP_COLY    "spColY"
#define SP_COLW    "spColW"
#define SP_COLH    "spColH"
#define TL_ANIMCAP "tlAnimCap"
#define LM_COUNT   "lmCount"
#define LM_VIEWW   "lmViewW"
#define LM_VIEWH   "lmViewH"

/* ------------------------------------------------------------------ */
/* Field helpers                                                       */
/* ------------------------------------------------------------------ */
/*
 * Fields are resolved by name on every access.  nj_field_lookup is hashed and
 * the class library has nowhere to cache an nj_field* without introducing
 * mutable global state, which this project forbids; a few hash probes are cheap
 * next to a blit.
 */
static nj_s4 fgi(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                 const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void fsi(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                const char *desc, nj_s4 v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static nj_ref fgr(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                  const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void fsr(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                const char *desc, nj_ref v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

/* Layer state: declared in Layer, inherited by Sprite and TiledLayer. */
static nj_s4 lay_x(nj_vm *vm, nj_ref o) { return fgi(vm, o, NJ_C_LAYER, NJ_F_L_X, D_INT); }
static nj_s4 lay_y(nj_vm *vm, nj_ref o) { return fgi(vm, o, NJ_C_LAYER, NJ_F_L_Y, D_INT); }
static nj_s4 lay_w(nj_vm *vm, nj_ref o) { return fgi(vm, o, NJ_C_LAYER, NJ_F_L_W, D_INT); }
static nj_s4 lay_h(nj_vm *vm, nj_ref o) { return fgi(vm, o, NJ_C_LAYER, NJ_F_L_H, D_INT); }

static void lay_pos(nj_vm *vm, nj_ref o, int x, int y)
{
    fsi(vm, o, NJ_C_LAYER, NJ_F_L_X, D_INT, x);
    fsi(vm, o, NJ_C_LAYER, NJ_F_L_Y, D_INT, y);
}

static void lay_size(nj_vm *vm, nj_ref o, int w, int h)
{
    fsi(vm, o, NJ_C_LAYER, NJ_F_L_W, D_INT, w);
    fsi(vm, o, NJ_C_LAYER, NJ_F_L_H, D_INT, h);
}

static int lay_visible(nj_vm *vm, nj_ref o)
{
    /* Absent field => visible, which is the documented default anyway. */
    nj_field *f = nj_field_lookup(vm, NJ_C_LAYER, NJ_F_L_VISIBLE, D_BOOL, 0);
    return f ? (nj_field_read_int(vm, o, f) != 0) : 1;
}

/* Sprite state. */
#define SPG(vm, o, n, d)    fgi((vm), (o), NJ_C_SPRITE, (n), (d))
#define SPS(vm, o, n, d, v) fsi((vm), (o), NJ_C_SPRITE, (n), (d), (v))

/* ------------------------------------------------------------------ */
/* Small integer helpers                                               */
/* ------------------------------------------------------------------ */
static int imin(int a, int b) { return (a < b) ? a : b; }
static int imax(int a, int b) { return (a > b) ? a : b; }
static int iabs(int a) { return (a < 0) ? -a : a; }

/* nj_s8 inside: layer bounds and the default view window can be huge. */
static int rects_overlap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh)
{
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) {
        return 0;
    }
    return !((nj_s8)ax + aw <= bx || (nj_s8)bx + bw <= ax ||
             (nj_s8)ay + ah <= by || (nj_s8)by + bh <= ay);
}

/* ------------------------------------------------------------------ */
/* Transform geometry                                                  */
/* ------------------------------------------------------------------ */
static int trans_ok(int t)
{
    return t == NJ_TRANS_NONE || t == NJ_TRANS_ROT90 ||
           t == NJ_TRANS_ROT180 || t == NJ_TRANS_ROT270 ||
           t == NJ_TRANS_MIRROR || t == NJ_TRANS_MIRROR_ROT90 ||
           t == NJ_TRANS_MIRROR_ROT180 || t == NJ_TRANS_MIRROR_ROT270;
}

/* Bounding box of a (fw x fh) frame once `t` has been applied. */
static void trans_size(int t, int fw, int fh, int *dw, int *dh)
{
    if (t == NJ_TRANS_ROT90 || t == NJ_TRANS_ROT270 ||
        t == NJ_TRANS_MIRROR_ROT90 || t == NJ_TRANS_MIRROR_ROT270) {
        *dw = fh;
        *dh = fw;
    } else {
        *dw = fw;
        *dh = fh;
    }
}

/* Untransformed frame pixel -> transformed frame pixel. */
static void trans_fwd(int t, int fw, int fh, int ux, int uy, int *vx, int *vy)
{
    switch (t) {
    case NJ_TRANS_ROT90:         *vx = fh - 1 - uy; *vy = ux;          break;
    case NJ_TRANS_ROT180:        *vx = fw - 1 - ux; *vy = fh - 1 - uy; break;
    case NJ_TRANS_ROT270:        *vx = uy;          *vy = fw - 1 - ux; break;
    case NJ_TRANS_MIRROR:        *vx = fw - 1 - ux; *vy = uy;          break;
    case NJ_TRANS_MIRROR_ROT90:  *vx = fh - 1 - uy; *vy = fw - 1 - ux; break;
    case NJ_TRANS_MIRROR_ROT180: *vx = ux;          *vy = fh - 1 - uy; break;
    case NJ_TRANS_MIRROR_ROT270: *vx = uy;          *vy = ux;          break;
    default:                     *vx = ux;          *vy = uy;          break;
    }
}

/* Transformed frame pixel -> untransformed frame pixel (T's inverse). */
static void trans_inv(int t, int fw, int fh, int vx, int vy, int *ux, int *uy)
{
    switch (t) {
    case NJ_TRANS_ROT90:         *ux = vy;          *uy = fh - 1 - vx; break;
    case NJ_TRANS_ROT180:        *ux = fw - 1 - vx; *uy = fh - 1 - vy; break;
    case NJ_TRANS_ROT270:        *ux = fw - 1 - vy; *uy = vx;          break;
    case NJ_TRANS_MIRROR:        *ux = fw - 1 - vx; *uy = vy;          break;
    case NJ_TRANS_MIRROR_ROT90:  *ux = fw - 1 - vy; *uy = fh - 1 - vx; break;
    case NJ_TRANS_MIRROR_ROT180: *ux = vx;          *uy = fh - 1 - vy; break;
    case NJ_TRANS_MIRROR_ROT270: *ux = vy;          *uy = vx;          break;
    default:                     *ux = vx;          *uy = vy;          break;
    }
}

/*
 * Transform an axis-aligned rectangle of the untransformed frame into the
 * rectangle it occupies in the transformed frame.  Every accepted transform
 * maps a rectangle to a rectangle, so the two opposite corners suffice.
 */
static void trans_rect(int t, int fw, int fh, int x, int y, int w, int h,
                       int *ox, int *oy, int *ow, int *oh)
{
    int ax, ay, bx, by;

    if (w <= 0 || h <= 0) {
        *ox = 0; *oy = 0; *ow = 0; *oh = 0;
        return;
    }
    trans_fwd(t, fw, fh, x, y, &ax, &ay);
    trans_fwd(t, fw, fh, x + w - 1, y + h - 1, &bx, &by);
    *ox = imin(ax, bx);
    *oy = imin(ay, by);
    *ow = iabs(ax - bx) + 1;
    *oh = iabs(ay - by) + 1;
}

/* ------------------------------------------------------------------ */
/* Binding an nj_gfx to a Java Graphics                                */
/* ------------------------------------------------------------------ */
/*
 * A Java Graphics is a serialised nj_gfx (nj_midp.h fixes the field names).
 * Drawing through it means: view the target surface, copy the transform state
 * across, run one nj_gfx call.  The LCDUI module does the same for its own
 * primitives, so a Sprite drawn from here and a line drawn from Java land in
 * the same place.
 *
 * The clip is kept (by the LCDUI module) in surface coordinates, which is what
 * nj_gfx_set_clip wants: MIDP's setClip() is not affected by translate().  A
 * clip of exactly 0,0,0,0 means "never initialised" - that would silently draw
 * nothing - so it is read as "the whole surface" instead.
 */
typedef struct njg_bind {
    nj_surface  scratch;   /* storage when the target is an Image */
    nj_surface *surf;      /* the surface nj_gfx draws into       */
} njg_gfx_bind;

static int njg_bind(nj_vm *vm, nj_ref gobj, njg_gfx_bind *b, nj_gfx *g)
{
    nj_ref target;
    nj_s4 tx, ty, cx, cy, cw, ch;

    b->surf = NULL;
    if (!gobj) {
        return 0;
    }
    target = fgr(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TARGET, D_IMAGE);
    b->surf = nj_midp_surface(vm, target, &b->scratch);
    if (!b->surf) {
        return 0;
    }
    nj_gfx_init(g, b->surf);

    tx = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT);
    ty = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT);
    g->translate_x = (int)tx;
    g->translate_y = (int)ty;

    cx = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT);
    cy = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT);
    cw = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT);
    ch = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT);
    if (!(cx == 0 && cy == 0 && cw == 0 && ch == 0)) {
        nj_gfx_set_clip(g, (int)cx, (int)cy, (int)cw, (int)ch);
    }

    g->color = (nj_u4)fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_COLOR, D_INT);
    {
        nj_s4 blend = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_BLEND, D_INT);
        g->blend = (blend == NJ_BLEND_NONE) ? NJ_BLEND_NONE : NJ_BLEND_SRC_OVER;
    }
    {
        nj_s4 stroke = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_STROKE, D_INT);
        g->stroke = (stroke == NJ_STROKE_DOTTED) ? NJ_STROKE_DOTTED
                                                 : NJ_STROKE_SOLID;
    }
    if (fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_XOR, D_INT) != 0) {
        nj_gfx_set_xor(g, (nj_u4)fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_XORCOLOR,
                                     D_INT));
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Image views                                                         */
/* ------------------------------------------------------------------ */
/*
 * The blitters want an nj_image; the game package has an Image whose pixels are
 * a Java int[].  nj_midp_image_pixels() gives the store; this wrapper also
 * decides `has_alpha`, which the rasterizer uses to pick its fast path.  The
 * flag is measured over exactly the region about to be drawn, so per-pixel
 * alpha still decides every pixel and no stale per-image cache is needed.
 */
static int region_has_alpha(const nj_u4 *px, int stride, int sx, int sy,
                            int sw, int sh)
{
    int x, y;

    for (y = 0; y < sh; y++) {
        const nj_u4 *row = px + (nj_size)(sy + y) * (nj_size)stride +
                           (nj_size)sx;
        for (x = 0; x < sw; x++) {
            if (((row[x] >> 24) & 0xFFu) != 0xFFu) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * View `imgref`, with has_alpha measured over the region (ax,ay,aw,ah) (the
 * region that is about to be drawn).  width/height describe the whole image,
 * because the source coordinates of a blit are relative to the image.
 */
static int image_view(nj_vm *vm, nj_ref imgref, int ax, int ay, int aw, int ah,
                      nj_image *out)
{
    int w = 0, h = 0;
    nj_u4 *px;

    if (!imgref) {
        return 0;
    }
    if (!nj_midp_image_size(vm, imgref, &w, &h) || w <= 0 || h <= 0) {
        return 0;
    }
    px = nj_midp_image_pixels(vm, imgref);
    if (!px) {
        return 0;
    }
    out->width = w;
    out->height = h;
    out->format = NJ_PIXFMT_ARGB8888;
    out->pixels = px;
    out->has_alpha = region_has_alpha(px, w, ax, ay, aw, ah);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Layer                                                               */
/* ------------------------------------------------------------------ */
NJFN(layer_init)
{
    lay_pos(vm, NJ_THIS(args), 0, 0);
    lay_size(vm, NJ_THIS(args), 0, 0);
    fsi(vm, NJ_THIS(args), NJ_C_LAYER, NJ_F_L_VISIBLE, D_BOOL, 1);
    return NJ_OK;
}

NJFN(layer_getX)      { ret->i = lay_x(vm, NJ_THIS(args)); return NJ_OK; }
NJFN(layer_getY)      { ret->i = lay_y(vm, NJ_THIS(args)); return NJ_OK; }
NJFN(layer_getWidth)  { ret->i = lay_w(vm, NJ_THIS(args)); return NJ_OK; }
NJFN(layer_getHeight) { ret->i = lay_h(vm, NJ_THIS(args)); return NJ_OK; }

NJFN(layer_setPosition)
{
    lay_pos(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(layer_move)
{
    nj_ref self = NJ_THIS(args);
    lay_pos(vm, self,
            (int)lay_x(vm, self) + NJ_ARG_INT(args, 0),
            (int)lay_y(vm, self) + NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(layer_isVisible)
{
    ret->i = lay_visible(vm, NJ_THIS(args)) ? 1 : 0;
    return NJ_OK;
}

NJFN(layer_setVisible)
{
    fsi(vm, NJ_THIS(args), NJ_C_LAYER, NJ_F_L_VISIBLE, D_BOOL,
        NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Sprite                                                              */
/* ------------------------------------------------------------------ */
/*
 * Recompute the transformed-space reference pixel from the defined one, and
 * (optionally) move the sprite so that the reference pixel keeps its place in
 * the painter's coordinate system.  This is the ONE place that knows the
 * arithmetic; everything else calls it.
 */
static void sprite_sync_ref(nj_vm *vm, nj_ref s, int fw, int fh, int t,
                            int reposition)
{
    int oldvx = (int)SPG(vm, s, NJ_F_SP_REFX, D_INT);
    int oldvy = (int)SPG(vm, s, NJ_F_SP_REFY, D_INT);
    int dx = (int)SPG(vm, s, SP_REFDX, D_INT);
    int dy = (int)SPG(vm, s, SP_REFDY, D_INT);
    int nvx, nvy;

    trans_fwd(t, fw, fh, dx, dy, &nvx, &nvy);
    SPS(vm, s, NJ_F_SP_REFX, D_INT, nvx);
    SPS(vm, s, NJ_F_SP_REFY, D_INT, nvy);
    if (reposition) {
        lay_pos(vm, s, (int)lay_x(vm, s) + oldvx - nvx,
                       (int)lay_y(vm, s) + oldvy - nvy);
    }
}

/* Update the visual bounds: getWidth/getHeight reflect the transform. */
static void sprite_sync_size(nj_vm *vm, nj_ref s, int fw, int fh, int t)
{
    int dw, dh;

    trans_size(t, fw, fh, &dw, &dh);
    lay_size(vm, s, dw, dh);
}

/*
 * Install a frame grid.
 *
 * `keep` selects MIDP's "the new frame set has as many or more frames"
 * behaviour of setImage(): the current sequence index (and any custom sequence)
 * survives, otherwise the sprite reverts to the default sequence and frame 0.
 *
 * Order matters: the old reference offset is captured before the frame size
 * changes, because the offset depends on the frame size.
 */
static nj_status sprite_set_frames(nj_vm *vm, nj_ref s, nj_ref img, int fw,
                                   int fh, int keep)
{
    int iw = 0, ih = 0;
    int old_fw, old_fh, cols, frames;
    nj_ref seq;

    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image is null");
    }
    if (fw < 1 || fh < 1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "frame size must be positive");
    }
    if (!nj_midp_image_size(vm, img, &iw, &ih) || iw <= 0 || ih <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "not an image");
    }
    if ((iw % fw) != 0 || (ih % fh) != 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "image size is not a multiple of the frame size");
    }

    old_fw = (int)SPG(vm, s, NJ_F_SP_FRAMEW, D_INT);
    old_fh = (int)SPG(vm, s, NJ_F_SP_FRAMEH, D_INT);
    seq = fgr(vm, s, NJ_C_SPRITE, SP_SEQ, D_INTARR);
    cols = iw / fw;
    frames = cols * (ih / fh);

    fsr(vm, s, NJ_C_SPRITE, NJ_F_SP_IMAGE, D_IMAGE, img);
    SPS(vm, s, NJ_F_SP_FRAMEW, D_INT, fw);
    SPS(vm, s, NJ_F_SP_FRAMEH, D_INT, fh);
    SPS(vm, s, NJ_F_SP_COLS, D_INT, cols);
    SPS(vm, s, NJ_F_SP_FRAMES, D_INT, frames);

    if (!keep) {
        fsr(vm, s, NJ_C_SPRITE, SP_SEQ, D_INTARR, NULL);
        SPS(vm, s, SP_SEQIDX, D_INT, 0);
        SPS(vm, s, NJ_F_SP_FRAME, D_INT, 0);
    } else {
        int idx = (int)SPG(vm, s, SP_SEQIDX, D_INT);
        int raw;
        if (idx < 0 || idx >= frames) {
            idx = 0;
        }
        if (seq && (nj_u4)idx < nj_array_length(seq)) {
            raw = nj_array_get_int(seq, (nj_u4)idx);
        } else {
            raw = idx;
        }
        SPS(vm, s, SP_SEQIDX, D_INT, idx);
        SPS(vm, s, NJ_F_SP_FRAME, D_INT, raw);
    }

    /* A frame-size change resets the collision rectangle to the new bounds. */
    if (fw != old_fw || fh != old_fh) {
        SPS(vm, s, SP_COLX, D_INT, 0);
        SPS(vm, s, SP_COLY, D_INT, 0);
        SPS(vm, s, SP_COLW, D_INT, fw);
        SPS(vm, s, SP_COLH, D_INT, fh);
    }

    sprite_sync_ref(vm, s, fw, fh, (int)SPG(vm, s, NJ_F_SP_TRANS, D_INT), 1);
    sprite_sync_size(vm, s, fw, fh, (int)SPG(vm, s, NJ_F_SP_TRANS, D_INT));
    return NJ_OK;
}

static nj_status sprite_construct(nj_vm *vm, nj_ref s, nj_ref img, int fw,
                                  int fh)
{
    if (nj_call_special(vm, s, NJ_C_LAYER, "<init>", "()V", NULL, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    SPS(vm, s, NJ_F_SP_TRANS, D_INT, NJ_TRANS_NONE);
    SPS(vm, s, SP_REFDX, D_INT, 0);
    SPS(vm, s, SP_REFDY, D_INT, 0);
    SPS(vm, s, NJ_F_SP_COLLIDE, D_BOOL, 1);
    return sprite_set_frames(vm, s, img, fw, fh, 0);
}

NJFN(sprite_init_image)
{
    nj_ref img = NJ_ARG_REF(args, 0);
    int w = 0, h = 0;

    if (!img) {
        NJ_THROW(vm, "java/lang/NullPointerException", "image is null");
    }
    if (!nj_midp_image_size(vm, img, &w, &h)) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException", "not an image");
    }
    return sprite_construct(vm, NJ_THIS(args), img, w, h);
}

NJFN(sprite_init_frames)
{
    return sprite_construct(vm, NJ_THIS(args), NJ_ARG_REF(args, 0),
                            NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2));
}

NJFN(sprite_init_copy)
{
    nj_ref self = NJ_THIS(args);
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_ref seq;
    nj_u4 mark;
    int i, n;

    if (!src) {
        NJ_THROW(vm, "java/lang/NullPointerException", "sprite is null");
    }
    if (nj_call_special(vm, self, NJ_C_LAYER, "<init>", "()V", NULL, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    /* MIDP: every instance attribute is duplicated in the new Sprite. */
    fsr(vm, self, NJ_C_SPRITE, NJ_F_SP_IMAGE, D_IMAGE,
        fgr(vm, src, NJ_C_SPRITE, NJ_F_SP_IMAGE, D_IMAGE));
    SPS(vm, self, NJ_F_SP_FRAMEW, D_INT, SPG(vm, src, NJ_F_SP_FRAMEW, D_INT));
    SPS(vm, self, NJ_F_SP_FRAMEH, D_INT, SPG(vm, src, NJ_F_SP_FRAMEH, D_INT));
    SPS(vm, self, NJ_F_SP_FRAMES, D_INT, SPG(vm, src, NJ_F_SP_FRAMES, D_INT));
    SPS(vm, self, NJ_F_SP_COLS, D_INT, SPG(vm, src, NJ_F_SP_COLS, D_INT));
    SPS(vm, self, NJ_F_SP_FRAME, D_INT, SPG(vm, src, NJ_F_SP_FRAME, D_INT));
    SPS(vm, self, NJ_F_SP_TRANS, D_INT, SPG(vm, src, NJ_F_SP_TRANS, D_INT));
    SPS(vm, self, SP_REFDX, D_INT, SPG(vm, src, SP_REFDX, D_INT));
    SPS(vm, self, SP_REFDY, D_INT, SPG(vm, src, SP_REFDY, D_INT));
    SPS(vm, self, NJ_F_SP_REFX, D_INT, SPG(vm, src, NJ_F_SP_REFX, D_INT));
    SPS(vm, self, NJ_F_SP_REFY, D_INT, SPG(vm, src, NJ_F_SP_REFY, D_INT));
    SPS(vm, self, SP_SEQIDX, D_INT, SPG(vm, src, SP_SEQIDX, D_INT));
    SPS(vm, self, SP_COLX, D_INT, SPG(vm, src, SP_COLX, D_INT));
    SPS(vm, self, SP_COLY, D_INT, SPG(vm, src, SP_COLY, D_INT));
    SPS(vm, self, SP_COLW, D_INT, SPG(vm, src, SP_COLW, D_INT));
    SPS(vm, self, SP_COLH, D_INT, SPG(vm, src, SP_COLH, D_INT));
    SPS(vm, self, NJ_F_SP_COLLIDE, D_BOOL,
        SPG(vm, src, NJ_F_SP_COLLIDE, D_BOOL));
    lay_pos(vm, self, lay_x(vm, src), lay_y(vm, src));
    lay_size(vm, self, lay_w(vm, src), lay_h(vm, src));
    fsi(vm, self, NJ_C_LAYER, NJ_F_L_VISIBLE, D_BOOL,
        lay_visible(vm, src) ? 1 : 0);

    /* The sequence is copied, not shared: MIDP copies the array in. */
    seq = fgr(vm, src, NJ_C_SPRITE, SP_SEQ, D_INTARR);
    if (seq) {
        nj_ref copy;
        mark = nj_root_mark(vm);
        n = (int)nj_array_length(seq);
        copy = nj_root(vm, nj_int_array_new(vm, (nj_u4)n));
        if (!copy) {
            nj_root_release(vm, mark);
            return NJ_ERR;
        }
        for (i = 0; i < n; i++) {
            nj_array_set_int(copy, (nj_u4)i, nj_array_get_int(seq, (nj_u4)i));
        }
        fsr(vm, self, NJ_C_SPRITE, SP_SEQ, D_INTARR, copy);
        nj_root_release(vm, mark);
    }
    return NJ_OK;
}

/* ---- frames and sequences ---- */
NJFN(sprite_setImage)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    int fw = NJ_ARG_INT(args, 1);
    int fh = NJ_ARG_INT(args, 2);
    int old_frames = (int)SPG(vm, self, NJ_F_SP_FRAMES, D_INT);
    int iw = 0, ih = 0, frames;

    if (!img) {
        NJ_THROW(vm, "java/lang/NullPointerException", "image is null");
    }
    if (fw < 1 || fh < 1) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "frame size must be positive");
    }
    if (!nj_midp_image_size(vm, img, &iw, &ih)) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException", "not an image");
    }
    if ((iw % fw) != 0 || (ih % fh) != 0) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "image size is not a multiple of the frame size");
    }
    frames = (iw / fw) * (ih / fh);
    return sprite_set_frames(vm, self, img, fw, fh, frames >= old_frames);
}

/* Additive: setImage(Image) - a single frame the size of the image. */
NJFN(sprite_setImage_simple)
{
    nj_ref img = NJ_ARG_REF(args, 0);
    int w = 0, h = 0;

    if (!img) {
        NJ_THROW(vm, "java/lang/NullPointerException", "image is null");
    }
    if (!nj_midp_image_size(vm, img, &w, &h)) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException", "not an image");
    }
    return sprite_set_frames(vm, NJ_THIS(args), img, w, h, 1);
}

NJFN(sprite_getImage)
{
    ret->r = fgr(vm, NJ_THIS(args), NJ_C_SPRITE, NJ_F_SP_IMAGE, D_IMAGE);
    return NJ_OK;
}

/* The raw frame a sequence index selects (the sequence may not be set). */
static int sprite_raw_frame(nj_vm *vm, nj_ref s, int idx)
{
    nj_ref seq = fgr(vm, s, NJ_C_SPRITE, SP_SEQ, D_INTARR);
    int n = (int)SPG(vm, s, NJ_F_SP_FRAMES, D_INT);

    if (idx < 0) {
        idx = 0;
    }
    if (seq) {
        nj_u4 len = nj_array_length(seq);
        if (len == 0u) {
            return 0;
        }
        if ((nj_u4)idx >= len) {
            idx = (int)len - 1;
        }
        return nj_array_get_int(seq, (nj_u4)idx);
    }
    if (n > 0 && idx >= n) {
        idx = n - 1;
    }
    return idx;
}

static int sprite_seq_len(nj_vm *vm, nj_ref s)
{
    nj_ref seq = fgr(vm, s, NJ_C_SPRITE, SP_SEQ, D_INTARR);

    if (seq) {
        return (int)nj_array_length(seq);
    }
    return (int)SPG(vm, s, NJ_F_SP_FRAMES, D_INT);
}

NJFN(sprite_setFrame)
{
    nj_ref self = NJ_THIS(args);
    int idx = NJ_ARG_INT(args, 0);
    int len = sprite_seq_len(vm, self);

    if (idx < 0 || idx >= len) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException",
                        "frame sequence index");
    }
    SPS(vm, self, SP_SEQIDX, D_INT, idx);
    SPS(vm, self, NJ_F_SP_FRAME, D_INT, sprite_raw_frame(vm, self, idx));
    return NJ_OK;
}

NJFN(sprite_getFrame)
{
    ret->i = SPG(vm, NJ_THIS(args), SP_SEQIDX, D_INT);
    return NJ_OK;
}

NJFN(sprite_getFrameSequenceIndex)
{
    ret->i = SPG(vm, NJ_THIS(args), SP_SEQIDX, D_INT);
    return NJ_OK;
}

NJFN(sprite_getRawFrameCount)
{
    ret->i = SPG(vm, NJ_THIS(args), NJ_F_SP_FRAMES, D_INT);
    return NJ_OK;
}

NJFN(sprite_getFrameSequenceLength)
{
    ret->i = sprite_seq_len(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(sprite_setFrameSequence)
{
    nj_ref self = NJ_THIS(args);
    nj_ref seq = NJ_ARG_REF(args, 0);
    nj_ref copy;
    nj_u4 mark, i, n;
    int frames = (int)SPG(vm, self, NJ_F_SP_FRAMES, D_INT);

    if (!seq) {
        /* null reverts to the default sequence. */
        fsr(vm, self, NJ_C_SPRITE, SP_SEQ, D_INTARR, NULL);
        SPS(vm, self, SP_SEQIDX, D_INT, 0);
        SPS(vm, self, NJ_F_SP_FRAME, D_INT, 0);
        return NJ_OK;
    }
    n = nj_array_length(seq);
    if (n < 1u) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "frame sequence is empty");
    }
    for (i = 0u; i < n; i++) {
        nj_s4 v = nj_array_get_int(seq, i);
        if (v < 0 || v >= frames) {
            NJ_THROW(vm, "java/lang/ArrayIndexOutOfBoundsException",
                     "frame sequence element");
        }
    }
    mark = nj_root_mark(vm);
    copy = nj_root(vm, nj_int_array_new(vm, n));
    if (!copy) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    for (i = 0u; i < n; i++) {
        nj_array_set_int(copy, i, nj_array_get_int(seq, i));
    }
    fsr(vm, self, NJ_C_SPRITE, SP_SEQ, D_INTARR, copy);
    nj_root_release(vm, mark);

    SPS(vm, self, SP_SEQIDX, D_INT, 0);
    SPS(vm, self, NJ_F_SP_FRAME, D_INT, nj_array_get_int(copy, 0u));
    return NJ_OK;
}

NJFN(sprite_nextFrame)
{
    nj_ref self = NJ_THIS(args);
    int len = sprite_seq_len(vm, self);
    int idx = (int)SPG(vm, self, SP_SEQIDX, D_INT);

    if (len < 1) {
        return NJ_OK;
    }
    idx = (idx + 1) % len;
    SPS(vm, self, SP_SEQIDX, D_INT, idx);
    SPS(vm, self, NJ_F_SP_FRAME, D_INT, sprite_raw_frame(vm, self, idx));
    return NJ_OK;
}

NJFN(sprite_prevFrame)
{
    nj_ref self = NJ_THIS(args);
    int len = sprite_seq_len(vm, self);
    int idx = (int)SPG(vm, self, SP_SEQIDX, D_INT);

    if (len < 1) {
        return NJ_OK;
    }
    idx = (idx + len - 1) % len;
    SPS(vm, self, SP_SEQIDX, D_INT, idx);
    SPS(vm, self, NJ_F_SP_FRAME, D_INT, sprite_raw_frame(vm, self, idx));
    return NJ_OK;
}

/* ---- transforms ---- */
NJFN(sprite_setTransform)
{
    nj_ref self = NJ_THIS(args);
    int t = NJ_ARG_INT(args, 0);
    int fw, fh;

    if (!trans_ok(t)) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException", "bad transform");
    }
    fw = (int)SPG(vm, self, NJ_F_SP_FRAMEW, D_INT);
    fh = (int)SPG(vm, self, NJ_F_SP_FRAMEH, D_INT);
    SPS(vm, self, NJ_F_SP_TRANS, D_INT, t);
    /* Repositions the sprite so the reference pixel stays put (MIDP). */
    sprite_sync_ref(vm, self, fw, fh, t, 1);
    sprite_sync_size(vm, self, fw, fh, t);
    return NJ_OK;
}

NJFN(sprite_getTransform)
{
    ret->i = SPG(vm, NJ_THIS(args), NJ_F_SP_TRANS, D_INT);
    return NJ_OK;
}

/* ---- reference pixel ---- */
NJFN(sprite_defineReferencePixel)
{
    nj_ref self = NJ_THIS(args);
    int fw = (int)SPG(vm, self, NJ_F_SP_FRAMEW, D_INT);
    int fh = (int)SPG(vm, self, NJ_F_SP_FRAMEH, D_INT);
    int t = (int)SPG(vm, self, NJ_F_SP_TRANS, D_INT);

    SPS(vm, self, SP_REFDX, D_INT, NJ_ARG_INT(args, 0));
    SPS(vm, self, SP_REFDY, D_INT, NJ_ARG_INT(args, 1));
    /* Defining the pixel never moves the sprite (MIDP: getX/getY unchanged). */
    sprite_sync_ref(vm, self, fw, fh, t, 0);
    return NJ_OK;
}

NJFN(sprite_setRefPixelPosition)
{
    nj_ref self = NJ_THIS(args);
    lay_pos(vm, self,
            NJ_ARG_INT(args, 0) - (int)SPG(vm, self, NJ_F_SP_REFX, D_INT),
            NJ_ARG_INT(args, 1) - (int)SPG(vm, self, NJ_F_SP_REFY, D_INT));
    return NJ_OK;
}

NJFN(sprite_getRefPixelX)
{
    nj_ref self = NJ_THIS(args);
    ret->i = lay_x(vm, self) + SPG(vm, self, NJ_F_SP_REFX, D_INT);
    return NJ_OK;
}

NJFN(sprite_getRefPixelY)
{
    nj_ref self = NJ_THIS(args);
    ret->i = lay_y(vm, self) + SPG(vm, self, NJ_F_SP_REFY, D_INT);
    return NJ_OK;
}

NJFN(sprite_defineCollisionRectangle)
{
    nj_ref self = NJ_THIS(args);
    int w = NJ_ARG_INT(args, 2);
    int h = NJ_ARG_INT(args, 3);

    if (w < 0 || h < 0) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "negative collision rectangle");
    }
    SPS(vm, self, SP_COLX, D_INT, NJ_ARG_INT(args, 0));
    SPS(vm, self, SP_COLY, D_INT, NJ_ARG_INT(args, 1));
    SPS(vm, self, SP_COLW, D_INT, w);
    SPS(vm, self, SP_COLH, D_INT, h);
    return NJ_OK;
}

/* ---- painting ---- */
/*
 * Source rectangle of the frame being displayed.  spFrame always holds the RAW
 * frame (already resolved through the frame sequence), so this is a modulo.
 */
static void sprite_frame_rect(nj_vm *vm, nj_ref s, int iw, int ih,
                              int *sx, int *sy, int *sw, int *sh)
{
    int cols = (int)SPG(vm, s, NJ_F_SP_COLS, D_INT);
    int fw = (int)SPG(vm, s, NJ_F_SP_FRAMEW, D_INT);
    int fh = (int)SPG(vm, s, NJ_F_SP_FRAMEH, D_INT);
    int f = (int)SPG(vm, s, NJ_F_SP_FRAME, D_INT);

    if (cols < 1) { cols = 1; }
    if (fw < 1) { fw = iw; }
    if (fh < 1) { fh = ih; }
    if (f < 0) { f = 0; }
    *sx = (f % cols) * fw;
    *sy = (f / cols) * fh;
    *sw = fw;
    *sh = fh;
    /* Defensive: never read past the image even if the fields disagree. */
    if (*sx + *sw > iw) { *sw = iw - *sx; }
    if (*sy + *sh > ih) { *sh = ih - *sy; }
    if (*sw < 0) { *sw = 0; }
    if (*sh < 0) { *sh = 0; }
}

NJFN(sprite_paint)
{
    nj_ref self = NJ_THIS(args);
    nj_ref gobj = NJ_ARG_REF(args, 0);
    nj_ref imgref;
    njg_gfx_bind b;
    nj_gfx g;
    nj_image view;
    int iw = 0, ih = 0, sx, sy, sw, sh, t, dw, dh;

    if (!gobj) {
        NJ_THROW(vm, "java/lang/NullPointerException", "graphics is null");
    }
    if (!lay_visible(vm, self)) {
        return NJ_OK;
    }
    imgref = fgr(vm, self, NJ_C_SPRITE, NJ_F_SP_IMAGE, D_IMAGE);
    if (!imgref || !nj_midp_image_size(vm, imgref, &iw, &ih)) {
        return NJ_OK;
    }
    t = (int)SPG(vm, self, NJ_F_SP_TRANS, D_INT);
    if (!trans_ok(t)) {
        t = NJ_TRANS_NONE;
    }
    sprite_frame_rect(vm, self, iw, ih, &sx, &sy, &sw, &sh);
    if (sw <= 0 || sh <= 0) {
        return NJ_OK;
    }
    if (!njg_bind(vm, gobj, &b, &g)) {
        return vm->exception ? NJ_ERR : NJ_OK;
    }

    /*
     * Cheap rejection against the Graphics' clip, which the LayerManager has
     * already narrowed to the view window: this is what makes a layer that is
     * outside the visible region free.
     */
    trans_size(t, sw, sh, &dw, &dh);
    if (g.clip_w > 0 && g.clip_h > 0 &&
        !rects_overlap(lay_x(vm, self) + g.translate_x,
                       lay_y(vm, self) + g.translate_y, dw, dh,
                       g.clip_x, g.clip_y, g.clip_w, g.clip_h)) {
        return NJ_OK;
    }
    if (!image_view(vm, imgref, sx, sy, sw, sh, &view)) {
        return NJ_OK;
    }

    /* The sprite's upper-left corner goes to (lX,lY): nj_gfx places the
     * TRANSFORMED region's bounding box at (dx,dy), which is exactly the
     * sprite's visual bounds. */
    nj_gfx_draw_region(&g, &view, sx, sy, sw, sh, t, g.use_xor, g.xor_color,
                       NULL, lay_x(vm, self), lay_y(vm, self), NJ_ROP_PAINT);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Sprite collision                                                    */
/* ------------------------------------------------------------------ */
/* Everything collision needs about one sprite, resolved once per call. */
typedef struct sp_view {
    nj_u4 *px;              /* raw image pixels (ARGB8888), or NULL */
    int    iw, ih;          /* image size                           */
    int    fw, fh;          /* frame size                           */
    int    sx, sy;          /* frame origin inside the image        */
    int    t;               /* transform                            */
    int    lx, ly;          /* upper-left corner of the visual box  */
    int    vw, vh;          /* transformed frame size               */
    int    cx, cy, cw, ch;  /* collision rectangle, frame space     */
    int    refx, refy;      /* reference offset, transformed space  */
    int    ok, visible;
} sp_view;

static void sp_load(nj_vm *vm, nj_ref s, sp_view *v)
{
    nj_ref img;
    int w = 0, h = 0, cols, f;

    v->px = NULL;
    v->iw = v->ih = 0;
    v->fw = (int)SPG(vm, s, NJ_F_SP_FRAMEW, D_INT);
    v->fh = (int)SPG(vm, s, NJ_F_SP_FRAMEH, D_INT);
    v->t = (int)SPG(vm, s, NJ_F_SP_TRANS, D_INT);
    if (!trans_ok(v->t)) {
        v->t = NJ_TRANS_NONE;
    }
    v->lx = lay_x(vm, s);
    v->ly = lay_y(vm, s);
    v->cx = (int)SPG(vm, s, SP_COLX, D_INT);
    v->cy = (int)SPG(vm, s, SP_COLY, D_INT);
    v->cw = (int)SPG(vm, s, SP_COLW, D_INT);
    v->ch = (int)SPG(vm, s, SP_COLH, D_INT);
    v->refx = (int)SPG(vm, s, NJ_F_SP_REFX, D_INT);
    v->refy = (int)SPG(vm, s, NJ_F_SP_REFY, D_INT);
    v->visible = lay_visible(vm, s);

    img = fgr(vm, s, NJ_C_SPRITE, NJ_F_SP_IMAGE, D_IMAGE);
    if (img && nj_midp_image_size(vm, img, &w, &h)) {
        v->px = nj_midp_image_pixels(vm, img);
        v->iw = w;
        v->ih = h;
    }
    if (v->fw < 1) { v->fw = w; }
    if (v->fh < 1) { v->fh = h; }
    cols = (int)SPG(vm, s, NJ_F_SP_COLS, D_INT);
    f = (int)SPG(vm, s, NJ_F_SP_FRAME, D_INT);
    if (cols < 1) { cols = 1; }
    if (f < 0) { f = 0; }
    v->sx = (f % cols) * v->fw;
    v->sy = (f / cols) * v->fh;
    trans_size(v->t, v->fw, v->fh, &v->vw, &v->vh);
    v->ok = (v->px != NULL && v->fw > 0 && v->fh > 0 &&
             v->cw > 0 && v->ch > 0);
}

/* Bounding box of the sprite's collision rectangle in painter coordinates. */
static void sp_world_rect(const sp_view *v, int *x, int *y, int *w, int *h)
{
    int rx, ry;

    trans_rect(v->t, v->fw, v->fh, v->cx, v->cy, v->cw, v->ch, &rx, &ry, w, h);
    *x = v->lx + rx;
    *y = v->ly + ry;
}

/*
 * Is the pixel at visual-local (vx,vy) opaque?
 *
 * Opaque means "the player can see it": alpha != 0.  Every Image in this
 * runtime is ARGB8888 (nj_midp_image_pixels), so the alpha channel is always
 * there; an image decoded from an opaque source simply has 0xFF everywhere and
 * every pixel answers "opaque", which is the same answer a
 * compare-against-the-transparent-colour test gives.  Pixels outside the frame
 * (a collision rectangle may be larger than the sprite) and pixels outside the
 * collision rectangle are transparent, as MIDP specifies.
 */
static int sp_opaque(const sp_view *v, int vx, int vy)
{
    int ux, uy;
    nj_u4 p;

    if (!v->ok) {
        return 0;
    }
    if (vx < 0 || vy < 0 || vx >= v->vw || vy >= v->vh) {
        return 0;
    }
    trans_inv(v->t, v->fw, v->fh, vx, vy, &ux, &uy);
    if (ux < v->cx || uy < v->cy ||
        ux >= v->cx + v->cw || uy >= v->cy + v->ch) {
        return 0;
    }
    if (ux < 0 || uy < 0 || ux >= v->fw || uy >= v->fh) {
        return 0;
    }
    if (v->sx + ux >= v->iw || v->sy + uy >= v->ih) {
        return 0;
    }
    p = v->px[(nj_size)(v->sy + uy) * (nj_size)v->iw +
              (nj_size)(v->sx + ux)];
    return ((p >> 24) & 0xFFu) != 0u;
}

/* Pixel-level test between two loaded sprites inside their overlap. */
static int sp_pixel_hit(const sp_view *a, const sp_view *b,
                        int ox, int oy, int ow, int oh)
{
    int x, y;

    for (y = oy; y < oy + oh; y++) {
        for (x = ox; x < ox + ow; x++) {
            if (sp_opaque(a, x - a->lx, y - a->ly) &&
                sp_opaque(b, x - b->lx, y - b->ly)) {
                return 1;
            }
        }
    }
    return 0;
}

NJFN(sprite_collidesWith_sprite)
{
    sp_view a, b;
    int ax, ay, aw, ah, bx, by, bw, bh, ox, oy, ow, oh;
    nj_ref other = NJ_ARG_REF(args, 0);

    if (!other) {
        NJ_THROW(vm, "java/lang/NullPointerException", "sprite is null");
    }
    sp_load(vm, NJ_THIS(args), &a);
    sp_load(vm, other, &b);
    ret->i = 0;
    if (!a.visible || !b.visible) {
        return NJ_OK;
    }
    sp_world_rect(&a, &ax, &ay, &aw, &ah);
    sp_world_rect(&b, &bx, &by, &bw, &bh);
    ox = imax(ax, bx);
    oy = imax(ay, by);
    ow = imin(ax + aw, bx + bw) - ox;
    oh = imin(ay + ah, by + bh) - oy;
    if (ow <= 0 || oh <= 0) {
        return NJ_OK;
    }
    if (!NJ_ARG_INT(args, 1)) {
        ret->i = 1;
        return NJ_OK;
    }
    ret->i = sp_pixel_hit(&a, &b, ox, oy, ow, oh);
    return NJ_OK;
}

/*
 * Additive collidesWith(Sprite, boolean, boolean): the third argument lines the
 * two sprites' reference pixels up before testing, which is what a game wants
 * when it compares a projectile with a target by anchor point rather than by
 * drawn position.
 */
NJFN(sprite_collidesWith_sprite_ref)
{
    sp_view a, b;
    int ax, ay, aw, ah, bx, by, bw, bh, ox, oy, ow, oh;
    nj_ref other = NJ_ARG_REF(args, 0);

    if (!other) {
        NJ_THROW(vm, "java/lang/NullPointerException", "sprite is null");
    }
    sp_load(vm, NJ_THIS(args), &a);
    sp_load(vm, other, &b);
    if (NJ_ARG_INT(args, 2)) {
        b.lx = a.lx + a.refx - b.refx;
        b.ly = a.ly + a.refy - b.refy;
    }
    ret->i = 0;
    if (!a.visible || !b.visible) {
        return NJ_OK;
    }
    sp_world_rect(&a, &ax, &ay, &aw, &ah);
    sp_world_rect(&b, &bx, &by, &bw, &bh);
    ox = imax(ax, bx);
    oy = imax(ay, by);
    ow = imin(ax + aw, bx + bw) - ox;
    oh = imin(ay + ah, by + bh) - oy;
    if (ow <= 0 || oh <= 0) {
        return NJ_OK;
    }
    if (!NJ_ARG_INT(args, 1)) {
        ret->i = 1;
        return NJ_OK;
    }
    ret->i = sp_pixel_hit(&a, &b, ox, oy, ow, oh);
    return NJ_OK;
}

NJFN(sprite_collidesWith_image)
{
    sp_view a;
    nj_ref img = NJ_ARG_REF(args, 0);
    nj_u4 *px;
    int iw = 0, ih = 0, ix, iy;
    int ax, ay, aw, ah, ox, oy, ow, oh, x, y;

    if (!img) {
        NJ_THROW(vm, "java/lang/NullPointerException", "image is null");
    }
    if (!nj_midp_image_size(vm, img, &iw, &ih)) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException", "not an image");
    }
    px = nj_midp_image_pixels(vm, img);
    ix = NJ_ARG_INT(args, 1);
    iy = NJ_ARG_INT(args, 2);
    sp_load(vm, NJ_THIS(args), &a);
    ret->i = 0;
    if (!a.visible || !a.ok || !px || iw <= 0 || ih <= 0) {
        return NJ_OK;
    }
    sp_world_rect(&a, &ax, &ay, &aw, &ah);
    ox = imax(ax, ix);
    oy = imax(ay, iy);
    ow = imin(ax + aw, ix + iw) - ox;
    oh = imin(ay + ah, iy + ih) - oy;
    if (ow <= 0 || oh <= 0) {
        return NJ_OK;
    }
    if (!NJ_ARG_INT(args, 3)) {
        ret->i = 1;
        return NJ_OK;
    }
    for (y = oy; y < oy + oh; y++) {
        for (x = ox; x < ox + ow; x++) {
            nj_u4 p = px[(nj_size)(y - iy) * (nj_size)iw + (nj_size)(x - ix)];
            if (((p >> 24) & 0xFFu) == 0u) {
                continue;
            }
            if (sp_opaque(&a, x - a.lx, y - a.ly)) {
                ret->i = 1;
                return NJ_OK;
            }
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* TiledLayer                                                          */
/* ------------------------------------------------------------------ */
/*
 * Cell contents are tile indices.  0 is an empty cell.  1..nStatic refer to the
 * static tiles, read row-major from the source image.  Animated tiles are
 * -1, -2, ... (MIDP 2.0); the positive alias nStatic+1, nStatic+2, ... is
 * accepted as well, purely for tolerance.  tlAnim[slot] is the static tile an
 * animated tile currently shows.
 */
static int tl_static_count(nj_vm *vm, nj_ref t)
{
    nj_ref img = fgr(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_IMAGE, D_IMAGE);
    int iw = 0, ih = 0, tw, th;

    if (!img || !nj_midp_image_size(vm, img, &iw, &ih)) {
        return 0;
    }
    tw = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_TILEW, D_INT);
    th = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_TILEH, D_INT);
    if (tw < 1 || th < 1) {
        return 0;
    }
    return (iw / tw) * (ih / th);
}

/* Map an animated tile reference to its table slot. */
static int tl_anim_slot(nj_vm *vm, nj_ref t, int idx, int *slot)
{
    int nstatic = tl_static_count(vm, t);
    int n = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_ANIMN, D_INT);

    if (idx < 0) {
        int s = -idx - 1;
        if (s >= 0 && s < n) {
            *slot = s;
            return 1;
        }
        return 0;
    }
    if (idx > nstatic) {
        int s = idx - nstatic - 1;
        if (s >= 0 && s < n) {
            *slot = s;
            return 1;
        }
    }
    return 0;
}

/* Is `idx` usable as a cell value?  (0, a static tile, or an existing anim.) */
static int tl_index_ok(nj_vm *vm, nj_ref t, int idx)
{
    int slot;

    if (idx == 0) {
        return 1;
    }
    if (idx > 0 && idx <= tl_static_count(vm, t)) {
        return 1;
    }
    return tl_anim_slot(vm, t, idx, &slot);
}

/* Everything a paint or a collision needs about one TiledLayer. */
typedef struct tl_view {
    nj_ref  cells;                  /* int[] cell -> tile index   */
    nj_ref  anim;                   /* int[] animated tile table  */
    nj_u4  *px;                     /* tile atlas pixels          */
    int     iw, ih;                 /* tile atlas size            */
    int     cols, rows;             /* grid size                  */
    int     cellw, cellh;           /* cell size                  */
    int     tilew, tileh;           /* source tile size           */
    int     tpr;                    /* tiles per row in the atlas */
    int     nstatic;                /* number of static tiles     */
    int     anim_n;                 /* number of animated tiles   */
    int     lx, ly, lw, lh;         /* layer bounds               */
    int     visible;
    int     ok;
} tl_view;

static void tl_load(nj_vm *vm, nj_ref t, tl_view *v)
{
    nj_ref img = fgr(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_IMAGE, D_IMAGE);

    v->cells = fgr(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_CELLS, D_INTARR);
    v->anim = fgr(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_ANIM, D_INTARR);
    v->px = NULL;
    v->iw = v->ih = 0;
    if (img) {
        v->px = nj_midp_image_pixels(vm, img);
        (void)nj_midp_image_size(vm, img, &v->iw, &v->ih);
    }
    v->cols = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_COLS, D_INT);
    v->rows = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_ROWS, D_INT);
    v->cellw = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_CELLW, D_INT);
    v->cellh = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_CELLH, D_INT);
    v->tilew = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_TILEW, D_INT);
    v->tileh = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_TILEH, D_INT);
    v->anim_n = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_ANIMN, D_INT);
    v->lx = lay_x(vm, t);
    v->ly = lay_y(vm, t);
    v->lw = lay_w(vm, t);
    v->lh = lay_h(vm, t);
    v->visible = lay_visible(vm, t);
    v->tpr = (v->tilew > 0) ? v->iw / v->tilew : 0;
    v->nstatic = v->tpr * ((v->tileh > 0) ? v->ih / v->tileh : 0);
    v->ok = (v->px != NULL && v->cells != NULL && v->cols > 0 &&
             v->rows > 0 && v->cellw > 0 && v->cellh > 0 &&
             v->tilew > 0 && v->tileh > 0 && v->tpr > 0);
}

/* Resolve a cell value to a static tile index; 0 = nothing to draw. */
static int tl_resolve_in(const tl_view *v, int idx)
{
    int slot;

    if (idx == 0) {
        return 0;
    }
    if (idx > 0 && idx <= v->nstatic) {
        return idx;
    }
    if (idx < 0) {
        slot = -idx - 1;
    } else if (idx > v->nstatic) {
        slot = idx - v->nstatic - 1;
    } else {
        return 0;
    }
    if (slot < 0 || slot >= v->anim_n || !v->anim ||
        (nj_u4)slot >= nj_array_length(v->anim)) {
        return 0;
    }
    return nj_array_get_int(v->anim, (nj_u4)slot);
}

/* Is the pixel at painter coordinates (wx,wy) part of an opaque tile pixel? */
static int tl_opaque(const tl_view *v, int wx, int wy)
{
    int col, row, idx, tile, cx, cy, tx, ty;
    nj_u4 off, p;

    if (!v->ok) {
        return 0;
    }
    if (wx < v->lx || wy < v->ly ||
        wx >= v->lx + v->lw || wy >= v->ly + v->lh) {
        return 0;
    }
    col = (wx - v->lx) / v->cellw;
    row = (wy - v->ly) / v->cellh;
    if (col < 0 || row < 0 || col >= v->cols || row >= v->rows) {
        return 0;
    }
    off = (nj_u4)(row * v->cols + col);
    if (off >= nj_array_length(v->cells)) {
        return 0;
    }
    idx = nj_array_get_int(v->cells, off);
    if (idx == 0) {
        return 0;
    }
    tile = tl_resolve_in(v, idx);
    if (tile < 1 || tile > v->nstatic) {
        return 0;
    }
    cx = (wx - v->lx) % v->cellw;
    cy = (wy - v->ly) % v->cellh;
    /* A cell may be larger than its tile; the padding is transparent. */
    if (cx >= v->tilew || cy >= v->tileh) {
        return 0;
    }
    tx = ((tile - 1) % v->tpr) * v->tilew + cx;
    ty = ((tile - 1) / v->tpr) * v->tileh + cy;
    if (tx >= v->iw || ty >= v->ih) {
        return 0;
    }
    p = v->px[(nj_size)ty * (nj_size)v->iw + (nj_size)tx];
    return ((p >> 24) & 0xFFu) != 0u;
}

NJFN(sprite_collidesWith_tiled)
{
    sp_view a;
    tl_view t;
    nj_ref other = NJ_ARG_REF(args, 0);
    int ax, ay, aw, ah, ox, oy, ow, oh, x, y;

    if (!other) {
        NJ_THROW(vm, "java/lang/NullPointerException", "tiled layer is null");
    }
    sp_load(vm, NJ_THIS(args), &a);
    tl_load(vm, other, &t);
    ret->i = 0;
    if (!a.visible || !t.visible || !a.ok || !t.ok) {
        return NJ_OK;
    }
    sp_world_rect(&a, &ax, &ay, &aw, &ah);
    ox = imax(ax, t.lx);
    oy = imax(ay, t.ly);
    ow = imin(ax + aw, t.lx + t.lw) - ox;
    oh = imin(ay + ah, t.ly + t.lh) - oy;
    if (ow <= 0 || oh <= 0) {
        return NJ_OK;
    }
    if (!NJ_ARG_INT(args, 1)) {
        /* Bounds test: does the collision rectangle touch a non-empty cell? */
        int col0 = (ox - t.lx) / t.cellw, col1 = (ox + ow - 1 - t.lx) / t.cellw;
        int row0 = (oy - t.ly) / t.cellh, row1 = (oy + oh - 1 - t.ly) / t.cellh;
        int col, row;
        for (row = row0; row <= row1; row++) {
            for (col = col0; col <= col1; col++) {
                nj_u4 off;
                if (col < 0 || row < 0 || col >= t.cols || row >= t.rows) {
                    continue;
                }
                off = (nj_u4)(row * t.cols + col);
                if (off < nj_array_length(t.cells) &&
                    nj_array_get_int(t.cells, off) != 0) {
                    ret->i = 1;
                    return NJ_OK;
                }
            }
        }
        return NJ_OK;
    }
    for (y = oy; y < oy + oh; y++) {
        for (x = ox; x < ox + ow; x++) {
            if (sp_opaque(&a, x - a.lx, y - a.ly) && tl_opaque(&t, x, y)) {
                ret->i = 1;
                return NJ_OK;
            }
        }
    }
    return NJ_OK;
}

/*
 * Additive collidesWith(Layer, boolean): dispatch on the layer's real class so
 * that generic code can test against "whatever layer this is" without casting.
 * A layer that is neither a Sprite nor a TiledLayer has no pixel data, so only
 * its bounds can be tested.
 */
NJFN(sprite_collidesWith_layer)
{
    nj_ref other = NJ_ARG_REF(args, 0);
    nj_class *sc, *tc;

    if (!other || !other->cls) {
        NJ_THROW(vm, "java/lang/NullPointerException", "layer is null");
    }
    sc = nj_class_lookup(vm, NJ_C_SPRITE);
    tc = nj_class_lookup(vm, NJ_C_TILEDLAYER);
    if (sc && nj_class_is_subclass_of(other->cls, sc)) {
        return sprite_collidesWith_sprite(vm, th, args, ret);
    }
    if (tc && nj_class_is_subclass_of(other->cls, tc)) {
        return sprite_collidesWith_tiled(vm, th, args, ret);
    }
    {
        sp_view a;
        int ax, ay, aw, ah;

        sp_load(vm, NJ_THIS(args), &a);
        ret->i = 0;
        if (!a.visible || !lay_visible(vm, other)) {
            return NJ_OK;
        }
        sp_world_rect(&a, &ax, &ay, &aw, &ah);
        ret->i = rects_overlap(ax, ay, aw, ah, lay_x(vm, other), lay_y(vm, other),
                               lay_w(vm, other), lay_h(vm, other)) ? 1 : 0;
    }
    return NJ_OK;
}

static nj_status tiled_construct(nj_vm *vm, nj_ref self, int cols, int rows,
                                 nj_ref img, int tilew, int tileh)
{
    int iw = 0, ih = 0;
    nj_ref cells, anim;
    nj_u4 mark;

    if (!img) {
        return nj_throw(vm, "java/lang/NullPointerException", "image is null");
    }
    if (cols < 1 || rows < 1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "rows and columns must be positive");
    }
    if (tilew < 1 || tileh < 1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "tile size must be positive");
    }
    if (!nj_midp_image_size(vm, img, &iw, &ih) || iw <= 0 || ih <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "not an image");
    }
    if ((iw % tilew) != 0 || (ih % tileh) != 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "image size is not a multiple of the tile size");
    }
    if (nj_call_special(vm, self, NJ_C_LAYER, "<init>", "()V", NULL, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }

    mark = nj_root_mark(vm);
    cells = nj_root(vm, nj_int_array_new(vm, (nj_u4)cols * (nj_u4)rows));
    if (!cells) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    anim = nj_root(vm, nj_int_array_new(vm, 8u));
    if (!anim) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    fsr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_IMAGE, D_IMAGE, img);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_COLS, D_INT, cols);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ROWS, D_INT, rows);
    /* MIDP couples the cell size to the tile size; they are kept separate here
     * because nj_midp.h has room for a future cell-size API, and because a cell
     * larger than its tile simply leaves transparent padding. */
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLW, D_INT, tilew);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLH, D_INT, tileh);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_TILEW, D_INT, tilew);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_TILEH, D_INT, tileh);
    fsr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLS, D_INTARR, cells);
    fsr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIM, D_INTARR, anim);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIMN, D_INT, 0);
    fsi(vm, self, NJ_C_TILEDLAYER, TL_ANIMCAP, D_INT, 8);
    lay_size(vm, self, cols * tilew, rows * tileh);
    nj_root_release(vm, mark);
    return NJ_OK;
}

NJFN(tiled_init)
{
    return tiled_construct(vm, NJ_THIS(args), NJ_ARG_INT(args, 0),
                           NJ_ARG_INT(args, 1), NJ_ARG_REF(args, 2),
                           NJ_ARG_INT(args, 3), NJ_ARG_INT(args, 4));
}

NJFN(tiled_setStaticTileSet)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    int tiw = NJ_ARG_INT(args, 1);
    int tih = NJ_ARG_INT(args, 2);
    int iw = 0, ih = 0, old_n, new_n, i;
    nj_ref cells;

    if (!img) {
        NJ_THROW(vm, "java/lang/NullPointerException", "image is null");
    }
    if (tiw < 1 || tih < 1) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "tile size must be positive");
    }
    if (!nj_midp_image_size(vm, img, &iw, &ih)) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException", "not an image");
    }
    if ((iw % tiw) != 0 || (ih % tih) != 0) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "image size is not a multiple of the tile size");
    }
    old_n = tl_static_count(vm, self);
    new_n = (iw / tiw) * (ih / tih);

    fsr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_IMAGE, D_IMAGE, img);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_TILEW, D_INT, tiw);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_TILEH, D_INT, tih);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLW, D_INT, tiw);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLH, D_INT, tih);
    lay_size(vm, self,
             (int)fgi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_COLS, D_INT) * tiw,
             (int)fgi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ROWS, D_INT) * tih);

    if (new_n >= old_n) {
        return NJ_OK;   /* MIDP: cells and animated tiles are preserved */
    }
    /* Fewer tiles: the grid is cleared and the animated tiles are dropped. */
    cells = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLS, D_INTARR);
    if (cells) {
        for (i = 0; (nj_u4)i < nj_array_length(cells); i++) {
            nj_array_set_int(cells, (nj_u4)i, 0);
        }
    }
    fsr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIM, D_INTARR, NULL);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIMN, D_INT, 0);
    fsi(vm, self, NJ_C_TILEDLAYER, TL_ANIMCAP, D_INT, 0);
    return NJ_OK;
}

static int tiled_cell_offset(nj_vm *vm, nj_ref t, int col, int row, int *off)
{
    int cols = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_COLS, D_INT);
    int rows = (int)fgi(vm, t, NJ_C_TILEDLAYER, NJ_F_TL_ROWS, D_INT);

    if (col < 0 || row < 0 || col >= cols || row >= rows) {
        return 0;
    }
    *off = row * cols + col;
    return 1;
}

NJFN(tiled_setCell)
{
    nj_ref self = NJ_THIS(args);
    int off;
    nj_ref cells;

    if (!tiled_cell_offset(vm, self, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                           &off)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "cell");
    }
    if (!tl_index_ok(vm, self, NJ_ARG_INT(args, 2))) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "tile index");
    }
    cells = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLS, D_INTARR);
    if (!cells || (nj_u4)off >= nj_array_length(cells)) {
        NJ_THROW(vm, "java/lang/IllegalStateException", "cell grid");
    }
    nj_array_set_int(cells, (nj_u4)off, NJ_ARG_INT(args, 2));
    return NJ_OK;
}

NJFN(tiled_getCell)
{
    nj_ref self = NJ_THIS(args);
    int off;
    nj_ref cells;

    if (!tiled_cell_offset(vm, self, NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1),
                           &off)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "cell");
    }
    cells = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLS, D_INTARR);
    ret->i = (cells && (nj_u4)off < nj_array_length(cells))
                 ? nj_array_get_int(cells, (nj_u4)off) : 0;
    return NJ_OK;
}

NJFN(tiled_createAnimatedTile)
{
    nj_ref self = NJ_THIS(args);
    int idx = NJ_ARG_INT(args, 0);
    int n = (int)fgi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIMN, D_INT);
    int cap = (int)fgi(vm, self, NJ_C_TILEDLAYER, TL_ANIMCAP, D_INT);
    nj_ref anim;

    if (idx < 0 || idx > tl_static_count(vm, self)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "static tile index");
    }
    if (n >= cap) {
        nj_ref bigger;
        int newcap = (cap > 0) ? cap * 2 : 8;
        int i;
        nj_u4 mark = nj_root_mark(vm);

        bigger = nj_root(vm, nj_int_array_new(vm, (nj_u4)newcap));
        if (!bigger) {
            nj_root_release(vm, mark);
            return NJ_ERR;
        }
        anim = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIM, D_INTARR);
        for (i = 0; i < n && anim && (nj_u4)i < nj_array_length(anim); i++) {
            nj_array_set_int(bigger, (nj_u4)i,
                             nj_array_get_int(anim, (nj_u4)i));
        }
        fsr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIM, D_INTARR, bigger);
        fsi(vm, self, NJ_C_TILEDLAYER, TL_ANIMCAP, D_INT, newcap);
        anim = bigger;
        nj_root_release(vm, mark);
    } else {
        anim = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIM, D_INTARR);
    }
    if (!anim || (nj_u4)n >= nj_array_length(anim)) {
        NJ_THROW(vm, "java/lang/IllegalStateException", "animated tile table");
    }
    nj_array_set_int(anim, (nj_u4)n, idx);
    fsi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIMN, D_INT, n + 1);
    /* MIDP: animated tile indices are always negative; the first is -1. */
    ret->i = -(n + 1);
    return NJ_OK;
}

NJFN(tiled_setAnimatedTile)
{
    nj_ref self = NJ_THIS(args);
    int static_idx = NJ_ARG_INT(args, 1);
    int slot;
    nj_ref anim;

    if (static_idx < 0 || static_idx > tl_static_count(vm, self)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "static tile index");
    }
    if (!tl_anim_slot(vm, self, NJ_ARG_INT(args, 0), &slot)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "animated tile index");
    }
    anim = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIM, D_INTARR);
    if (!anim || (nj_u4)slot >= nj_array_length(anim)) {
        NJ_THROW(vm, "java/lang/IllegalStateException", "animated tile table");
    }
    nj_array_set_int(anim, (nj_u4)slot, static_idx);
    return NJ_OK;
}

NJFN(tiled_getAnimatedTile)
{
    nj_ref self = NJ_THIS(args);
    int slot;
    nj_ref anim;

    if (!tl_anim_slot(vm, self, NJ_ARG_INT(args, 0), &slot)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "animated tile index");
    }
    anim = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ANIM, D_INTARR);
    ret->i = (anim && (nj_u4)slot < nj_array_length(anim))
                 ? nj_array_get_int(anim, (nj_u4)slot) : 0;
    return NJ_OK;
}

static nj_status tiled_fill(nj_vm *vm, nj_ref self, int col, int row,
                            int ncols, int nrows, int idx)
{
    int cols = (int)fgi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_COLS, D_INT);
    int rows = (int)fgi(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_ROWS, D_INT);
    nj_ref cells;
    int x, y;

    if (ncols < 0 || nrows < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative cell count");
    }
    if (col < 0 || row < 0 || (nj_s8)col + ncols > cols ||
        (nj_s8)row + nrows > rows) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException",
                        "cell region");
    }
    if (!tl_index_ok(vm, self, idx)) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException",
                        "tile index");
    }
    cells = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_CELLS, D_INTARR);
    if (!cells) {
        return nj_throw(vm, "java/lang/IllegalStateException", "cell grid");
    }
    for (y = row; y < row + nrows; y++) {
        for (x = col; x < col + ncols; x++) {
            nj_u4 off = (nj_u4)(y * cols + x);
            if (off < nj_array_length(cells)) {
                nj_array_set_int(cells, off, idx);
            }
        }
    }
    return NJ_OK;
}

NJFN(tiled_fillCells)
{
    return tiled_fill(vm, NJ_THIS(args), NJ_ARG_INT(args, 0),
                      NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2),
                      NJ_ARG_INT(args, 3), NJ_ARG_INT(args, 4));
}

/* Additive: fillCells(col,row,numCols,numRows) empties the region. */
NJFN(tiled_fillCells_empty)
{
    return tiled_fill(vm, NJ_THIS(args), NJ_ARG_INT(args, 0),
                      NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2),
                      NJ_ARG_INT(args, 3), 0);
}

NJFN(tiled_getCellWidth)
{
    ret->i = fgi(vm, NJ_THIS(args), NJ_C_TILEDLAYER, NJ_F_TL_CELLW, D_INT);
    return NJ_OK;
}

NJFN(tiled_getCellHeight)
{
    ret->i = fgi(vm, NJ_THIS(args), NJ_C_TILEDLAYER, NJ_F_TL_CELLH, D_INT);
    return NJ_OK;
}

NJFN(tiled_getColumns)
{
    ret->i = fgi(vm, NJ_THIS(args), NJ_C_TILEDLAYER, NJ_F_TL_COLS, D_INT);
    return NJ_OK;
}

NJFN(tiled_getRows)
{
    ret->i = fgi(vm, NJ_THIS(args), NJ_C_TILEDLAYER, NJ_F_TL_ROWS, D_INT);
    return NJ_OK;
}

/*
 * paint(): draw ONLY the cells that can appear inside the Graphics' clip.
 *
 * The layer puts its upper-left corner at (lX,lY) in the Graphics' user space,
 * so the clip - which lives in surface coordinates - maps back to user space by
 * subtracting the translate.  That rectangle, intersected with the layer's own
 * bounds, gives the range of columns and rows worth visiting: a 200x200 map
 * costs the cells on screen, not 40000 blits.  Partially visible cells are
 * still drawn whole and the rasterizer trims them, which is cheaper than
 * clipping every blit by hand.  The image view is rebuilt per cell so that
 * has_alpha describes exactly the tile being drawn.
 */
NJFN(tiled_paint)
{
    nj_ref self = NJ_THIS(args);
    nj_ref gobj = NJ_ARG_REF(args, 0);
    nj_ref imgref;
    tl_view v;
    njg_gfx_bind b;
    nj_gfx g;
    nj_image view;
    int ux0, uy0, ux1, uy1, col0, col1, row0, row1, col, row;

    if (!gobj) {
        NJ_THROW(vm, "java/lang/NullPointerException", "graphics is null");
    }
    if (!lay_visible(vm, self)) {
        return NJ_OK;
    }
    imgref = fgr(vm, self, NJ_C_TILEDLAYER, NJ_F_TL_IMAGE, D_IMAGE);
    tl_load(vm, self, &v);
    if (!imgref || !v.ok) {
        return NJ_OK;
    }
    if (!njg_bind(vm, gobj, &b, &g)) {
        return vm->exception ? NJ_ERR : NJ_OK;
    }

    ux0 = g.clip_x - g.translate_x;
    uy0 = g.clip_y - g.translate_y;
    ux1 = ux0 + g.clip_w;
    uy1 = uy0 + g.clip_h;

    ux0 = imax(ux0, v.lx);
    uy0 = imax(uy0, v.ly);
    ux1 = imin(ux1, v.lx + v.cols * v.cellw);
    uy1 = imin(uy1, v.ly + v.rows * v.cellh);
    if (ux1 <= ux0 || uy1 <= uy0) {
        return NJ_OK;
    }
    col0 = imax((ux0 - v.lx) / v.cellw, 0);
    row0 = imax((uy0 - v.ly) / v.cellh, 0);
    col1 = imin((ux1 - 1 - v.lx) / v.cellw, v.cols - 1);
    row1 = imin((uy1 - 1 - v.ly) / v.cellh, v.rows - 1);
    if (col1 < col0 || row1 < row0) {
        return NJ_OK;
    }

    for (row = row0; row <= row1; row++) {
        for (col = col0; col <= col1; col++) {
            nj_u4 off = (nj_u4)(row * v.cols + col);
            int idx, tile, tx, ty;

            if (off >= nj_array_length(v.cells)) {
                continue;
            }
            idx = nj_array_get_int(v.cells, off);
            if (idx == 0) {
                continue;
            }
            tile = tl_resolve_in(&v, idx);
            if (tile < 1 || tile > v.nstatic) {
                continue;
            }
            tx = ((tile - 1) % v.tpr) * v.tilew;
            ty = ((tile - 1) / v.tpr) * v.tileh;
            if (!image_view(vm, imgref, tx, ty, v.tilew, v.tileh, &view)) {
                continue;
            }
            nj_gfx_draw_image(&g, &view, v.lx + col * v.cellw,
                              v.ly + row * v.cellh, tx, ty, v.tilew, v.tileh);
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* LayerManager                                                        */
/* ------------------------------------------------------------------ */
static nj_ref lm_array(nj_vm *vm, nj_ref m)
{
    return fgr(vm, m, NJ_C_LAYERMANAGER, NJ_F_LM_LAYERS, D_OBJARR);
}

static int lm_count(nj_vm *vm, nj_ref m)
{
    return (int)fgi(vm, m, NJ_C_LAYERMANAGER, LM_COUNT, D_INT);
}

static int lm_index_of(nj_vm *vm, nj_ref m, nj_ref layer)
{
    nj_ref a = lm_array(vm, m);
    int i, n = lm_count(vm, m);

    if (!a || !layer) {
        return -1;
    }
    for (i = 0; i < n && (nj_u4)i < nj_array_length(a); i++) {
        if (nj_array_get_ref(a, (nj_u4)i) == layer) {
            return i;
        }
    }
    return -1;
}

/* Make room for `need` entries, growing the array by doubling. */
static nj_status lm_reserve(nj_vm *vm, nj_ref m, int need)
{
    nj_ref a = lm_array(vm, m);
    nj_u4 len = a ? nj_array_length(a) : 0u;
    nj_ref bigger;
    nj_u4 mark;
    int cap, i, n = lm_count(vm, m);

    if (a && (nj_u4)need <= len) {
        return NJ_OK;
    }
    cap = (len > 0u) ? (int)len : 8;
    while (cap < need) {
        cap *= 2;
    }
    mark = nj_root_mark(vm);
    bigger = nj_root(vm,
                     nj_object_array_new(vm, "java/lang/Object", (nj_u4)cap));
    if (!bigger) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    for (i = 0; i < n && a && (nj_u4)i < len; i++) {
        nj_array_set_ref(bigger, (nj_u4)i, nj_array_get_ref(a, (nj_u4)i));
    }
    fsr(vm, m, NJ_C_LAYERMANAGER, NJ_F_LM_LAYERS, D_OBJARR, bigger);
    nj_root_release(vm, mark);
    return NJ_OK;
}

NJFN(lm_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref a = nj_object_array_new(vm, "java/lang/Object", 8u);

    if (!a) {
        return NJ_ERR;
    }
    fsr(vm, self, NJ_C_LAYERMANAGER, NJ_F_LM_LAYERS, D_OBJARR, a);
    fsi(vm, self, NJ_C_LAYERMANAGER, LM_COUNT, D_INT, 0);
    fsi(vm, self, NJ_C_LAYERMANAGER, NJ_F_LM_VX, D_INT, 0);
    fsi(vm, self, NJ_C_LAYERMANAGER, NJ_F_LM_VY, D_INT, 0);
    /* MIDP's default view window: (0,0), width and height Integer.MAX_VALUE. */
    fsi(vm, self, NJ_C_LAYERMANAGER, LM_VIEWW, D_INT, (nj_s4)0x7FFFFFFF);
    fsi(vm, self, NJ_C_LAYERMANAGER, LM_VIEWH, D_INT, (nj_s4)0x7FFFFFFF);
    return NJ_OK;
}

static void lm_remove_slot(nj_vm *vm, nj_ref m, int at)
{
    nj_ref a = lm_array(vm, m);
    int i, n = lm_count(vm, m);

    if (!a) {
        return;
    }
    for (i = at; i + 1 < n; i++) {
        nj_array_set_ref(a, (nj_u4)i, nj_array_get_ref(a, (nj_u4)(i + 1)));
    }
    if (n > 0) {
        nj_array_set_ref(a, (nj_u4)(n - 1), NULL);
    }
    fsi(vm, m, NJ_C_LAYERMANAGER, LM_COUNT, D_INT, n - 1);
}

static nj_status lm_put(nj_vm *vm, nj_ref m, nj_ref layer, int index)
{
    nj_ref a;
    int i, n;

    if (index < 0) {
        index = 0;
    }
    if (lm_reserve(vm, m, index + 1) != NJ_OK) {
        return NJ_ERR;
    }
    a = lm_array(vm, m);
    if (!a) {
        return nj_throw(vm, "java/lang/IllegalStateException", "layer list");
    }
    n = lm_count(vm, m);
    if (index > n) {
        index = n;
    }
    for (i = n; i > index; i--) {
        nj_array_set_ref(a, (nj_u4)i, nj_array_get_ref(a, (nj_u4)(i - 1)));
    }
    nj_array_set_ref(a, (nj_u4)index, layer);
    fsi(vm, m, NJ_C_LAYERMANAGER, LM_COUNT, D_INT, n + 1);
    return NJ_OK;
}

NJFN(lm_append)
{
    nj_ref self = NJ_THIS(args);
    nj_ref layer = NJ_ARG_REF(args, 0);
    int at;

    if (!layer) {
        NJ_THROW(vm, "java/lang/NullPointerException", "layer is null");
    }
    /* MIDP: a layer already in the list is removed first. */
    at = lm_index_of(vm, self, layer);
    if (at >= 0) {
        lm_remove_slot(vm, self, at);
    }
    return lm_put(vm, self, layer, lm_count(vm, self));
}

NJFN(lm_insert)
{
    nj_ref self = NJ_THIS(args);
    nj_ref layer = NJ_ARG_REF(args, 0);
    int index = NJ_ARG_INT(args, 1);
    int at, n;

    if (!layer) {
        NJ_THROW(vm, "java/lang/NullPointerException", "layer is null");
    }
    n = lm_count(vm, self);   /* the index is validated before any removal */
    if (index < 0 || index > n) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "layer index");
    }
    at = lm_index_of(vm, self, layer);
    if (at >= 0) {
        lm_remove_slot(vm, self, at);
    }
    return lm_put(vm, self, layer, index);
}

NJFN(lm_remove)
{
    nj_ref self = NJ_THIS(args);
    nj_ref layer = NJ_ARG_REF(args, 0);
    int at;

    if (!layer) {
        NJ_THROW(vm, "java/lang/NullPointerException", "layer is null");
    }
    at = lm_index_of(vm, self, layer);
    if (at >= 0) {
        lm_remove_slot(vm, self, at);   /* a layer not present: no-op */
    }
    return NJ_OK;
}

NJFN(lm_getLayerAt)
{
    nj_ref self = NJ_THIS(args);
    int index = NJ_ARG_INT(args, 0);
    nj_ref a;

    if (index < 0 || index >= lm_count(vm, self)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "layer index");
    }
    a = lm_array(vm, self);
    if (!a || (nj_u4)index >= nj_array_length(a)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "layer index");
    }
    ret->r = nj_array_get_ref(a, (nj_u4)index);
    return NJ_OK;
}

NJFN(lm_getSize)
{
    ret->i = lm_count(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(lm_setViewWindow)
{
    nj_ref self = NJ_THIS(args);
    int w = NJ_ARG_INT(args, 2);
    int h = NJ_ARG_INT(args, 3);

    if (w < 0 || h < 0) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "negative view window");
    }
    fsi(vm, self, NJ_C_LAYERMANAGER, NJ_F_LM_VX, D_INT, NJ_ARG_INT(args, 0));
    fsi(vm, self, NJ_C_LAYERMANAGER, NJ_F_LM_VY, D_INT, NJ_ARG_INT(args, 1));
    fsi(vm, self, NJ_C_LAYERMANAGER, LM_VIEWW, D_INT, w);
    fsi(vm, self, NJ_C_LAYERMANAGER, LM_VIEWH, D_INT, h);
    return NJ_OK;
}

NJFN(lm_getViewX) { ret->i = fgi(vm, NJ_THIS(args), NJ_C_LAYERMANAGER, NJ_F_LM_VX, D_INT); return NJ_OK; }
NJFN(lm_getViewY) { ret->i = fgi(vm, NJ_THIS(args), NJ_C_LAYERMANAGER, NJ_F_LM_VY, D_INT); return NJ_OK; }
NJFN(lm_getViewW) { ret->i = fgi(vm, NJ_THIS(args), NJ_C_LAYERMANAGER, LM_VIEWW, D_INT); return NJ_OK; }
NJFN(lm_getViewH) { ret->i = fgi(vm, NJ_THIS(args), NJ_C_LAYERMANAGER, LM_VIEWH, D_INT); return NJ_OK; }

/*
 * paint(Graphics, x, y)
 *
 * MIDP: translate so that the view window's origin lands on (x,y), narrow the
 * clip to the view-window rectangle at that spot, paint every visible layer
 * whose bounds meet the view window, then restore the Graphics state.
 *
 * The z-order is by DESCENDING index: index 0 is closest to the user, so it is
 * painted last and ends up on top.
 */
NJFN(lm_paint)
{
    nj_ref self = NJ_THIS(args);
    nj_ref gobj = NJ_ARG_REF(args, 0);
    int x = NJ_ARG_INT(args, 1);
    int y = NJ_ARG_INT(args, 2);
    nj_ref a;
    int i, n;
    nj_s4 vx, vy, vw, vh;
    nj_s4 otx, oty, ocx, ocy, ocw, och;
    nj_status st = NJ_OK;

    if (!gobj) {
        NJ_THROW(vm, "java/lang/NullPointerException", "graphics is null");
    }
    a = lm_array(vm, self);
    if (!a) {
        return NJ_OK;
    }
    n = lm_count(vm, self);
    vx = fgi(vm, self, NJ_C_LAYERMANAGER, NJ_F_LM_VX, D_INT);
    vy = fgi(vm, self, NJ_C_LAYERMANAGER, NJ_F_LM_VY, D_INT);
    vw = fgi(vm, self, NJ_C_LAYERMANAGER, LM_VIEWW, D_INT);
    vh = fgi(vm, self, NJ_C_LAYERMANAGER, LM_VIEWH, D_INT);

    otx = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT);
    oty = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT);
    ocx = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT);
    ocy = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT);
    ocw = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT);
    och = fgi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT);

    fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT, otx + x - vx);
    fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT, oty + y - vy);

    /* Clip to the view-window rectangle in surface coordinates, keeping the
     * caller's clip as the outer bound.  nj_s8 throughout because the default
     * view window is Integer.MAX_VALUE. */
    {
        nj_s8 x0 = ocx, y0 = ocy;
        nj_s8 x1 = (nj_s8)ocx + ocw, y1 = (nj_s8)ocy + och;
        nj_s8 wx0 = (nj_s8)x + otx, wy0 = (nj_s8)y + oty;
        nj_s8 wx1 = wx0 + vw, wy1 = wy0 + vh;

        if (ocw > 0 && och > 0) {
            x0 = (wx0 > x0) ? wx0 : x0;
            y0 = (wy0 > y0) ? wy0 : y0;
            x1 = (wx1 < x1) ? wx1 : x1;
            y1 = (wy1 < y1) ? wy1 : y1;
        } else {
            /* No usable clip yet: the view window alone bounds the target. */
            x0 = wx0; y0 = wy0; x1 = wx1; y1 = wy1;
        }
        if (x1 < x0) { x1 = x0; }
        if (y1 < y0) { y1 = y0; }
        fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT, (nj_s4)x0);
        fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT, (nj_s4)y0);
        fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT, (nj_s4)(x1 - x0));
        fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT, (nj_s4)(y1 - y0));
    }

    for (i = n - 1; i >= 0; i--) {
        nj_ref layer;
        nj_value gv[1];

        if ((nj_u4)i >= nj_array_length(a)) {
            continue;
        }
        layer = nj_array_get_ref(a, (nj_u4)i);
        if (!layer) {
            continue;
        }
        if (!lay_visible(vm, layer)) {
            continue;   /* invisible layers are never painted */
        }
        /* Completely outside the view window: skipped without a call. */
        if (!rects_overlap(lay_x(vm, layer), lay_y(vm, layer),
                           lay_w(vm, layer), lay_h(vm, layer),
                           (int)vx, (int)vy, (int)vw, (int)vh)) {
            continue;
        }
        gv[0].r = gobj;
        st = nj_call(vm, layer, NJ_C_LAYER, "paint",
                     "(Ljavax/microedition/lcdui/Graphics;)V", gv, NULL);
        if (st != NJ_OK) {
            break;
        }
    }

    /* Restore, even when a layer's paint threw. */
    fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT, otx);
    fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT, oty);
    fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT, ocx);
    fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT, ocy);
    fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT, ocw);
    fsi(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT, och);
    return st;
}

/* ------------------------------------------------------------------ */
/* GameCanvas                                                          */
/* ------------------------------------------------------------------ */
/*
 * The off-screen buffer.  MIDP: "A dedicated buffer is created for each
 * GameCanvas instance ... The buffer is initially filled with white pixels."
 * Canvas owns cBuffer/cWidth/cHeight; GameCanvas only makes sure the buffer
 * exists (the Canvas constructor may have created it already) and that it
 * starts white.
 */
static void gc_fill_white(nj_vm *vm, nj_ref img)
{
    int w = 0, h = 0, i, n;
    nj_u4 *px;

    if (!nj_midp_image_size(vm, img, &w, &h)) {
        return;
    }
    px = nj_midp_image_pixels(vm, img);
    if (!px) {
        return;
    }
    n = w * h;
    for (i = 0; i < n; i++) {
        px[i] = 0xFFFFFFFFu;
    }
}

static nj_ref gc_buffer(nj_vm *vm, nj_ref self, int create)
{
    nj_ref buf = fgr(vm, self, NJ_C_CANVAS, NJ_F_C_BUFFER, D_IMAGE);
    int w, h;
    nj_u4 mark;

    if (buf || !create) {
        return buf;
    }
    w = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_WIDTH, D_INT);
    h = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_HEIGHT, D_INT);
    if (w <= 0) { w = (int)vm->screen_w; }
    if (h <= 0) { h = (int)vm->screen_h; }
    if (w <= 0) { w = NJ_SCREEN_WIDTH; }
    if (h <= 0) { h = NJ_SCREEN_HEIGHT; }

    mark = nj_root_mark(vm);
    buf = nj_root(vm, nj_midp_image_new(vm, w, h, 1));
    if (!buf) {
        nj_root_release(vm, mark);
        nj_clear_exception(vm);
        return NULL;
    }
    gc_fill_white(vm, buf);
    fsr(vm, self, NJ_C_CANVAS, NJ_F_C_BUFFER, D_IMAGE, buf);
    fsi(vm, self, NJ_C_CANVAS, NJ_F_C_WIDTH, D_INT, w);
    fsi(vm, self, NJ_C_CANVAS, NJ_F_C_HEIGHT, D_INT, h);
    nj_root_release(vm, mark);
    return buf;
}

/*
 * GameCanvas(boolean) chains to Canvas.  MIDP 2.0 gives Canvas a
 * (boolean suppressKeyEvents) constructor; the no-arg one is accepted as a
 * fallback so that a partial LCDUI implementation still yields a usable
 * GameCanvas instead of a NoSuchMethodError inside the MIDlet's constructor.
 */
static void gc_chain_super(nj_vm *vm, nj_ref self, int suppress)
{
    nj_class *cv = nj_class_lookup(vm, NJ_C_CANVAS);
    nj_value a1[1];

    if (!cv) {
        return;
    }
    a1[0].i = suppress;
    if (nj_method_lookup(cv, "<init>", "(Z)V")) {
        (void)nj_call_special(vm, self, NJ_C_CANVAS, "<init>", "(Z)V", a1,
                              NULL);
        return;
    }
    if (nj_method_lookup(cv, "<init>", "()V")) {
        (void)nj_call_special(vm, self, NJ_C_CANVAS, "<init>", "()V", NULL,
                              NULL);
    }
}

NJFN(gc_init)
{
    nj_ref self = NJ_THIS(args);
    int suppress = NJ_ARG_INT(args, 0) ? 1 : 0;

    fsi(vm, self, NJ_C_GAMECANVAS, NJ_F_GC_SUPPRESS, D_BOOL, suppress);
    fsi(vm, self, NJ_C_GAMECANVAS, NJ_F_GC_FULL, D_BOOL, 0);
    gc_chain_super(vm, self, suppress);
    if (vm->exception) {
        return NJ_ERR;
    }
    (void)gc_buffer(vm, self, 1);
    return vm->exception ? NJ_ERR : NJ_OK;
}

/*
 * Key state.
 *
 * NJ_F_C_KEYS ("cKeys") is the sticky bitmap the LCDUI key path maintains: a
 * press ORs in its NJ_GAME_* bit, a release clears it, so a press+release
 * between two reads is still seen once.  Reading consumes the latched bits
 * (MIDP: "Calling this method has the side effect of clearing any latched
 * state"), except when the GameCanvas was created with suppressKeyEvents, in
 * which case Canvas keeps delivering the ordinary key events and the latch is
 * left alone for the canvas' own keyPressed/keyReleased handlers.
 *
 * The value RETURNED must use the MIDP bit values, because a MIDlet's compiler
 * inlined them: UP_PRESSED 0x0002, LEFT_PRESSED 0x0004, RIGHT_PRESSED 0x0020,
 * DOWN_PRESSED 0x0040, FIRE_PRESSED 0x0100, GAME_A 0x0200, GAME_B 0x0400,
 * GAME_C 0x0800, GAME_D 0x1000.  nj_midp.h's NJ_GAME_* bits are the internal
 * spelling and differ for everything except UP and LEFT, hence the mapping.
 */
static int gc_midp_bits(int raw)
{
    int out = 0;

    if (raw & NJ_GAME_UP)    { out |= 0x0002; }
    if (raw & NJ_GAME_LEFT)  { out |= 0x0004; }
    if (raw & NJ_GAME_RIGHT) { out |= 0x0020; }
    if (raw & NJ_GAME_DOWN)  { out |= 0x0040; }
    if (raw & NJ_GAME_FIRE)  { out |= 0x0100; }
    if (raw & NJ_GAME_A)     { out |= 0x0200; }
    if (raw & NJ_GAME_B)     { out |= 0x0400; }
    if (raw & NJ_GAME_C)     { out |= 0x0800; }
    if (raw & NJ_GAME_D)     { out |= 0x1000; }
    return out;
}

NJFN(gc_getKeyStates)
{
    nj_ref self = NJ_THIS(args);
    int raw = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT);
    int suppress = (int)fgi(vm, self, NJ_C_GAMECANVAS, NJ_F_GC_SUPPRESS, D_BOOL);

    if (!suppress) {
        fsi(vm, self, NJ_C_CANVAS, NJ_F_C_KEYS, D_INT, 0);
    }
    ret->i = gc_midp_bits(raw);
    return NJ_OK;
}

/*
 * The Graphics returned by getGraphics() is a fresh object every time, as MIDP
 * documents ("A new Graphics object is created and returned each time this
 * method is called").  nj_midp.h exposes no "graphics for a Canvas buffer"
 * helper, so the object is built here from the frozen NJ_C_GRAPHICS / NJ_F_G_*
 * names - the same layout the LCDUI module uses for an Image or a Canvas, so
 * both layers agree.
 */
static nj_ref gc_new_graphics(nj_vm *vm, nj_ref target, int w, int h)
{
    nj_ref g;
    nj_u4 mark = nj_root_mark(vm);

    g = nj_root(vm, nj_object_new(vm, NJ_C_GRAPHICS));
    if (!g) {
        nj_root_release(vm, mark);
        return NULL;
    }
    fsr(vm, g, NJ_C_GRAPHICS, NJ_F_G_TARGET, D_IMAGE, target);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_TX, D_INT, 0);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_TY, D_INT, 0);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_CX, D_INT, 0);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_CY, D_INT, 0);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_CW, D_INT, w);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_CH, D_INT, h);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_COLOR, D_INT, (nj_s4)0xFF000000u);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_BLEND, D_INT, NJ_BLEND_SRC_OVER);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_STROKE, D_INT, NJ_STROKE_SOLID);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_XOR, D_INT, 0);
    fsi(vm, g, NJ_C_GRAPHICS, NJ_F_G_XORCOLOR, D_INT, 0);
    nj_root_release(vm, mark);
    return g;
}

NJFN(gc_getGraphics)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = gc_buffer(vm, self, 1);
    int w = 0, h = 0;

    if (!buf || !nj_midp_image_size(vm, buf, &w, &h)) {
        return NJ_OK;
    }
    ret->r = gc_new_graphics(vm, buf, w, h);
    return ret->r ? NJ_OK : NJ_ERR;
}

/*
 * Copy a rectangle of the off-screen buffer to a surface.
 *
 * Two paths: when both sides have the same 4-byte ARGB layout a row copy is
 * exact and fastest; otherwise the rasterizer converts (a device panel is
 * usually RGB565).  The copy is opaque - a flush puts the buffer's pixels on
 * the display, transparent ones included, like the real thing.
 */
static void gc_copy_region(nj_surface *dst, nj_surface *src,
                           int dx, int dy, int sx, int sy, int w, int h,
                           int cx, int cy, int cw, int ch)
{
    int x0, y0, x1, y1, y;

    if (!dst || !src || w <= 0 || h <= 0 || cw <= 0 || ch <= 0) {
        return;
    }
    x0 = imax(imax(dx, cx), 0);
    y0 = imax(imax(dy, cy), 0);
    x1 = imin(imin(dx + w, cx + cw), dst->width);
    y1 = imin(imin(dy + h, cy + ch), dst->height);
    x1 = imin(x1, src->width + (dx - sx));
    y1 = imin(y1, src->height + (dy - sy));
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    for (y = y0; y < y1; y++) {
        const nj_u1 *sp = (const nj_u1 *)src->pixels +
                          (nj_size)(sy + (y - dy)) * (nj_size)src->stride +
                          (nj_size)(sx + (x0 - dx)) * (nj_size)src->bpp;
        nj_u1 *dp = (nj_u1 *)dst->pixels + (nj_size)y * (nj_size)dst->stride +
                    (nj_size)x0 * (nj_size)dst->bpp;
        memcpy(dp, sp, (nj_size)(x1 - x0) * (nj_size)src->bpp);
    }
}

static void gc_blit(nj_vm *vm, nj_ref buf, nj_surface *dst,
                    int dx, int dy, int sx, int sy, int w, int h,
                    int cx, int cy, int cw, int ch)
{
    nj_surface sbuf;
    nj_surface *src;

    if (!dst || !buf || w <= 0 || h <= 0 || cw <= 0 || ch <= 0) {
        return;
    }
    src = nj_midp_surface(vm, buf, &sbuf);
    if (!src || src == dst) {
        return;   /* nothing to do, or painting the buffer into itself */
    }
    /* The source region must stay inside the buffer. */
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > src->width) { w = src->width - sx; }
    if (sy + h > src->height) { h = src->height - sy; }
    if (w <= 0 || h <= 0) {
        return;
    }

    if (src->format == dst->format && src->bpp == dst->bpp &&
        src->format == NJ_PIXFMT_ARGB8888) {
        gc_copy_region(dst, src, dx, dy, sx, sy, w, h, cx, cy, cw, ch);
        return;
    }
    /* Format conversion (and any exotic stride): let the rasterizer do it. */
    {
        nj_gfx g;
        nj_image view;

        nj_gfx_init(&g, dst);
        nj_gfx_set_clip(&g, cx, cy, cw, ch);
        if (!image_view(vm, buf, sx, sy, w, h, &view)) {
            return;
        }
        nj_gfx_draw_image_rop(&g, &view, dx, dy, sx, sy, w, h, NJ_ROP_SET,
                              NJ_TRANS_NONE);
    }
}

/* Shared by flushGraphics() and paint(): present a region of the buffer. */
static void gc_flush_region(nj_vm *vm, nj_ref self, nj_ref gobj,
                            int x, int y, int w, int h)
{
    nj_ref buf = gc_buffer(vm, self, 1);
    nj_surface *dst;
    int req_w = w, req_h = h, canvas_w, canvas_h;

    if (!buf) {
        return;
    }
    dst = nj_midp_screen(vm);
    if (!dst) {
        return;
    }
    canvas_w = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_WIDTH, D_INT);
    canvas_h = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_HEIGHT, D_INT);
    if (canvas_w <= 0) { canvas_w = dst->width; }
    if (canvas_h <= 0) { canvas_h = dst->height; }
    /* MIDP: no pixels when the requested width/height is < 1, and only the
     * part of the request that intersects the GameCanvas' current bounds. */
    if (req_w < 1 || req_h < 1) {
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > canvas_w) { w = canvas_w - x; }
    if (y + h > canvas_h) { h = canvas_h - y; }
    if (w < 1 || h < 1) {
        return;
    }
    if (gobj) {
        /* paint(Graphics): honour the caller's translate and clip. */
        njg_gfx_bind b;
        nj_gfx g;

        if (!njg_bind(vm, gobj, &b, &g)) {
            return;
        }
        if (fgr(vm, gobj, NJ_C_GRAPHICS, NJ_F_G_TARGET, D_IMAGE) == buf) {
            return;   /* the system handed us the buffer itself */
        }
        gc_blit(vm, buf, g.surf, x + g.translate_x, y + g.translate_y,
                x, y, w, h, g.clip_x, g.clip_y, g.clip_w, g.clip_h);
    } else {
        gc_blit(vm, buf, dst, x, y, x, y, w, h, 0, 0, dst->width,
                dst->height);
    }
    vm->repaint_pending = 1u;
}

NJFN(gc_flushGraphics)
{
    nj_ref self = NJ_THIS(args);
    int w = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_WIDTH, D_INT);
    int h = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_HEIGHT, D_INT);

    if (w <= 0 || h <= 0) {
        nj_ref buf = gc_buffer(vm, self, 1);
        int bw = 0, bh = 0;
        if (buf && nj_midp_image_size(vm, buf, &bw, &bh)) {
            w = bw;
            h = bh;
        }
    }
    gc_flush_region(vm, self, NULL, 0, 0, w, h);
    return NJ_OK;
}

NJFN(gc_flushGraphics_rect)
{
    gc_flush_region(vm, NJ_THIS(args), NULL, NJ_ARG_INT(args, 0),
                    NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2),
                    NJ_ARG_INT(args, 3));
    return NJ_OK;
}

/*
 * paint(Graphics).
 *
 * MIDP 2.0 documents this as "renders the off-screen buffer at (0,0)", subject
 * to the clip region and origin translation of the Graphics, and GameCanvas
 * declares it final so a MIDlet cannot replace it.  It is therefore NOT empty:
 * the system calls it whenever the screen has to be repainted, and a no-op
 * would leave the display blank until the game's own flushGraphics() happened
 * to run again.  flushGraphics() and this method share one code path, so the
 * two can never disagree about what is on screen.
 */
NJFN(gc_paint)
{
    nj_ref self = NJ_THIS(args);
    nj_ref gobj = NJ_ARG_REF(args, 0);
    int w = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_WIDTH, D_INT);
    int h = (int)fgi(vm, self, NJ_C_CANVAS, NJ_F_C_HEIGHT, D_INT);

    if (!gobj) {
        NJ_THROW(vm, "java/lang/NullPointerException", "graphics is null");
    }
    if (w <= 0 || h <= 0) {
        nj_ref buf = gc_buffer(vm, self, 1);
        int bw = 0, bh = 0;
        if (buf && nj_midp_image_size(vm, buf, &bw, &bh)) {
            w = bw;
            h = bh;
        }
    }
    gc_flush_region(vm, self, gobj, 0, 0, w, h);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                             */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define F(name, desc, flags) { name, desc, flags }

/* ---- Layer ---- */
static const nj_builtin_field layer_fields[] = {
    F(NJ_F_L_X, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_L_Y, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_L_W, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_L_H, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_L_VISIBLE, D_BOOL, NJ_ACC_PRIVATE)
};

static const nj_builtin_method layer_methods[] = {
    M("<init>", "()V", NJ_ACC_PROTECTED),
    M("getX", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("getY", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("getWidth", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("getHeight", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("setPosition", "(II)V", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("move", "(II)V", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("isVisible", "()Z", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("setVisible", "(Z)V", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    /*
     * Abstract, deliberately without an implementation: a Layer subclass that
     * forgets to override paint() must raise AbstractMethodError, not silently
     * draw nothing.  A silent no-op is the hardest kind of bug to find in a
     * game, and the VM's invoker already reports the error for a method flagged
     * abstract that nothing overrides.
     */
    M("paint", "(Ljavax/microedition/lcdui/Graphics;)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

/* ---- Sprite ---- */
static const nj_builtin_field sprite_fields[] = {
    F(NJ_F_SP_IMAGE, D_IMAGE, NJ_ACC_PRIVATE),
    F(NJ_F_SP_FRAMEW, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_SP_FRAMEH, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_SP_FRAMES, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_SP_COLS, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_SP_FRAME, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_SP_TRANS, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_SP_REFX, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_SP_REFY, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_SP_COLLIDE, D_BOOL, NJ_ACC_PRIVATE),
    /* Additive state (see the header comment). */
    F(SP_REFDX, D_INT, NJ_ACC_PRIVATE),
    F(SP_REFDY, D_INT, NJ_ACC_PRIVATE),
    F(SP_SEQ, D_INTARR, NJ_ACC_PRIVATE),
    F(SP_SEQIDX, D_INT, NJ_ACC_PRIVATE),
    F(SP_COLX, D_INT, NJ_ACC_PRIVATE),
    F(SP_COLY, D_INT, NJ_ACC_PRIVATE),
    F(SP_COLW, D_INT, NJ_ACC_PRIVATE),
    F(SP_COLH, D_INT, NJ_ACC_PRIVATE)
};

static const nj_builtin_method sprite_methods[] = {
    M("<init>", "(Ljavax/microedition/lcdui/Image;)V", NJ_ACC_PUBLIC),
    M("<init>", "(Ljavax/microedition/lcdui/Image;II)V", NJ_ACC_PUBLIC),
    M("<init>", "(Ljavax/microedition/lcdui/game/Sprite;)V", NJ_ACC_PUBLIC),
    M("setImage", "(Ljavax/microedition/lcdui/Image;II)V", NJ_ACC_PUBLIC),
    M("setImage", "(Ljavax/microedition/lcdui/Image;)V", NJ_ACC_PUBLIC),
    M("getImage", "()Ljavax/microedition/lcdui/Image;", NJ_ACC_PUBLIC),
    M("setFrame", "(I)V", NJ_ACC_PUBLIC),
    M("getFrame", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("getRawFrameCount", "()I", NJ_ACC_PUBLIC),
    M("setFrameSequence", "([I)V", NJ_ACC_PUBLIC),
    M("getFrameSequenceLength", "()I", NJ_ACC_PUBLIC),
    M("getFrameSequenceIndex", "()I", NJ_ACC_PUBLIC),
    M("nextFrame", "()V", NJ_ACC_PUBLIC),
    M("prevFrame", "()V", NJ_ACC_PUBLIC),
    M("setTransform", "(I)V", NJ_ACC_PUBLIC),
    M("getTransform", "()I", NJ_ACC_PUBLIC),
    M("defineReferencePixel", "(II)V", NJ_ACC_PUBLIC),
    M("setRefPixelPosition", "(II)V", NJ_ACC_PUBLIC),
    M("getRefPixelX", "()I", NJ_ACC_PUBLIC),
    M("getRefPixelY", "()I", NJ_ACC_PUBLIC),
    M("defineCollisionRectangle", "(IIII)V", NJ_ACC_PUBLIC),
    M("paint", "(Ljavax/microedition/lcdui/Graphics;)V",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("collidesWith", "(Ljavax/microedition/lcdui/game/Sprite;Z)Z",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("collidesWith", "(Ljavax/microedition/lcdui/game/Sprite;ZZ)Z",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("collidesWith", "(Ljavax/microedition/lcdui/game/TiledLayer;Z)Z",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("collidesWith", "(Ljavax/microedition/lcdui/Image;IIZ)Z",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("collidesWith", "(Ljavax/microedition/lcdui/game/Layer;Z)Z",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL)
};

/* ---- TiledLayer ---- */
static const nj_builtin_field tiled_fields[] = {
    F(NJ_F_TL_IMAGE, D_IMAGE, NJ_ACC_PRIVATE),
    F(NJ_F_TL_COLS, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_TL_ROWS, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_TL_CELLW, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_TL_CELLH, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_TL_TILEW, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_TL_TILEH, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_TL_CELLS, D_INTARR, NJ_ACC_PRIVATE),
    F(NJ_F_TL_ANIM, D_INTARR, NJ_ACC_PRIVATE),
    F(NJ_F_TL_ANIMN, D_INT, NJ_ACC_PRIVATE),
    F(TL_ANIMCAP, D_INT, NJ_ACC_PRIVATE)
};

static const nj_builtin_method tiled_methods[] = {
    M("<init>", "(IILjavax/microedition/lcdui/Image;II)V", NJ_ACC_PUBLIC),
    M("setStaticTileSet", "(Ljavax/microedition/lcdui/Image;II)V",
      NJ_ACC_PUBLIC),
    M("setCell", "(III)V", NJ_ACC_PUBLIC),
    M("getCell", "(II)I", NJ_ACC_PUBLIC),
    M("createAnimatedTile", "(I)I", NJ_ACC_PUBLIC),
    M("setAnimatedTile", "(II)V", NJ_ACC_PUBLIC),
    M("getAnimatedTile", "(I)I", NJ_ACC_PUBLIC),
    M("fillCells", "(IIIII)V", NJ_ACC_PUBLIC),
    M("fillCells", "(IIII)V", NJ_ACC_PUBLIC),
    M("getCellWidth", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("getCellHeight", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("getColumns", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("getRows", "()I", NJ_ACC_PUBLIC | NJ_ACC_FINAL),
    M("paint", "(Ljavax/microedition/lcdui/Graphics;)V",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL)
};

/* ---- LayerManager ---- */
static const nj_builtin_field lm_fields[] = {
    F(NJ_F_LM_LAYERS, D_OBJARR, NJ_ACC_PRIVATE),
    F(LM_COUNT, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_LM_VX, D_INT, NJ_ACC_PRIVATE),
    F(NJ_F_LM_VY, D_INT, NJ_ACC_PRIVATE),
    F(LM_VIEWW, D_INT, NJ_ACC_PRIVATE),
    F(LM_VIEWH, D_INT, NJ_ACC_PRIVATE)
};

static const nj_builtin_method lm_methods[] = {
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("append", "(Ljavax/microedition/lcdui/game/Layer;)V", NJ_ACC_PUBLIC),
    M("insert", "(Ljavax/microedition/lcdui/game/Layer;I)V", NJ_ACC_PUBLIC),
    M("remove", "(Ljavax/microedition/lcdui/game/Layer;)V", NJ_ACC_PUBLIC),
    M("getLayerAt", "(I)Ljavax/microedition/lcdui/game/Layer;", NJ_ACC_PUBLIC),
    M("getSize", "()I", NJ_ACC_PUBLIC),
    M("setViewWindow", "(IIII)V", NJ_ACC_PUBLIC),
    M("getViewX", "()I", NJ_ACC_PUBLIC),
    M("getViewY", "()I", NJ_ACC_PUBLIC),
    M("getViewW", "()I", NJ_ACC_PUBLIC),
    M("getViewH", "()I", NJ_ACC_PUBLIC),
    M("paint", "(Ljavax/microedition/lcdui/Graphics;II)V", NJ_ACC_PUBLIC)
};

/* ---- GameCanvas ---- */
static const nj_builtin_field gc_fields[] = {
    F(NJ_F_GC_SUPPRESS, D_BOOL, NJ_ACC_PRIVATE),
    /* Reserved for the LCDUI module's full-screen handling of a game canvas;
     * this module neither reads nor writes it. */
    F(NJ_F_GC_FULL, D_BOOL, NJ_ACC_PRIVATE)
};

static const nj_builtin_method gc_methods[] = {
    M("<init>", "(Z)V", NJ_ACC_PROTECTED),
    M("getKeyStates", "()I", NJ_ACC_PUBLIC),
    M("flushGraphics", "()V", NJ_ACC_PUBLIC),
    M("flushGraphics", "(IIII)V", NJ_ACC_PUBLIC),
    M("getGraphics", "()Ljavax/microedition/lcdui/Graphics;", NJ_ACC_PROTECTED),
    M("paint", "(Ljavax/microedition/lcdui/Graphics;)V",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL)
};

const nj_builtin_class nj_lib_game_classes[] = {
    { NJ_C_LAYER, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      layer_fields,
      (nj_u2)(sizeof(layer_fields) / sizeof(layer_fields[0])),
      layer_methods,
      (nj_u2)(sizeof(layer_methods) / sizeof(layer_methods[0])) },
    { NJ_C_SPRITE, NJ_C_LAYER, NJ_ACC_PUBLIC,
      sprite_fields,
      (nj_u2)(sizeof(sprite_fields) / sizeof(sprite_fields[0])),
      sprite_methods,
      (nj_u2)(sizeof(sprite_methods) / sizeof(sprite_methods[0])) },
    { NJ_C_TILEDLAYER, NJ_C_LAYER, NJ_ACC_PUBLIC,
      tiled_fields,
      (nj_u2)(sizeof(tiled_fields) / sizeof(tiled_fields[0])),
      tiled_methods,
      (nj_u2)(sizeof(tiled_methods) / sizeof(tiled_methods[0])) },
    { NJ_C_LAYERMANAGER, "java/lang/Object", NJ_ACC_PUBLIC,
      lm_fields,
      (nj_u2)(sizeof(lm_fields) / sizeof(lm_fields[0])),
      lm_methods,
      (nj_u2)(sizeof(lm_methods) / sizeof(lm_methods[0])) },
    { NJ_C_GAMECANVAS, NJ_C_CANVAS, NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      gc_fields,
      (nj_u2)(sizeof(gc_fields) / sizeof(gc_fields[0])),
      gc_methods,
      (nj_u2)(sizeof(gc_methods) / sizeof(gc_methods[0])) }
};
const int nj_lib_game_class_count =
    (int)(sizeof(nj_lib_game_classes) / sizeof(nj_lib_game_classes[0]));

const nj_native_entry nj_lib_game_table[] = {
    /* Layer */
    { NJ_C_LAYER, "<init>", "()V", layer_init },
    { NJ_C_LAYER, "getX", "()I", layer_getX },
    { NJ_C_LAYER, "getY", "()I", layer_getY },
    { NJ_C_LAYER, "getWidth", "()I", layer_getWidth },
    { NJ_C_LAYER, "getHeight", "()I", layer_getHeight },
    { NJ_C_LAYER, "setPosition", "(II)V", layer_setPosition },
    { NJ_C_LAYER, "move", "(II)V", layer_move },
    { NJ_C_LAYER, "isVisible", "()Z", layer_isVisible },
    { NJ_C_LAYER, "setVisible", "(Z)V", layer_setVisible },
    /* Sprite */
    { NJ_C_SPRITE, "<init>", "(Ljavax/microedition/lcdui/Image;)V",
      sprite_init_image },
    { NJ_C_SPRITE, "<init>", "(Ljavax/microedition/lcdui/Image;II)V",
      sprite_init_frames },
    { NJ_C_SPRITE, "<init>", "(Ljavax/microedition/lcdui/game/Sprite;)V",
      sprite_init_copy },
    { NJ_C_SPRITE, "setImage", "(Ljavax/microedition/lcdui/Image;II)V",
      sprite_setImage },
    { NJ_C_SPRITE, "setImage", "(Ljavax/microedition/lcdui/Image;)V",
      sprite_setImage_simple },
    { NJ_C_SPRITE, "getImage", "()Ljavax/microedition/lcdui/Image;",
      sprite_getImage },
    { NJ_C_SPRITE, "setFrame", "(I)V", sprite_setFrame },
    { NJ_C_SPRITE, "getFrame", "()I", sprite_getFrame },
    { NJ_C_SPRITE, "getRawFrameCount", "()I", sprite_getRawFrameCount },
    { NJ_C_SPRITE, "setFrameSequence", "([I)V", sprite_setFrameSequence },
    { NJ_C_SPRITE, "getFrameSequenceLength", "()I",
      sprite_getFrameSequenceLength },
    { NJ_C_SPRITE, "getFrameSequenceIndex", "()I",
      sprite_getFrameSequenceIndex },
    { NJ_C_SPRITE, "nextFrame", "()V", sprite_nextFrame },
    { NJ_C_SPRITE, "prevFrame", "()V", sprite_prevFrame },
    { NJ_C_SPRITE, "setTransform", "(I)V", sprite_setTransform },
    { NJ_C_SPRITE, "getTransform", "()I", sprite_getTransform },
    { NJ_C_SPRITE, "defineReferencePixel", "(II)V",
      sprite_defineReferencePixel },
    { NJ_C_SPRITE, "setRefPixelPosition", "(II)V", sprite_setRefPixelPosition },
    { NJ_C_SPRITE, "getRefPixelX", "()I", sprite_getRefPixelX },
    { NJ_C_SPRITE, "getRefPixelY", "()I", sprite_getRefPixelY },
    { NJ_C_SPRITE, "defineCollisionRectangle", "(IIII)V",
      sprite_defineCollisionRectangle },
    { NJ_C_SPRITE, "paint", "(Ljavax/microedition/lcdui/Graphics;)V",
      sprite_paint },
    { NJ_C_SPRITE, "collidesWith",
      "(Ljavax/microedition/lcdui/game/Sprite;Z)Z", sprite_collidesWith_sprite },
    { NJ_C_SPRITE, "collidesWith",
      "(Ljavax/microedition/lcdui/game/Sprite;ZZ)Z",
      sprite_collidesWith_sprite_ref },
    { NJ_C_SPRITE, "collidesWith",
      "(Ljavax/microedition/lcdui/game/TiledLayer;Z)Z",
      sprite_collidesWith_tiled },
    { NJ_C_SPRITE, "collidesWith",
      "(Ljavax/microedition/lcdui/Image;IIZ)Z", sprite_collidesWith_image },
    { NJ_C_SPRITE, "collidesWith",
      "(Ljavax/microedition/lcdui/game/Layer;Z)Z", sprite_collidesWith_layer },
    /* TiledLayer */
    { NJ_C_TILEDLAYER, "<init>", "(IILjavax/microedition/lcdui/Image;II)V",
      tiled_init },
    { NJ_C_TILEDLAYER, "setStaticTileSet",
      "(Ljavax/microedition/lcdui/Image;II)V", tiled_setStaticTileSet },
    { NJ_C_TILEDLAYER, "setCell", "(III)V", tiled_setCell },
    { NJ_C_TILEDLAYER, "getCell", "(II)I", tiled_getCell },
    { NJ_C_TILEDLAYER, "createAnimatedTile", "(I)I", tiled_createAnimatedTile },
    { NJ_C_TILEDLAYER, "setAnimatedTile", "(II)V", tiled_setAnimatedTile },
    { NJ_C_TILEDLAYER, "getAnimatedTile", "(I)I", tiled_getAnimatedTile },
    { NJ_C_TILEDLAYER, "fillCells", "(IIIII)V", tiled_fillCells },
    { NJ_C_TILEDLAYER, "fillCells", "(IIII)V", tiled_fillCells_empty },
    { NJ_C_TILEDLAYER, "getCellWidth", "()I", tiled_getCellWidth },
    { NJ_C_TILEDLAYER, "getCellHeight", "()I", tiled_getCellHeight },
    { NJ_C_TILEDLAYER, "getColumns", "()I", tiled_getColumns },
    { NJ_C_TILEDLAYER, "getRows", "()I", tiled_getRows },
    { NJ_C_TILEDLAYER, "paint", "(Ljavax/microedition/lcdui/Graphics;)V",
      tiled_paint },
    /* LayerManager */
    { NJ_C_LAYERMANAGER, "<init>", "()V", lm_init },
    { NJ_C_LAYERMANAGER, "append", "(Ljavax/microedition/lcdui/game/Layer;)V",
      lm_append },
    { NJ_C_LAYERMANAGER, "insert",
      "(Ljavax/microedition/lcdui/game/Layer;I)V", lm_insert },
    { NJ_C_LAYERMANAGER, "remove", "(Ljavax/microedition/lcdui/game/Layer;)V",
      lm_remove },
    { NJ_C_LAYERMANAGER, "getLayerAt",
      "(I)Ljavax/microedition/lcdui/game/Layer;", lm_getLayerAt },
    { NJ_C_LAYERMANAGER, "getSize", "()I", lm_getSize },
    { NJ_C_LAYERMANAGER, "setViewWindow", "(IIII)V", lm_setViewWindow },
    { NJ_C_LAYERMANAGER, "getViewX", "()I", lm_getViewX },
    { NJ_C_LAYERMANAGER, "getViewY", "()I", lm_getViewY },
    { NJ_C_LAYERMANAGER, "getViewW", "()I", lm_getViewW },
    { NJ_C_LAYERMANAGER, "getViewH", "()I", lm_getViewH },
    { NJ_C_LAYERMANAGER, "paint",
      "(Ljavax/microedition/lcdui/Graphics;II)V", lm_paint },
    /* GameCanvas */
    { NJ_C_GAMECANVAS, "<init>", "(Z)V", gc_init },
    { NJ_C_GAMECANVAS, "getKeyStates", "()I", gc_getKeyStates },
    { NJ_C_GAMECANVAS, "flushGraphics", "()V", gc_flushGraphics },
    { NJ_C_GAMECANVAS, "flushGraphics", "(IIII)V", gc_flushGraphics_rect },
    { NJ_C_GAMECANVAS, "getGraphics", "()Ljavax/microedition/lcdui/Graphics;",
      gc_getGraphics },
    { NJ_C_GAMECANVAS, "paint", "(Ljavax/microedition/lcdui/Graphics;)V",
      gc_paint }
};
const int nj_lib_game_count =
    (int)(sizeof(nj_lib_game_table) / sizeof(nj_lib_game_table[0]));
