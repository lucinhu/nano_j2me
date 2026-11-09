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
#include "nj_internal.h"

#include <limits.h>
#include <string.h>

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

/* Compare directly with a UTF-16 source, including NUL and surrogate units. */
static void test_buffer_get_chars(nj_vm *vm)
{
    static const nj_u2 text[] = {'A', 0x4e2d, 0, 0xd83d, 0xde00, 'Z'};
    static const char *classes[] = {
        "java/lang/StringBuffer", "java/lang/StringBuilder", "java/lang/String"
    };
    static const int bad[][3] = {
        {-1, 2, 0}, {0, 7, 0}, {3, 2, 0}, {0, 1, -1},
        {0, 2, 7}, {0, 1, INT_MAX}, {0, 6, INT_MAX},
        {INT_MAX, INT_MAX, 0}, {0, INT_MAX, 0}
    };
    nj_u4 mark = nj_root_mark(vm);
    nj_ref chars = nj_root(vm, nj_array_new(vm, "C", 6));
    nj_ref dst = nj_root(vm, nj_array_new(vm, "C", 8));
    int k, begin, end, off;
    memcpy(nj_array_data(chars), text, sizeof(text));
    for (k = 0; k < 3; k++) {
        nj_ref sb = nj_root(vm, nj_object_new(vm, classes[k]));
        nj_value a[4];
        nj_value r;
        nj_vm_stats before, after;
        char desc[64];
        a[0].r = chars;
        if (k == 2) {
            check(nj_call(vm, sb, classes[k], "<init>", "([C)V", a, NULL) == NJ_OK,
                  "getChars: initialize String");
        } else {
            check(nj_call(vm, sb, classes[k], "<init>", "()V", NULL, NULL) == NJ_OK,
                  "getChars: initialize buffer");
            snprintf(desc, sizeof(desc), "([C)L%s;", classes[k]);
            check(nj_call(vm, sb, classes[k], "append", desc, a, &r) == NJ_OK,
                  "getChars: populate buffer");
        }
        /* Warm lookup/binding before checking Java allocation counts. */
        a[0].i = 0; a[1].i = 6; a[2].r = dst; a[3].i = 1;
        check(nj_call(vm, sb, classes[k], "getChars", "(II[CI)V", a, NULL) == NJ_OK,
              "getChars: warm full copy");
        nj_vm_get_stats(vm, &before);
        for (begin = 0; begin <= 6; begin++) {
            for (end = begin; end <= 6; end++) {
                for (off = 0; off <= 8 - (end - begin); off++) {
                    nj_u2 expected[8];
                    int i;
                    for (i = 0; i < 8; i++) expected[i] = 0x55aa;
                    memcpy(nj_array_data(dst), expected, sizeof(expected));
                    memcpy(expected + off, text + begin,
                           (size_t)(end - begin) * sizeof(nj_u2));
                    a[0].i = begin; a[1].i = end; a[2].r = dst; a[3].i = off;
                    check(nj_call(vm, sb, classes[k], "getChars", "(II[CI)V", a, NULL) == NJ_OK &&
                          memcmp(nj_array_data(dst), expected, sizeof(expected)) == 0,
                          "getChars: range and untouched destination match UTF-16 reference");
                }
            }
        }
        nj_vm_get_stats(vm, &after);
        check(after.total_objects == before.total_objects,
              "getChars: repeated valid copies allocate no Java objects");
        for (begin = 0; begin < (int)(sizeof(bad) / sizeof(bad[0])); begin++) {
            nj_u2 snapshot[8];
            nj_ref ex;
            memcpy(snapshot, nj_array_data(dst), sizeof(snapshot));
            a[0].i = bad[begin][0]; a[1].i = bad[begin][1];
            a[2].r = dst; a[3].i = bad[begin][2];
            check(nj_call(vm, sb, classes[k], "getChars", "(II[CI)V", a, NULL) != NJ_OK,
                  "getChars: invalid range throws");
            ex = nj_take_exception(vm);
            check(ex && nj_class_is_assignable(ex->cls,
                  nj_class_find(vm, "java/lang/IndexOutOfBoundsException")),
                  "getChars: range exception has expected type");
            check(memcmp(snapshot, nj_array_data(dst), sizeof(snapshot)) == 0,
                  "getChars: invalid range leaves destination unchanged");
        }
        a[0].i = 0; a[1].i = 0; a[2].r = NULL; a[3].i = 0;
        check(nj_call(vm, sb, classes[k], "getChars", "(II[CI)V", a, NULL) != NJ_OK,
              "getChars: null destination throws even for empty range");
        (void)nj_take_exception(vm);
    }
    nj_root_release(vm, mark);
}

