/* Private optional libretro hardware adapter. No GL/libretro ABI types. */
#ifndef NJ_LIBRETRO_HW_H
#define NJ_LIBRETRO_HW_H

#include <stdint.h>
#include "nj2me/nj_gfx.h"
#include "nj2me/nj_gfx_raster.h"
#include "nj2me/nj_gfx_triangles.h"

typedef void (*nj_libretro_gl_proc)(void);

typedef struct nj_libretro_hw_config {
    nj_libretro_gl_proc (*get_proc)(void *ud, const char *name);
    int (*is_current)(void *ud);
    void *ud;
    int context_type; /* libretro RETRO_HW_CONTEXT_* numeric value */
    unsigned major, minor;
} nj_libretro_hw_config;

/* Every entry except destroy(lost=1) is called only in a frontend-guaranteed
 * current GL section. create/destroy must preserve frontend GL state themselves.
 * begin/end bracket a batch of drawing + presentation and preserve ALL affected
 * frontend state. A failed end MUST NOT undo an already committed drawing DONE.
 * valid must not mutate frontend state; it may query reset status/currentness.
 * destroy(lost=1) releases CPU allocations without GL, including on core deinit
 * when the frontend omitted context_destroy. GL objects then belong to the host
 * context and disappear when the host destroys it.
 *
 * Drawing callbacks retain the synchronous atomic CPU-write contracts. present
 * uploads the complete rotated CPU surface to the supplied frontend FBO (0 is
 * valid), using bottom-left framebuffer semantics; nonzero means successful.
 * Dispatch pointers/resources are per instance; no linked GLES entry points. */
typedef struct nj_libretro_hw_driver {
    void *(*create)(const nj_libretro_hw_config *config);
    void (*destroy)(void *state, int lost);
    int (*valid)(void *state);
    int (*begin)(void *state);
    int (*end)(void *state);
    nj_gfx_accel accel;
    nj_gfx_raster_fn raster;
    nj_gfx_triangles_fn triangles;
    nj_gfx_triangles_list_fn triangles_list; /* NULL: single-packet only */
    int (*present)(void *state, uintptr_t framebuffer, const nj_surface *surface);
    const char *(*info)(void *state); /* renderer/version; valid until destroy */
} nj_libretro_hw_driver;

/* Supplied only by the optional proc-loaded GL adapter. */
const nj_libretro_hw_driver *nj_libretro_hw_gl_driver(void);
#endif
