/*
 * nano_j2me - nj_vm.h
 *
 * The API the pure-C class library (MIDP/CLDC natives) and embedders use to
 * touch Java objects: creation, field access, method invocation, exceptions,
 * strings, arrays.
 *
 * Everything is expressed in terms of nj_ref plus "class name + member name +
 * descriptor" triples, so the class library never needs to know about vtable
 * slots or instance layouts.  Look the member up once and cache the nj_field*
 * in a static if it is on a hot path - nj_field_lookup is hashed but still
 * much slower than a direct offset.
 *
 * Descriptors use the standard JVM syntax: "()V", "(II)I", "(Ljava/lang/String;)V".
 */
#ifndef NJ_VM_H
#define NJ_VM_H

#include "nj_types.h"
#include "nj_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nj_thread nj_thread;
typedef struct nj_field nj_field;
typedef struct nj_method nj_method;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */
nj_vm  *nj_vm_create(const nj_port *port, nj_size heap_bytes);

/*
 * Tear the VM down completely and return the number of outstanding host
 * allocations that were released in the process.
 *
 * The whole runtime is built so that this number is the ONLY thing left to
 * release - the Java arena, class bytes, member tables, thread stacks, the
 * native registry, the constant-pool cache.  After this call the process holds
 * no memory belonging to the VM, which is what lets a MIDlet be started and
 * stopped repeatedly on a device that never reboots.  A caller can use the
 * return value as a leak assertion in a test or a boot self-check.
 */
nj_u4   nj_vm_destroy(nj_vm *vm);


nj_status nj_vm_start_app(nj_vm *vm, nj_ref midlet);  /* calls MIDlet.startApp() */
/*
 * Create a MIDlet instance.
 *
 * `class_name` IS REQUIRED - there is no "pass NULL and it picks one from the
 * manifest".  The name comes from the manifest, via nj_app_midlet_class():
 *
 *     nj_ref m = nj_vm_new_midlet(vm, nj_app_midlet_class(app, 0), NULL);
 *     if (!m) { ... }                    // NULL on failure
 *     nj_vm_start_app(vm, m);
 *
 * Two things about this are easy to get wrong and both cost time here: passing
 * NULL returns NULL immediately, and the return is a REF, so comparing it
 * against NJ_OK (which is 0) reads a NULL as success.  Test it for NULL.
 *
 * On failure the reason is returned through out_exc when it is non-NULL, and is
 * NOT left on the VM.
 */
nj_ref  nj_vm_new_midlet(nj_vm *vm, const char *class_name, nj_ref *out_exc);

nj_thread *nj_thread_self(nj_vm *vm);
void       nj_yield(nj_vm *vm);

/* ------------------------------------------------------------------ */
/* Class objects                                                       */
/* ------------------------------------------------------------------ */
/* Look up a class in dotted ("java.lang.String") or slashed
 * ("java/lang/String") form.  Returns NULL if the class cannot be loaded. */
nj_class *nj_class_lookup(nj_vm *vm, const char *name);
nj_ref    nj_class_object(nj_vm *vm, const char *name);   /* java.lang.Class */

/* Resolve a field by name.  `desc` may be NULL to match by name only.
 * Returns NULL when the field does not exist (no exception is raised). */
nj_field *nj_field_lookup(nj_vm *vm, const char *cls, const char *name,
                          const char *desc, int is_static);

/* ------------------------------------------------------------------ */
/* Object and array creation                                           */
/* ------------------------------------------------------------------ */
nj_ref nj_object_new(nj_vm *vm, const char *cls);                 /* NULL on OOM */
nj_ref nj_array_new(nj_vm *vm, const char *elem_cls_or_prim, nj_u4 len);
nj_ref nj_byte_array_new(nj_vm *vm, nj_u4 len);
nj_ref nj_char_array_new(nj_vm *vm, nj_u4 len);
nj_ref nj_short_array_new(nj_vm *vm, nj_u4 len);
nj_ref nj_int_array_new(nj_vm *vm, nj_u4 len);
nj_ref nj_object_array_new(nj_vm *vm, const char *elem_cls, nj_u4 len);

