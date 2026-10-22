/*
 * nano_j2me - nj_save.c
 *
 * Save and restore the complete state of a running VM: "save state" for a
 * J2ME MIDlet, so a handset (or an emulator) can put a game away mid-level
 * and pick it up later in a brand new process.
 *
 * WHAT IS SAVED
 * -------------
 * The Java world of this runtime is almost entirely self-contained:
 *
 *   - every Java object lives in ONE contiguous arena (nj_heap), allocated
 *     linearly and preceded by a 16-byte nj_block header.  The arena image is
 *     therefore byte-copyable, and the only things that need fixing up inside
 *     it are pointers: each object's nj_class, each NJ_OBJ_CLASS mirror, and
 *     the reference-typed slots of every object, array, wrapped String,
 *     static field and frame local/operand slot.
 *   - classes are re-created from the JAR the caller attaches to the fresh VM,
 *     so the blob stores class NAMES and per-class ABI numbers, never host
 *     pointers.  A blob whose classes do not match the attached JAR (or whose
 *     field offsets / static slot assignment differ) is refused rather than
 *     half-applied.
 *   - everything that is a pure host derivation - the constant-pool cache, the
 *     nj_class_find and nj_field_lookup memos, the cached well-known classes,
 *     class file bytes, the resolve/link failure strings - is dropped on load
 *     and rebuilt on demand.
 *
 * WHAT IS NOT SAVED (and why)
 * ---------------------------
 * Native handles owned by the port - sockets, Bluetooth connections, sensor
 * channels, the audio device, the framebuffer - are not representable in a
 * process-independent blob, and the core cannot even enumerate them: the
 * Android port keeps them in Java fields as ids into its own tables.  A
 * restored VM therefore has DEAD native handles, and the port must drop them
 * (close sockets, stop playback, re-open sensors) before letting the MIDlet run
 * again.  nj_vm_can_save() refuses a snapshot while a native call is on the C
 * stack, which is what makes a saved blob resumable at all.
 *
 * BLOB FORMAT (v1, little-endian, one pass, CRC32 at the end)
 * -----------------------------------------------------------
 *   header        176 bytes, fixed layout, NOT covered by the CRC (every field
 *                 in it is range-checked before use)
 *   class table   nclasses entries: u16 name length + name bytes,
 *                 u16 instance_size, u16 field_count, u16 method_count,
 *                 u16 is_array, u16 state, u16 pad, u32 statics_base
 *   statics       nstatics * 8 bytes, verbatim
 *   arena         heap bump bytes, verbatim
 *   interned      ninterned * u32 arena offsets (0xFFFFFFFF = NULL)
 *   objects       per live arena block: u16 class index, plus u16 mirror class
 *                 index when the object is an NJ_OBJ_CLASS
 *   records A     ref SLOT fixups for statics, VM refs, class mirrors,
 *                 object/array/String fields - terminated by kind 0
 *   threads       per thread: scalars, stack image, frames
 *   records B     ref SLOT fixups for thread refs and frame stack slots,
 *                 terminated by kind 0
 *   crc32         u32 over everything above except the header and itself
 *
 * A ref slot is stored as (arena offset of the slot, arena offset of the
 * target), so a load is a rebase: value = new_base + offset.  Offsets are
 * 32-bit, which bounds the arena at 4 GiB (NJ_MAX_HEAP is 16 MiB).
 */
#include "nj_internal.h"

#include <stdio.h>
#include <stdlib.h>   /* getenv, for NJ_SAVE_DEBUG */
#include <string.h>

/* Restore walks all objects: unstarted Players are absent from the audio pump
 * registry but can still own native MIDI streams.  No saved handle may reach
 * resumed Java code or the first GC sweep. */
static int restore_native_object(nj_vm *vm, nj_ref object, nj_u4 offset, void *ud)
{
    (void)offset;
    (void)ud;
    nj_media_restore_object(vm, object);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Blob constants                                                      */
/* ------------------------------------------------------------------ */
#define NJ_SAVE_MAGIC     0x53534A4Eu   /* 'N' 'J' 'S' 'S' */
/*
 * 2: the image grew a live frame's operand slots above the allocation
 * frontier (see save_threads and the NJ_SR_STACK bound in apply_recordsB).
 * A version-1 blob cannot be repaired - it simply does not contain those
 * slots - so it is refused instead of restored into a program running on
 * zeros.
 */
#define NJ_SAVE_VERSION   2u
#define NJ_SAVE_ENDIAN    0x01020304u
#define NJ_SAVE_HDR_SIZE  176u
/* Explicit heaps may exceed the library default. Loading may use that
 * caller-selected capacity, but cannot request an arbitrary larger arena. */
#define NJ_SAVE_HEAP_MAX(vm) ((nj_u8)(vm)->heap.size > (nj_u8)NJ_MAX_HEAP \
                             ? (nj_u8)(vm)->heap.size : (nj_u8)NJ_MAX_HEAP)
/* the largest blob worth believing: arena + stacks + statics + records */
#define NJ_SAVE_BLOB_MAX(vm) (NJ_SAVE_HEAP_MAX(vm) + (nj_u8)NJ_MAX_THREADS * (nj_u8)NJ_MAX_STACK_SLOTS * 8u + 4u * 1024u * 1024u)
#define NJ_SAVE_NONE      0xFFFFFFFFu   /* NULL arena offset / no thread   */
#define NJ_SAVE_NO_CLASS  0xFFFFu       /* NULL nj_class                   */
#define NJ_SAVE_NAME_MAX  1024u

/* Ref-slot record kinds.  Records A and records B are two sequences, each
 * terminated by NJ_SR_END. */
#define NJ_SR_END        0u
#define NJ_SR_STATIC     1u   /* a = static slot index,  v = target       */
#define NJ_SR_VM         2u   /* a = NJ_SV_*,            v = target       */
#define NJ_SR_CLASS_OBJ  3u   /* a = class index,        v = target       */
#define NJ_SR_OBJ        4u   /* a = slot offset,        v = target       */
#define NJ_SR_METHOD     5u   /* b = method index,       v = class index  */
#define NJ_SR_THREAD     6u   /* a = thread index, b = NJ_ST_*, v = target */
#define NJ_SR_STACK      7u   /* a = thread index, b = slot index, v = target */

/* VM-held refs (NJ_SR_VM).  Every nj_ref the collector roots must be here. */
#define NJ_SV_EXCEPTION           0u
#define NJ_SV_APP                 1u
#define NJ_SV_DISPLAY             2u
#define NJ_SV_CURRENT_DISPLAYABLE 3u
#define NJ_SV_THREAD_GROUP        4u
#define NJ_SV_MEDIA_PLAYERS       5u
#define NJ_SV_SENSOR_CONNS        6u
#define NJ_SV_BT_JOBS             7u
#define NJ_SV_COUNT               8u

/* Thread-held refs (NJ_SR_THREAD). */
#define NJ_ST_OBJ          0u
#define NJ_ST_TARGET       1u
#define NJ_ST_ENTRY_THIS   2u
#define NJ_ST_NAME         3u
#define NJ_ST_PENDING_EXC  4u
#define NJ_ST_WAIT_MONITOR 5u
#define NJ_ST_COUNT        6u

/* ------------------------------------------------------------------ */
/* CRC32 (IEEE, nibble table - const, so nothing lands in .data)       */
/* ------------------------------------------------------------------ */
static const nj_u4 nj_crc_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

static nj_u4 nj_crc_update(nj_u4 crc, const nj_u1 *p, nj_size n)
{
    nj_size i;

    crc = ~crc;
    for (i = 0u; i < n; i++) {
        crc ^= (nj_u4)p[i];
        crc = (crc >> 4) ^ nj_crc_nibble[crc & 0x0Fu];
        crc = (crc >> 4) ^ nj_crc_nibble[crc & 0x0Fu];
    }
    return ~crc;
}

/* ------------------------------------------------------------------ */
/* Little-endian encoding helpers                                      */
/* ------------------------------------------------------------------ */
static void put_u16(nj_u1 *p, nj_u2 v)
{
    p[0] = (nj_u1)(v & 0xFFu);
    p[1] = (nj_u1)((v >> 8) & 0xFFu);
}

static void put_u32(nj_u1 *p, nj_u4 v)
{
    p[0] = (nj_u1)(v & 0xFFu);
    p[1] = (nj_u1)((v >> 8) & 0xFFu);
    p[2] = (nj_u1)((v >> 16) & 0xFFu);
    p[3] = (nj_u1)((v >> 24) & 0xFFu);
}

static void put_u64(nj_u1 *p, nj_u8 v)
{
    put_u32(p, (nj_u4)(v & 0xFFFFFFFFu));
    put_u32(p + 4, (nj_u4)(v >> 32));
}

static nj_u2 get_u16(const nj_u1 *p)
{
    return (nj_u2)((nj_u4)p[0] | ((nj_u4)p[1] << 8));
}

static nj_u4 get_u32(const nj_u1 *p)
{
    return (nj_u4)p[0] | ((nj_u4)p[1] << 8) |
           ((nj_u4)p[2] << 16) | ((nj_u4)p[3] << 24);
}

static nj_u8 get_u64(const nj_u1 *p)
{
    return (nj_u8)get_u32(p) | ((nj_u8)get_u32(p + 4) << 32);
}

/* ------------------------------------------------------------------ */
/* Writer: streams to a FILE, keeps a running CRC of the body          */
/* ------------------------------------------------------------------ */
typedef struct nj_swriter {
    FILE  *f;
    nj_u4  crc;
    int    err;
} nj_swriter;

static int sw_raw(nj_swriter *w, const void *p, nj_size n)
{
    if (w->err) {
        return 0;
    }
    if (n != 0u && fwrite(p, 1, (size_t)n, w->f) != (size_t)n) {
        w->err = 1;
        return 0;
    }
    return 1;
}

/* Body bytes: also folded into the CRC. */
static int sw_body(nj_swriter *w, const void *p, nj_size n)
{
    if (!sw_raw(w, p, n)) {
        return 0;
    }
    w->crc = nj_crc_update(w->crc, (const nj_u1 *)p, n);
    return 1;
}

static int sw_u8(nj_swriter *w, nj_u1 v)
{
    return sw_body(w, &v, 1u);
}

static int sw_u16(nj_swriter *w, nj_u2 v)
{
    nj_u1 b[2];
    put_u16(b, v);
    return sw_body(w, b, 2u);
}

static int sw_u32(nj_swriter *w, nj_u4 v)
{
    nj_u1 b[4];
    put_u32(b, v);
    return sw_body(w, b, 4u);
}

static int sw_u64(nj_swriter *w, nj_u8 v)
{
    nj_u1 b[8];
    put_u64(b, v);
    return sw_body(w, b, 8u);
}

