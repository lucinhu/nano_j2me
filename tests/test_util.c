/* Hashtable regressions: GC during array construction/enumeration, and
 * collision-heavy growth must not invoke user equals() while relocating. */
#include "nj_internal.h"
#include "nj2me/nj.h"
#include <stdio.h>
#include <stdlib.h>

#define HT "java/util/Hashtable"
#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); \
} } while (0)

static nj_native_fn string_equals;
static nj_ref inserting;
static unsigned rehash_equals;

NJ_NATIVE_FN(count_equals)
{
    if (args[0].r != inserting && args[1].r != inserting) {
        rehash_equals++;
    }
    return string_equals(vm, th, args, ret);
}

static nj_ref table_new(nj_vm *vm)
{
    nj_ref h = nj_root(vm, nj_object_new(vm, HT));
    CHECK(h != NULL, "table allocation");
    CHECK(nj_call_special(vm, h, HT, "<init>", "()V", NULL, NULL) == NJ_OK,
          "table constructor");
    return h;
}

static nj_ref array_field(nj_vm *vm, nj_ref h, const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, HT, name, desc, 0);
    CHECK(f != NULL, "table array field");
    return nj_field_read_ref(vm, h, f);
}

/* Consume the unused arena with unreachable garbage.  The next allocation
 * larger than leave must collect; there are no earlier free-list blocks. */
static void pressure(nj_vm *vm, nj_size leave)
{
    nj_size free_tail = vm->heap.size - vm->heap.bump;
    CHECK(free_tail > leave + NJ_BLOCK_HDR, "enough space for pressure");
    CHECK(nj_alloc(vm, free_tail - leave - NJ_BLOCK_HDR) != NULL,
          "pressure allocation");
}

static void check_array_gc(nj_port *port)
{
    nj_vm *vm = nj_vm_create(port, 512u * 1024u);
    nj_ref h, k, v, hash, state;
    nj_value a;
    nj_u4 gc;
    CHECK(vm != NULL, "GC test VM");
    h = table_new(vm);
    /* One Object[256] fits, but the next must collect the first if unrooted. */
    pressure(vm, sizeof(nj_object) + 256u * sizeof(nj_ref) + 128u);
    gc = vm->heap.gc_count;
    a.i = 256;
    CHECK(nj_call_special(vm, h, HT, "<init>", "(I)V", &a, NULL) == NJ_OK,
          "capacity constructor survives GC");
    CHECK(vm->heap.gc_count > gc, "collection occurred between allocations");
    k = array_field(vm, h, "htKeys", "[Ljava/lang/Object;");
    v = array_field(vm, h, "htValues", "[Ljava/lang/Object;");
    hash = array_field(vm, h, "htHash", "[I");
    state = array_field(vm, h, "htState", "[I");
    CHECK(k && v && hash && state, "all arrays allocated");
    CHECK(nj_heap_owns_object(vm, k) && nj_heap_owns_object(vm, v) &&
          nj_heap_owns_object(vm, hash) && nj_heap_owns_object(vm, state),
          "arrays remain live after collection");
    CHECK(k != v && k != hash && k != state && v != hash && v != state &&
          hash != state, "GC does not reclaim arrays under construction");
    CHECK(k->length == 256 && v->length == 256 && hash->length == 256 &&
          state->length == 256, "all array lengths preserved");
    nj_vm_destroy(vm);
}

