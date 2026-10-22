/* Standalone ARM64 emitter tests. Link with nj_jit_arm64.c only.
 * AArch64 Linux executes the actual emitted bytes; other hosts explicitly run
 * encoding/metadata checks only. The JVM oracle is independent of lowering.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "nj_jit_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#if NJ_JIT_HOST_ARM64 && defined(__linux__)
#define EXECUTE_ARM64 1
#else
#define EXECUTE_ARM64 0
#endif
#if !NJ_ENABLE_JIT
#error "test_jit_arm64 requires NJ_ENABLE_JIT=1"
#endif

enum { STACK_SLOTS = 96, LOCAL_SLOTS = 260, BASE = 16 };
typedef struct test_frame {
    nj_value stack[STACK_SLOTS];
    nj_value locals[LOCAL_SLOTS];
} test_frame;
static const char *test_name;
/* Standalone contract tests use NULL by default; field/array cases install a
 * minimal fake VM so the emitter can resolve CP entries without a runtime. */
static nj_vm *jit_vm;
static unsigned assertions, compiled, executed, seen_f, seen_d, seen_fpcr;

static int analyze_method(const nj_method *m, nj_u4 pc, nj_jit_block *out)
{
    return nj_arm64_analyze(jit_vm, m, pc, out);
}
static int compile_method(const nj_method *m, nj_u4 pc, nj_u1 *buf,
                          nj_size capacity, nj_jit_block *out)
{
    return nj_arm64_compile(jit_vm, m, pc, buf, capacity, out);
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
    m.code = code; m.code_len = (nj_u4)length;
    m.max_stack = STACK_SLOTS - BASE; m.max_locals = (nj_u2)locals;
    return m;
}
/* Compare the allocation-free scan against each emitted prefix. Capacity may
 * shorten compilation, but must never change stack metadata for that prefix. */
static void same_metadata(const nj_jit_block *a, const nj_jit_block *b)
{
    CHECK(a->next_pc == b->next_pc && a->insns == b->insns);
    CHECK(a->stack_delta == b->stack_delta && a->min_stack == b->min_stack);
    CHECK(a->max_growth == b->max_growth);
    CHECK(a->nfaults == b->nfaults);
    {
        unsigned i;
        for (i = 0; i < a->nfaults; ++i) {
            CHECK(a->faults[i].pc_off == b->faults[i].pc_off);
            CHECK(a->faults[i].sp_delta == b->faults[i].sp_delta);
        }
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
        same_metadata(out, &prefix);
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

/* Scan the complete output, including FPCR setup/restoration. The test binary
 * itself may use the compiler's full ISA independently of emitter switches. */
static void inspect(const nj_u1 *code, size_t length)
{
    size_t p;
    unsigned saves = 0, restores = 0;
    CHECK(length >= 8 && length % 4 == 0);
    CHECK(word(code) == 0xd503245fu); /* BTI C entry, a hint on older cores. */
    for (p = 0; p < length; p += 4) {
        nj_u4 insn = word(code + p);
        int fp_data = (insn & 0x0e000000u) == 0x0e000000u;
        int fp_memory = (insn & 0x0a000000u) == 0x08000000u &&
                        (insn & 0x04000000u);
        int fpcr_read = (insn & 0xffffffe0u) == 0xd53b4400u;
        int fpcr_write = (insn & 0xffffffe0u) == 0xd51b4400u;
        CHECK(insn != 0 && insn != UINT32_MAX);
        if (fp_data || fp_memory || fpcr_read || fpcr_write) {
            CHECK(NJ_JIT_ARM64_FP && NJ_ENABLE_FLOAT);
            if (fp_data || fp_memory) {
                int is_double = fp_memory ? (insn >> 30) == 3 : ((insn >> 22) & 3u) == 1;
                if (is_double) { CHECK(NJ_ENABLE_LONG); ++seen_d; }
                else ++seen_f;
            }
            if (fpcr_read) { ++saves; ++seen_fpcr; }
            if (fpcr_write) ++restores;
        }
        /* Advanced SIMD and LSE are separate optional instruction classes. */
        if ((insn & 0x1e000000u) == 0x0e000000u) CHECK(NJ_JIT_ARM64_SIMD);
        if ((insn & 0x3b000000u) == 0x08000000u ||
            (insn & 0x3f200c00u) == 0x38200000u) CHECK(NJ_JIT_ARM64_LSE);
        /* Fault guards are forward branches to an in-block exit stub. */
        {
            long target = -1;
            if ((insn & 0xff000010u) == 0x54000000u) { /* b.cond */
                long off = (long)((insn >> 5) & 0x7ffffu);
                off = (off << 45) >> 45;
                target = (long)p + off * 4;
            } else if ((insn & 0x7e000000u) == 0x34000000u) { /* cbz/cbnz */
                long off = (long)((insn >> 5) & 0x7ffffu);
                off = (off << 45) >> 45;
                target = (long)p + off * 4;
            } else if ((insn & 0x7e000000u) == 0x36000000u) { /* tbz/tbnz */
                long off = (long)((insn >> 5) & 0x3fffu);
                off = (off << 50) >> 50;
                target = (long)p + off * 4;
            } else if ((insn & 0x7c000000u) == 0x14000000u) { /* b */
                long off = (long)(insn & 0x3ffffffu);
                off = (off << 38) >> 38;
                target = (long)p + off * 4;
            }
            if (target >= 0) CHECK(target < (long)length && (target & 3) == 0);
        }
        CHECK((insn & 0xfc000000u) != 0x94000000u); /* bl */
        if ((insn & 0xfe000000u) == 0xd6000000u)
            CHECK(insn == 0xd65f03c0u); /* ret x30 (main exit or fault stub) */
    }
    CHECK(word(code + length - 4) == 0xd65f03c0u);
    CHECK(saves <= 1 && restores == 2 * saves); /* clear once, restore once */
}

#if EXECUTE_ARM64
static void *exec_page;
static size_t exec_size;
/* Check every AAPCS64 callee-saved GPR and low 64 bits of v8..v15 around each
 * generated call. Save the harness's own registers before installing sentinels.
 * This out-of-line assembly avoids the compiler assuming preservation itself. */
extern int arm64_check_abi(nj_jit_fn fn, nj_value *, nj_value *, nj_u4 *);
__asm__(
".text\n"
".align 2\n"
".type arm64_check_abi, %function\n"
"arm64_check_abi:\n"
"sub sp, sp, #176\n"
"str x3, [sp, #168]\n"
"stp x19, x20, [sp, #0]\n"
"stp x21, x22, [sp, #16]\n"
"stp x23, x24, [sp, #32]\n"
"stp x25, x26, [sp, #48]\n"
"stp x27, x28, [sp, #64]\n"
"stp x29, x30, [sp, #80]\n"
"stp d8, d9, [sp, #96]\n"
"stp d10, d11, [sp, #112]\n"
"stp d12, d13, [sp, #128]\n"
"stp d14, d15, [sp, #144]\n"
"mov x9, sp\n"
"str x9, [sp, #160]\n"
".irp n,19,20,21,22,23,24,25,26,27,28,29\n"
"mov x\\n, #\\n\n"
".endr\n"
"mov x9, #0x5a5a\n"
".irp n,8,9,10,11,12,13,14,15\n"
"fmov d\\n, x9\n"
".endr\n"
"mov x16, x0\n"
"mov x0, x1\n"
"mov x1, x2\n"
"blr x16\n"
"ldr x3, [sp, #168]\n"
"str w0, [x3]\n"
"mov w0, #0\n"
".irp n,19,20,21,22,23,24,25,26,27,28,29\n"
"cmp x\\n, #\\n\n"
"b.ne 1f\n"
".endr\n"
"mov x9, #0x5a5a\n"
".irp n,8,9,10,11,12,13,14,15\n"
"fmov x10, d\\n\n"
"cmp x10, x9\n"
"b.ne 1f\n"
".endr\n"
"ldr x9, [sp, #160]\n"
"mov x10, sp\n"
"cmp x9, x10\n"
"b.ne 1f\n"
"mov w0, #1\n"
"1:\n"
"ldp d14, d15, [sp, #144]\n"
"ldp d12, d13, [sp, #128]\n"
"ldp d10, d11, [sp, #112]\n"
"ldp d8, d9, [sp, #96]\n"
"ldp x29, x30, [sp, #80]\n"
"ldp x27, x28, [sp, #64]\n"
"ldp x25, x26, [sp, #48]\n"
"ldp x23, x24, [sp, #32]\n"
"ldp x21, x22, [sp, #16]\n"
"ldp x19, x20, [sp, #0]\n"
"add sp, sp, #176\n"
"ret\n"
".size arm64_check_abi, .-arm64_check_abi\n");

static uint64_t read_fpcr(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value) : : "memory");
    return value;
}
static void write_fpcr(uint64_t value)
{
    __asm__ volatile("msr fpcr, %0" : : "r"(value) : "memory");
}
static nj_u4 invoke(const nj_u1 *code, size_t length, nj_value *sp, nj_value *locals)
{
    nj_jit_fn fn;
    nj_u4 result;
    uint64_t old_fpcr = read_fpcr();
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
    CHECK(arm64_check_abi(fn, sp, locals, &result));
    CHECK(read_fpcr() == old_fpcr);
    ++executed;
    return result;
}
#endif

