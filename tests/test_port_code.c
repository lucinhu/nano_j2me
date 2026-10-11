/*
 * nano_j2me - the executable-memory contract.
 *
 * nj_port.code_alloc/code_commit/code_free exist so that a JIT has somewhere to
 * put generated code.  Nothing in the runtime calls them yet, and an API with no
 * caller and no test is an API nobody has checked - so this test is the caller.
 *
 * It is written the way a JIT would use them: allocate, write machine code,
 * commit, CALL WHAT WAS WRITTEN, then free.  Compiling proves nothing about
 * whether the pages are executable; jumping into them proves everything.
 *
 * The one architecture-specific part is the instruction sequence, so it is
 * guarded.  On a target where the bytes below are not "return 42", the test
 * still exercises alloc/commit/free and says so, rather than running off into
 * whatever those bytes happen to mean there.
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int fails;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL %s\n", what);
    }
}

int main(void)
{
    nj_port port;
    unsigned char *code;
    int written;
    int r = 0;

    printf("== nano_j2me executable memory (the JIT seam) ==\n");

    if (nj_port_headless_ex(&port, 64, 64, 0, NULL) != NJ_OK) {
        fprintf(stderr, "cannot create the headless port\n");
        return 2;
    }

    ok(port.code_alloc != NULL, "the port supplies code_alloc");
    ok(port.code_commit != NULL, "the port supplies code_commit");
    ok(port.code_free != NULL, "the port supplies code_free");
    if (fails) {
        printf("\n%d checks, %d failures\nFAIL\n", checks, fails);
        return 1;
    }

    code = (unsigned char *)port.code_alloc(port.ud, 64);
    ok(code != NULL, "code_alloc returns writable memory");
    if (!code) {
        printf("\n%d checks, %d failures\nFAIL\n", checks, fails);
        return 1;
    }

    /* Writability: a JIT has to be able to put bytes here at all. */
    code[0] = 0x90;                     /* nop, valid everywhere we build */
    ok(code[0] == 0x90, "the memory is writable before commit");

#if defined(__x86_64__)
    /* mov eax, 42 ; ret */
    code[0] = 0xB8; code[1] = 42; code[2] = 0; code[3] = 0; code[4] = 0;
    code[5] = 0xC3;
    written = 1;
#else
    written = 0;
#endif

    ok(port.code_commit(port.ud, code, 64) == 0, "code_commit accepts the block");

#if defined(__x86_64__)
    if (written) {
        int (*fn)(void);
        /*
         * The memcpy is not paranoia: casting a data pointer to a function
         * pointer and calling it is the one place this project steps outside
         * what ISO C promises, and going through an object of the right type
         * is how a JIT is expected to do it.
         */
        memcpy(&fn, &code, sizeof(fn));
        r = fn();
        ok(r == 42, "generated code RUNS and returns what it was written to");
        printf("  called generated code -> %d (want 42)\n", r);
    }
#else
    printf("  (not x86-64: alloc/commit/free exercised, execution not attempted)\n");
#endif

    port.code_free(port.ud, code, 64);
    ok(1, "code_free accepts the block it was given");

    nj_port_shutdown(&port);
    printf("\n%d checks, %d failures\n%s\n", checks, fails,
           fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
