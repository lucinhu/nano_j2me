/* Shared baseline-JIT/backend boundary. No backend owns VM state. */
#ifndef NJ_JIT_INTERNAL_H
#define NJ_JIT_INTERNAL_H

#include "nj_internal.h"

#if NJ_ENABLE_JIT
/* One bounded block. Stack requirements are relative to its incoming sp;
 * native code only touches frame slots and caller-saved registers. It never
 * calls C, allocates, throws, or changes frames.
 *
 * A block is a straight-line window plus, optionally, taken branches and
 * bounded back edges (loops). It always leaves through one of block->exits:
 * exits[0] is the end of the window, other exits are taken forward branches
 * that leave it and the quantum-budget check on a loop back edge. A successful
 * run returns
 *
 *     (completed << NJ_JIT_EXIT_SHIFT) | exit_index
 *
 * where `completed` counts the bytecodes finished on this run and is never
 * more than the budget argument (nor less than 1 when exit_index is softless),
 * and `exit_index` selects the exit. Straight-line blocks always return
 * exit 0 and completed == insns unless a capacity-limited prefix shortened
 * them; a loop block completes whole iterations and can return from the
 * budget exit with completed > insns.
 *
 * A block may name up to NJ_JIT_MAX_FAULTS instructions whose guards can fail
 * at run time (null receiver, null/bounds/kind on an array, zero divisor).
 * Such a guard must jump to the block's fault exit *before* the instruction's
 * first side effect, and the exit returns
 *
 *     NJ_JIT_FAULT_BIT | (fault_index << 24) | completed
 *
 * where `completed` counts the bytecodes finished before the fault. The C
 * caller then sets pc to entry_pc + faults[i].pc_off, resets the operand stack
 * to entry_sp + faults[i].sp_delta, and lets the interpreter execute that
 * instruction again - so the Java-level exception and its PC come from the
 * interpreter, exactly as in a purely interpreted run. When the block cannot
 * stay inside NJ_JIT_MAX_FAULTS it ends before the next faulting instruction.
 *
 * A block that enters a back edge keeps its own bytecode counter and checks
 * `budget - completed >= insns` before every iteration, so native execution
 * always returns to the interpreter after at most `budget` bytecodes. */
typedef struct nj_jit_fault {
    nj_u2 pc_off;   /* faulting bytecode pc, relative to the block entry pc */
    nj_s2 sp_delta; /* operand stack at that pc, relative to incoming sp     */
} nj_jit_fault;

typedef struct nj_jit_exit {
    nj_u2 pc_off;   /* bytecode pc to resume at, relative to the block entry */
    nj_s2 sp_delta; /* operand stack there, relative to incoming sp          */
} nj_jit_exit;

typedef struct nj_jit_block {
    nj_u4 next_pc;      /* pc after the straight-line window (exits[0])     */
    nj_u4 insns;        /* bytecodes in the window; a lower bound per run   */
    int stack_delta;    /* exits[0].sp_delta                               */
    int min_stack;
    int max_growth;
    nj_size size;
    nj_u1 nfaults;
    nj_u1 nexits;
    nj_u1 loop;         /* 1 when the block revisits a back edge           */
    nj_jit_fault faults[NJ_JIT_MAX_FAULTS];
    nj_jit_exit exits[NJ_JIT_MAX_EXITS];
} nj_jit_block;

/* Native block entry. Arguments are the interpreter's operand stack pointer,
 * the frame locals pointer and the remaining scheduling budget (in Java
 * bytecodes); neither pointer is advanced and no frame slot outside the
 * block's own operand-stack range is touched. The result word is returned in
 * the integer result register (a0/w0). */
typedef nj_u4 (*nj_jit_fn)(nj_value *incoming_sp, nj_value *locals,
                           nj_u4 budget);

