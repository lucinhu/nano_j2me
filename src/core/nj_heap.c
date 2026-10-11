/*
 * nano_j2me - nj_heap.c
 *
 * The Java heap and the host allocation registry.
 *
 * ---------------------------------------------------------------------------
 * DESIGN: contiguous arena, non-moving mark & sweep, segregated free lists
 * ---------------------------------------------------------------------------
 * The Java heap is one contiguous block owned by the VM.  Objects are
 * allocated from it and are *never moved* (raw nj_ref pointers stay valid),
 * which is what lets the interpreter and the class library hold plain C
 * pointers to Java objects.
 *
 * Layout
 *     base ──────────────────────────────────────────────► base + bump
 *     [blk][blk][blk][gap][blk] ...
 *
 * Every block starts with an nj_block header carrying its own total size, so
 * the whole arena can be walked linearly.  A live block has magic set, a free
 * block has magic cleared and its payload reused as a free-list link.
 *
 * Allocation
 *     1. segregated free lists (exact bin, then larger bins, then the big list)
 *     2. bump allocation
 *     3. garbage collection, then retry (1)
 *
 * Sweep
 *     The arena is walked once; consecutive free blocks are coalesced into a
 *     single larger block and the whole free structure is rebuilt from
 *     scratch.  Coalescing is what keeps fragmentation from destroying the
 *     bump pointer over a long game session.
 *
 * ---------------------------------------------------------------------------
 * DESIGN: host allocation registry
 * ---------------------------------------------------------------------------
 * Everything the runtime allocates outside the Java heap goes through
 * nj_host_alloc(), which links the block into an intrusive doubly-linked list
 * and counts it.  nj_host_free() unlinks it.  nj_vm_destroy() walks whatever
 * is left and releases it, so teardown is guaranteed complete even if a
 * module misses a free on some error path.  nj_host_live_count() returning 0
 * after destroy is the "exits cleanly, leaves nothing behind" assertion that
 * tests/test_lifecycle.c enforces.
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdlib.h>
#else
#  include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */
#define NJ_BLOCK_MAGIC   0x4E4A424Bu   /* 'NJBK' */
#define NJ_BLOCK_FREE    0x00000000u
#define NJ_BLOCK_HDR     16u           /* sizeof(nj_block), 16-byte aligned  */
/* NJ_OBJ_ALIGN is public (nj_config.h): a port's allocator has to honour it. */

#define NJ_BIN_STEP      16u
#define NJ_MIN_SPLIT     32u           /* don't split below this            */
#define NJ_MIN_BLOCK     (NJ_BLOCK_HDR + 16u)

typedef struct nj_block {
    nj_u4 size;    /* total bytes of this block, header included */
    nj_u4 magic;
    /* payload follows; when free, the first payload word is the next link */
} nj_block;

#define NJ_BLOCK_PAYLOAD(b) ((void *)((nj_u1 *)(b) + NJ_BLOCK_HDR))
#define NJ_BLOCK_NEXT(b)    ((nj_block *)((nj_u1 *)(b) + (b)->size))
#define NJ_BLOCK_LINK(b)    (*(void **)NJ_BLOCK_PAYLOAD(b))

static nj_size nj_align_up(nj_size v, nj_size a)
{
    return (v + (a - 1u)) & ~(a - 1u);
}

/* ------------------------------------------------------------------ */
/* Host allocation registry                                            */
/* ------------------------------------------------------------------ */
#define NJ_HOST_MAGIC 0x4E4A4848u   /* 'NJHH' */

typedef struct nj_hosthdr {
    struct nj_hosthdr *prev;
    struct nj_hosthdr *next;
    nj_size            size;    /* user-visible size */
    nj_u4              magic;
} nj_hosthdr;

/*
 * The user pointer must sit past the WHOLE header, rounded up to the object
 * alignment: a bare NJ_OBJ_ALIGN offset would overlap the size/magic fields
 * and let the runtime silently corrupt every host allocation it hands out.
 */
