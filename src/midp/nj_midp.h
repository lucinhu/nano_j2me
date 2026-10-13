/*
 * nano_j2me - nj_midp.h
 *
 * INTERNAL CONTRACT between the MIDP class-library modules.
 *
 * The whole MIDP layer is written in C, so "objects" are ordinary Java objects
 * with fields declared in the builtin class tables.  This header fixes three
 * things so the modules can be written independently:
 *
 *   1. WHERE STATE LIVES.  All MIDP state is held in Java fields, never in C
 *      globals and never in side tables.  An Image *is* an int[] of ARGB
 *      pixels plus its dimensions; a Graphics is a set of transform fields.
 *      That keeps the GC in charge of lifetime and keeps the runtime free of
 *      mutable global state.
 *
 *   2. THE FIELD NAMES.  Because a module looks another module's objects up by
 *      name, the names below are part of the ABI between modules.  Change one
 *      here and every module follows.
 *
 *   3. THE DRAWING PATH.  Everything renders through nj_gfx, which works on a
 *      plain linear buffer; nj_midp_surface() hands the rasterizer a view of
 *      an Image's pixels without copying.
 */
#ifndef NJ_MIDP_H
#define NJ_MIDP_H

#include "nj_internal.h"

/* ================================================================== */
/* 1. Class names (slashed)                                            */
/* ================================================================== */
#define NJ_C_DISPLAY        "javax/microedition/lcdui/Display"
#define NJ_C_DISPLAYABLE    "javax/microedition/lcdui/Displayable"
#define NJ_C_CANVAS         "javax/microedition/lcdui/Canvas"
#define NJ_C_GAMECANVAS     "javax/microedition/lcdui/game/GameCanvas"
#define NJ_C_GRAPHICS       "javax/microedition/lcdui/Graphics"
#define NJ_C_IMAGE          "javax/microedition/lcdui/Image"
#define NJ_C_FONT           "javax/microedition/lcdui/Font"
#define NJ_C_COMMAND        "javax/microedition/lcdui/Command"
#define NJ_C_TICKER         "javax/microedition/lcdui/Ticker"
#define NJ_C_ALERT          "javax/microedition/lcdui/Alert"
#define NJ_C_ALERTTYPE      "javax/microedition/lcdui/AlertType"
#define NJ_C_FORM           "javax/microedition/lcdui/Form"
#define NJ_C_LIST           "javax/microedition/lcdui/List"
#define NJ_C_TEXTBOX        "javax/microedition/lcdui/TextBox"
#define NJ_C_ITEM           "javax/microedition/lcdui/Item"
#define NJ_C_STRINGITEM     "javax/microedition/lcdui/StringItem"
#define NJ_C_IMAGEITEM      "javax/microedition/lcdui/ImageItem"
#define NJ_C_GAUGE          "javax/microedition/lcdui/Gauge"
#define NJ_C_CHOICEGROUP    "javax/microedition/lcdui/ChoiceGroup"
#define NJ_C_TEXTFIELD      "javax/microedition/lcdui/TextField"
#define NJ_C_DATEFIELD      "javax/microedition/lcdui/DateField"
#define NJ_C_SPACER         "javax/microedition/lcdui/Spacer"
#define NJ_C_COMMANDLISTENER "javax/microedition/lcdui/CommandListener"
#define NJ_C_ITEMCOMMANDLISTENER "javax/microedition/lcdui/ItemCommandListener"
#define NJ_C_ITEMSTATELISTENER   "javax/microedition/lcdui/ItemStateListener"
#define NJ_C_MIDLET         "javax/microedition/midlet/MIDlet"
#define NJ_C_RECORDSTORE    "javax/microedition/rms/RecordStore"
#define NJ_C_PLAYER         "javax/microedition/media/Player"
#define NJ_C_MANAGER        "javax/microedition/media/Manager"
#define NJ_C_SPRITE         "javax/microedition/lcdui/game/Sprite"
#define NJ_C_TILEDLAYER     "javax/microedition/lcdui/game/TiledLayer"
#define NJ_C_LAYER          "javax/microedition/lcdui/game/Layer"
#define NJ_C_LAYERMANAGER   "javax/microedition/lcdui/game/LayerManager"

