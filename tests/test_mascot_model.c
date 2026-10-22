/* Independently constructed MBAC fixtures; no game assets are embedded.
 * Standalone: cc -std=c99 -Wall -Wextra -Werror -Isrc/midp
 *   tests/test_mascot_model.c src/midp/nj_mascot_model.c -o test_mascot_model
 */
#include "nj_mascot_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)
#define ARRAY(m, type, name) ((const type *)((const unsigned char *)(m) + (m)->name##_offset))

typedef struct fixture {
    unsigned char data[4096];
    size_t bit;
    size_t vertex_chunk, normal_axis, color_width, color_index;
    size_t texture_width, texture_index, material, bone;
} fixture;

static unsigned checks, failures;

/* Write one bit at a time, independently of the decoder's cached reader. */
static void put(fixture *f, uint32_t n, unsigned width)
{
    unsigned i;
    for (i = 0; i < width; ++i, ++f->bit) {
        size_t index = f->bit / 8;
        unsigned bit = (unsigned)(f->bit % 8);
        if (index >= sizeof(f->data)) abort();
        if ((n >> i) & 1) f->data[index] |= (unsigned char)(1u << bit);
        else f->data[index] &= (unsigned char)~(1u << bit);
    }
}

static void patch(fixture *f, size_t at, uint32_t n, unsigned width)
{
    size_t end = f->bit;
    f->bit = at;
    put(f, n, width);
    f->bit = end;
}

static void align_fixture(fixture *f)
{
    while (f->bit % 8) put(f, 0, 1);
}

static size_t size(const fixture *f) { return (f->bit + 7) / 8; }

static void start(fixture *f, unsigned version, unsigned vf, unsigned nf, unsigned pf,
                  unsigned nv, unsigned t3, unsigned t4, unsigned nb,
                  unsigned c3, unsigned c4, unsigned nt, unsigned np, unsigned nc)
{
    memset(f, 0, sizeof(*f));
    put(f, 'M', 8); put(f, 'B', 8); put(f, version, 8); put(f, 0, 8);
    if (version >= 4) {
        put(f, vf, 8); put(f, nf, 8); put(f, pf, 8); put(f, 1, 8);
    }
    put(f, nv, 16); put(f, t3, 16); put(f, t4, 16); put(f, nb, 16);
    if (pf == 3) {
        put(f, c3, 16); put(f, c4, 16); put(f, nt, 16); put(f, np, 16); put(f, nc, 16);
    }
}

static void range(fixture *f, unsigned tri, unsigned quad)
{
    put(f, tri, 16); put(f, quad, 16);
}

static void bone(fixture *f, unsigned count, int parent, int tx)
{
    unsigned i;
    put(f, count, 16); put(f, (uint32_t)parent, 16);
    for (i = 0; i < 12; ++i)
        put(f, i == 0 || i == 5 || i == 10 ? 4096 : i == 3 ? (uint32_t)tx : 0, 16);
}

static void legacy(fixture *f, unsigned version)
{
    unsigned i;
    start(f, version, 1, 0, 1, 4, 1, 1, 2, 0, 0, 1, 1, 0);
    for (i = 0; i < 12; ++i) put(f, (uint32_t)((int)i * 7 - 40), 16);
    put(f, 6, 16); /* Triangle: double face + colorkey. */
    for (i = 0; i < 3; ++i) put(f, i, 16);
    for (i = 0; i < 6; ++i) put(f, i * 20, 8);
    put(f, 5, 16); /* Quad discriminator + double face. */
    for (i = 0; i < 4; ++i) put(f, i, 16);
    for (i = 0; i < 8; ++i) put(f, 255 - i, 8);
    f->bone = f->bit;
    bone(f, 2, -1, -7); bone(f, 2, 0, 9);
}