/* Java arithmetic wraps; signed division guards MIN/-1 before using C. */
static int reference(const nj_u1 *code, size_t length, nj_value *stack,
                     nj_value *locals, int sp, const nj_class *owner, unsigned *count)
{
    size_t pc = 0;
    *count = 0;
    while (pc < length) {
        unsigned op = code[pc++], index = 0;
        int wide = op == 0xc4u;
        nj_u4 a, b, result;
        if (wide) op = code[pc++];
        ++*count;
        if ((op >= 0x15u && op <= 0x19u) ||
            (op >= 0x36u && op <= 0x3au) || op == 0x84u) {
            index = code[pc++];
            if (wide) index = index * 256u + code[pc++];
        } else if (op >= 0x1au && op <= 0x2du) {
            index = (op - 0x1au) % 4u; op = 0x15u + (op - 0x1au) / 4u;
        } else if (op >= 0x3bu && op <= 0x4eu) {
            index = (op - 0x3bu) % 4u; op = 0x36u + (op - 0x3bu) / 4u;
        }
        if (op >= 0x02u && op <= 0x08u) stack[sp++].i = (int)op - 3;
        else if (op == 0x09u || op == 0x0au) {
            stack[sp].j = op - 0x09u; stack[sp + 1].bits = 0; sp += 2;
        } else if (op >= 0x0bu && op <= 0x0du) stack[sp++].f = (float)(op - 0x0bu);
        else if (op == 0x0eu || op == 0x0fu) {
            stack[sp].d = (double)(op - 0x0eu); stack[sp + 1].bits = 0; sp += 2;
        } else if (op == 0x10u) stack[sp++].i = (int8_t)code[pc++];
        else if (op == 0x11u) {
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
                stack[sp] = locals[index]; stack[sp + 1].bits = 0; sp += 2;
            } else if (op == 0x19u) stack[sp++].r = locals[index].r;
            else stack[sp++].bits = locals[index].bits;
        } else if (op >= 0x36u && op <= 0x3au) {
            if (op == 0x37u || op == 0x39u) {
                sp -= 2; locals[index] = stack[sp]; locals[index + 1].bits = 0;
            } else if (op == 0x3au) locals[index].r = stack[--sp].r;
            else locals[index].bits = stack[--sp].bits;
        } else if (op == 0x84u) {
            int increment;
            if (wide) {
                increment = (int16_t)((unsigned)code[pc] * 256 + code[pc + 1]); pc += 2;
            } else increment = (int8_t)code[pc++];
            locals[index].bits += (nj_u4)increment;
        } else if (op == 0x57u) --sp;
        else if (op == 0x58u) sp -= 2;
        else if (op == 0x59u) { stack[sp] = stack[sp - 1]; ++sp; }
        else if (op == 0x5cu) {
            stack[sp] = stack[sp - 2]; stack[sp + 1] = stack[sp - 1]; sp += 2;
        } else if (op == 0x5fu) {
            nj_value tmp = stack[sp - 1];
            stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = tmp;
        } else if (op == 0x00u) { /* nop */ }
        else if (op == 0x01u) stack[sp++].r = NULL;
        else if (op == 0x74u) stack[sp - 1].bits = 0u - stack[sp - 1].bits;
        else if (op >= 0x91u && op <= 0x93u) {
            a = stack[sp - 1].bits;
            stack[sp - 1].i = op == 0x91u ? (int8_t)a :
                                 op == 0x92u ? (uint16_t)a : (int16_t)a;
        } else if (op == 0x76u) stack[sp - 1].bits ^= 0x80000000u;
        else if (op == 0x77u) {
            uint64_t bits;
            memcpy(&bits, &stack[sp - 2], 8); bits ^= UINT64_C(0x8000000000000000);
            memcpy(&stack[sp - 2], &bits, 8);
        } else if (op == 0x62u || op == 0x66u || op == 0x6au || op == 0x6eu) {
            volatile float x = stack[sp - 2].f, y = stack[sp - 1].f;
            float z = op == 0x62u ? x + y : op == 0x66u ? x - y : op == 0x6au ? x * y : x / y;
            stack[sp - 2].f = z; --sp;
        } else if (op == 0x63u || op == 0x67u || op == 0x6bu || op == 0x6fu) {
            volatile double x = stack[sp - 4].d, y = stack[sp - 2].d;
            double z = op == 0x63u ? x + y : op == 0x67u ? x - y : op == 0x6bu ? x * y : x / y;
            stack[sp - 4].d = z; stack[sp - 3].bits = 0; sp -= 2;
        } else {
            a = stack[sp - 2].bits; b = stack[sp - 1].bits;
            switch (op) {
            case 0x60: result = a + b; break;
            case 0x64: result = a - b; break;
            case 0x68: result = a * b; break;
            case 0x6c: case 0x70:
                CHECK(b != 0);
                if (a == 0x80000000u && b == UINT32_MAX) result = op == 0x6cu ? a : 0;
                else if (op == 0x6cu) result = (nj_u4)(signed_bits(a) / signed_bits(b));
                else result = (nj_u4)(signed_bits(a) % signed_bits(b));
                break;
            case 0x78: result = a << (b & 31u); break;
            case 0x7a:
                result = a >> (b & 31u);
                if ((a & 0x80000000u) && (b & 31u)) result |= UINT32_MAX << (32u - (b & 31u));
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

#if EXECUTE_ARM64
static void compare_frame(const test_frame *actual, const test_frame *expected)
{
    size_t i;
    if (memcmp(actual, expected, sizeof(*actual)) == 0) { CHECK(1); return; }
    for (i = 0; i < sizeof(*actual); ++i) {
        const unsigned char *a = (const unsigned char *)actual;
        const unsigned char *b = (const unsigned char *)expected;
        if (a[i] != b[i]) {
            fprintf(stderr, "%s: frame byte %lu got %02x expected %02x\n",
                    test_name, (unsigned long)i, a[i], b[i]); break;
        }
    }
    CHECK(!"generated code differs from oracle");
}
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
    CHECK(block.next_pc == length && block.size <= sizeof(generated));
    CHECK(block.min_stack <= depth && BASE + depth + block.max_growth <= STACK_SLOTS);
    inspect(generated, block.size);
    final_sp = reference(code, length, expected.stack, expected.locals, BASE + depth, owner, &count);
    CHECK(block.insns == count && block.stack_delta == final_sp - BASE - depth);
    ++compiled;
#if EXECUTE_ARM64
    {
        test_frame actual = *initial;
        nj_u4 done = invoke(generated, block.size, &actual.stack[BASE + depth],
                            actual.locals);
        CHECK(done == block.insns);
        compare_frame(&actual, &expected);
    }
#endif
}
static void program(const char *name, const nj_u1 *code, size_t length,
                    unsigned local_count, int depth, const test_frame *initial)
{
    program_owned(name, code, length, local_count, depth, initial, NULL);
}

static void declined(const char *name, const nj_u1 *code, size_t length, unsigned locals)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES], prefix[16];
    nj_jit_block block;
    nj_method m = method(code, length, locals);
    test_name = name;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 0);
    CHECK(length + 1 <= sizeof(prefix));
    prefix[0] = 0x03; memcpy(prefix + 1, code, length);
    m.code = prefix; m.code_len = (nj_u4)length + 1;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 1);
    CHECK(block.next_pc == 1 && block.insns == 1 && block.stack_delta == 1);
    CHECK(block.min_stack == 0 && block.max_growth == 1 && block.nfaults == 0);
    inspect(generated, block.size);
}

