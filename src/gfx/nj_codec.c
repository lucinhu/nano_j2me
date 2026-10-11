/*
 * nano_j2me - nj_codec.c
 *
 * The image-codec registry.
 *
 * Decoding is the one part of a J2ME runtime that a platform almost always has
 * a better answer for than portable C: a hardware JPEG block, a mask-ROM
 * codec, or the host OS's own image stack.  This file is the seam.
 *
 * The registry belongs to the VM, not to a global, for the same reason
 * everything else does: a device that starts and stops games must not leak a
 * provider's state between launches, and a port must be able to install a
 * codec for one VM without affecting another.
 *
 * Providers are stored as per-VM COPIES of the caller's descriptor.  The copy
 * is what lets `attach()` hand a provider a private context without touching
 * the shared `const` original - which is the only way to have a stateful
 * hardware codec and still keep zero mutable globals.
 */
#include "nj_internal.h"
#include "nj2me/nj_png.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* Registry                                                            */
/* ------------------------------------------------------------------ */
nj_status nj_codec_register(nj_vm *vm, const nj_codec *codec, int priority)
{
    nj_u2 i;

    if (!vm || !codec) {
        return NJ_ERR;
    }
    /* Re-registering the same descriptor replaces it, so a port can be
     * re-initialised without accumulating entries. */
    for (i = 0u; i < vm->ncodecs; i++) {
        if (vm->codecs[i].codec.accepts == codec->accepts &&
            vm->codecs[i].codec.decode == codec->decode) {
            vm->codecs[i].codec = *codec;
            vm->codecs[i].priority = priority;
            return NJ_OK;
        }
    }

    if (vm->ncodecs == vm->codec_cap) {
        nj_u2 cap = vm->codec_cap ? (nj_u2)(vm->codec_cap * 2u) : 8u;
        nj_codec_reg *nr = (nj_codec_reg *)nj_host_alloc(
            vm, (nj_size)cap * sizeof(nj_codec_reg));
        if (!nr) {
            return NJ_ERR_NOMEM;
        }
        if (vm->codecs) {
            memcpy(nr, vm->codecs, (nj_size)vm->ncodecs * sizeof(nj_codec_reg));
            nj_host_free(vm, vm->codecs);
        }
        vm->codecs = nr;
        vm->codec_cap = cap;
    }

    vm->codecs[vm->ncodecs].codec = *codec;
    vm->codecs[vm->ncodecs].priority = priority;
    vm->ncodecs++;

    if (codec->attach) {
        nj_codec *self = &vm->codecs[vm->ncodecs - 1u].codec;
        if (self->attach(vm, self) != NJ_OK) {
            vm->ncodecs--;
            return NJ_ERR;
        }
    }
    return NJ_OK;
}

void nj_codec_unregister(nj_vm *vm, const nj_codec *codec)
{
    nj_u2 i;

    if (!vm || !codec) {
        return;
    }
    for (i = 0u; i < vm->ncodecs; i++) {
        if (vm->codecs[i].codec.decode == codec->decode &&
            vm->codecs[i].codec.accepts == codec->accepts) {
            if (vm->codecs[i].codec.detach) {
                vm->codecs[i].codec.detach(vm, &vm->codecs[i].codec);
            }
            vm->codecs[i] = vm->codecs[vm->ncodecs - 1u];
            vm->ncodecs--;
            return;
        }
    }
}

const nj_codec *nj_codec_find(nj_vm *vm, const void *data, nj_u4 size,
                              const char *name)
{
    const nj_codec *best = NULL;
    int best_prio = 0;
    nj_u2 i;

    if (!vm) {
        return NULL;
    }
    for (i = 0u; i < vm->ncodecs; i++) {
        nj_codec_reg *r = &vm->codecs[i];
        if (name && (!r->codec.name || strcmp(r->codec.name, name) != 0)) {
            continue;
        }
        if (!name) {
            if (!r->codec.accepts || !r->codec.accepts(data, size)) {
                continue;
            }
        }
        if (!best || r->priority > best_prio) {
            best = &r->codec;
            best_prio = r->priority;
        }
    }
    return best;
}

