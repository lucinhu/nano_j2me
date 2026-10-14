/*
 * java.lang.String.regionMatches, both overloads.
 *
 * Neither existed until 3D台球之王_5014 asked for the five-argument one: a
 * MIDP 1.0 class-library method was simply absent, and the game could not
 * finish starting.  The interesting cases are the ones a naive implementation
 * gets wrong -- the contract says an out-of-range window answers FALSE rather
 * than throwing, an empty window always matches, and the four-argument form is
 * case sensitive while the five-argument one is not unless told otherwise.
 */

#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_vm.h"

#include <stdio.h>

static int checks, failures;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

/* A java.lang.String holding the given UTF-8 text. */
static nj_ref str(nj_vm *vm, const char *utf8)
{
    return nj_string_new(vm, utf8);
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_value a[5], r;
    nj_ref hello, world, other;

    nj_port_headless(&port);
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) {
        printf("tests/test_string: could not create a VM\n");
        return 1;
    }

    hello = str(vm, "hello world");
    world = str(vm, "world");
    other = str(vm, "WORLD");
    if (!hello || !world || !other) {
        printf("tests/test_string: could not build the strings\n");
        nj_vm_destroy(vm);
        return 1;
    }

    /* regionMatches(int, String, int, int) */
    a[0].i = 6; a[1].r = world; a[2].i = 0; a[3].i = 5;
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 1,
          "\"hello world\".regionMatches(6, \"world\", 0, 5) is true");

    a[2].i = 1;
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 0,
          "the same window one character later is false");

    a[1].r = other;   /* WORLD */
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 0,
          "the four-argument form is case sensitive");

    /* regionMatches(boolean, int, String, int, int) */
    a[0].i = 1; a[1].i = 6; a[2].r = other; a[3].i = 0; a[4].i = 5;
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ZILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 1,
          "the five-argument form folds case when asked to");

    /* An empty window always matches, even at the very end of both strings. */
    a[0].i = 0; a[1].i = 11; a[2].r = world; a[3].i = 5; a[4].i = 0;
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ZILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 1,
          "a window of length zero matches at the end of the strings");

    /*
     * Out of range is FALSE, not an exception: a game that probes a substring
     * like this would otherwise die on the one call that is documented to be
     * safe.
     */
    a[1].i = 6; a[4].i = 6;
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ZILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 0,
          "a window running past the receiver's end answers false");

    a[1].i = -1;
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ZILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 0,
          "a negative offset answers false rather than throwing");

    a[1].i = 6; a[3].i = -2; a[4].i = 2;
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ZILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 0,
          "a negative offset in the other string answers false");

    /* A null argument is a NullPointerException, unlike an odd window. */
    a[1].i = 0; a[2].r = 0; a[3].i = 0; a[4].i = 1;
    {
        nj_status st = nj_call(vm, hello, "java/lang/String", "regionMatches",
                               "(ZILjava/lang/String;II)Z", a, &r);
        check(st != NJ_OK, "a null argument is refused, not compared");
        (void)nj_take_exception(vm);
    }

    /*
     * The control: an exact match must come back true, or every "false" above
     * would pass on an implementation that always answers false.
     */
    a[0].i = 0; a[1].i = 6; a[2].r = hello; a[3].i = 6; a[4].i = 5;
    check(nj_call(vm, hello, "java/lang/String", "regionMatches",
                  "(ZILjava/lang/String;II)Z", a, &r) == NJ_OK && r.i == 1,
          "control: \"hello world\" matches itself at 0 and 6");

    /* Metal Slug 4 appends char[] while painting its HUD. */
    {
        const char *classes[] = {"java/lang/StringBuffer", "java/lang/StringBuilder"};
        const char *descs[] = {"([C)Ljava/lang/StringBuffer;", "([C)Ljava/lang/StringBuilder;"};
        int k;
        for (k = 0; k < 2; k++) {
            nj_ref sb = nj_object_new(vm, classes[k]);
            nj_ref chars = nj_array_new(vm, "C", 2);
            nj_u2 *data = (nj_u2 *)nj_array_data(chars);
            nj_value values[2] = {{0}, {0}};
            data[0] = 'A'; data[1] = 0x4e2d;
            check(nj_call(vm, sb, classes[k], "<init>", "()V", NULL, NULL) == NJ_OK,
                  "create character buffer");
            values[0].r = chars;
            check(nj_call(vm, sb, classes[k], "append", descs[k], values, &r) == NJ_OK && r.r == sb,
                  "append(char[]) returns receiver");
            check(nj_call(vm, sb, classes[k], "length", "()I", NULL, &r) == NJ_OK && r.i == 2,
                  "append(char[]) copies entire array");
            values[0].i = 1;
            check(nj_call(vm, sb, classes[k], "charAt", "(I)C", values, &r) == NJ_OK && r.i == 0x4e2d,
                  "append(char[]) preserves Unicode");
        }
    }

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
