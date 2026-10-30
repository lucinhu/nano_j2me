/*
 * nano_j2me - nj_vm.c
 *
 * VM lifecycle, Java object/field access, exception raising and the
 * C-to-Java call bridge.
 *
 * OWNERSHIP RULE: every byte this file allocates hangs off `vm` (either in the
 * Java heap or in the host-allocation registry), so nj_vm_destroy() is a
 * complete teardown.  There is no file-scope mutable state anywhere here.
 */
#include "nj_internal.h"
#include "nj2me/nj_jit.h"

#if NJ_USE_LIBC
#  include <stdio.h>
#  include <stdarg.h>
#  include <string.h>
#  include <stdlib.h>
#endif

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */
static const char *level_name(int level)
{
    switch (level) {
    case 0:  return "error";
    case 1:  return "warn";
    case 3:  return "debug";
    default: return "info";
    }
}

void nj_logf(nj_vm *vm, int level, const char *fmt, ...)
{
#if NJ_USE_LIBC
    if (vm && (nj_u4)level > (nj_u4)vm->verbose) {
        return;
    }
    char buf[320];
    va_list ap;

    /*
     * A NULL format is a caller's mistake, and vsnprintf does not survive it.
     * GCC says so at -O3 under NJ_SANITIZE ("null format string"), which used
     * to stop the sanitizer build - the one build whose whole job is to catch
     * this kind of thing - before it started.  Return instead of calling.
     */
    if (fmt == NULL) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /*
     * Throttle before the sink, whoever owns it.  See nj_vm.log_emitted: a
     * MIDlet that prints a stack trace per frame produced 24 MB in ten seconds
     * and looked like a hang from outside.
     */
    if (vm) {
        if (vm->log_emitted >= NJ_LOG_LIMIT) {
            if (vm->log_dropped++ == 0u) {
                /* Say it once, so the silence afterwards is explained. */
                if (vm->port && vm->port->log) {
                    vm->port->log(vm->port->user, 0,
                                  "log limit reached; further messages from "
                                  "this run are suppressed");
                } else {
                    fprintf(stderr, "[nj2me] log limit reached; further "
                                    "messages from this run are suppressed\n");
                }
            }
            return;
        }
        vm->log_emitted++;
    }
    if (vm && vm->port && vm->port->log) {
        vm->port->log(vm->port->user, level, buf);
        return;
    }
    fprintf(stderr, "[nj2me %s] %s\n", level_name(level), buf);
#else
    (void)vm; (void)level; (void)fmt;
#endif
}

