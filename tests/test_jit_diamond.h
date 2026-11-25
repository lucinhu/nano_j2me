/* Common control-flow fixtures for the x86-64 and AArch64 window scanners. */
static void tests_diamond(void)
{
    /* local1 = local0 ? 1 : 2. The goto leaves one value, but the other
     * arm starts with the empty stack from ifeq. */
    static const nj_u1 select[] = {
        0x1a, 0x99, 0x00, 0x07, 0x04, 0xa7, 0x00, 0x04, 0x05, 0x3c
    };
    /* Two reachable branches enter pc12 after a goto at depths 0 and 1. */
    static const nj_u1 bad_join[] = {
        0x1a, 0x99, 0x00, 0x0b, 0x04, 0x1a, 0x99, 0x00, 0x06,
        0xa7, 0x00, 0x04, 0x00, 0x00
    };
    /* goto 8 skips two pushes and an ifeq also targeting pc8. Its inflated
     * depth and the dead branch must not participate in the reachable join. */
    static const nj_u1 dead[] = {
        0xa7, 0x00, 0x08, 0x04, 0x04, 0x99, 0x00, 0x03, 0x05, 0x3c
    };
    /* A guard in the second arm must restore that arm's incoming stack. */
    static const nj_u1 guarded[] = {
        0x1a, 0x99, 0x00, 0x07, 0x04, 0xa7, 0x00, 0x05, 0x2b, 0xbe, 0x3d
    };
    /* Both arms reach the unsupported ireturn right at the window end. */
    static const nj_u1 at_end[] = {
        0x1a, 0x99, 0x00, 0x07, 0x04, 0xa7, 0x00, 0x04, 0x05, 0xac
    };
    nj_u1 buf[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block b;
    nj_method m = method(select, sizeof(select), 2);
    nj_vm fake = {0};
    nj_vm *saved_vm = jit_vm;
#if EXECUTE_DIAMOND
    test_frame f;
    nj_u4 word;
    unsigned i;
#endif
    test_name = "diamond";
    CHECK(compile_checked(&m, 0, buf, sizeof(buf), &b));
    CHECK(b.insns == 6 && b.nexits == 1 && b.stack_delta == 0);
    CHECK(b.min_stack == 0 && b.max_growth == 1);
#if EXECUTE_DIAMOND
    for (i = 0; i <= 1; ++i) {
        init_frame(&f);
        f.locals[0].i = (nj_s4)i;
        word = invoke(buf, b.size, &f.stack[BASE], f.locals, b.insns);
        CHECK(word == ((i ? 5u : 4u) << NJ_JIT_EXIT_SHIFT));
        CHECK(f.locals[1].i == (i ? 1 : 2));
    }
#endif
    m = method(bad_join, sizeof(bad_join), 1);
    CHECK(!analyze_method(&m, 0, &b));
    m = method(dead, sizeof(dead), 2);
    CHECK(compile_checked(&m, 0, buf, sizeof(buf), &b));
    CHECK(b.min_stack == 0 && b.max_growth == 1 && b.stack_delta == 0);
#if EXECUTE_DIAMOND
    init_frame(&f);
    word = invoke(buf, b.size, &f.stack[BASE], f.locals, b.insns);
    CHECK(word == (3u << NJ_JIT_EXIT_SHIFT) && f.locals[1].i == 2);
#endif
    jit_vm = &fake; /* arraylength needs the VM-aware decoder */
    m = method(guarded, sizeof(guarded), 3);
    CHECK(compile_checked(&m, 0, buf, sizeof(buf), &b));
    CHECK(b.nfaults == 1 && b.faults[0].pc_off == 9 && b.faults[0].sp_delta == 1);
#if EXECUTE_DIAMOND
    init_frame(&f);
    f.locals[0].i = 0;
    f.locals[1].r = NULL;
    word = invoke(buf, b.size, &f.stack[BASE], f.locals, b.insns);
    CHECK(word == (NJ_JIT_FAULT_BIT | 3u) && f.stack[BASE].r == NULL);
#endif
    jit_vm = saved_vm;
    m = method(at_end, sizeof(at_end), 1);
    CHECK(compile_checked(&m, 0, buf, sizeof(buf), &b));
    CHECK(b.nexits == 1 && b.exits[0].pc_off == 9 && b.exits[0].sp_delta == 1);
#if EXECUTE_DIAMOND
    for (i = 0; i <= 1; ++i) {
        init_frame(&f);
        f.locals[0].i = (nj_s4)i;
        word = invoke(buf, b.size, &f.stack[BASE], f.locals, b.insns);
        CHECK(word == ((i ? 4u : 3u) << NJ_JIT_EXIT_SHIFT));
        CHECK(f.stack[BASE].i == (i ? 1 : 2));
    }
#endif
}
