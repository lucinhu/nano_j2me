/*
 * nano_j2me - nj_internal.h
 *
 * Internal data structures of the virtual machine: classes, members, the
 * global constant-pool resolution cache, heap, threads and frames.
 *
 * Nothing in here is part of the public API.  The class library and the ports
 * talk to the VM through <nj2me/nj_vm.h>.
 */
#ifndef NJ_INTERNAL_H
#define NJ_INTERNAL_H

#include "nj2me/nj_config.h"
#include "nj2me/nj_types.h"
#include "nj2me/nj_vm.h"
#include "nj2me/nj_app.h"
#include "nj2me/nj_zip.h"
#include "nj2me/nj_gfx.h"
#include "nj2me/nj_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Member resolution kinds (constant-pool cache entries)               */
/* ------------------------------------------------------------------ */
#define NJ_LINK_NONE        0
#define NJ_LINK_FIELD_STATIC 1
#define NJ_LINK_FIELD_INST   2
#define NJ_LINK_METHOD_STATIC 3  /* invokestatic: call `target` directly    */
#define NJ_LINK_METHOD_INST   4  /* instance method: invokespecial calls    */
                                 /* `target` directly, invokevirtual and    */
                                 /* invokeinterface dispatch through the    */
                                 /* inline cache (recv_cls/ic_target).      */
#define NJ_LINK_STRING      7
#define NJ_LINK_CLASS       8

/* ------------------------------------------------------------------ */
/* Field                                                               */
/* ------------------------------------------------------------------ */
struct nj_field {
    const char *name;
    const char *desc;
    nj_class   *owner;
    nj_u2       flags;
    nj_u2       offset;   /* instance byte offset, or static slot index */
    nj_u2       const_cp; /* ConstantValue attribute CP index, 0 = none */
    nj_u1       ptype;    /* nj_ptype                                   */
    nj_u1       width;    /* slots: 1 or 2 (long/double)                */
};

/* ------------------------------------------------------------------ */
/* Exception handler table entry                                       */
/* ------------------------------------------------------------------ */
typedef struct nj_handler {
    nj_u2 start_pc;
    nj_u2 end_pc;
    nj_u2 handler_pc;
    nj_u2 catch_cp;   /* 0 == catch-all (finally) */
} nj_handler;

/* ------------------------------------------------------------------ */
/* Method                                                              */
/* ------------------------------------------------------------------ */
struct nj_method {
    const char   *name;
    const char   *desc;
    nj_class     *owner;
    const nj_u1  *code;
    nj_u4         code_len;
    const nj_handler *handlers;
    nj_u2         handler_count;
    nj_u2         flags;
    nj_u2         max_stack;
    nj_u2         max_locals;
    nj_u2         arg_slots; /* argument slots, excluding `this`   */
    nj_u1         ret_ptype; /* nj_ptype of the return value       */
    nj_native_fn  native;    /* bound implementation for ACC_NATIVE */
};

/* ------------------------------------------------------------------ */
/* Constant pool resolution cache (global, open-addressed)             */
/* ------------------------------------------------------------------ */
/*
 * There is deliberately no global vtable.  Each constant-pool entry that has
 * been resolved once becomes an INLINE CACHE:
 *
 *     target      - the statically resolved member (invokespecial/static/field)
 *     recv_cls    - receiver class observed last time (virtual dispatch)
 *     ic_target   - the method that class answered with
 *
 * Real MIDlets are overwhelmingly monomorphic (one concrete receiver type per
 * call site), so `recv->cls == recv_cls` hits almost every time and dispatch
 * costs one pointer compare instead of a hierarchy walk.  This trades a
 * handful of bytes per call site for the megabytes a per-class vtable array
 * would need on a 512 KB microcontroller.
 */
typedef struct nj_cpentry {
    nj_class *cls;       /* key: declaring class                      */
    nj_u4     cp_index;  /* key: constant pool index                  */
    void     *target;    /* nj_method* or nj_field*                   */
    nj_class *recv_cls;  /* inline-cache: last receiver class         */
    void     *ic_target; /* inline-cache: nj_method* for recv_cls     */
    nj_u2     slot;      /* field byte offset or static slot index    */
    nj_u1     kind;      /* NJ_LINK_*                                 */
    nj_u1     used;
} nj_cpentry;