#define NJ_HOST_HDR_SIZE     ((nj_size)((sizeof(nj_hosthdr) + (NJ_OBJ_ALIGN - 1u)) & ~(nj_size)(NJ_OBJ_ALIGN - 1u)))

static nj_u1 *host_user(nj_hosthdr *h) { return (nj_u1 *)h + NJ_HOST_HDR_SIZE; }

nj_u4 nj_host_live_count(nj_vm *vm)
{
    return vm ? vm->host_count : 0u;
}

void *nj_host_alloc(nj_vm *vm, nj_size n)
{
    nj_hosthdr *h;
    nj_size total;

    if (!vm || n == 0u) {
        return NULL;
    }
    /* Overflow guard: never wrap on a hostile size. */
    if (n > (nj_size)-1 - (NJ_HOST_HDR_SIZE * 2u)) {
        return NULL;
    }
    total = nj_align_up(n, NJ_OBJ_ALIGN) + NJ_HOST_HDR_SIZE;

    if (vm->port && vm->port->alloc) {
        h = (nj_hosthdr *)vm->port->alloc(vm->port->ud, total);
    } else {
#if NJ_USE_LIBC
        h = (nj_hosthdr *)malloc(total);
#else
        h = NULL;   /* freestanding builds must supply port.alloc */
#endif
    }
    if (!h) {
        return NULL;
    }
    memset(h, 0, total);

    h->size = n;
    h->magic = NJ_HOST_MAGIC;
    h->prev = NULL;
    h->next = vm->host_head;
    if (vm->host_head) {
        vm->host_head->prev = h;
    }
    vm->host_head = h;

    vm->host_count++;
    vm->host_bytes += (nj_u8)total;
    if (vm->host_count > vm->host_peak) {
        vm->host_peak = vm->host_count;
    }
    return host_user(h);
}

void nj_host_free(nj_vm *vm, void *p)
{
    nj_hosthdr *h;

    if (!vm || !p) {
        return;
    }
    h = (nj_hosthdr *)((nj_u1 *)p - NJ_HOST_HDR_SIZE);
    if (h->magic != NJ_HOST_MAGIC) {
        /* Not ours, or already freed: ignore rather than corrupt the list. */
        return;
    }
    h->magic = 0u;

    if (h->prev) {
        h->prev->next = h->next;
    } else {
        vm->host_head = h->next;
    }
    if (h->next) {
        h->next->prev = h->prev;
    }
    if (vm->host_count > 0u) {
        vm->host_count--;
    }

    if (vm->port && vm->port->free) {
        vm->port->free(vm->port->ud, h);
    } else {
#if NJ_USE_LIBC
        free(h);
#endif
    }
}

/* Release every remaining host allocation.  Called from nj_vm_destroy(). */
static void nj_host_free_all(nj_vm *vm)
{
    nj_hosthdr *h = vm->host_head;

    while (h) {
        nj_hosthdr *next = h->next;
        h->magic = 0u;
        if (vm->port && vm->port->free) {
            vm->port->free(vm->port->ud, h);
        } else {
#if NJ_USE_LIBC
            free(h);
#endif
        }
        h = next;
    }
    vm->host_head = NULL;
    vm->host_count = 0u;
    vm->host_bytes = 0u;
}

