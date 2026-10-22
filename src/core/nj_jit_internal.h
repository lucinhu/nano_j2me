/* Shared baseline-JIT/backend boundary. No backend owns VM state. */
#ifndef NJ_JIT_INTERNAL_H
#define NJ_JIT_INTERNAL_H

#include "nj_internal.h"

#if NJ_ENABLE_JIT
/* One bounded, straight-line block. Stack requirements are relative to its
 * incoming sp; native code only touches frame slots and caller-saved registers.
 * It never calls C, allocates, throws, branches to Java, or changes frames.
 *
 * A block may name up to NJ_JIT_MAX_FAULTS instructions whose guards can fail
 * at run time (null receiver, null/bounds/kind on an array, zero divisor). Such
 * a guard must jump to the block's fault exit *before* the instruction's first
 * side effect, and the exit returns
 *
 *     NJ_JIT_FAULT_BIT | (completed << 8) | fault_index
 *
 * where `completed` counts the bytecodes finished before the fault. The C
 * caller then sets pc to entry_pc + faults[i].pc_off, resets the operand stack
 * to entry_sp + faults[i].sp_delta, and lets the interpreter execute that
 * instruction again - so the Java-level exception and its PC come from the
 * interpreter, exactly as in a purely interpreted run. When the block cannot
 * stay inside NJ_JIT_MAX_FAULTS it ends before the next faulting instruction.
 *
 * A successful run returns the number of bytecodes it completed, which equals
 * insns unless a capacity-limited prefix shortened the block. */
typedef struct nj_jit_fault {
    nj_u2 pc_off;   /* faulting bytecode pc, relative to the block entry pc */
    nj_s2 sp_delta; /* operand stack at that pc, relative to incoming sp     */
} nj_jit_fault;

typedef struct nj_jit_block {
    nj_u4 next_pc;
    nj_u4 insns;
    int stack_delta;
    int min_stack;
    int max_growth;
    nj_size size;
    nj_u1 nfaults;
    nj_jit_fault faults[NJ_JIT_MAX_FAULTS];
} nj_jit_block;

/* Native block entry. Arguments are the interpreter's operand stack pointer
 * and frame locals pointer; neither pointer is advanced and no frame slot
 * outside the block's own operand-stack range is touched. The result word is
 * returned in the integer result register (a0/w0). */
typedef nj_u4 (*nj_jit_fn)(nj_value *incoming_sp, nj_value *locals);

#define NJ_JIT_FAULT_BIT 0x80000000u
#define NJ_JIT_FAULT_INDEX_MASK 0xffu
#define NJ_JIT_FAULT_COMPLETED_MASK 0xffu

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
           (op >= 0x36u && op <= 0x4eu) || (op >= 0x57u && op <= 0x84u) ||
           (op >= 0x91u && op <= 0x93u) || op == 0xc4u ||
           op == 0x2eu || op == 0x4fu ||           /* iaload / iastore */
           op == 0xb4u || op == 0xb5u ||           /* getfield / putfield */
           op == 0xbeu;                            /* arraylength */
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
    nj_u4 hash;
    if (!vm || !c || !vm->cp_cache || vm->cp_cache_size == 0u ||
        index == 0u || index > 0xffffu) {
        return NULL;
    }
    hash = nj_cp_key_hash(c, index) & ((nj_u4)vm->cp_cache_size - 1u);
    e = &vm->cp_cache[hash];
    if (e->used && e->cls == c && e->cp_index == index) {
        return e;
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
