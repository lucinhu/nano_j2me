/* Standalone x86-64 emitter tests. Link with the nj2me library, exactly like
 * test_jit_arm64.c; the backend under test is selected through nj_jit.c.
 *
 * On an x86-64 Linux host the emitted bytes are executed for real.  Other
 * hosts still run the encoding/metadata checks (analyze vs compile agreement,
 * capacity-prefix stability), which is where most emitter mistakes show up.
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
#if NJ_JIT_HOST_X86_64 && defined(__linux__) && NJ_ENABLE_JIT
#define EXECUTE_X86_64 1
#else
#define EXECUTE_X86_64 0
#endif
#if !NJ_ENABLE_JIT
#error "test_jit_x86_64 requires NJ_ENABLE_JIT=1"
#endif

enum { STACK_SLOTS = 96, LOCAL_SLOTS = 260, BASE = 16 };
typedef struct test_frame {
    nj_value stack[STACK_SLOTS];
    nj_value locals[LOCAL_SLOTS];
} test_frame;

static const char *test_name;
static nj_vm *jit_vm;
static nj_class *run_owner;
static unsigned assertions, compiled, executed;

static int analyze_method(const nj_method *m, nj_u4 pc, nj_jit_block *out)
{
    return nj_x86_64_analyze(jit_vm, m, pc, out);
}
static int compile_method(const nj_method *m, nj_u4 pc, nj_u1 *buf,
                          nj_size capacity, nj_jit_block *out)
{
    return nj_x86_64_compile(jit_vm, m, pc, buf, capacity, out);
}
#define CHECK(c) do { ++assertions; if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, \
            test_name ? test_name : "setup", #c); exit(1); } } while (0)

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

/* The allocation-free scan must agree with every emitted prefix.  Capacity may
 * shorten compilation, but never change the stack metadata of the prefix. */
static void same_metadata(const nj_jit_block *a, const nj_jit_block *b)
{
    unsigned i;
    CHECK(a->next_pc == b->next_pc && a->insns == b->insns);
    CHECK(a->stack_delta == b->stack_delta && a->min_stack == b->min_stack);
    CHECK(a->max_growth == b->max_growth);
    CHECK(a->nfaults == b->nfaults && a->nexits == b->nexits);
    for (i = 0; i < a->nfaults; ++i) {
        CHECK(a->faults[i].pc_off == b->faults[i].pc_off);
        CHECK(a->faults[i].sp_delta == b->faults[i].sp_delta);
    }
    for (i = 0; i < a->nexits; ++i) {
        CHECK(a->exits[i].pc_off == b->exits[i].pc_off);
        CHECK(a->exits[i].sp_delta == b->exits[i].sp_delta);
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
        if (prefix.next_pc == out->next_pc) same_metadata(out, &prefix);
        if (out->next_pc == analysis.next_pc) same_metadata(out, &analysis);
    } else if (buf && out && capacity >= NJ_JIT_MAX_CODE_BYTES) {
        CHECK(!scanned); /* one supported instruction must fit the full buffer */
    }
    return emitted;
}

static unsigned result_completed(nj_u4 w)
{
    if (w & NJ_JIT_FAULT_BIT) return (unsigned)(w & NJ_JIT_FAULT_COMPLETED_MASK);
    return (unsigned)(w >> NJ_JIT_EXIT_SHIFT);
}
static unsigned result_exit(nj_u4 w) { return (unsigned)(w & NJ_JIT_EXIT_INDEX_MASK); }
static unsigned result_fault(nj_u4 w)
{
    return (unsigned)((w >> NJ_JIT_FAULT_INDEX_SHIFT) & NJ_JIT_FAULT_INDEX_MASK);
}
static int result_is_fault(nj_u4 w) { return (w & NJ_JIT_FAULT_BIT) != 0u; }

#if EXECUTE_X86_64
static void *exec_page;
static size_t exec_size;

