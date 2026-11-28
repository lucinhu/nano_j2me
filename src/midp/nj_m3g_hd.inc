/* Host-only presentation depth. Active Graphics3D bindings have independent
 * lifetimes, even if they address the same MIDP target. A released binding
 * leaves one reusable allocation. The collector removes dead weak owners
 * before sweep, so a recycled object address cannot inherit its depth. */
#define M3G_HD_DEPTH_SLOTS 32
#define M3G_HD_DEPTH_LIMIT ((nj_size)96 * 1024u * 1024u)
typedef struct m3g_hd_depth {
    nj_ref owner;
    int width, height, fresh;
    nj_s4 *keys;
} m3g_hd_depth;

typedef struct {
    m3g_hd_depth depths[M3G_HD_DEPTH_SLOTS];
    nj_size depth_bytes;
    void *temporal;
} m3g_hd_state;

static m3g_hd_state *m3g_hd_get(nj_vm *vm)
{
    m3g_hd_state *s = vm->m3g_hd_state;
    if (!s) {
        s = nj_host_alloc(vm, sizeof(*s));
        if (s) memset(s, 0, sizeof(*s));
        vm->m3g_hd_state = s;
    }
    return s;
}

#if NJ_ENABLE_FSR2
static void **m3g_temporal_slot(nj_vm *vm)
{
    m3g_hd_state *s = m3g_hd_get(vm);
    return s ? &s->temporal : NULL;
}
static void m3g_temporal_dispose(nj_vm *vm);
#endif

static void m3g_hd_free_depth(nj_vm *vm, m3g_hd_state *state, m3g_hd_depth *depth)
{
    if (depth->keys) {
        state->depth_bytes -= (nj_size)depth->width * depth->height * sizeof(nj_s4);
        nj_host_free(vm, depth->keys);
    }
    memset(depth, 0, sizeof(*depth));
}

void nj_m3g_hd_collect(nj_vm *vm)
{
    m3g_hd_state *state = vm->m3g_hd_state;
    if (!state) return;
    for (int i = 0; i < M3G_HD_DEPTH_SLOTS; ++i) {
        m3g_hd_depth *depth = &state->depths[i];
        if (depth->owner && !depth->owner->mark) m3g_hd_free_depth(vm, state, depth);
    }
}

static void m3g_hd_depth_release(nj_vm *vm, nj_ref self)
{
    m3g_hd_state *state = vm->m3g_hd_state;
    if (!state) return;
    for (int i = 0; i < M3G_HD_DEPTH_SLOTS; ++i) {
        m3g_hd_depth *depth = &state->depths[i];
        if (depth->owner != self) continue;
        if (i == 0) {
            depth->owner = NULL;
            depth->fresh = 1;
        } else m3g_hd_free_depth(vm, state, depth);
        return;
    }
}

static m3g_hd_depth *m3g_hd_depth_get(nj_vm *vm, nj_ref owner, const nj_surface *surf)
{
    m3g_hd_state *state = m3g_hd_get(vm);
    m3g_hd_depth *depth = NULL;
    nj_u8 bytes = (nj_u8)surf->width * surf->height * sizeof(nj_s4);
    if (!state || !owner || !bytes || bytes > M3G_HD_DEPTH_LIMIT) return NULL;
    for (int i = 0; i < M3G_HD_DEPTH_SLOTS; ++i) {
        m3g_hd_depth *entry = &state->depths[i];
        if (entry->owner == owner) { depth = entry; break; }
        if (!entry->owner && !depth) depth = entry;
    }
    if (!depth) return NULL;
    if (!depth->keys || depth->width != surf->width || depth->height != surf->height) {
        m3g_hd_free_depth(vm, state, depth);
        if (bytes > M3G_HD_DEPTH_LIMIT - state->depth_bytes) return NULL;
        depth->keys = nj_host_alloc(vm, (nj_size)bytes);
        if (!depth->keys) return NULL;
        state->depth_bytes += (nj_size)bytes;
        depth->width = surf->width;
        depth->height = surf->height;
        depth->fresh = 1;
    }
    depth->owner = owner;
    return depth;
}

void nj_m3g_hd_dispose(nj_vm *vm)
{
    m3g_hd_state *s = vm ? vm->m3g_hd_state : NULL;
    if (!s) return;
#if NJ_ENABLE_FSR2
    m3g_temporal_dispose(vm);
#endif
    for (int i = 0; i < M3G_HD_DEPTH_SLOTS; ++i)
        m3g_hd_free_depth(vm, s, &s->depths[i]);
    nj_host_free(vm, s);
    vm->m3g_hd_state = NULL;
}

static void m3g_hd_clear_depth(nj_vm *vm, nj_ref self, int x0, int y0,
                               int x1, int y1, const nj_render_mirror *mirror)
{
    m3g_hd_state *s = vm->m3g_hd_state;
    if (!s) return;
    for (int i = 0; i < M3G_HD_DEPTH_SLOTS; ++i) {
        m3g_hd_depth *d = &s->depths[i];
        if (d->owner != self || !d->keys || d->fresh ||
            d->width != mirror->target->width || d->height != mirror->target->height) continue;
        for (int y = y0; y < y1; ++y)
            memset(d->keys + (size_t)y * d->width + x0, 0, (size_t)(x1-x0) * sizeof(*d->keys));
        ++vm->hd_depth_epoch;
        return;
    }
}
