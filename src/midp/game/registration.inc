/* Static registration for javax.microedition.lcdui.game. Each concrete method
 * list supplies both the Java declaration and the native dispatch row. */
#define M(name, desc, flags, fn) { name, desc, flags }
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
#include "layer_methods.inc"
    /* Layer subclasses must override paint or raise AbstractMethodError. */
    M("paint", "(Ljavax/microedition/lcdui/Graphics;)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT, NULL)
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
#include "sprite_methods.inc"
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
#include "tiled_methods.inc"
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
#include "lm_methods.inc"
};

/* ---- GameCanvas ---- */
static const nj_builtin_field gc_fields[] = {
    F(NJ_F_GC_SUPPRESS, D_BOOL, NJ_ACC_PRIVATE),
    /* Reserved for the LCDUI module's full-screen handling of a game canvas;
     * this module neither reads nor writes it. */
    F(NJ_F_GC_FULL, D_BOOL, NJ_ACC_PRIVATE)
};

static const nj_builtin_method gc_methods[] = {
#include "gc_methods.inc"
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

#undef F
#undef M

#define M(name, desc, flags, fn) { GAME_OWNER, name, desc, fn }
const nj_native_entry nj_lib_game_table[] = {
#define GAME_OWNER NJ_C_LAYER
#include "layer_methods.inc"
#undef GAME_OWNER
#define GAME_OWNER NJ_C_SPRITE
#include "sprite_methods.inc"
#undef GAME_OWNER
#define GAME_OWNER NJ_C_TILEDLAYER
#include "tiled_methods.inc"
#undef GAME_OWNER
#define GAME_OWNER NJ_C_LAYERMANAGER
#include "lm_methods.inc"
#undef GAME_OWNER
#define GAME_OWNER NJ_C_GAMECANVAS
#include "gc_methods.inc"
#undef GAME_OWNER
};
#undef M
const int nj_lib_game_count =
    (int)(sizeof(nj_lib_game_table) / sizeof(nj_lib_game_table[0]));
