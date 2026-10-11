/*
 * nano_j2me - nj_png.h
 *
 * Minimal PNG decoder for Image.createImage().  Supports what J2ME assets
 * actually use: 8/1/4/2/16-bit greyscale, truecolour, palette, alpha, and
 * non-interlaced + Adam7 interlaced images.  Output is always ARGB8888.
 */
#ifndef NJ_PNG_H
#define NJ_PNG_H

#include "nj_types.h"
#include "nj_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode `data` into a newly allocated nj_image.
 * `alloc`/`free` are the VM allocator (pass NULL to use malloc/free).
 * On failure returns NULL; when `err` is non-NULL it receives a static
 * description of the problem.
 */
nj_image *nj_png_decode(const void *data, nj_u4 size,
                        void *(*alloc)(void *, nj_size), void *ud,
                        void  (*dealloc)(void *, void *),
                        const char **err);

/* Free an image produced by nj_png_decode / nj_image_load. */
void nj_image_free(nj_image *img, void (*dealloc)(void *, void *), void *ud);

/*
 * Sniff the container and decode accordingly (PNG, and JPEG when
 * NJ_ENABLE_JPEG).  Returns NULL for unsupported formats.
 */
nj_image *nj_image_load(const void *data, nj_u4 size,
                        void *(*alloc)(void *, nj_size), void *ud,
                        void  (*dealloc)(void *, void *),
                        const char **err);

/* Create an all-zero (transparent black) image. */
nj_image *nj_image_create(int w, int h,
                          void *(*alloc)(void *, nj_size), void *ud);

#ifdef __cplusplus
}
#endif

#endif /* NJ_PNG_H */
