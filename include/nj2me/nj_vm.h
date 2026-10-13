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

/*
 * Draw MIDP text through a TrueType face instead of the built-in bitmap one.
 *
 * `path` is read through the port's own file hooks, so a platform that keeps
 * its font in flash uses the same call as one that reads it from a disk; there
 * is no stdio in the path.  The bytes and the faces built from them are held
 * in the VM's host registry, so they are released by nj_vm_destroy() with
 * everything else and there is nothing to unload.
 *
 * Load it before the MIDlet starts.  MIDP layout is measured, not re-flowed:
 * the first screen a MIDlet builds is laid out with whatever face was
 * installed at the time, so swapping the font afterwards moves text but not
 * the layout decision that placed it.
 *
 * The built-in faces are kept as the fallback, so a character the supplied
 * font has no glyph for still renders rather than coming out blank.
 *
 * Returns 1 on success.  Returns 0, leaving the VM on its bitmap fonts, if the
 * file cannot be read, the font does not parse, or the build has
 * NJ_ENABLE_TTF=0 - in which case a caller that needs a font file must be told
 * rather than left to discover it by looking at the screen.
 */
int     nj_vm_use_font_file(nj_vm *vm, const char *path);

/*
 * Install the port's own glyph source, for a platform whose text stack is
 * reachable only through an API rather than a font file.
 *
 * `fonts` is indexed by size_index(size) * NJ_FONT_SLOT_STYLES + bold - small,
 * medium, large, each plain then bold - and each face is borrowed, not copied,
 * so it must outlive the VM.  A NULL entry leaves that slot alone.  The faces
 * are consulted for characters the built-in bitmap faces cannot draw, and the
 * renderer blends them when they set `coverage`.
 *
 * Install before the MIDlet starts, and before nj_vm_use_font_file(): a
 * TrueType face rebuilds the slots and gives each one its own fallback chain.
 *
 * Returns 0, or -1 if vm or fonts is NULL or count is not positive.
 */
int     nj_vm_set_fallback_fonts(nj_vm *vm, const nj_font *const *fonts,
                                 int count);

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

/*
 * Ask the call in progress to give up after ms milliseconds (0 clears it).
 * The clock is the port's, so this is the caller's wall clock.  Used around
 * destroyApp(): teardown has to finish even when the MIDlet's own destroyApp()
 * does not.  Clears it and returns non-zero if the deadline being replaced had
 * already elapsed, which is how a caller learns the call had to be cut short.
 */
int     nj_vm_set_halt_after_ms(nj_vm *vm, nj_u4 ms);

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
/*
 * elem_cls_or_prim is the ELEMENT TYPE as the class file spells it: the
 * descriptor letter for a primitive ("I", "F", "S", "B", "C", "J", "D",
 * "Z") or the internal class name ("java/lang/String").  The Java spelling
 * ("int") is not one of them, and passing it builds an array of a class
 * that does not exist - it resolves, the length is right, and every element
 * reads back as zero, which is how RecordStore.getRecordIDs() and
 * ToneControl.getToneSequence() were silently broken.
 */
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
nj_s4    nj_field_read_int (nj_vm *vm, nj_ref obj, const nj_field *f);
void     nj_field_write_int(nj_vm *vm, nj_ref obj, const nj_field *f, nj_s4 v);
nj_s8    nj_field_read_long (nj_vm *vm, nj_ref obj, const nj_field *f);
void     nj_field_write_long(nj_vm *vm, nj_ref obj, const nj_field *f, nj_s8 v);
nj_ref   nj_field_read_ref (nj_vm *vm, nj_ref obj, const nj_field *f);
void     nj_field_write_ref(nj_vm *vm, nj_ref obj, const nj_field *f, nj_ref v);

/*
 * Raw access, for the interpreter.
 *
 * The typed accessors above convert: reading a `float` field holding 2.5f
 * through nj_field_read_int() gives 2, and writing 0x40200000 through
 * nj_field_write_int() stores (float)1075838976.  The interpreter's operand
 * stack holds a float's or a double's *bit pattern*, so its getstatic /
 * getfield / putstatic / putfield have to move the bytes and nothing else.
 */