int nj_codec_count(nj_vm *vm)
{
    return vm ? (int)vm->ncodecs : 0;
}

const char *nj_codec_name_at(nj_vm *vm, int index)
{
    if (!vm || index < 0 || index >= (int)vm->ncodecs) {
        return NULL;
    }
    return vm->codecs[index].codec.name;
}

int nj_codec_available(nj_vm *vm, const void *data, nj_u4 size)
{
    return nj_codec_find(vm, data, size, NULL) != NULL;
}

/* ------------------------------------------------------------------ */
/* Decoding                                                            */
/* ------------------------------------------------------------------ */
nj_u4 nj_codec_pixel_bytes(int pixfmt)
{
    switch (pixfmt) {
    case NJ_PIXFMT_RGB565:   return 2u;
    case NJ_PIXFMT_RGB888:   return 3u;
    case NJ_PIXFMT_ARGB8888: return 4u;
    default:                 return 4u;
    }
}

int nj_codec_pick_format(nj_u4 can_produce, int display_format)
{
    /* Prefer a layout that matches the display, so nothing has to be
     * converted at blit time; fall back to whatever the provider has. */
    if (can_produce & NJ_CODEC_CAN(display_format)) {
        return display_format;
    }
    if (can_produce & NJ_CODEC_CAN_ARGB8888) {
        return NJ_PIXFMT_ARGB8888;
    }
    if (can_produce & NJ_CODEC_CAN_RGB888) {
        return NJ_PIXFMT_RGB888;
    }
    if (can_produce & NJ_CODEC_CAN_RGB565) {
        return NJ_PIXFMT_RGB565;
    }
    return NJ_PIXFMT_ARGB8888;
}

nj_image *nj_codec_image_new(const nj_codec_req *req, int w, int h,
                             int pixfmt, int has_alpha)
{
    nj_image *img;
    nj_size bytes;

    if (!req || w <= 0 || h <= 0) {
        return NULL;
    }
    /* 64-bit arithmetic: a hostile header must not wrap the size. */
    bytes = (nj_size)w * (nj_size)h * (nj_size)nj_codec_pixel_bytes(pixfmt);
    if (bytes == 0u || bytes > (nj_size)0x7FFFFFFFu) {
        return NULL;
    }
    img = (nj_image *)req->alloc(req->ud, sizeof(nj_image));
    if (!img) {
        return NULL;
    }
    memset(img, 0, sizeof(*img));
    img->pixels = (nj_u4 *)req->alloc(req->ud, bytes);
    if (!img->pixels) {
        req->dealloc(req->ud, img);
        return NULL;
    }
    memset(img->pixels, 0, bytes);
    img->width = w;
    img->height = h;
    img->format = pixfmt;
    img->has_alpha = has_alpha ? 1 : 0;
    return img;
}

static void image_fail(const nj_codec_req *req, const char *msg)
{
    if (req && req->err) {
        *req->err = msg;
    }
}

nj_image *nj_image_decode(nj_vm *vm, const nj_codec_req *req,
                          const char **err)
{
    const nj_codec *c;
    nj_image *img;

    if (err) {
        *err = NULL;
    }
    if (!vm || !req || !req->data || req->size == 0u) {
        if (err) {
            *err = "empty image data";
        }
        return NULL;
    }
    c = nj_codec_find(vm, req->data, req->size, NULL);
    if (!c) {
        if (err) {
            *err = nj_codec_available(vm, req->data, req->size)
                       ? "no decoder for this image format"
                       : "unrecognised image format";
        }
        return NULL;
    }
    if (c->caps.max_pixels != 0u && c->info) {
        nj_image_info info;
        memset(&info, 0, sizeof(info));
        if (c->info(req->data, req->size, &info) == NJ_OK) {
            nj_u4 px = (nj_u4)info.width * (nj_u4)info.height;
            if (px > c->caps.max_pixels) {
                if (err) {
                    *err = "image too large for this decoder";
                }
                return NULL;
            }
        }
    }
    img = c->decode(req);
    if (!img && err && !*err) {
        *err = "the decoder rejected the image";
    }
    return img;
}

