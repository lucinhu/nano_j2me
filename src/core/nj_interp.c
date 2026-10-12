/*
 * nano_j2me - nj_interp.c
 *
 * The bytecode interpreter: the full CLDC 1.1 instruction set.
 *
 * ---------------------------------------------------------------------------
 * VALUE REPRESENTATION
 * One operand-stack slot is one nj_value (8 bytes).  long and double still
 * consume TWO slots, as the JVM requires, but the 8-byte payload lives in the
 * LOWER slot and the upper slot is always written as zero.  Two reasons:
 *
 *   1. dup2 / dup2_x1 / dup2_x2 / pop2 work unchanged on raw slots,
 *   2. the garbage collector walks untyped slots looking for references, so a
 *      "don't care" upper half could look like a pointer.  Zeroing it (plus
 *      the arena range check in nj_heap.c) makes that impossible.
 *
 * ---------------------------------------------------------------------------
 * EXCEPTIONS
 * Every instruction that can throw jumps to `do_unwind`, which saves the
 * current frame, searches the handler tables of the frames above the call
 * barrier, and either resumes at a handler (with the exception pushed onto a
 * cleared operand stack) or reports that the thread died.  A native signals an
 * exception by returning non-NJ_OK with vm->exception set, and blocks by
 * returning NJ_ERR_TIMEOUT.
 *
 * ---------------------------------------------------------------------------
 * NO FILE-SCOPE STATE
 * The interpreter keeps its pc/code/locals/sp in C locals; the U1/S2/S4 macros
 * below are textual and expand inside nj_exec only.  That is why the runtime
 * can host several VMs in one process.
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <math.h>
#endif

/* ------------------------------------------------------------------ */
/* Exception unwinding                                                 */
/* ------------------------------------------------------------------ */
/*
 * Search for a handler, starting at the innermost frame and stopping above
 * `stop_depth` (the C-call barrier).  On success the frame is positioned at the
 * handler with the exception pushed; returns 1.  Returns 0 when the exception
 * escapes every frame in the window.
 */
static int interp_unwind(nj_vm *vm, nj_thread *th, nj_u4 throw_pc,
                         int stop_depth, nj_ref exc)
{
    while (th->nframes > stop_depth) {
        nj_frame *f = &th->frames[th->nframes - 1];
        nj_method *mm = f->m;

        if (mm->code && mm->handlers && mm->handler_count > 0u) {
            nj_u2 i;
            for (i = 0u; i < mm->handler_count; i++) {
                const nj_handler *h = &mm->handlers[i];
                int match = 0;

                if (throw_pc < h->start_pc || throw_pc >= h->end_pc) {
                    continue;
                }
                if (h->catch_cp == 0u) {
                    match = 1;                     /* finally / catch-all */
                } else {
                    const char *cn = nj_cp_class_name(mm->owner, h->catch_cp);
                    if (cn) {
                        nj_class *hc = nj_class_resolve(vm, cn);
                        if (hc && exc && exc->cls &&
                            nj_class_is_assignable(exc->cls, hc)) {
                            match = 1;
                        }
                    }
                }
                if (match) {
                    f->pc = h->handler_pc;
                    f->stack[0].r = exc;
                    f->sp = f->stack + 1;
                    f->is_native = 0u;
                    th->nframes = (int)((f - th->frames) + 1);
                    th->top = f->stack + mm->max_stack;
                    /*
                     * The exception is now handled: clear the VM's pending
                     * slot, or the next failure would be masked by this one
                     * (nj_throw keeps the first exception it sees).
                     */
                    vm->exception = NULL;
                    vm->exception_method = NULL;
                    vm->exception_pc = 0u;
                    return 1;
                }
            }
        }
        /* No handler in this frame: drop it and look in the caller. */
        th->nframes--;
        th->top = f->locals;
        throw_pc = (th->nframes > stop_depth &&
                    th->frames[th->nframes - 1].pc > 0u)
                       ? th->frames[th->nframes - 1].pc - 1u
                       : 0u;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Native invocation                                                   */
/* ------------------------------------------------------------------ */
/*
 * Run a native method.  Shared by the interpreter's invoke opcodes and by the
 * C-to-Java bridge, so a native reached from either side behaves identically:
 * one frame is pushed (which is what makes the argument window and unwinding
 * uniform), the C function runs, and the frame is popped again.
 *
 * On return, `ret[0]` (and `ret[1]` for a wide return) holds the result.
 * NJ_ERR_TIMEOUT means the native blocked and already set the thread state.
 */
nj_status nj_invoke_native(nj_vm *vm, nj_thread *th, nj_method *target,
                           nj_value *argbase, nj_u2 total, nj_value *ret)
{
    nj_native_fn fn;
    nj_value *args;
    nj_status st;

    vm->native_calls++;

    /*
     * Clear BOTH slots completely.  `ret[0].bits = 0` only writes four of the
     * eight bytes of an nj_value, so a native that returns without setting
     * `ret` (every void method) would hand the caller an nj_value whose upper
     * half is whatever was on the stack - which is a wild pointer the moment
     * anything reads it as a reference.
     */
    memset(ret, 0, 2u * sizeof(nj_value));

    st = nj_frame_push(vm, th, target, argbase, total);
    if (st != NJ_OK) {
        return st;
    }
    fn = target->native;
    if (!fn) {
        fn = nj_native_find(vm, target->owner->name, target->name,
                            target->desc);
        if (fn) {
            target->native = fn;   /* bind once */
        }
    }
    if (!fn) {
        nj_frame_pop(vm, th);
        (void)nj_throw(vm, "java/lang/UnsatisfiedLinkError", target->name);
        return NJ_ERR;
    }

    args = argbase + ((target->flags & NJ_ACC_STATIC) ? 0u : 1u);
    vm->native_depth++;
    st = fn(vm, th, args, ret);
    vm->native_depth--;
    nj_frame_pop(vm, th);

    if (st != NJ_OK && st != NJ_ERR_TIMEOUT && !vm->exception) {
        (void)nj_throw(vm, "java/lang/RuntimeException", target->name);
    }
    return st;
}

/* ------------------------------------------------------------------ */
/* Invocation                                                          */
/* ------------------------------------------------------------------ */
/*
 * Push a frame for `target` whose arguments start at sp-total, then either run
 * it (native) or leave it as the current frame (Java).
 *
 *   *pushed_java == 1 -> a new Java frame is current; the caller must reload
 *   *pushed_java == 0 -> the call completed; *sp_io is the new operand top
 *   NJ_ERR_TIMEOUT    -> the callee blocked; the thread state is already set
 */
static nj_status interp_invoke(nj_vm *vm, nj_thread *th, nj_method *target,
                               nj_u2 total, nj_value **sp_io, int *pushed_java)
{
    nj_value *argbase;
    nj_status st;

    *pushed_java = 0;
    argbase = *sp_io - total;

    if (target->code != NULL) {
        /* Java callee: push its frame and let the interpreter run it. */
        st = nj_frame_push(vm, th, target, argbase, total);
        if (st != NJ_OK) {
            return st;   /* exception already raised */
        }
        *pushed_java = 1;
        return NJ_OK;
    }

    /*
     * Native callee: nj_invoke_native() pushes and pops the frame itself, so
     * nothing may be pushed here - doing both would leak a frame per native
     * call and shift every subsequent frame slot.
     */
    {
        nj_value ret[2];
        int wide = (target->ret_ptype == NJ_PT_LONG ||
                    target->ret_ptype == NJ_PT_DOUBLE);

        st = nj_invoke_native(vm, th, target, argbase, total, ret);
        *sp_io = argbase;

        if (st == NJ_ERR_TIMEOUT) {
            /* Blocking native: deliver the result and suspend the thread. */
            if (target->ret_ptype != NJ_PT_VOID) {
                *(*sp_io) = ret[0];
                (*sp_io)++;
                if (wide) {
                    (*sp_io)->bits = 0u;
                    (*sp_io)++;
                }
            }
            return NJ_ERR_TIMEOUT;
        }
        if (st != NJ_OK) {
            return NJ_ERR;
        }

        switch (target->ret_ptype) {
        case NJ_PT_VOID:
            break;
        case NJ_PT_LONG:
        case NJ_PT_DOUBLE:
            *(*sp_io) = ret[0];
            (*sp_io)++;
            (*sp_io)->bits = 0u;
            (*sp_io)++;
            break;
        default:
            *(*sp_io) = ret[0];
            (*sp_io)++;
            break;
        }
        return NJ_OK;
    }
}

/* ------------------------------------------------------------------ */
/* multianewarray                                                      */
/* ------------------------------------------------------------------ */
static nj_ref interp_multianew(nj_vm *vm, nj_class *arr_cls,
                               const nj_s4 *lens, int dims, int level)
{
    nj_u4 mark;
    nj_ref a;
    nj_u4 i;

    if (level >= dims || !arr_cls) {
        return NULL;
    }
    a = nj_new_array(vm, arr_cls, (nj_u4)lens[level],
                     arr_cls->array_elem ? (int)arr_cls->array_elem
                                         : NJ_ARRAY_T_REF);
    if (!a) {
        (void)nj_throw_oom(vm);
        return NULL;
    }
    if (level + 1 < dims && arr_cls->component_cls) {
        mark = nj_root_mark(vm);
        nj_root(vm, a);
        for (i = 0u; i < a->length; i++) {
            nj_ref child = interp_multianew(vm, arr_cls->component_cls,
                                            lens, dims, level + 1);
            if (!child) {
                nj_root_release(vm, mark);
                return NULL;
            }
            a->u.refs[i] = child;
        }
        nj_root_release(vm, mark);
    }
    return a;
}

/* ------------------------------------------------------------------ */
/* The main loop                                                       */
/* ------------------------------------------------------------------ */
/*
 * Is `c` one of this VM's classes?
 *
 * The operand stack is untyped and there is no verifier, so anything can be
 * sitting in a slot that is supposed to hold a receiver.  See the invoke
 * handler for why the arena range check alone is not enough.
 */
static int class_is_registered(nj_vm *vm, const nj_class *c)
{
    nj_u4 i;

    if (!c) {
        return 0;
    }
    for (i = 0u; i < vm->nclasses; i++) {
        if (vm->classes[i] == c) {
            return 1;
        }
    }
    return 0;
}

nj_status nj_exec(nj_vm *vm, nj_thread *th, int stop_depth)
{
    nj_frame *fr;
    nj_method *m;
    const nj_u1 *code;
    nj_u4 code_len;
    nj_value *locals;
    nj_value *stack;
    nj_value *sp;
    nj_u4 pc;
    nj_u4 quantum = (nj_u4)NJ_SCHED_QUANTUM;

    if (!vm || !th) {
        return NJ_ERR;
    }
    if (th->nframes <= stop_depth) {
        return NJ_OK;
    }

    fr = &th->frames[th->nframes - 1];
    m = fr->m;
    code = m->code;
    code_len = m->code_len;
    locals = fr->locals;
    stack = fr->stack;
    sp = fr->sp;
    pc = fr->pc;

    if (!code) {
        return NJ_ERR_HALT;   /* a native frame never enters the interpreter */
    }

/* ---- operand decoding (textual: these expand inside nj_exec only) ---- */
#define U1()  (code[pc++])
#define S1()  ((nj_s1)code[pc++])
#define U2()  (pc += 2u, (nj_u2)(((nj_u2)code[pc - 2u] << 8) | \
                                 (nj_u2)code[pc - 1u]))
#define S2()  ((nj_s2)U2())
#define S4()  (pc += 4u, (nj_s4)(((nj_u4)code[pc - 4u] << 24) |   \
                                 ((nj_u4)code[pc - 3u] << 16) |   \
                                 ((nj_u4)code[pc - 2u] << 8)  |   \
                                 (nj_u4)code[pc - 1u]))