static nj_u4 invoke(const nj_u1 *code, size_t length, nj_value *sp,
                    nj_value *locals, nj_u4 budget)
{
    nj_jit_fn fn;
    nj_u4 result;
    if (!exec_page) {
        long page = sysconf(_SC_PAGESIZE);
        CHECK(page > 0);
        exec_size = ((size_t)NJ_JIT_MAX_CODE_BYTES + (size_t)page - 1u) /
                    (size_t)page * (size_t)page;
        exec_page = mmap(NULL, exec_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(exec_page != MAP_FAILED);
    } else {
        CHECK(mprotect(exec_page, exec_size, PROT_READ | PROT_WRITE) == 0);
    }
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
#endif

/* Compare the frame's live region only: the operand stack slots above the
 * final stack pointer are scratch (the interpreter leaves stale bits there
 * too), so only the incoming prefix and the final live depth are compared. */
static void compare_frame(const test_frame *actual, const test_frame *expected,
                          int depth, int stack_delta)
{
    int live = depth + (stack_delta > 0 ? stack_delta : 0);
    size_t stack_bytes = (size_t)(BASE + live) * sizeof(nj_value);
    CHECK(live >= 0 && BASE + live <= STACK_SLOTS);
    CHECK(memcmp(actual->locals, expected->locals,
                 sizeof(expected->locals)) == 0);
    CHECK(memcmp(actual->stack, expected->stack, stack_bytes) == 0);
}

/* Compile at pc 0, run it with `depth` slots already on the operand stack, and
 * compare the whole frame against a hand-written expectation.  Returns the
 * native result word. */
static nj_u4 run_expect(const char *name, const nj_u1 *code, size_t length,
                        unsigned local_count, int depth, const test_frame *initial,
                        const test_frame *expected, nj_jit_block *out)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_method m = method(code, length, local_count);
    nj_u4 ret = 0;
    m.owner = run_owner;
    test_name = name;
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), out) == 1);
    CHECK(out->next_pc <= length);
    if (getenv("X64DBG")) {
        size_t bi;
        fprintf(stderr, "DBG %s size=%u bytes:", name, (unsigned)out->size);
        for (bi = 0; bi < out->size; ++bi) fprintf(stderr, " %02x", generated[bi]);
        fprintf(stderr, "\n");
    }
    ++compiled;
#if EXECUTE_X86_64
    {
        test_frame actual = *initial;
        ret = invoke(generated, out->size, &actual.stack[BASE + depth],
                     actual.locals, out->insns + 64u);
        compare_frame(&actual, expected, depth, out->stack_delta);
    }
#else
    (void)initial;
    (void)expected;
    (void)depth;
#endif
    return ret;
}

static void expect_success(nj_u4 ret, const nj_jit_block *block)
{
#if EXECUTE_X86_64
    CHECK(!result_is_fault(ret));
    CHECK(result_exit(ret) == 0u);
    CHECK(result_completed(ret) == block->insns);
#else
    (void)ret; (void)block;
#endif
}

static void expect_fault(nj_u4 ret, unsigned fault, unsigned completed)
{
#if EXECUTE_X86_64
    CHECK(result_is_fault(ret));
    CHECK(result_fault(ret) == fault);
    CHECK(result_completed(ret) == completed);
#else
    (void)ret; (void)fault; (void)completed;
#endif
}

/* ---- straight-line integer cases ------------------------------------ */