static void packed(fixture *f)
{
    unsigned group, i, j;
    const unsigned widths[4] = {8, 10, 13, 16};
    const int values[4][3] = {{-128, 127, -1}, {-512, 511, 3}, {-4096, 4095, -3}, {-32768, 32767, 40}};
    start(f, 5, 2, 2, 3, 8, 2, 1, 2, 1, 1, 2, 2, 2);
    range(f, 1, 0); range(f, 1, 0); range(f, 0, 0);
    range(f, 0, 1); range(f, 0, 1); range(f, 1, 0);
    f->vertex_chunk = f->bit;
    for (group = 0; group < 4; ++group) {
        put(f, (group << 6) | 1, 8);
        for (i = 0; i < 2; ++i)
            for (j = 0; j < 3; ++j) put(f, (uint32_t)values[group][j], widths[group]);
    }
    align_fixture(f);
    for (i = 0; i < 6; ++i) {
        put(f, 64, 7);
        if (i == 0) f->normal_axis = f->bit;
        put(f, i, 3);
    }
    put(f, 3, 7); put(f, (uint32_t)-4, 7); put(f, 0, 1);
    put(f, (uint32_t)-63, 7); put(f, (uint32_t)-64, 7); put(f, 1, 1);
    align_fixture(f);
    put(f, 9, 8); put(f, 3, 8);
    f->color_width = f->bit;
    put(f, 4, 8); put(f, 2, 8); put(f, 0, 8);
    put(f, 15, 4); put(f, 0, 4); put(f, 8, 4);
    put(f, 0, 4); put(f, 15, 4); put(f, 0, 4);
    put(f, 0x320 >> 1, 9);
    for (i = 0; i < 3; ++i) put(f, i, 3);
    f->color_index = f->bit;
    put(f, 1, 2);
    put(f, 0x16 >> 1, 9);
    for (i = 0; i < 4; ++i) put(f, i + 2, 3);
    put(f, 0, 2);
    /* No alignment between colored and textured sections. */
    f->texture_width = f->bit;
    put(f, 10, 8); put(f, 4, 8); put(f, 8, 8); put(f, 0, 8);
    for (group = 0; group < 3; ++group) {
        unsigned n = group == 2 ? 4 : 3;
        if (group == 0) f->material = f->bit;
        put(f, group == 0 ? 0x335 : 0x205, 10);
        for (i = 0; i < n; ++i) {
            if (group == 0 && i == 0) f->texture_index = f->bit;
            put(f, i + group, 4);
        }
        for (i = 0; i < n; ++i) { put(f, i * 50, 8); put(f, 255 - i, 8); }
    }
    align_fixture(f);
    f->bone = f->bit;
    bone(f, 3, -1, -123); bone(f, 5, 0, 456);
}

static void v4(fixture *f)
{
    unsigned i;
    start(f, 4, 1, 1, 2, 3, 1, 0, 1, 0, 0, 1, 1, 0);
    for (i = 0; i < 9; ++i) put(f, i * 3, 16);
    for (i = 0; i < 3; ++i) { put(f, 3, 16); put(f, 4, 16); put(f, 0, 16); }
    put(f, 7, 8); put(f, 2, 8);
    put(f, 0x31, 7);
    for (i = 0; i < 3; ++i) put(f, i, 2);
    for (i = 0; i < 6; ++i) put(f, 127 - i, 7);
    align_fixture(f);
    f->bone = f->bit;
    bone(f, 3, -1, 0);
}

static nj_mascot_model *decode(const fixture *f)
{
    size_t required = 0, filled = 0, i;
    unsigned char *out;
    nj_mascot_model_result result = nj_mascot_model_decode(f->data, size(f), NULL, 0, &required);
    CHECK(result == NJ_MASCOT_MODEL_OK);
    if (result != NJ_MASCOT_MODEL_OK) return NULL;
    CHECK(required >= sizeof(nj_mascot_model));
    out = (unsigned char *)malloc(required + 16);
    if (!out) abort();
    memset(out, 0xa5, required + 16);
    CHECK(nj_mascot_model_decode(f->data, size(f), out, required - 1, &filled) == NJ_MASCOT_MODEL_STORAGE);
    CHECK(filled == required);
    for (i = 0; i < required + 16; ++i) if (out[i] != 0xa5) break;
    CHECK(i == required + 16);
    CHECK(nj_mascot_model_decode(f->data, size(f), out, required, &filled) == NJ_MASCOT_MODEL_OK);
    CHECK(filled == required);
    for (i = required; i < required + 16; ++i) CHECK(out[i] == 0xa5);
    CHECK(((nj_mascot_model *)out)->total_bytes == required);
    CHECK(((nj_mascot_model *)out)->source_bytes == size(f));
    return (nj_mascot_model *)out;
}

static void reject(const fixture *f, nj_mascot_model_result expected)
{
    size_t bytes = 123;
    union { nj_mascot_model align; unsigned char data[8192]; } out;
    CHECK(nj_mascot_model_decode(f->data, size(f), NULL, 0, &bytes) == expected);
    CHECK(bytes == 0);
    bytes = 123;
    CHECK(nj_mascot_model_decode(f->data, size(f), out.data, sizeof(out.data), &bytes) == expected);
    CHECK(bytes == 0);
}