/* ------------------------------------------------------------------ */
/* Class                                                               */
/* ------------------------------------------------------------------ */
#define NJ_CLASS_LOADED  1
#define NJ_CLASS_LINKED  2
#define NJ_CLASS_READY   3

struct nj_class {
    const char *name;       /* internal, slash separated, e.g. java/lang/String */
    const char *super_name; /* builtin classes: superclass name (else NULL)     */
    nj_class   *super;
    nj_class  **interfaces;
    nj_u2       *iface_cp;  /* CP indices of the interfaces, resolved at link */
    nj_u2       ninterfaces;

    const nj_u1 *data;      /* raw class file bytes (kept for lazy CP resolution) */
    nj_u4        data_len;
    nj_u4        data_off;  /* offset of the constant pool in `data` */
    nj_u2        cp_count;
    const char **cp_utf8;   /* [cp_count] NUL-terminated UTF8 constants, or NULL */

    nj_field   *fields;  nj_u2 field_count;   /* declared only */
    nj_method  *methods; nj_u2 method_count;  /* declared only */

    nj_u2       instance_size;  /* total bytes, including super */
    nj_u2       id;
    nj_u2       flags;
    nj_u2       super_cp;    /* CP index of the superclass, 0 for Object */
    nj_u2       this_cp;     /* CP index of this class */
    nj_u4      *cp_info;     /* [cp_count]: (byte offset << 4) | tag */

    nj_ref      class_obj;   /* java.lang.Class mirror, or NULL */
    nj_class   *component_cls; /* array component type (arrays only) */

    nj_u1       state;
    nj_u1       is_array;
    nj_u1       array_elem;  /* NJ_ARRAY_T_* for arrays, 0 otherwise */
    nj_u1       prim;        /* primitive kind for primitive mirrors  */

    struct nj_class *next_hash; /* class-table hash chain */
};

/* ------------------------------------------------------------------ */
/* Threads and frames                                                  */
/* ------------------------------------------------------------------ */
#define NJ_THREAD_NEW      0
#define NJ_THREAD_RUNNING  1
#define NJ_THREAD_SLEEPING 2
#define NJ_THREAD_WAITING  3
#define NJ_THREAD_BLOCKED  4
#define NJ_THREAD_DEAD     5

typedef struct nj_frame {
    nj_method *m;
    nj_u4      pc;
    nj_value  *locals;   /* == stack_mem + base */
    nj_value  *stack;    /* operand stack base */
    nj_value  *sp;       /* next free operand slot */
    nj_value  *argbase;  /* where the caller's arguments start */
    nj_u2      total_slots; /* argument slots consumed from the caller */
    nj_ref     monitor;  /* monitor held by a synchronized method, else NULL */
    nj_u1      is_native;
} nj_frame;

struct nj_thread {
    nj_value  *stack_mem;   /* NJ_MAX_STACK_SLOTS slots */
    nj_value  *stack_end;
    nj_value  *top;         /* bump pointer for frame allocation */

    nj_frame   frames[NJ_MAX_FRAMES];
    int        nframes;

    nj_ref     obj;         /* java.lang.Thread mirror OR NULL */
    nj_ref     target;      /* Runnable for Thread(Runnable) */
    nj_ref     entry_this;  /* receiver for the BOTTOM frame; defaults to obj */
    nj_ref     name;
    nj_ref     pending_exc; /* exception being propagated into this thread */

    nj_s8      wake_at_ms;  /* absolute deadline when SLEEPING */
    nj_ref     wait_monitor;/* monitor being waited on */
    nj_s8      wait_deadline;
    nj_u4      wakeup_seq;  /* notify() wakeup counter for wait() */

