/* Display-only surfaces paired with the MIDP screen and mutable Java images.
 * The logical raster remains the source of truth for getRGB, collisions and
 * snapshots. A physical target is allocated on its first draw. Bitmap-only
 * sources never need a companion. Storage is private to one VM. */
#include "nj_midp.h"
#include <stdint.h>
#include <string.h>

#define HD_TARGETS 32
#define HD_BYTE_LIMIT ((nj_size)96 * 1024u * 1024u)

typedef struct hd_entry {
    nj_render_mirror mirror;
    nj_surface physical;
    const void *key;
    nj_ref owner;                     /* weak Java int[]; NULL for the screen */
    int width, height, valid;
} hd_entry;

struct nj_hd_store {
    hd_entry entries[HD_TARGETS];     /* slot zero is reserved for the screen */
    nj_surface resolve;
    nj_size bytes;
};

static const nj_surface *hd_lookup(void *ud, const void *key, int w, int h)
{
    struct nj_hd_store *store = (struct nj_hd_store *)ud;
    int i;
    for (i = 0; i < HD_TARGETS; ++i) {
        hd_entry *e = &store->entries[i];
        if (e->key == key && e->valid && e->width == w && e->height == h)
            return &e->physical;
    }
    return NULL;
}

const nj_surface *nj_midp_hd_source(nj_vm *vm, const nj_image *image)
{
    return vm && vm->hd_store && image ?
        hd_lookup(vm->hd_store, image->pixels, image->width, image->height) : NULL;
}

static void hd_free_surface(nj_vm *vm, nj_surface *s)
{
    if (s->pixels) {
        vm->hd_store->bytes -= (nj_size)s->height * (nj_size)s->stride;
        nj_host_free(vm, s->pixels);
        memset(s, 0, sizeof(*s));
    }
}

static void hd_release(nj_vm *vm, hd_entry *entry)
{
    hd_free_surface(vm, &entry->physical);
    memset(entry, 0, sizeof(*entry));
}

static int hd_surface(nj_vm *vm, nj_surface *s, int w, int h)
{
    nj_size bytes;
    if (w <= 0 || h <= 0 || (nj_size)w > HD_BYTE_LIMIT / 4u / (nj_size)h)
        return 0;
    bytes = (nj_size)w * (nj_size)h * 4u;
    if (s->pixels && s->width == w && s->height == h) return 1;
    hd_free_surface(vm, s);
    if (bytes > HD_BYTE_LIMIT - vm->hd_store->bytes) return 0;
    s->pixels = nj_host_alloc(vm, bytes);
    if (!s->pixels) return 0;
    s->format = NJ_PIXFMT_ARGB8888;
    s->width = w;
    s->height = h;
    s->stride = w * 4;
    s->bpp = 4;
    vm->hd_store->bytes += bytes;
    return 1;
}

static nj_ref hd_owner(nj_vm *vm, const nj_surface *s)
{
    uintptr_t addr = (uintptr_t)s->pixels;
    uintptr_t base = (uintptr_t)vm->heap.base;
    nj_ref a;
    if (!base || addr < base || addr - base < NJ_BLOCK_HDR + sizeof(nj_object) ||
        addr - base >= vm->heap.bump || s->format != NJ_PIXFMT_ARGB8888) return NULL;
    a = (nj_ref)(addr - sizeof(nj_object));
    if (!nj_heap_owns_object_fast(vm, a) || a->kind != NJ_OBJ_ARRAY ||
        a->aflags != NJ_ARRAY_T_INT || a->u.bytes != s->pixels ||
        (nj_u8)(unsigned)s->width * (unsigned)s->height > a->length) return NULL;
    return a;
}

static void hd_seed(hd_entry *e, const nj_surface *s)
{
    int k = e->mirror.scale, x, y, i;
    for (y = 0; y < s->height; ++y) {
        nj_u4 *row = (nj_u4 *)e->physical.pixels + (nj_size)y * k * e->physical.width;
        for (x = 0; x < s->width; ++x) {
            nj_u4 color = nj_surface_get(s, x, y);
            for (i = 0; i < k; ++i) row[x * k + i] = color;
        }
        for (i = 1; i < k; ++i)
            memcpy(row + (nj_size)i * e->physical.width, row, (nj_size)e->physical.stride);
    }
    e->valid = 1;
}

