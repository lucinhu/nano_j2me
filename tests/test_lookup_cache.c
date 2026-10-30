/*
 * nano_j2me - tests/test_lookup_cache.c
 *
 * nj_class_lookup() and nj_field_lookup() remember their answers by the
 * POINTERS they were handed.  That matters because the 3D renderer asks for
 * the same fields once per node per draw - 22,000 class resolutions and 14,000
 * field lookups per frame of Minecraft PE - and every call site there spells
 * the names as string literals.
 *
 * Two properties keep that memo correct instead of fast-and-wrong, and this
 * test pins both:
 *
 *   1. A hit is only taken when the class or field still carries the names
 *      that were asked for.  A caller reusing one buffer for different names
 *      gets a fresh lookup, not the answer to the previous name.
 *   2. The memo is per VM and starts empty, so a VM built over the memory of a
 *      destroyed one cannot answer with the old VM's class pointers.
 *
 * The counters in nj_vm_stats are what a benchmark reads to tell a working
 * memo from a thrashing one, so they are checked here too.
 *
 * Usage: test_lookup_cache
 */
#include "nj2me/nj.h"
#include "nj2me/nj_config.h"
#include "nj2me/nj_vm.h"
#include "nj_internal.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int fails;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static const char *CLS_STRING = "java/lang/String";
static const char *CLS_OBJECT = "java/lang/Object";
static const char *CLS_M3G_NODE = "javax/microedition/m3g/Node";

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_class *a, *b;
    nj_vm_stats before, after;
    char buf[64];
    int i;

    if (nj_port_headless(&port) != NJ_OK) {
        printf("FAILED: no headless port\n");
        return 1;
    }
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        printf("FAILED: no VM\n");
        nj_port_shutdown(&port);
        return 1;
    }
    (void)nj_lib_late_init(vm);

    /* 1. Repeated literals answer from the memo, and it is the right class. */
    a = nj_class_lookup(vm, CLS_STRING);
    b = nj_class_lookup(vm, CLS_STRING);
    ok(a != NULL && a == b, "a repeated class name answers from the memo");
    ok(a != NULL && strcmp(a->name, CLS_STRING) == 0,
       "the memo answered with the class that was asked for");

    /* 2. A reused buffer is safe: same address, different text.  This is the
     *    failure a pointer-only cache has - it must not answer with Integer. */
    strcpy(buf, "java/lang/Integer");       /* 17 characters, fits */
    a = nj_class_lookup(vm, buf);
    strcpy(buf, CLS_OBJECT);
    b = nj_class_lookup(vm, buf);
    ok(a != NULL && b != NULL && a != b,
       "a reused buffer naming another class misses the memo");
    ok(b != NULL && strcmp(b->name, CLS_OBJECT) == 0,
       "the class under the reused buffer is the one that is resolved");
    ok(a != NULL && strcmp(a->name, "java/lang/Integer") == 0,
       "the first class under the reused buffer was Integer");

#if NJ_ENABLE_M3G
    /* 3. The renderer's field lookup: literal repeat, then a reused buffer. */
    {
        nj_field *f1 = nj_field_lookup(vm, CLS_M3G_NODE, "_nj_scope", "I", 0);
        nj_field *f2 = nj_field_lookup(vm, CLS_M3G_NODE, "_nj_scope", "I", 0);

        ok(f1 != NULL && f1 == f2,
           "a repeated field lookup answers from the memo");
        ok(f1 != NULL && strcmp(f1->name, "_nj_scope") == 0 && f1->owner != NULL &&
           strcmp(f1->owner->name, CLS_M3G_NODE) == 0,
           "the memo answered with the field that was asked for");

        strcpy(buf, "_nj_scope");
        f1 = nj_field_lookup(vm, CLS_M3G_NODE, buf, "I", 0);
        strcpy(buf, "_nj_alpha");            /* same address, other name */
        f2 = nj_field_lookup(vm, CLS_M3G_NODE, buf, "F", 0);
        ok(f1 != NULL && f2 != NULL && f1 != f2,
           "a reused buffer naming another field misses the memo");
        ok(f2 != NULL && strcmp(f2->name, "_nj_alpha") == 0,
           "the field under the reused buffer is the one that is resolved");

        /* A static request must not be answered by an instance field. */
        ok(nj_field_lookup(vm, CLS_M3G_NODE, "_nj_scope", "I", 1) == NULL,
           "a static request does not match an instance field with that name");

        /* A NULL descriptor means "match by name only".  Substituting an
         * empty descriptor for it would match nothing, which is how the
         * sensor test caught this. */
        f2 = nj_field_lookup(vm, CLS_M3G_NODE, "_nj_scope", NULL, 0);
        ok(f2 != NULL && strcmp(f2->name, "_nj_scope") == 0,
           "a NULL descriptor matches by name");
    }
#endif

    /* 4. The counters report the memo at work. */
    nj_vm_get_stats(vm, &before);
    for (i = 0; i < 100; i++) {
        (void)nj_class_lookup(vm, CLS_STRING);
    }
    nj_vm_get_stats(vm, &after);
    ok(after.find_hits > before.find_hits, "the memo reports hits");
    ok(after.find_misses == before.find_misses,
       "a warm literal lookup misses nothing");

    /* 5. A new VM starts with an empty memo. */
    nj_vm_destroy(vm);
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    ok(vm != NULL, "a second VM comes up");
    if (vm) {
        /* The memo lives in the VM structure, which is reused memory; VM
         * creation clears it.  A stale entry would answer with a class that
         * the old VM owned, so resolving a known class is the check. */
        (void)nj_lib_late_init(vm);
        a = nj_class_lookup(vm, CLS_OBJECT);
        ok(a != NULL && strcmp(a->name, CLS_OBJECT) == 0,
           "the fresh VM resolves a class without the old VM's memo");
        nj_vm_destroy(vm);
    }
    nj_port_shutdown(&port);

    printf("lookup cache: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