/* ================================================================== */
/* 2. Field names - part of the ABI between MIDP modules               */
/* ================================================================== */

/* Image: pixels are ARGB8888 in a Java int[], so the GC owns them. */
#define NJ_F_IMG_WIDTH   "imgWidth"    /* I  */
#define NJ_F_IMG_HEIGHT  "imgHeight"   /* I  */
#define NJ_F_IMG_PIXELS  "imgPixels"   /* [I */
#define NJ_F_IMG_MUTABLE "imgMutable"  /* Z  - created via createImage(w,h) */

/* Graphics */
#define NJ_F_G_TARGET    "gTarget"     /* Image or null => the display buffer */
#define NJ_F_G_TX        "gTx"         /* I */
#define NJ_F_G_TY        "gTy"         /* I */
#define NJ_F_G_CX        "gClipX"      /* I */
#define NJ_F_G_CY        "gClipY"      /* I */
#define NJ_F_G_CW        "gClipW"      /* I */
#define NJ_F_G_CH        "gClipH"      /* I */
#define NJ_F_G_COLOR     "gColor"      /* I  ARGB */
#define NJ_F_G_BLEND     "gBlend"      /* I  NJ_BLEND_* */
#define NJ_F_G_STROKE    "gStroke"     /* I  NJ_STROKE_* */
#define NJ_F_G_FONT      "gFont"       /* Font */
#define NJ_F_G_XOR       "gXor"        /* I  non-zero => XOR mode active */
#define NJ_F_G_XORCOLOR  "gXorColor"   /* I */

/* Font */
#define NJ_F_FONT_FACE   "fontFace"    /* I */
#define NJ_F_FONT_STYLE  "fontStyle"   /* I */
#define NJ_F_FONT_SIZE   "fontSize"    /* I */

/* Displayable */
#define NJ_F_D_TITLE     "dTitle"      /* String   */
#define NJ_F_D_TICKER    "dTicker"     /* Ticker   */
#define NJ_F_D_LISTENER  "dListener"   /* CommandListener */
#define NJ_F_D_COMMANDS  "dCommands"   /* Vector   */
#define NJ_F_D_FULLSCRN  "dFullScreen" /* Z        */

/* Canvas */
#define NJ_F_C_BUFFER    "cBuffer"     /* Image backing store */
#define NJ_F_C_WIDTH     "cWidth"      /* I */
#define NJ_F_C_HEIGHT    "cHeight"     /* I */
#define NJ_F_C_KEYS      "cKeys"       /* I  sticky key state bitmap */
#define NJ_F_C_VISIBLE   "cVisible"    /* Z  showNotify/drawNotify delivered */
#define NJ_F_C_PRESSED   "cPressed"    /* Z  a key is currently down */

/* GameCanvas */
#define NJ_F_GC_SUPPRESS "gcSuppress"  /* Z  suppressKeyEvents */
#define NJ_F_GC_FULL    "gcFullScreen" /* Z */

/* Layer */
#define NJ_F_L_X         "lX"          /* I */
#define NJ_F_L_Y         "lY"          /* I */
#define NJ_F_L_W         "lW"          /* I */
#define NJ_F_L_H         "lH"          /* I */
#define NJ_F_L_VISIBLE   "lVisible"    /* Z */

