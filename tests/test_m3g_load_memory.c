/* Small M3G models must load without a maximum-size temporary object table. */
#include "nj2me/nj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef union { nj_size size; long double alignment; void *pointer; } header;
typedef struct { nj_size limit, live, peak_request; } memory;
static unsigned failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; \
} } while (0)

static void *allocate(void *ud, nj_size size)
{
    memory *s = ud;
    header *h;
    if (size > s->peak_request) s->peak_request = size;
    if (s->limit && size > s->limit) return NULL;
    h = malloc(sizeof(*h) + size);
    if (!h) return NULL;
    h->size = size; s->live += size;
    return h + 1;
}

static void release(void *ud, void *ptr)
{
    memory *s = ud;
    header *h;
    if (!ptr) return;
    h = (header *)ptr - 1;
    s->live -= h->size;
    free(h);
}

static void put32(nj_u1 *p, nj_u4 n)
{
    unsigned i;
    for (i = 0; i < 4; i++) p[i] = (nj_u1)(n >> (i * 8));
}

static void load(nj_vm *vm, unsigned count, int valid)
{
    static const nj_u1 ident[12] = {
        0xab, 'J','S','R','1','8','4', 0xbb, 13, 10, 26, 10
    };
    nj_u4 length = 55u + 5u * count, i;
    nj_ref bytes = nj_array_new(vm, "B", length);
    nj_u1 *p;
    nj_value a[2], r;
    nj_status st;
    CHECK(bytes != NULL);
    if (!bytes) return;
    p = nj_array_data(bytes);
    memset(p, 0, length);
    memcpy(p, ident, sizeof(ident));
    put32(p + 13, 30); put32(p + 17, 17); put32(p + 22, 12);
    p[26] = 1;
    put32(p + 29, length); put32(p + 33, length);
    put32(p + 43, 13u + 5u * count); put32(p + 47, 5u * count);
    for (i = 0; i < count; i++) p[51u + 5u * i] = 9; /* Group */
    a[0].r = bytes; a[1].i = 0; r.r = NULL;
    st = nj_call_static(vm, "javax/microedition/m3g/Loader", "load",
        "([BI)[Ljavax/microedition/m3g/Object3D;", a, &r);
    if (valid) {
        CHECK(st == NJ_OK && r.r != NULL);
        if (st == NJ_OK && r.r) {
            CHECK(nj_array_length(r.r) == count);
            for (i = 0; i < count; i++) {
                nj_ref group = nj_array_get_ref(r.r, i);
                CHECK(group && !strcmp(nj_ref_class_name(group),
                                      "javax/microedition/m3g/Group"));
            }
        }
        CHECK(nj_take_exception(vm) == NULL);
    } else {
        nj_ref exception = nj_take_exception(vm);
        CHECK(st != NJ_OK && exception != NULL);
        if (exception) CHECK(!strcmp(nj_ref_class_name(exception), "java/io/IOException"));
    }
}

/* Independent binary fixture: a skin owns its vertex buffer, skeleton and
 * a bone link; an unrelated Group must remain a separate root. */
static void skin_roots(nj_vm *vm)
{
    nj_u1 buf[512] = {0};
    static const nj_u1 ident[12] = {
        0xab, 'J','S','R','1','8','4', 0xbb, 13, 10, 26, 10
    };
    const unsigned types[] = {21, 11, 3, 9, 9, 16, 9};
    const unsigned sizes[] = {48, 25, 33, 26, 26, 62, 26};
    unsigned i, at = 51, roots = 0, skins = 0, groups = 0;
    nj_ref bytes;
    nj_value a[2], r;
    memcpy(buf, ident, 12);
    put32(buf + 13, 30); put32(buf + 17, 17); put32(buf + 22, 12);
    buf[26] = 1;
    for (i = 0; i < 7; i++) {
        nj_u1 *d = buf + at + 5;
        buf[at] = (nj_u1)types[i]; put32(buf + at + 1, sizes[i]);
        if (i == 1) { put32(d + 17, 1); put32(d + 21, 3); }
        if (i == 5) {
            put32(d + 16, 0xffffffffu);
            put32(d + 22, 2); /* VertexBuffer, file indices include header */
            put32(d + 26, 1); put32(d + 30, 3); put32(d + 34, 4);
            put32(d + 38, 5); /* skeleton */
            put32(d + 42, 1); put32(d + 46, 6); /* bone link */
            put32(d + 54, 3); put32(d + 58, 1);
        }
        at += 5 + sizes[i];
    }
    put32(buf + 29, at + 4); put32(buf + 33, at + 4);
    put32(buf + 43, at + 4 - 42); put32(buf + 47, at - 51);
    bytes = nj_array_new(vm, "B", at + 4);
    CHECK(bytes != NULL); if (!bytes) return;
    memcpy(nj_array_data(bytes), buf, at + 4);
    a[0].r = bytes; a[1].i = 0; r.r = NULL;
    CHECK(nj_call_static(vm, "javax/microedition/m3g/Loader", "load",
        "([BI)[Ljavax/microedition/m3g/Object3D;", a, &r) == NJ_OK);
    CHECK(!nj_has_exception(vm));
    if (r.r) {
        roots = nj_array_length(r.r);
        for (i = 0; i < roots; i++) {
            nj_ref o = nj_array_get_ref(r.r, i);
            const char *name = o ? nj_ref_class_name(o) : "";
            skins += !strcmp(name, "javax/microedition/m3g/SkinnedMesh");
            groups += !strcmp(name, "javax/microedition/m3g/Group");
        }
    }
    CHECK(roots == 2 && skins == 1 && groups == 1);
}

static void texture_units(nj_vm *vm)
{
    nj_value props, key, value, count;
    CHECK(nj_call_static(vm, "javax/microedition/m3g/Graphics3D",
        "getProperties", "()Ljava/util/Hashtable;", NULL, &props) == NJ_OK);
    key.r = nj_string_new(vm, "numTextureUnits");
    CHECK(nj_call(vm, props.r, "java/util/Hashtable", "get",
        "(Ljava/lang/Object;)Ljava/lang/Object;", &key, &value) == NJ_OK);
    CHECK(value.r != NULL);
    if (value.r) {
        CHECK(!strcmp(nj_ref_class_name(value.r), "java/lang/Integer"));
        CHECK(nj_call(vm, value.r, "java/lang/Integer", "intValue", "()I",
            NULL, &count) == NJ_OK);
        CHECK(count.i == 1);
    }
}

int main(void)
{
    memory s = {0};
    nj_port port = { .ud = &s, .user = &s, .alloc = allocate, .free = release };
    nj_vm *vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    if (!vm) return 1;
    s.limit = 128u * 1024u; s.peak_request = 0;
    load(vm, 1, 1);
    load(vm, 256, 1);
    load(vm, 0, 0);
    load(vm, 4097, 0);
    skin_roots(vm);
    texture_units(vm);
    CHECK(s.peak_request <= s.limit);
    nj_vm_destroy(vm);
    CHECK(s.live == 0);
    printf("M3G bounded loading: %u failures\n", failures);
    return failures ? 1 : 0;
}