nj_u4    nj_field_read_bits32(nj_vm *vm, nj_ref obj, const nj_field *f);
void     nj_field_write_bits32(nj_vm *vm, nj_ref obj, const nj_field *f, nj_u4 v);
nj_u8    nj_field_read_bits64(nj_vm *vm, nj_ref obj, const nj_field *f);
void     nj_field_write_bits64(nj_vm *vm, nj_ref obj, const nj_field *f, nj_u8 v);

/* One predicate, so a caller can tell a float field from an int one without
 * seeing the struct: nj_field is opaque here on purpose. */
int      nj_field_is_float(const nj_field *f);

/* Look a field up once and cache the nj_field* in a static const-free slot of
 * your own objects; these are the hot-path accessors. */
#define NJ_F(vm, cls, name, desc)  nj_field_lookup((vm), (cls), (name), (desc), 0)
#define NJ_FS(vm, cls, name, desc) nj_field_lookup((vm), (cls), (name), (desc), 1)

/* Short forms used throughout the class library.
 *
 * On a `float` field these two move the bit pattern, not a number: every
 * caller that has one writes f2bits(v) into it and reads it back through
 * bits2f(), and the typed accessors would convert on the way in and out -
 * storing (float)0x40200000 instead of 2.5f, and losing the low bits of any
 * pattern that 24 bits of mantissa cannot hold. */
static inline nj_s4  nj_iget(nj_vm *vm, nj_ref o, const nj_field *f)
{
    if (f != NULL && nj_field_is_float(f)) {
        return (nj_s4)nj_field_read_bits32(vm, o, f);
    }
    return nj_field_read_int(vm, o, f);
}

static inline void nj_iput(nj_vm *vm, nj_ref o, const nj_field *f, nj_s4 v)
{
    if (f != NULL && nj_field_is_float(f)) {
        nj_field_write_bits32(vm, o, f, (nj_u4)v);
        return;
    }
    nj_field_write_int(vm, o, f, v);
}
static inline nj_ref nj_rget(nj_vm *vm, nj_ref o, const nj_field *f) { return nj_field_read_ref(vm, o, f); }
static inline void   nj_rput(nj_vm *vm, nj_ref o, const nj_field *f, nj_ref v) { nj_field_write_ref(vm, o, f, v); }

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
    nj_u8   paints;         /* paint() calls.  A game that presents frames
                             * without painting anything looks exactly like
                             * a game that draws - until you count. */
    nj_u8   tris;           /* triangles the 3D rasterizer filled */
    nj_u8   m3g_renders;    /* JSR 184 render() calls that were reached */
    nj_u8   gfx_calls;      /* javax.microedition.lcdui.Graphics method
                             * calls, counted where the Graphics is loaded
                             * from the Java object.  paints says the game
                             * ran paint(); this says paint() touched the
                             * drawing surface at all. */
} nj_vm_stats;

void nj_vm_get_stats(nj_vm *vm, nj_vm_stats *out);

/*
 * Diagnostic: describe where every live thread currently is.
 *
 * One line per thread (id, state, depth) followed by the innermost frames of
 * its stack, passed to `sink` as they are built.
 *
 * This is not decoration.  "The MIDlet presented no frames" is how most
 * titles fail and it is the least informative symptom there is: a game
 * spinning in its own update loop, one blocked on a monitor, one parked
 * inside a native and one that never returned from its constructor are
 * indistinguishable from the outside, and they need completely different
 * fixes.  The top frames tell them apart.
 */
void nj_vm_describe_threads(nj_vm *vm,
                            void (*sink)(void *ud, const char *line),
                            void *ud);
void nj_vm_dump_stats(nj_vm *vm);

/* A human-readable description of the pending exception, or NULL. */
const char *nj_exception_describe(nj_vm *vm);

/* The dotted class name of an object ("java.lang.String"), or NULL.
 * The returned string belongs to the VM's class table and stays valid until
 * the VM is destroyed. */
const char *nj_ref_class_name(nj_ref o);

