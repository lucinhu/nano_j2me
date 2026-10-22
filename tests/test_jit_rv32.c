/* Standalone RV32 emitter checks. Link with nj_jit_rv32.c only.
 * On RV32 Linux this executes the emitted bytes; other hosts explicitly run
 * encoding/metadata checks only. The small oracle is independent of lowering.
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
        CHECK(op != 0x63u && op != 0x6fu); /* no branches/calls */
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

static void invoke(const nj_u1 *code, size_t length, nj_value *sp,
                   nj_value *locals)
{
    void (*fn)(nj_value *, nj_value *);
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
    fn(sp, locals);
    ++executed;
}
#endif

/* A deliberately direct JVM oracle. Unsigned arithmetic models Java wrapping;
 * division handles MIN/-1 without invoking undefined C signed division. */
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
            index = (op - 0x1au) % 4u;
            op = 0x15u + (op - 0x1au) / 4u;
        } else if (op >= 0x3bu && op <= 0x4eu) {
            index = (op - 0x3bu) % 4u;
            op = 0x36u + (op - 0x3bu) / 4u;
        }
        if (op >= 0x02u && op <= 0x08u) {
            f->stack[sp++].i = (int)op - 3;
        } else if (op == 0x09u || op == 0x0au) {
            f->stack[sp].j = op - 0x09u;
            f->stack[sp + 1].bits = 0; sp += 2;
        } else if (op >= 0x0bu && op <= 0x0du) {
            f->stack[sp++].f = (float)(op - 0x0bu);
        } else if (op == 0x0eu || op == 0x0fu) {
            f->stack[sp].d = (double)(op - 0x0eu);
            f->stack[sp + 1].bits = 0; sp += 2;
        } else if (op == 0x10u) {
            f->stack[sp++].i = (int8_t)code[pc++];
        } else if (op == 0x11u) {
            int v = (int)code[pc] * 256 + code[pc + 1]; pc += 2;
            f->stack[sp++].i = (int16_t)v;
        } else if (op >= 0x15u && op <= 0x19u) {
            if (op == 0x16u || op == 0x18u) {
                f->stack[sp] = f->locals[index];
                f->stack[sp + 1].bits = 0; sp += 2;
            } else f->stack[sp++].bits = f->locals[index].bits;
        } else if (op >= 0x36u && op <= 0x3au) {
            if (op == 0x37u || op == 0x39u) {
                sp -= 2; f->locals[index] = f->stack[sp];
                f->locals[index + 1].bits = 0;
            } else f->locals[index].bits = f->stack[--sp].bits;
        } else if (op == 0x84u) {
            int increment;
            if (wide) {
                increment = (int16_t)((unsigned)code[pc] * 256 + code[pc + 1]);
                pc += 2;
            } else increment = (int8_t)code[pc++];
            f->locals[index].bits += (nj_u4)increment;
        } else if (op == 0x57u) --sp;
        else if (op == 0x58u) sp -= 2;
        else if (op == 0x59u) { f->stack[sp] = f->stack[sp - 1]; ++sp; }
        else if (op == 0x5cu) {
            f->stack[sp] = f->stack[sp - 2];
            f->stack[sp + 1] = f->stack[sp - 1]; sp += 2;
        } else if (op == 0x5fu) {
            nj_value tmp = f->stack[sp - 1];
            f->stack[sp - 1] = f->stack[sp - 2]; f->stack[sp - 2] = tmp;
        } else if (op == 0x00u) { /* nop */ }
        else if (op == 0x01u) f->stack[sp++].bits = 0;
        else if (op == 0x74u) f->stack[sp - 1].bits = 0u - f->stack[sp - 1].bits;
        else if (op >= 0x91u && op <= 0x93u) {
            a = f->stack[sp - 1].bits;
            f->stack[sp - 1].i = op == 0x91u ? (int8_t)a :
                                 op == 0x92u ? (uint16_t)a : (int16_t)a;
        } else if (op == 0x76u) f->stack[sp - 1].bits ^= 0x80000000u;
        else if (op == 0x77u) {
            uint64_t bits;
            memcpy(&bits, &f->stack[sp - 2], 8);
            bits ^= UINT64_C(0x8000000000000000);
            memcpy(&f->stack[sp - 2], &bits, 8);
        } else if (op == 0x62u || op == 0x66u || op == 0x6au || op == 0x6eu) {
            volatile float x = f->stack[sp - 2].f, y = f->stack[sp - 1].f;
            float z = op == 0x62u ? x + y : op == 0x66u ? x - y :
                      op == 0x6au ? x * y : x / y;
            f->stack[sp - 2].f = z; --sp;
        } else if (op == 0x63u || op == 0x67u || op == 0x6bu || op == 0x6fu) {
            volatile double x = f->stack[sp - 4].d, y = f->stack[sp - 2].d;
            double z = op == 0x63u ? x + y : op == 0x67u ? x - y :
                       op == 0x6bu ? x * y : x / y;
            f->stack[sp - 4].d = z;
            f->stack[sp - 3].bits = 0; sp -= 2;
        } else {
            a = f->stack[sp - 2].bits; b = f->stack[sp - 1].bits;
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
            f->stack[sp - 2].bits = result; --sp;
        }
        CHECK(sp >= 0 && sp < STACK_SLOTS);
    }
    return sp;
}

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
    CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block) == 1);
    CHECK(block.next_pc == length);
    CHECK(block.size <= sizeof(generated));
    CHECK(block.min_stack <= depth);
    CHECK(BASE + depth + block.max_growth <= STACK_SLOTS);
    inspect(generated, block.size);
    final_sp = reference(code, length, &expected, BASE + depth, &count);
    CHECK(block.insns == count);
    CHECK(block.stack_delta == final_sp - BASE - depth);
    ++compiled;
