/* Standalone RV32 emitter checks. Link with nj_jit_rv32.c only.
 * On RV32 Linux this executes the emitted bytes; other hosts explicitly run
 * encoding/metadata checks only. The small oracle is independent of lowering.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "nj_jit_internal.h"
#include "nj2me/nj_jit.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The check scripts link this encoder harness without the VM heap.  The JIT
 * runtime only needs a plain allocation for its metadata in that mode. */
#if defined(NJ_TEST_STANDALONE_JIT)
void *nj_host_alloc(nj_vm *vm, nj_size n)
{
    (void)vm;
    return malloc((size_t)n);
}

void nj_host_free(nj_vm *vm, void *p)
{
    (void)vm;
    free(p);
}

/* The runtime's engine vtable is retained by nj_jit_enable even though this
 * harness never enters the interpreter; provide the dispatch symbol. */
nj_status nj_interp_run(nj_vm *vm, nj_thread *th, int stop_depth)
{
    (void)vm;
    (void)th;
    (void)stop_depth;
    return NJ_ERR;
}
#endif

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#if NJ_JIT_HOST_RV32 && defined(__linux__)
#define EXECUTE_RV32 1
#else
#define EXECUTE_RV32 0
#endif

#if !NJ_ENABLE_JIT
#error "test_jit_rv32 requires NJ_ENABLE_JIT=1"
#endif

enum { STACK_SLOTS = 96, LOCAL_SLOTS = 260, BASE = 16 };
typedef struct test_frame {
    nj_value stack[STACK_SLOTS];
    nj_value locals[LOCAL_SLOTS];
} test_frame;

static const char *test_name;
static unsigned assertions, compiled, executed, seen_m, seen_f, seen_d, seen_c;
static unsigned seen_cli, seen_caddi;
/* NULL means "no VM": field and array opcodes have no operands to resolve and
 * must be declined, exactly like a host-side encoder check. Field/array tests
 * install a minimal fake VM with a resolved CP cache. */
static nj_vm *jit_vm;
static int analyze_method(const nj_method *m, nj_u4 pc, nj_jit_block *out)
{
    return nj_rv32_analyze(jit_vm, m, pc, out);
}
static int compile_method(const nj_method *m, nj_u4 pc, nj_u1 *buf,
                          nj_size capacity, nj_jit_block *out)
{
    return nj_rv32_compile(jit_vm, m, pc, buf, capacity, out);
}
#define CHECK(c) do { ++assertions; if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, \
            test_name ? test_name : "setup", #c); exit(1); } } while (0)

static nj_s4 signed_bits(nj_u4 u)
{
    nj_s4 s;
    memcpy(&s, &u, sizeof(s));
    return s;
}

static void init_frame(test_frame *f)
{
    unsigned char *bytes = (unsigned char *)f;
    size_t i;
    for (i = 0; i < sizeof(*f); ++i)
        bytes[i] = (unsigned char)(0xa5u ^ (i * 37u + i / 8u));
}

static nj_method method(const nj_u1 *code, size_t length, unsigned locals)
{
    nj_method m;
    memset(&m, 0, sizeof(m));
    m.code = code;
    m.code_len = (nj_u4)length;
    m.max_stack = STACK_SLOTS - BASE;
    m.max_locals = (nj_u2)locals;
    return m;
}

/* Compare the allocation-free scan against each emitted prefix. Capacity may
 * shorten compilation, but must never change stack metadata for that prefix. */
static void same_metadata(const nj_jit_block *a, const nj_jit_block *b)
{
    unsigned i;
    CHECK(a->next_pc == b->next_pc && a->insns == b->insns);
    CHECK(a->stack_delta == b->stack_delta && a->min_stack == b->min_stack);
    CHECK(a->max_growth == b->max_growth && a->nfaults == b->nfaults);
    for (i = 0; i < a->nfaults; ++i) {
        CHECK(a->faults[i].pc_off == b->faults[i].pc_off);
        CHECK(a->faults[i].sp_delta == b->faults[i].sp_delta);
    }
}
static int compile_checked(const nj_method *m, nj_u4 pc, nj_u1 *buf,
                           nj_size capacity, nj_jit_block *out)
{
    nj_jit_block analysis, prefix;
    int scanned = analyze_method(m, pc, &analysis);
    int emitted = compile_method(m, pc, buf, capacity, out);
    if (scanned) CHECK(analysis.size == 0 && analysis.insns > 0);
    if (emitted) {
        nj_method limited = *m;
        CHECK(scanned && out->next_pc <= analysis.next_pc);
        limited.code_len = out->next_pc;
        CHECK(analyze_method(&limited, pc, &prefix));
        CHECK(prefix.size == 0);
        /* A branch whose target is exactly the prefix cut makes the truncated
         * scan stop at the branch; that is a truncation artefact, not a
         * metadata disagreement. The full-analysis comparison below still
         * checks the emitted block. */
        if (prefix.next_pc == out->next_pc) same_metadata(out, &prefix);
        if (out->next_pc == analysis.next_pc) same_metadata(out, &analysis);
    } else if (buf && out && capacity >= NJ_JIT_MAX_CODE_BYTES) {
        CHECK(!scanned); /* One supported instruction must fit the full buffer. */
    }
    return emitted;
}

static nj_u4 word(const nj_u1 *p)
{
    return (nj_u4)p[0] | (nj_u4)p[1] << 8 |
           (nj_u4)p[2] << 16 | (nj_u4)p[3] << 24;
}

/* Inspect every emitted instruction, not just the first arithmetic opcode.
 * This also catches accidental extension use in prologues or epilogues. */
static void inspect(const nj_u1 *code, size_t length)
{
    size_t p = 0;
    int last_return = 0;
    CHECK(length >= 2);
    while (p < length) {
        nj_u4 insn;
        unsigned op, funct3, funct7;
        CHECK(p + 2 <= length);
        last_return = 0;
        if ((code[p] & 3u) != 3u) {
            unsigned half = code[p] | (unsigned)code[p + 1] << 8;
            CHECK(NJ_JIT_RV32_C);
            CHECK(half != 0 && half != 0xffffu);
            ++seen_c;
            if ((half & 0xe003u) == 0x4001u) ++seen_cli;
            if ((half & 0xe003u) == 0x0001u) ++seen_caddi;
            last_return = half == 0x8082u; /* c.jr ra */
            p += 2;
            continue;
        }
        CHECK(p + 4 <= length);
        insn = word(code + p);
        op = insn & 0x7fu;
        funct3 = (insn >> 12) & 7u;
        funct7 = insn >> 25;
        if (op == 0x33u && funct7 == 1) {
            CHECK(NJ_JIT_RV32_M);
            ++seen_m;
        }
        if (op == 0x2fu) CHECK(NJ_JIT_RV32_A);
        if (op == 0x07u || op == 0x27u || op == 0x53u) {
            int is_double = op == 0x53u ? (int)(funct7 & 1u) : funct3 == 3;
            CHECK(NJ_JIT_RV32_F);
            if (is_double) { CHECK(NJ_JIT_RV32_D); ++seen_d; }
            else ++seen_f;
            /* Arithmetic must encode RNE explicitly, never dynamic frm. */
            if (op == 0x53u && (funct7 & ~1u) <= 0x0cu)
                CHECK(funct3 == 0);
        }
        CHECK(op != 0x6fu); /* no calls */
        if (op == 0x63u) {
            /* Fault guards are forward branches to an in-block exit stub. */
            nj_u4 bits = ((((insn >> 31) & 1u) << 12) |
                          (((insn >> 7) & 1u) << 11) |
                          (((insn >> 25) & 0x3fu) << 5) |
                          (((insn >> 8) & 0xfu) << 1));
            /* Sign-extend without signed left shifts (UB under UBSan). */
            nj_s4 imm = (bits & 0x1000u) ? (nj_s4)(bits | 0xffffe000u)
                                         : (nj_s4)bits;
            CHECK(imm != 0);
            CHECK((nj_size)((nj_s4)p + imm) < length);
        }
        if (op == 0x67u) {
            CHECK(insn == 0x00008067u);
            last_return = 1;
        }
        p += 4;
    }
    CHECK(p == length);
    CHECK(last_return);
}

#if EXECUTE_RV32
static void *exec_page;
static size_t exec_size;

/* ABI v3: fn(incoming_sp, locals, budget) returns either the success word
 * (completed << 4) | exit_index or NJ_JIT_FAULT_BIT | (index << 24) | completed.
 * The helpers below decode both forms so callers never inspect raw bits. */
static nj_u4 invoke(const nj_u1 *code, size_t length, nj_value *sp,
                    nj_value *locals, nj_u4 budget)
{
    nj_jit_fn fn;
    nj_u4 result;
    if (!exec_page) {
        long page = sysconf(_SC_PAGESIZE);
        CHECK(page > 0);
        exec_size = (NJ_JIT_MAX_CODE_BYTES + (size_t)page - 1) /
                    (size_t)page * (size_t)page;
        exec_page = mmap(NULL, exec_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(exec_page != MAP_FAILED);
    } else CHECK(mprotect(exec_page, exec_size, PROT_READ | PROT_WRITE) == 0);
    CHECK(length <= exec_size);
    memcpy(exec_page, code, length);
    CHECK(mprotect(exec_page, exec_size, PROT_READ | PROT_EXEC) == 0);
    __builtin___clear_cache((char *)exec_page, (char *)exec_page + length);
    CHECK(sizeof(fn) == sizeof(exec_page));
    memcpy(&fn, &exec_page, sizeof(fn));
    result = fn(sp, locals, budget);
    ++executed;
    return result;
}
static int result_is_fault(nj_u4 word) { return (word & NJ_JIT_FAULT_BIT) != 0u; }
static unsigned result_completed(nj_u4 word)
{
    return result_is_fault(word)
               ? (unsigned)(word & NJ_JIT_FAULT_COMPLETED_MASK)
               : (unsigned)(word >> NJ_JIT_EXIT_SHIFT);
}
static unsigned result_exit(nj_u4 word)
{
    return (unsigned)(word & NJ_JIT_EXIT_INDEX_MASK);
}
static unsigned result_fault_index(nj_u4 word)
{
    return (unsigned)((word >> NJ_JIT_FAULT_INDEX_SHIFT) & NJ_JIT_FAULT_INDEX_MASK);
}
/* A straight-line block compiled at pc 0 must leave through exits[0]. */
static void expect_success(nj_u4 word, const nj_jit_block *block,
                           unsigned completed)
{
    CHECK(!result_is_fault(word));
    CHECK(result_exit(word) == 0u);
    CHECK(result_completed(word) == completed);
    CHECK(block->exits[0].pc_off == block->next_pc);
    CHECK(block->exits[0].sp_delta == block->stack_delta);
}
#endif

/* A deliberately direct JVM oracle. Unsigned arithmetic models Java wrapping;
 * division handles MIN/-1 without invoking undefined C signed division. */
static int reference(const nj_u1 *code, size_t length, nj_value *stack,
                     nj_value *locals, int sp, const nj_class *owner, unsigned *count)
{
    size_t pc = 0;
    unsigned steps = 0;
    *count = 0;
    while (pc < length) {
        unsigned op = code[pc++], index = 0;
        int wide = op == 0xc4u;
        nj_u4 a, b, result;
        if (wide) op = code[pc++];
        if (++steps > 1000000u) { CHECK(!"oracle step limit"); break; }
        if (op == 0xacu) break; /* return: stop without counting */
        ++*count;
        if ((op >= 0x15u && op <= 0x19u) ||
            (op >= 0x36u && op <= 0x3au) || op == 0x84u) {
            index = code[pc++];
            if (wide) index = index * 256u + code[pc++];
        } else if (op >= 0x1au && op <= 0x2du) {
            index = (op - 0x1au) % 4u;
            op = 0x15u + (op - 0x1au) / 4u;
        } else if (op >= 0x3bu && op <= 0x4eu) {
            index = (op - 0x3bu) % 4u;
            op = 0x36u + (op - 0x3bu) / 4u;
        }
        if (op >= 0x02u && op <= 0x08u) {
            stack[sp++].i = (int)op - 3;
        } else if (op == 0x09u || op == 0x0au) {
            stack[sp].j = op - 0x09u;
            stack[sp + 1].bits = 0; sp += 2;
        } else if (op >= 0x0bu && op <= 0x0du) {
            stack[sp++].f = (float)(op - 0x0bu);
        } else if (op == 0x0eu || op == 0x0fu) {
            stack[sp].d = (double)(op - 0x0eu);
            stack[sp + 1].bits = 0; sp += 2;
        } else if (op == 0x10u) {
            stack[sp++].i = (int8_t)code[pc++];
        } else if (op == 0x11u) {
            int v = (int)code[pc] * 256 + code[pc + 1]; pc += 2;
            stack[sp++].i = (int16_t)v;
        } else if (op == 0x12u || op == 0x13u) {
            unsigned cp_index = code[pc++], off;
            if (op == 0x13u) cp_index = cp_index * 256u + code[pc++];
            CHECK(owner && owner->cp_info && owner->data);
            CHECK(cp_index > 0 && cp_index < owner->cp_count);
            off = owner->cp_info[cp_index] >> 4;
            CHECK(off > 0 && off <= owner->data_len && owner->data_len - off >= 4);
            /* Independent big-endian class-file read, with no resolver/helper. */
            stack[sp++].bits = (nj_u4)owner->data[off] << 24 |
                (nj_u4)owner->data[off + 1] << 16 |
                (nj_u4)owner->data[off + 2] << 8 | owner->data[off + 3];
        } else if (op >= 0x15u && op <= 0x19u) {
            if (op == 0x16u || op == 0x18u) {
                stack[sp] = locals[index];
                stack[sp + 1].bits = 0; sp += 2;
            } else if (op == 0x19u) stack[sp++].r = locals[index].r;
            else stack[sp++].bits = locals[index].bits;
        } else if (op >= 0x36u && op <= 0x3au) {
            if (op == 0x37u || op == 0x39u) {
                sp -= 2; locals[index] = stack[sp];
                locals[index + 1].bits = 0;
            } else if (op == 0x3au) locals[index].r = stack[--sp].r;
            else locals[index].bits = stack[--sp].bits;
        } else if (op == 0x84u) {
            int increment;
            if (wide) {
                increment = (int16_t)((unsigned)code[pc] * 256 + code[pc + 1]);
                pc += 2;
            } else increment = (int8_t)code[pc++];
            locals[index].bits += (nj_u4)increment;
        } else if (op == 0x57u) --sp;
        else if (op == 0x58u) sp -= 2;
        else if (op == 0x59u) { stack[sp] = stack[sp - 1]; ++sp; }
        else if (op == 0x5cu) {
            stack[sp] = stack[sp - 2];
            stack[sp + 1] = stack[sp - 1]; sp += 2;
        } else if (op == 0x5fu) {
            nj_value tmp = stack[sp - 1];
            stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = tmp;
        } else if (op == 0x00u) { /* nop */ }
        else if (op == 0x01u) stack[sp++].bits = 0;
        else if (op == 0xa7u) {
            nj_s4 target = (nj_s4)(pc - 1) +
                (int16_t)((unsigned)code[pc] << 8 | code[pc + 1]);
            CHECK(target >= 0 && (size_t)target <= length);
            pc = (size_t)target;
        } else if (op >= 0x99u && op <= 0x9eu) {
            int value = stack[--sp].i;
            int taken = op == 0x99u ? value == 0 :
                        op == 0x9au ? value != 0 :
                        op == 0x9bu ? value < 0 :
                        op == 0x9cu ? value >= 0 :
                        op == 0x9du ? value > 0 : value <= 0;
            nj_s4 target = (nj_s4)(pc - 1) +
                (int16_t)((unsigned)code[pc] << 8 | code[pc + 1]);
            CHECK(target >= 0 && (size_t)target <= length);
            pc = taken ? (size_t)target : pc + 2;
        } else if (op >= 0x9fu && op <= 0xa4u) {
            int rhs = stack[--sp].i, lhs = stack[--sp].i;
            int taken = op == 0x9fu ? lhs == rhs :
                        op == 0xa0u ? lhs != rhs :
                        op == 0xa1u ? lhs < rhs :
                        op == 0xa2u ? lhs >= rhs :
                        op == 0xa3u ? lhs > rhs : lhs <= rhs;
            nj_s4 target = (nj_s4)(pc - 1) +
                (int16_t)((unsigned)code[pc] << 8 | code[pc + 1]);
            CHECK(target >= 0 && (size_t)target <= length);
            pc = taken ? (size_t)target : pc + 2;
        } else if (op == 0xa5u || op == 0xa6u) {
            nj_ref rhs = stack[--sp].r, lhs = stack[--sp].r;
            int taken = (lhs == rhs) == (op == 0xa5u);
            nj_s4 target = (nj_s4)(pc - 1) +
                (int16_t)((unsigned)code[pc] << 8 | code[pc + 1]);
            CHECK(target >= 0 && (size_t)target <= length);
            pc = taken ? (size_t)target : pc + 2;
        } else if (op == 0xc6u || op == 0xc7u) {
            nj_ref value = stack[--sp].r;
            int taken = (value == NULL) == (op == 0xc6u);
            nj_s4 target = (nj_s4)(pc - 1) +
                (int16_t)((unsigned)code[pc] << 8 | code[pc + 1]);
            CHECK(target >= 0 && (size_t)target <= length);
            pc = taken ? (size_t)target : pc + 2;
        }
        else if (op == 0x74u) stack[sp - 1].bits = 0u - stack[sp - 1].bits;
        else if (op >= 0x91u && op <= 0x93u) {
            a = stack[sp - 1].bits;
            stack[sp - 1].i = op == 0x91u ? (int8_t)a :
                                 op == 0x92u ? (uint16_t)a : (int16_t)a;
        } else if (op == 0x76u) stack[sp - 1].bits ^= 0x80000000u;
        else if (op == 0x77u) {
            uint64_t bits;
            memcpy(&bits, &stack[sp - 2], 8);
            bits ^= UINT64_C(0x8000000000000000);
            memcpy(&stack[sp - 2], &bits, 8);
        } else if (op == 0x62u || op == 0x66u || op == 0x6au || op == 0x6eu) {
            volatile float x = stack[sp - 2].f, y = stack[sp - 1].f;
            float z = op == 0x62u ? x + y : op == 0x66u ? x - y :
                      op == 0x6au ? x * y : x / y;
            stack[sp - 2].f = z; --sp;
        } else if (op == 0x63u || op == 0x67u || op == 0x6bu || op == 0x6fu) {
            volatile double x = stack[sp - 4].d, y = stack[sp - 2].d;
            double z = op == 0x63u ? x + y : op == 0x67u ? x - y :
                       op == 0x6bu ? x * y : x / y;
            stack[sp - 4].d = z;
            stack[sp - 3].bits = 0; sp -= 2;
        } else {
            a = stack[sp - 2].bits; b = stack[sp - 1].bits;
            switch (op) {
            case 0x60: result = a + b; break;
            case 0x64: result = a - b; break;
            case 0x68: result = a * b; break;
            case 0x6c: case 0x70:
                CHECK(b != 0);
                if (a == 0x80000000u && b == UINT32_MAX)
                    result = op == 0x6cu ? a : 0;
                else if (op == 0x6cu)
                    result = (nj_u4)(signed_bits(a) / signed_bits(b));
                else result = (nj_u4)(signed_bits(a) % signed_bits(b));
                break;
            case 0x78: result = a << (b & 31u); break;
            case 0x7a:
                result = a >> (b & 31u);
                if ((a & 0x80000000u) && (b & 31u))
                    result |= UINT32_MAX << (32u - (b & 31u));
                break;
            case 0x7c: result = a >> (b & 31u); break;
            case 0x7e: result = a & b; break;
            case 0x80: result = a | b; break;
            case 0x82: result = a ^ b; break;
            default: CHECK(!"oracle missing opcode"); result = 0;
            }
            stack[sp - 2].bits = result; --sp;
        }
        CHECK(sp >= 0 && sp < STACK_SLOTS);
    }
    return sp;
}

#if EXECUTE_RV32
static void compare_frames(const char *name, const test_frame *actual,
                           const test_frame *expected);
#endif

static void program_owned(const char *name, const nj_u1 *code, size_t length,
                          unsigned local_count, int depth, const test_frame *initial,
                          nj_class *owner)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    nj_method m = method(code, length, local_count);
    test_frame expected = *initial;
    unsigned count;
    int final_sp;
    test_name = name;
    m.owner = owner;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 1);
    CHECK(block.next_pc == length);
    CHECK(block.size <= sizeof(generated));
    CHECK(block.min_stack <= depth);
    CHECK(BASE + depth + block.max_growth <= STACK_SLOTS);
    inspect(generated, block.size);
    final_sp = reference(code, length, expected.stack, expected.locals, BASE + depth, owner, &count);
    CHECK(block.insns == count);
    CHECK(block.stack_delta == final_sp - BASE - depth);
    ++compiled;