/*
 * Branch offsets in a class file are relative to the address of the BRANCH
 * OPCODE, not to the pc after its operands.  All relative jumps therefore go
 * through op_pc, which the dispatch loop records before fetching the opcode.
 */
#define JMP(o)      do { pc = (nj_u4)(nj_s4)((nj_s4)op_pc + (nj_s2)(o)); } while (0)
#define JUMP_ABS(a) do { pc = (nj_u4)(a); } while (0)

#define SAVE() do { fr->pc = pc; fr->sp = sp; } while (0)
#define RELOAD() do {                                                  \
        fr = &th->frames[th->nframes - 1];                             \
        m = fr->m;                                                     \
        code = m->code;                                                \
        code_len = m->code_len;                                        \
        locals = fr->locals;                                           \
        stack = fr->stack;                                             \
        sp = fr->sp;                                                   \
        pc = fr->pc;                                                   \
    } while (0)

#define THROW(cls, msg) do {                                           \
        if (!vm->exception) { (void)nj_throw(vm, (cls), (msg)); }      \
        goto do_unwind;                                                \
    } while (0)

#define PUSH_W(w) do { sp->j = (w); (sp + 1)->bits = 0u; sp += 2; } while (0)

#define CHECK_QUANTUM() do {                                           \
        if (--quantum == 0u) {                                         \
            SAVE();                                                    \
            return NJ_ERR_YIELD;                                       \
        }                                                              \
    } while (0)

    /*
     * REPLICATED DISPATCH, converted one opcode at a time.
     *
     * A converted handler ends with DISPATCH(), which fetches the next opcode
     * and branches through nj_dispatch[] itself, so the indirect branch is
     * PREDICTED PER HANDLER rather than at one shared site.  That is the whole
     * point of the technique; a shared `goto` after the switch would be one
     * branch however many times the source repeats it.
     *
     * A handler that has NOT been converted has no L_ label, its entry in the
     * table points at L_slow, and it runs through the switch exactly as before.
     * The two kinds coexist, which is what makes this incremental.  After
     * every conversion the benchmark must still print
     *
     *     BENCH sum=-136042548 s=5
     *
     * which is the only instrument left that can tell a correct conversion
     * from a subtle one.  It used to be the bytecode count, and that stopped
     * working when bipush+iand became a superinstruction: a fused pair runs
     * two instructions and counts one, so the total is 105717067 by design.
     *
     * The invoke family (0xb6-0xb9) sits behind one label because it always
     * shared one case body - which is exactly why it was the last thing
     * converted.  Interleaved A/B against the unconverted build measured it
     * NEUTRAL, not faster: 40 samples on the synthetic benchmark gave
     * min 0.179 vs 0.180 s and identical medians and means, and 300 frames of
     * vnes gave 0.112 vs 0.112 s.  It is kept because it completes the
     * conversion without costing anything, NOT because a speedup was
     * demonstrated - there is none to show on either instrument.
     */
    for (;;) {
        nj_u1 op;
        nj_u4 op_pc;

    /*
     * The fetch is written ONCE and used by both paths: the shared one at
     * L_slow for opcodes that have not been converted, and DISPATCH() for the
     * ones that have.  Two copies of this would be two chances to forget a
     * counter, and the benchmark's exact bytecode total is what catches that.
     */
    /*
     * Computed goto is a GCC/Clang extension, and this project builds for
     * ESP32 (xtensa and riscv), Cortex-M and three Android ABIs as well as the
     * host.  So the threaded dispatch is OPT-IN per compiler and the portable
     * switch remains the fallback: with NJ_THREADED_DISPATCH 0, DISPATCH() is
     * just `continue`, the table is not referenced, and the interpreter is
     * exactly what it was.  A port to a compiler without the extension loses
     * speed, not correctness.
     *
     * static const, so the table is placed in .data.rel.ro and read-only after
     * relocation - the same rule every other table here follows.
     */
#if NJ_THREADED_DISPATCH
    __extension__ static const void *const nj_dispatch[256] = {
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x03,
        &&L_0x03,
        &&L_0x03,
        &&L_0x03,
        &&L_0x03,
        &&L_0x03,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x10,
        &&L_0x11,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x15,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x1a,
        &&L_0x1a,
        &&L_0x1a,
        &&L_0x1a,
        &&L_0x1e,
        &&L_0x1e,
        &&L_0x1e,
        &&L_0x1e,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x3b,
        &&L_0x3b,
        &&L_0x3b,
        &&L_0x3b,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x60,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x68,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x7e,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x82,
        &&L_slow,
        &&L_0x84,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x94,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0x9f,
        &&L_0x9f,
        &&L_0x9f,
        &&L_0x9f,
        &&L_0x9f,
        &&L_0x9f,
        &&L_slow,
        &&L_slow,
        &&L_0xa7,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0xac,
        &&L_slow,
        &&L_0xac,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_0xb2,
        &&L_slow,
        &&L_0xb2,
        &&L_slow,
        &&L_invoke,
        &&L_invoke,
        &&L_invoke,
        &&L_invoke,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
        &&L_slow,
    };
    (void)nj_dispatch;      /* referenced once any opcode is converted */
#endif

#define NJ_FETCH_OP() do {                                             \
        if (pc >= code_len) {                                          \
            THROW("java/lang/VerifyError",                            \
                  "code runs past the end of the method");            \
        }                                                              \
        /*
         * The operand stack is bounded by THIS FRAME's max_stack, not just by
         * the end of the thread stack.  A method that pushes past its own
         * max_stack used to walk into the next frame's slots: bounded only by
         * stack_end, a frame in the middle of the thread stack could overwrite
         * its neighbour, and with a corrupted class file that is a pointer read
         * out of class bytes and dereferenced - the fuzzer produced
         * obj=0x4e4a484800010000, which is the ASCII "NJHH".
         *
         * The check sits next to the pc bound that was already here, so it is
         * one more compare on a path that already had one.  Without a verifier
         * this is what stands between a malformed JAR and memory corruption,
         * and a loud VerifyError is the outcome this project wants.
         */                                                             \
        if (sp > fr->stack_limit) {                                    \
            THROW("java/lang/VerifyError",                            \
                  "operand stack overflow");                           \
        }                                                              \
        _Pragma("GCC diagnostic push")                                 \
        _Pragma("GCC diagnostic ignored \"-Wpedantic\"")                \
        op_pc = pc;                                                    \
        op = code[pc++];                                               \
        _Pragma("GCC diagnostic pop")                                  \
        (void)op_pc;                                                   \
        vm->insns++;                                                   \
    } while (0)

    /*
     * A converted handler's exit.  It does everything the loop top did - the
     * quantum check, the bounds check, the counter - and then branches through
     * the table from ITS OWN site, which is the point of the exercise.
     */
