/*
 * nano_j2me - test_natives.c
 *
 * Every method a C-implemented class declares must have an implementation
 * behind it.
 *
 * The failure this exists to catch is silent until the moment it is fatal: a
 * method present in the class table with no native resolves normally, passes
 * every check, and then throws UnsatisfiedLinkError on the first call.  Three
 * of them - Integer.hashCode, Integer.equals and Date.hashCode - were found
 * this way, each one only when a real game happened to call it.  Date.hashCode
 * was worse than missing: the function had been written and then never bound,
 * so the fix was already in the file, unreachable.
 *
 * The check walks nj_builtin_sources[] rather than a list written out here.
 * That matters: the first attempt at this was a script that parsed the C
 * source, and its pattern did not match the tables produced by the
 * WRAPPER_CLASS macro - so the boxing classes, the ones with the actual bug,
 * were skipped in silence and the script reported success.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int checked;
static int known_missing_seen;

/*
 * Implementations that are known to be absent, named one by one.
 *
 * The list is empty: every method a builtin class declares now has a native.
 * It is kept because the mechanism is what makes a gap visible rather than
 * skimmed over - an entry here is a documented, named exception, and the
 * count below is asserted so that implementing one forces the entry to be
 * removed instead of leaving a stale allowlist behind.  (It did exactly that
 * when the five concrete Reader/Writer methods were implemented.)
 */
static const char *const known_missing[] = {
    ""      /* never matches; the count below is what decides */
};
#define NKNOWN 0


static int is_known_missing(const char *cls, const char *name, const char *desc)
{
    char buf[160];
    int i;

    (void)snprintf(buf, sizeof(buf), "%s.%s%s", cls, name, desc);
    for (i = 0; i < NKNOWN; i++) {
        if (strcmp(buf, known_missing[i]) == 0) {
            known_missing_seen++;
            return 1;
        }
    }
    return 0;
}

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            printf("  FAIL  ");                                               \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/*
 * A method is legitimately implementation-free when it is abstract.  Everything
 * else - including <init> and <clinit> - is expected to be callable, so it
 * needs a native.
 */
/*
 * Some class tables list their constants alongside their methods, using the
 * field type as the "descriptor" - Connector.READ appears with "I".  Those
 * need no native, and the giveaway is that a real method descriptor always
 * begins with "(".  Without this the check reported seven constants as missing
 * implementations and buried the eleven that really were.
 */
static int needs_native(const nj_builtin_method *m)
{
    if (m->desc == NULL || m->desc[0] != '(') {
        return 0;
    }
#ifdef NJ_ACC_ABSTRACT
    if ((m->flags & NJ_ACC_ABSTRACT) != 0u) {
        return 0;
    }
#endif
    return 1;
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    int s;

    printf("== every declared builtin method has a native ==\n");

    if (nj_port_headless_ex(&port, 240, 320, 0, NULL) != NJ_OK) {
        printf("  FAIL  headless port\n");
        return 1;
    }
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) {
        printf("  FAIL  vm\n");
        nj_port_shutdown(&port);
        return 1;
    }

    for (s = 0; s < nj_builtin_source_count; s++) {
        const nj_builtin_class *cs = nj_builtin_sources[s].classes;
        int nc = *nj_builtin_sources[s].count;
        int c;

        for (c = 0; c < nc; c++) {
            const nj_builtin_class *k = &cs[c];
            int m;

            for (m = 0; m < (int)k->nmethods; m++) {
                const nj_builtin_method *mm = &k->methods[m];
                nj_native_fn fn;

                if (!needs_native(mm)) {
                    continue;
                }
                checked++;
                fn = nj_native_find(vm, k->name, mm->name, mm->desc);
                if (!fn && is_known_missing(k->name, mm->name, mm->desc)) {
                    continue;
                }
                CHECK(fn != NULL, "%s.%s%s has no native",
                      k->name, mm->name, mm->desc);
            }
        }
    }

    /*
     * The control.  A checker that walks nothing passes trivially, so assert it
     * actually looked at a meaningful number of methods - and that a method
     * which really is absent comes back absent, proving the lookup can fail.
     */
    CHECK(checked > 200, "checked only %d methods; the walk is wrong", checked);
    CHECK(nj_native_find(vm, "java/lang/Integer", "noSuchMethod", "()I") == NULL,
          "a missing native was reported as present; the lookup cannot fail");
    CHECK(known_missing_seen == NKNOWN,
          "%d of the %d known-missing methods are now implemented - "
          "remove them from known_missing[]",
          NKNOWN - known_missing_seen, NKNOWN);

    /*
     * Print what the binding table actually holds.  A method can be missing
     * either because no one declared a native for it, or because the entry was
     * dropped while the table was built - and those two need different fixes.
     */
    printf("  checked %d methods across %d modules\n",
           checked, nj_builtin_source_count);
    printf("  binding table: %u entries inserted, capacity %u\n",
           (unsigned)(vm->nnatives), (unsigned)(vm->native_cap));

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASS", failures);
    return failures ? 1 : 0;
}