#if EXECUTE_RV32
    {
        test_frame actual = *initial;
        nj_u4 done = invoke(generated, block.size, &actual.stack[BASE + depth],
                            actual.locals, block.insns);
        expect_success(done, &block, block.insns);
        /* Compare all bytes, including untouched high halves and dead slots. */
        compare_frames(name, &actual, &expected);
    }
#endif
}

static void program(const char *name, const nj_u1 *code, size_t length,
                    unsigned local_count, int depth, const test_frame *initial)
{
    program_owned(name, code, length, local_count, depth, initial, NULL);
}

static void declined(const char *name, const nj_u1 *code, size_t length,
                     unsigned locals)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    nj_method m = method(code, length, locals);
    nj_u1 prefix[16];
    test_name = name;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 0);
    CHECK(length + 1 <= sizeof(prefix));
    prefix[0] = 0x03; /* An accepted prefix must stop before the declined opcode. */
    memcpy(prefix + 1, code, length);
    m.code = prefix;
    m.code_len = (nj_u4)length + 1;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 1);
    CHECK(block.next_pc == 1 && block.insns == 1 && block.stack_delta == 1);
    CHECK(block.min_stack == 0 && block.max_growth == 1 && !block.nfaults);
    inspect(generated, block.size);
}


#if EXECUTE_RV32
/* Operand-stack slots at or above the block's final sp are dead: the
 * generated code may use them as scratch and leave different bytes there than
 * the interpreter. Everything below sp (plus the locals array, which a block
 * may scroll back to its incoming entry) is live and must match exactly. */
static void compare_live_frames(const char *name, const test_frame *actual,
                                const test_frame *expected, int live_slots)
{
    test_frame a, e;
    if (live_slots < 0 || live_slots > STACK_SLOTS) {
        CHECK(!"live stack depth out of range");
        return;
    }
    a = *actual;
    e = *expected;
    memset(&a.stack[live_slots], 0, (size_t)(STACK_SLOTS - live_slots) *
           sizeof(a.stack[0]));
    memset(&e.stack[live_slots], 0, (size_t)(STACK_SLOTS - live_slots) *
           sizeof(e.stack[0]));
    compare_frames(name, &a, &e);
}

static void compare_frames(const char *name, const test_frame *actual,
                           const test_frame *expected)
{
    size_t i;
    if (memcmp(actual, expected, sizeof(*actual)) == 0) return;
    {
        const unsigned char *a = (const unsigned char *)actual;
        const unsigned char *b = (const unsigned char *)expected;
        for (i = 0; i < sizeof(*actual); ++i) if (a[i] != b[i]) {
            fprintf(stderr, "%s: frame byte %lu got %02x expected %02x\n",
                    name, (unsigned long)i, a[i], b[i]);
            break;
        }
    }
    CHECK(!"generated code differs from oracle");
}
#endif

/* Compile a block that must be declined. The VM (and its CP cache) is set by
 * the caller; only field/array tests need one. */
static void declined_owned(const char *name, nj_class *owner,
                           const nj_u1 *code, size_t length, unsigned locals)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    nj_method m = method(code, length, locals);
    m.owner = owner;
    test_name = name;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 0);
    CHECK(block.nfaults == 0);
}

/* Run a block whose oracle is built by hand (field/array/fault cases). */
static nj_u4 run_expect(const char *name, const nj_u1 *code, size_t length,
                        unsigned local_count, int depth, nj_class *owner,
                        const test_frame *initial, const test_frame *expected,
                        nj_jit_block *out)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_method m = method(code, length, local_count);
    nj_u4 ret = 0;
    (void)initial;
    (void)expected;
    m.owner = owner;
    test_name = name;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), out) == 1);
    CHECK(out->next_pc <= length);
    inspect(generated, out->size);
    ++compiled;
#if EXECUTE_RV32
    {
        test_frame actual = *initial;
        ret = invoke(generated, out->size, &actual.stack[BASE + depth],
                     actual.locals, out->insns);
        compare_frames(name, &actual, expected);
    }
#else
    (void)depth;
#endif
    return ret;
}

#if EXECUTE_RV32
static void expect_fault(nj_u4 word, unsigned index, unsigned completed)
{
    CHECK((word & NJ_JIT_FAULT_BIT) != 0u);
    CHECK(((word >> NJ_JIT_FAULT_INDEX_SHIFT) & NJ_JIT_FAULT_INDEX_MASK) ==
          index);
    CHECK((word & NJ_JIT_FAULT_COMPLETED_MASK) == completed);
}
#endif

/* ---- instance fields ------------------------------------------------- */

enum { FIELD_CACHE = 64, FIELD_OBJ_SIZE = 96 };

typedef struct field_fixture {
    nj_vm vm;
    nj_class owner;
    nj_cpentry cache[FIELD_CACHE];
    nj_field field;
    _Alignas(8) nj_u1 object[FIELD_OBJ_SIZE];
} field_fixture;

static unsigned field_offset(void)
{
    return ((unsigned)sizeof(nj_object) + 7u) & ~7u;
}

static unsigned field_bytes(unsigned ptype)
{
    switch (ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:   return 1u;
    case NJ_PT_CHAR:
    case NJ_PT_SHORT:  return 2u;
    case NJ_PT_LONG:
    case NJ_PT_DOUBLE: return 8u;
    default:           return 4u;
    }
}

static void field_fixture_init(field_fixture *fx, unsigned ptype, unsigned width,
                               unsigned offset)
{
    nj_u4 slot;
    memset(fx, 0, sizeof(*fx));
    fx->vm.cp_cache = fx->cache;
    fx->vm.cp_cache_size = FIELD_CACHE;
    fx->owner.state = NJ_CLASS_LINKED;
    fx->owner.instance_size = (nj_u4)sizeof(fx->object);
    fx->field.owner = &fx->owner;
    fx->field.ptype = (nj_u1)ptype;
    fx->field.width = (nj_u1)width;
    fx->field.offset = (nj_u2)offset;
    slot = nj_cp_key_hash(&fx->owner, 1u) & (FIELD_CACHE - 1u);
    fx->cache[slot].cls = &fx->owner;
    fx->cache[slot].cp_index = 1u;
    fx->cache[slot].kind = NJ_LINK_FIELD_INST;
    fx->cache[slot].target = &fx->field;
    fx->cache[slot].used = 1u;
    memset(fx->object, 0xa5, sizeof(fx->object));
    {
        nj_object *o = (nj_object *)(void *)fx->object;
        o->kind = NJ_OBJ_PLAIN;
        o->size = (nj_u4)sizeof(fx->object);
    }
}

static void field_code(nj_u1 *code, unsigned op)
{
    code[0] = (nj_u1)op;
    code[1] = 0u;
    code[2] = 1u;
}

static void field_read_case(const char *name, unsigned ptype, unsigned width,
                            nj_u8 raw)
{
    field_fixture fx;
    nj_u1 code[3];
    nj_u1 before[FIELD_OBJ_SIZE];
    test_frame initial, expected;
    nj_jit_block block;
    unsigned offset = field_offset();
    int wide = width == 2u;
    nj_u4 ret;
    field_fixture_init(&fx, ptype, width, offset);
    CHECK(nj_jit_instance_field(&fx.vm, &fx.owner, 1u) == &fx.field);
    memcpy(fx.object + offset, &raw, field_bytes(ptype));
    memcpy(before, fx.object, sizeof(before));
    field_code(code, 0xb4);
    init_frame(&initial);
    initial.stack[BASE].r = (nj_ref)(void *)fx.object;
    expected = initial;
    if (wide) {
        memcpy(&expected.stack[BASE], &raw, 8u);
        expected.stack[BASE + 1u].bits = 0u;
    } else if (ptype == NJ_PT_BOOLEAN || ptype == NJ_PT_BYTE) {
        expected.stack[BASE].i = (nj_s4)(nj_s1)raw;
    } else if (ptype == NJ_PT_CHAR) {
        expected.stack[BASE].i = (nj_s4)(nj_u2)raw;
    } else if (ptype == NJ_PT_SHORT) {
        expected.stack[BASE].i = (nj_s4)(nj_s2)raw;
    } else if (ptype == NJ_PT_OBJECT) {
        nj_ref r = (nj_ref)(uintptr_t)raw;
        memcpy(&expected.stack[BASE].r, &r, sizeof(r));
    } else {
        expected.stack[BASE].i = (nj_s4)(nj_u4)raw;
    }
    jit_vm = &fx.vm;
    ret = run_expect(name, code, sizeof(code), 1, 1, &fx.owner, &initial,
                     &expected, &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 3 && block.insns == 1);
    CHECK(block.stack_delta == (wide ? 1 : 0) && block.min_stack == 1);
    CHECK(block.max_growth == (wide ? 1 : 0));
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    expect_success(ret, &block, block.insns);
#endif
    CHECK(memcmp(fx.object, before, sizeof(before)) == 0);
    (void)ret;
}

static void field_write_case(const char *name, unsigned ptype, unsigned width,
                             nj_u8 raw)
{
    field_fixture fx;
    nj_u1 code[3];
    nj_u1 expected_obj[FIELD_OBJ_SIZE];
    test_frame initial, expected;
    nj_jit_block block;
    unsigned offset = field_offset();
    unsigned bytes = field_bytes(ptype);
    int wide = width == 2u;
    int depth = 1 + (int)width;
    nj_u4 ret;
    field_fixture_init(&fx, ptype, width, offset);
    CHECK(nj_jit_instance_field(&fx.vm, &fx.owner, 1u) == &fx.field);
    memcpy(expected_obj, fx.object, sizeof(expected_obj));
    memcpy(expected_obj + offset, &raw, bytes);
    field_code(code, 0xb5);
    init_frame(&initial);
    initial.stack[BASE].r = (nj_ref)(void *)fx.object;
    if (wide) {
        memcpy(&initial.stack[BASE + 1], &raw, 8u);
    } else if (ptype == NJ_PT_OBJECT) {
        nj_ref r = (nj_ref)(uintptr_t)raw;
        memcpy(&initial.stack[BASE + 1].r, &r, sizeof(r));
    } else {
        initial.stack[BASE + 1].i = (nj_s4)(nj_u4)raw;
    }
    expected = initial;
    jit_vm = &fx.vm;
    ret = run_expect(name, code, sizeof(code), 1, depth, &fx.owner, &initial,
                     &expected, &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 3 && block.insns == 1);
    CHECK(block.stack_delta == -depth && block.min_stack == depth);
    CHECK(block.max_growth == 0);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    expect_success(ret, &block, block.insns);
    CHECK(memcmp(fx.object, expected_obj, sizeof(expected_obj)) == 0);
#else
    (void)expected_obj;
#endif
    (void)ret;
}

static void field_fault_case(const char *name, unsigned op, int put)
{
    field_fixture fx;
    nj_u1 code[3];
    nj_u1 before[FIELD_OBJ_SIZE];
    test_frame initial, expected;
    nj_jit_block block;
    unsigned offset = field_offset();
    int depth = put ? 2 : 1;
    nj_u4 ret;
    field_fixture_init(&fx, NJ_PT_INT, 1u, offset);
    memcpy(fx.object + offset, "\x11\x22\x33\x44", 4u);
    memcpy(before, fx.object, sizeof(before));
    field_code(code, op);
    init_frame(&initial);
    initial.stack[BASE].r = NULL;
    if (put) initial.stack[BASE + 1].i = 0x55667788;
    expected = initial;
    jit_vm = &fx.vm;
    ret = run_expect(name, code, sizeof(code), 1, depth, &fx.owner, &initial,
                     &expected, &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 3 && block.insns == 1);
    CHECK(block.stack_delta == (put ? -2 : 0) && block.min_stack == depth);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    expect_fault(ret, 0u, 0u);
#endif
    CHECK(memcmp(fx.object, before, sizeof(before)) == 0);
    (void)ret;
}

static void field_decline_case(const char *name, int which)
{
    field_fixture fx;
    nj_u1 code[3];
    unsigned offset = field_offset();
    unsigned slot;
    field_fixture_init(&fx, NJ_PT_INT, 1u, offset);
    slot = nj_cp_key_hash(&fx.owner, 1u) & (FIELD_CACHE - 1u);
    field_code(code, 0xb4);
    switch (which) {
    case 0: /* no resolved entry */
        fx.cache[slot].used = 0u;
        break;
    case 1: /* static link for getfield */
        fx.cache[slot].kind = NJ_LINK_FIELD_STATIC;
        break;
    case 2: /* field declared static */
        fx.field.flags = NJ_ACC_STATIC;
        break;
    case 3: /* owner is not linked yet */
        fx.owner.state = NJ_CLASS_LOADED;
        break;
    case 4: /* payload starts inside the object header */
        fx.field.offset = (nj_u2)(sizeof(nj_object) - 1u);
        break;
    case 5: /* payload runs past instance_size */
        fx.owner.instance_size = offset + 3u;
        break;
    case 6: /* unknown primitive type */
        fx.field.ptype = 0xffu;
        break;
    case 7: /* CP index 0 is invalid */
        code[2] = 0u;
        break;
    case 8: /* no CP cache at all */
        fx.vm.cp_cache = NULL;
        fx.vm.cp_cache_size = 0u;
        break;
    default: /* truncated getfield operand */
        code[2] = 0u;
        break;
    }
    jit_vm = &fx.vm;
    if (which == 9)
        declined_owned(name, &fx.owner, code, 2u, 1);
    else
        declined_owned(name, &fx.owner, code, sizeof(code), 1);
    jit_vm = NULL;
}

/* ---- int arrays ------------------------------------------------------ */

enum { ARRAY_LEN = 8 };

typedef struct array_fixture {
    nj_vm vm;
    nj_object arr;
    nj_s4 ints[ARRAY_LEN];
} array_fixture;

static void array_fixture_init(array_fixture *fx, unsigned kind, unsigned aflags)
{
    unsigned i;
    memset(fx, 0, sizeof(*fx));
    for (i = 0; i < ARRAY_LEN; ++i)
        fx->ints[i] = (nj_s4)(0x10203040u + i * 0x01010101u);
    fx->arr.kind = (nj_u1)kind;
    fx->arr.aflags = (nj_u2)aflags;
    fx->arr.length = ARRAY_LEN;
    fx->arr.size = (nj_u4)sizeof(fx->ints);
    fx->arr.u.bytes = (nj_u1 *)(void *)fx->ints;
}

static void array_load_case(const char *name, array_fixture *fx, nj_ref ref,
                            unsigned index, int fault)
{
    static const nj_u1 code[] = {0x2e};
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;
    init_frame(&initial);
    initial.stack[BASE].r = ref;
    initial.stack[BASE + 1].i = (nj_s4)index;
    expected = initial;
    if (!fault) expected.stack[BASE].i = fx->ints[index];
    jit_vm = &fx->vm;
    ret = run_expect(name, code, sizeof(code), 1, 2, NULL, &initial, &expected,
                     &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 1 && block.insns == 1);
    CHECK(block.stack_delta == -1 && block.min_stack == 2);
    CHECK(block.max_growth == 0);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    if (fault) expect_fault(ret, 0u, 0u);
    else expect_success(ret, &block, block.insns);
#endif
    (void)ret;
}