nj_status nj_image_probe(nj_vm *vm, const void *data, nj_u4 size,
                         nj_image_info *out, const char *name)
{
    const nj_codec *c;

    if (!vm || !data || !out) {
        return NJ_ERR;
    }
    memset(out, 0, sizeof(*out));
    c = nj_codec_find(vm, data, size, name);
    if (!c || !c->info) {
        return NJ_ERR_UNSUPPORTED;
    }
    return c->info(data, size, out);
}

/* ------------------------------------------------------------------ */
/* VM-backed convenience wrappers                                      */
/* ------------------------------------------------------------------ */
/*
 * These fill the request's allocator hooks with the VM's own tracked
 * allocator, so everything a provider hands back is released by
 * nj_vm_destroy() even if the caller forgets - which is what keeps "exit
 * leaves nothing behind" true regardless of which codec ran.
 */
static void *vm_alloc(void *ud, nj_size n)
{
    return nj_host_alloc((nj_vm *)ud, n);
}

static void vm_dealloc(void *ud, void *p)
{
    nj_host_free((nj_vm *)ud, p);
}

static void req_init(nj_codec_req *req, nj_vm *vm, const void *data,
                     nj_u4 size, const char **err)
{
    memset(req, 0, sizeof(*req));
    req->data = data;
    req->size = size;
    req->alloc = vm_alloc;
    req->dealloc = vm_dealloc;
    req->ud = vm;
    req->want_alpha = 1;
    req->out_format = NJ_CODEC_OUT_ANY;
    req->err = err;
}

nj_image *nj_image_decode_mem(nj_vm *vm, const void *data, nj_u4 size,
                              const char **err)
{
    nj_codec_req req;
    req_init(&req, vm, data, size, err);
    return nj_image_decode(vm, &req, err);
}

nj_image *nj_image_decode_scaled(nj_vm *vm, const void *data, nj_u4 size,
                                 int want_w, int want_h, const char **err)
{
    nj_codec_req req;
    req_init(&req, vm, data, size, err);
    req.want_width = want_w;
    req.want_height = want_h;
    return nj_image_decode(vm, &req, err);
}

void nj_image_release(nj_vm *vm, nj_image *img)
{
    if (!vm || !img) {
        return;
    }
    nj_host_free(vm, img->pixels);
    img->pixels = NULL;
    nj_host_free(vm, img);
}

/* ------------------------------------------------------------------ */
/* Built-in providers                                                  */
/* ------------------------------------------------------------------ */
#if NJ_ENABLE_PNG
static int png_accepts(const void *data, nj_u4 size)
{
    static const nj_u1 magic[8] = { 0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au,
                                    0x1Au, 0x0Au };
    if (size < 8u) {
        return 0;
    }
    return memcmp(data, magic, 8u) == 0;
}

static nj_status png_info(const void *data, nj_u4 size, nj_image_info *out)
{
    /* Walk to IHDR: 8 magic + 4 length + 4 type + 8 payload. */
    const nj_u1 *p = (const nj_u1 *)data;
    if (size < 24u || !png_accepts(data, size)) {
        return NJ_ERR_FORMAT;
    }
    if (memcmp(p + 12, "IHDR", 4u) != 0) {
        return NJ_ERR_FORMAT;
    }
    out->width  = (int)(((nj_u4)p[16] << 24) | ((nj_u4)p[17] << 16) |
                        ((nj_u4)p[18] << 8) | (nj_u4)p[19]);
    out->height = (int)(((nj_u4)p[20] << 24) | ((nj_u4)p[21] << 16) |
                        ((nj_u4)p[22] << 8) | (nj_u4)p[23]);
    out->bits_per_pixel = (int)p[24];
    out->has_alpha = (p[25] == 4u || p[25] == 6u) ? 1 : 0;
    out->format = NJ_CODEC_FMT_PNG;
    return NJ_OK;
}