    nj_u1      state;
    nj_u1      daemon;
    nj_u1      started;
    nj_u1      is_main;
    nj_s4      priority;
    nj_s4      exit_value;
    nj_value   ret_value;   /* result of a C-initiated call */
    int        stop_depth;  /* nj_exec returns when nframes reaches this */

    void      *port_ud;     /* free for the port */
    nj_u4      id;
};

/* ------------------------------------------------------------------ */
/* Heap                                                                */
/* ------------------------------------------------------------------ */
#define NJ_GC_BINS 24       /* size classes, 16 bytes apart up to 384 */

typedef struct nj_gcbin {
    void  *head;            /* free list head  */
    nj_u4  count;
} nj_gcbin;

typedef struct nj_heap {
    nj_u1  *base;
    nj_size size;
    nj_size used;           /* high-water mark of the bump pointer */
    nj_size bump;           /* current bump offset */
    nj_size peak;
    nj_u4   gc_count;
    nj_gcbin bins[NJ_GC_BINS];
    void   *big_free;       /* free list for allocations > bin range */
    nj_u1   external;       /* base came from the port: never free it */
} nj_heap;

/* ------------------------------------------------------------------ */
/* Class source (the JAR loader plugs in here)                         */
/* ------------------------------------------------------------------ */
/*
 * The VM never touches a file system or a JAR directly.  Whatever can produce
 * class bytes implements this one function; nj_app.c provides the JAR-backed
 * implementation.  This keeps the core testable with in-memory fixtures and
 * lets an ESP32 build serve classes straight out of a flash partition.
 */
typedef struct nj_class_source {
    /* On success, store a freshly allocated buffer in *out_bytes (ownership
     * passes to the VM, which copies it and frees it immediately) and its
     * length in *out_len. */
    nj_status (*load)(nj_vm *vm, void *ud, const char *name,
                      nj_u1 **out_bytes, nj_u4 *out_len);
    void *ud;
} nj_class_source;

typedef struct nj_native_binding {
    const char  *cls;
    const char  *name;
    const char  *desc;
    nj_native_fn fn;
} nj_native_binding;

typedef struct nj_codec_reg {
    nj_codec codec;      /* a per-VM COPY, so attach() can set codec.ud */
    int      priority;
} nj_codec_reg;

/* ------------------------------------------------------------------ */
/* The VM                                                              */
/* ------------------------------------------------------------------ */
#define NJ_CLASS_HASH 64

/* Reasons a constant-pool entry failed to resolve. */
#define NJ_RESOLVE_OK     0u
#define NJ_RESOLVE_CLASS  1u
#define NJ_RESOLVE_FIELD  2u
#define NJ_RESOLVE_METHOD 3u
#define NJ_RESOLVE_FORMAT 4u

struct nj_vm {
    nj_heap heap;

    /* class table */
    nj_class  *class_head[NJ_CLASS_HASH];
    nj_class  *classes[NJ_MAX_CLASSES];
    nj_u2      nclasses;
    nj_u2      next_class_id;

    /* frequently used classes (filled lazily, keeps hot paths hash-free) */
    nj_class  *cls_string;
    nj_class  *cls_class;
    nj_class  *cls_object;
    nj_class  *cls_throwable;
    nj_class  *cls_object_array;

    /* constant-pool resolution cache */
    nj_cpentry *cp_cache;
    nj_u2       cp_cache_size;
    nj_u2       cp_cache_used;
    nj_u2       cp_cache_limit;   /* rehash threshold */
    nj_u4       cp_cache_hits;
    nj_u4       cp_cache_misses;

    /* statics */
    nj_value  *statics;
    nj_u4      nstatics;

    /* threads */
    nj_thread *threads[NJ_MAX_THREADS];
    nj_thread *cur;
    nj_u2      nthreads;
    nj_u4      next_thread_id;
    nj_u4      scheduler_ticks;

    /* pending exception */
    nj_ref     exception;
    nj_u4      exception_pc;
    nj_method *exception_method;