static void array_store_case(const char *name, array_fixture *fx, nj_ref ref,
                             unsigned index, nj_s4 value, int fault)
{
    static const nj_u1 code[] = {0x4f};
    test_frame initial, expected;
    nj_jit_block block;
    nj_s4 want[ARRAY_LEN];
    unsigned i;
    nj_u4 ret;
    memcpy(want, fx->ints, sizeof(want));
    if (!fault) want[index] = value;
    init_frame(&initial);
    /* Java iastore order is arrayref, index, value: value on top. */
    initial.stack[BASE].r = ref;
    initial.stack[BASE + 1].i = (nj_s4)index;
    initial.stack[BASE + 2].i = value;
    expected = initial;
    jit_vm = &fx->vm;
    ret = run_expect(name, code, sizeof(code), 1, 3, NULL, &initial, &expected,
                     &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 1 && block.insns == 1);
    CHECK(block.stack_delta == -3 && block.min_stack == 3);
    CHECK(block.max_growth == 0);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    if (fault) expect_fault(ret, 0u, 0u);
    else expect_success(ret, &block, block.insns);
    for (i = 0; i < ARRAY_LEN; ++i) CHECK(fx->ints[i] == want[i]);
#else
    (void)ret;
    (void)want;
    (void)i;
#endif
}

static void array_length_case(const char *name, array_fixture *fx, nj_ref ref,
                              int fault)
{
    static const nj_u1 code[] = {0xbe};
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;
    init_frame(&initial);
    initial.stack[BASE].r = ref;
    expected = initial;
    if (!fault) expected.stack[BASE].i = (nj_s4)fx->arr.length;
    jit_vm = &fx->vm;
    ret = run_expect(name, code, sizeof(code), 1, 1, NULL, &initial, &expected,
                     &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 1 && block.insns == 1);
    CHECK(block.stack_delta == 0 && block.min_stack == 1);
    CHECK(block.max_growth == 0);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    if (fault) expect_fault(ret, 0u, 0u);
    else expect_success(ret, &block, block.insns);
#endif
    (void)ret;
}

static void test_fields_arrays(void)
{
    unsigned offset = field_offset();
    field_fixture fx;
    array_fixture ax;
    unsigned i;
    nj_u1 code[3];

    test_name = "field and array decline without a VM";
    jit_vm = NULL;
    field_code(code, 0xb4);
    declined("getfield without vm", code, 3, 1);
    field_code(code, 0xb5);
    declined("putfield without vm", code, 3, 1);
    {
        static const nj_u1 ops[] = {0x2e,0x4f,0xbe};
        for (i = 0; i < sizeof(ops); ++i)
            declined("array opcode without vm", &ops[i], 1, 1);
    }

    test_name = "getfield primitive widths";
    field_read_case("getfield boolean", NJ_PT_BOOLEAN, 1u, 0x81u);
    field_read_case("getfield byte", NJ_PT_BYTE, 1u, 0x81u);
    field_read_case("getfield char", NJ_PT_CHAR, 1u, 0xfffeu);
    field_read_case("getfield short", NJ_PT_SHORT, 1u, 0x8001u);
    field_read_case("getfield int", NJ_PT_INT, 1u, 0x80000001u);
    field_read_case("getfield float", NJ_PT_FLOAT, 1u, 0x7fc12345u);
    field_read_case("getfield long", NJ_PT_LONG, 2u, UINT64_C(0x8000000000000001));
    field_read_case("getfield double", NJ_PT_DOUBLE, 2u,
                    UINT64_C(0xf0123456789abcde));
    field_read_case("getfield object", NJ_PT_OBJECT, 1u, 0x12345678u);

    test_name = "putfield primitive widths";
    field_write_case("putfield boolean", NJ_PT_BOOLEAN, 1u, 0x1234u);
    field_write_case("putfield byte", NJ_PT_BYTE, 1u, 0x1234u);
    field_write_case("putfield char", NJ_PT_CHAR, 1u, 0x12345u);
    field_write_case("putfield short", NJ_PT_SHORT, 1u, 0x12345u);
    field_write_case("putfield int", NJ_PT_INT, 1u, 0x89abcdefu);
    field_write_case("putfield float", NJ_PT_FLOAT, 1u, 0x7fc12345u);
    field_write_case("putfield long", NJ_PT_LONG, 2u, UINT64_C(0x0123456789abcdef));
    field_write_case("putfield double", NJ_PT_DOUBLE, 2u,
                     UINT64_C(0xfedcba9876543210));
    field_write_case("putfield object", NJ_PT_OBJECT, 1u, 0x12345678u);

    test_name = "field null receiver faults";
    field_fault_case("getfield null receiver", 0xb4, 0);
    field_fault_case("putfield null receiver", 0xb5, 1);
    field_fault_case("getfield null receiver again", 0xb4, 0);

    test_name = "field declines";
    field_decline_case("getfield unresolved", 0);
    field_decline_case("getfield static link", 1);
    field_decline_case("getfield static field", 2);
    field_decline_case("getfield owner unlinked", 3);
    field_decline_case("getfield offset in header", 4);
    field_decline_case("getfield offset past instance", 5);
    field_decline_case("getfield unknown ptype", 6);
    field_decline_case("getfield cp index zero", 7);
    field_decline_case("getfield no cp cache", 8);
    field_decline_case("getfield truncated", 9);
    (void)fx;

    test_name = "int array reads and writes";
    array_fixture_init(&ax, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT);
    for (i = 0; i < ARRAY_LEN; ++i)
        array_load_case("iaload", &ax, (nj_ref)&ax.arr, i, 0);
    array_load_case("iaload first", &ax, (nj_ref)&ax.arr, 0, 0);
    array_load_case("iaload last", &ax, (nj_ref)&ax.arr, ARRAY_LEN - 1, 0);
    for (i = 0; i < 4; ++i)
        array_store_case("iastore", &ax, (nj_ref)&ax.arr, i,
                         (nj_s4)(0x55667788u + i), 0);
    array_length_case("arraylength", &ax, (nj_ref)&ax.arr, 0);

    test_name = "array faults leave state untouched";
    array_load_case("iaload null", &ax, NULL, 0, 1);
    array_store_case("iastore null", &ax, NULL, 0, 0x11223344, 1);
    array_length_case("arraylength null", &ax, NULL, 1);
    array_load_case("iaload index -1", &ax, (nj_ref)&ax.arr, UINT32_MAX, 1);
    array_load_case("iaload index == length", &ax, (nj_ref)&ax.arr, ARRAY_LEN, 1);
    array_store_case("iastore index -1", &ax, (nj_ref)&ax.arr, UINT32_MAX,
                     0x11223344, 1);
    array_store_case("iastore index == length", &ax, (nj_ref)&ax.arr, ARRAY_LEN,
                     0x11223344, 1);
    array_fixture_init(&ax, NJ_OBJ_ARRAY, NJ_ARRAY_T_BYTE);
    array_load_case("iaload wrong array type", &ax, (nj_ref)&ax.arr, 0, 1);
    array_store_case("iastore wrong array type", &ax, (nj_ref)&ax.arr, 0,
                     0x11223344, 1);
    array_fixture_init(&ax, NJ_OBJ_PLAIN, NJ_ARRAY_T_INT);
    array_load_case("iaload not an array", &ax, (nj_ref)&ax.arr, 0, 1);
    array_store_case("iastore not an array", &ax, (nj_ref)&ax.arr, 0,
                     0x11223344, 1);
    (void)offset;
}

/* ---- static fields --------------------------------------------------- */

enum { STATIC_CACHE = 64, STATIC_SLOTS = 8, STATIC_FIELD_SLOT = 2 };

typedef struct static_fixture {
    nj_vm vm;
    nj_class owner;
    nj_cpentry cache[STATIC_CACHE];
    nj_field field;
    _Alignas(8) nj_value statics[STATIC_SLOTS];
} static_fixture;

static void static_fixture_init(static_fixture *fx, unsigned ptype, unsigned width,
                                unsigned state)
{
    nj_u4 slot;
    memset(fx, 0, sizeof(*fx));
    fx->vm.cp_cache = fx->cache;
    fx->vm.cp_cache_size = STATIC_CACHE;
    fx->vm.statics = fx->statics;
    fx->vm.nstatics = STATIC_SLOTS;
    fx->owner.state = (nj_u1)state;
    fx->field.owner = &fx->owner;
    fx->field.ptype = (nj_u1)ptype;
    fx->field.width = (nj_u1)width;
    fx->field.flags = NJ_ACC_STATIC;
    fx->field.offset = (nj_u2)STATIC_FIELD_SLOT;
    slot = nj_cp_key_hash(&fx->owner, 1u) & (STATIC_CACHE - 1u);
    fx->cache[slot].cls = &fx->owner;
    fx->cache[slot].cp_index = 1u;
    fx->cache[slot].kind = NJ_LINK_FIELD_STATIC;
    fx->cache[slot].target = &fx->field;
    fx->cache[slot].used = 1u;
    memset(fx->statics, 0xa5, sizeof(fx->statics));
}

static nj_u1 *static_slot(static_fixture *fx)
{
    return (nj_u1 *)(void *)&fx->statics[STATIC_FIELD_SLOT];
}

static void static_read_case(const char *name, unsigned ptype, unsigned width,
                             nj_u8 raw)
{
    static_fixture fx;
    nj_u1 code[3];
    nj_u1 before[STATIC_SLOTS * sizeof(nj_value)];
    test_frame initial, expected;
    nj_jit_block block;
    unsigned bytes = field_bytes(ptype);
    int wide = width == 2u;
    nj_u4 ret;

    static_fixture_init(&fx, ptype, width, NJ_CLASS_READY);
    CHECK(nj_jit_static_field(&fx.vm, &fx.owner, 1u) == &fx.field);
    memcpy(static_slot(&fx), &raw, bytes);
    memcpy(before, fx.statics, sizeof(before));
    field_code(code, 0xb2);
    init_frame(&initial);
    expected = initial;
    if (wide) {
        memcpy(&expected.stack[BASE], &raw, 8u);
        expected.stack[BASE + 1u].bits = 0u;
    } else if (ptype == NJ_PT_BOOLEAN || ptype == NJ_PT_BYTE) {
        expected.stack[BASE].i = (nj_s4)(nj_s1)raw;
    } else if (ptype == NJ_PT_CHAR) {
        expected.stack[BASE].i = (nj_s4)(nj_u2)raw;
    } else if (ptype == NJ_PT_SHORT) {
        expected.stack[BASE].i = (nj_s4)(nj_s2)raw;
    } else {
        expected.stack[BASE].bits = (nj_u4)raw;
    }
    jit_vm = &fx.vm;
    ret = run_expect(name, code, sizeof(code), 1, 0, &fx.owner, &initial,
                     &expected, &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 3 && block.insns == 1);
    CHECK(block.stack_delta == (wide ? 2 : 1) && block.min_stack == 0);
    CHECK(block.max_growth == (wide ? 2 : 1) && block.nfaults == 0);
#if EXECUTE_RV32
    expect_success(ret, &block, block.insns);
#endif
    CHECK(memcmp(fx.statics, before, sizeof(before)) == 0);
    (void)ret;
}

static void static_write_case(const char *name, unsigned ptype, unsigned width,
                              nj_u8 raw)
{
    static_fixture fx;
    nj_u1 code[3];
    nj_u1 want[STATIC_SLOTS * sizeof(nj_value)];
    test_frame initial, expected;
    nj_jit_block block;
    unsigned bytes = field_bytes(ptype);
    int wide = width == 2u;
    int depth = (int)width;
    nj_u4 ret;

    static_fixture_init(&fx, ptype, width, NJ_CLASS_READY);
    CHECK(nj_jit_static_field(&fx.vm, &fx.owner, 1u) == &fx.field);
    memcpy(want, fx.statics, sizeof(want));
    memcpy(want + STATIC_FIELD_SLOT * sizeof(nj_value), &raw, bytes);
    field_code(code, 0xb3);
    init_frame(&initial);
    if (wide) memcpy(&initial.stack[BASE], &raw, 8u);
    else initial.stack[BASE].bits = (nj_u4)raw;
    expected = initial;
    jit_vm = &fx.vm;
    ret = run_expect(name, code, sizeof(code), 1, depth, &fx.owner, &initial,
                     &expected, &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 3 && block.insns == 1);
    CHECK(block.stack_delta == -depth && block.min_stack == depth);
    CHECK(block.max_growth == 0 && block.nfaults == 0);
#if EXECUTE_RV32
    expect_success(ret, &block, block.insns);
    CHECK(memcmp(fx.statics, want, sizeof(want)) == 0);
#else
    (void)want;
#endif
    (void)ret;
}

/* The class is linked but not initialised: the block carries a state guard.
 * Executing it must fault without touching the slot; recompiling once the
 * class is READY must drop the guard and perform the access. */
