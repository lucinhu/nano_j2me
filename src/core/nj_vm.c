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

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

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
static nj_status vm_setup(nj_vm *vm, const nj_port *port, nj_size heap_bytes)
{
    nj_size hs;

    vm->port = port;
    vm->state = NJ_VM_READY;
    vm->hash_seed = 0x9E3779B9u;

    hs = heap_bytes ? heap_bytes : (nj_size)NJ_MAX_HEAP;
    if (nj_heap_init(vm, NULL, hs) != NJ_OK) {
        return NJ_ERR_NOMEM;
    }
    vm->statics = (nj_value *)nj_host_alloc(
        vm, (nj_size)NJ_MAX_STATICS * sizeof(nj_value));
    if (!vm->statics) {
        return NJ_ERR_NOMEM;
    }
    vm->nstatics = 0u;

    if (nj_cp_cache_init(vm, 512u) != NJ_OK) {
        return NJ_ERR_NOMEM;
    }
    if (nj_natives_init(vm) != NJ_OK) {
        return NJ_ERR_NOMEM;
    }

    /* Materialise the C-implemented class library, then let it install its
     * static state (System.out, Math constants, ...). */
    nj_builtins_register_all(vm);
    if (nj_lib_late_init(vm) != NJ_OK) {
        return NJ_ERR;
    }

    /* The primordial thread always exists so that C code can call into Java
     * even before any MIDlet starts. */
    vm->cur = nj_thread_create(vm, NULL, 1);
    if (!vm->cur) {
        return NJ_ERR_NOMEM;
    }
    vm->running = 1u;
    vm->verbose = 2u;      /* error + warn + info; debug needs opting in */
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

    if (vm_setup(vm, port, heap_bytes) != NJ_OK) {
        nj_vm_destroy(vm);
        return NULL;
    }
    vm->initialized = 1u;
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
    out->threads = vm->nthreads;
    out->cp_cache_entries = vm->cp_cache_used;
    out->cp_cache_hits = vm->cp_cache_hits;
    out->cp_cache_misses = vm->cp_cache_misses;
    out->natives = vm->nnatives;
    out->native_misses = vm->native_misses;
    out->host_live = vm->host_count;
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
    if (nj_class_link(vm, c) != NJ_OK) {
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
nj_status nj_class_init(nj_vm *vm, nj_class *c)
{
    nj_method *clinit;

    if (!vm || !c) {
        return NJ_ERR;
    }
    if (c->state >= NJ_CLASS_READY) {
        return NJ_OK;
    }
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

    if (clinit && clinit->code) {
        nj_status st = nj_call_static(vm, c->name, "<clinit>", "()V", NULL, NULL);
        if (st != NJ_OK) {
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
static nj_u1 *field_addr(nj_vm *vm, nj_ref obj, nj_field *f)
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

nj_s4 nj_field_read_int(nj_vm *vm, nj_ref obj, nj_field *f)
{
    nj_u1 *p = field_addr(vm, obj, f);

    if (!p) {
        return 0;
    }
    switch (f->ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:   return (nj_s4)(*(nj_s1 *)p);
    case NJ_PT_CHAR:   return (nj_s4)(*(nj_u2 *)p);
    case NJ_PT_SHORT:  return (nj_s4)(*(nj_s2 *)p);
    case NJ_PT_LONG:   return (nj_s4)(*(nj_s8 *)p);
    case NJ_PT_FLOAT:  return (nj_s4)(*(float *)p);
    case NJ_PT_DOUBLE: return (nj_s4)(*(double *)p);
    case NJ_PT_OBJECT: {
        nj_ref r;
        memcpy(&r, p, sizeof(r));
        return (nj_s4)(nj_size)(uintptr_t)r;
    }
    default:           return *(nj_s4 *)p;
    }
}

void nj_field_write_int(nj_vm *vm, nj_ref obj, nj_field *f, nj_s4 v)
{
    nj_u1 *p = field_addr(vm, obj, f);

    if (!p) {
        return;
    }
    switch (f->ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:   *(nj_s1 *)p = (nj_s1)v; break;
    case NJ_PT_CHAR:   *(nj_u2 *)p = (nj_u2)v; break;
    case NJ_PT_SHORT:  *(nj_s2 *)p = (nj_s2)v; break;
    case NJ_PT_LONG:   *(nj_s8 *)p = (nj_s8)v; break;
    case NJ_PT_FLOAT:  *(float *)p = (float)v; break;
    case NJ_PT_DOUBLE: *(double *)p = (double)v; break;
    case NJ_PT_OBJECT: {
        nj_ref r = (nj_ref)(void *)(uintptr_t)(nj_size)v;
        memcpy(p, &r, sizeof(r));
        break;
    }
    default:           *(nj_s4 *)p = v; break;
    }
}

nj_s8 nj_field_read_long(nj_vm *vm, nj_ref obj, nj_field *f)
{
    nj_u1 *p = field_addr(vm, obj, f);
    if (!p) {
        return 0;
    }
    if (f->ptype == NJ_PT_DOUBLE) {
        return (nj_s8)(*(double *)p);
    }
    if (f->ptype == NJ_PT_LONG) {
        return *(nj_s8 *)p;
    }
    return (nj_s8)nj_field_read_int(vm, obj, f);
}

void nj_field_write_long(nj_vm *vm, nj_ref obj, nj_field *f, nj_s8 v)
{
    nj_u1 *p = field_addr(vm, obj, f);
    if (!p) {
        return;
    }
    if (f->ptype == NJ_PT_DOUBLE) {
        *(double *)p = (double)v;
    } else if (f->ptype == NJ_PT_LONG) {
        *(nj_s8 *)p = v;
    } else {
        nj_field_write_int(vm, obj, f, (nj_s4)v);
    }
}

nj_ref nj_field_read_ref(nj_vm *vm, nj_ref obj, nj_field *f)
{
    nj_u1 *p = field_addr(vm, obj, f);
    nj_ref r;

    if (!p) {
        return NULL;
    }
    memcpy(&r, p, sizeof(r));
    return r;
}

void nj_field_write_ref(nj_vm *vm, nj_ref obj, nj_field *f, nj_ref v)
{
    nj_u1 *p = field_addr(vm, obj, f);
    if (!p) {
        return;
    }
    memcpy(p, &v, sizeof(v));
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
        nj_value args[1];
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
    }
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

static nj_status vm_invoke(nj_vm *vm, nj_ref obj, const char *cls,
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
    if (!vm->running) {
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

    st = nj_exec(vm, th, saved_frames);

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
nj_ref nj_vm_new_midlet(nj_vm *vm, const char *class_name, nj_ref *out_exc)
{
    nj_class *c;
    nj_ref o;

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
    if (nj_class_init(vm, c) != NJ_OK) {
        if (out_exc) {
            *out_exc = nj_take_exception(vm);
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
    if (nj_call_special(vm, o, c->name, "<init>", "()V", NULL, NULL) != NJ_OK) {
        if (out_exc) {
            *out_exc = nj_take_exception(vm);
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
