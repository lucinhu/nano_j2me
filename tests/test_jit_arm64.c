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
static unsigned assertions, compiled, executed, seen_f, seen_d, seen_fpcr;
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
        CHECK((insn & 0x7c000000u) != 0x14000000u); /* b/bl */
        CHECK((insn & 0xff000010u) != 0x54000000u); /* b.cond */
        CHECK((insn & 0x7e000000u) != 0x34000000u); /* cbz/cbnz */
        CHECK((insn & 0x7e000000u) != 0x36000000u); /* tbz/tbnz */
        if ((insn & 0xfe000000u) == 0xd6000000u)
            CHECK(insn == 0xd65f03c0u && p + 4 == length); /* ret x30 */
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
extern int arm64_check_abi(void (*fn)(nj_value *, nj_value *), nj_value *, nj_value *);
__asm__(
".text\n"
".align 2\n"
".type arm64_check_abi, %function\n"
"arm64_check_abi:\n"
"sub sp, sp, #176\n"
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
static void invoke(const nj_u1 *code, size_t length, nj_value *sp, nj_value *locals)
{
    void (*fn)(nj_value *, nj_value *);
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
    CHECK(arm64_check_abi(fn, sp, locals));
    CHECK(read_fpcr() == old_fpcr);
    ++executed;
}
#endif

/* Java arithmetic wraps; signed division guards MIN/-1 before using C. */
static int reference(const nj_u1 *code, size_t length, test_frame *f, int sp,
                     unsigned *count)
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
        if (op >= 0x02u && op <= 0x08u) f->stack[sp++].i = (int)op - 3;
        else if (op == 0x09u || op == 0x0au) {
            f->stack[sp].j = op - 0x09u; f->stack[sp + 1].bits = 0; sp += 2;
        } else if (op >= 0x0bu && op <= 0x0du) f->stack[sp++].f = (float)(op - 0x0bu);
        else if (op == 0x0eu || op == 0x0fu) {
            f->stack[sp].d = (double)(op - 0x0eu); f->stack[sp + 1].bits = 0; sp += 2;
        } else if (op == 0x10u) f->stack[sp++].i = (int8_t)code[pc++];
        else if (op == 0x11u) {
            int v = (int)code[pc] * 256 + code[pc + 1]; pc += 2;
            f->stack[sp++].i = (int16_t)v;
        } else if (op >= 0x15u && op <= 0x19u) {
            if (op == 0x16u || op == 0x18u) {
                f->stack[sp] = f->locals[index]; f->stack[sp + 1].bits = 0; sp += 2;
            } else if (op == 0x19u) f->stack[sp++].r = f->locals[index].r;
            else f->stack[sp++].bits = f->locals[index].bits;
        } else if (op >= 0x36u && op <= 0x3au) {
            if (op == 0x37u || op == 0x39u) {
                sp -= 2; f->locals[index] = f->stack[sp]; f->locals[index + 1].bits = 0;
            } else if (op == 0x3au) f->locals[index].r = f->stack[--sp].r;
            else f->locals[index].bits = f->stack[--sp].bits;
        } else if (op == 0x84u) {
            int increment;
            if (wide) {
                increment = (int16_t)((unsigned)code[pc] * 256 + code[pc + 1]); pc += 2;
            } else increment = (int8_t)code[pc++];
            f->locals[index].bits += (nj_u4)increment;
        } else if (op == 0x57u) --sp;
        else if (op == 0x58u) sp -= 2;
        else if (op == 0x59u) { f->stack[sp] = f->stack[sp - 1]; ++sp; }
        else if (op == 0x5cu) {
            f->stack[sp] = f->stack[sp - 2]; f->stack[sp + 1] = f->stack[sp - 1]; sp += 2;
        } else if (op == 0x5fu) {
            nj_value tmp = f->stack[sp - 1];
            f->stack[sp - 1] = f->stack[sp - 2]; f->stack[sp - 2] = tmp;
        } else if (op == 0x00u) { /* nop */ }
        else if (op == 0x01u) f->stack[sp++].r = NULL;
        else if (op == 0x74u) f->stack[sp - 1].bits = 0u - f->stack[sp - 1].bits;
        else if (op >= 0x91u && op <= 0x93u) {
            a = f->stack[sp - 1].bits;
            f->stack[sp - 1].i = op == 0x91u ? (int8_t)a :
                                 op == 0x92u ? (uint16_t)a : (int16_t)a;
        } else if (op == 0x76u) f->stack[sp - 1].bits ^= 0x80000000u;
        else if (op == 0x77u) {
            uint64_t bits;
            memcpy(&bits, &f->stack[sp - 2], 8); bits ^= UINT64_C(0x8000000000000000);
            memcpy(&f->stack[sp - 2], &bits, 8);
        } else if (op == 0x62u || op == 0x66u || op == 0x6au || op == 0x6eu) {
            volatile float x = f->stack[sp - 2].f, y = f->stack[sp - 1].f;
            float z = op == 0x62u ? x + y : op == 0x66u ? x - y : op == 0x6au ? x * y : x / y;
            f->stack[sp - 2].f = z; --sp;
        } else if (op == 0x63u || op == 0x67u || op == 0x6bu || op == 0x6fu) {
            volatile double x = f->stack[sp - 4].d, y = f->stack[sp - 2].d;
            double z = op == 0x63u ? x + y : op == 0x67u ? x - y : op == 0x6bu ? x * y : x / y;
            f->stack[sp - 4].d = z; f->stack[sp - 3].bits = 0; sp -= 2;
        } else {
            a = f->stack[sp - 2].bits; b = f->stack[sp - 1].bits;
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
            f->stack[sp - 2].bits = result; --sp;
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
static void program(const char *name, const nj_u1 *code, size_t length,
                    unsigned local_count, int depth, const test_frame *initial)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    nj_method m = method(code, length, local_count);
    test_frame expected = *initial;
    unsigned count;
    int final_sp;
    test_name = name;
    CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block) == 1);
    CHECK(block.next_pc == length && block.size <= sizeof(generated));
    CHECK(block.min_stack <= depth && BASE + depth + block.max_growth <= STACK_SLOTS);
    inspect(generated, block.size);
    final_sp = reference(code, length, &expected, BASE + depth, &count);
    CHECK(block.insns == count && block.stack_delta == final_sp - BASE - depth);
    ++compiled;