static int sw_record(nj_swriter *w, nj_u1 kind, nj_u4 a, nj_u4 b, nj_u4 v)
{
    nj_u1 rec[16];

    memset(rec, 0, sizeof(rec));
    rec[0] = kind;
    put_u32(rec + 4, a);
    put_u32(rec + 8, b);
    put_u32(rec + 12, v);
    return sw_body(w, rec, 16u);
}

/* ------------------------------------------------------------------ */
/* Reader: the whole blob is buffered first, so the CRC is verified    */
/* before a single field is trusted.  The file must be seekable.       */
/* ------------------------------------------------------------------ */
typedef struct nj_sreader {
    const nj_u1 *p;
    nj_size      len;
    nj_size      pos;
    int          err;
} nj_sreader;

static int sr_raw(nj_sreader *r, void *dst, nj_size n)
{
    if (r->err || n > r->len - r->pos) {
        r->err = 1;
        return 0;
    }
    if (dst) {
        memcpy(dst, r->p + r->pos, (size_t)n);
    }
    r->pos += n;
    return 1;
}

static int sr_u8(nj_sreader *r, nj_u1 *out)
{
    return sr_raw(r, out, 1u);
}

static int sr_u16(nj_sreader *r, nj_u2 *out)
{
    nj_u1 b[2];
    if (!sr_raw(r, b, 2u)) {
        return 0;
    }
    *out = get_u16(b);
    return 1;
}

static int sr_u32(nj_sreader *r, nj_u4 *out)
{
    nj_u1 b[4];
    if (!sr_raw(r, b, 4u)) {
        return 0;
    }
    *out = get_u32(b);
    return 1;
}

static int sr_u64(nj_sreader *r, nj_u8 *out)
{
    nj_u1 b[8];
    if (!sr_raw(r, b, 8u)) {
        return 0;
    }
    *out = get_u64(b);
    return 1;
}

/* ------------------------------------------------------------------ */
/* ABI hash: the class layout a blob was taken with                    */
/* ------------------------------------------------------------------ */
/*
 * Class identity is the class NAME, but a restore also depends on the LAYOUT
 * the JAR produced: instance field offsets, static slot assignment (fixed when
 * the class is registered), method order for frame re-resolution.  One FNV-1a
 * hash over all of it turns "the JAR changed under us" into a clean
 * NJ_ERR_FORMAT instead of a MIDlet that runs with the wrong field offsets.
 */
static nj_u4 hash_bytes(nj_u4 h, const void *p, nj_size n)
{
    const nj_u1 *b = (const nj_u1 *)p;
    nj_size i;

    for (i = 0u; i < n; i++) {
        h ^= (nj_u4)b[i];
        h *= 16777619u;
    }
    return h;
}

static nj_u4 hash_str(nj_u4 h, const char *s)
{
    if (!s) {
        return hash_bytes(h, "", 1u);
    }
    return hash_bytes(h, s, (nj_size)strlen(s));
}

static nj_u4 hash_u32(nj_u4 h, nj_u4 v)
{
    nj_u1 b[4];
    put_u32(b, v);
    return hash_bytes(h, b, 4u);
}