    /* Why the last constant-pool resolution failed.
     *
     * "it threw NoClassDefFoundError" is the least useful message a JVM can
     * produce, and the most common real cause is not a missing class at all:
     * it is a method or field that exists with a different descriptor.  A
     * version of a library built against a slightly different API produces
     * exactly that, and without the descriptor in the message it takes hours
     * to spot.  Recording the reason here lets the interpreter raise
     * NoSuchMethodError / NoSuchFieldError with the full signature instead. */
    nj_u1       resolve_fail;      /* NJ_RESOLVE_* */
    const char *resolve_cls;
    const char *resolve_name;
    const char *resolve_desc;

    /* interned strings */
    nj_ref     interned[NJ_MAX_INTERNED];
    nj_u2      ninterned;

    /* MIDlet state */
    nj_ref     app;             /* the MIDlet instance */
    nj_class  *app_class;
    nj_ref     display;         /* the singleton Display */
    nj_ref     current_displayable;
    nj_ref     thread_group;

    /* seeded identity hash */
    nj_u4      hash_seed;

    /* port */
    const nj_port *port;

    /* where class bytes come from (JAR loader, flash partition, test fixture) */
    nj_class_source *source;

    /* ---- MIDP layer state ----
     * Owning these here rather than in file-scope variables is what lets a
     * MIDlet be started, stopped and restarted in one process. */
    nj_fontset    fonts;          /* mutable font registry (see nj_gfx.h)  */
    nj_surface    screen;         /* view of the UI framebuffer            */
    nj_u1        *screen_pixels;  /* the framebuffer itself, host-allocated */
    nj_u4         midp_flags;     /* NJ_MIDP_* capability bits             */
    nj_u1         repaint_pending;
    nj_u4         frames_presented;
    nj_u2         screen_w;
    nj_u2         screen_h;

    /* image codec registry: the list of providers this VM will use.
     * Per-VM, so a port can install a hardware decoder for one VM and not
     * another, and so nothing survives nj_vm_destroy(). */
    nj_codec_reg *codecs;
    nj_u2         ncodecs;
    nj_u2         codec_cap;

    /* native method registry (per-VM hash; no GlobalState) */
    nj_native_binding *natives;
    nj_u2              nnatives;
    nj_u2              native_cap;
    nj_u4              native_hits;
    nj_u4              native_misses;

    /* builtin (C-implemented) class names, for fast "is this ours?" checks */
    nj_class         **builtins;
    nj_u2              nbuiltins;
    nj_u2              builtin_cap;

    /* control */
    nj_u1      state;
    nj_u1      running;
    nj_u1      initialized;
    nj_u2      verbose;      /* log level: 0=error .. 3=debug */
    nj_s4      exit_code;

    /* native call depth */
    nj_u2      native_depth;

    /* ---- GC roots owned by C code (class library, ports) ---- */
    nj_ref    *roots;          /* temporary root stack (JNI-local style) */
    nj_u4      nroots;
    nj_u4      root_cap;
    nj_ref   **reg_roots;      /* addresses of long-lived C-side ref slots */
    nj_u4      nreg_roots;
    nj_u4      reg_root_cap;

    /* ---- host allocation registry ----
     * Every byte handed out by nj_host_alloc() is linked here, so
     * nj_vm_destroy() can release all of it unconditionally.  This is what
     * makes "exit leaves nothing behind" true even if a module forgets a
     * free: teardown is a single sweep of this list. */
    struct nj_hosthdr *host_head;
    nj_u4      host_count;      /* live host allocations */
    nj_u4      host_peak;
    nj_u8      host_bytes;

    /* GC bookkeeping / observability */
    nj_u4      live_objects;    /* live Java objects counted at last sweep */
    nj_u4      total_objects;
    nj_u1      gc_requested;
    nj_u1      in_exception_new; /* re-entrancy guard for nj_exception_new */
    nj_u1      gc_disabled;     /* set while a native holds unrooted refs */
#if NJ_ENABLE_DEBUG
    nj_u1      trace;           /* NJ_TRACE: print every bytecode */
    const char *trace_filter;   /* only methods whose name contains this */
#endif