void nj_fatal(nj_vm *vm, const char *fmt, ...)
{
#if NJ_USE_LIBC
    char buf[320];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (vm && vm->port && vm->port->log) {
        vm->port->log(vm->port->user, 0, buf);
    } else {
        fprintf(stderr, "[nj2me fatal] %s\n", buf);
    }
#else
    (void)vm; (void)fmt;
#endif
    if (vm) {
        vm->state = NJ_VM_DEAD;
        vm->running = 0u;
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */
/*
 * Every failure path in vm_setup() names itself.
 *
 * nj_vm_create() returning NULL with no explanation is close to undebuggable on
 * a target with no debugger - which is exactly where it gets called.  The
 * message goes through the port's log, so it costs nothing when there is none.
 */
#define NJ_SETUP_FAIL(what)                                            \
    do {                                                               \
        nj_logf(vm, 0, "nj_vm_create: " what " failed");               \
    } while (0)

static nj_status vm_setup(nj_vm *vm, const nj_port *port, nj_size heap_bytes)
{
    nj_size hs;

    vm->port = port;
    vm->state = NJ_VM_READY;
    vm->hash_seed = 0x9E3779B9u;

    hs = heap_bytes ? heap_bytes : (nj_size)NJ_MAX_HEAP;
    if (nj_heap_init(vm, NULL, hs) != NJ_OK) {
        NJ_SETUP_FAIL("heap");
        return NJ_ERR_NOMEM;
    }
    vm->statics = (nj_value *)nj_host_alloc(
        vm, (nj_size)NJ_MAX_STATICS * sizeof(nj_value));
    if (!vm->statics) {
        NJ_SETUP_FAIL("statics");
        return NJ_ERR_NOMEM;
    }
    vm->nstatics = 0u;

    if (nj_cp_cache_init(vm, 512u) != NJ_OK) {
        NJ_SETUP_FAIL("cp cache");
        return NJ_ERR_NOMEM;
    }
    if (nj_natives_init(vm) != NJ_OK) {
        NJ_SETUP_FAIL("native registry");
        return NJ_ERR_NOMEM;
    }

    /*
     * Let the port install its own image codecs FIRST, then add the built-in
     * software ones.  A platform decoder therefore wins on priority while the
     * software path stays available as a fallback for formats the platform
     * does not handle - a board with a hardware JPEG block still gets PNG
     * from the built-in decoder.
     */
    if (port && port->install_codecs) {
        port->install_codecs(port->ud, vm);
    }
    nj_codecs_register_builtins(vm);

    /* Materialise the C-implemented class library, then let it install its
     * static state (System.out, Math constants, ...). */
    if (nj_builtins_register_all(vm) != NJ_OK) {
        /* A partially registered class table is not a VM.  Failing here means
         * nj_vm_create() returns NULL with a reason, instead of the crash that
         * a missing builtin causes the first time something looks it up. */
        NJ_SETUP_FAIL("builtin class registration");
        return NJ_ERR_NOMEM;
    }
    if (nj_lib_late_init(vm) != NJ_OK) {
        NJ_SETUP_FAIL("class library late init");
        return NJ_ERR;
    }

    /* The primordial thread always exists so that C code can call into Java
     * even before any MIDlet starts. */
    vm->cur = nj_thread_create(vm, NULL, 1);
    if (!vm->cur) {
        NJ_SETUP_FAIL("main thread");
        return NJ_ERR_NOMEM;
    }
    vm->running = 1u;
    vm->verbose = 2u;      /* error + warn + info; debug needs opting in */
    /* The runner paints the first frames; an application paint resets the idle
     * counter and teaches the hook its cadence (see nj_lib_midlet.c). */
    vm->app_paint_period = 1u;
#if NJ_ENABLE_DEBUG
    vm->trace = (nj_u1)nj_trace_enabled(vm);
    vm->trace_filter = getenv("NJ_TRACE");
    if (vm->trace_filter && !*vm->trace_filter) {
        vm->trace = 0u;
    }
#endif
    return NJ_OK;
}

nj_vm *nj_vm_create(const nj_port *port, nj_size heap_bytes)
{
    nj_vm *vm = NULL;

    /* Bootstrap: the VM struct itself must be allocated before the registry
     * it lives in exists, so use the port allocator (or malloc) directly. */
    if (port && port->alloc) {
        vm = (nj_vm *)port->alloc(port->ud, sizeof(nj_vm));
    } else {
#if NJ_USE_LIBC
        vm = (nj_vm *)malloc(sizeof(nj_vm));
#endif
    }
    if (!vm) {
        return NULL;
    }
    memset(vm, 0, sizeof(*vm));
    /* Real time until the player says otherwise. */
    vm->speed_percent = 100;
    vm->frame_cap = -1;

    if (vm_setup(vm, port, heap_bytes) != NJ_OK) {
        nj_vm_destroy(vm);
        return NULL;
    }
    nj_clock_init(vm);
    nj_avcodec_reset(vm);
    vm->initialized = 1u;
#if NJ_ENABLE_JIT
    /* A missing backend or executable allocator is an interpreter fallback. */
    (void)nj_jit_enable(vm);
#endif
    return vm;
}

nj_u4 nj_vm_destroy(nj_vm *vm)
{
    const nj_port *port;
    nj_u2 i;
    nj_u4 released;

    if (!vm) {
        return 0u;
    }
    port = vm->port;
    vm->running = 0u;
    vm->state = NJ_VM_DEAD;
#if NJ_ENABLE_JIT
    nj_jit_disable(vm);
#endif

    for (i = 0u; i < vm->nthreads; i++) {
        nj_thread_destroy(vm, vm->threads[i]);
        vm->threads[i] = NULL;
    }
    vm->nthreads = 0u;
    vm->cur = NULL;

    nj_natives_shutdown(vm);

    if (vm->builtins) {
        nj_host_free(vm, vm->builtins);
        vm->builtins = NULL;
    }
    if (vm->codecs) {
        nj_u2 k;
        for (k = 0u; k < vm->ncodecs; k++) {
            if (vm->codecs[k].codec.detach) {
                vm->codecs[k].codec.detach(vm, &vm->codecs[k].codec);
            }
        }
        nj_host_free(vm, vm->codecs);
        vm->codecs = NULL;
        vm->ncodecs = 0u;
    }
    if (vm->cp_cache) {
        nj_host_free(vm, vm->cp_cache);
        vm->cp_cache = NULL;
    }
    if (vm->statics) {
        nj_host_free(vm, vm->statics);
        vm->statics = NULL;
    }
    if (vm->scratch_u2) {
        nj_host_free(vm, vm->scratch_u2);
        vm->scratch_u2 = NULL;
    }
    if (vm->interned) {
        nj_host_free(vm, vm->interned);
        vm->interned = NULL;
        vm->intern_cap = 0u;
    }

    /*
     * Releases the Java arena plus every remaining host allocation, which is
     * where the class bytes, member arrays, name strings and thread stacks
     * live.  nj_heap_destroy() sweeps the allocation registry wholesale, so
     * teardown is complete even if some error path above skipped a free.
     */
    released = vm->host_count;
    nj_heap_destroy(vm);

    if (port && port->free) {
        port->free(port->ud, vm);
    } else {
#if NJ_USE_LIBC
        free(vm);
#endif
    }
    return released;
}

nj_size nj_vm_sizeof(void)
{
    return (nj_size)sizeof(nj_vm);
}

int nj_vm_running(nj_vm *vm)
{
    if (!vm) {
        return 0;
    }
    return (vm->running && vm->state != NJ_VM_STOPPING) ? 1 : 0;
}

nj_u8 nj_vm_frames(nj_vm *vm)
{
    return vm ? vm->frames : 0u;
}

const nj_port *nj_vm_port(const nj_vm *vm)
{
    return vm ? vm->port : NULL;
}

/*
 * The state names, so a log line says "waiting" instead of "3".  A number
 * would be worse than useless here: the whole point is to be read by a human
 * who is trying to find out why nothing was painted.
 */
static const char *thread_state_name(nj_u1 st)
{
    switch (st) {
    case NJ_THREAD_NEW:     return "new";
    case NJ_THREAD_RUNNING: return "running";
    case NJ_THREAD_SLEEPING:return "sleeping";
    case NJ_THREAD_WAITING: return "waiting";
    case NJ_THREAD_BLOCKED: return "blocked";
    case NJ_THREAD_DEAD:    return "dead";
    default:                return "?";
    }
}

#define NJ_WHERE_FRAMES 8   /* innermost frames shown per thread */

void nj_vm_describe_threads(nj_vm *vm,
                            void (*sink)(void *ud, const char *line),
                            void *ud)
{
    nj_u2 i;

    if (!vm || !sink) {
        return;
    }
    for (i = 0u; i < vm->nthreads; i++) {
        nj_thread *t = vm->threads[i];
        char buf[320];
        int d;

        if (!t) {
            continue;
        }
        snprintf(buf, sizeof(buf), "where: thread %lu %s depth=%d%s",
                 (unsigned long)t->id, thread_state_name(t->state),
                 t->nframes, (t == vm->cur) ? " (current)" : "");
        sink(ud, buf);

        for (d = t->nframes - 1; d >= 0 && d > t->nframes - 1 - NJ_WHERE_FRAMES;
             d--) {
            const nj_frame *fr = &t->frames[d];
            const nj_method *m = fr->m;

            if (!m) {
                continue;
            }
            snprintf(buf, sizeof(buf), "where:   %s.%s%s pc=%lu",
                     (m->owner && m->owner->name) ? m->owner->name : "?",
                     m->name ? m->name : "?", m->desc ? m->desc : "",
                     (unsigned long)fr->pc);
            sink(ud, buf);
        }
    }
}

void nj_vm_get_stats(nj_vm *vm, nj_vm_stats *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!vm) {
        return;
    }
    out->heap_size = vm->heap.size;
    out->heap_peak = vm->heap.peak;
    out->heap_used = nj_heap_used_bytes(vm);
    out->gc_count = vm->heap.gc_count;
    out->live_objects = vm->live_objects;
    out->total_objects = vm->total_objects;
    out->classes = vm->nclasses;
    /* Threads created since the VM started.  The live count falls back as
     * finished threads are reaped, so it cannot tell a test whether a thread
     * ever existed; the monotonic counter can. */
    out->threads = vm->next_thread_id;
    out->cp_cache_entries = vm->cp_cache_used;
    out->cp_cache_hits = vm->cp_cache_hits;
    out->cp_cache_misses = vm->cp_cache_misses;
    out->natives = vm->nnatives;
    out->native_misses = vm->native_misses;
    out->codecs = vm->ncodecs;
    out->host_live = vm->host_count;
    out->insns = vm->insns;
    out->native_calls = vm->native_calls;
    out->frames = vm->frames;
    out->paints = vm->paints;
    out->tris = vm->tris;
    out->m3g_renders = vm->m3g_renders;
    out->gfx_calls = vm->gfx_calls;
    out->host_peak = vm->host_peak;
    out->host_bytes = (nj_size)vm->host_bytes;
}

#if NJ_USE_LIBC
static int stats_printf(const nj_port *port, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (port && port->log) {
        port->log(port->user, 2, buf);
        return 0;
    }
    return fputs(buf, stderr);
}
#endif

void nj_vm_dump_stats(nj_vm *vm)
{
    nj_vm_stats s;

    nj_vm_get_stats(vm, &s);
#if NJ_USE_LIBC
    stats_printf(vm ? vm->port : NULL,
                 "heap %lu/%lu bytes, peak %lu, gc %lu, live objs %lu/%lu",
                 (unsigned long)s.heap_used, (unsigned long)s.heap_size,
                 (unsigned long)s.heap_peak, (unsigned long)s.gc_count,
                 (unsigned long)s.live_objects, (unsigned long)s.total_objects);
    stats_printf(vm ? vm->port : NULL,
                 "classes %u, threads %u, cp-cache %u (hits %lu misses %lu), "
                 "natives %u (misses %lu), host allocs %lu (peak %lu)",
                 (unsigned)s.classes, (unsigned)s.threads,
                 (unsigned)s.cp_cache_entries, (unsigned long)s.cp_cache_hits,
                 (unsigned long)s.cp_cache_misses, (unsigned)s.natives,
                 (unsigned long)s.native_misses, (unsigned long)s.host_live,
                 (unsigned long)s.host_peak);
#else
    (void)s;
#endif
}

/* ------------------------------------------------------------------ */
/* Classes                                                             */
/* ------------------------------------------------------------------ */
nj_class *nj_class_lookup(nj_vm *vm, const char *name)
{
    nj_class *c;

    if (!vm || !name) {
        return NULL;
    }
    c = nj_class_resolve(vm, name);
    if (!c) {
        return NULL;
    }
    /* A class that is already linked answers here on every later call, and this
     * function is on the field-lookup path the I/O natives walk millions of
     * times; keep the call to nj_class_link for the first visit only. */
    if (c->state < NJ_CLASS_READY && nj_class_link(vm, c) != NJ_OK) {
        return NULL;
    }
    return c;
}

nj_ref nj_class_object(nj_vm *vm, const char *name)
{
    nj_class *c = nj_class_lookup(vm, name);

    if (!c) {
        return NULL;
    }
    if (!c->class_obj) {
        nj_ref o = nj_new_instance(vm, vm->cls_class);
        if (!o) {
            return NULL;
        }
        o->kind = NJ_OBJ_CLASS;
        o->u.mirror = c;
        c->class_obj = o;
    }
    return c->class_obj;
}

nj_field *nj_field_lookup(nj_vm *vm, const char *cls, const char *name,
                          const char *desc, int is_static)
{
    nj_class *c;

    if (!vm || !cls || !name) {
        return NULL;
    }
    c = nj_class_lookup(vm, cls);
    if (!c) {
        return NULL;
    }
    return nj_field_lookup_in(c, name, desc, is_static);
}

/* ------------------------------------------------------------------ */
/* Class initialisation (<clinit>)                                     */
/* ------------------------------------------------------------------ */
static nj_u1 *field_addr(nj_vm *vm, nj_ref obj, const nj_field *f);

nj_status nj_class_init(nj_vm *vm, nj_class *c)
{
    nj_method *clinit;

    if (!vm || !c) {
        return NJ_ERR;
    }
    if (c->state == NJ_CLASS_ERROR) {
        return nj_throw(vm, "java/lang/NoClassDefFoundError", c->name);
    }
    if (c->state >= NJ_CLASS_READY) {
        return NJ_OK;
    }
    /* A link failure is already a describable exception: nj_class_link()
     * raises one naming what was missing.  The <clinit> below can fail with an
     * exception of its own.  So every NJ_ERR leaving here has one set. */
    if (nj_class_link(vm, c) != NJ_OK) {
        return NJ_ERR;
    }
    /* Interfaces and primitives have no initialiser. */
    clinit = NULL;
    {
        nj_u2 i;
        for (i = 0u; i < c->method_count; i++) {
            if (strcmp(c->methods[i].name, "<clinit>") == 0) {
                clinit = &c->methods[i];
                break;
            }
        }
    }

    /* Mark first so a static initialiser that touches its own class does not
     * recurse forever. */
    c->state = NJ_CLASS_READY;

    /* JVMS 5.5 step 4: a static field carrying a ConstantValue attribute takes
     * that value before <clinit> runs.  javac inlines compile-time constants at
     * their use sites, so a getstatic of one only appears when the reading
     * class was compiled against a non-final field or when the title's own
     * tools kept the reference - 20 such fields across 11 of the collected
     * titles, each of which read 0 or null because nothing applied it. */
    {
        nj_u2 fi;
        for (fi = 0u; fi < c->field_count; fi++) {
            nj_field *f = &c->fields[fi];
            nj_value cst[2];
            nj_ref sr = NULL;
            nj_u1 *p;
            int k;
            if ((f->flags & NJ_ACC_STATIC) == 0u || f->const_cp == 0u) {
                continue;
            }
            k = nj_cp_const(vm, c, f->const_cp, 1, cst, &sr);
            if (k < 0) {
                continue;
            }
            p = field_addr(vm, NULL, f);
            if (!p) {
                continue;
            }
            if (k == 1) {
                memcpy(p, &sr, sizeof sr);
            } else if (f->width == 2u) {
                memcpy(p, &cst[0].j, 8u);
            } else {
                memcpy(p, &cst[0].i, 4u);
            }
        }
    }

    if (clinit && clinit->code) {
        nj_status st = nj_call_static(vm, c->name, "<clinit>", "()V", NULL, NULL);
        if (st != NJ_OK) {
            /*
             * JVMS 5.5: an initialiser that throws anything but an Error is
             * wrapped in ExceptionInInitializerError.  A link failure or a
             * native error keeps the exception it already raised, and the
             * class stays erroneous for every later attempt.
             */
            nj_ref orig = vm->exception;
            if (orig != NULL && orig->cls != NULL) {
                nj_class *ec = nj_class_lookup(vm, "java/lang/Error");
                if (ec != NULL && !nj_class_is_subclass_of(orig->cls, ec)) {
                    vm->exception = NULL;
                    (void)nj_throw(vm, "java/lang/ExceptionInInitializerError",
                                   c->name);
                }
            }
            c->state = NJ_CLASS_ERROR;
            return st;
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Object and array creation                                           */
/* ------------------------------------------------------------------ */
nj_ref nj_object_new(nj_vm *vm, const char *cls)
{
    nj_class *c;
    nj_ref o;

    if (!vm || !cls) {
        return NULL;
    }
    c = nj_class_lookup(vm, cls);
    if (!c) {
        (void)nj_throw(vm, "java/lang/NoClassDefFoundError", cls);
        return NULL;
    }
    if (nj_class_init(vm, c) != NJ_OK) {
        return NULL;
    }
    o = nj_new_instance(vm, c);
    if (!o) {
        (void)nj_throw_oom(vm);
        return NULL;
    }
    return o;
}

static const char *prim_elem_name(int kind)
{
    switch (kind) {
    case NJ_ARRAY_T_BOOLEAN: return "Z";
    case NJ_ARRAY_T_BYTE:    return "B";
    case NJ_ARRAY_T_CHAR:    return "C";
    case NJ_ARRAY_T_SHORT:   return "S";
    case NJ_ARRAY_T_INT:     return "I";
    case NJ_ARRAY_T_LONG:    return "J";
    case NJ_ARRAY_T_FLOAT:   return "F";
    case NJ_ARRAY_T_DOUBLE:  return "D";
    default:                 return NULL;
    }
}

static nj_ref array_new_internal(nj_vm *vm, const char *elem, nj_u4 len)
{
    char buf[300];
    nj_u4 n = 1u;
    nj_class *ac;
    nj_ref a;

    buf[0] = '[';
    while (elem && *elem && n + 1u < (nj_u4)sizeof(buf)) {
        buf[n++] = *elem++;
    }
    buf[n] = '\0';

    if (len > 0x00FFFFFFu) {
        (void)nj_throw(vm, "java/lang/OutOfMemoryError", NULL);
        return NULL;
    }
    ac = nj_class_resolve(vm, buf);
    if (!ac) {
        (void)nj_throw(vm, "java/lang/NoClassDefFoundError", buf);
        return NULL;
    }
    a = nj_new_array(vm, ac, len,
                     ac->array_elem ? (int)ac->array_elem : NJ_ARRAY_T_REF);
    if (!a) {
        (void)nj_throw_oom(vm);
        return NULL;
    }
    return a;
}

nj_ref nj_array_new(nj_vm *vm, const char *elem, nj_u4 len)
{
    int i;

    if (!vm || !elem) {
        return NULL;
    }
    for (i = 0; i < 8; i++) {
        if (prim_elem_name(i + 1) && prim_elem_name(i + 1)[0] == elem[0] &&
            elem[1] == '\0') {
            return array_new_internal(vm, elem, len);
        }
    }
    if (elem[0] == '[' || elem[0] == 'L') {
        return array_new_internal(vm, elem, len);
    }
    {
        char buf[280];
        nj_u4 n = 0u;
        buf[n++] = 'L';
        while (elem[n - 1u] && n + 2u < (nj_u4)sizeof(buf)) {
            buf[n] = elem[n - 1u];
            n++;
        }
        buf[n - 1u] = '\0';
        {
            nj_u4 k;
            for (k = 0u; buf[k]; k++) {
                if (buf[k] == '.') {
                    buf[k] = '/';
                }
            }
        }
        return array_new_internal(vm, buf, len);
    }
}

/*
 * Allocate an array from a full array descriptor ("[I", "[[Ljava/lang/String;").
 * Used by newarray / anewarray / multianewarray in the interpreter.
 */
nj_ref nj_array_new_of(nj_vm *vm, const char *array_desc, nj_u4 len)
{
    if (!vm || !array_desc || !*array_desc) {
        return NULL;
    }
    if (array_desc[0] == '[') {
        return array_new_internal(vm, array_desc + 1, len);
    }
    return array_new_internal(vm, array_desc, len);
}

nj_ref nj_byte_array_new(nj_vm *vm, nj_u4 len)   { return array_new_internal(vm, "B", len); }
nj_ref nj_char_array_new(nj_vm *vm, nj_u4 len)   { return array_new_internal(vm, "C", len); }
nj_ref nj_short_array_new(nj_vm *vm, nj_u4 len)  { return array_new_internal(vm, "S", len); }
nj_ref nj_int_array_new(nj_vm *vm, nj_u4 len)    { return array_new_internal(vm, "I", len); }

nj_ref nj_object_array_new(nj_vm *vm, const char *elem_cls, nj_u4 len)
{
    char buf[280];
    nj_u4 n = 0u;
    const char *p = elem_cls;

    buf[n++] = '[';
    if (p && p[0] == '[') {
        /* Already an array descriptor. */
        while (*p && n + 1u < (nj_u4)sizeof(buf)) {
            buf[n++] = *p++;
        }
        buf[n] = '\0';
    } else {
        buf[n++] = 'L';
        while (p && *p && n + 2u < (nj_u4)sizeof(buf)) {
            buf[n++] = (*p == '.') ? '/' : *p;
            p++;
        }
        buf[n++] = ';';
        buf[n] = '\0';
    }
    return array_new_internal(vm, buf + 1, len);
}

nj_u4 nj_array_length(nj_ref a)
{
    return (a && a->kind == NJ_OBJ_ARRAY) ? a->length : 0u;
}

void *nj_array_data(nj_ref a)
{
    return (a && a->kind == NJ_OBJ_ARRAY) ? (void *)a->u.bytes : NULL;
}

nj_s4 nj_array_get_int(nj_ref a, nj_u4 i)
{
    if (!a || a->kind != NJ_OBJ_ARRAY || i >= a->length) {
        return 0;
    }
    switch (a->aflags) {
    case NJ_ARRAY_T_BOOLEAN:
    case NJ_ARRAY_T_BYTE:   return (nj_s4)((nj_s1 *)a->u.bytes)[i];
    case NJ_ARRAY_T_CHAR:   return (nj_s4)((nj_u2 *)a->u.bytes)[i];
    case NJ_ARRAY_T_SHORT:  return (nj_s4)((nj_s2 *)a->u.bytes)[i];
    case NJ_ARRAY_T_INT:    return ((nj_s4 *)a->u.bytes)[i];
    case NJ_ARRAY_T_LONG:   return (nj_s4)((nj_s8 *)a->u.bytes)[i];
    case NJ_ARRAY_T_FLOAT:  return (nj_s4)((float *)a->u.bytes)[i];
    case NJ_ARRAY_T_DOUBLE: return (nj_s4)((double *)a->u.bytes)[i];
    default:                return 0;
    }
}

void nj_array_set_int(nj_ref a, nj_u4 i, nj_s4 v)
{
    if (!a || a->kind != NJ_OBJ_ARRAY || i >= a->length) {
        return;
    }
    switch (a->aflags) {
    case NJ_ARRAY_T_BOOLEAN:
    case NJ_ARRAY_T_BYTE:   ((nj_s1 *)a->u.bytes)[i] = (nj_s1)v; break;
    case NJ_ARRAY_T_CHAR:   ((nj_u2 *)a->u.bytes)[i] = (nj_u2)v; break;
    case NJ_ARRAY_T_SHORT:  ((nj_s2 *)a->u.bytes)[i] = (nj_s2)v; break;
    case NJ_ARRAY_T_INT:    ((nj_s4 *)a->u.bytes)[i] = v; break;
    case NJ_ARRAY_T_LONG:   ((nj_s8 *)a->u.bytes)[i] = (nj_s8)v; break;
    case NJ_ARRAY_T_FLOAT:  ((float *)a->u.bytes)[i] = (float)v; break;
    case NJ_ARRAY_T_DOUBLE: ((double *)a->u.bytes)[i] = (double)v; break;
    default: break;
    }
}

nj_ref nj_array_get_ref(nj_ref a, nj_u4 i)
{
    if (!a || a->kind != NJ_OBJ_ARRAY || a->aflags != NJ_ARRAY_T_REF ||
        i >= a->length) {
        return NULL;
    }
    return a->u.refs[i];
}

void nj_array_set_ref(nj_ref a, nj_u4 i, nj_ref v)
{
    if (!a || a->kind != NJ_OBJ_ARRAY || a->aflags != NJ_ARRAY_T_REF ||
        i >= a->length) {
        return;
    }
    a->u.refs[i] = v;
}

/* ------------------------------------------------------------------ */
/* Field access                                                        */
/* ------------------------------------------------------------------ */
/*
 * Field storage helpers.
 *
 * Instance fields live at `f->offset` bytes into the object; static fields
 * live in vm->statics at index `f->offset`.  The accessors below take the VM
 * explicitly because statics cannot be located from the object pointer.
 */
static nj_u1 *field_addr(nj_vm *vm, nj_ref obj, const nj_field *f)
{
    if (!f) {
        return NULL;
    }
    if ((f->flags & NJ_ACC_STATIC) != 0u) {
        if (!vm || (nj_u4)f->offset >= vm->nstatics) {
            return NULL;
        }
        return (nj_u1 *)&vm->statics[f->offset];
    }
    if (!obj) {
        return NULL;
    }
    return (nj_u1 *)obj + f->offset;
}

nj_s4 nj_field_read_int(nj_vm *vm, nj_ref obj, const nj_field *f)
{
    nj_u1 *p = field_addr(vm, obj, f);

    /* The type switch itself lives in nj_internal.h so the interpreter can
     * inline it; this is the called version of the same body. */
    return p ? nj_field_load_int(p, f) : 0;
}

void nj_field_write_int(nj_vm *vm, nj_ref obj, const nj_field *f, nj_s4 v)
{
    nj_u1 *p = field_addr(vm, obj, f);

    if (p) {
        nj_field_store_int(p, f, v);
    }
}

nj_s8 nj_field_read_long(nj_vm *vm, nj_ref obj, const nj_field *f)
{
    nj_u1 *p = field_addr(vm, obj, f);
    if (!p) {
        return 0;
    }
    if (f->ptype == NJ_PT_DOUBLE || f->ptype == NJ_PT_LONG) {
        return nj_field_load_long(p, f);
    }
    return (nj_s8)nj_field_load_int(p, f);
}

void nj_field_write_long(nj_vm *vm, nj_ref obj, const nj_field *f, nj_s8 v)
{
    nj_u1 *p = field_addr(vm, obj, f);
    if (!p) {
        return;
    }
    if (f->ptype == NJ_PT_DOUBLE || f->ptype == NJ_PT_LONG) {
        nj_field_store_long(p, f, v);
    } else {
        nj_field_store_int(p, f, (nj_s4)v);
    }
}

nj_ref nj_field_read_ref(nj_vm *vm, nj_ref obj, const nj_field *f)
{
    nj_u1 *p = field_addr(vm, obj, f);
    nj_ref r = NULL;

    if (p) {
        memcpy(&r, p, sizeof(r));
    }
    return r;
}

void nj_field_write_ref(nj_vm *vm, nj_ref obj, const nj_field *f, nj_ref v)
{
    nj_u1 *p = field_addr(vm, obj, f);
    if (!p) {
        return;
    }
    memcpy(p, &v, sizeof(v));
}

int nj_field_is_float(const nj_field *f)
{
    return f != NULL && f->ptype == NJ_PT_FLOAT;
}

nj_u4 nj_field_read_bits32(nj_vm *vm, nj_ref obj, const nj_field *f)
{
    nj_u1 *p = field_addr(vm, obj, f);
    nj_u4 v = 0u;

    if (p) {
        memcpy(&v, p, 4u);
    }
    return v;
}

void nj_field_write_bits32(nj_vm *vm, nj_ref obj, const nj_field *f, nj_u4 v)
{
    nj_u1 *p = field_addr(vm, obj, f);
    if (p) {
        memcpy(p, &v, 4u);
    }
}

nj_u8 nj_field_read_bits64(nj_vm *vm, nj_ref obj, const nj_field *f)
{
    nj_u1 *p = field_addr(vm, obj, f);
    nj_u8 v = 0u;

    if (p) {
        memcpy(&v, p, 8u);
    }
    return v;
}

void nj_field_write_bits64(nj_vm *vm, nj_ref obj, const nj_field *f, nj_u8 v)
{
    nj_u1 *p = field_addr(vm, obj, f);
    if (p) {
        memcpy(p, &v, 8u);
    }
}

/* ------------------------------------------------------------------ */
/* Exceptions                                                          */
/* ------------------------------------------------------------------ */
nj_ref nj_exception_new(nj_vm *vm, const char *cls, const char *msg)
{
    nj_class *c;
    nj_ref e;

    if (!vm) {
        return NULL;
    }
    /*
     * Building an exception can itself fail (the class library may be missing,
     * or memory may be exhausted).  Re-entering this function on that path
     * would recurse until the C stack dies, so the second attempt gets a bare
     * instance with no constructor call at all.
     */
    if (vm->in_exception_new) {
        c = nj_class_lookup(vm, "java/lang/Throwable");
        return c ? nj_new_instance(vm, c) : NULL;
    }
    vm->in_exception_new = 1u;
    c = nj_class_lookup(vm, cls ? cls : "java/lang/Throwable");
    if (!c) {
        c = nj_class_lookup(vm, "java/lang/Throwable");
        if (!c) {
            vm->in_exception_new = 0u;
            return NULL;   /* the class library itself is missing */
        }
    }
    e = nj_new_instance(vm, c);
    if (!e) {
        vm->in_exception_new = 0u;
        return NULL;
    }

    {
        nj_u4 mark = nj_root_mark(vm);
        nj_value args[2];
        nj_status st;

        nj_root(vm, e);
        if (msg) {
            nj_ref s = nj_string_new(vm, msg);
            if (!s) {
                nj_root_release(vm, mark);
                vm->in_exception_new = 0u;
                return NULL;
            }
            nj_root(vm, s);
            args[0].r = s;
            st = nj_call_special(vm, e, c->name, "<init>",
                                 "(Ljava/lang/String;)V", args, NULL);
        } else {
            st = nj_call_special(vm, e, c->name, "<init>", "()V", NULL, NULL);
        }
        if (st != NJ_OK) {
            /* A missing constructor must not cascade into another exception:
             * fall back to the already-allocated bare instance. */
            nj_clear_exception(vm);
        }
        nj_root_release(vm, mark);
    }
    vm->in_exception_new = 0u;
    return e;
}

nj_status nj_vm_throw_ref(nj_vm *vm, nj_ref exc)
{
    if (!vm) {
        return NJ_ERR;
    }
    vm->exception_method = NULL;
    vm->exception_pc = 0u;
    vm->resolve_fail = NJ_RESOLVE_OK;
    if (!exc) {
        exc = nj_exception_new(vm, "java/lang/NullPointerException", NULL);
    }
    vm->exception = exc;
    return NJ_ERR;
}

nj_status nj_vm_throw_new(nj_vm *vm, const char *class_name)
{
    return nj_throw(vm, class_name, NULL);
}

nj_status nj_throw(nj_vm *vm, const char *cls, const char *msg)
{
    nj_ref e;

    if (!vm) {
        return NJ_ERR;
    }
    if (vm->exception) {
        return NJ_ERR;   /* keep the first, most specific exception */
    }
    /*
     * Name the throw site before the frame unwinds.  printStackTrace() reports
     * where the exception is CAUGHT, which for the titles that swallow their
     * own exceptions is a catch block - the interesting question is which call
     * failed, and that is only answerable here.
     */
    if (vm->cur && vm->cur->nframes > 0) {
        const nj_frame *fr = &vm->cur->frames[vm->cur->nframes - 1];
        if (fr->m && fr->m->owner) {
            nj_logf(vm, 2, "thrown: %s at %s.%s%s pc=%lu: %s", cls ? cls : "?",
                    fr->m->owner->name, fr->m->name, fr->m->desc,
                    (unsigned long)fr->pc, msg ? msg : "(no message)");
        }
    }
    e = nj_exception_new(vm, cls, msg);
    if (!e) {
        /* Absolute last resort: no memory and no class library.  Signal a
         * plain error; the caller will report it as OutOfMemoryError. */
        vm->exception = NULL;
        return NJ_ERR_OOM;
    }
    vm->exception = e;
    return NJ_ERR;
}

nj_status nj_throw_oom(nj_vm *vm)
{
    return nj_throw(vm, "java/lang/OutOfMemoryError", NULL);
}

int nj_has_exception(nj_vm *vm)
{
    return vm && vm->exception != NULL;
}

nj_ref nj_take_exception(nj_vm *vm)
{
    nj_ref e;

    if (!vm) {
        return NULL;
    }
    e = vm->exception;
    vm->exception = NULL;
    return e;
}

void nj_clear_exception(nj_vm *vm)
{
    if (vm) {
        vm->exception = NULL;
        vm->exception_method = NULL;
        vm->exception_pc = 0u;
        vm->resolve_fail = NJ_RESOLVE_OK;
    }
}

/*
 * Turn a recorded resolution failure into the Java error a real JVM would
 * raise, with the signature spelled out.
 *
 * The signature is the whole point.  "NoSuchMethodError: append" sends you
 * looking for a missing method; "NoSuchMethodError:
 * javax/microedition/lcdui/Command.<init>(Ljava/lang/String;II)V" tells you
 * immediately that the constructor exists with a different descriptor - which
 * is what actually happens when a class library is written against a slightly
 * different API than the one the compiler saw.
 */
nj_status nj_resolve_error(nj_vm *vm)
{
    char buf[320];
    nj_u4 n = 0u;
    const char *cls = vm->resolve_cls ? vm->resolve_cls : "?";
    const char *name = vm->resolve_name;
    const char *desc = vm->resolve_desc;
    const char *err;

    switch (vm->resolve_fail) {
    case NJ_RESOLVE_METHOD: err = "java/lang/NoSuchMethodError"; break;
    case NJ_RESOLVE_FIELD:  err = "java/lang/NoSuchFieldError";  break;
    default:                err = "java/lang/NoClassDefFoundError"; break;
    }

    while (*cls && n + 1u < sizeof(buf)) {
        buf[n++] = *cls++;
    }
    if (name && vm->resolve_fail != NJ_RESOLVE_CLASS) {
        if (n + 1u < sizeof(buf)) {
            buf[n++] = '.';
        }
        while (*name && n + 1u < sizeof(buf)) {
            buf[n++] = *name++;
        }
        if (desc) {
            while (*desc && n + 1u < sizeof(buf)) {
                buf[n++] = *desc++;
            }
        }
    }
    buf[n] = '\0';

    /* Keep the first, most specific exception; nj_throw honours that. */
    return nj_throw(vm, err, buf);
}

/* ------------------------------------------------------------------ */
/* C -> Java bridge                                                    */
/* ------------------------------------------------------------------ */
static nj_method *bridge_lookup(nj_vm *vm, nj_class *c, const char *name,
                                const char *desc, int want_static)
{
    nj_method *m;
    nj_u2 i;

    if (!c) {
        return NULL;
    }
    (void)vm;
    for (;;) {
        for (i = 0u; i < c->method_count; i++) {
            nj_method *k = &c->methods[i];
            int is_static = (k->flags & NJ_ACC_STATIC) != 0u;
            if (is_static != want_static) {
                continue;
            }
            if (strcmp(k->name, name) == 0 && strcmp(k->desc, desc) == 0) {
                return k;
            }
        }
        if (!c->super) {
            break;
        }
        c = c->super;
    }
    m = NULL;
    return m;
}

int nj_vm_set_halt_after_ms(nj_vm *vm, nj_u4 ms)
{
    int elapsed;

    if (!vm) {
        return 0;
    }
    elapsed = (vm->halt_at_ms != 0 && nj_now_ms(vm) >= vm->halt_at_ms) ? 1 : 0;
    vm->halt_at_ms = (ms == 0u) ? 0 : nj_now_ms(vm) + (nj_s8)ms;
    return elapsed;
}

static /* How long a C caller will let a blocked Java call wait to be resumable. */
#define NJ_CALL_BLOCK_MS 5000u

nj_status vm_invoke(nj_vm *vm, nj_ref obj, const char *cls,
                           const char *name, const char *desc,
                           nj_value *args, nj_value *ret, int special)
{
    nj_thread *th;
    nj_class *c;
    nj_method *m;
    nj_value *base;
    nj_value *p;
    nj_u2 total;
    int want_static = (obj == NULL);
    int saved_frames;
    nj_value *saved_top;
    nj_status st;

    if (!vm || !cls || !name || !desc) {
        return NJ_ERR;
    }
    /*
     * A MIDlet may keep running Java code after it calls notifyDestroyed():
     * a save-on-exit block, for instance, is ordinary bytecode the thread
     * executes until it returns.  Such code frequently calls back into Java
     * from a native (Integer.valueOf through RecordStore, for one), and the
     * old `!vm->running` gate refused it with NJ_ERR_STATE.  Callers turned
     * that into a Java exception - 宠物王国4 reported OutOfMemoryError and
     * died - so only a destroyed VM refuses now; a stopping one lets the
     * thread finish what it is doing.
     */
    if (!vm->running && vm->state != NJ_VM_STOPPING) {
        return NJ_ERR_STATE;
    }
    th = vm->cur;
    if (!th) {
        th = nj_thread_create(vm, NULL, 0);
        if (!th) {
            return NJ_ERR_NOMEM;
        }
        vm->cur = th;
    }

    /*
     * A native that ignored a failed allocation can end up calling a method on
     * a reference whose block the collector has already recycled.  Dereferencing
     * that runs into the free list's next pointer and faults inside vm_invoke
     * (test_lib: the harness keeps calling after createPlayer threw
     * OutOfMemoryError).  Reject a pointer the heap does not own instead.
     */
    if (obj && !nj_heap_owns_object_fast(vm, obj)) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "a call was made on a reference the heap does not own");
    }
    c = nj_class_lookup(vm, cls);
    if (!c) {
        return nj_throw(vm, "java/lang/NoClassDefFoundError", cls);
    }
    /* Virtual calls from C go to the receiver's own class first. */
    if (!special && obj && obj->cls) {
        nj_method *om = bridge_lookup(vm, obj->cls, name, desc, 0);
        if (om) {
            c = obj->cls;
        }
    }
    m = bridge_lookup(vm, c, name, desc, want_static);
    if (!m) {
        (void)nj_class_init(vm, c);
        if (vm->exception) {
            return NJ_ERR;
        }
        m = bridge_lookup(vm, c, name, desc, want_static);
    }
    if (!m) {
        char buf[160];
        nj_u4 n = 0u;
        const char *q = c->name;
        while (q && *q && n + 1u < sizeof(buf)) { buf[n++] = *q++; }
        if (n + 1u < sizeof(buf)) { buf[n++] = '.'; }
        q = name;
        while (q && *q && n + 1u < sizeof(buf)) { buf[n++] = *q++; }
        buf[n] = '\0';
        return nj_throw(vm, "java/lang/NoSuchMethodError", buf);
    }
    if (!special) {
        st = nj_class_init(vm, m->owner);
        if (st != NJ_OK) {
            return st;
        }
    }

    total = (nj_u2)((want_static ? 0u : 1u) + m->arg_slots);
    if (m->max_locals < total) {
        /* Malformed max_locals: the arguments would not fit. */
        return nj_throw(vm, "java/lang/VerifyError", m->name);
    }

    saved_frames = th->nframes;
    saved_top = th->top;
    base = (th->nframes > 0) ? th->frames[th->nframes - 1].sp : th->stack_mem;

    if (base + m->max_locals + m->max_stack + 8 > th->stack_end) {
        return nj_throw(vm, "java/lang/StackOverflowError", name);
    }

    p = base;
    if (!want_static) {
        (p++)->r = obj;
    }
    {
        nj_u2 i;
        for (i = 0u; i < m->arg_slots; i++) {
            /*
             * Copy the WHOLE nj_value.  Assigning only `.bits` (32 bits) would
             * silently truncate every reference and double argument to half a
             * pointer - which then looks like a wild address much later.
             */
            if (args) {
                p[i] = args[i];
            } else {
                p[i].bits = 0u;
            }
        }
        p += m->arg_slots;
    }

    if (m->code == NULL) {
        /* The target is a native: run it directly.  nj_exec interprets
         * bytecode only, so pushing a native frame and calling it would stop
         * with NJ_ERR_HALT. */
        nj_value nret[2];
        st = nj_invoke_native(vm, th, m, base, total, nret);
        th->nframes = saved_frames;
        th->top = saved_top;
        if (st == NJ_ERR_TIMEOUT) {
            /* A C caller cannot be suspended; treat the block as completed. */
            st = NJ_OK;
        }
        if (st == NJ_OK && ret) {
            *ret = nret[0];
        }
        return st;
    }

    st = nj_frame_push(vm, th, m, base, total);
    if (st != NJ_OK) {
        th->top = saved_top;
        return st;
    }

    /*
     * Run to completion, absorbing scheduling quanta.
     *
     * nj_exec() returns NJ_ERR_YIELD when the thread has executed its quantum;
     * that is normal, not a failure - and a C caller cannot be suspended and
     * resumed the way a Java frame can, so the bridge has to keep going.  The
     * frame was saved before the yield, so the next call resumes it exactly
     * where it stopped.
     *
     * Treating NJ_ERR_YIELD as an error makes any Java method called from C
     * that is longer than NJ_SCHED_QUANTUM bytecodes look like it failed.  A
     * graphics-heavy Canvas.paint() is the obvious victim: the painter reports
     * "paint() failed" and the screen never updates, with no exception set
     * anywhere.
     *
     * NJ_ERR_TIMEOUT is different: it means a native blocked (Thread.sleep and
     * friends) and the thread is no longer runnable, so spinning would hang.
     */
    for (;;) {
        st = nj_exec(vm, th, saved_frames);
        /*
         * A wall-clock deadline on this call.  The check sits here, once per
         * quantum, rather than in the dispatch loop: a MIDlet in a CPU-bound
         * loop yields every NJ_SCHED_QUANTUM bytecodes anyway, so this catches
         * it without paying a branch per instruction.
         */
        if (vm->halt_at_ms != 0 && nj_now_ms(vm) >= vm->halt_at_ms) {
            st = NJ_ERR_HALT;
            break;
        }
        if (st == NJ_ERR_YIELD && th->state == NJ_THREAD_RUNNING &&
            th->nframes > saved_frames) {
            continue;   /* quantum expired mid-call: resume it */
        }
        /*
         * The native returned NJ_ERR_YIELD, not NJ_ERR_TIMEOUT: the TIMEOUT
         * spelling is synthesised after this loop.  Matching only on TIMEOUT
         * here is why pumping appeared to do nothing at all.
         */
        if ((st == NJ_ERR_YIELD || st == NJ_ERR_TIMEOUT) &&
            (th->state == NJ_THREAD_SLEEPING ||
             th->state == NJ_THREAD_WAITING ||
             th->state == NJ_THREAD_BLOCKED)) {
            nj_s8 start = nj_now_ms(vm);

            /*
             * A native blocked - Thread.sleep(), Object.wait(), Thread.join()
             * and friends.  The thread is parked, NOT dead, and its frames are
             * intact, so the right answer is to wait for it to be runnable
             * again and resume the very frame that blocked, not to report a
             * failure.
             *
             * Waiting is not enough on its own, because the thread that will
             * wake this one is very often another Java thread: a MIDlet whose
             * constructor starts a loader and joins it, or waits for its first
             * frame, parks here with nothing but the loader able to move it.
             * So the other threads are given the CPU through
             * nj_scheduler_step(), which runs whatever is runnable, sleeps
             * briefly when the next deadline is near, and reports NJ_ERR_HALT
             * when there is nothing left that could ever wake this thread.
             *
             * The display stays alive meanwhile: on a handset the AMS paints
             * from its own thread, so a constructor that sleeps must not stop
             * the screen.  The hook is NULL unless a runner asked for it, so
             * nothing changes for an embedder that drives frames itself.
             */
            int stop_requested = 0;
            /*
             * This thread is now on a C stack that another thread's scheduler
             * step must not touch: tell the scheduler to leave it alone and
             * let this pump resume it, which is the only stack its frames
             * belong to.  Saved and restored rather than cleared, because a
             * native may nest another C call inside this one.
             */
            int prev_in_c_call = th->in_c_call;

            th->in_c_call = 1;

            for (;;) {
                nj_s8 now = nj_now_ms(vm);
                nj_s8 until = (th->state == NJ_THREAD_SLEEPING)
                              ? th->wake_at_ms : th->wait_deadline;

                if (th->state == NJ_THREAD_RUNNING || th->state == NJ_THREAD_DEAD) {
                    break;              /* another thread woke it */
                }
                if (until != 0 && now >= until) {
                    if (th->wait_kind == NJ_WAIT_OBJECT) {
                        /*
                         * wait(timeout) returns holding the monitor again.
                         * Mark the wait expired and let the monitor go through
                         * mon_try_reacquire(): if another thread holds it this
                         * thread stays parked and the loop below gives that
                         * thread the CPU - resuming unlocked would put two
                         * threads in the same critical section.
                         */
                        th->wait_deadline = 0;
                        if (nj_vm_monitor_reacquire(vm, th)) {
                            break;
                        }
                    } else {
                        th->state = NJ_THREAD_RUNNING;
                        th->wait_monitor = NULL;
                        th->wait_kind = NJ_WAIT_NONE;
                        th->wait_deadline = 0;
                        break;
                    }
                }
                if (vm->halt_at_ms != 0 && now >= vm->halt_at_ms) {
                    st = NJ_ERR_HALT;
                    break;
                }
                /* An indefinite join may span the entire game. A worker
                 * that can run or wake later still has a way to finish it;
                 * only a wait with no such worker gets the deadlock timeout. */
                if (until == 0) {
                    nj_u4 i;
                    int can_progress = 0;
                    if (!vm->in_call_pump) {
                        for (i = 0; i < vm->nthreads; i++) {
                            nj_thread *other = vm->threads[i];
                            if (!other || other == th || other->in_c_call ||
                                other->nframes == 0) continue;
                            if (other->state == NJ_THREAD_RUNNING ||
                                other->state == NJ_THREAD_SLEEPING ||
                                (other->state == NJ_THREAD_WAITING &&
                                 other->wait_deadline != 0)) {
                                can_progress = 1;
                                break;
                            }
                        }
                    }
                    if (can_progress) start = now;
                    else if (now - start >= (nj_s8)NJ_CALL_BLOCK_MS) break;
                }
                if (vm->frame_hook && !vm->in_frame_hook) {
                    int stop;
                    vm->in_frame_hook = 1;
                    stop = vm->frame_hook(vm, vm->frame_hook_ud);
                    vm->in_frame_hook = 0;
                    if (stop) {
                        stop_requested = 1;
                        break;
                    }
                }
                /*
                 * A nested C call inside another thread's native must not
                 * re-enter the scheduler; there the port clock is all this
                 * loop has to work with.
                 */
                if (vm->in_call_pump) {
                    if (vm->port && vm->port->sleep_ms) {
                        vm->port->sleep_ms(vm->port->ud, 1u);
                    }
                    continue;
                }
                vm->in_call_pump = 1;
                {
                    nj_status sched = nj_scheduler_step(vm);
                    vm->in_call_pump = 0;
                    if (sched != NJ_OK && th->state != NJ_THREAD_RUNNING) {
                        break;          /* nothing left that could wake it */
                    }
                }
            }
            if (stop_requested) {
                /*
                 * The embedder is done with this run.  The Java frame is
                 * parked in a sleep it will never wake from, so abandon it
                 * and unwind: this is the only way out for a MIDlet that
                 * never returns from startApp().
                 */
                st = NJ_ERR_HALT;
                break;
            }
            th->in_c_call = prev_in_c_call;
            if (th->state != NJ_THREAD_RUNNING) {
                break;   /* it really is stuck: report it rather than spin */
            }
            continue;
        }
        break;
    }
    if (st == NJ_ERR_YIELD) {
        /* The thread blocked inside the call.  Report it as such rather than
         * as a generic failure. */
        st = NJ_ERR_TIMEOUT;
    }

    if (st == NJ_OK && ret) {
        *ret = th->ret_value;
    }
    if (st != NJ_OK) {
        th->nframes = saved_frames;
        th->top = saved_top;
    }
    return st;
}

nj_status nj_call(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                  const char *desc, nj_value *args, nj_value *ret)
{
    return vm_invoke(vm, obj, cls, name, desc, args, ret, 0);
}

nj_status nj_call_special(nj_vm *vm, nj_ref obj, const char *cls,
                          const char *name, const char *desc,
                          nj_value *args, nj_value *ret)
{
    return vm_invoke(vm, obj, cls, name, desc, args, ret, 1);
}

nj_status nj_call_static(nj_vm *vm, const char *cls, const char *name,
                         const char *desc, nj_value *args, nj_value *ret)
{
    return vm_invoke(vm, NULL, cls, name, desc, args, ret, 1);
}

nj_s4 nj_call_int(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                  const char *desc, nj_value *args)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call(vm, obj, cls, name, desc, args, &ret) != NJ_OK) {
        return 0;
    }
    return ret.i;
}

