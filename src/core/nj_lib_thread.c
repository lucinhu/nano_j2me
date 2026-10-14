/*
 * nano_j2me - nj_lib_thread.c
 *
 * java.lang.Thread and java.lang.Runnable on top of the cooperative scheduler.
 *
 * A Java Thread maps one-to-one onto an nj_thread (its own Java stack) but
 * *not* onto an OS thread: the interpreter switches between them at quantum
 * boundaries.  See nj_thread.c for why.
 *
 * The association is kept in the `nativeId` field so that
 * Thread.currentThread() is a field read rather than a search.
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))
#define NJ_CALL(name, vm, th, args, ret) name((vm), (th), (args), (ret))

static nj_field *fld(nj_vm *vm, const char *name, const char *desc)
{
    return nj_field_lookup(vm, "java/lang/Thread", name, desc, 0);
}

static void thread_set_handle(nj_vm *vm, nj_ref o, nj_thread *t)
{
    nj_field *f = fld(vm, "nativeId", "I");
    if (f) {
        nj_field_write_int(vm, o, f, t ? (nj_s4)t->id : 0);
    }
    f = fld(vm, "alive", "Z");
    if (f) {
        nj_field_write_int(vm, o, f, t ? 1 : 0);
    }
}

NJFN(thread_init)
{
    nj_ref o = NJ_THIS(args);
    nj_field *f;

    f = fld(vm, "name", "Ljava/lang/String;");
    if (f) {
        nj_field_write_ref(vm, o, f, nj_string_new(vm, "thread"));
    }
    f = fld(vm, "priority", "I");
    if (f) {
        nj_field_write_int(vm, o, f, 5);
    }
    f = fld(vm, "alive", "Z");
    if (f) {
        nj_field_write_int(vm, o, f, 0);
    }
    f = fld(vm, "nativeId", "I");
    if (f) {
        nj_field_write_int(vm, o, f, 0);
    }
    f = fld(vm, "interrupted", "Z");
    if (f) {
        nj_field_write_int(vm, o, f, 0);
    }
    return NJ_OK;
}

NJFN(thread_init_runnable)
{
    nj_field *f;
    nj_status st = NJ_CALL(thread_init, vm, th, args, ret);
    if (st != NJ_OK) {
        return st;
    }
    f = fld(vm, "target", "Ljava/lang/Runnable;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(thread_init_runnable_name)
{
    nj_field *f;
    nj_status st = NJ_CALL(thread_init_runnable, vm, th, args, ret);
    if (st != NJ_OK) {
        return st;
    }
    f = fld(vm, "name", "Ljava/lang/String;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 1));
    }
    return NJ_OK;
}

NJFN(thread_init_name)
{
    nj_field *f;
    nj_status st = NJ_CALL(thread_init, vm, th, args, ret);
    if (st != NJ_OK) {
        return st;
    }
    f = fld(vm, "name", "Ljava/lang/String;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

/* protected void checkAccess(): there is no SecurityManager in this runtime,
 * so nothing can ever be denied and the check has nothing to do. */