/* Constructor capacities and public growth are part of the CLDC contract. */
static void test_buffer_capacity(nj_vm *vm)
{
    static const char *classes[] = {"java/lang/StringBuffer", "java/lang/StringBuilder"};
    static const int capacities[] = {0, 1, 15, 16, 17, 31, 64};
    static const int minimums[] = {0, -1, INT_MIN, 17, 35, 200, 1};
    static const int expected[] = {16, 16, 16, 34, 70, 200, 200};
    static const int bad[][2] = {{-1, 1}, {0, -1}, {INT_MAX, 1},
                                {1, INT_MAX}, {INT_MAX, INT_MAX}, {4, 0}};
    static const nj_u2 text[] = {'A', 0, 0x4e2d};
    nj_u4 mark = nj_root_mark(vm);
    nj_ref chars = nj_root(vm, nj_array_new(vm, "C", 3));
    nj_ref s = nj_root(vm, nj_new_string_utf16(vm, text, 3));
    int k, i;
    memcpy(nj_array_data(chars), text, sizeof(text));
    for (k = 0; k < 2; k++) {
        nj_ref sb;
        nj_value a[3], r;
        nj_ref ex;
        char desc[64];
        for (i = 0; i < (int)(sizeof(capacities) / sizeof(capacities[0])); i++) {
            sb = nj_root(vm, nj_object_new(vm, classes[k]));
            a[0].i = capacities[i];
            check(nj_call(vm, sb, classes[k], "<init>", "(I)V", a, NULL) == NJ_OK,
                  "capacity: integer constructor succeeds");
            check(nj_call(vm, sb, classes[k], "capacity", "()I", NULL, &r) == NJ_OK &&
                  r.i == capacities[i], "capacity: integer constructor preserves exact request");
            check(nj_call(vm, sb, classes[k], "length", "()I", NULL, &r) == NJ_OK &&
                  r.i == 0, "capacity: constructed buffer is empty");
        }
        sb = nj_root(vm, nj_object_new(vm, classes[k]));
        a[0].i = -1;
        check(nj_call(vm, sb, classes[k], "<init>", "(I)V", a, NULL) != NJ_OK,
              "capacity: negative constructor throws");
        ex = nj_take_exception(vm);
        check(ex && nj_class_is_assignable(ex->cls,
              nj_class_find(vm, "java/lang/NegativeArraySizeException")),
              "capacity: negative constructor exception type");
        a[0].r = s;
        check(nj_call(vm, sb, classes[k], "<init>", "(Ljava/lang/String;)V", a, NULL) == NJ_OK,
              "capacity: string constructor succeeds");
        check(nj_call(vm, sb, classes[k], "capacity", "()I", NULL, &r) == NJ_OK && r.i == 19,
              "capacity: string constructor reserves length plus sixteen");
        a[0].i = 2;
        check(nj_call(vm, sb, classes[k], "charAt", "(I)C", a, &r) == NJ_OK && r.i == 0x4e2d,
              "capacity: string constructor preserves UTF-16 content");
        check(nj_call(vm, sb, classes[k], "<init>", "()V", NULL, NULL) == NJ_OK,
              "capacity: default constructor succeeds");
        for (i = 0; i < (int)(sizeof(minimums) / sizeof(minimums[0])); i++) {
            a[0].i = minimums[i];
            check(nj_call(vm, sb, classes[k], "ensureCapacity", "(I)V", a, NULL) == NJ_OK,
                  "capacity: valid ensureCapacity succeeds");
            check(nj_call(vm, sb, classes[k], "capacity", "()I", NULL, &r) == NJ_OK &&
                  r.i == expected[i], "capacity: growth is max(request, twice old plus two)");
        }
        a[0].r = chars; a[1].i = 0; a[2].i = 3;
        snprintf(desc, sizeof(desc), "([CII)L%s;", classes[k]);
        check(nj_call(vm, sb, classes[k], "append", desc, a, &r) == NJ_OK,
              "capacity: populate buffer for failure checks");
        a[0].i = 201;
        check(nj_call(vm, sb, classes[k], "ensureCapacity", "(I)V", a, NULL) == NJ_OK,
              "capacity: nonempty buffer grows successfully");
        check(nj_call(vm, sb, classes[k], "capacity", "()I", NULL, &r) == NJ_OK && r.i == 402,
              "capacity: nonempty growth follows the same capacity rule");
        a[0].i = 1;
        check(nj_call(vm, sb, classes[k], "charAt", "(I)C", a, &r) == NJ_OK && r.i == 0,
              "capacity: growth preserves embedded zero character");
        for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
            a[0].r = chars; a[1].i = bad[i][0]; a[2].i = bad[i][1];
            check(nj_call(vm, sb, classes[k], "append", desc, a, &r) != NJ_OK,
                  "append range: invalid or overflowing window throws");
            ex = nj_take_exception(vm);
            check(ex && nj_class_is_assignable(ex->cls,
                  nj_class_find(vm, "java/lang/IndexOutOfBoundsException")),
                  "append range: exception has expected type");
        }
        a[0].i = INT_MAX;
        check(nj_call(vm, sb, classes[k], "ensureCapacity", "(I)V", a, NULL) != NJ_OK,
              "capacity: allocation failure returns error status");
        ex = nj_take_exception(vm);
        check(ex && nj_class_is_assignable(ex->cls,
              nj_class_find(vm, "java/lang/OutOfMemoryError")),
              "capacity: allocation failure leaves OutOfMemoryError");
        check(nj_call(vm, sb, classes[k], "capacity", "()I", NULL, &r) == NJ_OK && r.i == 402,
              "capacity: failed growth preserves old storage");
        check(nj_call(vm, sb, classes[k], "length", "()I", NULL, &r) == NJ_OK && r.i == 3,
              "capacity: failed operations preserve length");
        a[0].i = 2;
        check(nj_call(vm, sb, classes[k], "charAt", "(I)C", a, &r) == NJ_OK && r.i == 0x4e2d,
              "capacity: failed operations preserve content");
        a[0].i = 0;
        check(nj_call(vm, sb, classes[k], "<init>", "(I)V", a, NULL) == NJ_OK,
              "capacity: zero-capacity constructor succeeds");
        a[0].i = 1;
        check(nj_call(vm, sb, classes[k], "setLength", "(I)V", a, NULL) == NJ_OK,
              "capacity: zero-capacity buffer can grow");
        check(nj_call(vm, sb, classes[k], "capacity", "()I", NULL, &r) == NJ_OK && r.i == 2,
              "capacity: growth from zero uses two characters");
        a[0].i = 0;
        check(nj_call(vm, sb, classes[k], "charAt", "(I)C", a, &r) == NJ_OK && r.i == 0,
              "capacity: setLength initializes new character to zero");
    }
    nj_root_release(vm, mark);
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

    test_buffer_get_chars(vm);
    test_buffer_capacity(vm);

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