#if EXECUTE_ARM64
    {
        test_frame actual = *initial;
        if (block.check_divisor) CHECK(actual.stack[BASE + depth - 1].i != 0);
        invoke(generated, block.size, &actual.stack[BASE + depth], actual.locals);
        compare_frame(&actual, &expected);
    }
#endif
}
static void declined(const char *name, const nj_u1 *code, size_t length, unsigned locals)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES], prefix[16];
    nj_jit_block block;
    nj_method m = method(code, length, locals);
    test_name = name;
    CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block) == 0);
    CHECK(length + 1 <= sizeof(prefix));
    prefix[0] = 0x03; memcpy(prefix + 1, code, length);
    m.code = prefix; m.code_len = (nj_u4)length + 1;
    CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block) == 1);
    CHECK(block.next_pc == 1 && block.insns == 1 && block.stack_delta == 1);
    CHECK(block.min_stack == 0 && block.max_growth == 1 && !block.check_divisor);
    inspect(generated, block.size);
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
    CHECK(nj_arm64_compile(&m, 0, generated, NJ_JIT_MAX_CODE_BYTES, &block));
    full_size = block.size;
    for (cap = 0; cap <= full_size + 4; ++cap) {
        int ok;
        memset(generated, 0xa5, sizeof(generated));
        ok = nj_arm64_compile(&m, 0, generated + 16, (nj_size)cap, &block);
        for (i = 0; i < 16; ++i) CHECK(generated[i] == 0xa5);
        for (i = 16 + cap; i < sizeof(generated); ++i) CHECK(generated[i] == 0xa5);
        if (ok) {
            test_frame expected = initial;
            unsigned count;
            int final_sp;
            CHECK(block.size <= cap && block.insns > 0 && block.next_pc <= length);
            inspect(generated + 16, block.size);
            final_sp = reference(code, block.next_pc, &expected, BASE + depth, &count);
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
    CHECK(!nj_arm64_compile(&m, sizeof(ops), generated, sizeof(generated), &block));
    CHECK(!nj_arm64_compile(&m, UINT32_MAX, generated, sizeof(generated), &block));
    CHECK(!nj_arm64_compile(NULL, 0, generated, sizeof(generated), &block));
    CHECK(!nj_arm64_compile(&m, 0, NULL, sizeof(generated), &block));
    CHECK(!nj_arm64_compile(&m, 0, generated, sizeof(generated), NULL));
    m.code = NULL; CHECK(!nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
    {
        static const nj_u1 code[] = {0x60,0x59,0x04,0x05,0x60,0x60,0x3b};
        m = method(code, sizeof(code), 1);
        test_name = "precise stack metadata";
        CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == sizeof(code));
        CHECK(block.min_stack == 2 && block.max_growth == 2 && block.stack_delta == -1);
        CHECK(!block.check_divisor);
        test_name = "method max_stack bounds";
        m.max_stack = 1; CHECK(!nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
        m.max_stack = 3; CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 3 && block.insns == 3);
        CHECK(block.min_stack == 2 && block.max_growth == 1 && block.stack_delta == 1);
        m = method(ops, sizeof(ops), 1); m.max_stack = 0;
        CHECK(!nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
    }
    {
        nj_u1 code[NJ_JIT_MAX_BLOCK_INSNS + 7];
        memset(code, 0, sizeof(code)); m = method(code, sizeof(code), 0);
        test_name = "instruction budget";
        CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.insns == NJ_JIT_MAX_BLOCK_INSNS && block.next_pc == NJ_JIT_MAX_BLOCK_INSNS);
    }
    {
        static const nj_u1 code[] = {0xac,0x05,0xac};
        m = method(code, sizeof(code), 0); test_name = "nonzero starting pc and unsupported tail";
        CHECK(nj_arm64_compile(&m, 1, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 2 && block.insns == 1);
        CHECK(block.stack_delta == 1 && block.min_stack == 0 && block.max_growth == 1);
    }
    for (i = 0; i < 2; ++i) {
        nj_u1 first[] = {i ? 0x70 : 0x6c,0x04}, middle[] = {0x04,i ? 0x70 : 0x6c};
        test_name = "division isolation and zero guard contract";
        m = method(first, sizeof(first), 0);
        CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 1 && block.insns == 1 && block.check_divisor == 1);
        CHECK(block.min_stack == 2 && block.stack_delta == -1 && block.max_growth == 0);
        m = method(middle, sizeof(middle), 0);
        CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 1 && block.insns == 1 && block.check_divisor == 0);
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
        CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
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
        CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
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
        CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
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
            CHECK(nj_arm64_compile(&m, 0, generated, sizeof(generated), &block));
            write_fpcr(UINT64_C(0x03400000));
            invoke(generated, block.size, &f.stack[BASE + (code == 0x77 ? 2 : 4)], f.locals);
            write_fpcr(old_fpcr);
            memcpy(&got, &f.stack[BASE], 8); CHECK(got == dcases[i].result);
        }
    }
#endif
#endif
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
    test_integer(); test_float(); test_boundaries(); test_large_locals(); test_fpcr();
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