/* ---- resolved operands, fault ABI ----------------------------------- */

#if EXECUTE_ARM64
static void expect_fault(nj_u4 word, unsigned index, unsigned completed)
{
    CHECK((word & NJ_JIT_FAULT_BIT) != 0u);
    CHECK((word & NJ_JIT_FAULT_INDEX_MASK) == index);
    CHECK(((word >> 8) & NJ_JIT_FAULT_COMPLETED_MASK) == completed);
}
#endif

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
#if EXECUTE_ARM64
    {
        test_frame actual = *initial;
        ret = invoke(generated, out->size, &actual.stack[BASE + depth],
                     actual.locals);
        compare_frame(&actual, expected);
    }
#else
    (void)depth;
#endif
    return ret;
}

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

enum { FIELD_CACHE = 64, FIELD_OBJ_SIZE = 128 };
typedef union field_object {
    nj_u1 bytes[FIELD_OBJ_SIZE];
    nj_u8 align;
} field_object;
typedef struct field_fixture {
    nj_vm vm;
    nj_class owner;
    nj_field field;
    nj_cpentry cache[FIELD_CACHE];
    field_object object;
} field_fixture;

static unsigned field_offset(void)
{
    return (unsigned)((sizeof(nj_object) + 7u) & ~(size_t)7u);
}

static unsigned field_bytes(unsigned ptype)
{
    switch (ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:   return 1u;
    case NJ_PT_CHAR:
    case NJ_PT_SHORT:  return 2u;
    case NJ_PT_LONG:
    case NJ_PT_DOUBLE:
    case NJ_PT_OBJECT: return 8u;
    default:           return 4u;
    }
}