/* Sprite: an Image plus a frame grid. */
#define NJ_F_SP_IMAGE    "spImage"     /* Image */
#define NJ_F_SP_FRAMEW   "spFrameW"    /* I */
#define NJ_F_SP_FRAMEH   "spFrameH"    /* I */
#define NJ_F_SP_FRAMES   "spFrames"    /* I  total frames */
#define NJ_F_SP_COLS     "spCols"      /* I  frames per row in the source */
#define NJ_F_SP_FRAME    "spFrame"     /* I  current frame index */
#define NJ_F_SP_TRANS    "spTransform" /* I  NJ_TRANS_* */
#define NJ_F_SP_REFX     "spRefX"      /* I  reference pixel (rotated space) */
#define NJ_F_SP_REFY     "spRefY"      /* I */
#define NJ_F_SP_COLLIDE  "spCollide"   /* Z  collisions enabled */

/* TiledLayer */
#define NJ_F_TL_IMAGE    "tlImage"     /* Image */
#define NJ_F_TL_COLS     "tlCols"      /* I */
#define NJ_F_TL_ROWS     "tlRows"      /* I */
#define NJ_F_TL_CELLW    "tlCellW"     /* I */
#define NJ_F_TL_CELLH    "tlCellH"     /* I */
#define NJ_F_TL_TILEW    "tlTileW"     /* I  source tile width in the image */
#define NJ_F_TL_TILEH    "tlTileH"     /* I */
#define NJ_F_TL_CELLS    "tlCells"     /* [I  cell -> tile index (0 = empty) */
#define NJ_F_TL_ANIM     "tlAnim"      /* [I  animated tile -> static tile */
#define NJ_F_TL_ANIMN    "tlAnimCount" /* I  number of animated tiles */

/* LayerManager */
#define NJ_F_LM_LAYERS   "lmLayers"    /* Vector of Layer */
#define NJ_F_LM_VX       "lmViewX"     /* I */
#define NJ_F_LM_VY       "lmViewY"     /* I */

/* Display */
#define NJ_F_DISP_CUR    "dpCurrent"   /* Displayable (instance field of the singleton) */
#define NJ_F_DISP_ALERT  "dpAlert"     /* Displayable to restore after an Alert */

/* Alert */
#define NJ_F_AL_TIMEOUT  "alTimeout"   /* I  milliseconds, -1 = forever */
#define NJ_F_AL_TYPE     "alType"      /* AlertType */
#define NJ_F_AL_STRING   "alString"    /* String */
#define NJ_F_AL_IMAGE    "alImage"     /* Image */
#define NJ_F_AL_INDEF    "alIndefinite"/* Z */

/* RecordStore: two parallel java.util.Vectors, so insert and delete are O(1)
 * and the record ids can have gaps exactly as the API allows. */
#define NJ_F_RS_NAME     "rsName"      /* String */
#define NJ_F_RS_RECORDS  "rsRecords"   /* Vector of byte[] */
#define NJ_F_RS_IDS      "rsIds"       /* Vector of java/lang/Integer */
#define NJ_F_RS_NEXTID   "rsNextId"    /* I */
#define NJ_F_RS_OPEN     "rsOpen"      /* Z */
#define NJ_F_RS_MODIFIED "rsModified"  /* Z */

/* MIDlet */
#define NJ_F_M_PROPERTIES "mProps"     /* Hashtable */
#define NJ_F_M_STATE      "mState"     /* I  NJ_MIDLET_* */

/* Player (tone only) */
#define NJ_F_P_TONES     "pTones"      /* [I  freq,duration pairs */
#define NJ_F_P_NTONES    "pNTones"     /* I */
#define NJ_F_P_LOOP      "pLoop"       /* I */
#define NJ_F_P_STATE     "pState"      /* I */

/* ================================================================== */
/* 3. MIDlet states                                                    */
/* ================================================================== */
#define NJ_MIDLET_UNINITIALISED 0
#define NJ_MIDLET_ACTIVE        1
#define NJ_MIDLET_PAUSED        2
#define NJ_MIDLET_DESTROYED     3

/* ================================================================== */
/* 4. Helpers shared by the MIDP modules                               */
/* ================================================================== */

/*
 * Build an nj_surface that views an Image's pixel array in place.
 * Returns NULL (with a Java exception raised) for a NULL/invalid image.
 * The surface is valid only while the Image is referenced.
 */