nj_u4  nj_array_length(nj_ref a);
void  *nj_array_data(nj_ref a);                 /* raw payload */
nj_s4  nj_array_get_int(nj_ref a, nj_u4 i);
void   nj_array_set_int(nj_ref a, nj_u4 i, nj_s4 v);
nj_ref nj_array_get_ref(nj_ref a, nj_u4 i);
void   nj_array_set_ref(nj_ref a, nj_u4 i, nj_ref v);

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */
nj_ref nj_string_new(nj_vm *vm, const char *utf8);
nj_ref nj_string_new_n(nj_vm *vm, const char *utf8, int len);
nj_ref nj_string_new_chars(nj_vm *vm, const nj_u2 *chars, int len);
nj_ref nj_string_new_from_int(nj_vm *vm, nj_s4 v);
nj_ref nj_string_intern(nj_vm *vm, nj_ref s);
nj_u4  nj_string_length(nj_ref s);
const nj_u2 *nj_string_data(nj_ref s);
/* Copy into a caller buffer as modified UTF-8.  Always NUL-terminates. */
int    nj_string_to_utf8(nj_ref s, char *out, int cap);
int    nj_string_equals_utf8(nj_ref s, const char *utf8);

/* ------------------------------------------------------------------ */
/* Field access (works for both static and instance fields)            */
/* ------------------------------------------------------------------ */
/*
 * The VM is required because a static field is not reachable from an object
 * pointer: it lives in the VM's static slot array at `field->offset`.
 * For a static field pass obj == NULL.
 */
nj_s4    nj_field_read_int (nj_vm *vm, nj_ref obj, nj_field *f);
void     nj_field_write_int(nj_vm *vm, nj_ref obj, nj_field *f, nj_s4 v);
nj_s8    nj_field_read_long (nj_vm *vm, nj_ref obj, nj_field *f);
void     nj_field_write_long(nj_vm *vm, nj_ref obj, nj_field *f, nj_s8 v);
nj_ref   nj_field_read_ref (nj_vm *vm, nj_ref obj, nj_field *f);
void     nj_field_write_ref(nj_vm *vm, nj_ref obj, nj_field *f, nj_ref v);

/* Look a field up once and cache the nj_field* in a static const-free slot of
 * your own objects; these are the hot-path accessors. */
#define NJ_F(vm, cls, name, desc)  nj_field_lookup((vm), (cls), (name), (desc), 0)
#define NJ_FS(vm, cls, name, desc) nj_field_lookup((vm), (cls), (name), (desc), 1)

/* Short forms used throughout the class library. */
static inline nj_s4  nj_iget(nj_vm *vm, nj_ref o, nj_field *f) { return nj_field_read_int(vm, o, f); }
static inline void   nj_iput(nj_vm *vm, nj_ref o, nj_field *f, nj_s4 v) { nj_field_write_int(vm, o, f, v); }
static inline nj_ref nj_rget(nj_vm *vm, nj_ref o, nj_field *f) { return nj_field_read_ref(vm, o, f); }
static inline void   nj_rput(nj_vm *vm, nj_ref o, nj_field *f, nj_ref v) { nj_field_write_ref(vm, o, f, v); }

/* ------------------------------------------------------------------ */
/* Exceptions                                                          */
/* ------------------------------------------------------------------ */
/* Raise a Java exception and return NJ_ERR, so a native can write
 *     if (bad) return NJ_THROW(vm, "java/lang/NullPointerException", "msg");
 * `msg` may be NULL. */
nj_status nj_throw(nj_vm *vm, const char *cls, const char *msg);
nj_status nj_throw_oom(nj_vm *vm);
#define NJ_THROW(vm, cls, msg) return nj_throw((vm), (cls), (msg))

/* Raise a pre-built Throwable instance. */
nj_status nj_throw_ref(nj_vm *vm, nj_ref throwable);

/* Build (but do not raise) an exception instance. */
nj_ref nj_exception_new(nj_vm *vm, const char *cls, const char *msg);

int  nj_has_exception(nj_vm *vm);
nj_ref nj_take_exception(nj_vm *vm);   /* steals vm->exception */
void nj_clear_exception(nj_vm *vm);

/* ------------------------------------------------------------------ */
/* Calling Java from C                                                 */
/* ------------------------------------------------------------------ */
/* `args` holds the arguments in JVM slot order (long/double take 2 slots).
 * For an instance call, pass the receiver in `obj`; it is NOT in args.
 * Returns NJ_OK and writes *ret (may be NULL for void), or an error status
 * with vm->exception set. */
nj_status nj_call(nj_vm *vm, nj_ref obj, const char *cls,
                  const char *name, const char *desc,
                  nj_value *args, nj_value *ret);
nj_status nj_call_static(nj_vm *vm, const char *cls,
                         const char *name, const char *desc,
                         nj_value *args, nj_value *ret);

/* Non-virtual call (`invokespecial` semantics) - used for <init> and super. */
nj_status nj_call_special(nj_vm *vm, nj_ref obj, const char *cls,
                          const char *name, const char *desc,
                          nj_value *args, nj_value *ret);

/* Convenience wrappers. */
nj_s4  nj_call_int(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                   const char *desc, nj_value *args);
nj_ref nj_call_ref(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                   const char *desc, nj_value *args);