static void truncations(const fixture *f)
{
    size_t n, bytes;
    union { nj_mascot_model align; unsigned char data[8192]; } out;
    for (n = 0; n < size(f); ++n) {
        bytes = 123;
        CHECK(nj_mascot_model_decode(f->data, n, NULL, 0, &bytes) != NJ_MASCOT_MODEL_OK);
        CHECK(bytes == 0);
        CHECK(nj_mascot_model_decode(f->data, n, out.data, sizeof(out.data), &bytes) != NJ_MASCOT_MODEL_OK);
        CHECK(bytes == 0);
    }
}

static void test_legacy(void)
{
    fixture f;
    unsigned version;
    for (version = 2; version <= 3; ++version) {
        nj_mascot_model *m;
        const uint16_t *t;
        const nj_mascot_bone *b;
        legacy(&f, version);
        m = decode(&f);
        if (!m) continue;
        CHECK(m->version == version && !m->normals_offset);
        CHECK(ARRAY(m, int16_t, vertices)[0] == -40);
        t = ARRAY(m, uint16_t, poly_t3);
        CHECK(t[0] == 0x11 && t[3] == 2 && t[6] == 0x5064);
        t = ARRAY(m, uint16_t, poly_t4);
        CHECK(t[0] == 0x10 && t[4] == 3 && t[8] == 0xf9f8);
        b = ARRAY(m, nj_mascot_bone, bones);
        CHECK(b[0].parent == -1 && b[0].matrix[3] == -7);
        CHECK(b[1].parent == 0 && b[1].start_vertex == 2 && b[1].matrix[3] == 9);
        CHECK(ARRAY(m, nj_mascot_pattern_range, patterns)[1].num_quad == 1);
        free(m);
        truncations(&f);
    }
    legacy(&f, 2);
    patch(&f, 12 * 8 + 12 * 16, 1, 16); /* Quad bit on a triangle. */
    reject(&f, NJ_MASCOT_MODEL_INVALID);
}

static void test_packed(void)
{
    fixture f;
    nj_mascot_model *m;
    const int16_t *v, *n;
    const uint16_t *p;
    const nj_mascot_pattern_range *ranges;
    unsigned i;
    packed(&f);
    CHECK(f.texture_width % 8 != 0);
    m = decode(&f);
    if (!m) return;
    CHECK(m->num_vertices == 8 && m->num_poly_c3 == 1 && m->num_poly_c4 == 1);
    CHECK(m->num_textures == 2 && m->num_patterns == 2 && m->num_colors == 2);
    v = ARRAY(m, int16_t, vertices);
    CHECK(v[0] == -128 && v[1] == 127 && v[2] == -1);
    CHECK(v[6] == -512 && v[7] == 511 && v[12] == -4096 && v[13] == 4095);
    CHECK(v[18] == -32768 && v[19] == 32767);
    n = ARRAY(m, int16_t, normals);
    for (i = 0; i < 6; ++i) {
        unsigned axis;
        for (axis = 0; axis < 3; ++axis)
            CHECK(n[i * 3 + axis] == (axis == i % 3 ? (i < 3 ? 4096 : -4096) : 0));
    }
    CHECK(n[18] == 192 && n[19] == -256 && n[20] == 4083);
    CHECK(n[21] == -4032 && n[22] == -4096 && n[23] == 0);
    CHECK(ARRAY(m, uint32_t, colors)[0] == UINT32_C(0xffff0088));
    CHECK(ARRAY(m, uint32_t, colors)[1] == UINT32_C(0xff00ff00));
    p = ARRAY(m, uint16_t, poly_c3);
    CHECK(p[0] == 0x320 && p[1] == 1 && p[2] == 0 && p[4] == 2);
    p = ARRAY(m, uint16_t, poly_c4);
    CHECK(p[0] == 0x16 && p[1] == 0 && p[5] == 5);
    p = ARRAY(m, uint16_t, poly_t3);
    CHECK(p[0] == 0x335 && p[4] == 0x00ff && p[6] == 0x64fd && p[7] == 0x205);
    ranges = ARRAY(m, nj_mascot_pattern_range, patterns);
    CHECK(ranges[3].first_tri == 1 && ranges[3].num_quad == 1);
    CHECK(ranges[4].first_tri == 1 && ranges[4].num_quad == 1);
    CHECK(ranges[5].first_tri == 1 && ranges[5].num_tri == 1 && ranges[5].first_quad == 1);
    CHECK(m->bones_offset % 4 == 0 && m->patterns_offset % 4 == 0 && m->colors_offset % 4 == 0);
    CHECK(sizeof(nj_mascot_bone) == 56 && sizeof(nj_mascot_pattern_range) == 16);
    /* Copy the blob, then destroy its original allocation and source. */
    {
        size_t bytes = m->total_bytes;
        nj_mascot_model *copy = (nj_mascot_model *)malloc(bytes);
        if (!copy) abort();
        memcpy(copy, m, bytes);
        free(m);
        memset(f.data, 0, sizeof(f.data));
        CHECK(ARRAY(copy, nj_mascot_bone, bones)[1].matrix[3] == 456);
        CHECK(ARRAY(copy, int16_t, vertices)[18] == -32768);
        free(copy);
    }
    packed(&f);
    truncations(&f);
    /* The decoder reports structured bytes and tolerates vendor suffixes. */
    {
        size_t bytes, consumed = size(&f);
        unsigned char *out;
        for (i = 0; i < 20; ++i) put(&f, 0xa0 + i, 8);
        CHECK(nj_mascot_model_decode(f.data, size(&f), NULL, 0, &bytes) == NJ_MASCOT_MODEL_OK);
        out = (unsigned char *)malloc(bytes);
        if (!out) abort();
        CHECK(nj_mascot_model_decode(f.data, size(&f), out, bytes, &bytes) == NJ_MASCOT_MODEL_OK);
        CHECK(((nj_mascot_model *)out)->source_bytes == consumed);
        free(out);
    }
}