nj_surface *nj_midp_surface(nj_vm *vm, nj_ref image_obj, nj_surface *scratch);

/* Resolve an Image object's dimensions.  Returns 0 for a non-image. */
int nj_midp_image_size(nj_vm *vm, nj_ref image_obj, int *w, int *h);

/* Create a blank Image object (w x h, transparent black). */
nj_ref nj_midp_image_new(nj_vm *vm, int w, int h, int mutable);

/* Create an Image from decoded ARGB pixels, taking ownership of `pixels`. */
nj_ref nj_midp_image_adopt(nj_vm *vm, nj_u4 *pixels, int w, int h);

/* Fetch the pixel array of an Image as ARGB8888 (may be NULL). */
nj_u4 *nj_midp_image_pixels(nj_vm *vm, nj_ref image_obj);

/* The singleton Display object, creating it on first use. */
nj_ref nj_midp_display(nj_vm *vm);

/* The Displayable currently shown, or NULL. */
nj_ref nj_midp_current(nj_vm *vm);

/* Make `d` the current Displayable (delivers showNotify, triggers a repaint). */
nj_status nj_midp_set_current(nj_vm *vm, nj_ref d);

/* The framebuffer the runtime draws the UI into (owned by the MIDP layer). */
nj_surface *nj_midp_screen(nj_vm *vm);

/* Request a repaint of the current Displayable on the next frame. */
void nj_midp_repaint(nj_vm *vm);

/* Deliver a key event (NJ_KEY_*) to the current Canvas/GameCanvas.
 * `down` is 1 for press, 0 for release.  Returns 1 if it was consumed. */
int nj_midp_key(nj_vm *vm, int key, int down);

/*
 * Pointer events, in device pixels.  These three values are the whole reason
 * the parameter is not called `down` any more: NJ_EV_POINTER_MOVE used to be
 * passed as a bare 2, which is truthy, so a drag reached the Canvas as a
 * pointerPressed().  Named, the mistake is not available.
 */
#define NJ_POINTER_UP    0
#define NJ_POINTER_DOWN  1
#define NJ_POINTER_MOVE  2

int nj_midp_pointer(nj_vm *vm, int x, int y, int kind);

/* Paint the current Displayable into the screen surface. */
nj_status nj_midp_paint(nj_vm *vm);

/* The MIDlet lifecycle, called by the runner. */
nj_status nj_midp_run(nj_vm *vm);
nj_status nj_midp_destroy_app(nj_vm *vm, int unconditional);

/* Fonts: the VM-owned registry (no global state). */
nj_fontset *nj_midp_fonts(nj_vm *vm);
const nj_font *nj_midp_font_for(nj_vm *vm, nj_ref font_obj);