nj_ref nj_call_ref(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                   const char *desc, nj_value *args)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call(vm, obj, cls, name, desc, args, &ret) != NJ_OK) {
        return NULL;
    }
    return ret.r;
}

/* ------------------------------------------------------------------ */
/* MIDlet bootstrap                                                    */
/* ------------------------------------------------------------------ */
/*
 * The exception a failed step left behind, or - when it left none - one that
 * says which step failed.
 *
 * A failure with no exception object is not hypothetical: it is what a title
 * whose <clinit> or <init> bailed out through a path that returned an error
 * without throwing produces.  The runner can only name the exception it is
 * handed, so a NULL here used to be reported as "cannot instantiate X: ?",
 * which tells the reader nothing at all.  Naming the step is worth more than
 * the silence, and it is still true: an InternalError is exactly what "the
 * interpreter failed without saying why" is.
 */
/*
 * A number in a diagnostic is not a diagnosis: "status -3" made a reader go
 * and look up nj_types.h to find out that the thread had finished, which the
 * MIDlet did on purpose.  The names come from nj_status, so they cannot drift.
 */
static const char *status_name(nj_status st)
{
    switch (st) {
    case NJ_ERR_OOM:         return "the Java heap ran out";
    case NJ_ERR_HALT:        return "the thread finished";
    case NJ_ERR_YIELD:       return "the scheduler quantum expired";
    case NJ_ERR_TIMEOUT:     return "a blocking native timed out";
    case NJ_ERR_UNSUPPORTED: return "a feature is not compiled in";
    case NJ_ERR_FORMAT:      return "malformed jar, class or image";
    case NJ_ERR_STATE:       return "the VM was in the wrong state";
    case NJ_ERR_EXIT:        return "System.exit() was called";
    case NJ_ERR_NOMEM:       return "the host could not allocate";
    default:                 return "unknown status";
    }
}