static nj_image *png_decode(const nj_codec_req *req)
{
    return nj_png_decode(req->data, req->size,
                         req->alloc, req->ud, req->dealloc, req->err);
}
#endif /* NJ_ENABLE_PNG */

static int jpeg_accepts_stub(const void *data, nj_u4 size)
{
    const nj_u1 *p = (const nj_u1 *)data;
    /* SOI marker, then any APPn / DQT / SOF marker. */
    if (size < 4u || p[0] != 0xFFu || p[1] != 0xD8u) {
        return 0;
    }
    return 1;
}

static nj_image *jpeg_unavailable_decode(const nj_codec_req *req)
{
    image_fail(req, "this build has no JPEG decoder; install one with "
                    "nj_codec_register() (see include/nj2me/nj_codec.h)");
    return NULL;
}

const nj_codec nj_codec_png_software = {
    "png-software",
    { NJ_CODEC_FMT_PNG, NJ_CODEC_CAN_ARGB8888, NJ_CODEC_SCALE_1,
      0u, 1, 0 },
#if NJ_ENABLE_PNG
    png_accepts, png_info, png_decode,
#else
    NULL, NULL, NULL,
#endif
    NULL, NULL, NULL
};

const nj_codec nj_codec_jpeg_unavailable = {
    "jpeg-unavailable",
    { NJ_CODEC_FMT_JPEG, NJ_CODEC_CAN_ARGB8888, NJ_CODEC_SCALE_1,
      0u, 0, 0 },
    jpeg_accepts_stub, NULL, jpeg_unavailable_decode,
    NULL, NULL, NULL
};

#if NJ_ENABLE_JPEG
const nj_codec nj_codec_jpeg_software = {
    "jpeg-software",
    /* 1/2 and 1/4 are honoured in the DCT domain, which is both faster and
     * better looking than decoding full size and resampling - so say so, and
     * let a caller that wants a thumbnail ask for one. */
    { NJ_CODEC_FMT_JPEG, NJ_CODEC_CAN_ARGB8888,
      NJ_CODEC_SCALE_1 | NJ_CODEC_SCALE_2 | NJ_CODEC_SCALE_4,
      0u, 0, 0 },
    nj_jpeg_accepts, nj_jpeg_info, nj_jpeg_decode,
    NULL, NULL, NULL
};
#else
/* Still a valid descriptor: accepts() returns 0, so it never shadows the
 * "unavailable" provider that explains the situation. */
const nj_codec nj_codec_jpeg_software = {
    "jpeg-software",
    { NJ_CODEC_FMT_JPEG, NJ_CODEC_CAN_ARGB8888, NJ_CODEC_SCALE_1,
      0u, 0, 0 },
    NULL, NULL, NULL,
    NULL, NULL, NULL
};
#endif

void nj_codecs_register_builtins(nj_vm *vm)
{
    if (!vm) {
        return;
    }
#if NJ_ENABLE_PNG
    (void)nj_codec_register(vm, &nj_codec_png_software, NJ_CODEC_PRIO_BUILTIN);
#endif
#if NJ_ENABLE_JPEG
    (void)nj_codec_register(vm, &nj_codec_jpeg_software, NJ_CODEC_PRIO_BUILTIN);
#else
    /* Lowest priority so any platform decoder wins, but present so that
     * Image.createImage of a JPEG reports why it failed instead of
     * "unrecognised image format". */
    (void)nj_codec_register(vm, &nj_codec_jpeg_unavailable,
                            NJ_CODEC_PRIO_FALLBACK);
#endif
}