static nj_u4 field_low_value(const nj_u1 *p, unsigned ptype)
{
    switch (ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:
        return (nj_u4)(nj_s4)(nj_s1)p[0];
    case NJ_PT_CHAR:
        return (nj_u4)p[0] | (nj_u4)p[1] << 8;
    case NJ_PT_SHORT:
        return (nj_u4)(nj_s4)(nj_s2)((nj_u2)p[0] | (nj_u2)p[1] << 8);
    default: {
        nj_u4 v;
        memcpy(&v, p, 4u);
        return v;
    }
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
    fx->owner.instance_size = FIELD_OBJ_SIZE;
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
    memset(fx->object.bytes, 0xa5, sizeof(fx->object.bytes));
    {
        nj_object *o = (nj_object *)(void *)fx->object.bytes;
        o->kind = NJ_OBJ_PLAIN;
        o->size = FIELD_OBJ_SIZE;
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
    if (ptype == NJ_PT_OBJECT) {
        nj_ref r = (nj_ref)(uintptr_t)raw;
        memcpy(fx.object.bytes + offset, &r, sizeof(r));
    } else {
        memcpy(fx.object.bytes + offset, &raw, field_bytes(ptype));
    }
    memcpy(before, fx.object.bytes, sizeof(before));
    field_code(code, 0xb4);
    init_frame(&initial);
    initial.stack[BASE].r = (nj_ref)(void *)fx.object.bytes;
    expected = initial;
    if (wide) {
        memcpy(&expected.stack[BASE].bits, &raw, 8u);
        expected.stack[BASE + 1u].bits &= ~(nj_u8)0xffffffffu;
    } else if (ptype == NJ_PT_OBJECT) {
        nj_ref r;
        memcpy(&r, fx.object.bytes + offset, sizeof(r));
        expected.stack[BASE].r = r;
    } else {
        expected.stack[BASE].bits =
            (expected.stack[BASE].bits & (nj_u8)0xffffffff00000000u) |
            (nj_u8)field_low_value(fx.object.bytes + offset, ptype);
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
#if EXECUTE_ARM64
    CHECK(ret == 1);
#endif
    CHECK(memcmp(fx.object.bytes, before, sizeof(before)) == 0);
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
    memcpy(expected_obj, fx.object.bytes, sizeof(expected_obj));
    memcpy(expected_obj + offset, &raw, bytes);
    field_code(code, 0xb5);
    init_frame(&initial);
    initial.stack[BASE].r = (nj_ref)(void *)fx.object.bytes;
    if (wide) {
        memcpy(&initial.stack[BASE + 1].bits, &raw, 8u);
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
#if EXECUTE_ARM64
    CHECK(ret == 1);
    CHECK(memcmp(fx.object.bytes, expected_obj, sizeof(expected_obj)) == 0);
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
    memcpy(fx.object.bytes + offset, "\x11\x22\x33\x44", 4u);
    memcpy(before, fx.object.bytes, sizeof(before));
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
#if EXECUTE_ARM64
    expect_fault(ret, 0u, 0u);
#endif
    CHECK(memcmp(fx.object.bytes, before, sizeof(before)) == 0);
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
    case 0: fx.cache[slot].used = 0u; break;                 /* unresolved */
    case 1: fx.cache[slot].kind = NJ_LINK_FIELD_STATIC; break;
    case 2: fx.field.flags = NJ_ACC_STATIC; break;
    case 3: fx.owner.state = NJ_CLASS_LOADED; break;        /* not linked */
    case 4: fx.field.offset = (nj_u2)(sizeof(nj_object) - 1u); break;
    case 5: fx.owner.instance_size = offset + 3u; break;    /* past end */
    case 6: fx.field.ptype = 0xffu; break;
    case 7: code[2] = 0u; break;                            /* cp index 0 */
    case 8: fx.vm.cp_cache = NULL; fx.vm.cp_cache_size = 0u; break;
    default: code[2] = 0u; break;                           /* truncated */
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
    if (!fault) {
        expected.stack[BASE].bits =
            (expected.stack[BASE].bits & (nj_u8)0xffffffff00000000u) |
            (nj_u8)(nj_u4)fx->ints[index];
    }
    jit_vm = &fx->vm;
    ret = run_expect(name, code, sizeof(code), 1, 2, NULL, &initial, &expected,
                     &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 1 && block.insns == 1);
    CHECK(block.stack_delta == -1 && block.min_stack == 2);
    CHECK(block.max_growth == 0);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_ARM64
    if (fault) expect_fault(ret, 0u, 0u);
    else CHECK(ret == 1);
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
#if EXECUTE_ARM64
    if (fault) expect_fault(ret, 0u, 0u);
    else CHECK(ret == 1);
#endif
#if EXECUTE_ARM64
    CHECK(memcmp(fx->ints, want, sizeof(want)) == 0);
#else
    (void)want;
#endif
    (void)i;
    (void)ret;
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
    if (!fault) {
        expected.stack[BASE].bits =
            (expected.stack[BASE].bits & (nj_u8)0xffffffff00000000u) |
            (nj_u8)fx->arr.length;
    }
    jit_vm = &fx->vm;
    ret = run_expect(name, code, sizeof(code), 1, 1, NULL, &initial, &expected,
                     &block);
    jit_vm = NULL;
    CHECK(block.next_pc == 1 && block.insns == 1);
    CHECK(block.stack_delta == 0 && block.min_stack == 1);
    CHECK(block.max_growth == 0);
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
          block.faults[0].sp_delta == 0);
#if EXECUTE_ARM64
    if (fault) expect_fault(ret, 0u, 0u);
    else CHECK(ret == 1);
#endif
    (void)ret;
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
#if EXECUTE_ARM64
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
        /* 0xac is the terminal bytecode, not a JIT instruction: a complete
         * block ends at the preceding 0x00 nop. */
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
        CHECK(nj_arm64_analyze(&fx.vm, &m, 0, &analysis));
        CHECK(analysis.next_pc == five_next && analysis.insns == five_insns);
    }
    CHECK(analysis.nfaults == block.nfaults);
    for (i = 0; i < block.nfaults; ++i) {
        CHECK(analysis.faults[i].pc_off == block.faults[i].pc_off);
        CHECK(analysis.faults[i].sp_delta == block.faults[i].sp_delta);
    }
    jit_vm = NULL;

#if EXECUTE_ARM64
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

    test_name = "second fault reports its index and completed count";
    {
        nj_object *o;
        test_frame initial, expected;
        nj_u4 ret;
        field_fixture_init(&fx, NJ_PT_INT, 1u, field_offset());
        memcpy(fx.object.bytes + field_offset(), "\x11\x22\x33\x44", 4u);
        o = (nj_object *)(void *)fx.object.bytes;
        init_frame(&initial);
        initial.stack[BASE].r = (nj_ref)o;
        initial.locals[0].r = NULL;
        expected = initial;
        expected.stack[BASE].bits =
            (expected.stack[BASE].bits & (nj_u8)0xffffffff00000000u) |
            0x44332211u;
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
    field_read_case("getfield object", NJ_PT_OBJECT, 1u,
                    UINT64_C(0x123456789abcdef0));

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
    field_write_case("putfield object", NJ_PT_OBJECT, 1u,
                     UINT64_C(0x123456789abcdef0));

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

static nj_u4 random_word(nj_u4 *state)
{
    nj_u4 x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}
static void test_integer(void)
{
    static const nj_u4 values[] = {0,1,2,31,32,33,63,0x7fffffffu,0x80000000u,
                                    0xffffffffu,0xdeadbeefu,0x80008080u};
    static const nj_u1 binary[] = {0x60,0x64,0x78,0x7a,0x7c,0x7e,0x80,0x82,0x68,0x6c,0x70};
    static const nj_u1 unary[] = {0x74,0x91,0x92,0x93};
    nj_u4 seed = 0xa64c0deu;
    test_frame f;
    size_t i, j, k;
    init_frame(&f);
    for (k = 0; k < sizeof(binary); ++k) {
        for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
            for (j = 0; j < sizeof(values)/sizeof(values[0]); ++j) {
                if ((binary[k] == 0x6c || binary[k] == 0x70) && values[j] == 0) continue;
                f.stack[BASE].bits = values[i]; f.stack[BASE + 1].bits = values[j];
                program("integer arithmetic", &binary[k], 1, 0, 2, &f);
            }
        for (i = 0; i < 128; ++i) {
            f.stack[BASE].bits = random_word(&seed); f.stack[BASE + 1].bits = random_word(&seed);
            if ((binary[k] == 0x6c || binary[k] == 0x70) && f.stack[BASE + 1].bits == 0) continue;
            program("deterministic random integer arithmetic", &binary[k], 1, 0, 2, &f);
        }
    }
    for (k = 0; k < sizeof(unary); ++k)
        for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
            f.stack[BASE].bits = values[i]; program("integer unary", &unary[k], 1, 0, 1, &f);
        }
    {
        static const nj_u1 ops[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
            0x10,0x80,0x10,0x7f,0x11,0x80,0,0x11,0x7f,0xff,0x11,0xf8,0,0x11,0x08,0};
        program("constants and immediate boundaries", ops, sizeof(ops), 0, 0, &f);
    }
    {
        static const nj_u1 ops[] = {0x1a,0x1b,0x60,0x3d,0x15,2,0x36,3,
            0x84,0,0x80,0x84,1,0x7f,0xc4,0x84,0,2,0x80,0,0xc4,0x84,0,3,0x7f,0xff};
        f.locals[0].bits = 0x7fffffffu; f.locals[1].bits = 1;
        program("locals and signed wide iinc", ops, sizeof(ops), 4, 0, &f);
    }
    {
        static const nj_u1 ops[] = {0x59,0x5f,0x5c,0x57,0x58};
        program("stack copies preserve complete slots", ops, sizeof(ops), 0, 2, &f);
    }
    {
        static const nj_u1 ops[] = {0x2a,0x19,1,0x4d,0x3a,3,0x01,0x4c,0x2b,
                                    0xc4,0x19,0,255,0xc4,0x3a,1,0};
        f.locals[0].r = (nj_ref)(uintptr_t)UINT64_C(0x0000123456789abc);
        f.locals[1].r = (nj_ref)(uintptr_t)UINT64_C(0x0000fedcba987654);
        f.locals[255].r = (nj_ref)(uintptr_t)UINT64_C(0x000044448888cccc);
        program("LP64 references and null overwrite all eight bytes", ops, sizeof(ops), 257, 0, &f);
    }
    /* Reach beyond LDUR's negative imm9 and exercise synthesized stack addresses. */
    {
        nj_u1 ops[18];
        memset(ops, 0x58, 17); ops[17] = 0x74;
        program("negative stack address beyond imm9", ops, sizeof(ops), 0, 41, &f);
    }
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
        if (!NJ_JIT_ARM64_FP || !NJ_ENABLE_FLOAT) { declined("FP/float disabled", &fops[k], 1, 0); continue; }
        for (i = 0; i < sizeof(floats)/sizeof(floats[0]); ++i)
            for (j = 0; j < sizeof(floats)/sizeof(floats[0]); ++j) {
                f.stack[BASE].bits = floats[i]; f.stack[BASE + 1].bits = floats[j];
                program("float IEEE arithmetic", &fops[k], 1, 0, fops[k] == 0x76 ? 1 : 2, &f);
            }
    }
    for (k = 0; k < sizeof(dops); ++k) {
        if (!NJ_JIT_ARM64_FP || !NJ_ENABLE_FLOAT || !NJ_ENABLE_LONG) {
            declined("FP/double disabled", &dops[k], 1, 0); continue;
        }
        for (i = 0; i < sizeof(doubles)/sizeof(doubles[0]); ++i)
            for (j = 0; j < sizeof(doubles)/sizeof(doubles[0]); ++j) {
                memcpy(&f.stack[BASE], &doubles[i], 8); memcpy(&f.stack[BASE + 2], &doubles[j], 8);
                program("double IEEE arithmetic", &dops[k], 1, 0, dops[k] == 0x77 ? 2 : 4, &f);
            }
    }
#if NJ_ENABLE_FLOAT
    {
        static const nj_u1 ops[] = {0x0b,0x0c,0x0d,0x43,0x38,1,0x45,0x22,0x17,1,0x46,0x43};
        program("float constants and integer bit copies", ops, sizeof(ops), 4, 0, &f);
    }
#endif
#if NJ_ENABLE_LONG
    {
        static const nj_u1 ops[] = {0x09,0x0a,0x37,0,0x3f,0x1e,0x37,2,0xc4,0x16,0,2,0xc4,0x37,0,0};
        program("long constants copies and padding", ops, sizeof(ops), 4, 0, &f);
    }
#if NJ_ENABLE_FLOAT
    {
        static const nj_u1 ops[] = {0x0e,0x0f,0x39,2,0x47,0x26,0x39,2,0xc4,0x18,0,2,0xc4,0x39,0,0};
        program("double constants copies and padding", ops, sizeof(ops), 4, 0, &f);
    }
#endif
#endif
#if !NJ_ENABLE_FLOAT || !NJ_ENABLE_LONG
    {
        static const nj_u1 fcopy[] = {0x0b,0x0c,0x0d,0x22,0x43};
        static const nj_u1 dcopy[] = {0x0e,0x0f,0x26,0x47};
        static const nj_u1 lcopy[] = {0x09,0x0a,0x1e,0x3f};
        if (!NJ_ENABLE_FLOAT) for (i = 0; i < sizeof(fcopy); ++i) declined("float disabled copies", fcopy + i, 1, 4);
        for (i = 0; i < sizeof(dcopy); ++i) declined("double disabled copies", dcopy + i, 1, 4);
        if (!NJ_ENABLE_LONG) for (i = 0; i < sizeof(lcopy); ++i) declined("long disabled copies", lcopy + i, 1, 4);
    }
#endif
}

/* Test every capacity, executing each accepted prefix as well as scanning it.
 * FP prefixes run under poisoned FPCR, including capacities where the next
 * arithmetic lowering or its FPCR setup must be rolled back. */
static void capacity_case(const nj_u1 *code, size_t length, int depth)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES + 32];
    nj_jit_block block;
    nj_method m = method(code, length, 1);
    size_t i, cap, full_size;
    test_frame initial;
    init_frame(&initial);
    initial.stack[BASE].bits = 0x3f800000u; initial.stack[BASE + 1].bits = 0x33800000u;
    test_name = "capacity, canaries, executable prefix and FPCR rollback";
    CHECK(compile_checked(&m, 0, generated, NJ_JIT_MAX_CODE_BYTES, &block));
    full_size = block.size;
    for (cap = 0; cap <= full_size + 4; ++cap) {
        int ok;
        memset(generated, 0xa5, sizeof(generated));
        ok = compile_checked(&m, 0, generated + 16, (nj_size)cap, &block);
        for (i = 0; i < 16; ++i) CHECK(generated[i] == 0xa5);
        for (i = 16 + cap; i < sizeof(generated); ++i) CHECK(generated[i] == 0xa5);
        if (ok) {
            test_frame expected = initial;
            unsigned count;
            int final_sp;
            CHECK(block.size <= cap && block.insns > 0 && block.next_pc <= length);
            inspect(generated + 16, block.size);
            final_sp = reference(code, block.next_pc, expected.stack, expected.locals, BASE + depth, NULL, &count);
            CHECK(block.insns == count && block.stack_delta == final_sp - BASE - depth);
#if EXECUTE_ARM64
            {
                test_frame actual = initial;
                uint64_t old_fpcr = read_fpcr();
                write_fpcr(UINT64_C(0x03400000)); /* RUP, FZ, DN */
                invoke(generated + 16, block.size, &actual.stack[BASE + depth], actual.locals);
                write_fpcr(old_fpcr);
                compare_frame(&actual, &expected);
            }
#endif
        }
    }
}
static void test_boundaries(void)
{
    static const nj_u1 truncated[][6] = {{0x10},{0x11},{0x11,0},{0x15},{0x36},
        {0x84},{0x84,0},{0xc4},{0xc4,0x15},{0xc4,0x15,0},{0xc4,0x36,0},
        {0xc4,0x84},{0xc4,0x84,0},{0xc4,0x84,0,0},{0xc4,0x84,0,0,0}};
    static const unsigned lengths[] = {1,1,2,1,1,1,2,1,2,3,3,2,3,4,5};
    static const nj_u1 ops[] = {0x08,0x07,0x60,0x3b};
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    nj_method m;
    size_t i;
    for (i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i)
        declined("truncated bytecode", truncated[i], lengths[i], 4);
#if defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        nj_u1 *mapping;
        test_name = "truncated operands against inaccessible page";
        CHECK(page > 0);
        mapping = mmap(NULL, (size_t)page * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
        static const nj_u1 bad[][4] = {{0x15,4},{0x36,4},{0x84,4,1},{0x16,3},
            {0x18,3},{0x37,3},{0x39,3},{0xc4,0x15,0xff,0xff},{0xc4,0x00},
            {0xc4,0x16,0xff,0xfe},{0xc4,0x19,0xff,0xff},{0xc4,0x3a,0xff,0xff}};
        static const unsigned lens[] = {2,2,3,2,2,2,2,4,2,4,4,4};
        for (i = 0; i < sizeof(lens)/sizeof(lens[0]); ++i)
            declined("invalid local or wide opcode", bad[i], lens[i], i >= 7 ? 65535 : 4);
    }
    {
        static const nj_u1 stops[] = {0xac,0xb1,0xb8,0xbb,0xa7,0x12,0x61,0xff};
        for (i = 0; i < sizeof(stops); ++i) declined("unsupported instruction", &stops[i], 1, 4);
    }
    capacity_case(ops, sizeof(ops), 0);
#if NJ_JIT_ARM64_FP && NJ_ENABLE_FLOAT
    {
        static const nj_u1 fp[] = {0x00,0x62,0x0c,0x6a,0x43};
        capacity_case(fp, sizeof(fp), 2);
    }
#endif
    test_name = "pc and argument validation";
    m = method(ops, sizeof(ops), 1);
    CHECK(!compile_checked(&m, sizeof(ops), generated, sizeof(generated), &block));
    CHECK(!compile_checked(&m, UINT32_MAX, generated, sizeof(generated), &block));
    CHECK(!compile_checked(NULL, 0, generated, sizeof(generated), &block));
    CHECK(!compile_checked(&m, 0, NULL, sizeof(generated), &block));
    CHECK(!compile_checked(&m, 0, generated, sizeof(generated), NULL));
    m.code = NULL; CHECK(!compile_checked(&m, 0, generated, sizeof(generated), &block));
    {
        static const nj_u1 code[] = {0x60,0x59,0x04,0x05,0x60,0x60,0x3b};
        m = method(code, sizeof(code), 1);
        test_name = "precise stack metadata";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == sizeof(code));
        CHECK(block.min_stack == 2 && block.max_growth == 2 && block.stack_delta == -1);
        CHECK(block.nfaults == 0);
        test_name = "method max_stack bounds";
        m.max_stack = 1; CHECK(!compile_checked(&m, 0, generated, sizeof(generated), &block));
        m.max_stack = 3; CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 3 && block.insns == 3);
        CHECK(block.min_stack == 2 && block.max_growth == 1 && block.stack_delta == 1);
        m = method(ops, sizeof(ops), 1); m.max_stack = 0;
        CHECK(!compile_checked(&m, 0, generated, sizeof(generated), &block));
    }
    {
        nj_u1 code[NJ_JIT_MAX_BLOCK_INSNS + 7];
        memset(code, 0, sizeof(code)); m = method(code, sizeof(code), 0);
        test_name = "instruction budget";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.insns == NJ_JIT_MAX_BLOCK_INSNS && block.next_pc == NJ_JIT_MAX_BLOCK_INSNS);
    }
    {
        static const nj_u1 code[] = {0xac,0x05,0xac};
        m = method(code, sizeof(code), 0); test_name = "nonzero starting pc and unsupported tail";
        CHECK(compile_checked(&m, 1, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 2 && block.insns == 1);
        CHECK(block.stack_delta == 1 && block.min_stack == 0 && block.max_growth == 1);
    }
    for (i = 0; i < 2; ++i) {
        nj_u1 first[] = {i ? 0x70 : 0x6c,0x04}, middle[] = {0x04,i ? 0x70 : 0x6c};
        test_name = "division isolation and zero guard contract";
        m = method(first, sizeof(first), 0);
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 1 && block.insns == 1 && block.nfaults == 1);
        CHECK(block.faults[0].pc_off == 0 && block.faults[0].sp_delta == 0);
        CHECK(block.min_stack == 2 && block.stack_delta == -1 && block.max_growth == 0);
        m = method(middle, sizeof(middle), 0);
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 1 && block.insns == 1 && block.nfaults == 0);
        /* The VM owns the exception: never execute an unguarded zero divisor. */
    }
}