static void static_guard_case(const char *name, unsigned op, int put)
{
    static_fixture fx;
    nj_u1 code[3];
    nj_u1 before[STATIC_SLOTS * sizeof(nj_value)];
    test_frame initial, expected;
    nj_jit_block guarded, ready;
    int depth = put ? 1 : 0;
    nj_u4 ret;

    static_fixture_init(&fx, NJ_PT_INT, 1u, NJ_CLASS_LINKED);
    CHECK(nj_jit_static_field(&fx.vm, &fx.owner, 1u) == &fx.field);
    fx.statics[STATIC_FIELD_SLOT].bits = 0x11223344u;
    memcpy(before, fx.statics, sizeof(before));
    field_code(code, op);
    init_frame(&initial);
    if (put) initial.stack[BASE].bits = 0x55667788u;
    expected = initial;
    jit_vm = &fx.vm;
    ret = run_expect(name, code, sizeof(code), 1, depth, &fx.owner, &initial,
                     &expected, &guarded);
    jit_vm = NULL;
    CHECK(guarded.next_pc == 3 && guarded.insns == 1);
    CHECK(guarded.stack_delta == (put ? -1 : 1) && guarded.min_stack == depth);
    CHECK(guarded.max_growth == (put ? 0 : 1));
    CHECK(guarded.nfaults == 1 && guarded.faults[0].pc_off == 0 &&
          guarded.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    expect_fault(ret, 0u, 0u);
#endif
    CHECK(memcmp(fx.statics, before, sizeof(before)) == 0);

    fx.owner.state = NJ_CLASS_READY;
    expected = initial;
    if (!put) expected.stack[BASE].bits = 0x11223344u;
    jit_vm = &fx.vm;
    ret = run_expect(name, code, sizeof(code), 1, depth, &fx.owner, &initial,
                     &expected, &ready);
    jit_vm = NULL;
    CHECK(ready.next_pc == 3 && ready.insns == 1 && ready.nfaults == 0);
    CHECK(ready.stack_delta == (put ? -1 : 1) && ready.min_stack == depth);
#if EXECUTE_RV32
    expect_success(ret, &ready, ready.insns);
    if (put) CHECK(fx.statics[STATIC_FIELD_SLOT].bits == 0x55667788u);
    else CHECK(fx.statics[STATIC_FIELD_SLOT].bits == 0x11223344u);
#endif
    (void)ret;
}

/* A preceding instruction must be counted in the fault word (completed=1)
 * while pc_off still names the guarded getstatic. */
static void static_guard_completed_case(void)
{
    static_fixture fx;
    static const nj_u1 code[] = {0x00, 0xb2, 0u, 1u};
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;

    static_fixture_init(&fx, NJ_PT_INT, 1u, NJ_CLASS_LINKED);
    init_frame(&initial);
    expected = initial;
    jit_vm = &fx.vm;
    ret = run_expect("getstatic fault after nop", code, sizeof(code), 1, 0,
                     &fx.owner, &initial, &expected, &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 4 && block.insns == 2);
    CHECK(block.stack_delta == 1 && block.min_stack == 0 && block.max_growth == 1);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 1 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    expect_fault(ret, 0u, 1u);
#endif
    (void)ret;
}

static void static_decline_case(const char *name, int which)
{
    static_fixture fx;
    nj_u1 code[3];
    unsigned slot;
    static_fixture_init(&fx, NJ_PT_INT, 1u, NJ_CLASS_READY);
    slot = nj_cp_key_hash(&fx.owner, 1u) & (STATIC_CACHE - 1u);
    field_code(code, 0xb2);
    switch (which) {
    case 0: fx.cache[slot].used = 0u; break;
    case 1: fx.cache[slot].kind = NJ_LINK_FIELD_INST; break;
    case 2: fx.field.flags = 0u; break;
    case 3: fx.owner.state = NJ_CLASS_LOADED; break;
    case 4: fx.field.ptype = NJ_PT_LONG; fx.field.width = 2u;
            fx.field.offset = (nj_u2)(STATIC_SLOTS - 1u); break;
    case 5: fx.field.ptype = 0xffu; break;
    case 6: fx.vm.statics = NULL; fx.vm.nstatics = 0u; break;
    case 7: fx.cache[slot].target = NULL; break;
    case 8: fx.vm.cp_cache = NULL; fx.vm.cp_cache_size = 0u; break;
    default: break;
    }
    jit_vm = &fx.vm;
    if (which == 9) declined_owned(name, &fx.owner, code, 2u, 1);
    else declined_owned(name, &fx.owner, code, sizeof(code), 1);
    jit_vm = NULL;
}

static void test_static_fields(void)
{
    test_name = "getstatic primitive widths";
    static_read_case("getstatic boolean", NJ_PT_BOOLEAN, 1u, 0x81u);
    static_read_case("getstatic byte", NJ_PT_BYTE, 1u, 0x81u);
    static_read_case("getstatic char", NJ_PT_CHAR, 1u, 0xfffeu);
    static_read_case("getstatic short", NJ_PT_SHORT, 1u, 0x8001u);
    static_read_case("getstatic int", NJ_PT_INT, 1u, 0x80000001u);
    static_read_case("getstatic float", NJ_PT_FLOAT, 1u, 0x7fc12345u);
    static_read_case("getstatic long", NJ_PT_LONG, 2u,
                     UINT64_C(0x8000000000000001));
    static_read_case("getstatic double", NJ_PT_DOUBLE, 2u,
                     UINT64_C(0xf0123456789abcde));
    static_read_case("getstatic object", NJ_PT_OBJECT, 1u, 0x12345678u);

    test_name = "putstatic primitive widths";
    static_write_case("putstatic boolean", NJ_PT_BOOLEAN, 1u, 0x1234u);
    static_write_case("putstatic byte", NJ_PT_BYTE, 1u, 0x1234u);
    static_write_case("putstatic char", NJ_PT_CHAR, 1u, 0x12345u);
    static_write_case("putstatic short", NJ_PT_SHORT, 1u, 0x12345u);
    static_write_case("putstatic int", NJ_PT_INT, 1u, 0x89abcdefu);
    static_write_case("putstatic float", NJ_PT_FLOAT, 1u, 0x7fc12345u);
    static_write_case("putstatic long", NJ_PT_LONG, 2u,
                      UINT64_C(0x0123456789abcdef));
    static_write_case("putstatic double", NJ_PT_DOUBLE, 2u,
                      UINT64_C(0xfedcba9876543210));
    static_write_case("putstatic object", NJ_PT_OBJECT, 1u, 0x12345678u);

    test_name = "uninitialised class guard";
    static_guard_case("getstatic unready class", 0xb2, 0);
    static_guard_case("putstatic unready class", 0xb3, 1);
    static_guard_completed_case();

    test_name = "static field declines";
    static_decline_case("getstatic unresolved", 0);
    static_decline_case("getstatic instance link", 1);
    static_decline_case("getstatic non-static field", 2);
    static_decline_case("getstatic owner loaded", 3);
    static_decline_case("getstatic second slot past statics", 4);
    static_decline_case("getstatic unknown ptype", 5);
    static_decline_case("getstatic no statics", 6);
    static_decline_case("getstatic null target", 7);
    static_decline_case("getstatic no cp cache", 8);
    static_decline_case("getstatic truncated", 9);
}

/* ---- typed arrays ---------------------------------------------------- */

enum { TYPED_ARRAY_LEN = 8 };

typedef struct typed_array_fixture {
    nj_vm vm;
    nj_object arr;
    _Alignas(8) nj_u1 payload[TYPED_ARRAY_LEN * 8];
} typed_array_fixture;

static void typed_array_init(typed_array_fixture *fx, unsigned kind, unsigned aflags,
                             unsigned length)
{
    memset(fx, 0, sizeof(*fx));
    memset(fx->payload, 0xa5, sizeof(fx->payload));
    fx->arr.kind = (nj_u1)kind;
    fx->arr.aflags = (nj_u2)aflags;
    fx->arr.length = length;
    fx->arr.size = (nj_u4)sizeof(fx->payload);
    fx->arr.u.bytes = fx->payload;
}

static void typed_put_u1(typed_array_fixture *fx, unsigned index, nj_u4 v)
{
    fx->payload[index] = (nj_u1)v;
}

static void typed_put_u2(typed_array_fixture *fx, unsigned index, nj_u4 v)
{
    nj_u2 x = (nj_u2)v;
    memcpy(fx->payload + index * 2u, &x, 2u);
}

static void typed_put_ref(typed_array_fixture *fx, unsigned index, nj_ref ref)
{
    memcpy(fx->payload + index * sizeof(nj_ref), &ref, sizeof(ref));
}

static nj_u4 typed_ref_bits(const typed_array_fixture *fx, unsigned index)
{
    nj_ref ref;
    memcpy(&ref, fx->payload + index * sizeof(nj_ref), sizeof(ref));
    return (nj_u4)(uintptr_t)ref;
}

static void typed_load_case(const char *name, typed_array_fixture *fx, nj_ref ref,
                            unsigned op, unsigned index, nj_u4 expected_bits,
                            int fault)
{
    nj_u1 code[1];
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;

    code[0] = (nj_u1)op;
    init_frame(&initial);
    initial.stack[BASE].r = ref;
    initial.stack[BASE + 1].i = (nj_s4)index;
    expected = initial;
    if (!fault) expected.stack[BASE].bits = expected_bits;
    jit_vm = &fx->vm;
    ret = run_expect(name, code, sizeof(code), 1, 2, NULL, &initial, &expected,
                     &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 1 && block.insns == 1);
    CHECK(block.stack_delta == -1 && block.min_stack == 2);
    CHECK(block.max_growth == 0);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    if (fault) expect_fault(ret, 0u, 0u);
    else expect_success(ret, &block, block.insns);
#endif
    (void)ret;
}

static void typed_store_case(const char *name, typed_array_fixture *fx, nj_ref ref,
                             unsigned op, unsigned index, nj_u4 value,
                             const nj_u1 *want, int fault)
{
    nj_u1 code[1];
    nj_u1 before[sizeof(fx->payload)];
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;

    memcpy(before, fx->payload, sizeof(before));
    code[0] = (nj_u1)op;
    init_frame(&initial);
    initial.stack[BASE].r = ref;
    initial.stack[BASE + 1].i = (nj_s4)index;
    initial.stack[BASE + 2].bits = value;
    expected = initial;
    jit_vm = &fx->vm;
    ret = run_expect(name, code, sizeof(code), 1, 3, NULL, &initial, &expected,
                     &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 1 && block.insns == 1);
    CHECK(block.stack_delta == -3 && block.min_stack == 3);
    CHECK(block.max_growth == 0);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_RV32
    if (fault) expect_fault(ret, 0u, 0u);
    else expect_success(ret, &block, block.insns);
    CHECK(memcmp(fx->payload, fault ? before : want, sizeof(before)) == 0);
#else
    (void)want;
    (void)before;
#endif
    (void)fault;
    (void)ret;
}

static void test_typed_arrays(void)
{
    typed_array_fixture fx, want_fx;
    unsigned i;
    nj_u1 code53[1];

    test_name = "new array opcodes decline without a VM";
    {
        static const nj_u1 ops[] = {0x32, 0x33, 0x34, 0x35, 0x54, 0x55, 0x56};
        for (i = 0; i < sizeof(ops); ++i)
            declined("array opcode without vm", &ops[i], 1, 1);
    }
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT, TYPED_ARRAY_LEN);
    code53[0] = 0x53;
    jit_vm = &fx.vm;
    declined_owned("aastore stays interpreter-only", NULL, code53, 1u, 1);
    jit_vm = NULL;

    test_name = "byte and boolean array access";
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_BYTE, TYPED_ARRAY_LEN);
    {
        static const nj_u1 vals[TYPED_ARRAY_LEN] =
            {0x00, 0x7f, 0x80, 0xff, 0x81, 0x55, 0xaa, 0x01};
        memcpy(fx.payload, vals, sizeof(vals));
    }
    typed_load_case("baload zero", &fx, (nj_ref)&fx.arr, 0x33, 0, 0x00000000u, 0);
    typed_load_case("baload 127", &fx, (nj_ref)&fx.arr, 0x33, 1, 0x0000007fu, 0);
    typed_load_case("baload -128", &fx, (nj_ref)&fx.arr, 0x33, 2, 0xffffff80u, 0);
    typed_load_case("baload -1", &fx, (nj_ref)&fx.arr, 0x33, 3, 0xffffffffu, 0);
    typed_load_case("baload -127", &fx, (nj_ref)&fx.arr, 0x33, 4, 0xffffff81u, 0);
    want_fx = fx;
    typed_put_u1(&want_fx, 3, 0x34u);
    typed_store_case("bastore low byte", &fx, (nj_ref)&fx.arr, 0x54, 3, 0x1234u,
                     want_fx.payload, 0);
    want_fx = fx;
    typed_put_u1(&want_fx, 5, 0xffu);
    typed_store_case("bastore sign bits", &fx, (nj_ref)&fx.arr, 0x54, 5, 0x80ffu,
                     want_fx.payload, 0);

    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_BOOLEAN, TYPED_ARRAY_LEN);
    {
        static const nj_u1 vals[TYPED_ARRAY_LEN] = {0, 1, 1, 0, 1, 0, 0, 1};
        memcpy(fx.payload, vals, sizeof(vals));
    }
    typed_load_case("baload boolean zero", &fx, (nj_ref)&fx.arr, 0x33, 0,
                    0x00000000u, 0);
    typed_load_case("baload boolean one", &fx, (nj_ref)&fx.arr, 0x33, 1,
                    0x00000001u, 0);
    want_fx = fx;
    typed_put_u1(&want_fx, 2, 1u);
    typed_store_case("bastore boolean true", &fx, (nj_ref)&fx.arr, 0x54, 2, 1u,
                     want_fx.payload, 0);
    want_fx = fx;
    typed_put_u1(&want_fx, 3, 0u);
    typed_store_case("bastore boolean false", &fx, (nj_ref)&fx.arr, 0x54, 3, 0u,
                     want_fx.payload, 0);

    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_BOOLEAN, TYPED_ARRAY_LEN);
    {
        /* The interpreter normalises every boolean load to 0 or 1 even when a
           native write (or a stale image) stored another byte value. */
        static const nj_u1 raw[TYPED_ARRAY_LEN] = {0, 1, 2, 0x80, 0xff, 0xfe, 0x7f, 0};
        memcpy(fx.payload, raw, sizeof(raw));
    }
    typed_load_case("baload boolean normalises 2", &fx, (nj_ref)&fx.arr, 0x33, 2,
                    0x00000001u, 0);
    typed_load_case("baload boolean normalises 0x80", &fx, (nj_ref)&fx.arr, 0x33, 3,
                    0x00000001u, 0);
    typed_load_case("baload boolean normalises 0xff", &fx, (nj_ref)&fx.arr, 0x33, 4,
                    0x00000001u, 0);
    typed_load_case("baload boolean normalises 0x7f", &fx, (nj_ref)&fx.arr, 0x33, 6,
                    0x00000001u, 0);

    test_name = "char and short array access";
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_CHAR, TYPED_ARRAY_LEN);
    for (i = 0; i < TYPED_ARRAY_LEN; ++i) typed_put_u2(&fx, i, i);
    typed_put_u2(&fx, 2, 0x7fffu);
    typed_put_u2(&fx, 3, 0x8000u);
    typed_put_u2(&fx, 4, 0xffffu);
    typed_load_case("caload zero extend", &fx, (nj_ref)&fx.arr, 0x34, 3,
                    0x00008000u, 0);
    typed_load_case("caload max", &fx, (nj_ref)&fx.arr, 0x34, 4,
                    0x0000ffffu, 0);
    want_fx = fx;
    typed_put_u2(&want_fx, 1, 0x2345u);
    typed_store_case("castore low 16", &fx, (nj_ref)&fx.arr, 0x55, 1, 0x12345u,
                     want_fx.payload, 0);
    want_fx = fx;
    typed_put_u2(&want_fx, 5, 0xffffu);
    typed_store_case("castore truncates", &fx, (nj_ref)&fx.arr, 0x55, 5, 0x1ffffu,
                     want_fx.payload, 0);

    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_SHORT, TYPED_ARRAY_LEN);
    for (i = 0; i < TYPED_ARRAY_LEN; ++i) typed_put_u2(&fx, i, i);
    typed_put_u2(&fx, 2, 0x7fffu);
    typed_put_u2(&fx, 3, 0x8000u);
    typed_put_u2(&fx, 4, 0xffffu);
    typed_put_u2(&fx, 5, 0x8001u);
    typed_load_case("saload sign extend min", &fx, (nj_ref)&fx.arr, 0x35, 3,
                    0xffff8000u, 0);
    typed_load_case("saload -1", &fx, (nj_ref)&fx.arr, 0x35, 4,
                    0xffffffffu, 0);
    typed_load_case("saload -32767", &fx, (nj_ref)&fx.arr, 0x35, 5,
                    0xffff8001u, 0);
    want_fx = fx;
    typed_put_u2(&want_fx, 1, 0x2345u);
    typed_store_case("sastore low 16", &fx, (nj_ref)&fx.arr, 0x56, 1, 0x12345u,
                     want_fx.payload, 0);
    want_fx = fx;
    typed_put_u2(&want_fx, 6, 0x8001u);
    typed_store_case("sastore keeps low bits", &fx, (nj_ref)&fx.arr, 0x56, 6,
                     0x18001u, want_fx.payload, 0);

    test_name = "reference array access";
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_REF, TYPED_ARRAY_LEN);
    typed_put_ref(&fx, 0, (nj_ref)&fx.arr);
    typed_put_ref(&fx, 1, NULL);
    typed_load_case("aaload self", &fx, (nj_ref)&fx.arr, 0x32, 0,
                    typed_ref_bits(&fx, 0), 0);
    typed_load_case("aaload null element", &fx, (nj_ref)&fx.arr, 0x32, 1, 0u, 0);

    test_name = "array guard faults";
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_BYTE, TYPED_ARRAY_LEN);
    typed_load_case("baload null", &fx, NULL, 0x33, 0, 0u, 1);
    typed_load_case("baload index -1", &fx, (nj_ref)&fx.arr, 0x33, UINT32_MAX,
                    0u, 1);
    typed_load_case("baload index == length", &fx, (nj_ref)&fx.arr, 0x33,
                    TYPED_ARRAY_LEN, 0u, 1);
    fx.arr.kind = NJ_OBJ_PLAIN;
    typed_load_case("baload not an array", &fx, (nj_ref)&fx.arr, 0x33, 0, 0u, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_SHORT, TYPED_ARRAY_LEN);
    typed_load_case("baload wrong element kind", &fx, (nj_ref)&fx.arr, 0x33, 0,
                    0u, 1);
    typed_store_case("bastore null", &fx, NULL, 0x54, 0, 0x11u, fx.payload, 1);
    typed_store_case("bastore index == length", &fx, (nj_ref)&fx.arr, 0x54,
                     TYPED_ARRAY_LEN, 0x11u, fx.payload, 1);
    fx.arr.kind = NJ_OBJ_PLAIN;
    typed_store_case("bastore not an array", &fx, (nj_ref)&fx.arr, 0x54, 0,
                     0x11u, fx.payload, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_CHAR, TYPED_ARRAY_LEN);
    typed_load_case("caload null", &fx, NULL, 0x34, 0, 0u, 1);
    typed_load_case("caload index == length", &fx, (nj_ref)&fx.arr, 0x34,
                    TYPED_ARRAY_LEN, 0u, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_SHORT, TYPED_ARRAY_LEN);
    typed_load_case("caload wrong element kind", &fx, (nj_ref)&fx.arr, 0x34, 0,
                    0u, 1);
    typed_store_case("castore wrong element kind", &fx, (nj_ref)&fx.arr, 0x55, 0,
                     0x11u, fx.payload, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT, TYPED_ARRAY_LEN);
    typed_store_case("castore element kind mismatch", &fx, (nj_ref)&fx.arr, 0x55,
                     0, 0x11u, fx.payload, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_SHORT, TYPED_ARRAY_LEN);
    typed_load_case("saload null", &fx, NULL, 0x35, 0, 0u, 1);
    typed_load_case("saload index == length", &fx, (nj_ref)&fx.arr, 0x35,
                    TYPED_ARRAY_LEN, 0u, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_CHAR, TYPED_ARRAY_LEN);
    typed_load_case("saload wrong element kind", &fx, (nj_ref)&fx.arr, 0x35, 0,
                    0u, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_SHORT, TYPED_ARRAY_LEN);
    typed_store_case("bastore element kind mismatch", &fx, (nj_ref)&fx.arr, 0x54,
                     0, 0x11u, fx.payload, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_BYTE, TYPED_ARRAY_LEN);
    typed_store_case("sastore wrong element kind", &fx, (nj_ref)&fx.arr, 0x56, 0,
                     0x11u, fx.payload, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_CHAR, TYPED_ARRAY_LEN);
    typed_store_case("sastore element kind mismatch", &fx, (nj_ref)&fx.arr, 0x56,
                     0, 0x11u, fx.payload, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT, TYPED_ARRAY_LEN);
    typed_load_case("aaload wrong element kind", &fx, (nj_ref)&fx.arr, 0x32, 0,
                    0u, 1);
    typed_load_case("aaload null", &fx, NULL, 0x32, 0, 0u, 1);
    fx.arr.kind = NJ_OBJ_PLAIN;
    typed_load_case("aaload not an array", &fx, (nj_ref)&fx.arr, 0x32, 0, 0u, 1);
    typed_array_init(&fx, NJ_OBJ_ARRAY, NJ_ARRAY_T_REF, TYPED_ARRAY_LEN);
    typed_load_case("aaload index -1", &fx, (nj_ref)&fx.arr, 0x32, UINT32_MAX,
                    0u, 1);
    typed_load_case("aaload index == length", &fx, (nj_ref)&fx.arr, 0x32,
                    TYPED_ARRAY_LEN, 0u, 1);
}

/* Real int[][] traversal: aaload produces the row reference and iaload reads
 * through it.  On RV32 a reference is four bytes, so only the low half of the
 * array slot is written. */