static nj_ref take_or_synthesise(nj_vm *vm, const char *cls_name,
                                 const char *stage, const char *member,
                                 nj_status st)
{
    nj_ref e = nj_take_exception(vm);
    char buf[256];

    if (e) {
        return e;
    }
    if (st == NJ_ERR_TIMEOUT) {
        /*
         * The one case worth spelling out, because it is not a defect in the
         * class: the member called something that waits, and a C caller
         * cannot be suspended the way a Java frame can.  com.ea.sims3.MonkeyApp
         * does this in <init>, and the corpus has other titles that sleep on
         * their first frame.
         */
        snprintf(buf, sizeof(buf),
                 "%s of %s blocked (it called Thread.sleep or wait, and a C "
                 "caller cannot be suspended)", stage, cls_name);
    } else if (st == NJ_ERR_HALT) {
        /*
         * The MIDlet stopped the run itself - notifyDestroyed() from <init>,
         * which is what a title does when a check it makes fails.  Saying
         * "failed" here sent a reader looking for a defect that is not
         * there; the VM already knows the reason, so quote it.
         */
        const char *why = nj_vm_stop_why(vm);
        snprintf(buf, sizeof(buf), "%s of %s stopped the run (%s)%s%s",
                 stage, cls_name, status_name(st), why ? ": " : "",
                 why ? why : "");
    } else {
        snprintf(buf, sizeof(buf),
                 "%s of %s failed: %s (%d), and raised nothing (%s)",
                 stage, cls_name, status_name(st), (int)st, member);
    }
    return nj_exception_new(vm, "java/lang/InternalError", buf);
}