static void test_large_locals(void)
{
    static const unsigned indices[] = {31,32,255,256,2047,2048,4095,4096,65533,65534};
    size_t k;
    for (k = 0; k < sizeof(indices)/sizeof(indices[0]); ++k) {
        unsigned index = indices[k], hi = index >> 8, lo = index & 255u;
        nj_u1 code[] = {0xc4,0x19,(nj_u1)hi,(nj_u1)lo,0x4b,
                       0xc4,0x84,(nj_u1)hi,(nj_u1)lo,0x80,0,
                       0xc4,0x15,(nj_u1)hi,(nj_u1)lo,0x3c};
        nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
        nj_method m = method(code, sizeof(code), 65535);
        nj_jit_block block;
        test_name = "wide locals across address encoding limits";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == sizeof(code) && block.insns == 5 && block.stack_delta == 0);
        inspect(generated, block.size); ++compiled;
#if EXECUTE_ARM64
        {
            nj_value *locals = malloc(65535 * sizeof(nj_value));
            nj_value *expected = malloc(65535 * sizeof(nj_value));
            nj_value stack[4], stack_expected[4];
            CHECK(locals && expected);
            memset(locals, 0xa5, 65535 * sizeof(nj_value));
            locals[index].r = (nj_ref)(uintptr_t)UINT64_C(0x0000fedcba987654);
            memcpy(expected, locals, 65535 * sizeof(nj_value));
            memset(stack, 0x5a, sizeof(stack)); memcpy(stack_expected, stack, sizeof(stack));
            expected[0].r = expected[index].r;
            expected[index].bits += (nj_u4)-32768;
            expected[1].bits = expected[index].bits;
            stack_expected[1].r = locals[index].r;
            stack_expected[1].bits = expected[index].bits;
            invoke(generated, block.size, stack + 1, locals);
            CHECK(memcmp(locals, expected, 65535 * sizeof(nj_value)) == 0);
            CHECK(memcmp(stack, stack_expected, sizeof(stack)) == 0);
            free(expected); free(locals);
        }
#endif
    }