static void tests_integer(void)
{
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;
    static const nj_u1 arith[] = {
        0x1a, 0x1b, 0x60, 0x3c,   /* iload_0 iload_1 iadd istore_1 */
        0x1a, 0x1b, 0x68, 0x3d    /* iload_0 iload_1 imul istore_2 */
    };
    static const nj_u1 branch[] = {
        0x1a, 0x99, 0x00, 0x03,   /* iload_0 ifeq -> pc4 (depth 0) */
        0x04, 0x3c                /* iconst_1 istore_1 */
    };
    static const nj_u1 dup_swap[] = {
        0x1a, 0x1b, 0x5f, 0x3c, 0x3d  /* iload_0 iload_1 swap istore_1 istore_2 */
    };
    static const nj_u1 conv[] = {
        0x1a, 0x91, 0x3c,        /* iload_0 i2b istore_1 */
        0x1a, 0x92, 0x3d,        /* iload_0 i2c istore_2 */
        0x1a, 0x93, 0x3e         /* iload_0 i2s istore_3 */
    };

    init_frame(&initial);
    initial.locals[0].i = 7;
    initial.locals[1].i = 5;
    expected = initial;
    expected.locals[1].i = 12;
    expected.locals[2].i = 7 * 12; /* iload_0 * the just-stored iload_1 */
    ret = run_expect("arith", arith, sizeof(arith), 4, 0, &initial, &expected,
                     &block);
    CHECK(block.insns == 8 && block.stack_delta == 0 && block.next_pc == 8);
    CHECK(block.nfaults == 0 && block.nexits == 1);
    expect_success(ret, &block);

    /* Not taken: falls through to the exit-0 window end. */
    init_frame(&initial);
    initial.locals[0].i = 1;
    expected = initial;
    expected.locals[1].i = 1;
    ret = run_expect("if-fallthrough", branch, sizeof(branch), 2, 0, &initial,
                     &expected, &block);
    CHECK(block.insns == 4 && block.nfaults == 0 && block.nexits == 1);
    expect_success(ret, &block);

    /* Taken: the target instruction is inside the window, so the emitter
     * turns the branch into an internal jump and both paths converge on the
     * same code.  The result word is therefore the ordinary exit 0. */
    init_frame(&initial);
    initial.locals[0].i = 0;
    expected = initial;
    expected.locals[1].i = 1;
    ret = run_expect("if-taken", branch, sizeof(branch), 2, 0, &initial,
                     &expected, &block);
    CHECK(block.nexits == 1);
    expect_success(ret, &block);
    (void)ret;

    init_frame(&initial);
    initial.locals[0].i = 7;
    initial.locals[1].i = 5;
    expected = initial;
    expected.locals[1].i = 7;
    expected.locals[2].i = 5;
    ret = run_expect("swap", dup_swap, sizeof(dup_swap), 3, 0, &initial,
                     &expected, &block);
    expect_success(ret, &block);

    init_frame(&initial);
    initial.locals[0].i = 0x1ffff;
    expected = initial;
    expected.locals[1].i = -1;
    expected.locals[2].i = 0xffff;
    expected.locals[3].i = -1;
    ret = run_expect("i2b/i2c/i2s", conv, sizeof(conv), 4, 0, &initial,
                     &expected, &block);
    expect_success(ret, &block);
}

/* ---- loop and budget ------------------------------------------------ */

static void tests_loop(void)
{
    static const nj_u1 loop[] = {
        0x1a, 0x04, 0x60, 0x3b,  /* iload_0 iconst_1 iadd istore_0 */
        0x1a, 0x10, 0x05,        /* iload_0 bipush 5 */
        0xa1, 0xff, 0xf9         /* if_icmplt -7 (back to pc 0) */
    };
    test_frame initial, expected;
    nj_jit_block block;
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_method m = method(loop, sizeof(loop), 1);
    nj_u4 ret;

    test_name = "loop";
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 1);
    CHECK(block.loop == 1 && block.nfaults == 0);
    CHECK(block.exits[0].pc_off == sizeof(loop));
    {
        unsigned i, budget_exit = 0xffffffffu;
        for (i = 0; i < block.nexits; ++i)
            if (block.exits[i].pc_off == 0) budget_exit = i;
        CHECK(budget_exit != 0xffffffffu);
#if EXECUTE_X86_64
        /* Budget 10: one full iteration (7 completed) then the budget exit. */
        init_frame(&initial);
        init_frame(&expected);
        initial.locals[0].i = 0;
        ret = invoke(generated, block.size, &initial.stack[BASE],
                     initial.locals, 10u);
        CHECK(!result_is_fault(ret));
        CHECK(result_exit(ret) == budget_exit);
        CHECK(result_completed(ret) == 7);
        CHECK(initial.locals[0].i == 1);
        /* Large budget: five iterations complete and the loop falls through. */
        init_frame(&initial);
        init_frame(&expected);
        initial.locals[0].i = 0;
        expected.locals[0].i = 5;
        ret = invoke(generated, block.size, &initial.stack[BASE],
                     initial.locals, 1000u);
        CHECK(!result_is_fault(ret));
        CHECK(result_exit(ret) == 0);
        CHECK(result_completed(ret) == 35);
        CHECK(initial.locals[0].i == 5);
        (void)expected;
#endif
    }
    (void)ret;
}