static void test_v4(void)
{
    fixture f;
    nj_mascot_model *m;
    const int16_t *n;
    v4(&f);
    m = decode(&f);
    if (!m) return;
    n = ARRAY(m, int16_t, normals);
    CHECK(n[0] == 2457 && n[1] == 3276 && n[2] == 0);
    CHECK(ARRAY(m, int16_t, vertices)[1] == 3); /* Reference's accidental overwrite fails. */
    CHECK(ARRAY(m, uint16_t, poly_t3)[0] == (0x31 | NJ_MASCOT_MAT_FLAT_NORMAL));
    CHECK((m->materials_and & NJ_MASCOT_MAT_FLAT_NORMAL) != 0);
    CHECK(ARRAY(m, uint16_t, poly_t3)[4] == 0x7f7e);
    free(m);
    truncations(&f);
}

static void test_invalid(void)
{
    fixture f;
    size_t bytes;
    union { nj_mascot_model align; unsigned char data[8192]; } out;
#define BAD(at, value, width, error) do { packed(&f); patch(&f, (at), (value), (width)); reject(&f, (error)); } while (0)
    BAD(0, 'X', 8, NJ_MASCOT_MODEL_INVALID);
    BAD(2 * 8, 6, 8, NJ_MASCOT_MODEL_UNSUPPORTED);
    BAD(3 * 8, 1, 8, NJ_MASCOT_MODEL_INVALID);
    BAD(4 * 8, 3, 8, NJ_MASCOT_MODEL_UNSUPPORTED);
    BAD(5 * 8, 3, 8, NJ_MASCOT_MODEL_UNSUPPORTED);
    BAD(6 * 8, 4, 8, NJ_MASCOT_MODEL_UNSUPPORTED);
    BAD(7 * 8, 2, 8, NJ_MASCOT_MODEL_UNSUPPORTED);
    BAD(8 * 8, 21846, 16, NJ_MASCOT_MODEL_INVALID);
    BAD(20 * 8, 17, 16, NJ_MASCOT_MODEL_INVALID);
    BAD(22 * 8, 34, 16, NJ_MASCOT_MODEL_INVALID);
    BAD(22 * 8, 0, 16, NJ_MASCOT_MODEL_INVALID);
    BAD(24 * 8, 257, 16, NJ_MASCOT_MODEL_INVALID);
    BAD(24 * 8, 0, 16, NJ_MASCOT_MODEL_INVALID);
    BAD(26 * 8, 2, 16, NJ_MASCOT_MODEL_INVALID); /* Pattern overrun. */
    BAD(26 * 8, 0, 16, NJ_MASCOT_MODEL_INVALID); /* Pattern undercoverage. */
    BAD(f.vertex_chunk, 63, 8, NJ_MASCOT_MODEL_INVALID);
    BAD(f.normal_axis, 6, 3, NJ_MASCOT_MODEL_INVALID);
    BAD(f.color_width, 0, 8, NJ_MASCOT_MODEL_INVALID);
    BAD(f.color_width, 9, 8, NJ_MASCOT_MODEL_INVALID);
    BAD(f.color_index, 2, 2, NJ_MASCOT_MODEL_INVALID);
    BAD(f.texture_width, 255, 8, NJ_MASCOT_MODEL_INVALID);
    BAD(f.texture_width + 8, 17, 8, NJ_MASCOT_MODEL_INVALID);
    BAD(f.texture_width + 16, 9, 8, NJ_MASCOT_MODEL_INVALID);
    BAD(f.texture_index, 8, 4, NJ_MASCOT_MODEL_INVALID);
    BAD(f.material, 8, 10, NJ_MASCOT_MODEL_INVALID);
    BAD(f.bone, 9, 16, NJ_MASCOT_MODEL_INVALID);
    BAD(f.bone + 16, 0, 16, NJ_MASCOT_MODEL_INVALID); /* Self-parent. */
    BAD(f.bone + 16, (uint32_t)-2, 16, NJ_MASCOT_MODEL_INVALID);
    BAD(f.bone + 28 * 8, 4, 16, NJ_MASCOT_MODEL_INVALID); /* Bone undercoverage. */
    BAD(f.bone + 28 * 8 + 16, 2, 16, NJ_MASCOT_MODEL_INVALID); /* Forward parent. */
#undef BAD
    packed(&f);
    CHECK(nj_mascot_model_decode(NULL, 0, NULL, 0, &bytes) == NJ_MASCOT_MODEL_ARGUMENT);
    CHECK(nj_mascot_model_decode(f.data, size(&f), NULL, 1, &bytes) == NJ_MASCOT_MODEL_ARGUMENT);
    CHECK(nj_mascot_model_decode(f.data, size(&f), NULL, 0, NULL) == NJ_MASCOT_MODEL_ARGUMENT);
    CHECK(nj_mascot_model_decode(f.data, size(&f), out.data + 1, sizeof(out.data) - 1, &bytes) == NJ_MASCOT_MODEL_ARGUMENT);
    CHECK(nj_mascot_model_decode(f.data, size(&f), f.data, sizeof(f.data), &bytes) == NJ_MASCOT_MODEL_ARGUMENT);
    /* Huge declared polygon counts in a small buffer must not allocate/read past it. */
    start(&f, 5, 2, 2, 3, 21845, 65535, 65535, 65535, 65535, 65535, 16, 33, 256);
    reject(&f, NJ_MASCOT_MODEL_TRUNCATED);
}