#if EXECUTE_RV32
    {
        test_frame actual = *initial;
        size_t i;
        if (block.check_divisor) CHECK(actual.stack[BASE + depth - 1].i != 0);
        invoke(generated, block.size, &actual.stack[BASE + depth], actual.locals);
        /* Compare all bytes, including untouched high halves and dead slots. */
        if (memcmp(&actual, &expected, sizeof(actual)) != 0) {
            const unsigned char *a = (const unsigned char *)&actual;
            const unsigned char *b = (const unsigned char *)&expected;
            for (i = 0; i < sizeof(actual); ++i) if (a[i] != b[i]) {
                fprintf(stderr, "%s: frame byte %lu got %02x expected %02x\n",
                        name, (unsigned long)i, a[i], b[i]); break;
            }
            CHECK(!"generated code differs from oracle");
        }
    }
#endif
}

static void declined(const char *name, const nj_u1 *code, size_t length,
                     unsigned locals)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    nj_method m = method(code, length, locals);
    nj_u1 prefix[16];
    test_name = name;
    CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block) == 0);
    CHECK(length + 1 <= sizeof(prefix));
    prefix[0] = 0x03; /* An accepted prefix must stop before the declined opcode. */
    memcpy(prefix + 1, code, length);
    m.code = prefix;
    m.code_len = (nj_u4)length + 1;
    CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block) == 1);
    CHECK(block.next_pc == 1 && block.insns == 1 && block.stack_delta == 1);
    CHECK(block.min_stack == 0 && block.max_growth == 1 && !block.check_divisor);
    inspect(generated, block.size);
}

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
    CHECK(nj_rv32_compile(&m, 0, generated, NJ_JIT_MAX_CODE_BYTES, &block));
    full_size = block.size;
    for (cap = 0; cap <= full_size + 4; ++cap) {
        int ok;
        memset(generated, 0xa5, sizeof(generated));
        ok = nj_rv32_compile(&m, 0, generated + 16, cap, &block);
        for (i = 0; i < 16; ++i) CHECK(generated[i] == 0xa5);
        for (i = 16 + cap; i < sizeof(generated); ++i) CHECK(generated[i] == 0xa5);
        if (ok) {
            CHECK(block.size <= cap && block.insns > 0 && block.next_pc <= sizeof(ops));
            inspect(generated + 16, block.size);
        }
    }
    test_name = "pc and argument validation";
    CHECK(!nj_rv32_compile(&m, sizeof(ops), generated, sizeof(generated), &block));
    CHECK(!nj_rv32_compile(&m, UINT32_MAX, generated, sizeof(generated), &block));
    CHECK(!nj_rv32_compile(NULL, 0, generated, sizeof(generated), &block));
    CHECK(!nj_rv32_compile(&m, 0, NULL, sizeof(generated), &block));
    CHECK(!nj_rv32_compile(&m, 0, generated, sizeof(generated), NULL));
    m.code = NULL;
    CHECK(!nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
    {
        static const nj_u1 code[] = {0x60,0x59,0x04,0x05,0x60,0x60,0x3b};
        m = method(code, sizeof(code), 1);
        test_name = "precise stack metadata";
        CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == sizeof(code));
        CHECK(block.min_stack == 2 && block.max_growth == 2 && block.stack_delta == -1);
        CHECK(!block.check_divisor);
        test_name = "method max_stack bounds";
        m.max_stack = 1;
        CHECK(!nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
        m.max_stack = 3;
        CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 3 && block.insns == 3);
        CHECK(block.min_stack == 2 && block.max_growth == 1 && block.stack_delta == 1);
        m = method(ops, sizeof(ops), 1);
        m.max_stack = 0;
        CHECK(!nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
    }
    {
        nj_u1 code[NJ_JIT_MAX_BLOCK_INSNS + 7];
        memset(code, 0, sizeof(code));
        m = method(code, sizeof(code), 0);
        test_name = "instruction budget";
        CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.insns == NJ_JIT_MAX_BLOCK_INSNS);
        CHECK(block.next_pc == NJ_JIT_MAX_BLOCK_INSNS);
    }
    {
        static const nj_u1 code[] = {0xac,0x05,0xac};
        m = method(code, sizeof(code), 0);
        test_name = "nonzero starting pc and unsupported tail";
        CHECK(nj_rv32_compile(&m, 1, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 2 && block.insns == 1);
        CHECK(block.stack_delta == 1 && block.min_stack == 0 && block.max_growth == 1);
    }
#if NJ_JIT_RV32_M
    {
        static const nj_u1 first[] = {0x6c,0x04};
        static const nj_u1 middle[] = {0x04,0x6c};
        test_name = "division isolation and zero guard contract";
        m = method(first, sizeof(first), 0);
        CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 1 && block.insns == 1 && block.check_divisor == 1);
        CHECK(block.min_stack == 2 && block.stack_delta == -1 && block.max_growth == 0);
        m = method(middle, sizeof(middle), 0);
        CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
        CHECK(block.next_pc == 1 && block.insns == 1 && block.check_divisor == 0);
        /* Zero is intentionally not sent to generated code: the VM owns throwing. */
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
    unsigned old_frm;
    init_frame(&f);
    f.stack[BASE].bits = 0x3f800000u;
    f.stack[BASE + 1].bits = 0x33800000u; /* halfway: 1 + 2^-24 */
    test_name = "fixed float RNE with host rounding up";
    CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
    __asm__ volatile("csrrw %0, frm, %1" : "=r"(old_frm) : "r"(3u));
    invoke(generated, block.size, &f.stack[BASE + 2], f.locals);
    __asm__ volatile("csrw frm, %0" : : "r"(old_frm));
    CHECK(f.stack[BASE].bits == 0x3f800000u);
#if NJ_JIT_RV32_D && NJ_ENABLE_LONG
    {
        uint64_t one = UINT64_C(0x3ff0000000000000), half = UINT64_C(0x3ca0000000000000), got;
        code = 0x63;
        memcpy(&f.stack[BASE], &one, 8); memcpy(&f.stack[BASE + 2], &half, 8);
        test_name = "fixed double RNE with host rounding up";
        CHECK(nj_rv32_compile(&m, 0, generated, sizeof(generated), &block));
        __asm__ volatile("csrrw %0, frm, %1" : "=r"(old_frm) : "r"(3u));
        invoke(generated, block.size, &f.stack[BASE + 4], f.locals);
        __asm__ volatile("csrw frm, %0" : : "r"(old_frm));
        memcpy(&got, &f.stack[BASE], 8);
        CHECK(got == one);
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
    if (require_execution && !EXECUTE_RV32) {
        fprintf(stderr, "FAIL: real RV32 execution requested on a non-RV32 Linux host\n");
        return 2;
    }
    CHECK(sizeof(nj_value) == 8);
    test_integer();
    test_float();
    test_boundaries();
    test_rounding();
    test_name = "extension coverage";
    CHECK(!NJ_JIT_RV32_M || seen_m);
    CHECK(!NJ_JIT_RV32_F || seen_f);
    CHECK(!NJ_JIT_RV32_D || !NJ_ENABLE_LONG || seen_d);
    CHECK(!NJ_JIT_RV32_C || (seen_c && seen_cli && seen_caddi));
#if EXECUTE_RV32
    CHECK(executed > 0);
    CHECK(munmap(exec_page, exec_size) == 0);
#endif
    printf("PASS RV32 M=%d A=%d F=%d D=%d C=%d LONG=%d: %u assertions, "
           "%u compiled, %u native executions (%s)\n",
           NJ_JIT_RV32_M, NJ_JIT_RV32_A, NJ_JIT_RV32_F, NJ_JIT_RV32_D,
           NJ_JIT_RV32_C, NJ_ENABLE_LONG, assertions, compiled, executed,
           EXECUTE_RV32 ? "RV32 machine code" : "encoding/metadata only; execution SKIPPED");
    return 0;
}