/* The value passed to System.exit()/MIDlet.notifyDestroyed(), or 0. */
nj_s4 nj_vm_exit_code(nj_vm *vm);

/*
 * Who asked the VM to stop, or NULL when it stopped by itself (the frame budget
 * ran out, for instance).  The result points at a string literal, so it stays
 * valid after the VM is destroyed and never needs freeing.
 */
const char *nj_vm_stop_why(const nj_vm *vm);

/*
 * Stop the VM and say who asked.  The reason is a string literal - nothing
 * allocates, so nothing can leak - and it is what lets a run that ended on
 * purpose be told apart from one that failed.
 */
void nj_vm_stop_reason(nj_vm *vm, nj_s4 exit_code, const char *why);

/*
 * Register a callback the VM runs while a Java thread is sleeping or waiting.
 *
 * It exists because a MIDlet is allowed to run its own frame loop inside
 * startApp(), and a sleep inside that loop must not stop the display.  The
 * callback is called from inside the VM, so it must not assume it is at the
 * top of the stack; the VM guards against it re-entering itself.  Pass NULL to
 * unregister.
 */
int  nj_vm_set_frame_hook(nj_vm *vm, int (*fn)(nj_vm *vm, void *ud), void *ud);

/*
 * The port this VM was created with, or NULL for a bare VM.
 *
 * It exists for the callbacks that receive no port of their own - a codec
 * provider's decode(), a font provider's glyph_lookup() - and still have to
 * reach the platform: the port's `ud` is where a port keeps its state, and
 * nj_codec_req carries the VM for exactly this reason.
 */
const nj_port *nj_vm_port(const nj_vm *vm);

/* Is the VM still running?  0 once the MIDlet has exited or asked to stop.
 * The runner needs this to drive the frame loop itself (see --run-frames). */
int   nj_vm_running(nj_vm *vm);

/*
 * Frames presented so far.
 *
 * The runner checks this once per frame to enforce --run-frames, and asking
 * for the whole nj_vm_stats snapshot there is not the same thing: the
 * snapshot computes the bytes in use, which walks every block in the Java
 * heap (nj_heap_used_bytes).  That is 10% of a graphics title's frame time
 * for a number the loop does not look at.
 */
nj_u8 nj_vm_frames(nj_vm *vm);

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

/* Built by nj_lib_http.c (javax.microedition.io.HttpConnection).  The
 * implementation class is HttpImpl; the interfaces above it carry no state. */
extern const nj_builtin_class nj_lib_http_classes[];
extern const int nj_lib_http_class_count;
extern const nj_native_entry nj_lib_http_table[];
extern const int nj_lib_http_count;

/* Built by nj_lib_socket.c (javax.microedition.io.SocketConnection).  Four
 * classes: the interface, the implementation, and the two streams a MIDlet
 * reads and writes. */
extern const nj_builtin_class nj_lib_socket_classes[];
extern const int nj_lib_socket_class_count;
extern const nj_native_entry nj_lib_socket_table[];
extern const int nj_lib_socket_count;

/* Built by nj_lib_sensor.c (JSR 256, javax.microedition.sensor).  The five
 * implementation classes are the objects the interfaces describe; the sensors
 * themselves come from the port's optional sensor_* hooks. */
extern const nj_builtin_class nj_lib_sensor_classes[];
extern const int nj_lib_sensor_class_count;
extern const nj_native_entry nj_lib_sensor_table[];
extern const int nj_lib_sensor_count;

/* Built by nj_lib_bt.c (JSR 82, javax.bluetooth).  The radio is the port's,
 * through the optional bt_* hooks; the classes and the discovery job pump are
 * the runtime's. */
extern const nj_builtin_class nj_lib_bt_classes[];
extern const int nj_lib_bt_class_count;
extern const nj_native_entry nj_lib_bt_table[];
extern const int nj_lib_bt_count;

/* JSR 120/205, Wireless Messaging.  Messages go out through optional port
 * hooks; a port without them has the API and gets the exception the spec
 * defines, rather than a link error. */