#define NJ_JIT_FAULT_BIT 0x80000000u
#define NJ_JIT_FAULT_INDEX_SHIFT 24u
#define NJ_JIT_FAULT_INDEX_MASK 0x7fu
#define NJ_JIT_FAULT_COMPLETED_MASK 0x00ffffffu
#define NJ_JIT_EXIT_SHIFT 4u
#define NJ_JIT_EXIT_INDEX_MASK 0x0fu

/* Portable code generator: usable on any host for encoding tests. Returns 1
 * for a nonempty block, 0 when declined. No writes beyond capacity.
 * `vm` may be NULL on a host without a VM: instance-field and array opcodes are
 * then declined (their operands cannot be resolved), everything else works. */
int nj_rv32_compile(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_u1 *buf,
                    nj_size capacity, nj_jit_block *out);
int nj_arm64_compile(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_u1 *buf,
                     nj_size capacity, nj_jit_block *out);

/* Scan only: no emission, allocation, resolution or callbacks. size is zero;
 * emission may later shorten the prefix to fit the code buffer. This is the
 * hot-site cost path and must stay allocation-free and callback-free. */
int nj_rv32_analyze(nj_vm *vm, const nj_method *m, nj_u4 pc,
                    nj_jit_block *out);
int nj_arm64_analyze(nj_vm *vm, const nj_method *m, nj_u4 pc,
                     nj_jit_block *out);

/* Record one runtime-faulting instruction in a block under construction.
 * `start_pc` is the block entry, `fault_pc` the instruction's own pc and
 * `sp_delta` the operand stack depth before that instruction, all relative to
 * the block entry. Returns 1 when recorded, 0 when the block already holds
 * NJ_JIT_MAX_FAULTS faults (the caller must then end the block before this
 * instruction). */
static inline int nj_jit_record_fault(nj_jit_block *block, nj_u4 start_pc,
                                      nj_u4 fault_pc, int sp_delta)
{
    nj_jit_fault *f;
    nj_u4 off;
    if (!block || block->nfaults >= NJ_JIT_MAX_FAULTS || fault_pc < start_pc) {
        return 0;
    }
    off = fault_pc - start_pc;
    if (off > 0xffffu) {
        return 0;
    }
    f = &block->faults[block->nfaults];
    f->pc_off = (nj_u2)off;
    f->sp_delta = (nj_s2)sp_delta;
    block->nfaults++;
    return 1;
}

/* Record one exit in a block under construction. `start_pc` is the block
 * entry, `pc` the bytecode position to resume at and `sp_delta` the operand
 * stack there, both relative to the entry. Identical exits are shared.
 * exits[0] is reserved for the end of the straight-line window. Returns the
 * exit index, or -1 when the block already holds NJ_JIT_MAX_EXITS exits. */
static inline int nj_jit_record_exit(nj_jit_block *block, nj_u4 start_pc,
                                     nj_u4 pc, int sp_delta)
{
    nj_u4 off, i;
    if (!block || pc < start_pc) {
        return -1;
    }
    off = pc - start_pc;
    if (off > 0xffffu) {
        return -1;
    }
    for (i = 0; i < block->nexits; ++i) {
        if (block->exits[i].pc_off == (nj_u2)off &&
            block->exits[i].sp_delta == (nj_s2)sp_delta) {
            return (int)i;
        }
    }
    if (block->nexits >= NJ_JIT_MAX_EXITS) {
        return -1;
    }
    block->exits[block->nexits].pc_off = (nj_u2)off;
    block->exits[block->nexits].sp_delta = (nj_s2)sp_delta;
    block->nexits++;
    return (int)block->nexits - 1;
}

/* Numeric constants are immutable raw class bytes. Object constants must go
 * through the interpreter's resolution/allocation path. Bounds are checked
 * without addition so malformed offsets cannot wrap. */