/* ------------------------------------------------------------------ */
/* MIDP key codes (javax.microedition.lcdui.Canvas)                    */
/*                                                                     */
/* These are the codes a MIDlet sees in keyPressed()/keyReleased() and  */
/* reads back from getKeyCode().  They are NOT the port's input space:  */
/* the nj_key enum in nj_port.h numbers the same keys 1..27, and        */
/* nj_key_to_midp() is the single place that translates between them.   */
/*                                                                     */
/* The names here used to be NJ_KEY_UP and so on - the same names as    */
/* the enum members - so these macros SHADOWED the enum in every        */
/* translation unit that included this header.  nj_key_to_midp() then   */
/* compared its input (an enum value: UP is 13) against MIDP values     */
/* (UP is -1) and matched nothing, and every arrow and FIRE press from  */
/* every port reached the MIDlet as keyCode 13..17.  Two more sites     */
/* compared the raw input against these macros, so soft keys were dead  */
/* too, and one had been papered over with a bare `== 13`.  Numeric     */
/* keys survived by a coincidence: the enum is 1..10 for the digits and */
/* the case that mapped them read the enum, so KEY_NUM2 came out right  */
/* while UP did not.  That is why it lasted: the keypad worked.         */
/*                                                                     */
/* The prefix is the fix.  Anything that names a MIDP code uses         */
/* NJ_MIDP_KEY_*, anything that names a port key uses NJ_KEY_*.         */
/* ------------------------------------------------------------------ */
#define NJ_MIDP_KEY_NUM0     48
#define NJ_MIDP_KEY_NUM1     49
#define NJ_MIDP_KEY_NUM2     50
#define NJ_MIDP_KEY_NUM3     51
#define NJ_MIDP_KEY_NUM4     52
#define NJ_MIDP_KEY_NUM5     53
#define NJ_MIDP_KEY_NUM6     54
#define NJ_MIDP_KEY_NUM7     55
#define NJ_MIDP_KEY_NUM8     56
#define NJ_MIDP_KEY_NUM9     57
#define NJ_MIDP_KEY_STAR     42
#define NJ_MIDP_KEY_POUND    35
#define NJ_MIDP_KEY_UP       (-1)
#define NJ_MIDP_KEY_DOWN     (-2)
#define NJ_MIDP_KEY_LEFT     (-3)
#define NJ_MIDP_KEY_RIGHT    (-4)
#define NJ_MIDP_KEY_FIRE     (-5)
#define NJ_MIDP_KEY_GAME_A   (-6)
#define NJ_MIDP_KEY_GAME_B   (-7)
#define NJ_MIDP_KEY_GAME_C   (-8)
#define NJ_MIDP_KEY_GAME_D   (-9)
/* The soft keys share codes with GAME_A..GAME_C, which is MIDP's own
 * overlap, not a mistake here: Canvas.KEY_SOFTKEY1 is -6. */
#define NJ_MIDP_KEY_SOFT1    (-6)
#define NJ_MIDP_KEY_SOFT2    (-7)
#define NJ_MIDP_KEY_SOFT3    (-8)
#define NJ_MIDP_KEY_CLEAR    (-8)

/* Game action bits, as returned by GameCanvas.getKeyStates(). */
#define NJ_GAME_UP     0x0002
#define NJ_GAME_LEFT   0x0004
#define NJ_GAME_RIGHT  0x0008
#define NJ_GAME_DOWN   0x0010
#define NJ_GAME_FIRE   0x0020
#define NJ_GAME_A      0x0040
#define NJ_GAME_B      0x0080
#define NJ_GAME_C      0x0100
#define NJ_GAME_D      0x0200

/* Delays/latency flags that ports report through nj_midp_flags(). */
#define NJ_MIDP_HAS_POINTER   0x0001
#define NJ_MIDP_HAS_SOFTKEYS  0x0002
#define NJ_MIDP_SUPPORTS_ALPHA 0x0004

/*
 * javax.microedition.media, pumped once per emulated frame.
 *
 * Sampled audio is pushed to the port a buffer at a time rather than all at
 * once, because a small part has a small device buffer: pcm_write() says how
 * many frames it took and the Player keeps the rest.  A port that accepts
 * everything finishes on the first push and this does nothing afterwards.
 */
void nj_media_frame(nj_vm *vm);

/* Write the media package's static final strings.  Called once when the VM is
 * up, for the same reason LCDUI has a late init: a builtin class cannot carry a
 * ConstantValue attribute. */
nj_status nj_media_late_init(nj_vm *vm);

/* javax.microedition.rms: the comparator's ordering constants. */
nj_status nj_rms_late_init(nj_vm *vm);

/*
 * Seed the class constants that a MIDlet has inlined at its call sites -
 * Light.AMBIENT and friends.  A no-op in a build without the m3g module.
 */
nj_status nj_m3g_late_init(nj_vm *vm);

/*
 * javax.microedition.io.HttpConnection: build the object Connector.open()
 * hands back for an http:// or https:// URL.  The class the MIDlet holds is
 * nj_lib_http.c's own HttpImpl; the MIDP interfaces above it carry no state.
 * Returns NULL with an exception set when the URL cannot be parsed.
 */