/* ---- division faults ------------------------------------------------ */

static void tests_div(void)
{
    static const nj_u1 idiv[] = {0x6c};
    static const nj_u1 irem[] = {0x70};
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;
    unsigned i;

    for (i = 0; i < 2; ++i) {
        const nj_u1 *code = i ? irem : idiv;
        init_frame(&initial);
        initial.stack[BASE].i = 10;
        initial.stack[BASE + 1].i = 3;
        expected = initial;
        expected.stack[BASE].i = i ? 1 : 3; /* 10%3 = 1, 10/3 = 3 */
        ret = run_expect(i ? "irem" : "idiv", code, 1u, 2, 2, &initial,
                         &expected, &block);
        CHECK(block.insns == 1 && block.stack_delta == -1);
        CHECK(block.nfaults == 1 && block.faults[0].pc_off == 0 &&
              block.faults[0].sp_delta == 0);
        expect_success(ret, &block);

        /* divisor zero: fault to the interpreter before any side effect */
        init_frame(&initial);
        initial.stack[BASE].i = 10;
        initial.stack[BASE + 1].i = 0;
        expected = initial;
        ret = run_expect(i ? "irem-zero" : "idiv-zero", code, 1u, 2, 2,
                         &initial, &expected, &block);
        expect_fault(ret, 0u, 0u);

        /* divisor -1 would trap the CPU on INT_MIN; it must fault instead */
        init_frame(&initial);
        initial.stack[BASE].i = -2147483647 - 1;
        initial.stack[BASE + 1].i = -1;
        expected = initial;
        ret = run_expect(i ? "irem-minus1" : "idiv-minus1", code, 1u, 2, 2,
                         &initial, &expected, &block);
        expect_fault(ret, 0u, 0u);
    }
}

/* ---- float cases ---------------------------------------------------- */

static void tests_float(void)
{
    static const nj_u1 fadd[] = {
        0x0c, 0x22, 0x62, 0x43   /* fconst_1 fload_0 fadd fstore_0 */
    };
    static const nj_u1 fneg[] = {
        0x22, 0x76, 0x43        /* fload_0 fneg fstore_0 */
    };
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;

    init_frame(&initial);
    initial.locals[0].f = 2.5f;
    expected = initial;
    expected.locals[0].f = 3.5f;
    ret = run_expect("fadd", fadd, sizeof(fadd), 1, 0, &initial, &expected,
                     &block);
    expect_success(ret, &block);

    init_frame(&initial);
    initial.locals[0].f = 1.5f;
    expected = initial;
    expected.locals[0].f = -1.5f;
    ret = run_expect("fneg", fneg, sizeof(fneg), 1, 0, &initial, &expected,
                     &block);
    expect_success(ret, &block);
}

/* ---- field and array fixtures --------------------------------------- */

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