static void test_empty_and_zero_widths(void)
{
    fixture f;
    nj_mascot_model *m;
    unsigned i;
    start(&f, 5, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1, 0);
    range(&f, 0, 0);
    m = decode(&f);
    if (m) { CHECK(m->vertices_offset == 0 && m->bones_offset == 0); free(m); }
    start(&f, 5, 1, 0, 3, 1, 1, 0, 1, 0, 0, 1, 1, 0);
    range(&f, 0, 0); range(&f, 1, 0);
    for (i = 0; i < 3; ++i) put(&f, 0, 16);
    for (i = 0; i < 4; ++i) put(&f, 0, 8); /* Constant-zero material, indices, UV. */
    bone(&f, 1, -1, 0);
    m = decode(&f);
    if (m) { CHECK(ARRAY(m, uint16_t, poly_t3)[6] == 0); free(m); }
}

static void test_mutations(void)
{
    fixture original, changed;
    unsigned iteration;
    uint32_t random = 0x42c0ffee;
    packed(&original);
    for (iteration = 0; iteration < 2000; ++iteration) {
        size_t bytes = 0;
        nj_mascot_model_result result;
        changed = original;
        random = random * 1664525u + 1013904223u;
        changed.data[random % size(&changed)] ^= (unsigned char)(1u << ((random >> 24) & 7));
        result = nj_mascot_model_decode(changed.data, size(&changed), NULL, 0, &bytes);
        if (result == NJ_MASCOT_MODEL_OK) {
            void *out = malloc(bytes);
            if (!out) abort();
            CHECK(nj_mascot_model_decode(changed.data, size(&changed), out, bytes, &bytes) == result);
            free(out);
        } else CHECK(bytes == 0);
    }
}

int main(void)
{
    test_legacy();
    test_packed();
    test_v4();
    test_invalid();
    test_empty_and_zero_widths();
    test_mutations();
    printf("Mascot model: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