static nj_u4 class_abi_hash(nj_u4 h, nj_class *c)
{
    nj_u2 i;

    if (!c) {
        return hash_u32(hash_str(h, "<null>"), 0u);
    }
    h = hash_str(h, c->name);
    h = hash_u32(h, c->instance_size);
    h = hash_u32(h, c->field_count);
    h = hash_u32(h, c->method_count);
    h = hash_u32(h, c->is_array);
    for (i = 0u; i < c->field_count; i++) {
        nj_field *fd = &c->fields[i];
        h = hash_str(h, fd->name);
        h = hash_str(h, fd->desc);
        h = hash_u32(h, ((nj_u4)fd->flags << 16) | fd->offset);
        h = hash_u32(h, ((nj_u4)fd->ptype << 8) | fd->width);
    }
    for (i = 0u; i < c->method_count; i++) {
        nj_method *m = &c->methods[i];
        h = hash_str(h, m->name);
        h = hash_str(h, m->desc);
        h = hash_u32(h, ((nj_u4)m->max_locals << 16) | m->max_stack);
        h = hash_u32(h, ((nj_u4)m->arg_slots << 8) | m->ret_ptype);
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static nj_ref ref_at(nj_vm *vm, nj_u4 off)
{
    if (off == NJ_SAVE_NONE) {
        return NULL;
    }
    return (nj_ref)(void *)(vm->heap.base + off);
}

/* Arena offset of a pointer known to live in the arena, or NJ_SAVE_NONE. */
static nj_u4 ref_off(nj_vm *vm, nj_ref r)
{
    if (!r) {
        return NJ_SAVE_NONE;
    }
    return (nj_u4)((nj_u1 *)(void *)r - vm->heap.base);
}

static nj_u2 class_index_of(nj_vm *vm, const nj_class *c)
{
    nj_u2 i;

    if (!c) {
        return NJ_SAVE_NO_CLASS;
    }
    for (i = 0u; i < vm->nclasses; i++) {
        if (vm->classes[i] == c) {
            return i;
        }
    }
    return NJ_SAVE_NO_CLASS;
}

static int method_index_of(nj_class *c, const nj_method *m, nj_u2 *out)
{
    nj_u2 i;

    if (!c || !m) {
        return 0;
    }
    for (i = 0u; i < c->method_count; i++) {
        if (&c->methods[i] == m) {
            *out = i;
            return 1;
        }
    }
    return 0;
}

/* Is this pointer a live Java object of the VM's arena? */
static int is_live_obj(nj_vm *vm, nj_ref r)
{
    if (!r) {
        return 0;
    }
    return nj_heap_owns_object(vm, (const void *)(void *)r);
}

static int slot_in_thread(const nj_thread *t, const nj_value *p)
{
    return p && p >= t->stack_mem && p < t->stack_end;
}

/* ------------------------------------------------------------------ */
/* Arena walk                                                          */
/* ------------------------------------------------------------------ */
typedef int (*nj_obj_fn)(nj_vm *vm, nj_object *o, nj_u4 off, void *ud);

/*
 * Walk the arena block by block, in the same order nj_gc_sweep does, and hand
 * every LIVE object (block magic intact) to `fn`.  A blob is a copy of this
 * walk, so a corrupt heap must be refused here rather than written out.
 */
static nj_status arena_walk(nj_vm *vm, nj_obj_fn fn, void *ud, nj_u4 *out_count)
{
    nj_u1 *p = vm->heap.base;
    nj_u1 *end = vm->heap.base + vm->heap.bump;
    nj_u4 count = 0u;

    while (p < end) {
        nj_block *b = (nj_block *)(void *)p;
        nj_size sz = b->size;

        if (sz < NJ_BLOCK_HDR || (sz & (NJ_OBJ_ALIGN - 1u)) != 0u ||
            sz > (nj_size)(end - p)) {
            return NJ_ERR_STATE;      /* corrupt block chain */
        }
        if (b->magic == NJ_BLOCK_MAGIC) {
            nj_object *o = (nj_object *)(void *)(p + NJ_BLOCK_HDR);

            if (o->size < sizeof(nj_object) ||
                (nj_size)o->size > sz - NJ_BLOCK_HDR ||
                o->kind > NJ_OBJ_THROW) {
                return NJ_ERR_STATE;  /* a live block that is not an object */
            }
            if (fn && !fn(vm, o, (nj_u4)(p + NJ_BLOCK_HDR - vm->heap.base), ud)) {
                return NJ_ERR_STATE;
            }
            count++;
        }
        p += sz;
    }
    if (p != end) {
        return NJ_ERR_STATE;
    }
    if (out_count) {
        *out_count = count;
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* nj_vm_can_save                                                      */
/* ------------------------------------------------------------------ */
int nj_vm_can_save(const nj_vm *vm_c)
{
    /* Read-only inspection; the helpers that take nj_vm* do not modify it. */
    nj_vm *vm = (nj_vm *)(void *)(uintptr_t)vm_c;
    nj_u2 t;

    if (!vm || !vm->heap.base || !vm->statics) {
        return 0;                     /* no arena / no static slots */
    }
    if (vm->heap.size < 4096u || vm->heap.bump > vm->heap.size) {
        return 0;                     /* unusable or corrupt heap */
    }
    /*
     * An external arena belongs to the embedder (nj_heap_init with a block).
     * A load installs an arena of its own, so refusing here keeps the port's
     * block and the snapshot from disagreeing about who owns the bytes.
     */
    if (vm->heap.external) {
        return 0;
    }
    /*
     * A native call means a C stack frame is live between the Java frames we
     * would save and the code that would resume them.  Nothing in the blob can
     * represent it, so this is the one refusal that cannot be worked around.
     */
    if (vm->native_depth != 0u) {
        return 0;
    }
    /* Re-entrancy guards: the VM is inside its own paint/hook/pump. */
    if (vm->in_paint || vm->in_frame_hook || vm->in_call_pump ||
        vm->in_exception_new) {
        return 0;
    }
    /* A temporary C root means C code holds a ref across a call: that ref
     * would have nowhere to live after a restore. */
    if (vm->nroots != 0u) {
        return 0;
    }
    /* gc_disabled is set while a native holds unrooted refs. */
    if (vm->gc_disabled) {
        return 0;
    }
    if (vm->nthreads > (nj_u2)NJ_MAX_THREADS ||
        vm->nstatics > (nj_u4)NJ_MAX_STATICS ||
        vm->ninterned > (nj_u2)NJ_MAX_INTERNED ||
        vm->nclasses > (nj_u2)NJ_MAX_CLASSES) {
        return 0;
    }
    for (t = 0u; t < vm->nthreads; t++) {
        const nj_thread *th = vm->threads[t];
        int f;

        if (!th) {
            continue;
        }
        if (th->in_c_call) {
            return 0;                 /* parked in a nested C call */
        }
        if (!th->stack_mem || !th->stack_end || th->top < th->stack_mem ||
            th->top > th->stack_end) {
            return 0;
        }
        if (th->nframes < 0 || th->nframes > NJ_MAX_FRAMES) {
            return 0;
        }
        for (f = 0; f < th->nframes; f++) {
            const nj_frame *fr = &th->frames[f];

            /*
             * A native frame is a C function waiting to return; the C stack
             * behind it belongs to this thread, and no blob can carry it.  At
             * a frame boundary there is normally none.
             */
            if (fr->is_native) {
                return 0;
            }
            if (!slot_in_thread(th, fr->locals) ||
                !slot_in_thread(th, fr->stack) ||
                !slot_in_thread(th, fr->sp) ||
                !slot_in_thread(th, fr->stack_limit) ||
                !slot_in_thread(th, fr->argbase)) {
                return 0;
            }
            if (fr->sp < fr->locals || fr->stack_limit < fr->stack) {
                return 0;
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Save: section writers                                               */
/* ------------------------------------------------------------------ */
typedef struct nj_save_ctx {
    nj_swriter *w;
    nj_vm      *vm;
} nj_save_ctx;

static int save_object_cb(nj_vm *vm, nj_object *o, nj_u4 off, void *ud)
{
    nj_save_ctx *ctx = (nj_save_ctx *)ud;
    nj_u2 cls_idx = class_index_of(vm, o->cls);

    (void)off;
    if (o->cls && cls_idx == NJ_SAVE_NO_CLASS) {
        return 0;                     /* an object of a class this VM never
                                       * registered: not representable */
    }
    if (!sw_u16(ctx->w, cls_idx)) {
        return 0;
    }
    if (o->kind == NJ_OBJ_CLASS) {
        nj_u2 mir = class_index_of(vm, o->u.mirror);
        if (o->u.mirror && mir == NJ_SAVE_NO_CLASS) {
            return 0;
        }
        if (!sw_u16(ctx->w, mir)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Collect one reference slot: `slot` is the address of an nj_ref inside the
 * arena.  Anything non-NULL that is not a live object is a corrupt VM, and
 * writing it out would corrupt the loader too.
 */
static int save_slot(nj_swriter *w, nj_vm *vm, const nj_ref *slot)
{
    nj_ref r;

    memcpy(&r, slot, sizeof(r));
    if (!r) {
        return 1;
    }
    if (!is_live_obj(vm, r)) {
        return 0;
    }
    return sw_record(w, NJ_SR_OBJ,
                     (nj_u4)((const nj_u1 *)(const void *)slot - vm->heap.base),
                     NJ_SAVE_NONE, ref_off(vm, r));
}

static int save_object_records_cb(nj_vm *vm, nj_object *o, nj_u4 off, void *ud)
{
    nj_save_ctx *ctx = (nj_save_ctx *)ud;
    nj_swriter *w = ctx->w;

    (void)off;
    switch (o->kind) {
    case NJ_OBJ_ARRAY:
        if (o->aflags == NJ_ARRAY_T_REF && o->u.refs) {
            nj_u4 i;
            if ((nj_size)o->length * (nj_size)sizeof(nj_ref) +
                    (nj_size)sizeof(nj_object) > (nj_size)o->size) {
                return 0;
            }
            for (i = 0u; i < o->length; i++) {
                if (!save_slot(w, vm, &o->u.refs[i])) {
                    return 0;
                }
            }
        }
        break;
    case NJ_OBJ_PLAIN: {
        nj_class *c = o->cls;
        nj_u1 *base = (nj_u1 *)(void *)o;

        while (c) {
            nj_u2 f;
            for (f = 0u; f < c->field_count; f++) {
                nj_field *fd = &c->fields[f];
                nj_u4 need;

                if ((fd->flags & NJ_ACC_STATIC) != 0u ||
                    fd->ptype != NJ_PT_OBJECT) {
                    continue;
                }
                need = (fd->ptype == NJ_PT_LONG || fd->ptype == NJ_PT_DOUBLE)
                           ? 8u : 4u;
                if ((nj_u4)fd->offset + need > o->size) {
                    return 0;         /* a field outside its own object */
                }
                if (!save_slot(w, vm, (nj_ref *)(void *)(base + fd->offset))) {
                    return 0;
                }
            }
            c = c->super;
        }
        break;
    }
    case NJ_OBJ_STRING:
        if ((o->aflags & NJ_STR_WRAPPED) != 0u && o->u.raw) {
            if (!is_live_obj(vm, (nj_ref)o->u.raw)) {
                return 0;
            }
            if (!save_slot(w, vm, (nj_ref *)(void *)&o->u.raw)) {
                return 0;
            }
        }
        break;
    case NJ_OBJ_CLASS:
    default:
        break;                        /* the mirror is a class, not a ref */
    }
    return 1;
}

/* statics + VM refs + class mirrors + the exception method. */
static int save_records_a(nj_vm *vm, nj_swriter *w)
{
    nj_u2 i;
    nj_u4 slot;

    /* Object-typed static fields, addressed the way nj_field_addr_fast does. */
    for (i = 0u; i < vm->nclasses; i++) {
        nj_class *c = vm->classes[i];
        nj_u2 f;

        if (!c) {
            continue;
        }
        for (f = 0u; f < c->field_count; f++) {
            nj_field *fd = &c->fields[f];
            nj_ref r;

            if ((fd->flags & NJ_ACC_STATIC) == 0u ||
                fd->ptype != NJ_PT_OBJECT) {
                continue;
            }
            if ((nj_u4)fd->offset >= vm->nstatics) {
                return 0;
            }
            r = vm->statics[fd->offset].r;
            if (!r) {
                continue;
            }
            if (!is_live_obj(vm, r)) {
                return 0;
            }
            if (!sw_record(w, NJ_SR_STATIC, fd->offset, NJ_SAVE_NONE,
                           ref_off(vm, r))) {
                return 0;
            }
        }
    }
    /* The VM's own refs.  Keep in step with nj_gc_mark_roots(). */
    for (slot = 0u; slot < NJ_SV_COUNT; slot++) {
        nj_ref r = NULL;

        switch (slot) {
        case NJ_SV_EXCEPTION:           r = vm->exception; break;
        case NJ_SV_APP:                 r = vm->app; break;
        case NJ_SV_DISPLAY:             r = vm->display; break;
        case NJ_SV_CURRENT_DISPLAYABLE: r = vm->current_displayable; break;
        case NJ_SV_THREAD_GROUP:        r = vm->thread_group; break;
        case NJ_SV_MEDIA_PLAYERS:       r = vm->media_players; break;
        case NJ_SV_SENSOR_CONNS:        r = vm->sensor_conns; break;
        case NJ_SV_BT_JOBS:             r = vm->bt_jobs; break;
        default: break;
        }
        if (!r) {
            continue;
        }
        if (!is_live_obj(vm, r)) {
            return 0;
        }
        if (!sw_record(w, NJ_SR_VM, slot, NJ_SAVE_NONE, ref_off(vm, r))) {
            return 0;
        }
    }
    /* java.lang.Class mirrors. */
    for (i = 0u; i < vm->nclasses; i++) {
        nj_class *c = vm->classes[i];

        if (c && c->class_obj) {
            if (!is_live_obj(vm, c->class_obj) ||
                !sw_record(w, NJ_SR_CLASS_OBJ, i, NJ_SAVE_NONE,
                           ref_off(vm, c->class_obj))) {
                return 0;
            }
        }
    }
    /* The method a pending exception is being propagated from. */
    if (vm->exception_method) {
        nj_u2 mi = 0u;
        nj_u2 ci = class_index_of(vm, vm->exception_method->owner);

        if (ci == NJ_SAVE_NO_CLASS ||
            !method_index_of(vm->exception_method->owner,
                             vm->exception_method, &mi)) {
            return 0;
        }
        if (!sw_record(w, NJ_SR_METHOD, 0u, mi, ci)) {
            return 0;
        }
    }
    return sw_record(w, NJ_SR_END, 0u, 0u, 0u);
}

static int save_threads(nj_vm *vm, nj_swriter *w)
{
    nj_u2 t;

    for (t = 0u; t < vm->nthreads; t++) {
        nj_thread *th = vm->threads[t];
        nj_u4 used;
        nj_u8 ret;
        int f;

        if (!th || th->top < th->stack_mem || th->top > th->stack_end) {
            return 0;
        }
        used = (nj_u4)(th->top - th->stack_mem);
        if (th->nframes < 0 || th->nframes > NJ_MAX_FRAMES) {
            return 0;
        }
        /*
         * th->top is only the frame-allocation frontier and it can lag BEHIND
         * a live operand stack: nj_frame_pop() sets top to the locals of the
         * frame that just returned, and the caller then pushes operands above
         * it into the same slots.  Saving top bytes only would drop those
         * operands - the restored program then ran on a stack with zeros where
         * its operands were.  The image has to reach the highest slot any live
         * frame can read: its sp, or its locals.
         */
        for (f = 0; f < th->nframes; f++) {
            nj_frame *fr = &th->frames[f];
            nj_u4 need;

            if (!fr->sp || !fr->locals || fr->sp < th->stack_mem ||
                fr->sp > th->stack_end || fr->locals < th->stack_mem ||
                fr->locals > th->stack_end) {
                return 0;
            }
            need = (nj_u4)(fr->sp - th->stack_mem);
            if ((nj_u4)(fr->locals - th->stack_mem) > need) {
                need = (nj_u4)(fr->locals - th->stack_mem);
            }
            if (need > used) {
                used = need;
            }
        }
        if (used > (nj_u4)NJ_MAX_STACK_SLOTS ||
            th->nframes < 0 || th->nframes > NJ_MAX_FRAMES) {
            return 0;
        }
        memcpy(&ret, &th->ret_value, sizeof(ret));
        if (!sw_u32(w, ((nj_u4)th->state) | ((nj_u4)th->daemon << 8) |
                       ((nj_u4)th->started << 16) | ((nj_u4)th->is_main << 24)) ||
            !sw_u32(w, (nj_u4)th->priority) ||
            !sw_u32(w, (nj_u4)th->exit_value) ||
            !sw_u64(w, ret) ||
            !sw_u32(w, (nj_u4)th->stop_depth) ||
            !sw_u32(w, (nj_u4)th->in_c_call) ||
            !sw_u32(w, th->id) ||
            !sw_u64(w, (nj_u8)th->wake_at_ms) ||
            !sw_u64(w, (nj_u8)th->wait_deadline) ||
            !sw_u32(w, th->wakeup_seq) ||
            !sw_u32(w, used) ||
            /*
             * The used stack top, which is also where the live stack image
             * ends: the loader needs it to place th->top before it copies the
             * frames, and reading it back is how a truncated stack image is
             * caught.
             */
            !sw_u32(w, (nj_u4)(th->top - th->stack_mem)) ||
            !sw_u32(w, (nj_u4)th->nframes)) {
            return 0;
        }
        if (used != 0u &&
            !sw_body(w, th->stack_mem, used * (nj_u4)sizeof(nj_value))) {
            return 0;
        }
        for (f = 0; f < th->nframes; f++) {
            nj_frame *fr = &th->frames[f];
            nj_u2 ci = class_index_of(vm, fr->m ? fr->m->owner : NULL);
            nj_u2 mi = 0u;

            if (fr->m) {
                if (ci == NJ_SAVE_NO_CLASS ||
                    !method_index_of(fr->m->owner, fr->m, &mi)) {
                    return 0;
                }
            } else {
                ci = NJ_SAVE_NO_CLASS;
            }
            if (fr->monitor && !is_live_obj(vm, fr->monitor)) {
                return 0;
            }
            if (!sw_u16(w, ci) || !sw_u16(w, mi) ||
                !sw_u32(w, fr->pc) || !sw_u32(w, fr->total_slots) ||
                !sw_u8(w, fr->is_native) || !sw_u8(w, 0u) || !sw_u16(w, 0u) ||
                !sw_u32(w, (nj_u4)(fr->locals - th->stack_mem)) ||
                !sw_u32(w, (nj_u4)(fr->stack - th->stack_mem)) ||
                !sw_u32(w, (nj_u4)(fr->sp - th->stack_mem)) ||
                !sw_u32(w, (nj_u4)(fr->stack_limit - th->stack_mem)) ||
                !sw_u32(w, (nj_u4)(fr->argbase - th->stack_mem)) ||
                !sw_u32(w, fr->monitor ? ref_off(vm, fr->monitor)
                                       : NJ_SAVE_NONE)) {
                return 0;
            }
        }
    }
    return 1;
}

/*
 * Records B: thread refs, then every frame slot that holds a live object.  The
 * frame slots are untyped, so this is the same conservative test the collector
 * uses - a slot the GC would mark must survive the rebase.
 */
static int save_records_b(nj_vm *vm, nj_swriter *w)
{
    nj_u2 t;

    for (t = 0u; t < vm->nthreads; t++) {
        nj_thread *th = vm->threads[t];
        nj_u4 slot;
        int f;

        if (!th) {
            continue;
        }
        for (slot = 0u; slot < NJ_ST_COUNT; slot++) {
            nj_ref r = NULL;

            switch (slot) {
            case NJ_ST_OBJ:          r = th->obj; break;
            case NJ_ST_TARGET:       r = th->target; break;
            case NJ_ST_ENTRY_THIS:   r = th->entry_this; break;
            case NJ_ST_NAME:         r = th->name; break;
            case NJ_ST_PENDING_EXC:  r = th->pending_exc; break;
            case NJ_ST_WAIT_MONITOR: r = th->wait_monitor; break;
            default: break;
            }
            if (!r) {
                continue;
            }
            if (!is_live_obj(vm, r)) {
                return 0;
            }
            if (!sw_record(w, NJ_SR_THREAD, t, slot, ref_off(vm, r))) {
                return 0;
            }
        }
        for (f = 0; f < th->nframes; f++) {
            nj_frame *fr = &th->frames[f];
            nj_value *end;
            nj_value *p;

            end = fr->stack_limit ? fr->stack_limit
                                  : fr->stack + (fr->m ? fr->m->max_stack : 0u);
            if (end < fr->sp) {
                end = fr->sp;
            }
            if (end > fr->sp) {
                /*
                 * The live operand stack ends at this frame's own sp, NOT at
                 * th->top.  top is only the frame-allocation frontier and
                 * nj_frame_pop() deliberately leaves it behind: it points at
                 * the locals of the frame that just returned, and the caller
                 * then pushes fresh operands above it.  Clamping to top
                 * dropped the record that translates exactly those operands,
                 * so a restored run read an object pointer out of the saving
                 * process's address space and took SIGSEGV inside nj_exec.
                 */
                end = fr->sp;
            }
            for (p = fr->locals; p < end; p++) {
                if (!is_live_obj(vm, p->r)) {
                    continue;
                }
                if (!sw_record(w, NJ_SR_STACK, t,
                               (nj_u4)(p - th->stack_mem), ref_off(vm, p->r))) {
                    return 0;
                }
            }
        }
    }
    return sw_record(w, NJ_SR_END, 0u, 0u, 0u);
}

/* ------------------------------------------------------------------ */
/* nj_vm_save                                                          */
/* ------------------------------------------------------------------ */
static void save_header(nj_u1 *h, nj_vm *vm, nj_u4 abi, nj_u4 arena_crc,
                        nj_u8 midlet_ms)
{
    nj_u4 t;

    memset(h, 0, NJ_SAVE_HDR_SIZE);
    put_u32(h + 0, NJ_SAVE_MAGIC);
    put_u32(h + 4, NJ_SAVE_VERSION);
    put_u32(h + 8, NJ_SAVE_HDR_SIZE);
    put_u32(h + 12, (nj_u4)sizeof(void *));
    put_u32(h + 16, NJ_SAVE_ENDIAN);
    put_u32(h + 20, vm->motorola_softkeys ? 1u : 0u); /* Canvas soft keys */
    put_u64(h + 24, (nj_u8)vm->heap.size);
    put_u64(h + 32, (nj_u8)vm->heap.bump);
    put_u64(h + 40, (nj_u8)vm->heap.used);
    put_u64(h + 48, (nj_u8)vm->heap.peak);
    put_u64(h + 56, (nj_u8)(nj_size)(uintptr_t)vm->heap.base);  /* info */
    put_u32(h + 64, arena_crc);                   /* info, over the image */
    put_u32(h + 68, vm->nclasses);
    put_u32(h + 72, vm->nstatics);
    put_u32(h + 76, vm->ninterned);
    put_u32(h + 80, vm->nthreads);
    put_u32(h + 84, NJ_SAVE_NONE);                /* current thread */
    if (vm->cur) {
        for (t = 0u; t < vm->nthreads; t++) {
            if (vm->threads[t] == vm->cur) {
                put_u32(h + 84, t);
                break;
            }
        }
    }
    put_u32(h + 88, vm->screen_w);
    put_u32(h + 92, vm->screen_h);
    put_u32(h + 96, (nj_u4)vm->screen.format);
    put_u32(h + 100, vm->frames_presented);
    put_u32(h + 104, (nj_u4)vm->speed_percent);
    put_u32(h + 108, vm->midp_flags);
    put_u32(h + 112, abi);
    put_u32(h + 116, ((nj_u4)vm->repaint_pending) |
                     ((nj_u4)vm->painted_in_frame << 8) |
                     ((nj_u4)vm->state << 16) |
                     ((nj_u4)vm->running << 24));
    put_u64(h + 120, midlet_ms);                  /* saved MIDlet epoch     */
    put_u64(h + 128, (nj_u8)vm->scheduler_ticks); /* the folded port clock  */
    put_u64(h + 136, (nj_u8)vm->speed_virtual);
    put_u64(h + 144, (nj_u8)vm->speed_real);
    put_u64(h + 152, (nj_u8)vm->halt_at_ms);
    put_u32(h + 160, vm->exception_pc);
    put_u32(h + 164, class_index_of(vm, vm->app_class));
    put_u32(h + 168, vm->next_thread_id);
    put_u32(h + 172, 0u);
}

int nj_vm_save(nj_vm *vm, FILE *f)
{
    nj_u1 hdr[NJ_SAVE_HDR_SIZE];
    nj_swriter w;
    nj_save_ctx ctx;
    nj_u4 abi = 2166136261u;
    nj_u4 arena_crc;
    nj_u4 objects = 0u;
    nj_u2 i;
    nj_u4 slot;
    nj_u8 midlet_ms;
    nj_status st;

    if (!vm || !f) {
        return NJ_ERR;
    }
    if (!nj_vm_can_save(vm)) {
        return NJ_ERR_STATE;
    }
    if (vm->heap.bump > vm->heap.size || vm->heap.bump < NJ_BLOCK_HDR ||
        vm->nstatics > (nj_u4)NJ_MAX_STATICS ||
        vm->ninterned > (nj_u2)NJ_MAX_INTERNED ||
        vm->nclasses > (nj_u2)NJ_MAX_CLASSES) {
        return NJ_ERR_STATE;
    }
    /* Pre-flight: the heap must walk cleanly before a single byte is written. */
    st = arena_walk(vm, NULL, NULL, &objects);
    if (st != NJ_OK) {
        return st;
    }
    for (i = 0u; i < vm->nclasses; i++) {
        nj_class *c = vm->classes[i];
        /*
         * Only a linked class has a layout worth checking: an unlinked one has
         * instance_size 0 and no static slot numbers yet, so the two sides of
         * a save/load could never agree on its numbers.  It holds nothing in
         * the image either - the loader re-links it lazily when the restored
         * program actually asks for it.
         */
        if (c && c->state >= NJ_CLASS_LINKED) {
            abi = class_abi_hash(abi, c);
        }
    }
    arena_crc = nj_crc_update(0u, vm->heap.base, vm->heap.bump);
    /* Reading the clock folds the port's millis into scheduler_ticks; do it
     * before the header is built so the saved clock is the fresh one. */
    midlet_ms = (nj_u8)nj_now_ms(vm);

    save_header(hdr, vm, abi, arena_crc, midlet_ms);

    w.f = f;
    w.crc = 0u;
    w.err = 0;

    /* Header first, outside the CRC. */
    if (!sw_raw(&w, hdr, NJ_SAVE_HDR_SIZE)) {
        return NJ_ERR;
    }
    /* Class table. */
    for (i = 0u; i < vm->nclasses; i++) {
        nj_class *c = vm->classes[i];
        const char *name = c ? c->name : "";
        nj_size len = (nj_size)strlen(name);

        /*
         * statics_base is the slot this class's statics start at, which is
         * what the loader needs to re-link the classes in the same order and
         * get the same slot assignment.  state (LOADED/LINKED/READY) matters
         * too: a class that had already run <clinit> must not run it again
         * after a restore, or the restored static fields would be reset.
         */
        nj_u4 sbase = NJ_SAVE_NONE;

        /*
         * Only a LINKED class has had its static slots handed out.  A class
         * that was merely loaded when the snapshot was taken still has every
         * field offset at 0, which would make it look like the class owning
         * slots 0..n and send the loader down the wrong link order.  It has
         * no statics and no instances in the image, so it is left for lazy
         * linking, exactly as it was.
         */
        if (c && c->state >= NJ_CLASS_LINKED) {
            nj_u2 f;
            for (f = 0u; f < c->field_count; f++) {
                if ((c->fields[f].flags & NJ_ACC_STATIC) != 0u &&
                    c->fields[f].offset < sbase) {
                    sbase = c->fields[f].offset;
                }
            }
        }
        if (len > 0xFFFFu ||
            !sw_u16(&w, (nj_u2)len) ||
            !sw_body(&w, name, len) ||
            !sw_u16(&w, c ? c->instance_size : 0u) ||
            !sw_u16(&w, c ? c->field_count : 0u) ||
            !sw_u16(&w, c ? c->method_count : 0u) ||
            !sw_u16(&w, c ? c->is_array : 0u) ||
            !sw_u16(&w, c ? c->state : 0u) ||
            !sw_u16(&w, 0u) ||
            !sw_u32(&w, sbase)) {
            return NJ_ERR;
        }
    }
    /* Static slots, verbatim: ints, longs and refs share the array. */
    for (slot = 0u; slot < vm->nstatics; slot++) {
        nj_u8 bits;
        memcpy(&bits, &vm->statics[slot], sizeof(bits));
        if (!sw_u64(&w, bits)) {
            return NJ_ERR;
        }
    }
    /* The arena image. */
    if (!sw_body(&w, vm->heap.base, vm->heap.bump)) {
        return NJ_ERR;
    }
    /* Interned strings. */
    for (i = 0u; i < vm->ninterned; i++) {
        if (!sw_u32(&w, ref_off(vm, vm->interned[i]))) {
            return NJ_ERR;
        }
    }
    /* Per-object class table. */
    ctx.w = &w;
    ctx.vm = vm;
    if (arena_walk(vm, save_object_cb, &ctx, NULL) != NJ_OK) {
        return NJ_ERR_STATE;
    }
    /* The ref slots living inside arena objects (fields, array elements,
     * wrapped-String payloads). */
    if (arena_walk(vm, save_object_records_cb, &ctx, NULL) != NJ_OK) {
        return NJ_ERR_STATE;
    }
    /* Records A. */
    if (!save_records_a(vm, &w)) {
        return NJ_ERR_STATE;
    }
    /* Threads. */
    if (!save_threads(vm, &w)) {
        return NJ_ERR_STATE;
    }
    /* Records B. */
    if (!save_records_b(vm, &w)) {
        return NJ_ERR_STATE;
    }
    /* CRC. */
    {
        nj_u1 tail[4];
        put_u32(tail, w.crc);
        if (!sw_raw(&w, tail, 4u)) {
            return NJ_ERR;
        }
    }
    if (fflush(f) != 0) {
        return NJ_ERR;
    }
    return NJ_OK;
}

int nj_vm_save_file(nj_vm *vm, const char *path)
{
    FILE *f;
    int rc;

    if (!vm || !path) {
        return NJ_ERR;
    }
    f = fopen(path, "wb");
    if (!f) {
        return NJ_ERR;
    }
    rc = nj_vm_save(vm, f);
    if (fclose(f) != 0 && rc == NJ_OK) {
        rc = NJ_ERR;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Load                                                                */
/* ------------------------------------------------------------------ */
static int method_at(nj_vm *vm, nj_u4 cls_idx, nj_u4 meth_idx, nj_method **out)
{
    nj_class *c;

    *out = NULL;
    if (cls_idx == NJ_SAVE_NO_CLASS) {
        return 1;                     /* a frame with no method at all */
    }
    if (cls_idx >= vm->nclasses) {
        return 0;
    }
    c = vm->classes[cls_idx];
    if (!c || meth_idx >= c->method_count) {
        return 0;
    }
    *out = &c->methods[meth_idx];
    return 1;
}

/*
 * Resolve one saved class name.  Array descriptors and the primitive mirrors
 * have to go through the array factory, because a primitive class only comes
 * into existence once its array type has been asked for.
 */
static nj_class *resolve_saved_class(nj_vm *vm, const char *name)
{
    static const struct {
        const char *name;
        const char *desc;
    } prims[] = {
        { "boolean", "[Z" }, { "byte", "[B" }, { "char", "[C" },
        { "short", "[S" },   { "int",  "[I" }, { "long", "[J" },
        { "float", "[F" },   { "double", "[D" }
    };
    nj_u4 i;

    if (name[0] == '[') {
        return nj_class_resolve(vm, name);
    }
    for (i = 0u; i < (nj_u4)(sizeof(prims) / sizeof(prims[0])); i++) {
        if (strcmp(name, prims[i].name) == 0) {
            if (!nj_class_resolve(vm, prims[i].desc)) {
                return NULL;
            }
            return nj_class_find(vm, name);
        }
    }
    return nj_class_resolve(vm, name);
}

/* Clear every host-derived cache and per-class pointer that a restore
 * invalidates.  Classes are re-resolved from the JAR first, so this only
 * throws away memos - never anything the restored state depends on. */
static void clear_host_caches(nj_vm *vm)
{
    nj_u4 i;

    if (vm->cp_cache && vm->cp_cache_size) {
        memset(vm->cp_cache, 0, (size_t)vm->cp_cache_size * sizeof(nj_cpentry));
    }
    vm->cp_cache_used = 0u;
    vm->cp_cache_hits = 0u;
    vm->cp_cache_misses = 0u;
    for (i = 0u; i < (nj_u4)NJ_CLASS_FIND_SLOTS; i++) {
        vm->find_key[i] = NULL;
        vm->find_cls[i] = NULL;
    }
#if NJ_ENABLE_HOT_FIELDS
    /* Memoised hot-field pointers point into class structures that this
     * restore replaces, so drop them; each slot re-resolves on first use. */
    for (i = 0u; i < (nj_u4)NJ_HF_COUNT; i++) {
        vm->hot_field[i] = NULL;
    }
#endif
    /*
     * The per-field memo table is not part of every build of this VM yet, so
     * it is cleared under its own slot count: a build without the table simply
     * has nothing to throw away here.
     */
#ifdef NJ_FIELD_CACHE_SLOTS
    vm->find_hits = 0u;
    vm->find_misses = 0u;
    for (i = 0u; i < (nj_u4)NJ_FIELD_CACHE_SLOTS; i++) {
        vm->fld_key_cls[i] = NULL;
        vm->fld_key_name[i] = NULL;
        vm->fld_key_desc[i] = NULL;
        vm->fld_val[i] = NULL;
        vm->fld_key_static[i] = 0u;
        vm->fld_key_used[i] = 0u;
    }
#else
    (void)i;
#endif
    vm->cls_string = NULL;
    vm->cls_class = NULL;
    vm->cls_object = NULL;
    vm->cls_throwable = NULL;
    vm->cls_object_array = NULL;
    vm->resolve_fail = NJ_RESOLVE_OK;
    vm->resolve_cls = NULL;
    vm->resolve_name = NULL;
    vm->resolve_desc = NULL;
    vm->link_cls = NULL;
    vm->link_missing = NULL;
}

/* Drop every ref the fresh VM holds before the restored ones are applied, so
 * no stale pointer from the pre-load life of the VM can survive. */
static void reset_refs(nj_vm *vm)
{
    nj_u2 i;

    vm->exception = NULL;
    vm->app = NULL;
    vm->display = NULL;
    vm->current_displayable = NULL;
    vm->thread_group = NULL;
    vm->media_players = NULL;
    vm->sensor_conns = NULL;
    vm->bt_jobs = NULL;
    for (i = 0u; i < vm->ninterned && i < (nj_u2)NJ_MAX_INTERNED; i++) {
        vm->interned[i] = NULL;
    }
    vm->ninterned = 0u;
    for (i = 0u; i < vm->nclasses; i++) {
        if (vm->classes[i]) {
            vm->classes[i]->class_obj = NULL;
        }
    }
}

static int apply_recordsA(nj_vm *vm, nj_sreader *r)
{
    for (;;) {
        nj_u1 rec[16];
        nj_u1 kind;
        nj_u4 a, b, v;

        if (!sr_raw(r, rec, 16u)) {
            return 0;
        }
        kind = rec[0];
        a = get_u32(rec + 4);
        b = get_u32(rec + 8);
        v = get_u32(rec + 12);
        if (kind == NJ_SR_END) {
            return 1;
        }
        if (v != NJ_SAVE_NONE && v >= vm->heap.bump) {
            return 0;
        }
        switch (kind) {
        case NJ_SR_STATIC:
            if (a >= vm->nstatics) {
                return 0;
            }
            vm->statics[a].r = ref_at(vm, v);
            break;
        case NJ_SR_VM:
            switch (a) {
            case NJ_SV_EXCEPTION:           vm->exception = ref_at(vm, v); break;
            case NJ_SV_APP:                 vm->app = ref_at(vm, v); break;
            case NJ_SV_DISPLAY:             vm->display = ref_at(vm, v); break;
            case NJ_SV_CURRENT_DISPLAYABLE: vm->current_displayable = ref_at(vm, v); break;
            case NJ_SV_THREAD_GROUP:        vm->thread_group = ref_at(vm, v); break;
            case NJ_SV_MEDIA_PLAYERS:       vm->media_players = ref_at(vm, v); break;
            case NJ_SV_SENSOR_CONNS:        vm->sensor_conns = ref_at(vm, v); break;
            case NJ_SV_BT_JOBS:             vm->bt_jobs = ref_at(vm, v); break;
            default: return 0;
            }
            break;
        case NJ_SR_CLASS_OBJ:
            if (a >= vm->nclasses || !vm->classes[a]) {
                return 0;
            }
            vm->classes[a]->class_obj = ref_at(vm, v);
            break;
        case NJ_SR_OBJ:
            if (a + (nj_u4)sizeof(nj_ref) > vm->heap.bump) {
                return 0;
            }
            *(nj_ref *)(void *)(vm->heap.base + a) = ref_at(vm, v);
            break;
        case NJ_SR_METHOD:
            if (v >= vm->nclasses || !method_at(vm, v, b, &vm->exception_method)) {
                return 0;
            }
            break;
        default:
            return 0;
        }
    }
}

static int apply_recordsB(nj_vm *vm, nj_sreader *r)
{
    for (;;) {
        nj_u1 rec[16];
        nj_u1 kind;
        nj_u4 a, b, v;

        if (!sr_raw(r, rec, 16u)) {
            return 0;
        }
        kind = rec[0];
        a = get_u32(rec + 4);
        b = get_u32(rec + 8);
        v = get_u32(rec + 12);
        if (kind == NJ_SR_END) {
            return 1;
        }
        if (a >= vm->nthreads || !vm->threads[a]) {
            return 0;
        }
        if (v != NJ_SAVE_NONE && v >= vm->heap.bump) {
            return 0;
        }
        switch (kind) {
        case NJ_SR_THREAD:
            switch (b) {
            case NJ_ST_OBJ:          vm->threads[a]->obj = ref_at(vm, v); break;
            case NJ_ST_TARGET:       vm->threads[a]->target = ref_at(vm, v); break;
            case NJ_ST_ENTRY_THIS:   vm->threads[a]->entry_this = ref_at(vm, v); break;
            case NJ_ST_NAME:         vm->threads[a]->name = ref_at(vm, v); break;
            case NJ_ST_PENDING_EXC:  vm->threads[a]->pending_exc = ref_at(vm, v); break;
            case NJ_ST_WAIT_MONITOR: vm->threads[a]->wait_monitor = ref_at(vm, v); break;
            default: return 0;
            }
            break;
        case NJ_SR_STACK:
            /*
             * The bound is the thread's own stack, not th->top.  top is only
             * the allocation frontier: when a frame returns, nj_frame_pop()
             * leaves top at that frame's locals, and the caller then pushes
             * its own operands above it.  Those slots are part of the image
             * (save_threads walks each frame up to its sp), so a slot inside
             * stack_mem..stack_end is fair game - rejecting it here is what
             * made every restore of a real game fail with NJ_ERR_FORMAT.
             */
            if (b >= (nj_u4)NJ_MAX_STACK_SLOTS ||
                vm->threads[a]->stack_mem + b >= vm->threads[a]->stack_end) {
                return 0;            /* outside the thread's stack */
            }
            vm->threads[a]->stack_mem[b].r = ref_at(vm, v);
            break;
        default:
            return 0;
        }
    }
}


#define NJ_LOAD_FAIL(code) do {                                            \
    if (getenv("NJ_SAVE_DEBUG")) {                                        \
        fprintf(stderr, "nj_vm_load: refused (%s) at %s:%d\n",           \
                #code, __FILE__, __LINE__);                                \
    }                                                                      \
    rc = (code);                                                           \
    goto fail;                                                             \
} while (0)

int nj_vm_load(nj_vm *vm, FILE *f)
{
    nj_u1 hdr[NJ_SAVE_HDR_SIZE];
    nj_u1 *blob = NULL;
    nj_sreader rd;
    nj_size blob_len;
    long flen;
    nj_class **map = NULL;
    nj_u4 *csbase = NULL;      /* saved statics_base per class             */
    nj_u2 *cstate = NULL;      /* saved class state per class              */
    nj_u2 *cinfo = NULL;       /* saved inst/fields/methods/is_array       */
    nj_u2 *order = NULL;       /* class indices sorted by statics_base     */
    nj_u1 *block = NULL;
    nj_u1 *old_base = NULL;
    nj_u1 old_external = 0u;
    nj_u4 magic, version, hdr_size, ptr_size, endian, flags;
    nj_u8 heap_size, heap_bump, heap_used, heap_peak, saved_ms;
    nj_u8 halt;
    nj_u4 arena_crc, nclasses, nstatics, ninterned, nthreads, cur_idx;
    nj_u4 screen_w, screen_h, pixfmt, frames_presented, speed, midp_flags;
    nj_u4 abi, packed, exc_pc, app_cls, next_tid, stored_crc, i, t;
    nj_u4 max_id = 0u;
    nj_value *statics_old = NULL;   /* the caller's slots, for the fail path */
    nj_u4 statics_old_n = 0u;
    nj_u8 old_heap_size = 0u;
    int installed = 0;
    int threads_installed = 0;
    int rc = NJ_ERR_FORMAT;

    if (!vm || !f || !vm->statics) {
        return NJ_ERR;
    }
    /*
     * The whole blob is buffered so the CRC is verified before anything in it
     * is believed: a corrupted block header must not be able to walk the
     * loader off the end of the arena.  A non-seekable stream is refused.
     */
    if (fseek(f, 0L, SEEK_END) != 0) {
        return NJ_ERR_UNSUPPORTED;
    }
    flen = ftell(f);
    if (flen < 0) {
        return NJ_ERR_UNSUPPORTED;
    }
    /*
     * Sanity cap, not a heap cap: the blob carries the arena image plus every
     * live thread's stack image (NJ_MAX_STACK_SLOTS slots each), the statics
     * and the slot records - so a legitimate snapshot of a full heap is
     * several megabytes larger than the heap itself.  Anything past this is a
     * file that is not a snapshot, and is refused before it is buffered.
     */
    if ((nj_u8)flen > NJ_SAVE_BLOB_MAX(vm)) {
        return NJ_ERR_FORMAT;
    }
    if (fseek(f, 0L, SEEK_SET) != 0) {
        return NJ_ERR_UNSUPPORTED;
    }
    blob_len = (nj_size)flen;
    if (blob_len < NJ_SAVE_HDR_SIZE + 4u) {
        return NJ_ERR_FORMAT;
    }
    blob = (nj_u1 *)nj_host_alloc(vm, blob_len);
    if (!blob) {
        return NJ_ERR_NOMEM;
    }
    if (fread(blob, 1, (size_t)blob_len, f) != (size_t)blob_len) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);
    }
    /* CRC over everything after the header except the trailing CRC itself. */
    stored_crc = get_u32(blob + blob_len - 4u);
    if (nj_crc_update(0u, blob + NJ_SAVE_HDR_SIZE,
                      blob_len - NJ_SAVE_HDR_SIZE - 4u) != stored_crc) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);
    }
    memcpy(hdr, blob, NJ_SAVE_HDR_SIZE);
    rd.p = blob;
    rd.len = blob_len;
    rd.pos = NJ_SAVE_HDR_SIZE;
    rd.err = 0;

    magic = get_u32(hdr + 0);
    version = get_u32(hdr + 4);
    hdr_size = get_u32(hdr + 8);
    ptr_size = get_u32(hdr + 12);
    endian = get_u32(hdr + 16);
    flags = get_u32(hdr + 20);
    if ((flags & ~1u) != 0u || magic != NJ_SAVE_MAGIC || version != NJ_SAVE_VERSION ||
        hdr_size != NJ_SAVE_HDR_SIZE || endian != NJ_SAVE_ENDIAN ||
        ptr_size != (nj_u4)sizeof(void *)) {
        NJ_LOAD_FAIL(NJ_ERR_UNSUPPORTED);
    }
    heap_size = get_u64(hdr + 24);
    heap_bump = get_u64(hdr + 32);
    heap_used = get_u64(hdr + 40);
    heap_peak = get_u64(hdr + 48);
    arena_crc = get_u32(hdr + 64);
    nclasses = get_u32(hdr + 68);
    nstatics = get_u32(hdr + 72);
    ninterned = get_u32(hdr + 76);
    nthreads = get_u32(hdr + 80);
    cur_idx = get_u32(hdr + 84);
    screen_w = get_u32(hdr + 88);
    screen_h = get_u32(hdr + 92);
    pixfmt = get_u32(hdr + 96);
    frames_presented = get_u32(hdr + 100);
    speed = get_u32(hdr + 104);
    midp_flags = get_u32(hdr + 108);
    abi = get_u32(hdr + 112);
    packed = get_u32(hdr + 116);
    saved_ms = get_u64(hdr + 120);
    halt = get_u64(hdr + 152);
    exc_pc = get_u32(hdr + 160);
    app_cls = get_u32(hdr + 164);
    next_tid = get_u32(hdr + 168);
    (void)pixfmt;   /* geometry is only checked, never applied */

    if (heap_size < 4096u || heap_size > NJ_SAVE_HEAP_MAX(vm) ||
        heap_size > 0xFFFFFFFFu || heap_bump < NJ_BLOCK_HDR ||
        heap_bump > heap_size || nclasses == 0u ||
        nclasses > (nj_u4)NJ_MAX_CLASSES || nstatics > (nj_u4)NJ_MAX_STATICS ||
        ninterned > (nj_u4)NJ_MAX_INTERNED ||
        nthreads > (nj_u4)NJ_MAX_THREADS) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);
    }
    /* The classes have to come from the JAR the caller attached. */
    if (!vm->source || !vm->source->load) {
        NJ_LOAD_FAIL(NJ_ERR_STATE);
    }
    map = (nj_class **)nj_host_alloc(vm,
                                     (nj_size)nclasses * (nj_size)sizeof(*map));
    csbase = (nj_u4 *)nj_host_alloc(vm, (nj_size)nclasses * (nj_size)sizeof(*csbase));
    cstate = (nj_u2 *)nj_host_alloc(vm, (nj_size)nclasses * (nj_size)sizeof(*cstate));
    order = (nj_u2 *)nj_host_alloc(vm, (nj_size)nclasses * (nj_size)sizeof(*order));
    cinfo = (nj_u2 *)nj_host_alloc(vm, (nj_size)nclasses * 4u * (nj_size)sizeof(*cinfo));
    if (!map || !csbase || !cstate || !order || !cinfo) {
        NJ_LOAD_FAIL(NJ_ERR_NOMEM);
    }
    memset(map, 0, (size_t)nclasses * sizeof(*map));

    /* Class table: read it, then resolve every class by name. */
    for (i = 0u; i < nclasses; i++) {
        nj_u2 len;
        char name[NJ_SAVE_NAME_MAX];
        nj_u2 inst, fcount, mcount, is_arr, state, pad;
        nj_u4 sb;
        nj_class *c;
        nj_u4 j;

        if (!sr_u16(&rd, &len) || len == 0u ||
            len >= (nj_u2)NJ_SAVE_NAME_MAX ||
            !sr_raw(&rd, name, len) ||
            !sr_u16(&rd, &inst) || !sr_u16(&rd, &fcount) ||
            !sr_u16(&rd, &mcount) || !sr_u16(&rd, &is_arr) ||
            !sr_u16(&rd, &state) || !sr_u16(&rd, &pad) ||
            !sr_u32(&rd, &sb)) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);
        }
        (void)pad;
        name[len] = '\0';
        c = resolve_saved_class(vm, name);
        if (!c) {
            if (getenv("NJ_SAVE_DEBUG")) {
                fprintf(stderr, "nj_vm_load: no class named \"%s\"\n", name);
            }
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);   /* JAR does not match */
        }
        for (j = 0u; j < i; j++) {
            if (map[j] == c) {
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);  /* duplicate name */
            }
        }
        map[i] = c;
        csbase[i] = sb;
        cstate[i] = state;
        cinfo[i * 4u + 0u] = inst;
        cinfo[i * 4u + 1u] = fcount;
        cinfo[i * 4u + 2u] = mcount;
        cinfo[i * 4u + 3u] = is_arr;
    }
    /*
     * Link the classes in the order their static slots were handed out.  That
     * order is the only thing that reproduces the slot numbers: nj_class_link()
     * gives each STATIC field the next free slot from vm->nstatics as it goes,
     * so linking in registration order would silently hand a field a different
     * slot than the blob was saved with.  statics_base (the first slot of the
     * class, NJ_SAVE_NONE when it declares no statics) recovers the order; a
     * class with no statics cannot move anybody's slot, so those go last.
     * Everything is still verified against the saved ABI hash afterwards.
     */
    for (i = 0u; i < nclasses; i++) {
        order[i] = (nj_u2)i;
    }
    for (i = 1u; i < nclasses; i++) {          /* insertion sort, stable */
        nj_u2 k = order[i];
        nj_u4 key = csbase[k];
        nj_u4 j = i;

        while (j > 0u && csbase[order[j - 1u]] > key) {
            order[j] = order[j - 1u];
            j--;
        }
        order[j] = k;
    }
    for (i = 0u; i < nclasses; i++) {
        nj_u2 idx = order[i];
        nj_class *c = map[idx];

        /*
         * A class that was merely LOADED when the snapshot was taken had never
         * been linked there, so it owns no static slots.  Linking it here
         * would hand out slots that the saved VM gave to another class, and in
         * a different order - every static slot after it would then be wrong.
         * It is left loaded and linked on demand, exactly as it was, which is
         * also what makes an unlinked class harmless in the image: it has no
         * statics and no instances to restore.
         */
        if (cstate[idx] < NJ_CLASS_LINKED) {
            continue;
        }
        if (c->state < NJ_CLASS_LINKED) {
            if (nj_class_link(vm, c) != NJ_OK) {
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);   /* class cannot be linked */
            }
        }
    }
    /* Now that the classes are laid out, verify them against the saved ABI. */
    {
        nj_u4 h = 2166136261u;

        for (i = 0u; i < nclasses; i++) {
            nj_u2 inst = cinfo[i * 4u + 0u];
            nj_u2 fcount = cinfo[i * 4u + 1u];
            nj_u2 mcount = cinfo[i * 4u + 2u];
            nj_u2 is_arr = cinfo[i * 4u + 3u];

            if (map[i]->field_count != fcount ||
                map[i]->method_count != mcount ||
                (nj_u2)map[i]->is_array != is_arr ||
                (cstate[i] >= NJ_CLASS_LINKED &&
                 map[i]->instance_size != inst)) {
                if (getenv("NJ_SAVE_DEBUG")) {
                    fprintf(stderr, "  class %s: saved inst=%u f=%u m=%u arr=%u"
                            " have inst=%u f=%u m=%u arr=%u state=%u\n",
                            map[i]->name, (unsigned)inst, (unsigned)fcount,
                            (unsigned)mcount, (unsigned)is_arr,
                            (unsigned)map[i]->instance_size,
                            (unsigned)map[i]->field_count,
                            (unsigned)map[i]->method_count,
                            (unsigned)map[i]->is_array, (unsigned)map[i]->state);
                }
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);   /* the JAR does not match */
            }
            if (cstate[i] >= NJ_CLASS_LINKED) {
                h = class_abi_hash(h, map[i]);
            }
        }
        if (h != abi) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);      /* layout changed */
        }
    }
    if (nstatics != vm->nstatics) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);          /* static slots do not line up */
    }

    /* Static slots, verbatim.  From here until the arena image is in place no
     * code may allocate from the Java heap: those slots still hold pointers
     * into the arena that is about to be replaced. */
    if (vm->nstatics > 0u) {
        /*
         * Keep the caller's slots so that a failure below can put them back.
         * Without this a refused load left half of them holding offsets into
         * the arena that was then dropped, and the VM the caller falls back to
         * ("cannot resume, start the MIDlet instead") was full of dangling
         * pointers - the fallback start died in a Java method that read one.
         */
        statics_old_n = vm->nstatics;
        statics_old = (nj_value *)nj_host_alloc(vm, (nj_size)statics_old_n *
                                                   (nj_size)sizeof(nj_value));
        if (!statics_old) {
            NJ_LOAD_FAIL(NJ_ERR_NOMEM);
        }
        memcpy(statics_old, vm->statics,
               (size_t)statics_old_n * sizeof(nj_value));
    }
    for (i = 0u; i < nstatics; i++) {
        nj_u8 bits;
        if (!sr_u64(&rd, &bits)) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);
        }
        memcpy(&vm->statics[i], &bits, sizeof(bits));
    }
    vm->gc_disabled = 1u;
    old_heap_size = heap_size;

    /* A new arena, then drop the old one.  Allocating first means a failure
     * here leaves the running VM intact. */
    block = (nj_u1 *)nj_host_alloc(vm, (nj_size)heap_size);
    if (!block) {
        NJ_LOAD_FAIL(NJ_ERR_NOMEM);
    }
    if (((nj_size)(uintptr_t)block & (NJ_OBJ_ALIGN - 1u)) != 0u) {
        nj_host_free(vm, block);
        block = NULL;
        NJ_LOAD_FAIL(NJ_ERR_NOMEM);
    }
    old_base = vm->heap.base;
    old_external = vm->heap.external;
    if (nj_heap_init(vm, block, (nj_size)heap_size) != NJ_OK) {
        nj_host_free(vm, block);
        block = NULL;
        NJ_LOAD_FAIL(NJ_ERR_NOMEM);
    }
    vm->heap.external = 0u;               /* the arena is ours to free now */
    installed = 1;
    /*
     * The memo tables and the fresh VM's own refs point into the heap that was
     * just replaced, so they go now - BEFORE anything is restored from the
     * image.  Doing it later was a bug: reset_refs() zeroes vm->ninterned, so
     * the interned table restored a few lines below was wiped again and every
     * String.intern() identity was lost across a load.
     */
    clear_host_caches(vm);
    reset_refs(vm);
    if (old_base && !old_external) {
        nj_host_free(vm, old_base);
    }
    old_base = NULL;
    vm->heap.bump = (nj_size)heap_bump;
    vm->heap.used = (nj_size)heap_used;
    vm->heap.peak = (nj_size)heap_peak;
    vm->heap.gc_count = 0u;
    vm->heap.allocs_since_gc = 1u;

    if (!sr_raw(&rd, vm->heap.base, (nj_size)heap_bump)) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);
    }
    if (arena_crc != 0u &&
        nj_crc_update(0u, vm->heap.base, (nj_size)heap_bump) != arena_crc) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);
    }

    /* Interned strings. */
    for (i = 0u; i < ninterned; i++) {
        nj_u4 off;
        if (!sr_u32(&rd, &off) ||
            (off != NJ_SAVE_NONE && off >= vm->heap.bump)) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);
        }
        vm->interned[i] = ref_at(vm, off);
    }
    vm->ninterned = (nj_u2)ninterned;

    /* Every object: rebase its class (and Class mirror). */
    {
        nj_u1 *p = vm->heap.base;
        nj_u1 *end = vm->heap.base + vm->heap.bump;

        while (p < end) {
            nj_block *b = (nj_block *)(void *)p;
            nj_size sz = b->size;

            if (sz < NJ_BLOCK_HDR || (sz & (NJ_OBJ_ALIGN - 1u)) != 0u ||
                sz > (nj_size)(end - p)) {
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);
            }
            if (b->magic == NJ_BLOCK_MAGIC) {
                nj_object *o = (nj_object *)(void *)(p + NJ_BLOCK_HDR);
                nj_u2 ci, mi = NJ_SAVE_NO_CLASS;

                if (o->size < sizeof(nj_object) ||
                    (nj_size)o->size > sz - NJ_BLOCK_HDR ||
                    o->kind > NJ_OBJ_THROW) {
                    NJ_LOAD_FAIL(NJ_ERR_FORMAT);
                }
                if (!sr_u16(&rd, &ci) ||
                    (ci != NJ_SAVE_NO_CLASS && ci >= nclasses)) {
                    NJ_LOAD_FAIL(NJ_ERR_FORMAT);
                }
                if (o->kind == NJ_OBJ_CLASS) {
                    if (!sr_u16(&rd, &mi) ||
                        (mi != NJ_SAVE_NO_CLASS && mi >= nclasses)) {
                        NJ_LOAD_FAIL(NJ_ERR_FORMAT);
                    }
                }
                o->cls = (ci == NJ_SAVE_NO_CLASS) ? NULL : map[ci];
                o->mark = 0u;
                if (o->kind == NJ_OBJ_CLASS) {
                    o->u.mirror = (mi == NJ_SAVE_NO_CLASS) ? NULL : map[mi];
                }
                /*
                 * An array's u.bytes/u.refs is an INTERIOR pointer - it points
                 * just past the object header, inside the arena (nj_new_array
                 * sets it that way) - and it was baked into the image at the
                 * old arena's address.  A wrapped String's u.raw and a class
                 * mirror's u.mirror are ordinary refs and arrive through the
                 * slot records; the interior ones have no slot, so they are
                 * recomputed from the invariant instead.
                 *
                 * An EMBEDDED String (the common case: literals, concat,
                 * valueOf) is the same story - nj_new_string_utf16() points
                 * u.bytes at its own payload.  Only a wrapped String carries a
                 * ref there, and that one is in the slot records.
                 */
                if (o->kind == NJ_OBJ_ARRAY) {
                    o->u.bytes = (nj_u1 *)(void *)o + sizeof(nj_object);
                } else if (o->kind == NJ_OBJ_STRING &&
                           (o->aflags & NJ_STR_WRAPPED) == 0u) {
                    o->u.bytes = (nj_u1 *)(void *)o + sizeof(nj_object);
                }
            }
            p += sz;
        }
        if (p != end) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);
        }
    }

    /* Records A. */
    if (!apply_recordsA(vm, &rd)) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);
    }

    /* Threads: replace the fresh VM's own threads with the saved ones. */
    while (vm->nthreads > 0u) {
        nj_thread_destroy(vm, vm->threads[0]);
    }
    vm->cur = NULL;
    threads_installed = 1;
    for (t = 0u; t < nthreads; t++) {
        nj_thread *th;
        nj_u4 scalars, prio, exit_value, stop_depth, in_c_call, id;
        nj_u4 wake_lo, wake_hi, dl_lo, dl_hi;
        nj_u4 used, nframes, top_slot, f;
        nj_u8 ret;

        if (!sr_u32(&rd, &scalars) || !sr_u32(&rd, &prio) ||
            !sr_u32(&rd, &exit_value) || !sr_u64(&rd, &ret) ||
            !sr_u32(&rd, &stop_depth) || !sr_u32(&rd, &in_c_call) ||
            !sr_u32(&rd, &id) ||
            !sr_u32(&rd, &wake_lo) || !sr_u32(&rd, &wake_hi) ||
            !sr_u32(&rd, &dl_lo) || !sr_u32(&rd, &dl_hi) ||
            !sr_u32(&rd, &i) || !sr_u32(&rd, &used) ||
            !sr_u32(&rd, &top_slot) || !sr_u32(&rd, &nframes)) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);
        }
        if (in_c_call != 0u || used > (nj_u4)NJ_MAX_STACK_SLOTS ||
            top_slot > used || nframes > (nj_u4)NJ_MAX_FRAMES) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);
        }
        th = nj_thread_create(vm, NULL, (int)((scalars >> 24) & 0xFFu));
        if (!th) {
            NJ_LOAD_FAIL(NJ_ERR_NOMEM);
        }
        memset(th->frames, 0, sizeof(th->frames));
        th->state = (nj_u1)(scalars & 0xFFu);
        th->daemon = (nj_u1)((scalars >> 8) & 0xFFu);
        th->started = (nj_u1)((scalars >> 16) & 0xFFu);
        th->is_main = (nj_u1)((scalars >> 24) & 0xFFu);
        th->priority = (nj_s4)prio;
        th->exit_value = (nj_s4)exit_value;
        memcpy(&th->ret_value, &ret, sizeof(ret));
        th->stop_depth = (int)stop_depth;
        th->in_c_call = 0;
        th->id = id;
        th->wake_at_ms = (nj_s8)((nj_u8)wake_lo | ((nj_u8)wake_hi << 32));
        th->wait_deadline = (nj_s8)((nj_u8)dl_lo | ((nj_u8)dl_hi << 32));
        th->wakeup_seq = i;
        th->top = th->stack_mem + top_slot;
        if (id > max_id) {
            max_id = id;
        }
        if (used != 0u &&
            !sr_raw(&rd, th->stack_mem, used * (nj_u4)sizeof(nj_value))) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);
        }
        for (f = 0u; f < nframes; f++) {
            nj_u2 ci, mi;
            nj_u4 pc, total_slots, lo, so, spo, slo, abo, mo;
            nj_u1 is_native, pad1;
            nj_u2 pad2;
            nj_frame *fr;

            if (!sr_u16(&rd, &ci) || !sr_u16(&rd, &mi) ||
                !sr_u32(&rd, &pc) || !sr_u32(&rd, &total_slots) ||
                !sr_u8(&rd, &is_native) || !sr_u8(&rd, &pad1) ||
                !sr_u16(&rd, &pad2) ||
                !sr_u32(&rd, &lo) || !sr_u32(&rd, &so) ||
                !sr_u32(&rd, &spo) || !sr_u32(&rd, &slo) ||
                !sr_u32(&rd, &abo) || !sr_u32(&rd, &mo)) {
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);
            }
            (void)pad1;
            (void)pad2;
            /*
             * Locals and the live operand stack have to lie inside the image
             * that was just copied (used slots), and the frame's own bounds
             * have to be monotonically ordered inside the thread's stack.
             *
             * stack_limit is deliberately NOT compared with `used`: a callee
             * that has returned leaves th->top at its own locals, which is
             * below the caller's stack_limit, so a live frame may have room
             * above the copied image.  That room is inside the thread's stack
             * block (freshly allocated and zeroed) and is exactly the space
             * the frame is allowed to grow into.  sp is the bound that
             * matters: everything live is at or below it.
             */
            if (lo > used || spo > used ||
                abo > lo || lo > so || so > spo || spo > slo ||
                slo > (nj_u4)NJ_MAX_STACK_SLOTS ||
                (mo != NJ_SAVE_NONE && mo >= vm->heap.bump)) {
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);
            }
            fr = &th->frames[f];
            if (!method_at(vm, ci, mi, &fr->m)) {
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);
            }
            if (fr->m && pc > fr->m->code_len) {
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);
            }
            fr->pc = pc;
            fr->total_slots = (nj_u2)total_slots;
            fr->is_native = is_native;
            fr->locals = th->stack_mem + lo;
            fr->stack = th->stack_mem + so;
            fr->sp = th->stack_mem + spo;
            fr->stack_limit = th->stack_mem + slo;
            fr->argbase = th->stack_mem + abo;
            fr->monitor = (mo == NJ_SAVE_NONE) ? NULL : ref_at(vm, mo);
        }
        th->nframes = (int)nframes;
    }

    /* Records B. */
    if (!apply_recordsB(vm, &rd)) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);
    }
    if (rd.pos != blob_len - 4u) {
        NJ_LOAD_FAIL(NJ_ERR_FORMAT);          /* trailing bytes */
    }

    /* Keep the saved MIDlet epoch, including sleep/wait deadlines and Java
     * Timer timestamps. Only the raw port clock's origin changes on restore:
     * CLOCK_MONOTONIC and SDL ticks need not have a common epoch. Shifting the
     * virtual clock alone strands those deadlines; shifting individual fields
     * would miss Java timestamps and could turn a timed wait into sentinel 0.
     * nj_now_ms() folds the current port counter into scheduler_ticks. The old
     * raw/scale anchors remain in the v2 header for format compatibility. */
    (void)nj_now_ms(vm);
    vm->speed_real = vm->scheduler_ticks;
    vm->speed_virtual = (nj_s8)saved_ms;
    vm->speed_percent = (speed >= 10u && speed <= 800u) ? (int)speed : 100;
    vm->halt_at_ms = (nj_s8)halt;
    vm->exception_pc = exc_pc;
    vm->app_class = (app_cls == NJ_SAVE_NO_CLASS || app_cls >= nclasses)
                        ? NULL : map[app_cls];
    vm->next_thread_id = (next_tid > max_id) ? next_tid : max_id + 1u;
    vm->cur = (cur_idx == NJ_SAVE_NONE || cur_idx >= nthreads)
                  ? NULL : vm->threads[cur_idx];
    vm->repaint_pending = (nj_u1)(packed & 0xFFu);
    vm->painted_in_frame = (nj_u1)((packed >> 8) & 0xFFu);
    vm->state = (nj_u1)((packed >> 16) & 0xFFu);
    vm->running = (nj_u1)((packed >> 24) & 0xFFu);
    vm->frames_presented = frames_presented;
    vm->midp_flags = midp_flags;
    /*
     * Screen geometry: the pixels are not in the blob and cannot be - the
     * framebuffer belongs to the port.  A fresh VM has not brought its display
     * up yet (screen_w/h are 0, and lcdui calls the port's display_init on the
     * next frame), so the saved numbers are only used to CHECK that the two
     * processes agree.  Writing them over an uninitialised screen would make
     * lcdui skip display_init() and render into a private buffer the port
     * never presents.
     */
    if (screen_w != 0u && screen_h != 0u) {
        if (vm->screen_w != 0u && vm->screen_h != 0u) {
            if (vm->screen_w != (nj_u2)screen_w ||
                vm->screen_h != (nj_u2)screen_h) {
                NJ_LOAD_FAIL(NJ_ERR_FORMAT);  /* different display geometry */
            }
        } else if (vm->screen.pixels != NULL) {
            NJ_LOAD_FAIL(NJ_ERR_FORMAT);      /* a surface with no geometry */
        }
    }
    if (arena_walk(vm, restore_native_object, NULL, NULL) != NJ_OK) {
        goto fail;
    }
    /* Playback resources are deliberately not part of a snapshot. */
    vm->media_players = NULL;

    /*
     * Last of all, mark the classes whose <clinit> had already run in the
     * saved VM as READY, so a restored program does not initialize them a
     * second time and reset the statics that were just restored.  This has to
     * wait until nothing below can fail: a refused load used to leave classes
     * looking initialized while their <clinit> had never run in THIS VM, and
     * the caller's fallback start then died on the first static field it read.
     */
    for (i = 0u; i < nclasses; i++) {
        if ((nj_u2)map[i]->state < cstate[i]) {
            map[i]->state = (nj_u1)cstate[i];
        }
    }
    nj_host_free(vm, statics_old);
    statics_old = NULL;
    vm->gc_disabled = 0u;
    nj_host_free(vm, map);
    map = NULL;
    nj_host_free(vm, blob);
    blob = NULL;
    /*
     * Rebuild the free lists by collecting: the sweep is the only code that
     * knows the allocator's bin layout, and it also drops whatever became
     * unreachable.  After this nj_heap_alloc() works normally.
     */
    if (nj_gc_run(vm) != NJ_OK) {
        return NJ_ERR;
    }
    nj_host_free(vm, cinfo);
    nj_host_free(vm, order);
    nj_host_free(vm, cstate);
    nj_host_free(vm, csbase);
    nj_host_free(vm, map);
    vm->motorola_softkeys = (nj_u1)(flags & 1u);
    return NJ_OK;