#if NJ_ENABLE_LONG
    {
        static const nj_u1 code[] = {0xc4,0x16,0xff,0xfd,0xc4,0x37,0xff,0xfd};
        nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
        nj_method m = method(code, sizeof(code), 65535);
        nj_jit_block block;
        test_name = "largest valid two-slot local with padding";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == sizeof(code) && block.insns == 2 && block.stack_delta == 0);
        inspect(generated, block.size); ++compiled;
#if EXECUTE_ARM64
        {
            nj_value *locals = malloc(65535 * sizeof(nj_value));
            nj_value *expected = malloc(65535 * sizeof(nj_value));
            nj_value stack[4], stack_expected[4];
            CHECK(locals && expected);
            memset(locals, 0xa5, 65535 * sizeof(nj_value));
            locals[65533].j = INT64_C(0x123456789abcdef);
            memcpy(expected, locals, 65535 * sizeof(nj_value)); expected[65534].bits = 0;
            memset(stack, 0x5a, sizeof(stack)); memcpy(stack_expected, stack, sizeof(stack));
            stack_expected[1] = locals[65533]; stack_expected[2].bits = 0;
            invoke(generated, block.size, stack + 1, locals);
            CHECK(memcmp(locals, expected, 65535 * sizeof(nj_value)) == 0);
            CHECK(memcmp(stack, stack_expected, sizeof(stack)) == 0);
            free(expected); free(locals);
        }