/* ------------------------------------------------------------------ */
/* Native argument helpers                                             */
/* ------------------------------------------------------------------ */
/* Inside a native, `args` points at parameter slot 0, so for an instance
 * method args[-1] is `this`. */
/*
 * NOTE: the index parameter is deliberately called `idx`, not `i`: the
 * preprocessor rewrites every occurrence of a parameter name in the body, so
 * a parameter named `i` would corrupt the `.i` member access.
 */
#define NJ_THIS(args)          ((args)[-1].r)
#define NJ_ARG_REF(a, idx)     ((a)[(idx)].r)
#define NJ_ARG_INT(a, idx)     ((a)[(idx)].i)
#define NJ_ARG_LONG(a, idx)    ((a)[(idx)].j)
#define NJ_ARG_FLOAT(a, idx)   ((a)[(idx)].f)
#define NJ_ARG_DOUBLE(a, idx)  ((a)[(idx)].d)

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */
typedef struct nj_vm_stats {
    nj_size heap_size;
    nj_size heap_used;
    nj_size heap_peak;
    nj_u4   gc_count;
    nj_u4   live_objects;
    nj_u4   total_objects;
    nj_u2   classes;
    nj_u2   threads;
    nj_u2   cp_cache_entries;
    nj_u4   cp_cache_hits;
    nj_u4   cp_cache_misses;
    nj_u2   natives;
    nj_u4   native_misses;
    nj_u2   codecs;
    nj_u4   host_live;
    nj_u4   host_peak;
    nj_size host_bytes;
    /*
     * Execution counters.  These are the numbers behind the performance table
     * in docs/PERFORMANCE.md; they are public so an embedder can measure its
     * own target with the same definitions rather than an approximation of
     * them.
     */
    nj_u8   insns;          /* bytecodes dispatched */
    nj_u8   native_calls;   /* natives invoked from Java */
    nj_u8   frames;         /* frames presented to the port */
} nj_vm_stats;

void nj_vm_get_stats(nj_vm *vm, nj_vm_stats *out);
void nj_vm_dump_stats(nj_vm *vm);

/* A human-readable description of the pending exception, or NULL. */
const char *nj_exception_describe(nj_vm *vm);

/* The dotted class name of an object ("java.lang.String"), or NULL.
 * The returned string belongs to the VM's class table and stays valid until
 * the VM is destroyed. */
const char *nj_ref_class_name(nj_ref o);

/* The value passed to System.exit()/MIDlet.notifyDestroyed(), or 0. */
nj_s4 nj_vm_exit_code(nj_vm *vm);

/* Is the VM still running?  0 once the MIDlet has exited or asked to stop.
 * The runner needs this to drive the frame loop itself (see --run-frames). */
int   nj_vm_running(nj_vm *vm);

/* The size of one VM instance.  An embedder sizing a static arena needs it:
 * the arena the port hands out has to hold this AND the Java heap. */
nj_size nj_vm_sizeof(void);

/* Stop the frame loop (the same thing System.exit does). */
void nj_vm_request_stop(nj_vm *vm, nj_s4 code);

/* Run queued background threads for at most `budget_ms` of virtual time. */
nj_status nj_run_pending(nj_vm *vm, nj_u4 budget_ms);

/*
 * Force a garbage collection and return the bytes still in use afterwards.
 * The runtime collects on demand; this exists for embedders that want to
 * collect at a quiet moment (between games, or before deep-sleeping) and for
 * tests that need the heap to settle before measuring it.
 */
nj_size nj_gc_collect(nj_vm *vm);

/* ------------------------------------------------------------------ */
/* Native registration table                                           */
/* ------------------------------------------------------------------ */
typedef struct nj_native_entry {
    const char  *cls;    /* "java/lang/String" */
    const char  *name;   /* "<init>" */
    const char  *desc;   /* "(Ljava/lang/String;)V" */
    nj_native_fn fn;
} nj_native_entry;

/* Manifest of every built-in (C-implemented) class.  Each class-library
 * module contributes one nj_builtin_class entry per class it implements. */
typedef struct nj_builtin_field {
    const char *name;
    const char *desc;
    nj_u2       flags;
} nj_builtin_field;

typedef struct nj_builtin_method {
    const char *name;
    const char *desc;
    nj_u2       flags;
} nj_builtin_method;

typedef struct nj_builtin_class {
    const char           *name;
    const char           *super;
    nj_u2                 flags;
    const nj_builtin_field  *fields;
    nj_u2                 nfields;
    const nj_builtin_method *methods;
    nj_u2                 nmethods;
} nj_builtin_class;