    /*
     * Execution counters.
     *
     * Per-VM, like everything else, so two VMs never share a total and nothing
     * lives in .bss.  They are the only way to say anything quantitative about
     * the interpreter ("N million bytecodes per second") and they make a
     * regression visible without a profiler.
     */
    nj_u8      insns;           /* bytecodes dispatched */
    nj_u8      insns_peak;      /* not used for now; reserved, keeps 16B aligned */
    nj_u8      native_calls;
    nj_u8      frames;          /* frames presented to the port */

    /* scratch: reusable buffers owned by the VM */
    nj_u2     *scratch_u2;
    nj_u4      scratch_u2_len;

    void      *loader_ud;
};

/* VM states */
#define NJ_VM_READY     0
#define NJ_VM_RUNNING   1
#define NJ_VM_STOPPING  2
#define NJ_VM_DEAD      3

/* ------------------------------------------------------------------ */
/* Heap / GC (nj_heap.c)                                               */
/* ------------------------------------------------------------------ */
nj_status nj_heap_init(nj_vm *vm, void *block, nj_size size);
void      nj_heap_destroy(nj_vm *vm);
void     *nj_alloc(nj_vm *vm, nj_size bytes);         /* raw, zeroed */
nj_ref    nj_new_instance(nj_vm *vm, nj_class *cls);
nj_ref    nj_new_array(nj_vm *vm, nj_class *array_cls, nj_u4 length, int elem_kind);
nj_status nj_gc_run(nj_vm *vm);
void      nj_gc_mark_ref(nj_vm *vm, nj_ref r);
int       nj_heap_owns(nj_vm *vm, const void *p);   /* range check for untyped slots */
void      nj_gc_mark_value(nj_vm *vm, nj_value v);
nj_size   nj_heap_free_bytes(nj_vm *vm);
nj_size   nj_heap_used_bytes(nj_vm *vm);
void     *nj_host_alloc(nj_vm *vm, nj_size n);        /* tracked, zeroed */
void      nj_host_free(nj_vm *vm, void *p);
nj_u4     nj_host_live_count(nj_vm *vm);

/* Temporary GC roots.  Any C code that holds an nj_ref across a call which
 * might allocate (or reach a GC safe point) must root it:
 *
 *     nj_u4 mark = nj_root_mark(vm);
 *     nj_ref a = nj_root(vm, nj_object_new(vm, "java/lang/Object"));
 *     ...
 *     nj_root_release(vm, mark);
 */
nj_u4  nj_root_mark(nj_vm *vm);
nj_ref nj_root(nj_vm *vm, nj_ref r);
void   nj_root_release(nj_vm *vm, nj_u4 mark);
/* Register the address of a long-lived C-side nj_ref slot. */
nj_status nj_gc_register_root(nj_vm *vm, nj_ref *slot);
void      nj_gc_unregister_root(nj_vm *vm, nj_ref *slot);

/* ------------------------------------------------------------------ */
/* Class loading / linking (nj_class.c)                                */
/* ------------------------------------------------------------------ */
nj_class *nj_class_load(nj_vm *vm, const char *internal_name, const nj_u1 *bytes, nj_u4 len);
nj_class *nj_class_find(nj_vm *vm, const char *internal_name);
nj_class *nj_class_resolve(nj_vm *vm, const char *internal_name); /* load if needed */
nj_class *nj_class_find_or_create_array(nj_vm *vm, nj_class *elem, int dims);
nj_status nj_class_link(nj_vm *vm, nj_class *c);
nj_status nj_cp_cache_init(nj_vm *vm, nj_u2 size);
nj_status nj_class_ensure_refs(nj_vm *vm, nj_class *c);

/* accessors used by the interpreter */
const char *nj_cp_utf8(nj_class *c, nj_u2 index);
nj_status   nj_cp_resolve(nj_vm *vm, nj_class *c, nj_u2 index, nj_cpentry **out);
/* Raise the Java error matching the recorded resolution failure, with the
 * full signature in the message.  Call after nj_cp_resolve returns non-NJ_OK. */