static void test_array_chain(void)
{
    enum { CHAIN_ROWS = 2, CHAIN_CELLS = 8 };
    typedef struct chain_fixture {
        nj_vm vm;
        nj_object rows;
        nj_ref row_refs[CHAIN_ROWS];
        nj_object row;
        nj_s4 cells[CHAIN_CELLS];
    } chain_fixture;
    static const nj_u1 code[] = {0x32, 0x06, 0x2e}; /* aaload; iconst_3; iaload */
    chain_fixture fx;
    test_frame initial, expected;
    nj_jit_block block;
    unsigned i;
    nj_u4 ret;

    memset(&fx, 0, sizeof(fx));
    fx.rows.kind = NJ_OBJ_ARRAY;
    fx.rows.aflags = NJ_ARRAY_T_REF;
    fx.rows.length = CHAIN_ROWS;
    fx.rows.size = (nj_u4)sizeof(fx.row_refs);
    fx.rows.u.refs = fx.row_refs;
    fx.row.kind = NJ_OBJ_ARRAY;
    fx.row.aflags = NJ_ARRAY_T_INT;
    fx.row.length = CHAIN_CELLS;
    fx.row.size = (nj_u4)sizeof(fx.cells);
    fx.row.u.bytes = (nj_u1 *)(void *)fx.cells;
    for (i = 0; i < CHAIN_CELLS; ++i)
        fx.cells[i] = (nj_s4)(0x1000u + i);
    fx.row_refs[0] = &fx.row;
    fx.row_refs[1] = NULL;

    init_frame(&initial);
    initial.stack[BASE].r = (nj_ref)&fx.rows;
    initial.stack[BASE + 1].i = 0;
    expected = initial;
    expected.stack[BASE].bits = (nj_u4)fx.cells[3];
    expected.stack[BASE + 1].bits = 3u;
    jit_vm = &fx.vm;
    ret = run_expect("aaload then iaload", code, sizeof(code), 1, 2, NULL,
                     &initial, &expected, &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 3 && block.insns == 3);
    CHECK(block.stack_delta == -1 && block.min_stack == 2 && block.max_growth == 0);
    CHECK(block.nfaults == 2);
    CHECK(block.faults[0].pc_off == 0 && block.faults[0].sp_delta == 0);
    CHECK(block.faults[1].pc_off == 2 && block.faults[1].sp_delta == 0);
#if EXECUTE_RV32
    CHECK(sizeof(nj_ref) == 4u);
    expect_success(ret, &block, block.insns);
#endif
    (void)ret;
}

/* Analyze and compile must agree on stack/fault/pc metadata for a block that
 * mixes static accesses with the new array opcodes, both with and without the
 * class-initialisation guard. */
static void test_new_metadata(void)
{
    /* Four guards at most, so the block also fits a 4-fault configuration:
     * getstatic, aaload, iaload and putstatic. */
    static const nj_u1 code[] = {
        0xb2, 0, 1,     /* getstatic */
        0x03,           /* iconst_0 */
        0x32,           /* aaload */
        0x03,           /* iconst_0 */
        0x2e,           /* iaload */
        0xb3, 0, 1      /* putstatic */
    };
    static_fixture fx;
    nj_method m = method(code, sizeof(code), 1);
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block analysis, block;

    static_fixture_init(&fx, NJ_PT_INT, 1u, NJ_CLASS_READY);
    m.owner = &fx.owner;
    jit_vm = &fx.vm;
    test_name = "new opcode analyze/compile metadata";
    CHECK(analyze_method(&m, 0, &analysis));
    CHECK(compile_method(&m, 0, generated, sizeof(generated), &block));
    same_metadata(&block, &analysis);
    CHECK(block.next_pc == sizeof(code) && block.insns == 6);
    CHECK(block.stack_delta == 0 && block.min_stack == 0 && block.max_growth == 2);
    CHECK(block.nfaults == 2); /* aaload + iaload; class is READY */
    inspect(generated, block.size);

    fx.owner.state = NJ_CLASS_LINKED;
    CHECK(analyze_method(&m, 0, &analysis));
    CHECK(compile_method(&m, 0, generated, sizeof(generated), &block));
    same_metadata(&block, &analysis);
    CHECK(block.nfaults == 4); /* two static guards plus two array guards */
    inspect(generated, block.size);
    jit_vm = NULL;
}

/* ---- fault ABI ------------------------------------------------------- */

static void test_faults(void)
{
    static const nj_u1 five[] = {
        0xb4,0,1, 0xb4,0,1, 0xb4,0,1, 0xb4,0,1, 0xb4,0,1, 0x00,0xac
    };
    static const nj_u1 four[] = {
        0xb4,0,1, 0xb4,0,1, 0xb4,0,1, 0xb4,0,1, 0x00,0xac
    };
#if EXECUTE_RV32
    static const nj_u1 two[] = {0xb4,0,1, 0x2a, 0xb4,0,1, 0xac};
#endif
    field_fixture fx;
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block, analysis;
    nj_method m;
    unsigned i;

    test_name = "record_fault bounds";
    memset(&block, 0, sizeof(block));
    for (i = 0; i < NJ_JIT_MAX_FAULTS; ++i)
        CHECK(nj_jit_record_fault(&block, 10u, 10u + i, (int)i) == 1);
    CHECK(block.nfaults == NJ_JIT_MAX_FAULTS);
    CHECK(nj_jit_record_fault(&block, 10u, 99u, 0) == 0);
    CHECK(!nj_jit_record_fault(NULL, 0u, 0u, 0));
    memset(&block, 0, sizeof(block));
    CHECK(!nj_jit_record_fault(&block, 5u, 4u, 0));
    CHECK(!nj_jit_record_fault(&block, 0u, 0x10000u, 0));
    CHECK(block.nfaults == 0);

    test_name = "fault metadata stops at the budget";
    field_fixture_init(&fx, NJ_PT_INT, 1u, field_offset());
    jit_vm = &fx.vm;
    {
        /* A block may hold at most NJ_JIT_MAX_FAULTS guards. The four-getfield
         * program saturates the default budget exactly; a larger configured
         * budget compiles it whole. */
        const unsigned nfour = NJ_JIT_MAX_FAULTS < 4 ? NJ_JIT_MAX_FAULTS : 4u;
        const unsigned nfive = NJ_JIT_MAX_FAULTS < 5 ? NJ_JIT_MAX_FAULTS : 5u;
        const unsigned four_insns = nfour == 4u ? 5u : nfour;
        const unsigned four_next = nfour == 4u ? 13u : nfour * 3u;
        /* 0xac is the faulting program's terminal bytecode, not a JIT
         * instruction: a complete block ends at the preceding 0x00 nop. */
        const unsigned five_insns = nfive == 5u ? 6u : nfive;
        const unsigned five_next = nfive == 5u ? 16u : nfive * 3u;

        m = method(four, sizeof(four), 1);
        m.owner = &fx.owner;
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 1);
        CHECK(block.next_pc == four_next && block.insns == four_insns);
        CHECK(block.nfaults == nfour);
        for (i = 0; i < block.nfaults; ++i) {
            CHECK(block.faults[i].pc_off == i * 3u);
            CHECK(block.faults[i].sp_delta == 0);
        }
        m = method(five, sizeof(five), 1);
        m.owner = &fx.owner;
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 1);
        CHECK(block.next_pc == five_next && block.insns == five_insns);
        CHECK(block.nfaults == nfive);
        CHECK(nj_rv32_analyze(&fx.vm, &m, 0, &analysis));
        CHECK(analysis.next_pc == five_next && analysis.insns == five_insns);
        CHECK(analysis.nfaults == block.nfaults);
        for (i = 0; i < block.nfaults; ++i) {
            CHECK(analysis.faults[i].pc_off == block.faults[i].pc_off);
            CHECK(analysis.faults[i].sp_delta == block.faults[i].sp_delta);
        }
    }
    jit_vm = NULL;

#if EXECUTE_RV32 && NJ_JIT_RV32_M
    test_name = "div and rem zero divisor faults";
    {
        static const nj_u1 idiv[] = {0x6c,0xac};
        static const nj_u1 irem[] = {0x70,0xac};
        test_frame initial, expected;
        nj_u4 ret;
        int op;
        field_fixture_init(&fx, NJ_PT_INT, 1u, field_offset());
        jit_vm = &fx.vm;
        for (op = 0; op < 2; ++op) {
            init_frame(&initial);
            initial.stack[BASE].i = 7;
            initial.stack[BASE + 1].i = 0;
            expected = initial;
            ret = run_expect(op ? "irem zero" : "idiv zero", op ? irem : idiv,
                             2u, 0, 2, NULL, &initial, &expected, &block);
            CHECK(block.next_pc == 1u && block.insns == 1u);
            CHECK(block.stack_delta == -1 && block.min_stack == 2);
            CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
                  block.faults[0].sp_delta == 0);
            expect_fault(ret, 0u, 0u);
        }
        jit_vm = NULL;
    }
#endif

#if EXECUTE_RV32
    test_name = "second fault reports its index and completed count";
    {
        nj_object *o;
        test_frame initial, expected;
        nj_u4 ret;
        field_fixture_init(&fx, NJ_PT_INT, 1u, field_offset());
        memcpy(fx.object + field_offset(), "\x11\x22\x33\x44", 4u);
        o = (nj_object *)(void *)fx.object;
        init_frame(&initial);
        initial.stack[BASE].r = (nj_ref)o;
        initial.locals[0].r = NULL;
        expected = initial;
        expected.stack[BASE].i = (nj_s4)0x44332211;
        expected.stack[BASE + 1].r = NULL;
        jit_vm = &fx.vm;
        ret = run_expect("second getfield fault", two, sizeof(two), 1, 1,
                         &fx.owner, &initial, &expected, &block);
        jit_vm = NULL;
        CHECK(block.next_pc == 7u && block.insns == 3u);
        CHECK(block.nfaults == 2);
        CHECK(block.faults[0].pc_off == 0u && block.faults[0].sp_delta == 0);
        CHECK(block.faults[1].pc_off == 4u && block.faults[1].sp_delta == 1);
        expect_fault(ret, 1u, 2u);
    }
#endif
}

#if EXECUTE_RV32 && NJ_ENABLE_ENGINE
/* The direct invoke() oracle above bypasses nj_jit_execute, so it cannot see
 * the SAVE-side fault ABI.  This fixture drives the real probe/execute path
 * with a writable/executable code port. */
static void *jit_code_alloc(void *ud, nj_size n)
{
    void *p;
    (void)ud;
    p = mmap(NULL, (size_t)n, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static int jit_code_commit(void *ud, void *p, nj_size n)
{
    (void)ud;
    return mprotect(p, (size_t)n, PROT_READ | PROT_EXEC) == 0 ? 0 : -1;
}

static void jit_code_free(void *ud, void *p, nj_size n)
{
    (void)ud;
    if (p) (void)munmap(p, (size_t)n);
}

static void test_runtime_fault_abi(void)
{
    static const nj_u1 two[] = {0xb4,0,1, 0x2a, 0xb4,0,1, 0x00, 0xac};
    field_fixture fx;
    nj_port port;
    nj_method m;
    test_frame actual, expected;
    nj_frame fr;
    nj_u4 pc = 0u, ret = 0u;
    nj_value *sp;
    nj_jit_stats stats;
    unsigned i;

    test_name = "runtime fault updates pc, sp and frame";
    field_fixture_init(&fx, NJ_PT_INT, 1u, field_offset());
    memcpy(fx.object + field_offset(), "\x11\x22\x33\x44", 4u);
    memset(&port, 0, sizeof(port));
    port.code_alloc = jit_code_alloc;
    port.code_commit = jit_code_commit;
    port.code_free = jit_code_free;
    fx.vm.port = &port;
    CHECK(nj_jit_enable(&fx.vm) == NJ_OK);

    m = method(two, sizeof(two), 1);
    m.owner = &fx.owner;
    init_frame(&actual);
    actual.stack[BASE].r = (nj_ref)(void *)fx.object;
    actual.locals[0].r = NULL;
    expected = actual;
    expected.stack[BASE].i = (nj_s4)0x44332211;
    expected.stack[BASE + 1u].r = NULL;

    memset(&fr, 0, sizeof(fr));
    fr.m = &m;
    fr.locals = actual.locals;
    fr.stack = &actual.stack[BASE];
    fr.sp = fr.stack + 1;
    fr.stack_limit = actual.stack + STACK_SLOTS;
    sp = fr.sp;
    for (i = 0; i <= NJ_JIT_HOT_THRESHOLD; ++i) {
        fr.pc = pc;
        ret = nj_jit_try_block(&fx.vm, &fr, &pc, &sp, 64u);
        if (ret) break;
    }

    /* The second getfield faults after the first getfield and aload_0.  The
     * runtime must publish the faulting bytecode and the entry sp plus the
     * block's sp_delta, while returning only the completed count. */
    CHECK(ret == 2u);
    CHECK(pc == 4u);
    CHECK(sp == fr.stack + 2);
    CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
#if NJ_JIT_STATS
    CHECK(nj_jit_get_stats(&fx.vm, &stats));
    CHECK(stats.compiled_blocks == 1u);
    CHECK(stats.executed_blocks == 1u);
    CHECK(stats.faults == 1u);
    CHECK(stats.bytecodes == 2u);
#else
    (void)stats;
#endif
    nj_jit_disable(&fx.vm);
}
#endif

static void test_integer(void)
{
    static const nj_u4 values[] = {
        0, 1, 2, 31, 32, 33, 63, 0x7fffffffu, 0x80000000u,
        0xffffffffu, 0xdeadbeefu, 0x80008080u
    };
    static const nj_u1 binary[] = {0x60,0x64,0x78,0x7a,0x7c,0x7e,0x80,0x82,
#if NJ_JIT_RV32_M
                                  0x68,0x6c,0x70,
#endif
    };
    static const nj_u1 unary[] = {0x74,0x91,0x92,0x93};
    test_frame f;
    size_t i, j, k;
    init_frame(&f);
    for (k = 0; k < sizeof(binary); ++k)
        for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
            for (j = 0; j < sizeof(values)/sizeof(values[0]); ++j) {
                if ((binary[k] == 0x6c || binary[k] == 0x70) && values[j] == 0)
                    continue;
                f.stack[BASE].bits = values[i]; f.stack[BASE + 1].bits = values[j];
                program("integer arithmetic", &binary[k], 1, 0, 2, &f);
            }
    for (k = 0; k < sizeof(unary); ++k)
        for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
            f.stack[BASE].bits = values[i];
            program("integer unary", &unary[k], 1, 0, 1, &f);
        }
    {
        static const nj_u1 ops[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                                   0x10,0x80,0x10,0x7f,0x11,0x80,0,0x11,0x7f,0xff,
                                   0x11,0xf8,0,0x11,0x08,0};
        program("constants and immediate boundaries", ops, sizeof(ops), 0, 0, &f);
    }
    {
        static const nj_u1 ops[] = {0x1a,0x1b,0x60,0x3d,0x15,2,0x36,3,
                                   0x84,0,0x80,0x84,1,0x7f,
                                   0xc4,0x84,0,2,0x80,0,
                                   0xc4,0x84,0,3,0x7f,0xff};
        f.locals[0].bits = 0x7fffffffu; f.locals[1].bits = 1;
        program("locals and signed wide iinc", ops, sizeof(ops), 4, 0, &f);
    }
    {
        static const nj_u1 ops[] = {0xc4,0x15,0,255,0xc4,0x36,0,254};
        program("largest encodable local offset", ops, sizeof(ops), 256, 0, &f);
    }
    {
        static const nj_u1 ops[] = {0x59,0x5f,0x5c,0x57,0x58};
        program("stack copies preserve complete slots", ops, sizeof(ops), 0, 2, &f);
    }
#if !NJ_JIT_RV32_M
    {
        static const nj_u1 ops[] = {0x68,0x6c,0x70};
        for (i = 0; i < sizeof(ops); ++i) declined("M disabled", &ops[i], 1, 0);
    }
#endif
}

static void test_float(void)
{
    test_frame f;
    size_t i, j, k;
    static const nj_u1 fops[] = {0x62,0x66,0x6a,0x6e,0x76};
    static const nj_u1 dops[] = {0x63,0x67,0x6b,0x6f,0x77};
    static const nj_u4 floats[] = {0,0x80000000u,0x3fc00000u,0xc0100000u,
                                  1,0x7f7fffffu,0x7f800000u,0xff800000u,0x7fc12345u};
    static const uint64_t doubles[] = {0,UINT64_C(0x8000000000000000),
        UINT64_C(0x3ff8000000000000),UINT64_C(0xc002000000000000),1,
        UINT64_C(0x7fefffffffffffff),UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000),UINT64_C(0x7ff8123456789abc)};
    init_frame(&f);
    for (k = 0; k < sizeof(fops); ++k) {
        if (!NJ_JIT_RV32_F) { declined("F disabled", &fops[k], 1, 0); continue; }
        for (i = 0; i < sizeof(floats)/sizeof(floats[0]); ++i)
            for (j = 0; j < sizeof(floats)/sizeof(floats[0]); ++j) {
                f.stack[BASE].bits = floats[i]; f.stack[BASE + 1].bits = floats[j];
                program("float IEEE arithmetic", &fops[k], 1, 0,
                        fops[k] == 0x76 ? 1 : 2, &f);
            }
    }
    for (k = 0; k < sizeof(dops); ++k) {
        if (!NJ_JIT_RV32_D || !NJ_ENABLE_LONG) {
            declined("D disabled", &dops[k], 1, 0); continue;
        }
        for (i = 0; i < sizeof(doubles)/sizeof(doubles[0]); ++i)
            for (j = 0; j < sizeof(doubles)/sizeof(doubles[0]); ++j) {
                memcpy(&f.stack[BASE], &doubles[i], 8);
                memcpy(&f.stack[BASE + 2], &doubles[j], 8);
                program("double IEEE arithmetic", &dops[k], 1, 0,
                        dops[k] == 0x77 ? 2 : 4, &f);
            }
    }
    /* Constants and copies can use RV32I even with F/D disabled. */
    {
        static const nj_u1 ops[] = {0x0b,0x0c,0x0d,0x43,0x38,1,0x45,
                                   0x22,0x17,1,0x46,0x43};
        program("float constants and bit copies", ops, sizeof(ops), 4, 0, &f);
    }
#if NJ_ENABLE_LONG
    {
        static const nj_u1 ops[] = {0x09,0x0a,0x37,0,0x3f,0x0e,0x0f,
                                   0x39,2,0x47,0x1e,0x37,2,0x26,0x39,2,
                                   0xc4,0x18,0,2,0xc4,0x39,0,0};
        program("wide constants copies and padding", ops, sizeof(ops), 4, 0, &f);
    }
#endif
}