/*
 * Register a callback the VM runs while a Java thread is parked.  See
 * include/nj2me/nj_vm.h for why a runner wants one.
 */
int nj_vm_set_frame_hook(nj_vm *vm, int (*fn)(nj_vm *vm, void *ud), void *ud)
{
    if (!vm) {
        return -1;
    }
    vm->frame_hook = fn;
    vm->frame_hook_ud = ud;
    vm->in_frame_hook = 0;
    return 0;
}

/*
 * Install the port's own glyph source for characters the built-in faces
 * cannot draw.  See include/nj2me/nj_vm.h for the slot layout.
 */
int nj_vm_set_fallback_fonts(nj_vm *vm, const nj_font *const *fonts, int count)
{
    if (!vm || !fonts || count <= 0) {
        return -1;
    }
    nj_fontset_init(&vm->fonts);
    nj_fontset_set_fallback_slots(&vm->fonts, fonts, count);
    return 0;
}

/*
 * Install the port's own glyph source as the slots, for a platform whose
 * typeface should draw every character.  See include/nj2me/nj_vm.h.
 */
int nj_vm_set_primary_fonts(nj_vm *vm, const nj_font *const *fonts, int count)
{
    if (!vm || !fonts || count <= 0) {
        return -1;
    }
    nj_fontset_init(&vm->fonts);
    nj_fontset_set_primary_slots(&vm->fonts, fonts, count);
    return 0;
}