nj_status   nj_resolve_error(nj_vm *vm);
nj_method  *nj_method_lookup(nj_class *c, const char *name, const char *desc);
const char *nj_cp_class_name(nj_class *c, nj_u2 class_index);
nj_ref      nj_array_new_of(nj_vm *vm, const char *array_desc, nj_u4 len);
nj_field   *nj_field_lookup_in(nj_class *c, const char *name, const char *desc, int is_static);
int         nj_class_is_subclass_of(nj_class *c, nj_class *maybe_super);
int         nj_class_is_assignable(nj_class *from, nj_class *to);
const char *nj_intern_cstr(nj_vm *vm, const char *s);
/* Parse a field descriptor: returns the nj_ptype and writes the slot width. */
nj_u1       nj_desc_ptype(const char *desc, nj_u1 *width);
/* Number of argument slots a method descriptor consumes (excluding `this`). */
nj_u2       nj_desc_arg_slots(const char *desc);
nj_u1       nj_desc_ret_ptype(const char *desc);
/* 0 = primitive in out[0] (out[1] zeroed), 1 = object ref in *out_ref, -1 = bad */
int         nj_cp_const(nj_vm *vm, nj_class *c, nj_u2 index, int allow_string,
                        nj_value *out, nj_ref *out_ref);
nj_status   nj_class_init(nj_vm *vm, nj_class *c);

/* String representation flag (nj_object.aflags for NJ_OBJ_STRING). */
#define NJ_STR_WRAPPED 0x0001u

/* ------------------------------------------------------------------ */
/* Strings (nj_string.c)                                               */
/* ------------------------------------------------------------------ */
nj_ref      nj_new_string_utf8(nj_vm *vm, const char *s, int len);
nj_ref      nj_new_string_utf16(nj_vm *vm, const nj_u2 *s, int len);
nj_ref      nj_new_string_ascii(nj_vm *vm, const char *s);
nj_u2      *nj_string_chars(nj_ref s);      /* UTF-16 payload */
nj_u4       nj_string_len(nj_ref s);
int         nj_utf8_to_utf16(const char *src, int src_len, nj_u2 *dst, int dst_cap);
int         nj_utf16_to_utf8(const nj_u2 *src, int src_len, char *dst, int dst_cap);
nj_ref      nj_intern(nj_vm *vm, nj_ref s);
int         nj_string_equals_cstr(nj_ref s, const char *cstr);
int         nj_string_hash(nj_ref s);
nj_ref      nj_string_concat(nj_vm *vm, nj_ref a, nj_ref b);
nj_status   nj_string_set_chars(nj_vm *vm, nj_ref s, const nj_u2 *chars, nj_u4 len);
nj_s4       nj_identity_hash(nj_vm *vm, nj_ref o);

/* ------------------------------------------------------------------ */
/* Threads (nj_thread.c)                                               */
/* ------------------------------------------------------------------ */
nj_thread *nj_thread_create(nj_vm *vm, nj_ref thread_obj, int is_main);
void       nj_thread_destroy(nj_vm *vm, nj_thread *t);
nj_status  nj_thread_start(nj_vm *vm, nj_thread *t, nj_method *run);
nj_status  nj_frame_push(nj_vm *vm, nj_thread *th, nj_method *m,
                         nj_value *argbase, nj_u2 total_slots);
void       nj_frame_pop(nj_vm *vm, nj_thread *th);
nj_status  nj_scheduler_run(nj_vm *vm);
nj_status  nj_scheduler_step(nj_vm *vm);
nj_status  nj_scheduler_run_until_idle(nj_vm *vm, nj_u4 budget_ms);
nj_s8      nj_now_ms(nj_vm *vm);
void       nj_vm_stop(nj_vm *vm, nj_s4 exit_code);
nj_status  nj_vm_monitor_wait(nj_vm *vm, nj_thread *t, nj_ref obj, nj_s8 timeout_ms);
int        nj_vm_monitor_notify(nj_vm *vm, nj_ref obj, int all);
nj_status  nj_thread_sleep(nj_vm *vm, nj_thread *t, nj_s8 ms);
void       nj_vm_monitor_enter(nj_vm *vm, nj_ref obj);
void       nj_vm_monitor_exit(nj_vm *vm, nj_ref obj);

