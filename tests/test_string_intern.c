/* Interned string table: growth past the default capacity, JLS literal
 * identity, and survival of a collection.
 *
 * Regression test for the stale constant-pool String that GC turned into an
 * Image on titles with more than NJ_MAX_INTERNED string literals (see the
 * "interned string" section of docs/COMPATIBILITY.md): nj_intern() used to
 * return the String uninterned once a fixed 512-entry table filled up, and the
 * constant-pool cache kept that unrooted reference. */
#include "nj_internal.h"
#include "nj2me/nj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); \
} } while (0)

#define COUNT 3000

static void check_text(nj_ref s, const char *want)
{
    nj_u2 *ch;
    nj_u4 i, n;

    CHECK(s != NULL, "interned entry is not null");
    ch = nj_string_chars(s);
    n = nj_string_len(s);
    CHECK(n == (nj_u4)strlen(want), "interned length");
    for (i = 0; i < n; i++) {
        CHECK(ch[i] == (nj_u2)(unsigned char)want[i], "interned text");
    }
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref *refs;
    char buf[32];
    int i;

    refs = (nj_ref *)malloc(sizeof(nj_ref) * (size_t)COUNT);
    CHECK(refs != NULL, "malloc");
    CHECK(nj_port_headless(&port) == NJ_OK, "port");
    vm = nj_vm_create(&port, 4096u * 1024u);
    CHECK(vm != NULL, "vm");
    CHECK(vm->ninterned == 0u, "table starts empty");
    CHECK(vm->interned == NULL, "no table storage until needed");

    /* Fill well past the default 512-entry table. */
    for (i = 0; i < COUNT; i++) {
        nj_ref raw;
        snprintf(buf, sizeof buf, "intern-%d", i);
        raw = nj_new_string_utf8(vm, buf, (int)strlen(buf));
        CHECK(raw != NULL, "string allocation");
        refs[i] = nj_intern(vm, raw);
        CHECK(refs[i] != NULL, "interned");
        CHECK(vm->exception == NULL, "no exception while the table grows");
    }
    CHECK(vm->ninterned == (nj_u4)COUNT, "every distinct string is interned");
    CHECK(vm->intern_cap >= (nj_u4)COUNT, "table grew past the default");
    CHECK(vm->interned != NULL, "table has storage");

    /* A second copy of the same text must be the same object. */
    CHECK(nj_intern(vm, nj_new_string_utf8(vm, "intern-0", 8)) == refs[0],
          "duplicate literal keeps identity");
    CHECK(nj_intern(vm, nj_new_string_utf8(vm, "intern-2999", 11)) ==
          refs[COUNT - 1], "duplicate literal keeps identity after growth");

    /* A collection must not free any interned String. */
    CHECK(nj_gc_run(vm) == NJ_OK, "first collection");
    for (i = 0; i < COUNT; i++) {
        snprintf(buf, sizeof buf, "intern-%d", i);
        check_text(refs[i], buf);
    }

    /* Nor may a collection while the table is full of dead temporaries: the
     * literals live in the table heap, not in the arena. */
    for (i = 0; i < 2000; i++) {
        CHECK(nj_new_string_utf8(vm, "temporary", 9) != NULL, "garbage alloc");
    }
    CHECK(nj_gc_run(vm) == NJ_OK, "second collection");
    check_text(refs[COUNT - 1], "intern-2999");
    check_text(refs[1000], "intern-1000");

    free(refs);
    nj_vm_destroy(vm);
    puts("test_string_intern: intern growth, identity and GC survival OK");
    return 0;
}