fail:
    vm->gc_disabled = 0u;
    /*
     * Put the caller's static slots back exactly as they were.  What the
     * loader wrote into them is either an offset into an arena that is about
     * to be dropped or a value out of a blob it has just refused.
     */
    if (statics_old) {
        memcpy(vm->statics, statics_old,
               (size_t)statics_old_n * sizeof(nj_value));
        vm->nstatics = statics_old_n;
        nj_host_free(vm, statics_old);
        statics_old = NULL;
    }
    /* Linking a class can raise an exception; it belongs to a refused load. */
    vm->exception = NULL;
    vm->exception_method = NULL;
    vm->exception_pc = 0u;
    if (installed) {
        /* The arena holds a half-applied image: drop it whole rather than
         * leave the VM with pointers into a mix of two heaps. */
        if (vm->heap.base && !vm->heap.external) {
            nj_host_free(vm, vm->heap.base);
        }
        vm->heap.base = NULL;
        vm->heap.size = 0u;
        vm->heap.bump = 0u;
        vm->heap.used = 0u;
        vm->heap.external = 0u;
    }
    if (threads_installed) {
        /*
         * The threads now in the VM were read out of the blob and their refs
         * point into the arena that was just dropped.  A caller is allowed to
         * ignore a refused load and start the MIDlet from scratch (nj_run()
         * does), so leave it the one fresh main thread a new VM would have.
         */
        while (vm->nthreads > 0u) {
            nj_thread_destroy(vm, vm->threads[0]);
        }
        vm->cur = nj_thread_create(vm, NULL, 1);
    }
    if (installed) {
        /*
         * Leave the VM usable: every caller may fall back to starting the
         * MIDlet normally, and a VM with no arena at all would fail every
         * allocation instead.  A fresh, empty arena of the same size is the
         * VM it would have had if no snapshot had ever been offered.
         */
        nj_size hs = (nj_size)(old_heap_size ? old_heap_size
                                             : (nj_u8)NJ_MAX_HEAP);
        if (nj_heap_init(vm, NULL, hs) != NJ_OK) {
            vm->heap.base = NULL;
            vm->heap.size = 0u;
        }
    }
    if (cinfo) {
        nj_host_free(vm, cinfo);
    }
    if (order) {
        nj_host_free(vm, order);
    }
    if (cstate) {
        nj_host_free(vm, cstate);
    }
    if (csbase) {
        nj_host_free(vm, csbase);
    }
    if (map) {
        nj_host_free(vm, map);
    }
    if (blob) {
        nj_host_free(vm, blob);
    }
    return rc;
}

#undef NJ_LOAD_FAIL

int nj_vm_load_file(nj_vm *vm, const char *path)
{
    FILE *f;
    int rc;

    if (!vm || !path) {
        return NJ_ERR;
    }
    f = fopen(path, "rb");
    if (!f) {
        return NJ_ERR;
    }
    rc = nj_vm_load(vm, f);
    fclose(f);
    return rc;
}