static inline int nj_jit_numeric_constant(const nj_method *m, unsigned index,
                                          nj_u4 *bits)
{
    const nj_class *c = m ? m->owner : NULL;
    nj_u4 info, off, tag;
    if (!c || !c->cp_info || !c->data || !bits || index == 0u ||
        index >= c->cp_count) return 0;
    info = c->cp_info[index];
    tag = info & 15u;
    if (tag != NJ_CP_INTEGER && tag != NJ_CP_FLOAT) return 0;
    off = info >> 4;
    if (!off || off > c->data_len || c->data_len - off < 4u) return 0;
    *bits = ((nj_u4)c->data[off] << 24) | ((nj_u4)c->data[off + 1u] << 16) |
            ((nj_u4)c->data[off + 2u] << 8) | c->data[off + 3u];
    return 1;
}

/* Conservative opcode filter shared by the native backends. False positives
 * are decoded once into the per-method probe map; known interpreter-only
 * opcodes avoid even the out-of-line probe. Never inspect operands here. */
static inline int nj_jit_opcode_candidate(nj_u1 op)
{
    return op <= 0x13u || (op >= 0x15u && op <= 0x2du) ||
           (op >= 0x36u && op <= 0x4eu) || (op >= 0x57u && op <= 0xa7u) ||
           (op >= 0x91u && op <= 0x93u) || op == 0xc4u ||
           op == 0x2eu || op == 0x4fu ||           /* iaload / iastore */
           (op >= 0x32u && op <= 0x35u) ||         /* aaload .. saload */
           (op >= 0x54u && op <= 0x56u) ||         /* bastore .. sastore */
           op == 0xb2u || op == 0xb3u ||           /* getstatic / putstatic */
           op == 0xb4u || op == 0xb5u ||           /* getfield / putfield */
           op == 0xbeu ||                          /* arraylength */
           op == 0xc6u || op == 0xc7u;             /* ifnull / ifnonnull */
}

/* True when bytecode `op` can end a native block, so that the following pc is a
 * plausible block entry: either the encoder never compiles it (calls, allocation,
 * long arithmetic, switches, returns, throws ...) or it changes pc non-linearly
 * (conditional branches, goto, ifnull/ifnonnull). Probing every straight-line
 * bytecode instead would charge the interpreter an out-of-line probe for code
 * that can never become an entry, which is most of what makes the probe path
 * visible in profiles; a compiled block also ends on these opcodes, and the
 * probe after a native block already covers capacity-split continuations. */
static inline int nj_jit_probe_boundary(nj_u1 op)
{
    if (!nj_jit_opcode_candidate(op)) return 1;          /* block ends here */
    /* branch, goto, wide and ifnull/ifnonnull are the candidates that may
     * redirect pc or hide the next opcode */
    return op >= 0x99u;
}

/* Read-only view of the per-VM constant-pool cache: this never resolves, never
 * bumps a counter and never enters Java, which is what the probe path needs.
 * A miss simply means the site stays interpreted until the interpreter has
 * resolved the entry itself. */
static inline const nj_cpentry *nj_jit_cp_peek(const nj_vm *vm,
                                               const nj_class *c,
                                               nj_u4 index)
{
    const nj_cpentry *e;
    nj_u4 hash, probes, mask;
    if (!vm || !c || !vm->cp_cache || vm->cp_cache_size == 0u ||
        index == 0u || index > 0xffffu) {
        return NULL;
    }
    mask = (nj_u4)vm->cp_cache_size - 1u;
    hash = nj_cp_key_hash(c, index) & mask;
    /* Match cp_cache_slot's open addressing without resolving, growing the
     * table or changing hit counters. A collision is not an unresolved field. */
    for (probes = 0u; probes <= mask; ++probes) {
        e = &vm->cp_cache[hash];
        if (!e->used) return NULL;
        if (e->cls == c && e->cp_index == index) return e;
        hash = (hash + 1u) & mask;
    }
    return NULL;
}

/* instance field whose link and layout are already resolved. The probe path
 * uses this to decide whether getfield/putfield can be compiled; it never
 * links a class or resolves a CP entry. Field offsets are assigned by the
 * class linker and are only trustworthy once the owner is linked. */