#if NJ_THREADED_DISPATCH
#define DISPATCH() do {                                                \
        CHECK_QUANTUM();                                               \
        NJ_FETCH_OP();                                                 \
        _Pragma("GCC diagnostic push")                                 \
        _Pragma("GCC diagnostic ignored \"-Wpedantic\"")                \
        goto *nj_dispatch[op];                                         \
        _Pragma("GCC diagnostic pop")                                  \
    } while (0)
#else
/* Without the extension, a converted handler simply returns to the loop top. */
#define DISPATCH() continue
#endif

    L_slow:
        NJ_FETCH_OP();
#if NJ_ENABLE_DEBUG
        if (vm->trace) {
            nj_trace_step(vm, m, pc, (nj_u4)(sp - stack), locals, stack, sp);
        }
#endif

        switch (op) {

        /* ================= constants ================= */
        case 0x00: break;                                  /* nop */
        case 0x01: sp->r = NULL; sp++; break;              /* aconst_null */
        case 0x02: sp->i = -1; sp++; break;                /* iconst_m1 */
        L_0x03: case 0x03: case 0x04: case 0x05:
        case 0x06: case 0x07: case 0x08:                   /* iconst_0..5 */
            sp->i = (nj_s4)op - 3; sp++; DISPATCH();
        case 0x09: PUSH_W(0); break;                       /* lconst_0 */
        case 0x0a: PUSH_W(1); break;                       /* lconst_1 */
        case 0x0b: sp->f = 0.0f; sp++; break;
        case 0x0c: sp->f = 1.0f; sp++; break;
        case 0x0d: sp->f = 2.0f; sp++; break;
        case 0x0e: PUSH_W(0); break;                       /* dconst_0 */
        case 0x0f: PUSH_W(1); break;                       /* dconst_1 */
        /* SUPERINSTRUCTION: push-constant followed by iand.  The benchmark's
         * inner loop does it four times per iteration (sipush 255, sipush 511,
         * bipush 127, ldc 65535 - every one followed by iand), and it is what a
         * mask does in any game: clamp to a power of two minus one.
         *
         * FUSED AT DISPATCH TIME, NOT BY REWRITING THE BYTECODE.  Rewriting
         * needs every branch target checked, because a jump landing on the
         * second half of a pair would run it without the first - a small
         * verifier's worth of work and a real hazard.  Peeking here has neither
         * problem: pc advances normally, a branch onto either instruction still
         * does the right thing, and the fused path's peak stack depth is the
         * same as the unfused one, so stack_limit stays valid. */
        L_0x10: case 0x10: {                               /* bipush [+ iand] */
            nj_s4 v = S1();
            if (pc < code_len && code[pc] == 0x7e) {
                nj_s4 a;
                pc++;
                a = (--sp)->i;
                sp->i = a & v;
                sp++;
                DISPATCH();
            }
            sp->i = v; sp++; DISPATCH(); }
        L_0x11: case 0x11: {                               /* sipush [+ iand] */
            nj_s4 v = S2();
            if (pc < code_len && code[pc] == 0x7e) {
                nj_s4 a;
                pc++;
                a = (--sp)->i;
                sp->i = a & v;
                sp++;
                DISPATCH();
            }
            sp->i = v; sp++; DISPATCH(); }
        case 0x12: case 0x13: {                            /* ldc / ldc_w */
            nj_u2 idx = (op == 0x12) ? (nj_u2)U1() : U2();
            nj_value cst[2];
            nj_ref sr = NULL;
            int k = nj_cp_const(vm, m->owner, idx, 1, cst, &sr);
            if (k < 0) {
                THROW("java/lang/VerifyError", "bad ldc operand");
            }
            if (k == 1) {
                sp->r = sr;
            } else {
                sp->bits = cst[0].bits;   /* int or float: one slot */
            }
            sp++;
            break;
        }
        case 0x14: {                                       /* ldc2_w */
            nj_u2 idx = U2();
            nj_value cst[2];
            nj_ref sr = NULL;
            if (nj_cp_const(vm, m->owner, idx, 0, cst, &sr) < 0) {
                THROW("java/lang/VerifyError", "bad ldc2_w operand");
            }
            sp[0] = cst[0];
            sp[1].bits = 0u;
            sp += 2;
            break;
        }

        /* ================= loads ================= */
        L_0x15: case 0x15: { nj_u2 i = (nj_u2)U1(); sp->bits = locals[i].bits; sp++; DISPATCH(); }
        case 0x16: { nj_u2 i = (nj_u2)U1(); sp[0] = locals[i]; sp[1].bits=0u; sp+=2; break; }
        case 0x17: { nj_u2 i = (nj_u2)U1(); sp->bits = locals[i].bits; sp++; break; }
        case 0x18: { nj_u2 i = (nj_u2)U1(); sp[0] = locals[i]; sp[1].bits=0u; sp+=2; break; }
        case 0x19: { nj_u2 i = (nj_u2)U1(); sp->r = locals[i].r; sp++; break; }
        L_0x1a: case 0x1a: case 0x1b: case 0x1c: case 0x1d:  /* iload_0..3 */
            sp->bits = locals[op - 0x1a].bits; sp++; DISPATCH();
        L_0x1e: case 0x1e: case 0x1f: case 0x20: case 0x21:  /* lload_0..3 */
            sp[0] = locals[op - 0x1e]; sp[1].bits = 0u; sp += 2; DISPATCH();
        case 0x22: case 0x23: case 0x24: case 0x25:        /* fload_0..3 */
            sp->bits = locals[op - 0x22].bits; sp++; break;
        case 0x26: case 0x27: case 0x28: case 0x29:        /* dload_0..3 */
            sp[0] = locals[op - 0x26]; sp[1].bits = 0u; sp += 2; break;
        case 0x2a: case 0x2b: case 0x2c: case 0x2d:        /* aload_0..3 */
            sp->r = locals[op - 0x2a].r; sp++; break;

        /* ================= array loads ================= */
#define ARRAY_HEAD()                                                   \
        nj_s4 idx_ = (--sp)->i;                                        \
        nj_ref arr_ = (--sp)->r;                                       \
        if (!arr_) { THROW("java/lang/NullPointerException", NULL); }  \
        if (arr_->kind != NJ_OBJ_ARRAY || (nj_u4)idx_ >= arr_->length) \
            THROW("java/lang/ArrayIndexOutOfBoundsException", NULL)

        case 0x2e: { ARRAY_HEAD();                        /* iaload */
            sp->i = nj_array_get_int(arr_, (nj_u4)idx_); sp++; break; }
        case 0x2f: { ARRAY_HEAD();                        /* laload */
            PUSH_W(((nj_s8 *)arr_->u.bytes)[idx_]); break; }
        case 0x30: { ARRAY_HEAD();                        /* faload */
            sp->f = ((float *)arr_->u.bytes)[idx_]; sp++; break; }
        case 0x31: { ARRAY_HEAD();                        /* daload */
            { nj_s8 b; memcpy(&b, &((double *)arr_->u.bytes)[idx_], 8u);
              PUSH_W(b); } break; }
        case 0x32: { ARRAY_HEAD();                        /* aaload */
            sp->r = (arr_->aflags == NJ_ARRAY_T_REF) ? arr_->u.refs[idx_] : NULL;
            sp++; break; }
        case 0x33: { ARRAY_HEAD();                        /* baload */
            sp->i = (arr_->aflags == NJ_ARRAY_T_BOOLEAN)
                        ? (((nj_u1 *)arr_->u.bytes)[idx_] ? 1 : 0)
                        : (nj_s4)(((nj_s1 *)arr_->u.bytes)[idx_]);
            sp++; break; }
        case 0x34: { ARRAY_HEAD();                        /* caload */
            sp->i = (nj_s4)((nj_u2 *)arr_->u.bytes)[idx_]; sp++; break; }
        case 0x35: { ARRAY_HEAD();                        /* saload */
            sp->i = (nj_s4)((nj_s2 *)arr_->u.bytes)[idx_]; sp++; break; }

        /* ================= stores ================= */
        case 0x36: { nj_u2 i = (nj_u2)U1(); locals[i].bits = (--sp)->bits; break; }
        case 0x37: { nj_u2 i = (nj_u2)U1(); sp -= 2; locals[i] = sp[0];
                     locals[i + 1u].bits = 0u; break; }
        case 0x38: { nj_u2 i = (nj_u2)U1(); locals[i].bits = (--sp)->bits; break; }
        case 0x39: { nj_u2 i = (nj_u2)U1(); sp -= 2; locals[i] = sp[0];
                     locals[i + 1u].bits = 0u; break; }
        case 0x3a: { nj_u2 i = (nj_u2)U1(); locals[i].r = (--sp)->r; break; }
        L_0x3b: case 0x3b: case 0x3c: case 0x3d: case 0x3e:  /* istore_0..3 */
            locals[op - 0x3b].bits = (--sp)->bits; DISPATCH();
        case 0x3f: case 0x40: case 0x41: case 0x42: {      /* lstore_0..3 */
            nj_u2 i = (nj_u2)(op - 0x3f);
            sp -= 2; locals[i] = sp[0]; locals[i + 1u].bits = 0u; break; }
        case 0x43: case 0x44: case 0x45: case 0x46:        /* fstore_0..3 */
            locals[op - 0x43].bits = (--sp)->bits; break;
        case 0x47: case 0x48: case 0x49: case 0x4a: {      /* dstore_0..3 */
            nj_u2 i = (nj_u2)(op - 0x47);
            sp -= 2; locals[i] = sp[0]; locals[i + 1u].bits = 0u; break; }
        case 0x4b: case 0x4c: case 0x4d: case 0x4e:        /* astore_0..3 */
            locals[op - 0x4b].r = (--sp)->r; break;

        /* ================= array stores ================= */
        case 0x4f: case 0x51: case 0x54: case 0x55: case 0x56: {
            nj_s4 v_ = (--sp)->i;
            nj_s4 i_ = (--sp)->i;
            nj_ref a_ = (--sp)->r;
            if (!a_ || !nj_heap_owns(vm, a_)) THROW("java/lang/NullPointerException", NULL);
            if (a_->kind != NJ_OBJ_ARRAY || (nj_u4)i_ >= a_->length)
                THROW("java/lang/ArrayIndexOutOfBoundsException", NULL);
            switch (op) {
            case 0x4f: nj_array_set_int(a_, (nj_u4)i_, v_); break;
            case 0x51: ((float *)a_->u.bytes)[i_] = (float)v_; break;
            case 0x54: ((nj_s1 *)a_->u.bytes)[i_] = (nj_s1)v_; break;
            case 0x55: ((nj_u2 *)a_->u.bytes)[i_] = (nj_u2)(v_ & 0xFFFF); break;
            default:   ((nj_s2 *)a_->u.bytes)[i_] = (nj_s2)v_; break;
            }
            break; }
        case 0x50: {                                       /* lastore */
            nj_s8 v_;
            sp -= 2; v_ = sp[0].j;
            { nj_s4 i_ = (--sp)->i;
              nj_ref a_ = (--sp)->r;
              if (!a_ || !nj_heap_owns(vm, a_)) THROW("java/lang/NullPointerException", NULL);
              if (a_->kind != NJ_OBJ_ARRAY || (nj_u4)i_ >= a_->length)
                  THROW("java/lang/ArrayIndexOutOfBoundsException", NULL);
              ((nj_s8 *)a_->u.bytes)[i_] = v_; }
            break; }
        case 0x52: {                                       /* dastore */
            nj_s8 v_;
            sp -= 2; v_ = sp[0].j;
            { nj_s4 i_ = (--sp)->i;
              nj_ref a_ = (--sp)->r;
              if (!a_ || !nj_heap_owns(vm, a_)) THROW("java/lang/NullPointerException", NULL);
              if (a_->kind != NJ_OBJ_ARRAY || (nj_u4)i_ >= a_->length)
                  THROW("java/lang/ArrayIndexOutOfBoundsException", NULL);
              memcpy(&((double *)a_->u.bytes)[i_], &v_, 8u); }
            break; }
        case 0x53: {                                       /* aastore */
            nj_ref v_ = (--sp)->r;
            nj_s4 i_ = (--sp)->i;
            nj_ref a_ = (--sp)->r;
            if (!a_ || !nj_heap_owns(vm, a_)) THROW("java/lang/NullPointerException", NULL);
            if (a_->kind != NJ_OBJ_ARRAY || a_->aflags != NJ_ARRAY_T_REF ||
                (nj_u4)i_ >= a_->length)
                THROW("java/lang/ArrayIndexOutOfBoundsException", NULL);
            /*
             * The type check has to be able to TRUST v_, and it cannot: the
             * operand stack is untyped and there is no verifier, so a slot that
             * was proved to be a reference at class-load time can hold any int
             * by the time aastore reads it.  Dereferencing it unguarded is a
             * wild read - which is exactly what one of the corpus titles did,
             * with an address of 0x7bdb00000018.
             *
             * A value that is not in the arena cannot be stored into a
             * reference array, so ArrayStoreException is both the safe answer
             * and the correct one.
             */
            if (v_ && !nj_heap_owns(vm, v_)) {
                THROW("java/lang/ArrayStoreException", NULL);
            }
            if (v_ && v_->cls && a_->cls && a_->cls->component_cls &&
                !nj_class_is_assignable(v_->cls, a_->cls->component_cls)) {
                THROW("java/lang/ArrayStoreException", NULL);
            }
            a_->u.refs[i_] = v_;
            break; }

        /* ================= stack manipulation ================= */
        case 0x57: sp--; break;                            /* pop */
        case 0x58: sp -= 2; break;                         /* pop2 */
        case 0x59: sp[0] = sp[-1]; sp++; break;            /* dup */
        case 0x5a: {                                       /* dup_x1 */
            nj_value v2 = sp[-2], v1 = sp[-1];
            sp[-2] = v1; sp[-1] = v2; sp[0] = v1; sp++;
            break; }
        case 0x5b: {                                       /* dup_x2 */
            nj_value v3 = sp[-3], v2 = sp[-2], v1 = sp[-1];
            sp[-3] = v1; sp[-2] = v3; sp[-1] = v2; sp[0] = v1; sp++;
            break; }
        case 0x5c: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; break; /* dup2 */
        case 0x5d: {                                       /* dup2_x1 */
            nj_value v3 = sp[-3], v2 = sp[-2], v1 = sp[-1];
            sp[-3] = v2; sp[-2] = v1; sp[-1] = v3;
            sp[0] = v2; sp[1] = v1; sp += 2;
            break; }
        case 0x5e: {                                       /* dup2_x2 */
            nj_value v4 = sp[-4], v3 = sp[-3], v2 = sp[-2], v1 = sp[-1];
            sp[-4] = v2; sp[-3] = v1; sp[-2] = v4; sp[-1] = v3;
            sp[0] = v2; sp[1] = v1; sp += 2;
            break; }
        case 0x5f: { nj_value t = sp[-1]; sp[-1] = sp[-2]; sp[-2] = t; break; }

        /* ================= arithmetic ================= */
        L_0x60: case 0x60: { nj_s4 b = (--sp)->i, a = (--sp)->i;
                     sp->i = (nj_s4)((nj_u4)a + (nj_u4)b); sp++; DISPATCH(); }
        case 0x61: { nj_s8 b; sp -= 2; b = sp[0].j; sp -= 2;
                     sp[0].j = (nj_s8)((nj_u8)sp[0].j + (nj_u8)b);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x62: { float b = (--sp)->f, a = (--sp)->f;
                     sp->f = a + b; sp++; break; }
        case 0x63: { double a, b; sp -= 2;
                     memcpy(&b, &sp[0].j, 8u); sp -= 2;
                     memcpy(&a, &sp[0].j, 8u);
                     a += b; memcpy(&sp[0].j, &a, 8u);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x64: { nj_s4 b = (--sp)->i, a = (--sp)->i;
                     sp->i = (nj_s4)((nj_u4)a - (nj_u4)b); sp++; break; }
        case 0x65: { nj_s8 b; sp -= 2; b = sp[0].j; sp -= 2;
                     sp[0].j = (nj_s8)((nj_u8)sp[0].j - (nj_u8)b);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x66: { float b = (--sp)->f, a = (--sp)->f;
                     sp->f = a - b; sp++; break; }
        case 0x67: { double a, b; sp -= 2;
                     memcpy(&b, &sp[0].j, 8u); sp -= 2;
                     memcpy(&a, &sp[0].j, 8u);
                     a -= b; memcpy(&sp[0].j, &a, 8u);
                     sp[1].bits = 0u; sp += 2; break; }
        L_0x68: case 0x68: { nj_s4 b = (--sp)->i, a = (--sp)->i;
                     sp->i = (nj_s4)((nj_u4)a * (nj_u4)b); sp++; DISPATCH(); }
        case 0x69: { nj_s8 b; sp -= 2; b = sp[0].j; sp -= 2;
                     sp[0].j = (nj_s8)((nj_u8)sp[0].j * (nj_u8)b);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x6a: { float b = (--sp)->f, a = (--sp)->f;
                     sp->f = a * b; sp++; break; }
        case 0x6b: { double a, b; sp -= 2;
                     memcpy(&b, &sp[0].j, 8u); sp -= 2;
                     memcpy(&a, &sp[0].j, 8u);
                     a *= b; memcpy(&sp[0].j, &a, 8u);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x6c: { nj_s4 b = (--sp)->i, a = (--sp)->i;
                     if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
                     sp->i = (a == (nj_s4)0x80000000 && b == -1) ? a : (a / b);
                     sp++; break; }
        case 0x6d: { nj_s8 b, a; sp -= 2; b = sp[0].j; sp -= 2; a = sp[0].j;
                     if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
                     if (a == (nj_s8)0x8000000000000000LL && b == -1) {
                         sp[0].j = a;
                     } else {
                         sp[0].j = a / b;
                     }
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x6e: { float b = (--sp)->f, a = (--sp)->f;
                     sp->f = a / b; sp++; break; }
        case 0x6f: { double a, b; sp -= 2;
                     memcpy(&b, &sp[0].j, 8u); sp -= 2;
                     memcpy(&a, &sp[0].j, 8u);
                     a /= b; memcpy(&sp[0].j, &a, 8u);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x70: { nj_s4 b = (--sp)->i, a = (--sp)->i;
                     if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
                     sp->i = (b == -1) ? 0 : (a % b); sp++; break; }
        case 0x71: { nj_s8 b, a; sp -= 2; b = sp[0].j; sp -= 2; a = sp[0].j;
                     if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
                     sp[0].j = (b == -1) ? 0 : (a % b);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x72: { float b = (--sp)->f, a = (--sp)->f;
                     sp->f = (float)fmod((double)a, (double)b); sp++; break; }
        case 0x73: { double a, b; sp -= 2;
                     memcpy(&b, &sp[0].j, 8u); sp -= 2;
                     memcpy(&a, &sp[0].j, 8u);
                     a = fmod(a, b); memcpy(&sp[0].j, &a, 8u);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x74: sp[-1].i = -sp[-1].i; break;            /* ineg */
        case 0x75: sp[-2].j = -sp[-2].j; break;            /* lneg */
        case 0x76: sp[-1].f = -sp[-1].f; break;            /* fneg */
        case 0x77: { double d; memcpy(&d, &sp[-2].j, 8u);
                     d = -d; memcpy(&sp[-2].j, &d, 8u); break; }

        case 0x78: { nj_s4 n = (--sp)->i, v = (--sp)->i;
                     sp->i = (nj_s4)((nj_u4)v << ((nj_u4)n & 31u)); sp++; break; }
        case 0x79: { nj_s4 n = (--sp)->i; sp -= 2;
                     sp[0].j = (nj_s8)((nj_u8)sp[0].j << ((nj_u4)n & 63u));
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x7a: { nj_s4 n = (--sp)->i, v = (--sp)->i;
                     sp->i = v >> ((nj_u4)n & 31u); sp++; break; }
        case 0x7b: { nj_s4 n = (--sp)->i; sp -= 2;
                     sp[0].j = sp[0].j >> ((nj_u4)n & 63u);
                     sp[1].bits = 0u; sp += 2; break; }
        case 0x7c: { nj_s4 n = (--sp)->i; nj_u4 v = (nj_u4)(--sp)->i;
                     sp->i = (nj_s4)(v >> ((nj_u4)n & 31u)); sp++; break; }
        case 0x7d: { nj_s4 n = (--sp)->i; sp -= 2;
                     sp[0].j = (nj_s8)((nj_u8)sp[0].j >> ((nj_u4)n & 63u));
                     sp[1].bits = 0u; sp += 2; break; }
        L_0x7e: case 0x7e: { nj_s4 b = (--sp)->i, a = (--sp)->i; sp->i = a & b; sp++; DISPATCH(); }
        case 0x7f: { nj_s8 b; sp -= 2; b = sp[0].j; sp -= 2;
                     sp[0].j = sp[0].j & b; sp[1].bits = 0u; sp += 2; break; }
        case 0x80: { nj_s4 b = (--sp)->i, a = (--sp)->i; sp->i = a | b; sp++; break; }
        case 0x81: { nj_s8 b; sp -= 2; b = sp[0].j; sp -= 2;
                     sp[0].j = sp[0].j | b; sp[1].bits = 0u; sp += 2; break; }
        L_0x82: case 0x82: { nj_s4 b = (--sp)->i, a = (--sp)->i; sp->i = a ^ b; sp++; DISPATCH(); }
        case 0x83: { nj_s8 b; sp -= 2; b = sp[0].j; sp -= 2;
                     sp[0].j = sp[0].j ^ b; sp[1].bits = 0u; sp += 2; break; }
        L_0x84: case 0x84: {                               /* iinc */
            nj_u2 i = (nj_u2)U1();
            nj_s1 c = S1();
            locals[i].i += c;
            DISPATCH(); }

        /* ================= conversions ================= */
        case 0x85: { nj_s4 v = (--sp)->i; PUSH_W((nj_s8)v); break; }
        case 0x86: { nj_s4 v = (--sp)->i; sp->f = (float)v; sp++; break; }
        case 0x87: { nj_s4 v = (--sp)->i; double d = (double)v;
                     memcpy(&sp->j, &d, 8u); sp[1].bits = 0u; sp += 2; break; }
        case 0x88: { sp -= 2; sp->i = (nj_s4)sp[0].j; sp++; break; }
        case 0x89: { sp -= 2; sp->f = (float)sp[0].j; sp++; break; }
        case 0x8a: { sp -= 2; { double d = (double)sp[0].j;
                     memcpy(&sp[0].j, &d, 8u); sp[1].bits = 0u; sp += 2; } break; }
        case 0x8b: { float f = (--sp)->f;                  /* f2i */
                     if (f != f) sp->i = 0;
                     else if (f >= 2147483648.0f) sp->i = 0x7FFFFFFF;
                     else if (f <= -2147483904.0f) sp->i = (nj_s4)0x80000000;
                     else sp->i = (nj_s4)f;
                     sp++; break; }
        case 0x8c: { float f = (--sp)->f; PUSH_W((nj_s8)f); break; }
        case 0x8d: { float f = (--sp)->f; double d = (double)f;
                     memcpy(&sp->j, &d, 8u); sp[1].bits = 0u; sp += 2; break; }
        case 0x8e: { double d; sp -= 2; memcpy(&d, &sp[0].j, 8u);
                     if (d != d) sp->i = 0;
                     else if (d >= 2147483648.0) sp->i = 0x7FFFFFFF;
                     else if (d <= -2147483649.0) sp->i = (nj_s4)0x80000000;
                     else sp->i = (nj_s4)d;
                     sp++; break; }
        case 0x8f: { double d; sp -= 2; memcpy(&d, &sp[0].j, 8u);
                     sp[0].j = (nj_s8)d; sp[1].bits = 0u; sp += 2; break; }
        case 0x90: { double d; sp -= 2; memcpy(&d, &sp[0].j, 8u);
                     sp->f = (float)d; sp++; break; }
        case 0x91: sp[-1].i = (nj_s4)(nj_s1)sp[-1].i; break;   /* i2b */
        case 0x92: sp[-1].i = (nj_s4)(nj_u2)sp[-1].i; break;   /* i2c */
        case 0x93: sp[-1].i = (nj_s4)(nj_s2)sp[-1].i; break;   /* i2s */

        /* ================= comparisons ================= */
        L_0x94: case 0x94: { nj_s8 b, a; sp -= 2; b = sp[0].j; sp -= 2; a = sp[0].j;
                     sp->i = (a > b) ? 1 : ((a == b) ? 0 : -1); sp++; DISPATCH(); }
        case 0x95: case 0x96: {                            /* fcmpl / fcmpg */
            float b = (--sp)->f, a = (--sp)->f;
            if (a > b) sp->i = 1;
            else if (a < b) sp->i = -1;
            else if (a == b) sp->i = 0;
            else sp->i = (op == 0x95) ? -1 : 1;
            sp++; break; }
        case 0x97: case 0x98: {                            /* dcmpl / dcmpg */
            double a, b;
            sp -= 2; memcpy(&b, &sp[0].j, 8u);
            sp -= 2; memcpy(&a, &sp[0].j, 8u);
            if (a > b) sp->i = 1;
            else if (a < b) sp->i = -1;
            else if (a == b) sp->i = 0;
            else sp->i = (op == 0x97) ? -1 : 1;
            sp++; break; }

        /* ================= branches ================= */
        case 0x99: { nj_s4 v = (--sp)->i; nj_s2 o = S2(); if (v == 0) JMP(o); break; }
        case 0x9a: { nj_s4 v = (--sp)->i; nj_s2 o = S2(); if (v != 0) JMP(o); break; }
        case 0x9b: { nj_s4 v = (--sp)->i; nj_s2 o = S2(); if (v < 0) JMP(o); break; }
        case 0x9c: { nj_s4 v = (--sp)->i; nj_s2 o = S2(); if (v >= 0) JMP(o); break; }
        case 0x9d: { nj_s4 v = (--sp)->i; nj_s2 o = S2(); if (v > 0) JMP(o); break; }
        case 0x9e: { nj_s4 v = (--sp)->i; nj_s2 o = S2(); if (v <= 0) JMP(o); break; }
        L_0x9f: case 0x9f: case 0xa0: case 0xa1:
        case 0xa2: case 0xa3: case 0xa4: {
            nj_s4 b = (--sp)->i, a = (--sp)->i;
            nj_s2 o = S2();
            int take;
            switch (op) {
            case 0x9f: take = (a == b); break;
            case 0xa0: take = (a != b); break;
            case 0xa1: take = (a <  b); break;
            case 0xa2: take = (a >= b); break;
            case 0xa3: take = (a >  b); break;
            default:   take = (a <= b); break;
            }
            if (take) JMP(o);
            DISPATCH(); }
        case 0xa5: {                                       /* if_acmpeq */
            nj_ref b = (--sp)->r;
            nj_ref a = (--sp)->r;
            nj_s2 o = S2();
            if (a == b) JMP(o);
            break; }
        case 0xa6: {                                       /* if_acmpne */
            nj_ref b = (--sp)->r;
            nj_ref a = (--sp)->r;
            nj_s2 o = S2();
            if (a != b) JMP(o);
            break; }
        L_0xa7: case 0xa7: { nj_s2 o = S2(); JMP(o); DISPATCH(); }  /* goto */
        case 0xa8: {                                        /* jsr */
            nj_s2 o = S2();
            sp->i = (nj_s4)pc;
            sp++;
            JMP(o);
            break; }
        case 0xa9: {                                        /* ret */
            nj_u2 i = (nj_u2)U1();
            pc = (nj_u4)locals[i].i;
            break; }
        case 0xaa: {                                        /* tableswitch */
            nj_u4 base = op_pc;
            nj_s4 def, lo, hi, idx;
            /*
             * The padding aligns `default` to a multiple of four FROM THE
             * START OF THE METHOD, not from the opcode.
             *
             * The spec says the padding makes defaultbyte1 "begin at an address
             * that is a multiple of four bytes from the start of the current
             * method", and the code array is the method, so the test is on the
             * ABSOLUTE offset.  This used to test (pc - op_pc), which lands one
             * to three bytes late whenever the opcode is not itself 4-aligned:
             * every operand is then read shifted, the jump offsets are garbage,
             * and pc leaves the method.  A game with a dense switch hit it as
             * "code runs past the end of the method @ Game15Canvas.move+26880".
             */
            while ((pc & 3u) != 0u) { pc++; }
            def = S4(); lo = S4(); hi = S4();
            idx = (--sp)->i;
            if (idx < lo || idx > hi) {
                JUMP_ABS(base + (nj_u4)def);
            } else {
                nj_u4 at = pc + 4u * (nj_u4)(idx - lo);
                nj_u4 save = pc;
                pc = at;
                { nj_s4 off = S4(); (void)save; JUMP_ABS(base + (nj_u4)off); }
            }
            break; }
        case 0xab: {                                        /* lookupswitch */
            nj_u4 base = op_pc;
            nj_s4 def, npairs, idx, i;
            /* Absolute alignment, for the same reason as tableswitch above. */
            while ((pc & 3u) != 0u) { pc++; }
            def = S4();
            npairs = S4();
            idx = (--sp)->i;
            {
                nj_u4 table = pc;
                nj_u4 hit = 0u;
                int found = 0;
                for (i = 0; i < npairs; i++) {
                    nj_s4 key;
                    pc = table + 8u * (nj_u4)i;
                    key = S4();
                    if (key == idx) {
                        hit = (nj_u4)S4();
                        found = 1;
                        break;
                    }
                }
                JUMP_ABS(base + (found ? hit : (nj_u4)def));
            }
            break; }

        /* ================= returns ================= */
        /*
         * Returning to a Java caller has to do three things:
         *   1. remember where our arguments started - that is exactly where
         *      the caller's operand stack must be rewound to (fr->locals ==
         *      argbase, see nj_frame_push);
         *   2. pop our frame;
         *   3. reload the caller and push the result at that rewind point.
         * Skipping step 1 makes the caller's stack grow by one slot per call,
         * which shows up much later as a bogus value on the operand stack.
         */
#define DO_RETURN(rv, wide) do {                                       \
            nj_value rv_ = (rv);                                           \
            nj_value *rsp_ = fr->locals;                                   \
            SAVE();                                                        \
            nj_frame_pop(vm, th);                                          \
            if (th->nframes <= stop_depth) {                               \
                th->ret_value = rv_;                                       \
                return NJ_OK;                                              \
            }                                                              \
            RELOAD();                                                      \
            sp = rsp_;                                                     \
            if (wide) {                                                    \
                sp[0] = rv_;                                               \
                sp[1].bits = 0u;                                           \
                sp += 2;                                                   \
            } else {                                                       \
                *sp++ = rv_;                                               \
            }                                                              \
        } while (0)

        L_0xac: case 0xac: case 0xae:                      /* ireturn freturn */
            DO_RETURN(*(--sp), 0);
            /* DO_RETURN either returns outright or RELOADs into the CALLER's
             * frame, in which case this is an ordinary dispatch: pc and code
             * were both reloaded, so the next instruction comes from the
             * caller.  The break was reachable, and converting it is safe -
             * it was skipped for a round on the suspicion that it was not. */
            DISPATCH();
        case 0xad: case 0xaf:                              /* lreturn dreturn */
            sp -= 2;
            DO_RETURN(sp[0], 1);
            break;
        case 0xb0:                                         /* areturn */
            DO_RETURN(*(--sp), 0);
            break;
        case 0xb1:                                         /* return (void) */
            /*
             * A void return must NOT push anything.  Reusing DO_RETURN with a
             * zero value looks harmless and is not: it leaves a spurious slot
             * on the caller's operand stack, so every `new X()` where X is
             * Java - i.e. every constructor call - shifts the caller's stack
             * by one.  The next instruction then reads the wrong slot, which
             * shows up as a null receiver in a putfield two instructions
             * later, nowhere near the actual cause.
             *
             * main() and every method called from C never exposed this: they
             * leave through the stop_depth path below, before the push.
             */
            {
                nj_value *rsp_ = fr->locals;
                SAVE();
                nj_frame_pop(vm, th);
                if (th->nframes <= stop_depth) {
                    th->ret_value.bits = 0u;
                    return NJ_OK;
                }
                RELOAD();
                sp = rsp_;
            }
            break;

        /* ================= fields ================= */
        L_0xb2: case 0xb2: case 0xb4: {                    /* getstatic / getfield */
            nj_u2 idx = U2();
            nj_cpentry *e = NULL;
            nj_ref o = NULL;
            nj_field *f;
            if (op == 0xb4) {
                o = (--sp)->r;
                if (!o) THROW("java/lang/NullPointerException", NULL);
            }
            if (nj_cp_resolve(vm, m->owner, idx, &e) != NJ_OK || !e ||
                !e->target) {
                if (!vm->exception) {
                    (void)nj_resolve_error(vm);
                }
                goto do_unwind;
            }
            f = (nj_field *)e->target;
            /*
             * Reading a static field is an active use of the class that
             * DECLARES it, so its <clinit> has to have run.  Interfaces count:
             * obfuscated titles park lookup tables in an interface and fill
             * them from its static initialiser, and without this the table
             * reads back null and the next iaload throws.
             */
            if (e->kind == NJ_LINK_FIELD_STATIC) {
                SAVE();
                if (nj_class_init(vm, f->owner) != NJ_OK) {
                    if (!vm->exception) {
                        (void)nj_throw(vm, "java/lang/NoClassDefFoundError",
                                       f->owner->name);
                    }
                    goto do_unwind;
                }
            }
            switch (f->ptype) {
            case NJ_PT_LONG:
            case NJ_PT_DOUBLE:
                sp[0].j = nj_field_read_long(vm, o, f);
                sp[1].bits = 0u; sp += 2;
                break;
            case NJ_PT_OBJECT:
                sp->r = nj_field_read_ref(vm, o, f); sp++;
                break;
            default:
                sp->i = nj_field_read_int(vm, o, f); sp++;
                break;
            }
            DISPATCH(); }
        case 0xb3: case 0xb5: {                            /* putstatic / putfield */
            nj_u2 idx = U2();
            nj_cpentry *e = NULL;
            nj_field *f;
            if (nj_cp_resolve(vm, m->owner, idx, &e) != NJ_OK || !e ||
                !e->target) {
                if (!vm->exception) {
                    (void)nj_resolve_error(vm);
                }
                goto do_unwind;
            }
            f = (nj_field *)e->target;
            /* Writing one is an active use too, and the value is left on the
             * operand stack across the initialiser so the GC can see it. */
            if (e->kind == NJ_LINK_FIELD_STATIC) {
                SAVE();
                if (nj_class_init(vm, f->owner) != NJ_OK) {
                    if (!vm->exception) {
                        (void)nj_throw(vm, "java/lang/NoClassDefFoundError",
                                       f->owner->name);
                    }
                    goto do_unwind;
                }
            }
            switch (f->ptype) {
            case NJ_PT_LONG:
            case NJ_PT_DOUBLE: {
                nj_s8 v;
                nj_ref o = NULL;
                sp -= 2;
                v = sp[0].j;
                if (op == 0xb5) {
                    o = (--sp)->r;
                    if (!o) THROW("java/lang/NullPointerException", NULL);
                }
                nj_field_write_long(vm, o, f, v);
                break; }
            case NJ_PT_OBJECT: {
                nj_ref v = (--sp)->r;
                nj_ref o = NULL;
                if (op == 0xb5) {
                    o = (--sp)->r;
                    if (!o) THROW("java/lang/NullPointerException", NULL);
                }
                nj_field_write_ref(vm, o, f, v);
                break; }
            default: {
                nj_s4 v = (--sp)->i;
                nj_ref o = NULL;
                if (op == 0xb5) {
                    o = (--sp)->r;
                    if (!o) THROW("java/lang/NullPointerException", NULL);
                }
                nj_field_write_int(vm, o, f, v);
                break; }
            }
            break; }

        /* ================= invokes ================= */
        L_invoke: case 0xb6: case 0xb7: case 0xb8: case 0xb9: {
            nj_u2 idx;
            nj_cpentry *e = NULL;
            nj_method *target;
            nj_u2 total;
            int pushed = 0;
            nj_status st;

            if (op == 0xb9) {
                idx = U2();
                pc += 2u;   /* count + reserved byte */
            } else {
                idx = U2();
            }
            if (nj_cp_resolve(vm, m->owner, idx, &e) != NJ_OK || !e) {
                if (!vm->exception) {
                    (void)nj_resolve_error(vm);
                }
                goto do_unwind;
            }
            target = (nj_method *)e->target;
            if (!target) {
                THROW("java/lang/NoSuchMethodError", NULL);
            }

            /*
             * The OPCODE says whether an objectref is on the operand stack; the
             * CONSTANT POOL says what the method actually is - and they can
             * disagree.  Old J2ME compilers and obfuscators emit invokevirtual
             * for a static method (Canvas.getGameAction is the classic one, and
             * five of the games in tests/games/ do it), leaving an objectref
             * that the callee must never receive as `this`.
             *
             * nj_class.c already works this out when it resolves the entry: it
             * probes for an instance method and falls back to a static one,
             * recording the answer in e->kind.  Trusting the opcode instead -
             * as this dispatch used to - sent the call down the virtual path,
             * where nj_method_lookup() skips statics by design and therefore
             * never found it.  The receiver class then got blamed for a method
             * its superclass declares: "n does not implement
             * javax/microedition/lcdui/Canvas.getGameAction".
             *
             * So when the reference is static but the opcode pushed a receiver,
             * drop the artifact: shift the arguments down one slot and shrink
             * the stack.  The callee then sees exactly the operands a plain
             * invokestatic would have given it.
             */
            if (op != 0xb8 && e->kind == NJ_LINK_METHOD_STATIC) {
                nj_u2 aslots = target->arg_slots;
                nj_value *dst;

                if (sp - stack < (nj_s4)(aslots + 1u)) {
                    THROW("java/lang/VerifyError", "operand stack underflow");
                }
                dst = sp - aslots - 1u;
                {
                    nj_u2 i;
                    for (i = 0u; i < aslots; i++) {
                        dst[i] = dst[i + 1u];
                    }
                }
                sp = dst + aslots;
            } else if (op == 0xb8 && e->kind != NJ_LINK_METHOD_STATIC) {
                /*
                 * The mirror image: invokestatic naming an instance method.
                 * No receiver was pushed, so the callee would read `this` from
                 * whatever happens to sit below its arguments.  Say so instead
                 * of silently corrupting the frame.
                 */
                THROW("java/lang/IncompatibleClassChangeError",
                      "invokestatic on an instance method");
            }

            if ((op == 0xb6 || op == 0xb9) && e->kind != NJ_LINK_METHOD_STATIC) {
                /* virtual / interface: dispatch on the receiver's class.
                 * A static target never comes here: the block above already
                 * dropped its artifact receiver, and dispatching on the
                 * receiver is exactly what must not happen for a static call. */
                nj_u2 aslots = target->arg_slots;
                nj_ref recv;

                if (sp - stack < (nj_s4)(aslots + 1u)) {
                    THROW("java/lang/VerifyError", "operand stack underflow");
                }
                recv = (sp - aslots - 1u)->r;
                if (!recv) {
                    THROW("java/lang/NullPointerException", NULL);
                }
                /*
                 * The operand stack is untyped and this runtime has no
                 * verifier, so a slot that is supposed to hold a receiver can
                 * hold anything at all.  `!recv` is not enough: a title in the
                 * corpus put 0x87fff8008 there, which is not NULL, is not an
                 * object, and crashed the emulator inside nj_method_lookup
                 * three frames later with no indication of which method or
                 * which class was at fault.
                 *
                 * Range-checking it turns an unexplainable SIGSEGV into a
                 * named exception at the instruction that caused it.
                 */
                if (!nj_heap_owns(vm, recv)) {
                    THROW("java/lang/VerifyError",
                          "receiver is not an object reference");
                }
                if (e->recv_cls != recv->cls || !e->ic_target) {
                    nj_method *dm = nj_method_lookup(recv->cls, target->name,
                                                     target->desc);
                    if (!dm) {
                    /*
                     * SAVE FIRST.  nj_class_init() can run a <clinit>, which
                     * runs Java code through the C bridge - and that bridge
                     * places the new frame at th->frames[nframes-1].sp, the
                     * SAVED stack pointer.  Without this the saved value is
                     * stale (the last invoke, or the frame's entry), so the
                     * <clinit> frame lands inside the live operand stack and
                     * overwrites it.  The symptom is not a crash near the
                     * <clinit>: it is a wrong value two instructions later in
                     * the method that triggered it, which is why this took a
                     * while to find.
                     */
                    SAVE();
                        (void)nj_class_init(vm, recv->cls);
                        dm = nj_method_lookup(recv->cls, target->name,
                                              target->desc);
                    }
                    if (!dm) {
                        /*
                         * Name the receiver class as well as the method.
                         * "AbstractMethodError: paint" tells you nothing: the
                         * declaration is on some superclass, the receiver is
                         * the class that failed to override it, and which is
                         * which is the whole diagnosis.
                         */
                        if (!vm->exception) {
                            char mbuf[256];
                            nj_u4 mn = 0u;
                            const char *q = recv->cls->name;
                            const char *w = " does not implement ";
                            while (q && *q && mn + 1u < sizeof(mbuf)) {
                                mbuf[mn++] = *q++;
                            }
                            while (*w && mn + 1u < sizeof(mbuf)) {
                                mbuf[mn++] = *w++;
                            }
                            q = target->owner ? target->owner->name : "?";
                            while (*q && *q && mn + 1u < sizeof(mbuf)) {
                                mbuf[mn++] = *q++;
                            }
                            if (mn + 1u < sizeof(mbuf)) { mbuf[mn++] = '.'; }
                            q = target->name;
                            while (q && *q && mn + 1u < sizeof(mbuf)) {
                                mbuf[mn++] = *q++;
                            }
                            mbuf[mn] = '\0';
                            vm->exception_method = m;
                            vm->exception_pc = op_pc;
                            (void)nj_throw(vm,
                                ((target->flags & NJ_ACC_ABSTRACT) != 0u)
                                    ? "java/lang/AbstractMethodError"
                                    : "java/lang/NoSuchMethodError",
                                mbuf);
                        }
                        goto do_unwind;
                    }
                    e->recv_cls = recv->cls;
                    e->ic_target = dm;
                }
                target = (nj_method *)e->ic_target;
                /* An abstract method with no override is a genuine error; a
                 * native has code == NULL too, but interp_invoke binds and
                 * runs it (and reports UnsatisfiedLinkError if unbound). */
                if (!target->code && (target->flags & NJ_ACC_ABSTRACT) != 0u) {
                    if (!vm->exception) {
                        char mbuf[256];
                        nj_u4 mn = 0u;
                        const char *q = recv->cls->name;
                        while (q && *q && mn + 1u < sizeof(mbuf)) {
                            mbuf[mn++] = *q++;
                        }
                        q = " resolved to the abstract ";
                        while (*q && mn + 1u < sizeof(mbuf)) {
                            mbuf[mn++] = *q++;
                        }
                        q = target->owner ? target->owner->name : "?";
                        while (*q && mn + 1u < sizeof(mbuf)) {
                            mbuf[mn++] = *q++;
                        }
                        if (mn + 1u < sizeof(mbuf)) { mbuf[mn++] = '.'; }
                        q = target->name;
                        while (q && *q && mn + 1u < sizeof(mbuf)) {
                            mbuf[mn++] = *q++;
                        }
                        mbuf[mn] = '\0';
                        vm->exception_method = m;
                        vm->exception_pc = op_pc;
                        (void)nj_throw(vm, "java/lang/AbstractMethodError",
                                       mbuf);
                    }
                    goto do_unwind;
                }
            } else {
                /* invokespecial / invokestatic: initialise the owner first.
                 * SAVE() first for the same reason as the `new` case below:
                 * the init can run Java code, and the bridge needs a current
                 * saved sp to place its frame above the live stack. */
                SAVE();
                if (nj_class_init(vm, target->owner) != NJ_OK) {
                    if (!vm->exception) {
                        (void)nj_throw(vm, "java/lang/NoClassDefFoundError",
                                       target->owner->name);
                    }
                    goto do_unwind;
                }
            }

            total = (nj_u2)(target->arg_slots +
                            ((target->flags & NJ_ACC_STATIC) ? 0u : 1u));
            /*
             * Publish pc/sp into the frame BEFORE pushing the callee: if the
             * callee is Java, interp_invoke makes its frame current and the
             * only way back to our pc is through th->frames[].  Without this
             * the callee would return into whatever pc was last saved.
             */
            SAVE();
            st = interp_invoke(vm, th, target, total, &sp, &pushed);
            if (st == NJ_ERR_TIMEOUT) {
                SAVE();
                return NJ_ERR_YIELD;
            }
            if (st != NJ_OK) {
                goto do_unwind;
            }
            if (pushed) {
                RELOAD();
            }
            DISPATCH();
        }

        /* ================= object creation ================= */
        case 0xbb: {                                       /* new */
            nj_u2 idx = U2();
            const char *cn = nj_cp_class_name(m->owner, idx);
            nj_class *c;
            nj_ref o;
            if (!cn) THROW("java/lang/NoClassDefFoundError", NULL);
            c = nj_class_lookup(vm, cn);
            if (!c) THROW("java/lang/NoClassDefFoundError", cn);
            /* SAVE FIRST - see the note in the dispatch path above.  This is
             * the site that was found by a corpus title: `new DoorKey` ran
             * DoorKey.<clinit>, whose frame landed on top of the live operand
             * stack of the calling method and destroyed the array reference
             * and loop counter sitting there. */
            SAVE();
            if (nj_class_init(vm, c) != NJ_OK) goto do_unwind;
            o = nj_new_instance(vm, c);
            if (!o) THROW("java/lang/OutOfMemoryError", NULL);
            sp->r = o; sp++;
            break; }
        case 0xbc: {                                       /* newarray */
            nj_u1 atype = U1();
            nj_s4 len = (--sp)->i;
            const char *d = NULL;
            nj_ref a;
            if (len < 0) THROW("java/lang/NegativeArraySizeException", NULL);
            switch (atype) {
            case 4:  d = "Z"; break;
            case 5:  d = "C"; break;
            case 6:  d = "F"; break;
            case 7:  d = "D"; break;
            case 8:  d = "B"; break;
            case 9:  d = "S"; break;
            case 10: d = "I"; break;
            case 11: d = "J"; break;
            default: THROW("java/lang/VerifyError", "bad newarray type");
            }
            a = nj_array_new_of(vm, d, (nj_u4)len);
            if (!a) goto do_unwind;
            sp->r = a; sp++;
            break; }
        case 0xbd: {                                       /* anewarray */
            nj_u2 idx = U2();
            nj_s4 len = (--sp)->i;
            const char *cn = nj_cp_class_name(m->owner, idx);
            char buf[288];
            nj_u4 n = 0u;
            nj_ref a;
            if (len < 0) THROW("java/lang/NegativeArraySizeException", NULL);
            if (!cn) THROW("java/lang/NoClassDefFoundError", NULL);
            buf[n++] = '[';
            if (cn[0] == '[') {
                while (*cn && n + 1u < (nj_u4)sizeof(buf)) { buf[n++] = *cn++; }
            } else {
                buf[n++] = 'L';
                while (*cn && n + 2u < (nj_u4)sizeof(buf)) { buf[n++] = *cn++; }
                buf[n++] = ';';
            }
            buf[n] = '\0';
            a = nj_array_new_of(vm, buf, (nj_u4)len);
            if (!a) goto do_unwind;
            sp->r = a; sp++;
            break; }
        case 0xbe: {                                       /* arraylength */
            nj_ref a = (--sp)->r;
            if (!a) THROW("java/lang/NullPointerException", NULL);
            sp->i = (nj_s4)a->length; sp++;
            break; }
        case 0xbf: {                                       /* athrow */
            nj_ref e = (--sp)->r;
            if (!e) THROW("java/lang/NullPointerException", NULL);
            if (!vm->exception) {
                vm->exception = e;
            }
            goto do_unwind; }
        case 0xc0: {                                       /* checkcast */
            nj_u2 idx = U2();
            nj_ref o = sp[-1].r;
            if (o) {
                const char *cn = nj_cp_class_name(m->owner, idx);
                nj_class *c = cn ? nj_class_resolve(vm, cn) : NULL;
                if (!c) THROW("java/lang/NoClassDefFoundError", cn);
                if (!nj_class_is_assignable(o->cls, c)) {
                    THROW("java/lang/ClassCastException", cn);
                }
            }
            break; }
        case 0xc1: {                                       /* instanceof */
            nj_u2 idx = U2();
            nj_ref o = (--sp)->r;
            if (!o) {
                sp->i = 0; sp++;
            } else {
                const char *cn = nj_cp_class_name(m->owner, idx);
                nj_class *c = cn ? nj_class_resolve(vm, cn) : NULL;
                if (!c) THROW("java/lang/NoClassDefFoundError", cn);
                sp->i = nj_class_is_assignable(o->cls, c) ? 1 : 0;
                sp++;
            }
            break; }
        case 0xc2: {                                       /* monitorenter */
            nj_ref o = (--sp)->r;
            if (!o) THROW("java/lang/NullPointerException", NULL);
            nj_vm_monitor_enter(vm, o);
            break; }
        case 0xc3: {                                       /* monitorexit */
            nj_ref o = (--sp)->r;
            if (!o) THROW("java/lang/NullPointerException", NULL);
            nj_vm_monitor_exit(vm, o);
            break; }
        case 0xc4: {                                       /* wide */
            nj_u1 sub = U1();
            switch (sub) {
            case 0x15: { nj_u2 i = U2(); sp->bits = locals[i].bits; sp++; break; }
            case 0x16: { nj_u2 i = U2(); sp[0] = locals[i]; sp[1].bits=0u; sp+=2; break; }
            case 0x17: { nj_u2 i = U2(); sp->bits = locals[i].bits; sp++; break; }
            case 0x18: { nj_u2 i = U2(); sp[0] = locals[i]; sp[1].bits=0u; sp+=2; break; }
            case 0x19: { nj_u2 i = U2(); sp->r = locals[i].r; sp++; break; }
            case 0x36: { nj_u2 i = U2(); locals[i].bits = (--sp)->bits; break; }
            case 0x37: { nj_u2 i = U2(); sp -= 2; locals[i] = sp[0];
                         locals[i + 1u].bits = 0u; break; }
            case 0x38: { nj_u2 i = U2(); locals[i].bits = (--sp)->bits; break; }
            case 0x39: { nj_u2 i = U2(); sp -= 2; locals[i] = sp[0];
                         locals[i + 1u].bits = 0u; break; }
            case 0x3a: { nj_u2 i = U2(); locals[i].r = (--sp)->r; break; }
            case 0x84: { nj_u2 i = U2(); nj_s2 c = S2(); locals[i].i += c; break; }
            case 0xa9: { nj_u2 i = U2(); pc = (nj_u4)locals[i].i; break; }
            default:
                THROW("java/lang/VerifyError", "bad wide opcode");
            }
            break; }
        case 0xc5: {                                       /* multianewarray */
            nj_u2 idx = U2();
            nj_u1 dims = U1();
            const char *cn = nj_cp_class_name(m->owner, idx);
            nj_class *c;
            nj_ref a;
            nj_s4 lens[NJ_MAX_DIMENSIONS];
            int k;

            if (!cn) THROW("java/lang/NoClassDefFoundError", NULL);
            if (dims == 0u || dims > (nj_u1)NJ_MAX_DIMENSIONS) {
                THROW("java/lang/VerifyError", "bad multianewarray dims");
            }
            /* The dimension sizes are pushed outermost-first. */
            for (k = (int)dims - 1; k >= 0; k--) {
                lens[k] = (--sp)->i;
                if (lens[k] < 0) {
                    THROW("java/lang/NegativeArraySizeException", NULL);
                }
            }
            c = nj_class_resolve(vm, cn);
            if (!c) THROW("java/lang/NoClassDefFoundError", cn);
            a = interp_multianew(vm, c, lens, (int)dims, 0);
            if (!a) {
                if (!vm->exception) {
                    (void)nj_throw_oom(vm);
                }
                goto do_unwind;
            }
            sp->r = a; sp++;
            break; }
        case 0xc6: { nj_ref v = (--sp)->r; nj_s2 o = S2(); if (!v) JMP(o); break; }
        case 0xc7: { nj_ref v = (--sp)->r; nj_s2 o = S2(); if (v) JMP(o); break; }
        case 0xc8: {                                       /* goto_w */
            nj_s4 o = S4();
            JUMP_ABS((nj_u4)(nj_s4)((nj_s4)op_pc + o));
            break; }
        case 0xc9: {                                       /* jsr_w */
            nj_s4 o = S4();
            sp->i = (nj_s4)pc;
            sp++;
            JUMP_ABS((nj_u4)(nj_s4)((nj_s4)op_pc + o));
            break; }

        default:
            /* Includes 0xba breakpoint, 0xca/0xfe/0xff impdep. */
            THROW("java/lang/VerifyError", "unsupported opcode");
        }

#undef ARRAY_HEAD
        CHECK_QUANTUM();
        continue;

do_unwind:
        SAVE();
        if (!vm->exception) {
            (void)nj_throw(vm, "java/lang/InternalError", "unwind without exception");
        }
        /* Record where it happened, once, for diagnostics. */
        if (!vm->exception_method) {
            vm->exception_method = m;
            vm->exception_pc = (pc > 0u) ? (pc - 1u) : 0u;
        }
        if (interp_unwind(vm, th, (pc > 0u) ? (pc - 1u) : 0u, stop_depth,
                          vm->exception)) {
            RELOAD();
            continue;
        }
        return NJ_ERR_HALT;
    }

#undef DO_RETURN
#undef U1
#undef S1
#undef U2
#undef S2
#undef S4
#undef JMP
#undef JUMP_ABS
#undef SAVE
#undef RELOAD
#undef THROW
#undef PUSH_W
#undef CHECK_QUANTUM
}