/* ------------------------------------------------------------------ */
/* Native function signature helpers                                   */
/* ------------------------------------------------------------------ */
/*
 * Every native has the same four-parameter signature, so most of them do not
 * touch all four arguments.  Tagging the parameters here keeps
 * -Wall -Wextra -Werror clean without sprinkling (void) casts through the
 * whole class library.
 */
#if defined(__GNUC__) || defined(__clang__)
#  define NJ_MAYBE_UNUSED __attribute__((unused))
#else
#  define NJ_MAYBE_UNUSED
#endif

#define NJ_NATIVE_FN(name)                                            \
    static nj_status name(nj_vm *vm NJ_MAYBE_UNUSED,                  \
                          nj_thread *th NJ_MAYBE_UNUSED,              \
                          nj_value *args NJ_MAYBE_UNUSED,             \
                          nj_value *ret NJ_MAYBE_UNUSED)

/* ------------------------------------------------------------------ */
/* Natives (nj_native.c)                                               */
/* ------------------------------------------------------------------ */
nj_status    nj_natives_init(nj_vm *vm);
void         nj_natives_shutdown(nj_vm *vm);
nj_native_fn nj_native_find(nj_vm *vm, const char *cls, const char *name,
                            const char *desc);
/* Every class-library module exposes its bindings through these; nj_native.c
 * aggregates them, and the VM holds the resulting hash so there is no global
 * registry. */
void nj_natives_collect(nj_vm *vm);

/* Implemented in nj_builtins.c: materialises every C-implemented class. */
void nj_builtins_register_all(nj_vm *vm);
/* Create one builtin class from its descriptor (nj_class.c). */
nj_class *nj_class_create_builtin(nj_vm *vm, const nj_builtin_class *bc);

/* ------------------------------------------------------------------ */
/* Interpreter (nj_vm.c)                                               */
/* ------------------------------------------------------------------ */
/* Run one thread until it blocks, dies or the quantum expires.
 * The pending exception, if any, lives in vm->exception. */
nj_status nj_exec(nj_vm *vm, nj_thread *th, int stop_depth);
nj_status nj_invoke_native(nj_vm *vm, nj_thread *th, nj_method *target,
                           nj_value *argbase, nj_u2 total, nj_value *ret);
nj_status nj_vm_throw(nj_vm *vm, const char *class_name, const char *msg);
nj_status nj_vm_throw_ref(nj_vm *vm, nj_ref exc);
nj_status nj_vm_throw_new(nj_vm *vm, const char *class_name);
nj_ref    nj_vm_exception_new(nj_vm *vm, const char *class_name, const char *msg);
nj_status nj_vm_call(nj_vm *vm, nj_ref obj, const char *cls,
                     const char *name, const char *desc,
                     nj_value *args, int nargs, nj_value *ret);
nj_status nj_vm_call_static(nj_vm *vm, const char *cls,
                            const char *name, const char *desc,
                            nj_value *args, int nargs, nj_value *ret);
void      nj_fatal(nj_vm *vm, const char *fmt, ...);
#if NJ_ENABLE_DEBUG
void      nj_trace_step(nj_vm *vm, nj_method *m, nj_u4 pc, nj_u4 depth,
                        nj_value *locals, nj_value *stack, nj_value *sp);
int       nj_trace_enabled(nj_vm *vm);
#endif
void      nj_logf(nj_vm *vm, int level, const char *fmt, ...);

/*
 * NOTE: there is intentionally no nj_vm_current()/nj_vm_set_current().
 * The runtime keeps zero mutable global state, so several nj_vm instances can
 * coexist and every entry point takes the VM explicitly.  Natives receive it
 * as their first parameter; the interpreter and the class library thread it
 * through.  Do not reintroduce a hidden "current VM" singleton.
 */

#ifdef __cplusplus
}
#endif

#endif /* NJ_INTERNAL_H */