static inline const nj_field *nj_jit_instance_field(const nj_vm *vm,
                                                    const nj_class *c,
                                                    nj_u4 index)
{
    const nj_cpentry *e = nj_jit_cp_peek(vm, c, index);
    const nj_field *f;
    nj_u4 bytes;

    if (!e || e->kind != NJ_LINK_FIELD_INST || !e->target) {
        return NULL;
    }
    f = (const nj_field *)e->target;
    if ((f->flags & NJ_ACC_STATIC) != 0u || !f->owner ||
        f->owner->state < NJ_CLASS_LINKED) {
        return NULL;
    }
    switch (f->ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:   bytes = 1u; break;
    case NJ_PT_CHAR:
    case NJ_PT_SHORT:  bytes = 2u; break;
    case NJ_PT_INT:
    case NJ_PT_FLOAT:  bytes = 4u; break;
    case NJ_PT_OBJECT: bytes = (nj_u4)sizeof(nj_ref); break;
    case NJ_PT_LONG:
    case NJ_PT_DOUBLE: bytes = 8u; break;
    default:           return NULL;
    }
    if ((nj_u4)f->offset < (nj_u4)sizeof(nj_object) ||
        (nj_u4)f->offset + bytes > (nj_u4)f->owner->instance_size) {
        return NULL;
    }
    return f;
}

/* Read-only resolved view of a static field for getstatic/putstatic.  A static
 * access is only legal after the owning class is initialised: before that the
 * interpreter has to run <clinit>, which can execute arbitrary Java.  A caller
 * that sees NJ_CLASS_READY may emit a plain access (READY is terminal);
 * otherwise it must emit a run-time state check that faults back to the
 * interpreter.  The slot index was validated against vm->nstatics at link
 * time, including the second slot of long/double. */
static inline const nj_field *nj_jit_static_field(const nj_vm *vm,
                                                  const nj_class *c,
                                                  nj_u4 index)
{
    const nj_cpentry *e = nj_jit_cp_peek(vm, c, index);
    const nj_field *f;
    nj_u4 slots;

    if (!e || e->kind != NJ_LINK_FIELD_STATIC || !e->target) {
        return NULL;
    }
    f = (const nj_field *)e->target;
    if ((f->flags & NJ_ACC_STATIC) == 0u || !f->owner ||
        f->owner->state < NJ_CLASS_LINKED) {
        return NULL;
    }
    switch (f->ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:
    case NJ_PT_CHAR:
    case NJ_PT_SHORT:
    case NJ_PT_INT:
    case NJ_PT_FLOAT:
    case NJ_PT_OBJECT: slots = 1u; break;
    case NJ_PT_LONG:
    case NJ_PT_DOUBLE: slots = 2u; break;
    default:           return NULL;
    }
    if ((nj_u4)f->offset >= vm->nstatics ||
        (nj_u4)f->offset + slots > vm->nstatics) {
        return NULL;
    }
    return f;
}

struct nj_jit_entry;
typedef struct nj_jit_ticket {
    struct nj_jit_entry *entry;
    nj_u2 *cell;    /* this site's probe-map cell; valid until execute returns */
} nj_jit_ticket;
/* Probe mutates only bounded JIT metadata; it never touches frame slots, calls
 * the port, allocates, frees or enters Java. Consume the ticket immediately,
 * after SAVE, before another probe or any engine/lifetime change. */
int nj_jit_probe(nj_vm *vm, const nj_frame *fr, nj_u4 pc, nj_value *sp,
                 nj_u4 budget, nj_jit_ticket *ticket);
nj_u4 nj_jit_execute(nj_vm *vm, nj_frame *fr, nj_u4 *pc, nj_value **sp,
                     nj_u4 budget, const nj_jit_ticket *ticket);
/* Convenience for internal callers that have already published frame state.
 * Returns bytecodes executed, updating pc/sp only on success. */
nj_u4 nj_jit_try_block(nj_vm *vm, nj_frame *fr, nj_u4 *pc,
                       nj_value **sp, nj_u4 budget);
#endif
#endif