nj_ref nj_vm_new_midlet(nj_vm *vm, const char *class_name, nj_ref *out_exc)
{
    nj_class *c;
    nj_ref o;
    nj_status init_st;
    nj_status ctor_st;

    if (out_exc) {
        *out_exc = NULL;
    }
    if (!vm || !class_name) {
        return NULL;
    }
    c = nj_class_lookup(vm, class_name);
    if (!c) {
        if (out_exc) {
            *out_exc = nj_exception_new(vm, "java/lang/ClassNotFoundException",
                                        class_name);
        }
        return NULL;
    }
    init_st = nj_class_init(vm, c);
    if (init_st != NJ_OK) {
        if (out_exc) {
            *out_exc = take_or_synthesise(vm, c->name, "class initialisation",
                                          "<clinit>",
                                          init_st);
        }
        return NULL;
    }
    o = nj_new_instance(vm, c);
    if (!o) {
        if (out_exc) {
            *out_exc = nj_exception_new(vm, "java/lang/OutOfMemoryError", NULL);
        }
        return NULL;
    }
    ctor_st = nj_call_special(vm, o, c->name, "<init>", "()V", NULL, NULL);
    if (ctor_st != NJ_OK) {
        if (out_exc) {
            *out_exc = take_or_synthesise(vm, c->name, "construction",
                                          "<init>", ctor_st);
        }
        return NULL;
    }
    return o;
}

