/*
 * nano_j2me - nj_codec.h
 *
 * ============================================================================
 *  IMAGE CODEC ADAPTERS
 * ============================================================================
 *
 * Decoding PNG and JPEG in portable C is the right default, but it is rarely
 * the right answer on a specific board:
 *
 *   ESP32-S3   has a hardware JPEG decoder and a PPA that can also scale;
 *              decoding a 640x480 JPEG into an 8 MB PSRAM buffer in software
 *              costs ~200 ms, in hardware ~15 ms.
 *   Android    should hand the bytes to BitmapFactory, which uses the
 *              platform's own (usually hardware) path.
 *   A board with a mask ROM codec (many camera-oriented MCUs) should use it.
 *   A product with a fixed asset pipeline may not want a JPEG decoder linked
 *              in at all, and would rather get a clear error than 40 KB of
 *              code it never executes.
 *
 * So image decoding goes through a registry of providers.  The runtime ships
 * software PNG (and optionally software JPEG) as ordinary providers; a port
 * installs its own ahead of them and wins.  Nothing about the rest of the
 * runtime changes: MIDP still calls Image.createImage(byte[]) and still gets an
 * Image back.
 *
 * ============================================================================
 *  WHAT A PROVIDER MUST GUARANTEE
 * ============================================================================
 *   - `accepts()` is cheap: it looks at a magic number, nothing else.  It is
 *     called for every provider on every decode, so it must not scan.
 *   - `decode()` either returns a fully formed nj_image or NULL.  On NULL it
 *     MAY set *err to a static string; the runtime copies it into the Java
 *     exception message.
 *   - Memory is taken through the request's alloc/dealloc hooks, never
 *     malloc directly, so teardown stays complete and the Java heap can be
 *     backed by PSRAM.  `nj_image_free_ex()` releases whatever was handed out.
 *   - Untrusted input stays untrusted: bounds-check every read.  A provider
 *     that can be made to write outside its output buffer is a remote code
 *     execution bug in a game loader, which is exactly the threat model here.
 *   - A provider must be reentrant and hold no mutable global state, for the
 *     same reason the rest of the runtime does not.
 */
#ifndef NJ_CODEC_H
#define NJ_CODEC_H

#include "nj_types.h"
#include "nj_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif


/* ------------------------------------------------------------------ */
/* Formats                                                             */
/* ------------------------------------------------------------------ */
/* Containers a provider can recognise. */
#define NJ_CODEC_FMT_PNG   0x0001u
#define NJ_CODEC_FMT_JPEG  0x0002u
#define NJ_CODEC_FMT_GIF   0x0004u
#define NJ_CODEC_FMT_BMP   0x0008u
#define NJ_CODEC_FMT_WBMP  0x0010u
#define NJ_CODEC_FMT_RAW   0x0020u

/* Pixel layouts a provider can produce. */
#define NJ_CODEC_OUT_ARGB8888 0x0001u   /* 0xAARRGGBB, one nj_u4 per pixel */
#define NJ_CODEC_OUT_RGB565   0x0002u   /* 16bpp, one nj_u2 per pixel      */
#define NJ_CODEC_OUT_NATIVE   0x0004u   /* match the display's format      */

/* Scale factors a provider can decode at, as a bitmask:
 * bit 0 = 1/1, bit 1 = 1/2, bit 2 = 1/4, bit 3 = 1/8. */
#define NJ_CODEC_SCALE_1  (1u << 0)
#define NJ_CODEC_SCALE_2  (1u << 1)
#define NJ_CODEC_SCALE_4  (1u << 2)
#define NJ_CODEC_SCALE_8  (1u << 3)

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */
typedef struct nj_codec_caps {
    nj_u4 formats;      /* NJ_CODEC_FMT_* accepted                        */
    nj_u4 outputs;      /* NJ_CODEC_OUT_* produced                        */
    nj_u4 scales;       /* NJ_CODEC_SCALE_* honoured; 0 means 1/1 only   */
    nj_u4 max_pixels;   /* refuse larger images; 0 = no limit             */
    int   progressive;  /* handles progressive/interlaced variants        */
    int   streaming;    /* can decode from a buffer smaller than the file */
} nj_codec_caps;

/* Header-only probe result. */
typedef struct nj_image_info {
    int width;
    int height;
    int has_alpha;
    int bits_per_pixel;   /* per source pixel, 0 if unknown */
    nj_u4 format;         /* the NJ_CODEC_FMT_* that matched */
} nj_image_info;

/* ------------------------------------------------------------------ */
/* Decode request                                                      */
/* ------------------------------------------------------------------ */
typedef struct nj_codec_req {
    const void *data;
    nj_u4       size;

    /* Requested geometry.  0 means "native size".  A provider that cannot
     * scale must either decode natively (and the caller resamples) or refuse;
     * `want_width`/`want_height` are a HINT, and the returned image's actual
     * size is always authoritative. */
    int want_width;
    int want_height;

    int want_alpha;      /* 1 = alpha must be preserved exactly          */
    nj_u4 out_format;    /* one of NJ_CODEC_OUT_*, or 0 for "any"        */

    /* Memory.  A provider MUST use these. */
    void *(*alloc)(void *ud, nj_size n);
    void  (*dealloc)(void *ud, void *p);
    void   *ud;

    /* Optional: the provider may set this to a static description. */
    const char **err;
} nj_codec_req;