static void test_boundaries(void)
{
    static const nj_u1 truncated[][6] = {
        {0x10}, {0x11,0}, {0x15}, {0x36}, {0x84,0},
        {0xc4}, {0xc4,0x15,0}, {0xc4,0x36,0}, {0xc4,0x84,0,0,0}
    };
    static const unsigned lengths[] = {1,2,1,1,2,1,3,3,5};
    static const nj_u1 ops[] = {0x08,0x07,0x60,0x3b};
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES + 32];
    nj_jit_block block;
    nj_method m;
    size_t i, cap, full_size;
    test_name = "truncated bytecode";
    for (i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i)
        declined("truncated bytecode", truncated[i], lengths[i], 4);
#if defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        nj_u1 *mapping;
        test_name = "truncated operands against inaccessible page";
        CHECK(page > 0);
        mapping = mmap(NULL, (size_t)page * 2, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(mapping != MAP_FAILED);
        CHECK(mprotect(mapping, (size_t)page, PROT_READ | PROT_WRITE) == 0);
        for (i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
            nj_u1 *edge = mapping + page - lengths[i];
            memcpy(edge, truncated[i], lengths[i]);
            declined("truncated operands against inaccessible page", edge, lengths[i], 4);
        }
        CHECK(munmap(mapping, (size_t)page * 2) == 0);
    }
#endif
    {
        static const nj_u1 bad[][4] = {{0x15,4},{0x36,4},{0x84,4,1},
                                      {0x16,3},{0x18,3},{0x37,3},{0x39,3},
                                      {0xc4,0x15,1,0},{0xc4,0x00}};
        static const unsigned lens[] = {2,2,3,2,2,2,2,4,2};
        for (i = 0; i < sizeof(lens)/sizeof(lens[0]); ++i)
            declined("invalid local or wide opcode", bad[i], lens[i], i == 7 ? 260 : 4);
    }
    {
        static const nj_u1 stops[] = {0xac,0xb1,0xb8,0xbb,0xa7,0x12,0x61,0xff};
        for (i = 0; i < sizeof(stops); ++i) declined("unsupported instruction", &stops[i], 1, 4);
    }
    test_name = "capacity and canaries";
    m = method(ops, sizeof(ops), 1);
    CHECK(compile_checked(&m, 0, generated, NJ_JIT_MAX_CODE_BYTES, &block));
    full_size = block.size;
    for (cap = 0; cap <= full_size + 4; ++cap) {
        int ok;
        memset(generated, 0xa5, sizeof(generated));
        ok = compile_checked(&m, 0, generated + 16, cap, &block);
        for (i = 0; i < 16; ++i) CHECK(generated[i] == 0xa5);
        for (i = 16 + cap; i < sizeof(generated); ++i) CHECK(generated[i] == 0xa5);
        if (ok) {
            CHECK(block.size <= cap && block.insns > 0 && block.next_pc <= sizeof(ops));
            inspect(generated + 16, block.size);
        }
    }
    test_name = "pc and argument validation";
    CHECK(!compile_checked(&m, sizeof(ops), generated, sizeof(generated), &block));
    CHECK(!compile_checked(&m, UINT32_MAX, generated, sizeof(generated), &block));
    CHECK(!compile_checked(NULL, 0, generated, sizeof(generated), &block));
    CHECK(!compile_checked(&m, 0, NULL, sizeof(generated), &block));
    CHECK(!compile_checked(&m, 0, generated, sizeof(generated), NULL));
    m.code = NULL;
    CHECK(!compile_checked(&m, 0, generated, sizeof(generated), &block));
    {
        static const nj_u1 code[] = {0x60,0x59,0x04,0x05,0x60,0x60,0x3b};
        m = method(code, sizeof(code), 1);
        test_name = "precise stack metadata";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == sizeof(code));
        CHECK(block.min_stack == 2 && block.max_growth == 2 && block.stack_delta == -1);
        CHECK(block.nfaults == 0);
        test_name = "method max_stack bounds";
        m.max_stack = 1;
        CHECK(!compile_checked(&m, 0, generated, sizeof(generated), &block));
        m.max_stack = 3;
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 3 && block.insns == 3);
        CHECK(block.min_stack == 2 && block.max_growth == 1 && block.stack_delta == 1);
        m = method(ops, sizeof(ops), 1);
        m.max_stack = 0;
        CHECK(!compile_checked(&m, 0, generated, sizeof(generated), &block));
    }
    {
        nj_u1 code[NJ_JIT_MAX_BLOCK_INSNS + 7];
        memset(code, 0, sizeof(code));
        m = method(code, sizeof(code), 0);
        test_name = "instruction budget";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.insns == NJ_JIT_MAX_BLOCK_INSNS);
        CHECK(block.next_pc == NJ_JIT_MAX_BLOCK_INSNS);
    }
    {
        static const nj_u1 code[] = {0xac,0x05,0xac};
        m = method(code, sizeof(code), 0);
        test_name = "nonzero starting pc and unsupported tail";
        CHECK(compile_checked(&m, 1, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 2 && block.insns == 1);
        CHECK(block.stack_delta == 1 && block.min_stack == 0 && block.max_growth == 1);
    }
#if NJ_JIT_RV32_M
    {
        static const nj_u1 first[] = {0x6c,0x04};
        static const nj_u1 middle[] = {0x04,0x6c};
        test_name = "division isolation and zero guard contract";
        m = method(first, sizeof(first), 0);
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 1 && block.insns == 1 && block.nfaults == 1);
        CHECK(block.faults[0].pc_off == 0 && block.faults[0].sp_delta == 0);
        CHECK(block.min_stack == 2 && block.stack_delta == -1 && block.max_growth == 0);
        m = method(middle, sizeof(middle), 0);
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 1 && block.insns == 1 && block.nfaults == 0);
        /* A throwing division may not follow mutations; the VM owns throwing. */
    }
#endif
}

/* Poison host frm, so a dynamic-rounding encoding would produce the wrong bit.
 * Only RV32 code may execute these CSRs. The C compiler still targets F/D even
 * when the generator's independent feature switches are disabled. */
static void test_rounding(void)
{
#if EXECUTE_RV32 && NJ_JIT_RV32_F
    nj_u1 code = 0x62, generated[NJ_JIT_MAX_CODE_BYTES];
    nj_method m = method(&code, 1, 0);
    nj_jit_block block;
    test_frame f;
    nj_u4 word;
    unsigned old_frm;
    init_frame(&f);
    f.stack[BASE].bits = 0x3f800000u;
    f.stack[BASE + 1].bits = 0x33800000u; /* halfway: 1 + 2^-24 */
    test_name = "fixed float RNE with host rounding up";
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
    __asm__ volatile("csrrw %0, frm, %1" : "=r"(old_frm) : "r"(3u));
    word = invoke(generated, block.size, &f.stack[BASE + 2], f.locals,
                  block.insns);
    __asm__ volatile("csrw frm, %0" : : "r"(old_frm));
    expect_success(word, &block, block.insns);
    CHECK(f.stack[BASE].bits == 0x3f800000u);
#if NJ_JIT_RV32_D && NJ_ENABLE_LONG
    {
        uint64_t one = UINT64_C(0x3ff0000000000000), half = UINT64_C(0x3ca0000000000000), got;
        code = 0x63;
        memcpy(&f.stack[BASE], &one, 8); memcpy(&f.stack[BASE + 2], &half, 8);
        test_name = "fixed double RNE with host rounding up";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        __asm__ volatile("csrrw %0, frm, %1" : "=r"(old_frm) : "r"(3u));
        word = invoke(generated, block.size, &f.stack[BASE + 4], f.locals,
                      block.insns);
        __asm__ volatile("csrw frm, %0" : : "r"(old_frm));
        expect_success(word, &block, block.insns);
        memcpy(&got, &f.stack[BASE], 8);
        CHECK(got == one);
    }
#endif
#endif
}

/* A raw class-file CP fixture: offsets point at the four-byte payload. */
typedef struct constant_fixture {
    nj_class owner;
    nj_u4 info[260];
    nj_u1 bytes[32];
} constant_fixture;
static void init_constants(constant_fixture *pool)
{
    memset(pool, 0, sizeof(*pool));
    pool->owner.cp_info = pool->info; pool->owner.cp_count = 260;
    pool->owner.data = pool->bytes; pool->owner.data_len = sizeof(pool->bytes);
}
static void put_constant(constant_fixture *pool, unsigned index, unsigned offset,
                         unsigned tag, nj_u4 bits)
{
    CHECK(index < 260 && offset > 0 && offset + 4 <= sizeof(pool->bytes));
    pool->info[index] = (offset << 4) | tag;
    pool->bytes[offset] = (nj_u1)(bits >> 24);
    pool->bytes[offset + 1] = (nj_u1)(bits >> 16);
    pool->bytes[offset + 2] = (nj_u1)(bits >> 8);
    pool->bytes[offset + 3] = (nj_u1)bits;
}
static void constant_declined(const char *name, nj_method *m)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES], code[8];
    nj_jit_block block;
    nj_method prefix = *m;
    test_name = name;
    CHECK(!compile_checked(m, 0, generated, sizeof(generated), &block));
    CHECK(m->code_len + 1 <= sizeof(code));
    code[0] = 0x04; memcpy(code + 1, m->code, m->code_len);
    prefix.code = code; prefix.code_len = m->code_len + 1;
    CHECK(compile_checked(&prefix, 0, generated, sizeof(generated), &block));
    CHECK(block.next_pc == 1 && block.insns == 1 && block.stack_delta == 1);
    inspect(generated, block.size);
}

/* Retry every capacity and execute each accepted prefix. This checks rollback
 * across a cached register update, a constant load, and FP/wide stores. */
static void capacity_owned(const nj_u1 *code, size_t length, int depth,
                           nj_class *owner, const test_frame *initial)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES + 32];
    nj_jit_block block;
    nj_method m = method(code, length, 4);
    size_t cap, full_size, i;
    m.owner = owner;
    test_name = "cached prefix capacity rollback";
    CHECK(compile_checked(&m, 0, generated, NJ_JIT_MAX_CODE_BYTES, &block));
    CHECK(block.next_pc == length); full_size = block.size;
    CHECK(full_size + 4 <= NJ_JIT_MAX_CODE_BYTES);
    for (cap = 0; cap <= full_size + 4; ++cap) {
        int ok;
        memset(generated, 0xa5, sizeof(generated));
        ok = compile_checked(&m, 0, generated + 16, (nj_size)cap, &block);
        for (i = 0; i < 16; ++i) CHECK(generated[i] == 0xa5);
        for (i = 16 + cap; i < sizeof(generated); ++i) CHECK(generated[i] == 0xa5);
        if (ok) {
            test_frame expected = *initial;
            unsigned count;
            int end;
            CHECK(block.size <= cap && block.insns > 0 && block.next_pc <= length);
            inspect(generated + 16, block.size);
            end = reference(code, block.next_pc, expected.stack, expected.locals,
                            BASE + depth, owner, &count);
            CHECK(block.insns == count && block.stack_delta == end - BASE - depth);
#if EXECUTE_RV32
            {
                test_frame actual = *initial;
                nj_u4 word = invoke(generated + 16, block.size,
                                    &actual.stack[BASE + depth], actual.locals,
                                    block.insns);
                expect_success(word, &block, block.insns);
                CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
            }
#endif
        }
    }
}
static void test_numeric_constants(void)
{
    static const nj_u4 values[] = {0,1,UINT32_MAX,0x80000000u,0x7fffffffu,
        1664525u,1013904223u,0x00000800u,0xfffff800u,0x7fc12345u,0x7fa12345u,
        0xff800000u,0x3f800000u};
    static const unsigned indices[] = {1,255,256,259};
    constant_fixture pool;
    test_frame f;
    size_t i, j;
    unsigned tag;
    init_frame(&f);
    for (tag = 3; tag <= 4; ++tag)
        for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
            for (j = 0; j < sizeof(indices)/sizeof(indices[0]); ++j) {
                nj_u1 code[8];
                size_t length = 0;
                unsigned index = indices[j];
                init_constants(&pool); put_constant(&pool, index, 1, tag, values[i]);
                if (index <= 255) { code[length++] = 0x12; code[length++] = (nj_u1)index; }
                else {
                    code[length++] = 0x13; code[length++] = (nj_u1)(index >> 8);
                    code[length++] = (nj_u1)index;
                }
                code[length++] = 0x59; code[length++] = 0x3b;
                code[length++] = 0x1a; code[length++] = 0x57;
                /* Float entries use raw bits even when VM/hardware FP is off,
                 * exactly as the interpreter's ldc implementation does. */
                program_owned("ldc raw Integer/Float slots", code, length, 1, 0, &f, &pool.owner);
            }
    for (i = 0; i < 16; ++i) {
        nj_u1 code[] = {0x12,1};
        nj_method m = method(code, sizeof(code), 1);
        init_constants(&pool); put_constant(&pool, 1, 1, 3, 0x12345678);
        m.owner = &pool.owner;
        switch (i) {
        case 0: m.owner = NULL; break;
        case 1: pool.owner.cp_info = NULL; break;
        case 2: pool.owner.data = NULL; break;
        case 3: pool.owner.cp_count = 0; break;
        case 4: pool.owner.cp_count = 1; break;
        case 5: code[1] = 0; break;
        case 6: code[1] = 2; pool.owner.cp_count = 2; break;
        case 7: pool.info[1] = 3; break; /* zero offset */
        case 8: pool.info[1] = (32u << 4) | 3u; break;
        case 9: pool.info[1] = (33u << 4) | 3u; break;
        case 10: pool.info[1] = 0xfffffff3u; break;
        case 11: pool.owner.data_len = 0; break;
        case 12: pool.owner.data_len = 1; break;
        case 13: pool.owner.data_len = 2; break;
        case 14: pool.owner.data_len = 3; break;
        case 15: pool.owner.data_len = 4; break;
        }
        constant_declined("invalid constant pool metadata", &m);
    }
    {
        static const unsigned tags[] = {0,1,5,6,7,8,9,10,11,12,15};
        static const nj_u1 code[] = {0x13,1,0};
        nj_method m = method(code, sizeof(code), 1);
        for (i = 0; i < sizeof(tags)/sizeof(tags[0]); ++i) {
            init_constants(&pool); put_constant(&pool, 256, 1, tags[i], 0x12345678);
            m.owner = &pool.owner;
            constant_declined("unsupported CP tag requires interpreter", &m);
        }
    }
    {
        static const nj_u1 bad[][3] = {{0x12},{0x13},{0x13,1},
                                      {0x13,0,0},{0x13,1,4},{0x13,0xff,0xff}};
        static const unsigned lengths[] = {1,1,2,3,3,3};
        for (i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
            nj_method m = method(bad[i], lengths[i], 1);
            init_constants(&pool); put_constant(&pool, 1, 1, 3, 0x12345678);
            m.owner = &pool.owner;
            constant_declined("truncated ldc or out-of-range wide CP index", &m);
        }
    }
#if defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        nj_u1 *mapping;
        static const nj_u1 code[] = {0x12,1};
        nj_method m = method(code, sizeof(code), 1);
        test_name = "constant payload at inaccessible page";
        CHECK(page > 0);
        mapping = mmap(NULL, (size_t)page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(mapping != MAP_FAILED);
        CHECK(mprotect(mapping, (size_t)page, PROT_READ | PROT_WRITE) == 0);
        init_constants(&pool); pool.info[1] = (1u << 4) | 3u;
        m.owner = &pool.owner;
        for (i = 0; i < 4; ++i) {
            pool.owner.data = mapping + page - i - 1; pool.owner.data_len = (nj_u4)i + 1;
            constant_declined("truncated CP payload against guard page", &m);
        }
        pool.owner.data = mapping + page - 5; pool.owner.data_len = 5;
        memcpy(mapping + page - 4, "\x80\x00\x00\x01", 4);
        program_owned("last four readable CP bytes", code, sizeof(code), 1, 0, &f, &pool.owner);
        for (i = 1; i <= 2; ++i) {
            nj_u1 *edge = mapping + page - i;
            edge[0] = i == 1 ? 0x12 : 0x13;
            if (i == 2) edge[1] = 1;
            m.code = edge; m.code_len = (nj_u4)i;
            constant_declined("truncated ldc operand against guard page", &m);
        }
        CHECK(munmap(mapping, (size_t)page * 2) == 0);
    }
#endif
}

static void test_analysis(void)
{
    static const nj_u1 code[] = {0x04,0xac};
    nj_method m = method(code, sizeof(code), 1);
    nj_jit_block block;
    test_name = "analysis rejects invalid input and identifies short block";
    CHECK(!nj_rv32_analyze(NULL, NULL, 0, &block));
    CHECK(!nj_rv32_analyze(NULL, &m, 0, NULL));
    CHECK(!nj_rv32_analyze(NULL, &m, sizeof(code), &block));
    CHECK(!nj_rv32_analyze(NULL, &m, UINT32_MAX, &block));
    CHECK(nj_rv32_analyze(NULL, &m, 0, &block));
    CHECK(block.size == 0 && block.insns == 1 && block.next_pc == 1);
    CHECK(block.min_stack == 0 && block.max_growth == 1 && block.stack_delta == 1);
    CHECK(block.nfaults == 0);
    m.max_stack = 0; CHECK(!nj_rv32_analyze(NULL, &m, 0, &block));
    m.code = NULL; CHECK(!nj_rv32_analyze(NULL, &m, 0, &block));
    test_name = "new opcodes are candidates";
    CHECK(nj_jit_opcode_candidate(0x2e));
    CHECK(nj_jit_opcode_candidate(0x4f));
    CHECK(nj_jit_opcode_candidate(0xb4));
    CHECK(nj_jit_opcode_candidate(0xb5));
    CHECK(nj_jit_opcode_candidate(0xbe));
}