NJFN(thread_checkAccess)
{
    NJ_UNUSED(vm);
    NJ_UNUSED(th);
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

NJFN(thread_currentThread)
{
    nj_thread *t = vm->cur;
    if (!t) {
        ret->r = NULL;
        return NJ_OK;
    }
    if (!t->obj) {
        /* The primordial thread has no Java mirror yet: make one lazily. */
        nj_ref o = nj_object_new(vm, "java/lang/Thread");
        if (!o) {
            return NJ_ERR;
        }
        t->obj = o;
        thread_set_handle(vm, o, t);
    }
    ret->r = t->obj;
    return NJ_OK;
}

NJFN(thread_start)
{
    nj_ref o = NJ_THIS(args);
    nj_thread *t;
    nj_method *run;
    nj_field *f;
    nj_ref target;

    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (vm->nthreads >= (nj_u2)NJ_MAX_THREADS) {
        return nj_throw(vm, "java/lang/OutOfMemoryError", "too many threads");
    }
    t = nj_thread_create(vm, o, 0);
    if (!t) {
        return nj_throw(vm, "java/lang/OutOfMemoryError", "thread");
    }
    t->state = NJ_THREAD_NEW;

    f = fld(vm, "target", "Ljava/lang/Runnable;");
    target = f ? nj_field_read_ref(vm, o, f) : NULL;
    t->target = target;
    f = fld(vm, "name", "Ljava/lang/String;");
    t->name = f ? nj_field_read_ref(vm, o, f) : NULL;
    f = fld(vm, "priority", "I");
    t->priority = f ? nj_field_read_int(vm, o, f) : 5;

    /*
     * The thread's BOTTOM frame must be Java.
     *
     * A frame for a native method cannot be executed by the interpreter - it
     * has no bytecode - and a native cannot be suspended and resumed either
     * (docs/ARCHITECTURE.md section 5).  Starting the thread on the base
     * Thread.run() native would therefore do one of two wrong things: kill the
     * thread immediately, or run the whole game loop through a C frame that
     * cannot survive a Thread.sleep().
     *
     * So resolve the real Java run() here instead: the receiver's own override
     * if it has one, otherwise the Runnable's.  Thread.run()'s delegation to
     * target.run() is inlined at start time, which is what a real JVM ends up
     * doing anyway, only without the C frame in the middle.
     */
    run = nj_method_lookup(o->cls, "run", "()V");
    if (run && run->code == NULL) {
        nj_ref target = t->target;
        if (target) {
            nj_method *rm = nj_method_lookup(target->cls, "run", "()V");
            if (rm) {
                run = rm;
                t->entry_this = target;
            }
        }
    }
    if (!run) {
        run = nj_method_lookup(nj_class_lookup(vm, "java/lang/Thread"),
                               "run", "()V");
    }
    if (!run) {
        nj_thread_destroy(vm, t);
        return nj_throw(vm, "java/lang/NoSuchMethodError", "run");
    }
    thread_set_handle(vm, o, t);

    if (run->code == NULL) {
        /*
         * Still native - the Runnable itself has no Java run(), or the game
         * subclassed Thread and left run() unimplemented.  Run it here and let
         * the thread finish; there is nothing to resume.
         */
        nj_value ret[2];
        nj_status st;
        if (run->max_locals < 1u) {
            nj_thread_destroy(vm, t);
            return nj_throw(vm, "java/lang/VerifyError", run->name);
        }
        t->stack_mem[0].r = t->entry_this ? t->entry_this : t->obj;
        st = nj_invoke_native(vm, t, run, t->stack_mem, 1u, ret);
        t->started = 1u;
        if (st != NJ_ERR_TIMEOUT) {
            t->state = NJ_THREAD_DEAD;
        }
        return NJ_OK;
    }

    {
        nj_status st = nj_thread_start(vm, t, run);
        if (st != NJ_OK) {
            nj_thread_destroy(vm, t);
            return st;
        }
    }
    t->state = NJ_THREAD_RUNNING;
    return NJ_OK;
}

NJFN(thread_run)
{
    /* The base implementation delegates to the Runnable, if any. */
    nj_ref o = NJ_THIS(args);
    nj_field *f = fld(vm, "target", "Ljava/lang/Runnable;");
    nj_ref target = f ? nj_field_read_ref(vm, o, f) : NULL;

    if (!target) {
        return NJ_OK;
    }
    return nj_call(vm, target, "java/lang/Runnable", "run", "()V", NULL, NULL);
}

NJFN(thread_sleep)
{
    nj_s8 ms = NJ_ARG_LONG(args, 0);
    if (ms < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "timeout value is negative");
    }
    return nj_thread_sleep(vm, th, ms);
}

NJFN(thread_yield)
{
    /* Cooperate: the scheduler picks the next runnable thread at the next
     * quantum boundary, which is at most NJ_SCHED_QUANTUM bytecodes away. */
    NJ_UNUSED(args);
    return NJ_OK;
}

NJFN(thread_isAlive)
{
    nj_field *f = fld(vm, "alive", "Z");
    ret->i = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

NJFN(thread_join)
{
    nj_ref o = NJ_THIS(args);
    nj_field *f = fld(vm, "nativeId", "I");
    nj_s4 id = f ? nj_field_read_int(vm, o, f) : 0;
    nj_u2 i;

    if (id == 0) {
        return NJ_OK;   /* never started: nothing to wait for */
    }
    for (i = 0u; i < vm->nthreads; i++) {
        nj_thread *t = vm->threads[i];
        if (t && t->id == (nj_u4)id && t->state != NJ_THREAD_DEAD) {
            /* Park the caller until that thread dies; the scheduler re-checks
             * this condition every tick. */
            th->wait_monitor = o;
            th->wait_deadline = 0;
            th->state = NJ_THREAD_WAITING;
            return NJ_ERR_TIMEOUT;
        }
    }
    return NJ_OK;
}

NJFN(thread_getName)
{
    nj_field *f = fld(vm, "name", "Ljava/lang/String;");
    ret->r = f ? nj_field_read_ref(vm, NJ_THIS(args), f) : NULL;
    return NJ_OK;
}

NJFN(thread_setName)
{
    nj_field *f = fld(vm, "name", "Ljava/lang/String;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(thread_getPriority)
{
    nj_field *f = fld(vm, "priority", "I");
    ret->i = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 5;
    return NJ_OK;
}

NJFN(thread_setPriority)
{
    nj_field *f = fld(vm, "priority", "I");
    nj_s4 p = NJ_ARG_INT(args, 0);
    if (p < 1 || p > 10) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "priority");
    }
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, p);
    }
    return NJ_OK;
}

NJFN(thread_interrupt)
{
    nj_field *f = fld(vm, "interrupted", "Z");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, 1);
    }
    return NJ_OK;
}

NJFN(thread_activeCount)
{
    ret->i = (nj_s4)vm->nthreads;
    return NJ_OK;
}

