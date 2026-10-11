/*
 * nano_j2me - stub constants against the runtime's.
 *
 * A `public static final int` in tools/midp_stubs.py is INLINED by javac at
 * every use site, so a MIDlet carries the STUB's value while the runtime
 * compares against its own.  List.IMPLICIT was 1 in the stubs and 3 in the
 * runtime: every MIDlet asking for an implicit list got an exclusive one, and
 * nothing reported it.
 *
 * THIS READS THE RUNTIME DIRECTLY, and the first version did not - it ran a
 * MIDlet that printed the constants, which proves nothing, because javac had
 * already replaced each one with the stub's own value.  It agreed with itself
 * and would have passed whatever the runtime contained.  Reading the field out
 * of the class table is the only way to see what the runtime actually holds.
 *
 * The class and field names come from the generated expectation file
 * (tools/gen_const_fixture.py reads midp_stubs.py), so the two cannot drift.
 *
 * Usage: test_consts <constdemo.expected>
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
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

int main(int argc, char **argv)
{
    nj_vm *vm;
    FILE *f;
    char line[256];
    int n = 0, mism = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <constdemo.expected>\n", argv[0]);
        return 2;
    }
    printf("== nano_j2me stub constants vs runtime ==\n");

    vm = nj_vm_create(NULL, 2u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "cannot create a VM\n");
        return 2;
    }

    f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        nj_vm_destroy(vm);
        return 2;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq, *dot, *cls, *name;
        long want;
        int value;

        while (*line && (line[strlen(line) - 1u] == '\n' ||
                         line[strlen(line) - 1u] == '\r')) {
            line[strlen(line) - 1u] = '\0';
        }
        if (!line[0]) { continue; }
        eq = strrchr(line, '=');
        if (!eq) { continue; }
        *eq = '\0';
        want = strtol(eq + 1, NULL, 10);

        /* "a.b.C.NAME" -> class "a/b/C", field "NAME" */
        dot = strrchr(line, '.');
        if (!dot) { continue; }
        *dot = '\0';
        name = dot + 1;
        cls = line;
        for (dot = cls; *dot; dot++) {
            if (*dot == '.') { *dot = '/'; }
        }

        {
            nj_field *fd = nj_field_lookup(vm, cls, name, "I", 1);
            if (!fd) {
                /* A long constant.  TIME_UNKNOWN is one, and looking only for
                 * "I" reported it as missing when it was there. */
                nj_field *fl = nj_field_lookup(vm, cls, name, "J", 1);
                if (fl) {
                    nj_s8 v = nj_field_read_long(vm, NULL, fl);
                    if (v != want && mism < 200) {
                        printf("  %s.%s: the stubs say %ld, the runtime holds "
                               "%ld\n", cls, name, want, (long)v);
                        mism++;
                    }
                    n++;
                    continue;
                }
            }
            if (!fd) {
                if (mism < 200) {
                    printf("  the stubs declare %s.%s, the runtime has no such "
                           "field\n", cls, name);
                }
                mism++;
                continue;
            }
            value = nj_field_read_int(vm, NULL, fd);
            if ((long)value != want) {
                if (mism < 200) {
                    printf("  %s.%s: the stubs say %ld, the runtime holds %d\n",
                           cls, name, want, value);
                }
                mism++;
            }
        }
        n++;
    }
    fclose(f);

    printf("compared %d constants\n", n);
    ok(n > 100, "the expectation file was read");
    ok(mism == 0, "every constant the stubs declare has the runtime's value");

    nj_vm_destroy(vm);
    printf("\n%d checks, %d failures\n%s\n", checks, fails,
           fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