static void test_write_through(void)
{
    static const nj_u1 aliases[] = {0x04,0x3b,0x1a,0x05,0x60,0x3b,0x1a,
                                    0x84,0,0xff,0x1a,0x60,0x3c,0x1b};
    static const nj_u1 copies[] = {0x04,0x05,0x5f,0x5c,0x58,0x60,0x59,0x3b,0x1a,0x82};
    static const nj_u1 overlap[] = {0x1a,0x04,0x60,0x3c,0x1b,0x3b,
                                    0x84,0,1,0x1a,0x59,0x5f,0x57};
    static const unsigned local_offsets[] = {0,BASE - 1,BASE,BASE + 1};
    test_frame f;
    size_t i;
    init_frame(&f);
    program("write-through local invalidation", aliases, sizeof(aliases), 4, 0, &f);
    program("write-through complete-slot stack copies", copies, sizeof(copies), 4, 0, &f);
    capacity_owned(aliases, sizeof(aliases), 0, NULL, &f);
    capacity_owned(copies, sizeof(copies), 0, NULL, &f);
    {
        static const nj_u1 operators[] = {0x60,0x64,0x78,0x7a,0x7c,0x7e,0x80,0x82,
#if NJ_JIT_RV32_M
            0x68,
#endif
        };
        nj_u4 state = 0x913766abu;
        size_t op, trial, local;
        for (op = 0; op < sizeof(operators); ++op)
            for (trial = 0; trial < 32; ++trial) {
                nj_u1 code[] = {0x1a,0x1b,operators[op],0x1c,operators[op],
                    0x1d,operators[op],0x3b,0x1a,0x59,operators[op]};
                for (local = 0; local < 4; ++local) {
                    state = state * 1664525u + 1013904223u;
                    f.locals[local].bits = state;
                }
                program("cached operand order and random dependency chains", code, sizeof(code), 4, 0, &f);
            }
    }
    for (i = 0; i < sizeof(local_offsets)/sizeof(local_offsets[0]); ++i) {
        nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
        nj_method m = method(overlap, sizeof(overlap), 4);
        nj_jit_block block;
        test_frame expected = f;
        unsigned count;
        int end;
        test_name = "stack and locals base alias/partial overlap";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == sizeof(overlap)); inspect(generated, block.size);
        end = reference(overlap, sizeof(overlap), expected.stack,
                        expected.stack + local_offsets[i], BASE, NULL, &count);
        CHECK(block.insns == count && block.stack_delta == end - BASE);
        ++compiled;
#if EXECUTE_RV32
        {
            test_frame actual = f;
            nj_u4 word = invoke(generated, block.size, actual.stack + BASE,
                                actual.stack + local_offsets[i], block.insns);
            expect_success(word, &block, block.insns);
            CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
        }
#endif
    }
#if NJ_ENABLE_LONG
    {
        static const nj_u1 wide[] = {0x0a,0x3f,0x1b,0x04,0x60,0x3c,
                                     0x1e,0x5c,0x37,0,0x58,0x1b};
        program("wide copy and low-half padding invalidation", wide, sizeof(wide), 4, 0, &f);
        capacity_owned(wide, sizeof(wide), 0, NULL, &f);
    }
#endif
#if NJ_JIT_RV32_F && NJ_ENABLE_FLOAT
    {
        static const nj_u1 fp[] = {0x0c,0x0d,0x62,0x59,0x3b,0x1a,0x60};
        program("FP store invalidates cached integer bits", fp, sizeof(fp), 4, 0, &f);
        capacity_owned(fp, sizeof(fp), 0, NULL, &f);
    }
#endif
#if NJ_JIT_RV32_D && NJ_ENABLE_LONG && NJ_ENABLE_FLOAT
    {
        static const nj_u1 fp[] = {0x0f,0x0f,0x63,0x5c,0x47,0x26,0x63,0x58,0x1b};
        program("double store invalidates both halves and padding", fp, sizeof(fp), 4, 0, &f);
        capacity_owned(fp, sizeof(fp), 0, NULL, &f);
    }
#endif
}

static void test_constant_chain(void)
{
#if NJ_JIT_RV32_M
    static const nj_u1 chain[] = {
        0x1a,0x12,1,0x68,0x13,1,0,0x60,0x3b,
        0x1a,0x12,1,0x68,0x13,1,0,0x60,0x3b,
        0x1a,0x12,1,0x68,0x13,1,0,0x60,0x3b};
    constant_fixture pool;
    test_frame f;
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    nj_method m = method(chain, sizeof(chain), 4);
    unsigned i, loads = 0, stores = 0;
    size_t p;
    init_constants(&pool);
    put_constant(&pool, 1, 1, 3, 1664525u);
    put_constant(&pool, 256, 5, 3, 1013904223u);
    init_frame(&f);
    for (i = 0; i < 64; ++i) {
        f.locals[0].bits = i * 0x9e3779b9u;
        program_owned("ldc multiply-add long block", chain, sizeof(chain), 4, 0, &f, &pool.owner);
    }
    capacity_owned(chain, sizeof(chain), 0, &pool.owner, &f);
    m.owner = &pool.owner; test_name = "LCG emitted code sample";
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
    CHECK(block.next_pc == sizeof(chain) && block.insns == 18 && block.stack_delta == 0);
    for (p = 0; p < block.size;) {
        if ((generated[p] & 3u) != 3u) {
            unsigned half = generated[p] | (unsigned)generated[p + 1] << 8;
            unsigned kind = half & 0xe003u;
            if (kind == 0x4000u || kind == 0x4002u) ++loads;
            if (kind == 0xc000u || kind == 0xc002u) ++stores;
            p += 2;
        } else {
            nj_u4 instruction = word(generated + p);
            if ((instruction & 0x707fu) == 0x2003u) ++loads;
            if ((instruction & 0x707fu) == 0x2023u) ++stores;
            p += 4;
        }
    }
    printf("SAMPLE RV32 cache=%d insns=%u bytes=%lu loads=%u stores=%u\n",
           NJ_JIT_RV32_REG_CACHE, block.insns, (unsigned long)block.size, loads, stores);
#endif
}

/* ---- branches and loops ---------------------------------------------- */

static size_t oracle_branch_target(const nj_u1 *code, size_t operand_pc,
                                   size_t length)
{
    nj_s4 target = (nj_s4)(operand_pc - 1u) +
        (nj_s4)(int16_t)((unsigned)code[operand_pc] << 8 | code[operand_pc + 1]);
    CHECK(target >= 0 && (size_t)target <= length);
    return (size_t)target;
}

/* Compile a non-loop branch block at pc 0, execute it natively once and
 * compare the whole frame against reference(). Returns the raw ABI word and
 * the oracle's completed-bytecode count. */
static nj_u4 branch_run(const char *name, const nj_u1 *code, size_t length,
                        unsigned local_count, const test_frame *initial,
                        nj_u4 budget, nj_jit_block *out, test_frame *expected,
                        unsigned *count)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_method m = method(code, length, local_count);
    test_name = name;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), out) == 1);
    CHECK(out->next_pc <= length);
    inspect(generated, out->size);
    ++compiled;
    *expected = *initial;
    {
        int end = reference(code, length, expected->stack, expected->locals,
                            BASE, NULL, count);
        (void)end;
    }
    (void)budget;
#if EXECUTE_RV32
    {
        test_frame actual = *initial;
        nj_u4 word = invoke(generated, out->size, &actual.stack[BASE],
                            actual.locals, budget);
        CHECK(!result_is_fault(word));
        CHECK(result_exit(word) < out->nexits);
        CHECK(result_completed(word) <= budget);
        CHECK(result_completed(word) == *count); /* window ends where the oracle stops */
        compare_frames(name, &actual, expected);
        return word;
    }
#else
    return 0;
#endif
}

/* Focused interpreter for loops over int locals, int arrays and one int
 * instance field. It models exactly the bytecodes the loop tests emit and
 * stops before stop_pc (used to model a guard fault). */
typedef struct loop_env {
    nj_s4 *ints;
    unsigned ints_len;
    nj_u1 *field_obj;
    unsigned field_off;
} loop_env;

static int loop_oracle(const nj_u1 *code, size_t length, test_frame *frame,
                       int sp, const loop_env *env, size_t stop_pc,
                       unsigned *count)
{
    size_t pc = 0;
    unsigned steps = 0;
    *count = 0;
    while (pc < length) {
        unsigned op;
        if (pc == stop_pc) break;
        op = code[pc++];
        if (++steps > 1000000u) { CHECK(!"loop oracle step limit"); break; }
        if (op == 0xacu) break;
        ++*count;
        switch (op) {
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08:
            frame->stack[sp++].i = (int)op - 3;
            break;
        case 0x10:
            frame->stack[sp++].i = (int8_t)code[pc++];
            break;
        case 0x1a: case 0x1b: case 0x1c: case 0x1d:
            frame->stack[sp++].bits = frame->locals[op - 0x1au].bits;
            break;
        case 0x15: case 0x19: {
            unsigned index = code[pc++];
            if (op == 0x19u)
                frame->stack[sp++].r = frame->locals[index].r;
            else
                frame->stack[sp++].bits = frame->locals[index].bits;
            break;
        }
        case 0x2a: case 0x2b: case 0x2c: case 0x2d:
            frame->stack[sp++].r = frame->locals[op - 0x2au].r;
            break;
        case 0x3b: case 0x3c: case 0x3d: case 0x3e:
            frame->locals[op - 0x3bu].bits = frame->stack[--sp].bits;
            break;
        case 0x36: case 0x3a: {
            unsigned index = code[pc++];
            if (op == 0x3au)
                frame->locals[index].r = frame->stack[--sp].r;
            else
                frame->locals[index].bits = frame->stack[--sp].bits;
            break;
        }
        case 0x84: {
            unsigned index = code[pc++];
            int increment = (int8_t)code[pc++];
            frame->locals[index].bits += (nj_u4)increment;
            break;
        }
        case 0x60: case 0x64: {
            nj_u4 a = frame->stack[sp - 2].bits, b = frame->stack[sp - 1].bits;
            frame->stack[sp - 2].bits = op == 0x60u ? a + b : a - b;
            --sp;
            break;
        }
        case 0x2e: {
            int index = frame->stack[sp - 1].i;
            if (!env || !env->ints || index < 0 ||
                (unsigned)index >= env->ints_len) {
                --*count;
                return (int)*count; /* the JIT guard faults here */
            }
            frame->stack[sp - 2].i = env->ints[index];
            --sp;
            break;
        }
        case 0x4f: {
            int index = frame->stack[sp - 2].i;
            if (!env || !env->ints || index < 0 ||
                (unsigned)index >= env->ints_len) {
                --*count;
                return (int)*count; /* the JIT guard faults here */
            }
            env->ints[index] = frame->stack[sp - 1].i;
            sp -= 3;
            break;
        }
        case 0xb4: {
            nj_u4 value = 0;
            CHECK(code[pc] == 0u && code[pc + 1] == 1u);
            CHECK(env && env->field_obj);
            memcpy(&value, env->field_obj + env->field_off, 4);
            frame->stack[sp - 1].bits = value;
            pc += 2;
            break;
        }
        case 0xb5: {
            CHECK(code[pc] == 0u && code[pc + 1] == 1u);
            CHECK(env && env->field_obj);
            memcpy(env->field_obj + env->field_off, &frame->stack[sp - 2].bits, 4);
            pc += 2;
            sp -= 2;
            break;
        }
        case 0xa7:
            pc = oracle_branch_target(code, pc, length);
            break;
        case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: {
            int v = frame->stack[--sp].i;
            int taken = op == 0x99u ? v == 0 :
                        op == 0x9au ? v != 0 :
                        op == 0x9bu ? v < 0 :
                        op == 0x9cu ? v >= 0 :
                        op == 0x9du ? v > 0 : v <= 0;
            pc = taken ? oracle_branch_target(code, pc, length) : pc + 2;
            break;
        }
        case 0x9f: case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: {
            int b = frame->stack[--sp].i;
            int a = frame->stack[--sp].i;
            int taken = op == 0x9fu ? a == b :
                        op == 0xa0u ? a != b :
                        op == 0xa1u ? a < b :
                        op == 0xa2u ? a >= b :
                        op == 0xa3u ? a > b : a <= b;
            pc = taken ? oracle_branch_target(code, pc, length) : pc + 2;
            break;
        }
        case 0xa5: case 0xa6: {
            nj_ref b = frame->stack[--sp].r;
            nj_ref a = frame->stack[--sp].r;
            int taken = (a == b) == (op == 0xa5u);
            pc = taken ? oracle_branch_target(code, pc, length) : pc + 2;
            break;
        }
        case 0xc6: case 0xc7: {
            nj_ref v = frame->stack[--sp].r;
            int taken = (v == NULL) == (op == 0xc6u);
            pc = taken ? oracle_branch_target(code, pc, length) : pc + 2;
            break;
        }
        default:
            CHECK(!"loop oracle missing opcode");
            return sp;
        }
    }
    return sp;
}

static nj_u4 loop_run(const char *name, const nj_u1 *code, size_t length,
                      unsigned local_count, const test_frame *initial,
                      nj_class *owner, nj_s4 *ints, unsigned ints_len,
                      nj_u1 *field_obj, unsigned field_off, size_t stop_pc,
                      nj_u4 budget, nj_jit_block *out, test_frame *expected,
                      unsigned *count)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_method m = method(code, length, local_count);
    loop_env env;
    *out = (nj_jit_block){0};
    m.owner = owner;
    test_name = name;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), out) == 1);
    CHECK(out->next_pc <= length);
    inspect(generated, out->size);
    ++compiled;
    env.ints = ints;
    env.ints_len = ints_len;
    env.field_obj = field_obj;
    env.field_off = field_off;
    /* The oracle may mutate the array or the field; run it on copies so the
     * native call starts from the same clean fixture. */
    {
        nj_s4 *copy_ints = NULL;
        nj_u1 field_copy[4];
        if (ints != NULL && ints_len != 0u) {
            copy_ints = (nj_s4 *)malloc((size_t)ints_len * sizeof(*copy_ints));
            CHECK(copy_ints != NULL);
            memcpy(copy_ints, ints, (size_t)ints_len * sizeof(*copy_ints));
            env.ints = copy_ints;
        }
        if (field_obj != NULL) {
            memcpy(field_copy, field_obj + field_off, sizeof(field_copy));
            env.field_obj = field_copy;
            env.field_off = 0u;
        }
        *expected = *initial;
        (void)loop_oracle(code, length, expected, BASE, &env, stop_pc, count);
        free(copy_ints);
    }
    (void)budget;
#if EXECUTE_RV32
    {
        test_frame actual = *initial;
        nj_u4 word = invoke(generated, out->size, &actual.stack[BASE],
                            actual.locals, budget);
        {
            int delta;
            if (result_is_fault(word)) {
                unsigned index = result_fault_index(word);
                CHECK(index < out->nfaults);
                delta = out->faults[index].sp_delta;
            } else {
                unsigned which = result_exit(word);
                CHECK(which < out->nexits);
                CHECK(result_completed(word) <= budget);
                delta = out->exits[which].sp_delta;
            }
            if (result_is_fault(word) || result_exit(word) == 0)
                CHECK(result_completed(word) == *count);
            compare_live_frames(name, &actual, expected, BASE + delta);
        }
        return word;
    }
#else
    return 0;
#endif
}