NJFN(thread_toString)
{
    nj_field *f = fld(vm, "name", "Ljava/lang/String;");
    nj_ref n = f ? nj_field_read_ref(vm, NJ_THIS(args), f) : NULL;
    nj_ref out;
    nj_u4 mark = nj_root_mark(vm);
    nj_root(vm, n);
    out = nj_string_concat(vm, nj_string_new(vm, "Thread["),
                           n ? n : nj_string_new(vm, "null"));
    out = nj_string_concat(vm, out, nj_string_new(vm, "]"));
    nj_root_release(vm, mark);
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_field thread_fields[] = {
    { "target",      "Ljava/lang/Runnable;", 0 },
    { "name",        "Ljava/lang/String;",   0 },
    { "priority",    "I",                    0 },
    { "alive",       "Z",                    0 },
    { "interrupted", "Z",                    0 },
    { "nativeId",    "I",                    0 }
};

static const nj_builtin_method thread_methods[] = {
    M("<init>",               "()V",                                        0),
    M("<init>",               "(Ljava/lang/Runnable;)V",                    0),
    M("<init>",               "(Ljava/lang/Runnable;Ljava/lang/String;)V",  0),
    M("<init>",               "(Ljava/lang/String;)V",                       0),
    M("currentThread",        "()Ljava/lang/Thread;",                       NJ_ACC_STATIC),
    M("sleep",                "(J)V",                                       NJ_ACC_STATIC),
    M("yield",                "()V",                                        NJ_ACC_STATIC),
    M("activeCount",          "()I",                                        NJ_ACC_STATIC),
    M("start",                "()V",                                        0),
    M("run",                  "()V",                                        0),
    M("isAlive",              "()Z",                                        0),
    M("join",                 "()V",                                        0),
    M("getName",              "()Ljava/lang/String;",                       0),
    M("setName",              "(Ljava/lang/String;)V",                      0),
    M("getPriority",          "()I",                                        0),
    M("setPriority",          "(I)V",                                       0),
    M("interrupt",            "()V",                                        0),
    M("toString",             "()Ljava/lang/String;",                       0),
    M("checkAccess",          "()V",                                        NJ_ACC_PROTECTED)
};

static const nj_builtin_method runnable_methods[] = {
    M("run", "()V", NJ_ACC_ABSTRACT | NJ_ACC_PUBLIC)
};

const nj_builtin_class nj_lib_thread_classes[] = {
    { "java/lang/Thread", "java/lang/Object", NJ_ACC_PUBLIC,
      thread_fields,
      (nj_u2)(sizeof(thread_fields) / sizeof(thread_fields[0])),
      thread_methods,
      (nj_u2)(sizeof(thread_methods) / sizeof(thread_methods[0])) },
    { "java/lang/Runnable", NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, runnable_methods,
      (nj_u2)(sizeof(runnable_methods) / sizeof(runnable_methods[0])) }
};
const int nj_lib_thread_class_count = 2;

const nj_native_entry nj_lib_thread_table[] = {
    { "java/lang/Thread", "<init>",                 "()V",                        thread_init },
    { "java/lang/Thread", "<init>",                 "(Ljava/lang/Runnable;)V",    thread_init_runnable },
    { "java/lang/Thread", "<init>",                 "(Ljava/lang/Runnable;Ljava/lang/String;)V", thread_init_runnable_name },
    { "java/lang/Thread", "<init>",                 "(Ljava/lang/String;)V",      thread_init_name },
    { "java/lang/Thread", "currentThread",          "()Ljava/lang/Thread;",       thread_currentThread },
    { "java/lang/Thread", "sleep",                  "(J)V",                       thread_sleep },
    { "java/lang/Thread", "yield",                  "()V",                        thread_yield },
    { "java/lang/Thread", "activeCount",            "()I",                        thread_activeCount },
    { "java/lang/Thread", "start",                  "()V",                        thread_start },
    { "java/lang/Thread", "run",                    "()V",                        thread_run },
    { "java/lang/Thread", "isAlive",                "()Z",                        thread_isAlive },
    { "java/lang/Thread", "join",                   "()V",                        thread_join },
    { "java/lang/Thread", "getName",                "()Ljava/lang/String;",       thread_getName },
    { "java/lang/Thread", "setName",                "(Ljava/lang/String;)V",      thread_setName },
    { "java/lang/Thread", "getPriority",            "()I",                        thread_getPriority },
    { "java/lang/Thread", "setPriority",            "(I)V",                       thread_setPriority },
    { "java/lang/Thread", "interrupt",              "()V",                        thread_interrupt },
    { "java/lang/Thread", "toString",               "()Ljava/lang/String;",       thread_toString },
    { "java/lang/Thread", "checkAccess",            "()V",                        thread_checkAccess }
};
const int nj_lib_thread_count =
    (int)(sizeof(nj_lib_thread_table) / sizeof(nj_lib_thread_table[0]));