nj_status nj_vm_start_app(nj_vm *vm, nj_ref midlet)
{
    if (!vm || !midlet) {
        return NJ_ERR;
    }
    vm->app = midlet;
    vm->app_class = midlet->cls;
    return nj_call_special(vm, midlet, midlet->cls->name, "startApp", "()V",
                           NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* Public diagnostics                                                  */
/* ------------------------------------------------------------------ */
/* Local forward declarations: these live in nj_lib_object.c. */
nj_ref class_name_string_pub(nj_vm *vm, nj_class *c);
nj_field *fld_detailMessage_pub(nj_vm *vm);

const char *nj_exception_describe(nj_vm *vm)
{
    static const char unknown[] = "(no exception)";
    nj_ref e;
    nj_field *f;
    nj_ref msg;
    nj_ref cn;

    if (!vm) {
        return unknown;
    }
    e = vm->exception;
    if (!e) {
        return unknown;
    }
    /* The diagnostic must never be the thing that crashes: validate first. */
    if (e->kind > NJ_OBJ_THROW || e->cls == NULL) {
        return "(corrupt exception object)";
    }
    cn = class_name_string_pub(vm, e->cls);
    f = fld_detailMessage_pub(vm);
    msg = f ? nj_field_read_ref(vm, e, f) : NULL;
    {
        /* Render into a buffer owned by the VM so the caller never frees it. */
        static const nj_u4 cap = 224u;
        char *buf = (char *)nj_host_alloc(vm, cap);
        nj_u4 n = 0u;
        if (!buf) {
            return unknown;
        }
        if (cn) {
            n += (nj_u4)nj_string_to_utf8(cn, buf + n, (int)(cap - n - 1u));
        }
        if (msg && nj_string_len(msg) > 0u && n + 3u < cap) {
            buf[n++] = ':';
            buf[n++] = ' ';
            n += (nj_u4)nj_string_to_utf8(msg, buf + n, (int)(cap - n - 1u));
        }
        if (vm->exception_method && n + 8u < cap) {
            const char *q = vm->exception_method->owner->name;
            buf[n++] = ' ';
            buf[n++] = '@';
            buf[n++] = ' ';
            while (q && *q && n + 24u < cap) {
                buf[n++] = (*q == '/') ? '.' : *q;
                q++;
            }
            buf[n++] = '.';
            q = vm->exception_method->name;
            while (q && *q && n + 24u < cap) {
                buf[n++] = *q++;
            }
            buf[n++] = '+';
            {
                nj_u4 v = vm->exception_pc;
                char digits[12];
                int dn = 0;
                if (v == 0u) {
                    digits[dn++] = '0';
                }
                while (v != 0u && dn < 12) {
                    digits[dn++] = (char)('0' + (v % 10u));
                    v /= 10u;
                }
                while (dn > 0) {
                    buf[n++] = digits[--dn];
                }
            }
        }
        buf[n] = '\0';
        /* Cache it on the VM so repeated calls do not allocate. */
        if (vm->scratch_u2) {
            nj_host_free(vm, vm->scratch_u2);
        }
        vm->scratch_u2 = (nj_u2 *)buf;
        return buf;
    }
}

const char *nj_ref_class_name(nj_ref o)
{
    if (!o || !o->cls || !o->cls->name) {
        return NULL;
    }
    return o->cls->name;
}

nj_s4 nj_vm_exit_code(nj_vm *vm)
{
    return vm ? vm->exit_code : 0;
}

void nj_vm_request_stop(nj_vm *vm, nj_s4 code)
{
    nj_vm_stop(vm, code);
}

nj_status nj_run_pending(nj_vm *vm, nj_u4 budget_ms)
{
    return nj_scheduler_run_until_idle(vm, budget_ms);
}

nj_size nj_gc_collect(nj_vm *vm)
{
    if (!vm) {
        return 0u;
    }
    (void)nj_gc_run(vm);
    return nj_heap_used_bytes(vm);
}

void nj_port_shutdown(nj_port *port)
{
    if (!port) {
        return;
    }
    if (port->shutdown) {
        port->shutdown(port->ud);
    }
    port->ud = NULL;
    port->shutdown = NULL;
}

#if NJ_ENABLE_DEBUG
/*
 * Optional instruction trace.  NJ_TRACE=<substring> prints every bytecode
 * executed in a method whose class or method name contains the substring,
 * together with the operand-stack depth.  Compiled out unless NJ_ENABLE_DEBUG
 * is set, so it costs nothing in a release build.
 */
#include <stdlib.h>
#include <stdio.h>

void nj_trace_step(nj_vm *vm, nj_method *m, nj_u4 pc, nj_u4 depth,
                   nj_value *locals, nj_value *stack, nj_value *sp)
{
    int k;

    if (vm->trace_filter && strcmp(vm->trace_filter, "*") != 0 &&
        !strstr(m->name, vm->trace_filter)) {
        return;
    }
    fprintf(stderr, "  %-20s %-14s pc=%-4u sp=%-3u |",
            m->owner ? m->owner->name : "?", m->name,
            (unsigned)pc, (unsigned)depth);
    for (k = 0; k < 4; k++) {
        fprintf(stderr, " L%d=%d", k, (int)locals[k].i);
    }
    fprintf(stderr, " |");
    for (k = 0; k < (int)depth && k < 4; k++) {
        fprintf(stderr, " S%d=%d", k, (int)stack[k].i);
    }
    fprintf(stderr, "\n");
    (void)sp;
}

int nj_trace_enabled(nj_vm *vm)
{
    const char *t = getenv("NJ_TRACE");
    (void)vm;
    return t && *t;
}
#endif