/*
 * Declared by each class-library translation unit.
 *
 * A module contributes two things:
 *   nj_lib_<name>_table[]  - the native implementations
 *   nj_lib_<name>_classes[] - the classes/members those implementations serve
 *
 * Both are `const`, so they live in .rodata and cost no RAM.
 */
extern const nj_native_entry nj_lib_object_table[];
extern const int nj_lib_object_count;
extern const nj_builtin_class nj_lib_object_classes[];
extern const int nj_lib_object_class_count;

extern const nj_native_entry nj_lib_string_table[];
extern const int nj_lib_string_count;
extern const nj_builtin_class nj_lib_string_classes[];
extern const int nj_lib_string_class_count;

extern const nj_native_entry nj_lib_system_table[];
extern const int nj_lib_system_count;
extern const nj_builtin_class nj_lib_system_classes[];
extern const int nj_lib_system_class_count;

extern const nj_native_entry nj_lib_math_table[];
extern const int nj_lib_math_count;
extern const nj_builtin_class nj_lib_math_classes[];
extern const int nj_lib_math_class_count;

extern const nj_native_entry nj_lib_thread_table[];
extern const int nj_lib_thread_count;
extern const nj_builtin_class nj_lib_thread_classes[];
extern const int nj_lib_thread_class_count;

extern const nj_native_entry nj_lib_boxing_table[];
extern const int nj_lib_boxing_count;
extern const nj_builtin_class nj_lib_boxing_classes[];
extern const int nj_lib_boxing_class_count;

extern const nj_native_entry nj_lib_util_table[];
extern const int nj_lib_util_count;
extern const nj_builtin_class nj_lib_util_classes[];
extern const int nj_lib_util_class_count;

extern const nj_native_entry nj_lib_io_table[];
extern const int nj_lib_io_count;
extern const nj_builtin_class nj_lib_io_classes[];
extern const int nj_lib_io_class_count;

extern const nj_native_entry nj_lib_lcdui_table[];
extern const int nj_lib_lcdui_count;
extern const nj_builtin_class nj_lib_lcdui_classes[];
extern const int nj_lib_lcdui_class_count;

extern const nj_native_entry nj_lib_game_table[];
extern const int nj_lib_game_count;
extern const nj_builtin_class nj_lib_game_classes[];
extern const int nj_lib_game_class_count;

extern const nj_native_entry nj_lib_rms_table[];
extern const int nj_lib_rms_count;
extern const nj_builtin_class nj_lib_rms_classes[];
extern const int nj_lib_rms_class_count;

extern const nj_native_entry nj_lib_media_table[];
extern const int nj_lib_media_count;
extern const nj_builtin_class nj_lib_media_classes[];
extern const int nj_lib_media_class_count;

extern const nj_builtin_class nj_lib_content_classes[];
extern const int nj_lib_content_class_count;
extern const nj_native_entry nj_lib_content_table[];
extern const int nj_lib_content_count;

extern const nj_builtin_class nj_lib_fc_classes[];
extern const int nj_lib_fc_class_count;
extern const nj_native_entry nj_lib_fc_table[];
extern const int nj_lib_fc_count;

/* Built by nj_lib_fc.c, used by Connector.open() in nj_lib_io.c.  Returns a new
 * FileConnection, or NULL with an exception pending. */
nj_ref nj_fc_new(nj_vm *vm, const char *url);

extern const nj_builtin_class nj_lib_nokia_classes[];
extern const int nj_lib_nokia_class_count;
extern const nj_native_entry nj_lib_nokia_table[];
extern const int nj_lib_nokia_count;

extern const nj_native_entry nj_lib_midlet_table[];
extern const int nj_lib_midlet_count;
extern const nj_builtin_class nj_lib_midlet_classes[];
extern const int nj_lib_midlet_class_count;

/*
 * Late initialisation of static state (System.out, Math constants, the
 * Display singleton).  Called once by nj_vm_create() after every builtin
 * class has been registered and linked.  Implemented by the class library.
 */
nj_status nj_lib_late_init(nj_vm *vm);

/*
 * The LCDUI half of nj_lib_late_init(): the MIDP constants that a builtin
 * class cannot carry as ConstantValue attributes (Canvas.KEY_*, Graphics.LEFT,
 * Command.OK, ...), the Font/AlertType singletons and the font registry.
 * nj_lib_late_init() calls it; it is exported so a build that trims
 * java.lang.System can still bring the UI up.
 */
nj_status nj_lcdui_late_init(nj_vm *vm);
/* The media package's constants (Player states, Manager's TONE_*), built after
 * the classes exist for the same reason the LCDUI ones are. */
nj_status nj_media_late_init(nj_vm *vm);

#ifdef __cplusplus
}
#endif

#endif /* NJ_VM_H */