#endif
    }
#endif
}

static void test_fpcr(void)
{
#if EXECUTE_ARM64 && NJ_JIT_ARM64_FP && NJ_ENABLE_FLOAT
    static const struct { nj_u1 op; nj_u4 a, b, result; } cases[] = {
        {0x62,0x3f800000u,0x33800000u,0x3f800000u}, /* halfway rounds to even */
        {0x62,1,1,2},                              /* retain subnormal inputs */
        {0x6a,0x00800000u,0x3f000000u,0x00400000u},/* retain subnormal result */
        {0x62,0x7fc12345u,0,0x7fc12345u},          /* preserve NaN payload */
        {0x76,0x7fc12345u,0,0xffc12345u}
    };
    size_t i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        nj_u1 code = cases[i].op, generated[NJ_JIT_MAX_CODE_BYTES];
        nj_method m = method(&code, 1, 0);
        nj_jit_block block;
        test_frame f;
        uint64_t old_fpcr = read_fpcr();
        init_frame(&f); f.stack[BASE].bits = cases[i].a; f.stack[BASE + 1].bits = cases[i].b;
        test_name = "fixed float RNE non-FTZ and FPCR restoration";
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
        write_fpcr(UINT64_C(0x03400000));
        CHECK(read_fpcr() == UINT64_C(0x03400000));
        invoke(generated, block.size, &f.stack[BASE + (code == 0x76 ? 1 : 2)], f.locals);
        write_fpcr(old_fpcr);
        CHECK(f.stack[BASE].bits == cases[i].result);
    }