nj_render_mirror *nj_midp_hd_target(nj_vm *vm, const nj_surface *s)
{
    struct nj_hd_store *store;
    hd_entry *e = NULL;
    nj_ref owner = NULL;
    int k, i;
    if (!vm || !s || !s->pixels || s->width <= 0 || s->height <= 0) return NULL;
    k = vm->render_quality.scale * vm->render_quality.ssaa;
    if (k < 2 || k > 8 || s->width > 16384 / k || s->height > 16384 / k) return NULL;
    if (s->pixels != vm->screen.pixels && !(owner = hd_owner(vm, s))) return NULL;
    store = vm->hd_store;
    if (!store) {
        store = (struct nj_hd_store *)nj_host_alloc(vm, sizeof(*store));
        if (!store) return NULL;
        vm->hd_store = store;
    }
    if (!owner) {
        e = &store->entries[0];
    } else {
        for (i = 1; i < HD_TARGETS; ++i) {
            hd_entry *candidate = &store->entries[i];
            if (candidate->key == s->pixels) { e = candidate; break; }
            if (!candidate->key && !e) e = candidate;
        }
    }
    if (!e) return NULL;
    if (e->key != s->pixels || e->width != s->width || e->height != s->height ||
        e->mirror.scale != k) {
        hd_release(vm, e);
        if (!hd_surface(vm, &e->physical, s->width * k, s->height * k)) return NULL;
        e->width = s->width;
        e->height = s->height;
        e->key = s->pixels;
        e->owner = owner;
        e->mirror.target = &e->physical;
        e->mirror.scale = k;
        e->mirror.ud = store;
        e->mirror.lookup = hd_lookup;
    }
    if (!e->valid) hd_seed(e, s);
    return &e->mirror;
}

void nj_midp_hd_invalidate(nj_vm *vm, const nj_surface *s)
{
    int i;
    if (!vm || !vm->hd_store || !s) return;
    for (i = 0; i < HD_TARGETS; ++i)
        if (vm->hd_store->entries[i].key == s->pixels) vm->hd_store->entries[i].valid = 0;
}

void nj_midp_hd_collect(nj_vm *vm)
{
    int i;
#if NJ_ENABLE_M3G
    if (vm->m3g_hd_state) nj_m3g_hd_collect(vm);
#endif
    if (!vm->hd_store) return;
    for (i = 1; i < HD_TARGETS; ++i) {
        hd_entry *e = &vm->hd_store->entries[i];
        if (e->owner && !e->owner->mark) hd_release(vm, e);
    }
}

void nj_midp_hd_dispose(nj_vm *vm)
{
    int i;
#if NJ_ENABLE_M3G
    if (vm->m3g_hd_state) nj_m3g_hd_dispose(vm);
#endif
    if (!vm->hd_store) return;
    for (i = 0; i < HD_TARGETS; ++i) hd_release(vm, &vm->hd_store->entries[i]);
    hd_free_surface(vm, &vm->hd_store->resolve);
    nj_host_free(vm, vm->hd_store);
    vm->hd_store = NULL;
}

/* Two byte lanes fit four samples apiece without carrying into each other. */
static nj_u4 hd_average(nj_u4 a, nj_u4 b, nj_u4 c, nj_u4 d)
{
    const nj_u4 mask = 0x00ff00ffu, round = 0x00020002u;
    nj_u4 rb = (a & mask) + (b & mask) + (c & mask) + (d & mask) + round;
    nj_u4 ag = ((a >> 8) & mask) + ((b >> 8) & mask) + ((c >> 8) & mask) +
               ((d >> 8) & mask) + round;
    return ((rb >> 2) & mask) | (((ag >> 2) & mask) << 8);
}

int nj_midp_hd_present(nj_vm *vm)
{
    nj_render_mirror *mirror = nj_midp_hd_target(vm, &vm->screen);
    const nj_surface *frame;
    if (!mirror || !vm->port || !vm->port->display_hd) return 0;
    frame = mirror->target;
    if (vm->render_quality.ssaa == 2) {
        nj_surface *out = &vm->hd_store->resolve;
        int x, y;
        if (!hd_surface(vm, out, frame->width / 2, frame->height / 2)) return 0;
        for (y = 0; y < out->height; ++y) {
            const nj_u4 *top = (const nj_u4 *)frame->pixels + (nj_size)y * 2 * frame->width;
            const nj_u4 *bottom = top + frame->width;
            nj_u4 *dst = (nj_u4 *)out->pixels + (nj_size)y * out->width;
            for (x = 0; x < out->width; ++x)
                dst[x] = hd_average(top[2*x], top[2*x+1], bottom[2*x], bottom[2*x+1]);
        }
        frame = out;
    }
    vm->port->display_hd(vm->port->ud, frame);
    return 1;
}