static void check_collisions(nj_port *port)
{
    nj_vm *vm = nj_vm_create(port, 512u * 1024u);
    nj_ref h, keys[16], value, e;
    nj_value a[2], r;
    nj_method *eq;
    int i, j;
    CHECK(vm != NULL, "collision test VM");
    h = table_new(vm);
    value = nj_root(vm, nj_string_new(vm, "payload"));
    CHECK(value != NULL, "value allocation");
    eq = nj_method_lookup(nj_class_lookup(vm, "java/lang/String"),
                          "equals", "(Ljava/lang/Object;)Z");
    CHECK(eq != NULL, "String.equals method");
    string_equals = nj_native_find(vm, "java/lang/String", "equals", "(Ljava/lang/Object;)Z");
    CHECK(string_equals != NULL, "String.equals native");
    eq->native = count_equals;
    rehash_equals = 0;
    for (i = 0; i < 16; ++i) {
        char text[9];
        /* Aa and BB have the same String hash; so do all concatenations. */
        for (j = 0; j < 4; ++j) {
            text[j * 2] = (i & (1 << j)) ? 'A' : 'B';
            text[j * 2 + 1] = (i & (1 << j)) ? 'a' : 'B';
        }
        text[8] = 0;
        keys[i] = nj_root(vm, nj_string_new(vm, text));
        CHECK(keys[i] != NULL, "key allocation");
        inserting = keys[i];
        a[0].r = keys[i];
        a[1].r = value;
        CHECK(nj_call(vm, h, HT, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                      a, &r) == NJ_OK, "put colliding key");
        CHECK(r.r == NULL, "new key has no previous value");
    }
    eq->native = string_equals;
    CHECK(rehash_equals == 0, "rehash never calls equals between existing keys");
    CHECK(nj_call(vm, h, HT, "size", "()I", NULL, &r) == NJ_OK && r.i == 16,
          "size survives repeated growth");
    for (i = 0; i < 16; ++i) {
        a[0].r = keys[i];
        CHECK(nj_call(vm, h, HT, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                      a, &r) == NJ_OK && r.r == value, "mapping survives growth");
        if (!(i & 1)) {
            CHECK(nj_call(vm, h, HT, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;",
                          a, &r) == NJ_OK && r.r == value, "remove from collision chain");
        }
    }
    for (i = 1; i < 16; i += 2) {
        a[0].r = keys[i];
        CHECK(nj_call(vm, h, HT, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                      a, &r) == NJ_OK && r.r == value, "tombstone preserves probe chain");
    }
    CHECK(nj_call(vm, h, HT, "elements", "()Ljava/util/Enumeration;", NULL, &r) == NJ_OK,
          "value enumeration");
    e = nj_root(vm, r.r);
    for (i = 0; i < 8; ++i) {
        CHECK(nj_call(vm, e, "java/util/Enumeration", "nextElement", "()Ljava/lang/Object;",
                      NULL, &r) == NJ_OK && r.r == value, "enumerates remaining values");
    }
    nj_vm_destroy(vm);
}

static void check_enumerator_gc(nj_port *port)
{
    nj_vm *vm = nj_vm_create(port, 512u * 1024u);
    nj_ref h, key, e;
    nj_value a[2], r;
    nj_size snapshot_bytes;
    nj_u4 gc;
    CHECK(vm != NULL, "enumeration test VM");
    h = table_new(vm);
    key = nj_root(vm, nj_string_new(vm, "key"));
    CHECK(key != NULL, "enumeration key");
    a[0].r = key;
    a[1].r = key;
    CHECK(nj_call(vm, h, HT, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                  a, &r) == NJ_OK, "enumeration mapping");
    snapshot_bytes = sizeof(nj_object) + sizeof(nj_ref) + NJ_BLOCK_HDR;
    snapshot_bytes = (snapshot_bytes + NJ_OBJ_ALIGN - 1u) & ~(nj_size)(NJ_OBJ_ALIGN - 1u);
    pressure(vm, snapshot_bytes);
    gc = vm->heap.gc_count;
    CHECK(nj_call(vm, h, HT, "keys", "()Ljava/util/Enumeration;", NULL, &r) == NJ_OK,
          "keys under memory pressure");
    e = nj_root(vm, r.r);
    CHECK(vm->heap.gc_count > gc, "collection occurred after snapshot allocation");
    CHECK(nj_heap_owns_object(vm, nj_field_read_ref(vm, e,
          nj_field_lookup(vm, "java/util/HashtableEnumerator", "heKeys",
                          "[Ljava/lang/Object;", 0))),
          "enumeration snapshot remains live after collection");
    CHECK(nj_call(vm, e, "java/util/Enumeration", "nextElement", "()Ljava/lang/Object;",
                  NULL, &r) == NJ_OK && r.r == key, "snapshot survives enumerator allocation");
    nj_vm_destroy(vm);
}

int main(void)
{
    nj_port port;
    CHECK(nj_port_headless(&port) == NJ_OK, "headless port");
    check_collisions(&port);
    check_array_gc(&port);
    check_enumerator_gc(&port);
    nj_port_shutdown(&port);
    puts("test_util: Hashtable collision growth, tombstones and GC survival OK");
    return 0;
}