static void tests_fields(void)
{
    field_fixture fx;
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;
    unsigned off = field_offset();
    static const nj_u1 getfield[] = {
        0x2a, 0xb4, 0x00, 0x01, 0x3c  /* aload_0 getfield #1 istore_1 */
    };
    static const nj_u1 putfield[] = {
        0x2a, 0x1b, 0xb5, 0x00, 0x01 /* aload_0 iload_1 putfield #1 */
    };
    nj_s4 raw;

    field_fixture_init(&fx, NJ_PT_INT, 1u, off);
    raw = 0x12345678;
    memcpy(&fx.object.bytes[off], &raw, 4u);
    init_frame(&initial);
    initial.locals[0].r = (nj_ref)(void *)fx.object.bytes;
    expected = initial;
    expected.locals[1].i = raw;
    jit_vm = &fx.vm;
    run_owner = &fx.owner;
    ret = run_expect("getfield:int", getfield, sizeof(getfield), 2, 1,
                     &initial, &expected, &block);
    jit_vm = NULL;
    run_owner = NULL;
    CHECK(block.nfaults == 1 && block.faults[0].pc_off == 1 &&
          block.faults[0].sp_delta == 1);
    expect_success(ret, &block);

    field_fixture_init(&fx, NJ_PT_INT, 1u, off);
    init_frame(&initial);
    initial.locals[0].r = (nj_ref)(void *)fx.object.bytes;
    initial.locals[1].i = 0x0badcafe;
    expected = initial;
    jit_vm = &fx.vm;
    run_owner = &fx.owner;
    ret = run_expect("putfield:int", putfield, sizeof(putfield), 2, 2,
                     &initial, &expected, &block);
    jit_vm = NULL;
    run_owner = NULL;
#if EXECUTE_X86_64
    memcpy(&raw, &fx.object.bytes[off], 4u);
    CHECK(raw == 0x0badcafe);
#endif
    expect_success(ret, &block);
    (void)raw;
}

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

static void tests_arrays(void)
{
    array_fixture ax;
    test_frame initial, expected;
    nj_jit_block block;
    nj_u4 ret;
    static const nj_u1 iaload[] = {
        0x2a, 0x1b, 0x2e, 0x3d  /* aload_0 iload_1 iaload istore_2 */
    };
    static const nj_u1 iaload_oob[] = {
        0x2a, 0x1b, 0x2e, 0x3d
    };
    static const nj_u1 iastore[] = {
        0x2a, 0x1b, 0x1c, 0x4f  /* aload_0 iload_1 iload_2 iastore */
    };
    static const nj_u1 arraylength[] = {0x2a, 0xbe, 0x3c};

    array_fixture_init(&ax, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT);
    init_frame(&initial);
    initial.locals[0].r = (nj_ref)(void *)&ax.arr;
    initial.locals[1].i = 3;
    expected = initial;
    expected.locals[2].i = ax.ints[3];
    jit_vm = &ax.vm;
    ret = run_expect("iaload", iaload, sizeof(iaload), 3, 0, &initial,
                     &expected, &block);
    jit_vm = NULL;
    CHECK(block.insns == 4 && block.nfaults == 1);
    expect_success(ret, &block);

    array_fixture_init(&ax, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT);
    init_frame(&initial);
    initial.locals[0].r = (nj_ref)(void *)&ax.arr;
    initial.locals[1].i = ARRAY_LEN + 2;
    expected = initial;
    jit_vm = &ax.vm;
    ret = run_expect("iaload-oob", iaload_oob, sizeof(iaload_oob), 3, 0,
                     &initial, &expected, &block);
    jit_vm = NULL;
    CHECK(block.faults[0].pc_off == 2 && block.faults[0].sp_delta == 2);
    expect_fault(ret, 0u, 2u);

    array_fixture_init(&ax, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT);
    init_frame(&initial);
    initial.locals[0].r = (nj_ref)(void *)&ax.arr;
    initial.locals[1].i = 5;
    initial.locals[2].i = 0x55aa55aa;
    expected = initial;
    jit_vm = &ax.vm;
    ret = run_expect("iastore", iastore, sizeof(iastore), 3, 0, &initial,
                     &expected, &block);
    jit_vm = NULL;
#if EXECUTE_X86_64
    CHECK(ax.ints[5] == 0x55aa55aa);
#endif
    CHECK(block.nexits == 1 && block.nfaults == 1 && block.stack_delta == 0);
    expect_success(ret, &block);

    array_fixture_init(&ax, NJ_OBJ_ARRAY, NJ_ARRAY_T_INT);
    init_frame(&initial);
    initial.locals[0].r = (nj_ref)(void *)&ax.arr;
    expected = initial;
    expected.locals[1].i = ARRAY_LEN;
    jit_vm = &ax.vm;
    ret = run_expect("arraylength", arraylength, sizeof(arraylength), 2, 0,
                     &initial, &expected, &block);
    jit_vm = NULL;
    expect_success(ret, &block);
}