nj_ref nj_http_new(nj_vm *vm, const char *url);

/* HttpConnection.GET/POST/HEAD - same reason as the other late inits. */
nj_status nj_http_late_init(nj_vm *vm);

/*
 * javax.microedition.io.SocketConnection: the object Connector.open() hands
 * back for a socket:// URL, already connected.
 *
 * The connect happens here rather than on the first read, because that is what
 * open() means for a socket and because a MIDlet that cannot reach its server
 * wants to hear about it at the point it asked.  With no socket_connect hook
 * on the port this returns NULL with a ConnectionNotFoundException whose
 * message says the build has no sockets, and with one that declines it the
 * port's own sentence goes to the MIDlet.
 */
nj_ref nj_socket_new(nj_vm *vm, const char *url);

/* SocketConnection.DELAY/LINGER/KEEPALIVE/RCVBUF/SNDBUF - the option numbers
 * are also the indices setSocketOption() checks, so one list seeds both. */
nj_status nj_socket_late_init(nj_vm *vm);

/*
 * JSR 256 sensors.
 *
 * nj_sensor_frame() is the pump: it reads any connection that has a
 * DataListener installed and calls dataReceived() on it, once per emulated
 * frame, next to nj_media_frame() above.  A MIDlet that only polls with
 * getData() never registers here and the call costs one NULL check.
 *
 * nj_sensor_shutdown() closes every connection still open, so a port's sensor
 * hardware is released even when the MIDlet never called close(); the VM calls
 * it on the way out.
 *
 * nj_sensor_late_init() writes the interfaces' static final values, for the
 * same reason the rest of the package has one.
 */
void nj_sensor_frame(nj_vm *vm);
void nj_sensor_shutdown(nj_vm *vm);
nj_status nj_sensor_late_init(nj_vm *vm);

/*
 * JSR 82 Bluetooth.
 *
 * nj_bt_listen() opens a "btspp://localhost:..." or "btl2cap://localhost:..."
 * server through the port's bt_listen() hook and returns the notifier object
 * Connector.open() hands the MIDlet, or NULL when there is no radio - the
 * caller then throws ConnectionNotFoundException with
 * nj_bt_listen_error()'s sentence, which is what the specification asks for.
 *
 * nj_bt_frame() is where an inquiry and a service search make progress: they
 * are jobs the port is asked about a slice at a time, and the MIDlet's
 * DiscoveryListener is called from here, once per frame.
 *
 * nj_bt_shutdown() drops those jobs when the MIDlet ends - the port's own
 * shutdown() closes anything it still holds open.
 *
 * nj_bt_late_init() writes the static final constants (the DataElement types,
 * the inquiry access codes, the search result codes).
 */
nj_ref  nj_bt_listen(nj_vm *vm, const char *url);
const char *nj_bt_listen_error(nj_vm *vm, const char *url);
void    nj_bt_frame(nj_vm *vm);
void    nj_bt_shutdown(nj_vm *vm);
nj_status nj_bt_late_init(nj_vm *vm);

/*
 * Fill a triangle through a MIDlet's own javax.microedition.lcdui.Graphics.
 *
 * The Mascot Capsule rasterizer lives in another file and cannot reach
 * gfx_load/gfx_store, which are static in nj_lib_lcdui.c.  Rather than
 * duplicate the mapping from a Java Graphics to an nj_gfx -- including the
 * clip and translate bookkeeping that took a real bug to get right -- it goes
 * through this one entry point.  Returns 0 if the object is not a Graphics.
 */
int nj_midp_gfx_triangle(nj_vm *vm, nj_ref g_obj,
                         nj_s4 x1, nj_s4 y1, nj_s4 x2, nj_s4 y2,
                         nj_s4 x3, nj_s4 y3, nj_u4 argb);

#endif /* NJ_MIDP_H */