/* ------------------------------------------------------------------ */
/* Java heap                                                           */
/* ------------------------------------------------------------------ */
nj_status nj_heap_init(nj_vm *vm, void *block, nj_size size)
{
    nj_gcbin *bins = vm->heap.bins;
    int i;

    if (size < 4096u) {
        return NJ_ERR;
    }
    memset(&vm->heap, 0, sizeof(vm->heap));

    if (block) {
        vm->heap.base = (nj_u1 *)block;
        vm->heap.size = size;
        vm->heap.external = 1u;
    } else {
        void *p = nj_host_alloc(vm, size);
        if (!p) {
            return NJ_ERR_NOMEM;
        }
        vm->heap.base = (nj_u1 *)p;
        vm->heap.size = size;
        vm->heap.external = 0u;
    }

    /* Alignment: the arena must be 16-byte aligned for long/double fields. */
    {
        nj_u1 *base = vm->heap.base;
        nj_size misalign = (nj_size)((nj_u1 *)0 - (nj_u1 *)0); /* silence unused */
        (void)misalign;
        if (((nj_size)(uintptr_t)base & (NJ_OBJ_ALIGN - 1u)) != 0u) {
            /* Say which of the two things is wrong: the port returned an
             * unaligned block, or the runtime's header size is not a multiple
             * of the alignment.  Both look identical from the outside. */
            nj_logf(vm, 0, "nj_heap_init: arena base is not "
                           "NJ_OBJ_ALIGN-aligned (port allocator contract)");
            return NJ_ERR;
        }
    }

    for (i = 0; i < NJ_GC_BINS; i++) {
        bins[i].head = NULL;
        bins[i].count = 0u;
    }
    vm->heap.big_free = NULL;
    vm->heap.used = 0u;
    vm->heap.bump = 0u;
    vm->heap.peak = 0u;
    vm->heap.gc_count = 0u;
    return NJ_OK;
}

nj_size nj_heap_used_bytes(nj_vm *vm)
{
    nj_size used = 0u;
    nj_u1 *p;
    nj_u1 *end;

    if (!vm || !vm->heap.base) {
        return 0u;
    }
    p = vm->heap.base;
    end = vm->heap.base + vm->heap.bump;
    while (p < end) {
        nj_block *b = (nj_block *)p;
        if (b->size < NJ_MIN_BLOCK || b->size > (nj_size)(end - p)) {
            break;   /* corrupt; refuse to loop forever */
        }
        if (b->magic == NJ_BLOCK_MAGIC) {
            used += b->size;
        }
        p += b->size;
    }
    return used;
}

nj_size nj_heap_free_bytes(nj_vm *vm)
{
    if (!vm || !vm->heap.base) {
        return 0u;
    }
    return vm->heap.size - nj_heap_used_bytes(vm);
}

/* Size class helpers. */
static int nj_bin_for(nj_size total)
{
    nj_size idx = (total + NJ_BIN_STEP - 1u) / NJ_BIN_STEP;
    if (idx == 0u) {
        idx = 1u;
    }
    if (idx > (nj_size)NJ_GC_BINS) {
        return -1;
    }
    return (int)(idx - 1u);
}

static void nj_free_insert(nj_vm *vm, nj_block *b)
{
    int bin = nj_bin_for(b->size);
    if (bin < 0) {
        NJ_BLOCK_LINK(b) = vm->heap.big_free;
        vm->heap.big_free = b;
    } else {
        NJ_BLOCK_LINK(b) = vm->heap.bins[bin].head;
        vm->heap.bins[bin].head = b;
        vm->heap.bins[bin].count++;
    }
}

/* Pop the first block from `bin`; returns NULL when empty. */
static nj_block *nj_free_pop_bin(nj_vm *vm, int bin)
{
    nj_block *b = (nj_block *)vm->heap.bins[bin].head;
    if (b) {
        vm->heap.bins[bin].head = NJ_BLOCK_LINK(b);
        if (vm->heap.bins[bin].count > 0u) {
            vm->heap.bins[bin].count--;
        }
    }
    return b;
}

/*
 * Find a free block of at least `total` bytes.  Searches the exact bin first,
 * then larger bins (which hold blocks of a known narrow size range), then the
 * big list.  The chosen block is unlinked and returned.
 */
static nj_block *nj_free_find(nj_vm *vm, nj_size total)
{
    int bin = nj_bin_for(total);
    int i;
    nj_block **pp;

    if (bin >= 0) {
        if (vm->heap.bins[bin].head) {
            return nj_free_pop_bin(vm, bin);
        }
        for (i = bin + 1; i < NJ_GC_BINS; i++) {
            if (vm->heap.bins[i].head) {
                return nj_free_pop_bin(vm, i);
            }
        }
    }
    /* Big list: first fit. */
    pp = (nj_block **)&vm->heap.big_free;
    while (*pp) {
        if ((*pp)->size >= total) {
            nj_block *b = *pp;
            *pp = (nj_block *)NJ_BLOCK_LINK(b);
            return b;
        }
        pp = (nj_block **)&NJ_BLOCK_LINK(*pp);
    }
    return NULL;
}

