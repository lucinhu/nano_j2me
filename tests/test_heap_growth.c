/* Optional stable-address heap growth, including snapshot receiver policy.
 * Host allocator poison detects exposure of uninitialized reservation bytes. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define K 1024u
#define HOLDER "test/HeapGrowthRoots"
static unsigned checks, failures;
#define CHECK(x, why) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "FAIL growth line %d: %s\n", __LINE__, why); } } while (0)
static void require(int ok, const char *why)
{
    if (!ok) { fprintf(stderr, "growth fixture: %s\n", why); exit(2); }
}
typedef struct host_state { unsigned live, calls, fail_at, growth_logs; } host_state;
static void *host_alloc(void *ud, nj_size n)
{
    host_state *s = ud; void *p;
    if (++s->calls == s->fail_at) return NULL;
    p = malloc(n);
    if (p) { memset(p, 0xa5, n); ++s->live; }
    return p;
}
static void host_free(void *ud, void *p)
{
    host_state *s = ud;
    if (p) { require(s->live != 0u, "balanced host free"); --s->live; free(p); }
}
static void host_log(void *ud, int level, const char *msg)
{
    host_state *s = ud;
    (void)level;
    if (strstr(msg, "heap grew:")) ++s->growth_logs;
}
static void port_init(nj_port *p, host_state *s)
{
    memset(p, 0, sizeof(*p)); memset(s, 0, sizeof(*s));
    p->ud = s; p->user = s; p->alloc = host_alloc; p->free = host_free; p->log = host_log;
}
static int bytes_are(const void *p, size_t n, unsigned char v)
{
    const unsigned char *b = p;
    while (n--) if (*b++ != v) return 0;
    return 1;
}
static nj_ref rooted(nj_vm *vm, nj_ref r)
{
    nj_u4 mark = nj_root_mark(vm);
    require(r != NULL, "expected Java allocation");
    (void)nj_root(vm, r); require(nj_root_mark(vm) == mark + 1u, "checked root");
    return r;
}
static void destroy(nj_vm *vm, host_state *s)
{
    (void)nj_vm_destroy(vm); CHECK(s->live == 0u, "all reserved and host storage freed");
}
static void limits_and_fixed(void)
{
    nj_port p; host_state s; nj_vm *v; unsigned i;
    port_init(&p, &s);
    CHECK(nj_vm_create_with_heap_limit(&p, 64*K, 32*K) == NULL, "limit below initial rejected");
    CHECK(nj_vm_create_with_heap_limit(&p, 0u, 64*K) == NULL, "limit checked against default initial");
    CHECK(nj_vm_create_with_heap_limit(&p, 64*K, UINT32_MAX & ~(NJ_OBJ_ALIGN - 1u)) == NULL, "overflowing reservation rejected");
    CHECK(nj_vm_create_with_heap_limit(&p, 64*K + 1u, 256*K) == NULL, "unaligned auto initial rejected");
    CHECK(nj_vm_create_with_heap_limit(&p, 64*K, 256*K + 1u) == NULL, "unaligned auto limit rejected");
    CHECK(s.live == 0u, "invalid creation leaves no host storage");
    for (i = 0; i < 3; ++i) {
        v = i == 0 ? nj_vm_create(&p, 64*K)
                   : nj_vm_create_with_heap_limit(&p, 64*K, i == 1 ? 0u : 64*K);
        require(v != NULL, "fixed VM");
        CHECK(nj_array_new(v, "B", 80*K) == NULL && nj_has_exception(v), "fixed heap still throws OOM");
        CHECK(v->heap.size == 64*K && s.growth_logs == 0, "disabled/equal limit never grows");
        nj_clear_exception(v); destroy(v, &s);
    }
    /* Inject failure of the VM allocation and of the arena reservation. */
    for (i = 1; i <= 2; ++i) {
        port_init(&p, &s); s.fail_at = i;
        v = nj_vm_create_with_heap_limit(&p, 64*K, 256*K);
        if (v) (void)nj_vm_destroy(v);
        CHECK(s.live == 0u, "creation failure releases complete reservation");
    }
}
static void stable_growth(void)
{
    nj_port p; host_state s; nj_vm *v; nj_ref keep, large, small;
    nj_u1 *base, *data; nj_size before; nj_vm_stats st; nj_value ret;
    unsigned i; nj_u4 gc;
    port_init(&p, &s); v = nj_vm_create_with_heap_limit(&p, 64*K, 512*K);
    require(v != NULL, "growing VM"); base = v->heap.base;
    CHECK(v->heap.size == 64*K && v->heap_capacity == 512*K, "initial logical heap separate from reservation");
    CHECK(bytes_are(base + 128*K, 256u, 0xa5), "unopened reservation was not eagerly zeroed");
    data = nj_host_alloc(v, 51u); require(data != NULL, "ordinary host allocation");
    CHECK(bytes_are(data, 51u, 0), "ordinary host allocation remains fully zeroed"); nj_host_free(v, data);
    keep = rooted(v, nj_array_new(v, "B", 4096u)); data = nj_array_data(keep);
    memset(data, 0x6b, 4096u); gc = v->heap.gc_count;
    before = v->heap.size;
    large = rooted(v, nj_array_new(v, "B", 80*K));
    CHECK(v->heap.size > before && v->heap.size <= 512*K, "failed allocation grows after GC");
    CHECK(v->heap.gc_count == gc + 1u, "growth follows one collection, without an unmarked sweep");
    CHECK(v->heap.base == base && nj_array_data(keep) == data && bytes_are(data, 4096u, 0x6b), "rooted object and interior pointer stay stable");
    CHECK(bytes_are(nj_array_data(large), 80*K, 0), "new array payload starts zero");
    CHECK(bytes_are(base + v->heap.size - 256u, 256u, 0), "unallocated newly opened tail is zero for snapshots");
    CHECK(nj_call_static(v, "java/lang/Runtime", "getRuntime", "()Ljava/lang/Runtime;", NULL, &ret) == NJ_OK, "Runtime instance");
    small = rooted(v, ret.r);
    CHECK(nj_call(v, small, "java/lang/Runtime", "totalMemory", "()J", NULL, &ret) == NJ_OK && (nj_u8)ret.j == v->heap.size, "Runtime reports logical capacity");
    nj_vm_get_stats(v, &st); CHECK(st.heap_size == v->heap.size, "stats report logical capacity");
    CHECK(nj_call(v, small, "java/lang/Runtime", "freeMemory", "()J", NULL, &ret) == NJ_OK && (nj_u8)ret.j == nj_heap_free_bytes(v), "Runtime free memory agrees with allocator");
    for (i = 0; i < 6; ++i) {
        nj_ref a = rooted(v, nj_array_new(v, "B", (i + 5u)*4096u));
        memset(nj_array_data(a), (int)(i + 1u), nj_array_length(a));
    }
    CHECK(v->heap.base == base && bytes_are(data, 4096u, 0x6b), "multiple sizes and collections preserve earlier contents");
    before = v->heap.size; gc = v->heap.gc_count;
    CHECK(nj_array_new(v, "B", 600*K) == NULL && nj_has_exception(v), "above-cap request throws OOM");
    CHECK(v->heap.base == base && v->heap.size == before && v->heap.gc_count == gc + 1u, "unsatisfiable cap request terminates after bounded GC");
    nj_clear_exception(v);
    CHECK(s.growth_logs >= 1u, "successful growth logs once per transition");
    destroy(v, &s);
}
static void fragmented_tail(void)
{
    nj_vm *v = nj_vm_create_with_heap_limit(NULL, 64*K, 96*K);
    nj_ref a[12], result; nj_size old; nj_u1 *base; unsigned n = 0, i;
    require(v != NULL, "fragment VM"); base = v->heap.base;
    /* Keep a chain of blocks, then release alternating entries and the tail.
     * A request larger than the extension alone must reuse the old free tail. */
    for (i = 0; i < 10; ++i) a[i] = rooted(v, nj_array_new(v, "B", 4096u));
    for (i = 0; i < 10; ++i) if ((i & 1u) == 0 || i == 9) v->roots[i] = NULL;
    require(nj_gc_run(v) == NJ_OK, "fragment collection"); old = v->heap.size;
    result = rooted(v, nj_array_new(v, "B", 40*K));
    CHECK(v->heap.size > old && v->heap.base == base, "old tail coalesces with bounded extension");
    CHECK(bytes_are(nj_array_data(result), 40*K, 0), "coalesced allocation is initialized");
    for (i = 1; i < 9; i += 2) { CHECK(nj_heap_owns_object(v, a[i]), "fragment survivors remain valid"); ++n; }
    CHECK(n == 4u, "checked all fragment survivors");
    (void)nj_vm_destroy(v);
}
static const nj_builtin_field holder_fields[] = {
    {"root", "[Ljava/lang/Object;", NJ_ACC_PUBLIC | NJ_ACC_STATIC}
};
static const nj_builtin_class holder = {HOLDER, "java/lang/Object", NJ_ACC_PUBLIC, holder_fields, 1, NULL, 0};
static nj_status no_classes(nj_vm *vm, void *ud, const char *name, nj_u1 **out, nj_u4 *len)
{
    (void)vm; (void)ud; (void)name; *out = NULL; *len = 0u; return NJ_ERR_FORMAT;
}
static nj_class_source source = {no_classes, NULL};
static nj_field *root_field(nj_vm *v)
{
    nj_field *f = nj_field_lookup(v, HOLDER, "root", "[Ljava/lang/Object;", 1);
    require(f != NULL, "holder field"); return f;
}
static nj_ref state(nj_vm *v) { return nj_field_read_ref(v, NULL, root_field(v)); }
static nj_vm *snapshot_vm(nj_size initial, nj_size limit)
{
    nj_vm *v = nj_vm_create_with_heap_limit(NULL, initial, limit);
    require(v != NULL, "snapshot VM"); v->source = &source;
    require(nj_class_create_builtin(v, &holder) != NULL, "holder class");
    require(nj_class_link(v, nj_class_find(v, HOLDER)) == NJ_OK, "holder linked");
    (void)root_field(v); return v;
}
static void seed(nj_vm *v, nj_size bytes)
{
    nj_ref r = nj_array_new(v, "Ljava/lang/Object;", 2u), b;
    require(r != NULL, "state array"); nj_field_write_ref(v, NULL, root_field(v), r);
    b = nj_array_new(v, "B", bytes); require(b != NULL, "state bytes");
    state(v)->u.refs[0] = b; memset(nj_array_data(b), 0x37, bytes);
}
static void roundtrip(nj_size initial, nj_size limit, nj_size receiver_initial,
                      nj_size receiver_limit, nj_size bytes, int grow_after)
{
    nj_vm *a = snapshot_vm(initial, limit), *b; FILE *f; nj_size saved;
    seed(a, bytes); saved = a->heap.size;
    require(a->nroots == 0u && nj_vm_can_save(a), "save at safe point");
    f = tmpfile(); require(f != NULL, "snapshot file");
    CHECK(nj_vm_save(a, f) == NJ_OK, "growth policy does not prevent snapshot");
    rewind(f); b = snapshot_vm(receiver_initial, receiver_limit);
    CHECK(nj_vm_load(b, f) == NJ_OK, "fixed/auto snapshot cross-loads");
    require(state(b) && state(b)->u.refs[0], "restored object graph");
    CHECK(b->heap.size == saved && b->heap_limit == receiver_limit, "restore keeps logical size and receiver policy");
    CHECK(bytes_are(nj_array_data(state(b)->u.refs[0]), bytes, 0x37), "snapshot graph and payload restored");
    if (grow_after) {
        nj_u1 *base = b->heap.base; nj_ref extra;
        extra = nj_array_new(b, "B", saved + 4096u); require(extra != NULL, "post-load larger array");
        state(b)->u.refs[1] = extra;
        CHECK(b->heap.base == base && b->heap.size > saved, "restored auto VM grows in place again");
        CHECK(bytes_are(nj_array_data(state(b)->u.refs[0]), bytes, 0x37), "restore then grow preserves graph");
    }
    fclose(f); (void)nj_vm_destroy(a); (void)nj_vm_destroy(b);
}
static void above_default_snapshot(void)
{
    const nj_size initial = (nj_size)NJ_MAX_HEAP, cap = 2u * (nj_size)NJ_MAX_HEAP;
    const nj_size bytes = (nj_size)NJ_MAX_HEAP + 4096u;
    nj_vm *a = snapshot_vm(initial, cap), *b; nj_ref r, big;
    nj_u1 *base; FILE *f = tmpfile();
    require(f != NULL, "above-default snapshot stream");
    seed(a, 4096u);
    /* int[] keeps element count below the VM's 0x00ffffff array limit while
     * requiring one contiguous allocation larger than the default arena. */
    big = nj_array_new(a, "I", bytes / 4u); require(big != NULL, "above-default int array");
    state(a)->u.refs[0] = big; memset(nj_array_data(big), 0x37, bytes);
    CHECK(a->heap.size > initial && a->heap.size <= cap, "real heap grows past NJ_MAX_HEAP");
    require(nj_vm_save(a, f) == NJ_OK, "save expanded above-default arena once");
    (void)nj_vm_destroy(a);
    b = snapshot_vm(initial, initial); seed(b, 4096u); base = b->heap.base;
    rewind(f);
    CHECK(nj_vm_load(b, f) == NJ_ERR_FORMAT, "same above-default snapshot rejects at lower explicit cap");
    CHECK(b->heap.base == base && bytes_are(nj_array_data(state(b)->u.refs[0]), 4096u, 0x37), "cap refusal preserves receiving graph");
    r = nj_array_new(b, "I", 128u); CHECK(r != NULL && !nj_has_exception(b), "cap refusal leaves receiver usable");
    (void)nj_vm_destroy(b);
    b = snapshot_vm(initial, cap); rewind(f);
    CHECK(nj_vm_load(b, f) == NJ_OK, "above-default snapshot loads into smaller active heap with sufficient authorized cap");
    require(state(b) && state(b)->u.refs[0], "above-default restored graph");
    CHECK(b->heap.size > initial && b->heap_capacity == cap && b->heap_limit == cap, "above-default restore preserves receiver policy");
    CHECK(bytes_are(nj_array_data(state(b)->u.refs[0]), bytes, 0x37), "above-default payload restored intact");
    fclose(f); (void)nj_vm_destroy(b);
}
static void refused_snapshots(void)
{
    nj_vm *a = snapshot_vm(64*K, 0u), *b = snapshot_vm(64*K, 512*K);
    nj_u1 *base; FILE *f = tmpfile();
    require(f != NULL, "late rejection stream");
    seed(a, 4096u); a->screen_w = 128; a->screen_h = 116;
    require(nj_vm_save(a, f) == NJ_OK, "late rejection saves");
    b->screen_w = 129; b->screen_h = 116; rewind(f);
    CHECK(nj_vm_load(b, f) == NJ_ERR_FORMAT, "geometry mismatch rejects after arena installation");
    CHECK(b->heap.base != NULL && b->heap_capacity == 512*K && b->heap_limit == 512*K, "late rejection rebuild retains auto reservation and policy");
    base = b->heap.base; seed(b, 80*K);
    CHECK(b->heap.base == base && b->heap.size > 64*K, "fresh graph grows after late load failure");
    fclose(f); (void)nj_vm_destroy(a); (void)nj_vm_destroy(b);
    a = snapshot_vm(64*K, 512*K); b = snapshot_vm(64*K, 96*K);
    seed(a, 80*K); seed(b, 4096u); base = b->heap.base;
    f = tmpfile(); require(f != NULL, "cap rejection stream");
    require(nj_vm_save(a, f) == NJ_OK, "expanded snapshot saves"); rewind(f);
    CHECK(nj_vm_load(b, f) == NJ_ERR_FORMAT, "snapshot exceeding receiver cap rejects");
    CHECK(b->heap.base == base && b->heap.size == 64*K && bytes_are(nj_array_data(state(b)->u.refs[0]), 4096u, 0x37), "over-cap header leaves receiving graph untouched");
    fclose(f); (void)nj_vm_destroy(a); (void)nj_vm_destroy(b);
}
int main(void)
{
    limits_and_fixed(); stable_growth(); fragmented_tail();
    roundtrip(64*K, 0, 64*K, 512*K, 4096u, 1);
    roundtrip(64*K, 512*K, 256*K, 0, 80*K, 0);
    roundtrip(64*K, 512*K, 64*K, 512*K, 80*K, 1);
    refused_snapshots(); above_default_snapshot();
    printf("heap growth: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