/* ------------------------------------------------------------------ */
/* Provider                                                            */
/* ------------------------------------------------------------------ */
typedef struct nj_codec {
    const char *name;         /* "png-software", "jpeg-hw", ... */

    nj_codec_caps caps;   /* a value, so the per-VM copy stays assignable */

    /*
     * Cheap magic check.  Return 1 when this provider recognises the bytes.
     * May be NULL, in which case the provider is only used when explicitly
     * asked for by name.
     */
    int (*accepts)(const void *data, nj_u4 size);

    /*
     * Dimensions and properties without decoding.  Used by
     * Image.createImage(byte[],off,len) bounds checks and by callers that
     * want to allocate the destination themselves.  Optional but strongly
     * recommended: without it, probing a 2000x2000 image decodes it.
     */
    nj_status (*info)(const void *data, nj_u4 size, nj_image_info *out);

    /* Decode.  Return NULL on failure. */
    nj_image *(*decode)(const nj_codec_req *req);

    /* Provider-private context, handed back to the hooks above. */
    void *ud;

    /*
     * If non-NULL, the provider needs a per-VM instance and this is called
     * with `ud` set to a VM-owned copy's private area.  Simple providers
     * leave it NULL.  It exists so a platform codec can keep per-run state
     * (a hardware handle) without a global.
     */
    nj_status (*attach)(nj_vm *vm, struct nj_codec *self);
    void      (*detach)(nj_vm *vm, struct nj_codec *self);
} nj_codec;

/* ------------------------------------------------------------------ */
/* Registry (owned by the VM - no globals)                             */
/* ------------------------------------------------------------------ */
/*
 * Install a provider.  `priority` orders the registry: higher wins, so a port
 * passes something above NJ_CODEC_PRIO_BUILTIN to shadow the software
 * decoders.  The nj_codec struct must outlive the VM (a `const` static, or
 * memory the port owns); the runtime copies the descriptor but not its
 * pointed-to members.
 */
#define NJ_CODEC_PRIO_FALLBACK  (-100)
#define NJ_CODEC_PRIO_BUILTIN      0
#define NJ_CODEC_PRIO_PREFERRED  100
#define NJ_CODEC_PRIO_PLATFORM   200

nj_status nj_codec_register(nj_vm *vm, const nj_codec *codec, int priority);
void      nj_codec_unregister(nj_vm *vm, const nj_codec *codec);

/* Find the highest-priority provider that accepts these bytes.
 * `name` may be non-NULL to require a specific provider. */
const nj_codec *nj_codec_find(nj_vm *vm, const void *data, nj_u4 size,
                              const char *name);

/* Number of registered providers, for diagnostics. */
int nj_codec_count(nj_vm *vm);

/* Name of provider `index`, or NULL. */
const char *nj_codec_name_at(nj_vm *vm, int index);

/* True when a provider that accepts `data` exists. */
int nj_codec_available(nj_vm *vm, const void *data, nj_u4 size);

/* ------------------------------------------------------------------ */
/* Decoding                                                            */
/* ------------------------------------------------------------------ */
/*
 * Decode an image using the registry.  This is what the MIDP layer calls; it
 * is the entry point that makes a platform codec take effect.
 *
 * On failure returns NULL, and when `err` is non-NULL sets it to a static
 * description suitable for an exception message ("no JPEG decoder on this
 * platform" rather than a silent NULL).
 */
nj_image *nj_image_decode(nj_vm *vm, const nj_codec_req *req,
                          const char **err);

/* Probe dimensions without decoding.  `name` may select a provider. */
nj_status nj_image_probe(nj_vm *vm, const void *data, nj_u4 size,
                         nj_image_info *out, const char *name);

/*
 * Convenience wrappers on top of nj_image_decode, for the common cases.
 * They fill in the allocator hooks from the VM.
 */
nj_image *nj_image_decode_mem(nj_vm *vm, const void *data, nj_u4 size,
                              const char **err);
nj_image *nj_image_decode_scaled(nj_vm *vm, const void *data, nj_u4 size,
                                 int want_w, int want_h, const char **err);

/* Release an image produced by any provider. */
void nj_image_release(nj_vm *vm, nj_image *img);

/* ------------------------------------------------------------------ */
/* Built-in providers                                                  */
/* ------------------------------------------------------------------ */
/* The software PNG decoder (src/gfx/nj_png.c).  Always present unless
 * NJ_ENABLE_PNG is 0. */
extern const nj_codec nj_codec_png_software;

/*
 * The software baseline JPEG decoder (src/gfx/nj_jpeg.c), when
 * NJ_ENABLE_JPEG is 1.  Otherwise this is a stub whose accepts() returns 0,
 * so a JPEG simply reports "no decoder" instead of being mis-detected.
 */
extern const nj_codec nj_codec_jpeg_software;

/* A provider that recognises JPEG and always fails with a clear message.
 * Registered at the lowest priority so that Image.createImage of a JPEG on a
 * build without any JPEG decoder explains itself. */
extern const nj_codec nj_codec_jpeg_unavailable;

/* Install the built-in providers into a VM.  Called by nj_vm_create(). */
void nj_codecs_register_builtins(nj_vm *vm);

/* ------------------------------------------------------------------ */
/* Helpers for writing a provider                                      */
/* ------------------------------------------------------------------ */
/* Allocate a blank image through the request's hooks. */
nj_image *nj_codec_image_new(const nj_codec_req *req, int w, int h,
                             nj_u4 format, int has_alpha);

/* The number of bytes one pixel occupies in a given output format. */
nj_u4 nj_codec_pixel_bytes(nj_u4 out_format);

#ifdef __cplusplus
}
#endif

#endif /* NJ_CODEC_H */