#if NJ_ENABLE_LONG
    {
        static const struct { nj_u1 op; uint64_t a, b, result; } dcases[] = {
            {0x63,UINT64_C(0x3ff0000000000000),UINT64_C(0x3ca0000000000000),UINT64_C(0x3ff0000000000000)},
            {0x63,1,1,2},
            {0x6b,UINT64_C(0x0010000000000000),UINT64_C(0x3fe0000000000000),UINT64_C(0x0008000000000000)},
            {0x63,UINT64_C(0x7ff8123456789abc),0,UINT64_C(0x7ff8123456789abc)},
            {0x77,UINT64_C(0x7ff8123456789abc),0,UINT64_C(0xfff8123456789abc)}
        };
        for (i = 0; i < sizeof(dcases)/sizeof(dcases[0]); ++i) {
            nj_u1 code = dcases[i].op, generated[NJ_JIT_MAX_CODE_BYTES];
            nj_method m = method(&code, 1, 0);
            nj_jit_block block;
            test_frame f;
            uint64_t got, old_fpcr = read_fpcr();
            init_frame(&f); memcpy(&f.stack[BASE], &dcases[i].a, 8); memcpy(&f.stack[BASE + 2], &dcases[i].b, 8);
            test_name = "fixed double RNE non-FTZ and FPCR restoration";
            CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block));
            write_fpcr(UINT64_C(0x03400000));
            invoke(generated, block.size, &f.stack[BASE + (code == 0x77 ? 2 : 4)], f.locals);
            write_fpcr(old_fpcr);
            memcpy(&got, &f.stack[BASE], 8); CHECK(got == dcases[i].result);
        }
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
#if EXECUTE_ARM64
            {
                test_frame actual = *initial;
                invoke(generated + 16, block.size, &actual.stack[BASE + depth], actual.locals);
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
    CHECK(!analyze_method(NULL, 0, &block));
    CHECK(!analyze_method(&m, 0, NULL));
    CHECK(!analyze_method(&m, sizeof(code), &block));
    CHECK(!analyze_method(&m, UINT32_MAX, &block));
    CHECK(analyze_method(&m, 0, &block));
    CHECK(block.size == 0 && block.insns == 1 && block.next_pc == 1);
    CHECK(block.min_stack == 0 && block.max_growth == 1 && block.stack_delta == 1);
    CHECK(block.nfaults == 0);
    m.max_stack = 0; CHECK(!analyze_method(&m, 0, &block));
    m.code = NULL; CHECK(!analyze_method(&m, 0, &block));
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
        static const nj_u1 operators[] = {0x60,0x64,0x78,0x7a,0x7c,0x7e,0x80,0x82,0x68};
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
#if EXECUTE_ARM64
        {
            test_frame actual = f;
            invoke(generated, block.size, actual.stack + BASE, actual.stack + local_offsets[i]);
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
#if NJ_JIT_ARM64_FP && NJ_ENABLE_FLOAT
    {
        static const nj_u1 fp[] = {0x0c,0x0d,0x62,0x59,0x3b,0x1a,0x60};
        program("FP store invalidates cached integer bits", fp, sizeof(fp), 4, 0, &f);
        capacity_owned(fp, sizeof(fp), 0, NULL, &f);
    }
#endif
#if NJ_JIT_ARM64_FP && NJ_ENABLE_LONG && NJ_ENABLE_FLOAT
    {
        static const nj_u1 fp[] = {0x0f,0x0f,0x63,0x5c,0x47,0x26,0x63,0x58,0x1b};
        program("double store invalidates both halves and padding", fp, sizeof(fp), 4, 0, &f);
        capacity_owned(fp, sizeof(fp), 0, NULL, &f);
    }
#endif
}

static void test_constant_chain(void)
{
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
    for (p = 0; p < block.size; p += 4) {
        nj_u4 instruction = word(generated + p);
        if ((instruction & 0x0a000000u) == 0x08000000u) {
            if (instruction & 0x00400000u) ++loads; else ++stores;
        }
    }
    printf("SAMPLE ARM64 insns=%u bytes=%lu loads=%u stores=%u\n",
           block.insns, (unsigned long)block.size, loads, stores);
}

int main(int argc, char **argv)
{
    int require_execution = argc == 2 && strcmp(argv[1], "--require-execution") == 0;
    if (argc > 2 || (argc == 2 && !require_execution)) {
        fprintf(stderr, "usage: %s [--require-execution]\n", argv[0]); return 2;
    }
    if (require_execution && !EXECUTE_ARM64) {
        fprintf(stderr, "FAIL: real ARM64 execution requested on a non-AArch64 LP64 Linux host\n"); return 2;
    }
    CHECK(sizeof(nj_value) == 8);
#if EXECUTE_ARM64
    CHECK(sizeof(nj_ref) == 8);
#endif
    test_integer(); test_float(); test_boundaries();
    test_faults(); test_fields_arrays();
    test_large_locals(); test_fpcr();
    test_numeric_constants(); test_analysis(); test_write_through(); test_constant_chain();
    test_name = "extension coverage";
    CHECK(!NJ_JIT_ARM64_FP || !NJ_ENABLE_FLOAT || (seen_f && seen_fpcr));
    CHECK(!NJ_JIT_ARM64_FP || !NJ_ENABLE_FLOAT || !NJ_ENABLE_LONG || seen_d);
#if EXECUTE_ARM64
    CHECK(executed > 0); CHECK(munmap(exec_page, exec_size) == 0);
#endif
    printf("PASS ARM64 FP=%d SIMD=%d LSE=%d LONG=%d FLOAT=%d: %u assertions, "
           "%u compiled, %u native executions (%s)\n",
           NJ_JIT_ARM64_FP, NJ_JIT_ARM64_SIMD, NJ_JIT_ARM64_LSE, NJ_ENABLE_LONG,
           NJ_ENABLE_FLOAT, assertions, compiled, executed,
           EXECUTE_ARM64 ? "ARM64 machine code" : "encoding/metadata only; execution SKIPPED");
    return 0;
}