extern const nj_builtin_class nj_lib_wireless_classes[];
extern const int nj_lib_wireless_class_count;
extern const nj_native_entry nj_lib_wireless_table[];
extern const int nj_lib_wireless_count;

/* JSR 184 (Mobile 3D Graphics): the classes, the scene graph and the
 * transform maths.  Compiled out with -DNJ_ENABLE_M3G=0, in which case both
 * tables are empty and the classes are simply absent. */
extern const nj_builtin_class nj_lib_m3g_classes[];
extern const int nj_lib_m3g_class_count;
extern const nj_native_entry nj_lib_m3g_table[];
extern const int nj_lib_m3g_count;

/* Built by nj_lib_mascot.c (Mascot Capsule Micro3D v3).  Compiled out with
 * -DNJ_ENABLE_MASCOT=0, in which case both tables are empty. */
extern const nj_builtin_class nj_lib_mascot_classes[];
extern const int nj_lib_mascot_class_count;
extern const nj_native_entry nj_lib_mascot_table[];
extern const int nj_lib_mascot_count;

/* Built by nj_lib_fc.c, used by Connector.open() in nj_lib_io.c.  Returns a new
 * FileConnection, or NULL with an exception pending. */
nj_ref nj_fc_new(nj_vm *vm, const char *url);

/* JSR 120 (messaging) and JSR 82 (Bluetooth): the classes exist so that a
 * MIDlet's Class.forName / catch fallback works, and every operation that
 * would need a radio throws BluetoothStateException instead. */
extern const nj_builtin_class nj_lib_wireless_classes[];
extern const int nj_lib_wireless_class_count;
extern const nj_native_entry nj_lib_wireless_table[];
extern const int nj_lib_wireless_count;

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
nj_status nj_nokia_late_init(nj_vm *vm);
nj_status nj_io_late_init(nj_vm *vm);

#ifdef __cplusplus
}
#endif


/*
 * The two layers of a .m3g container, both usable without a MIDlet:
 * the container probe checks that the sections tile the file, and the object
 * scan walks the objects inside them (inflating compressed sections through
 * the port's inflate hook).
 */
typedef struct nj_m3g_fields {
    nj_u4 user_id;
    nj_u4 a, b, c;   /* the three fields of whichever type this is */
    /*
     * A Group's children, as indices into the file's object list.
     *
     * JSR 184 section 3.2: an ObjectIndex[] is a UInt32 count followed by that
     * many UInt32 indices, and index 0 means null.  The indices are what the
     * scene graph is actually made of - without them a Group is an empty box
     * and Loader.load() hands the game a null where a child should be.
     */
    nj_u4 ref_off;   /* where the indices start, relative to the object data */
    nj_u4 nrefs;     /* how many there are */
    /*
     * The indices, copied out of the file.
     *
     * The loader needs the numbers, not just where they were: by the time it
     * builds the scene graph the section's bytes have been released.  A Group
     * with more children than this keeps the first ones - truncation is
     * visible in nrefs_total, and a scene that is missing some of its parts
     * still draws, which is the same choice made everywhere else here.
     */
#define NJ_M3G_MAX_REFS 16u
    nj_u4 refs[NJ_M3G_MAX_REFS];
    /*
     * The children past the inline ones, copied out of the section buffer and
     * released once the scene graph has been built.
     *
     * A Group or a World can name many more children than fit inline - one
     * World in 毁灭战士3_10598 names 155 - and dropping the rest is not a small
     * loss: the title looks its model up by userID, does not find it among the
     * first sixteen, and calls addChild(null), which is that title failing to
     * start.  NULL when the list fits inline.
     */
    nj_u4 *extra;
    nj_u4 nrefs_total;
    const nj_u1 *data;   /* a VertexArray's bytes, inside the section buffer */
    nj_u4 data_len;      /* how many of them there are */
    nj_u1 decoded;
} nj_m3g_fields;

