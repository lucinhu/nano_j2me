#ifndef NJ_FSR2_H
#define NJ_FSR2_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NjFsr2 NjFsr2;

/* All CPU arrays are tightly packed, row-major, top-down. Color is opaque
 * 0xAARRGGBB. Depth is perspective device Z (0 near, 1 far). Motion is
 * previous minus current unjittered position in input pixels, +Y down. */
typedef struct NjFsr2Frame {
    const uint32_t *color_argb;
    const float *depth;
    const float *motion_xy;
    float jitter_x, jitter_y; /* sample offset in input pixels, +Y down */
    float delta_ms, camera_near, camera_far, fov_y; /* vertical FOV in radians */
    int reset;
} NjFsr2Frame;

/* Contexts are thread-confined. Resize by recreating the context. */
NjFsr2 *nj_fsr2_create(int rw, int rh, int ow, int oh, char *error, size_t cap);
void nj_fsr2_destroy(NjFsr2 *ctx);
void nj_fsr2_jitter(NjFsr2 *ctx, unsigned frame, float *x, float *y);
/* Returns 1 after GPU completion, 0 on failure. Recreate after GPU errors. */
int nj_fsr2_dispatch(NjFsr2 *ctx, const NjFsr2Frame *frame, uint32_t *out_argb,
                     char *error, size_t cap);
#ifdef __cplusplus
}
#endif
#endif