void *nj_alloc(nj_vm *vm, nj_size bytes)
{
    nj_block *b;
    nj_size total;

    if (!vm) {
        return NULL;
    }
    total = nj_align_up(bytes + NJ_BLOCK_HDR, NJ_OBJ_ALIGN);
    if (total < NJ_MIN_BLOCK) {
        total = NJ_MIN_BLOCK;
    }

    for (;;) {
        b = nj_free_find(vm, total);
        if (b) {
            /* Split off the tail when it is worth keeping. */
            nj_size remain = b->size - total;
            if (remain >= NJ_MIN_SPLIT) {
                nj_block *tail = (nj_block *)((nj_u1 *)b + total);
                tail->size = remain;
                tail->magic = NJ_BLOCK_FREE;
                nj_free_insert(vm, tail);
                b->size = total;
            }
            b->magic = NJ_BLOCK_MAGIC;
            memset(NJ_BLOCK_PAYLOAD(b), 0, b->size - NJ_BLOCK_HDR);
            return NJ_BLOCK_PAYLOAD(b);
        }

        /* Bump allocation. */
        if (vm->heap.bump + total <= vm->heap.size) {
            b = (nj_block *)(vm->heap.base + vm->heap.bump);
            b->size = total;
            b->magic = NJ_BLOCK_MAGIC;
            vm->heap.bump += total;
            if (vm->heap.bump > vm->heap.peak) {
                vm->heap.peak = vm->heap.bump;
            }
            memset(NJ_BLOCK_PAYLOAD(b), 0, b->size - NJ_BLOCK_HDR);
            return NJ_BLOCK_PAYLOAD(b);
        }

        /* Out of room: collect once and retry, then give up. */
        if (!vm->gc_requested) {
            vm->gc_requested = 1u;
            if (nj_gc_run(vm) == NJ_OK) {
                continue;
            }
        }
        return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* GC: mark                                                            */
/* ------------------------------------------------------------------ */
static void nj_gc_mark_object(nj_vm *vm, nj_object *o);

static void nj_gc_mark_children(nj_vm *vm, nj_object *o)
{
    nj_u4 i;

    if (!o) {
        return;
    }
    switch (o->kind) {
    case NJ_OBJ_ARRAY:
        if (o->aflags == NJ_ARRAY_T_REF && o->u.refs) {
            for (i = 0; i < o->length; i++) {
                nj_gc_mark_object(vm, o->u.refs[i]);
            }
        }
        break;
    case NJ_OBJ_PLAIN: {
        /* Instance fields: every reference-typed field of the class. */
        nj_class *c = o->cls;
        nj_u1 *base = (nj_u1 *)o;
        while (c) {
            nj_u2 f;
            for (f = 0; f < c->field_count; f++) {
                nj_field *fd = &c->fields[f];
                if ((fd->flags & NJ_ACC_STATIC) == 0u && fd->ptype == NJ_PT_OBJECT) {
                    nj_ref r;
                    memcpy(&r, base + fd->offset, sizeof(nj_ref));
                    nj_gc_mark_object(vm, r);
                }
            }
            c = c->super;
        }
        break;
    }
    case NJ_OBJ_STRING:
        /* A wrapped string owns a char[] through u.raw. */
        if ((o->aflags & NJ_STR_WRAPPED) != 0u) {
            nj_gc_mark_object(vm, (nj_object *)o->u.raw);
        }
        break;
    case NJ_OBJ_CLASS:
    default:
        break;
    }
}

/*
 * SAFETY: locals and operand slots are untyped.  A Java `int` can hold any
 * 32-bit pattern, and treating that as a pointer would be a wild read.  Every
 * Java object lives inside the single contiguous arena, so membership in
 * [base, base+bump) is a sound and nearly free validity test: anything outside
 * it is provably not an object and is skipped.
 */
static int nj_is_heap_ptr(nj_vm *vm, const void *p)
{
    const nj_u1 *q = (const nj_u1 *)p;

    if (!q || !vm->heap.base) {
        return 0;
    }
    if (q < vm->heap.base || q >= vm->heap.base + vm->heap.bump) {
        return 0;
    }
    /* Objects are 16-byte aligned; a stray int is unlikely to be, and the
     * alignment test rejects most garbage before it reaches ->kind. */
    if (((nj_size)(uintptr_t)q & (NJ_OBJ_ALIGN - 1u)) != 0u) {
        return 0;
    }
    return 1;
}

static void nj_gc_mark_object(nj_vm *vm, nj_object *o)
{
    /*
     * ORDER MATTERS: the range check has to come FIRST.
     *
     * The operand stack is untyped, so a Java `int` slot is routinely passed
     * here as if it were a pointer.  Reading o->mark before establishing that
     * the address is inside the arena is a wild read, and it is the one place
     * in the collector where a conservative scan can fault - which it did, on
     * a slot holding 0x30.
     */
    if (!nj_is_heap_ptr(vm, o)) {
        return;   /* untyped slot holding a non-reference value */
    }
    if (o->mark) {
        return;
    }
    if (o->kind > NJ_OBJ_THROW || o->size < sizeof(nj_object)) {
        return;   /* not a real object header */
    }
    o->mark = 1u;
    nj_gc_mark_children(vm, o);
}

void nj_gc_mark_ref(nj_vm *vm, nj_ref r)
{
    nj_gc_mark_object(vm, r);
}

void nj_gc_mark_value(nj_vm *vm, nj_value v)
{
    nj_gc_mark_object(vm, v.r);
}

static void nj_gc_mark_roots(nj_vm *vm)
{
    nj_u4 i;
    nj_u2 t;

    /* statics */
    for (i = 0; i < vm->nstatics; i++) {
        nj_gc_mark_object(vm, vm->statics[i].r);
    }
    /* interned strings */
    for (i = 0; i < vm->ninterned; i++) {
        nj_gc_mark_object(vm, vm->interned[i]);
    }
    /* class objects */
    for (i = 0; i < vm->nclasses; i++) {
        nj_class *c = vm->classes[i];
        if (!c) {
            continue;
        }
        nj_gc_mark_object(vm, c->class_obj);
    }
    /* VM-held refs */
    nj_gc_mark_object(vm, vm->exception);
    nj_gc_mark_object(vm, vm->app);
    nj_gc_mark_object(vm, vm->display);
    nj_gc_mark_object(vm, vm->current_displayable);
    nj_gc_mark_object(vm, vm->thread_group);

    /* temporary + registered C roots */
    for (i = 0; i < vm->nroots; i++) {
        nj_gc_mark_object(vm, vm->roots[i]);
    }
    for (i = 0; i < vm->nreg_roots; i++) {
        if (vm->reg_roots[i]) {
            nj_gc_mark_object(vm, *vm->reg_roots[i]);
        }
    }

    /* threads */
    for (t = 0; t < vm->nthreads; t++) {
        nj_thread *th = vm->threads[t];
        int f;
        if (!th) {
            continue;
        }
        nj_gc_mark_object(vm, th->obj);
        nj_gc_mark_object(vm, th->target);
        nj_gc_mark_object(vm, th->name);
        nj_gc_mark_object(vm, th->pending_exc);
        nj_gc_mark_object(vm, th->wait_monitor);
        for (f = 0; f < th->nframes; f++) {
            nj_frame *fr = &th->frames[f];
            nj_value *p;
            nj_value *end;
            /*
             * CONSERVATIVE SCAN.
             *
             * The interpreter keeps its operand-stack pointer in a C local and
             * only writes it back to the frame on a call or an unwind.  If a
             * GC runs in the middle of an instruction (an allocation inside a
             * newarray, say), fr->sp is stale and every value pushed since the
             * last save would be invisible - which silently frees live objects.
             *
             * Scanning the frame's whole slot region instead costs a few
             * hundred compares and is sound because nj_gc_mark_object()
             * range-checks every candidate against the arena before touching
             * it, so a stale int slot can never be mistaken for a reference.
             */
            end = fr->stack + (fr->m ? fr->m->max_stack : 0u);
            if (end > fr->sp) {
                /* nothing to do: `end` already covers everything below */
            }
            for (p = fr->locals; p < end; p++) {
                nj_gc_mark_object(vm, p->r);
            }
            nj_gc_mark_object(vm, fr->monitor);
        }
    }
}

/* ------------------------------------------------------------------ */
/* GC: sweep (with coalescing)                                         */
/* ------------------------------------------------------------------ */
static void nj_gc_sweep(nj_vm *vm)
{
    nj_u1 *p = vm->heap.base;
    nj_u1 *end = vm->heap.base + vm->heap.bump;
    nj_block *run = NULL;     /* start of the current run of free blocks */
    nj_u4 live = 0u;
    int i;

    for (i = 0; i < NJ_GC_BINS; i++) {
        vm->heap.bins[i].head = NULL;
        vm->heap.bins[i].count = 0u;
    }
    vm->heap.big_free = NULL;

    while (p < end) {
        nj_block *b = (nj_block *)p;
        nj_size sz = b->size;

        if (sz < NJ_MIN_BLOCK || sz > (nj_size)(end - p)) {
            break;   /* corrupt arena: stop rather than run off the end */
        }

        if (b->magic == NJ_BLOCK_MAGIC &&
            ((nj_object *)NJ_BLOCK_PAYLOAD(b))->mark) {
            /* Live: the mark byte lives in the object header. */
            ((nj_object *)NJ_BLOCK_PAYLOAD(b))->mark = 0u;
            live++;
            if (run) {
                nj_free_insert(vm, run);
                run = NULL;
            }
        } else {
            /* dead or already free: extend the run */
            b->magic = NJ_BLOCK_FREE;
            if (run) {
                run->size += sz;
            } else {
                run = b;
            }
        }
        p += sz;
    }
    if (run) {
        nj_free_insert(vm, run);
    }
    vm->live_objects = live;
}

nj_status nj_gc_run(nj_vm *vm)
{
    if (!vm || !vm->heap.base) {
        return NJ_ERR;
    }
    vm->gc_requested = 0u;
    nj_gc_mark_roots(vm);
    nj_gc_sweep(vm);
    vm->heap.gc_count++;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Roots                                                               */
/* ------------------------------------------------------------------ */
nj_u4 nj_root_mark(nj_vm *vm)
{
    return vm ? vm->nroots : 0u;
}

nj_ref nj_root(nj_vm *vm, nj_ref r)
{
    if (!vm) {
        return r;
    }
    if (vm->nroots == vm->root_cap) {
        nj_u4 cap = vm->root_cap ? vm->root_cap * 2u : 32u;
        nj_ref *nr = (nj_ref *)nj_host_alloc(vm, (nj_size)cap * sizeof(nj_ref));
        if (!nr) {
            return r;   /* cannot grow: the caller's ref is unprotected */
        }
        if (vm->roots) {
            memcpy(nr, vm->roots, (nj_size)vm->nroots * sizeof(nj_ref));
            nj_host_free(vm, vm->roots);
        }
        vm->roots = nr;
        vm->root_cap = cap;
    }
    vm->roots[vm->nroots++] = r;
    return r;
}

void nj_root_release(nj_vm *vm, nj_u4 mark)
{
    if (vm && mark <= vm->nroots) {
        vm->nroots = mark;
    }
}

nj_status nj_gc_register_root(nj_vm *vm, nj_ref *slot)
{
    nj_u4 i;

    if (!vm || !slot) {
        return NJ_ERR;
    }
    for (i = 0; i < vm->nreg_roots; i++) {
        if (vm->reg_roots[i] == slot) {
            return NJ_OK;
        }
    }
    if (vm->nreg_roots == vm->reg_root_cap) {
        nj_u4 cap = vm->reg_root_cap ? vm->reg_root_cap * 2u : 32u;
        nj_ref **nr = (nj_ref **)nj_host_alloc(vm, (nj_size)cap * sizeof(nj_ref *));
        if (!nr) {
            return NJ_ERR_NOMEM;
        }
        if (vm->reg_roots) {
            memcpy(nr, vm->reg_roots, (nj_size)vm->nreg_roots * sizeof(nj_ref *));
            nj_host_free(vm, vm->reg_roots);
        }
        vm->reg_roots = nr;
        vm->reg_root_cap = cap;
    }
    vm->reg_roots[vm->nreg_roots++] = slot;
    return NJ_OK;
}

void nj_gc_unregister_root(nj_vm *vm, nj_ref *slot)
{
    nj_u4 i;

    if (!vm || !slot) {
        return;
    }
    for (i = 0; i < vm->nreg_roots; i++) {
        if (vm->reg_roots[i] == slot) {
            vm->reg_roots[i] = vm->reg_roots[vm->nreg_roots - 1u];
            vm->nreg_roots--;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Object construction                                                 */
/* ------------------------------------------------------------------ */
nj_ref nj_new_instance(nj_vm *vm, nj_class *cls)
{
    nj_object *o;

    if (!vm || !cls) {
        return NULL;
    }
    o = (nj_object *)nj_alloc(vm, cls->instance_size);
    if (!o) {
        return NULL;
    }
    o->cls = cls;
    o->kind = NJ_OBJ_PLAIN;
    o->size = cls->instance_size;
    o->length = 0u;
    o->aflags = 0u;
    o->mark = 0u;
    vm->total_objects++;
    return o;
}

nj_ref nj_new_array(nj_vm *vm, nj_class *array_cls, nj_u4 length, int elem_kind)
{
    nj_object *o;
    nj_size elem = 1u;
    nj_size bytes;

    switch (elem_kind) {
    case NJ_ARRAY_T_BOOLEAN:
    case NJ_ARRAY_T_BYTE:   elem = 1u; break;
    case NJ_ARRAY_T_CHAR:
    case NJ_ARRAY_T_SHORT:  elem = 2u; break;
    case NJ_ARRAY_T_INT:
    case NJ_ARRAY_T_FLOAT:  elem = 4u; break;
    case NJ_ARRAY_T_REF:    elem = sizeof(nj_ref); break;
    case NJ_ARRAY_T_LONG:
    case NJ_ARRAY_T_DOUBLE: elem = 8u; break;
    default: return NULL;
    }

    /* Overflow-safe size computation. */
    if (length > (nj_u4)((~(nj_size)0) / elem) - 64u) {
        return NULL;
    }
    bytes = (nj_size)length * elem;
    o = (nj_object *)nj_alloc(vm, sizeof(nj_object) + bytes);
    if (!o) {
        return NULL;
    }
    o->cls = array_cls;
    o->kind = NJ_OBJ_ARRAY;
    o->aflags = (nj_u2)elem_kind;
    o->length = length;
    o->size = (nj_u4)(sizeof(nj_object) + bytes);
    o->mark = 0u;
    o->u.bytes = (nj_u1 *)o + sizeof(nj_object);
    vm->total_objects++;
    return o;
}

/* ------------------------------------------------------------------ */
/* Teardown                                                            */
/* ------------------------------------------------------------------ */
void nj_heap_destroy(nj_vm *vm)
{
    if (!vm) {
        return;
    }
    /* The Java arena: only release it when we allocated it ourselves
     * (external arenas belong to the embedder). */
    if (vm->heap.base && !vm->heap.external) {
        nj_host_free(vm, vm->heap.base);
    }
    vm->heap.base = NULL;
    vm->heap.size = 0u;
    vm->heap.bump = 0u;

    if (vm->roots) {
        nj_host_free(vm, vm->roots);
        vm->roots = NULL;
        vm->root_cap = 0u;
        vm->nroots = 0u;
    }
    if (vm->reg_roots) {
        nj_host_free(vm, vm->reg_roots);
        vm->reg_roots = NULL;
        vm->reg_root_cap = 0u;
        vm->nreg_roots = 0u;
    }
    /* Finally: release anything the modules left behind. */
    nj_host_free_all(vm);
}