typedef struct nj_m3g_object {
    nj_u1 type;
    nj_u4 off;     /* where the object sits in the file, for messages */
    /*
     * The object's fields, decoded while the bytes were still reachable.
     *
     * The scanner must not hand out a pointer into its scratch buffer: for a
     * compressed section that buffer is freed and reallocated as bigger
     * sections turn up, so pointers recorded for earlier sections dangle - and
     * a dangling pointer here reads plausible nonsense, not a crash.  Decoding
     * in place is what makes the two impossible to confuse.
     */
    nj_m3g_fields fields;
    int decoded;
    nj_u4 len;     /* the object's own Length field */
    /*
     * The object's userID, the first thing every object's data holds.
     *
     * Measured, not assumed: across the 975 real .m3g files collected under
     * tests/games the first four bytes of an object's data are a small
     * integer - 0, 1, 2, 4, 5, 6, 13, 44, 45, 124, 401 - and the eight bytes
     * after it are zero in every one of the 36859 objects.  The one exception
     * is an ExternalReference, whose data is a bare string ("citroen.png")
     * with no header at all, so it has no userID and this stays zero.
     */
    nj_u4 user_id;
} nj_m3g_object;

nj_status nj_m3g_container_probe(const nj_u1 *p, nj_u4 n,
                                 nj_u1 *ids, nj_u4 id_count,
                                 char *err, nj_u4 errcap);

/*
 * The fields of one object, decoded from its data.  `decoded` says whether the
 * type's layout is known: for an unknown type the userID is still filled in and
 * the rest is zero, so a caller can tell "not decoded yet" from "decoded to 0".
 */


nj_status nj_m3g_decode_object(const nj_u1 *data, nj_u4 len, nj_u1 type,
                               nj_m3g_fields *out, char *err, nj_u4 errcap);

nj_status nj_m3g_scan_objects(nj_vm *vm, const nj_u1 *p, nj_u4 n,
                              nj_m3g_object *out, nj_u4 out_cap,
                              nj_u4 *count, nj_u4 *sections,
                              char *err, nj_u4 errcap);

/* Give every Group in an object table the children the file names.  Split out
   of Loader.load() so a test can reach it without a real MIDlet suite. */
/* Project one vertex through a 4x4 matrix into pixel coordinates.  The
   convention is v' = M v (a column vector), the same one Transform uses.
   y is flipped, because the screen's origin is top-left and the clip space
   origin is bottom-left. */
void nj_m3g_project(const float m[16], const float v[3], int w, int h,
                    float out[2]);

/* Fill one screen-space triangle and count it.  The rasteriser is built on
   the 2D triangle the graphics layer already has, so what a title sees here
   goes through the same fill the rest of MIDP does. */
struct nj_gfx;   /* nj_gfx.h includes this header, so name it only */
nj_u4 nj_m3g_raster_tri(struct nj_gfx *g, const float xy[6], nj_u4 argb);

/* Move the geometry a file carries onto the Java objects that describe it.
   A VertexArray's bytes are in the file; the class the titles call expects
   them in a short[], which is what set() puts there too. */
/* Walk a scene graph from `root` and collect the Meshes under it, in the
   order the graph gives them.  Returns how many there are, which may be more
   than `cap`; only the first `cap` are written. */
nj_u4 nj_m3g_collect_meshes(nj_vm *vm, nj_ref root, nj_ref *out, nj_u4 cap);

/* Draw a Mesh: read its VertexBuffer's positions, project them through the
   matrix, and fill the triangles they make.  Returns how many were filled. */
nj_u4 nj_m3g_raster_mesh(nj_vm *vm, struct nj_gfx *g, nj_ref mesh,
                         const float m[16], int w, int h, nj_u4 argb);

/* The k'th reference of an object whose list may run past the inline ones. */
nj_u4 nj_m3g_ref_at(const nj_m3g_fields *f, nj_u4 k);

nj_status nj_m3g_load_geometry(nj_vm *vm, const nj_m3g_object *objs, nj_u4 nobj,
                               nj_ref arr);

nj_status nj_m3g_link_groups(nj_vm *vm, const nj_m3g_object *objs, nj_u4 nobj,
                             nj_ref arr);

#endif /* NJ_VM_H */