static void test_branches(void)
{
    nj_jit_block block;
    test_frame initial, expected;
    nj_u4 word = 0;
    unsigned count = 0, i;

    /* if<cond>: a taken branch skips a two-bytecode then-block. */
    {
        static const unsigned ops[] = {0x99u, 0x9au, 0x9bu, 0x9cu, 0x9du, 0x9eu};
        static const int values[6][2] = {
            {0, 5}, {5, 0}, {-1, 1}, {1, -1}, {1, -1}, {-1, 1}
        };
        for (i = 0; i < 6; ++i) {
            nj_u1 code[8];
            int k;
            code[0] = 0x1a; code[1] = (nj_u1)ops[i];
            code[2] = 0x00; code[3] = 0x05;
            code[4] = 0x04; code[5] = 0x3c; code[6] = 0x00; code[7] = 0xac;
            for (k = 0; k < 2; ++k) {
                init_frame(&initial);
                initial.locals[0].i = values[i][k];
                initial.locals[1].i = 0x1234;
                word = branch_run("if<cond> family", code, sizeof(code), 2,
                                  &initial, 32, &block, &expected, &count);
                CHECK(block.nexits == 1u);
                CHECK(count == (k == 0 ? 3u : 5u));
#if EXECUTE_RV32
                CHECK(!result_is_fault(word));
                CHECK(result_exit(word) == 0u);
                CHECK(result_completed(word) == count);
#endif
            }
        }
    }

    /* if_icmp<cond>: all six comparisons, both outcomes. */
    {
        static const unsigned ops[] = {0x9fu, 0xa0u, 0xa1u, 0xa2u, 0xa3u, 0xa4u};
        static const int values[6][2][2] = {
            {{1, 1}, {1, 2}}, {{1, 2}, {1, 1}}, {{-3, 2}, {2, -3}},
            {{2, -3}, {-3, 2}}, {{2, -3}, {-3, 2}}, {{-3, 2}, {2, -3}}
        };
        for (i = 0; i < 6; ++i) {
            nj_u1 code[8];
            int k;
            code[0] = 0x1a; code[1] = 0x1b; code[2] = (nj_u1)ops[i];
            code[3] = 0x00; code[4] = 0x05;
            code[5] = 0x04; code[6] = 0x3c; code[7] = 0xac;
            for (k = 0; k < 2; ++k) {
                init_frame(&initial);
                initial.locals[0].i = values[i][k][0];
                initial.locals[1].i = values[i][k][1];
                initial.locals[2].i = 0x55aa;
                word = branch_run("if_icmp<cond> family", code, sizeof(code),
                                  3, &initial, 32, &block, &expected, &count);
                CHECK(block.nexits == 1u);
                CHECK(count == (k == 0 ? 3u : 5u));
#if EXECUTE_RV32
                CHECK(!result_is_fault(word));
                CHECK(result_exit(word) == 0u);
                CHECK(result_completed(word) == count);
#endif
            }
        }
    }

    /* if_acmpeq/ne over real reference values. */
    {
        nj_object oa, ob;
        nj_ref pairs[5][2];
        memset(&oa, 0, sizeof(oa));
        memset(&ob, 0, sizeof(ob));
        pairs[0][0] = (nj_ref)&oa; pairs[0][1] = (nj_ref)&oa; /* eq taken */
        pairs[1][0] = (nj_ref)&oa; pairs[1][1] = (nj_ref)&ob; /* eq fall */
        pairs[2][0] = (nj_ref)&oa; pairs[2][1] = (nj_ref)&ob; /* ne taken */
        pairs[3][0] = (nj_ref)&oa; pairs[3][1] = (nj_ref)&oa; /* ne fall */
        pairs[4][0] = NULL;        pairs[4][1] = NULL;        /* eq taken */
        for (i = 0; i < 5; ++i) {
            static const nj_u1 code_eq[] = {0x2a, 0x2b, 0xa5, 0x00, 0x05,
                                            0x04, 0x3c, 0xac};
            static const nj_u1 code_ne[] = {0x2a, 0x2b, 0xa6, 0x00, 0x05,
                                            0x04, 0x3c, 0xac};
            const nj_u1 *code = (i == 2u || i == 3u) ? code_ne : code_eq;
            int taken = i != 1u && i != 3u;
            init_frame(&initial);
            initial.locals[0].r = pairs[i][0];
            initial.locals[1].r = pairs[i][1];
            initial.locals[2].i = 0x77;
            word = branch_run("if_acmp eq/ne", code, 8, 3, &initial, 32,
                              &block, &expected, &count);
            CHECK(block.nexits == 1u);
            CHECK(count == (taken ? 3u : 5u));
#if EXECUTE_RV32
            CHECK(!result_is_fault(word));
            CHECK(result_exit(word) == 0u);
            CHECK(result_completed(word) == count);
#endif
        }
    }

    /* ifnull/ifnonnull. */
    {
        nj_object oa;
        memset(&oa, 0, sizeof(oa));
        for (i = 0; i < 4; ++i) {
            nj_u1 code[7];
            int taken = i == 0u || i == 2u;
            init_frame(&initial);
            code[0] = 0x2a;
            code[1] = (i < 2u) ? 0xc6u : 0xc7u;
            code[2] = 0x00; code[3] = 0x05;
            code[4] = 0x04; code[5] = 0x3c; code[6] = 0xac;
            initial.locals[0].r = (i == 0u || i == 3u)
                                      ? NULL : (nj_ref)&oa;
            initial.locals[1].i = 0x33;
            word = branch_run("ifnull/ifnonnull", code, sizeof(code), 2,
                              &initial, 32, &block, &expected, &count);
            CHECK(block.nexits == 1u);
            CHECK(count == (taken ? 2u : 4u));
#if EXECUTE_RV32
            CHECK(!result_is_fault(word));
            CHECK(result_exit(word) == 0u);
            CHECK(result_completed(word) == count);
#endif
        }
    }

    /* At a join, an immediate known on the fall-through path is not known
     * on the taken path. Reusing that compile-time constant drops the load. */
    {
        static const nj_u1 code[] = {
            0x1a, 0x99, 0x00, 0x05, /* iload_0; ifeq 6 */
            0x04, 0x3c,             /* iconst_1; istore_1 */
            0x04, 0x3d,             /* iconst_1; istore_2 */
            0xac
        };
        for (i = 0; i < 2; ++i) {
            init_frame(&initial);
            initial.locals[0].i = (nj_s4)i;
            word = branch_run("constant at control-flow join", code,
                              sizeof(code), 3, &initial, 32, &block,
                              &expected, &count);
            CHECK(expected.locals[2].i == 1);
#if EXECUTE_RV32
            CHECK(!result_is_fault(word));
            CHECK(result_exit(word) == 0u);
            CHECK(result_completed(word) == count);
#endif
        }
    }

    /* A forward goto that lands on an instruction inside the window. */
    {
        static const nj_u1 code[] = {
            0xa7, 0x00, 0x05,       /* goto 5 */
            0x04, 0x3b,             /* iconst_1; istore_0 (skipped) */
            0x05, 0x3b,             /* iconst_2; istore_0 */
            0xac
        };
        init_frame(&initial);
        initial.locals[0].i = 0x600d;
        word = branch_run("forward goto inside window", code, sizeof(code), 1,
                          &initial, 16, &block, &expected, &count);
        CHECK(block.nexits == 1u);
        CHECK(block.insns == 5u);
        CHECK(count == 3u);
        CHECK(expected.locals[0].i == 2);
#if EXECUTE_RV32
        CHECK(!result_is_fault(word));
        CHECK(result_exit(word) == 0u);
        CHECK(result_completed(word) == 3u);
#endif
    }

    /* A forward branch whose target equals the window end but whose stack
     * depth differs from the fall-through end needs its own exit record. */
#if NJ_JIT_MAX_EXITS >= 2
    {
        nj_u1 code[] = {
            0x04, 0x04, 0x9f, 0x00, 0x05, /* iconst_1; iconst_1; if_icmpeq 7 */
            0x06,                          /* iconst_3 */
            0x00, 0xac                     /* nop; return */
        };
        init_frame(&initial);
        initial.locals[0].i = 1;
        initial.locals[1].i = 1;
        word = branch_run("branch target at window end, distinct exit", code,
                          sizeof(code), 2, &initial, 16, &block, &expected,
                          &count);
        CHECK(block.loop == 0);
        CHECK(block.nexits == 2u);
        CHECK(block.exits[0].pc_off == 7u && block.exits[0].sp_delta == 1);
        CHECK(block.exits[1].pc_off == 7u && block.exits[1].sp_delta == 0);
        CHECK(count == 3u);
#if EXECUTE_RV32
        CHECK(!result_is_fault(word));
        CHECK(result_exit(word) == 1u);
        CHECK(result_completed(word) == 3u);
#endif
        init_frame(&initial);
        initial.locals[0].i = 1;
        initial.locals[1].i = 2;
        code[1] = 0x05; /* iconst_2: 1 != 2, so fall through */
        word = branch_run("branch not taken, fall-through exit", code,
                          sizeof(code), 2, &initial, 16, &block, &expected,
                          &count);
        CHECK(block.nexits == 2u);
        CHECK(count == 5u);
        CHECK(expected.stack[BASE].i == 3);
#if EXECUTE_RV32
        CHECK(!result_is_fault(word));
        CHECK(result_exit(word) == 0u);
        CHECK(result_completed(word) == 5u);
#endif
    }
#endif
    (void)word;
}

static void test_loops(void)
{
#if NJ_JIT_MAX_EXITS >= 2
    nj_jit_block block;
    test_frame initial, expected;
    nj_u4 word = 0;
    unsigned count = 0;

    /* Do-while sum: sum(1..n), n = 2000, executed natively in one call. */
    {
        static const nj_u1 code[] = {
            0x84, 0x00, 0x01,             /* iinc 0, 1 */
            0x1c, 0x1a, 0x60, 0x3d,       /* iload_2; iload_0; iadd; istore_2 */
            0x1a, 0x1b,                   /* iload_0; iload_1 */
            0xa1, 0xff, 0xf7,             /* if_icmplt 0 (i < n) */
            0xac
        };
        init_frame(&initial);
        initial.locals[0].i = 0;
        initial.locals[1].i = 2000;
        initial.locals[2].i = 0;
        word = loop_run("loop sum 1..n", code, sizeof(code), 3, &initial,
                        NULL, NULL, 0, NULL, 0, SIZE_MAX, 100000, &block,
                        &expected, &count);
        CHECK(block.loop == 1);
        CHECK(block.nexits >= 2u);
        CHECK(block.insns == 8u);
        CHECK(block.exits[0].pc_off == 12u && block.exits[0].sp_delta == 0);
        CHECK(block.exits[block.nexits - 1u].pc_off == 0u &&
              block.exits[block.nexits - 1u].sp_delta == 0);
        CHECK(count == 16000u);
        CHECK(expected.locals[0].i == 2000);
        CHECK(expected.locals[1].i == 2000);
        CHECK(expected.locals[2].i == 2001000);
#if EXECUTE_RV32
        CHECK(!result_is_fault(word));
        CHECK(result_exit(word) == 0u);
        CHECK(result_completed(word) == 16000u);
#endif
    }

    /* Small budget: one iteration per call, always leaving through the budget
     * exit, with the frame advancing across calls. */
    {
        static const nj_u1 code[] = {
            0x84, 0x00, 0x01,
            0x1c, 0x1a, 0x60, 0x3d,
            0x1a, 0x1b,
            0xa1, 0xff, 0xf7,
            0xac
        };
        nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
        nj_method m = method(code, sizeof(code), 3);
        unsigned budget_exit = 0u, k;
        test_name = "loop budget exit";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 1);
        CHECK(block.loop == 1);
        inspect(generated, block.size);
        ++compiled;
        for (k = 1; k < block.nexits; ++k)
            if (block.exits[k].pc_off == 0u && block.exits[k].sp_delta == 0)
                budget_exit = k;
        CHECK(budget_exit != 0u);
        init_frame(&initial);
        initial.locals[0].i = 0;
        initial.locals[1].i = 2000;
        initial.locals[2].i = 0;
#if EXECUTE_RV32
        {
            test_frame actual = initial;
            for (k = 0; k < 5; ++k) {
                word = invoke(generated, block.size, &actual.stack[BASE],
                              actual.locals, 8);
                CHECK(!result_is_fault(word));
                CHECK(result_exit(word) == budget_exit);
                CHECK(result_completed(word) == 8u);
                CHECK(result_completed(word) <= 8u);
                CHECK(actual.locals[0].i == (nj_s4)(k + 1u));
                CHECK(actual.locals[2].i == (nj_s4)((k + 1u) * (k + 2u) / 2u));
            }
        }
#else
        (void)budget_exit;
#endif
    }

    /* A guard inside the loop faults on the ninth iteration: the fault word
     * carries the completed count, and the exit metadata rewinds pc/sp to the
     * guarded instruction with the operand stack still holding its inputs. */
    {
        static const nj_u1 code[] = {
            0x1c, 0x2d, 0x1a, 0x2e, 0x60, 0x3d, /* sum += arr[i] */
            0x84, 0x00, 0x01,                   /* iinc 0, 1 */
            0x1a, 0x1b,                         /* iload_0; iload_1 */
            0xa1, 0xff, 0xf5,                   /* if_icmplt 0 */
            0xac
        };
        array_fixture ax;
        array_fixture_init(&ax, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT);
        init_frame(&initial);
        initial.locals[0].i = 0;
        initial.locals[1].i = 10;
        initial.locals[2].i = 0;
        initial.locals[3].r = (nj_ref)&ax.arr;
        jit_vm = &ax.vm;
        word = loop_run("guarded array load faults in loop", code,
                        sizeof(code), 4, &initial, NULL, ax.ints, ARRAY_LEN,
                        NULL, 0, SIZE_MAX, 100000, &block, &expected, &count);
        jit_vm = NULL;
        CHECK(block.loop == 1);
        CHECK(block.nfaults == 1u);
        CHECK(block.faults[0].pc_off == 3u && block.faults[0].sp_delta == 3);
        CHECK(count == 83u);
        CHECK(expected.locals[0].i == 8);
        CHECK(expected.locals[2].i == (nj_s4)0x9d1d9e1cu);
        CHECK(expected.stack[BASE].i == (nj_s4)0x9d1d9e1cu);
        CHECK(expected.stack[BASE + 1].r == (nj_ref)&ax.arr);
        CHECK(expected.stack[BASE + 2].i == 8);
#if EXECUTE_RV32
        CHECK(result_is_fault(word));
        CHECK(result_fault_index(word) == 0u);
        CHECK(result_completed(word) == 83u);
#endif
    }

    /* Arrays mutated in a loop: every iteration executes two guarded
     * instructions; the whole frame and the array bytes must match the
     * interpreter oracle. */
#if NJ_JIT_MAX_FAULTS >= 2
    {
        static const nj_u1 code[] = {
            0x2d, 0x1a, 0x2e, 0x1a, 0x60, 0x36, 0x04, /* t = arr[i] + i */
            0x2d, 0x1a, 0x15, 0x04, 0x4f,             /* arr[i] = t */
            0x84, 0x00, 0x01,
            0x1a, 0x1b,
            0xa1, 0xff, 0xef,
            0xac
        };
        array_fixture ax;
        unsigned j;
        array_fixture_init(&ax, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT);
        init_frame(&initial);
        initial.locals[0].i = 0;
        initial.locals[1].i = ARRAY_LEN;
        initial.locals[3].r = (nj_ref)&ax.arr;
        jit_vm = &ax.vm;
        word = loop_run("array mutation loop", code, sizeof(code), 5, &initial,
                        NULL, ax.ints, ARRAY_LEN, NULL, 0, SIZE_MAX, 100000,
                        &block, &expected, &count);
        jit_vm = NULL;
        CHECK(block.loop == 1 && block.nfaults == 2u);
        CHECK(count == 112u);
        CHECK(expected.locals[0].i == (nj_s4)ARRAY_LEN);
#if EXECUTE_RV32
        CHECK(!result_is_fault(word));
        CHECK(result_completed(word) == 112u);
#endif
#if EXECUTE_RV32
        for (j = 0; j < ARRAY_LEN; ++j)
            CHECK(ax.ints[j] == (nj_s4)(0x10203040u + j * 0x01010101u +
                                        (nj_s4)j));
#else
        (void)j;
#endif
    }

    /* Field mutation loop: box.value += i, two guarded instructions. */
    {
        static const nj_u1 code[] = {
            0x2c, 0x2c, 0xb4, 0x00, 0x01,       /* aload_2; aload_2; getfield 1 */
            0x1a, 0x60,                         /* iload_0; iadd */
            0xb5, 0x00, 0x01,                   /* putfield 1 */
            0x84, 0x00, 0x01,
            0x1a, 0x1b,
            0xa1, 0xff, 0xf1,                   /* if_icmplt 0 */
            0xac
        };
        field_fixture fx;
        nj_u4 value = 5u;
        unsigned offset = field_offset();
        field_fixture_init(&fx, NJ_PT_INT, 1, offset);
        memcpy(fx.object + offset, &value, 4);
        init_frame(&initial);
        initial.locals[0].i = 0;
        initial.locals[1].i = ARRAY_LEN;
        initial.locals[2].r = (nj_ref)(void *)fx.object;
        jit_vm = &fx.vm;
        word = loop_run("field mutation loop", code, sizeof(code), 3, &initial,
                        &fx.owner, NULL, 0, fx.object, offset, SIZE_MAX,
                        100000, &block, &expected, &count);
        jit_vm = NULL;
        CHECK(block.loop == 1 && block.nfaults == 2u);
        CHECK(count == 80u);
        CHECK(expected.locals[0].i == (nj_s4)ARRAY_LEN);
#if EXECUTE_RV32
        CHECK(!result_is_fault(word));
        CHECK(result_completed(word) == 80u);
#endif
#if EXECUTE_RV32
        memcpy(&value, fx.object + offset, 4);
        CHECK(value == 5u + (nj_u4)(ARRAY_LEN * (ARRAY_LEN - 1u) / 2u));
#else
        (void)value;
#endif
    }
#endif
    (void)word;
#endif
}

int main(int argc, char **argv)
{
    int require_execution = argc == 2 && strcmp(argv[1], "--require-execution") == 0;
    if (argc > 2 || (argc == 2 && !require_execution)) {
        fprintf(stderr, "usage: %s [--require-execution]\n", argv[0]); return 2;
    }
    if (require_execution && !EXECUTE_RV32) {
        fprintf(stderr, "FAIL: real RV32 execution requested on a non-RV32 Linux host\n");
        return 2;
    }
    CHECK(sizeof(nj_value) == 8);
    test_integer();
    test_float();
    test_boundaries();
    test_faults();
#if EXECUTE_RV32 && NJ_ENABLE_ENGINE
    test_runtime_fault_abi();
#endif
    test_fields_arrays();
    test_static_fields();
    test_typed_arrays();
    test_array_chain();
    test_new_metadata();
    test_rounding();
    test_branches();
    test_loops();
    test_numeric_constants(); test_analysis(); test_write_through(); test_constant_chain();
    test_name = "extension coverage";
    CHECK(!NJ_JIT_RV32_M || seen_m);
    CHECK(!NJ_JIT_RV32_F || seen_f);
    CHECK(!NJ_JIT_RV32_D || !NJ_ENABLE_LONG || seen_d);
    CHECK(!NJ_JIT_RV32_C || (seen_c && seen_cli && seen_caddi));
#if EXECUTE_RV32
    CHECK(executed > 0);
    CHECK(munmap(exec_page, exec_size) == 0);
#endif
    printf("PASS RV32 M=%d A=%d F=%d D=%d C=%d LONG=%d CACHE=%d: %u assertions, "
           "%u compiled, %u native executions (%s)\n",
           NJ_JIT_RV32_M, NJ_JIT_RV32_A, NJ_JIT_RV32_F, NJ_JIT_RV32_D,
           NJ_JIT_RV32_C, NJ_ENABLE_LONG, NJ_JIT_RV32_REG_CACHE, assertions, compiled, executed,
           EXECUTE_RV32 ? "RV32 machine code" : "encoding/metadata only; execution SKIPPED");
    return 0;
}