/* ---- declines without a VM ------------------------------------------ */

static void tests_declines(void)
{
    nj_u1 generated[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    static const nj_u1 getfield[] = {0xb4, 0x00, 0x01};
    static const nj_u1 iaload[] = {0x2e};
    nj_method m;

    test_name = "decline-getfield-null-vm";
    m = method(getfield, sizeof(getfield), 2);
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 0);

    test_name = "decline-iaload-null-vm";
    m = method(iaload, sizeof(iaload), 2);
    CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 0);

    /* Unsigned long arithmetic is outside this backend's coverage. */
    {
        static const nj_u1 ladd[] = {0x61}; /* ladd: outside the covered set */
        test_name = "decline-ladd";
        m = method(ladd, sizeof(ladd), 4);
        CHECK(compile_checked(&m, 0, generated, sizeof(generated), &block) == 0);
    }
}

/* ---- capacity and bounds -------------------------------------------- */

static void tests_capacity(void)
{
    static const nj_u1 many[] = {
        0x02, 0x03, 0x60, 0x02, 0x03, 0x60, 0x02, 0x03, 0x60,
        0x02, 0x03, 0x60, 0x02, 0x03, 0x60, 0x02, 0x03, 0x60,
        0x02, 0x03, 0x60, 0x02, 0x03, 0x60
    };
    nj_u1 guarded[64 + NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block full, short_block;
    nj_method m = method(many, sizeof(many), 2);
    nj_size i;

    test_name = "capacity";
    for (i = 0; i < sizeof(guarded); ++i) guarded[i] = 0x5a;
    CHECK(compile_checked(&m, 0, guarded + 64, NJ_JIT_MAX_CODE_BYTES, &full) == 1);
    CHECK(full.size > 0 && full.size <= NJ_JIT_MAX_CODE_BYTES);
    /* The entry bound is exact: the block fits a buffer of its own size. */
    CHECK(compile_method(&m, 0, guarded + 64, full.size, &short_block) == 1);
    CHECK(short_block.size == full.size && short_block.insns == full.insns);
    /* One byte short: declined, and nothing is written past the capacity. */
    for (i = 64u; i < sizeof(guarded); ++i) guarded[i] = 0x5a;
    CHECK(compile_method(&m, 0, guarded + 64, (nj_size)(full.size - 1u),
                         &short_block) == 0);
    for (i = 64u + (full.size - 1u); i < sizeof(guarded); ++i)
        CHECK(guarded[i] == 0x5a);
    /* Hopelessly small capacity is declined too. */
    for (i = 64u; i < sizeof(guarded); ++i) guarded[i] = 0x5a;
    CHECK(compile_method(&m, 0, guarded + 64, 2u, &short_block) == 0);
    for (i = 64u + 2u; i < sizeof(guarded); ++i) CHECK(guarded[i] == 0x5a);
}

int main(void)
{
    tests_integer();
    tests_loop();
    tests_div();
    tests_float();
    tests_fields();
    tests_arrays();
    tests_declines();
    tests_capacity();
    printf("test_jit_x86_64: ok (%u assertions, %u compiled, %u executed)\n",
           assertions, compiled, executed);
    return 0;
}
