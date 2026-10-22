/* Synthetic MTRA fixtures only: no proprietary model/animation data. */
#include "nj_mascot_anim.h"
/* These standalone checks remain active in release builds. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const int32_t identity[12] = {4096,0,0,0, 0,4096,0,0, 0,0,4096,0};

typedef struct { uint8_t data[2048]; size_t n; } fixture;

static void u8(fixture *f, unsigned v) { assert(f->n < sizeof f->data); f->data[f->n++] = (uint8_t)v; }
static void u16(fixture *f, int v) { u8(f, (unsigned)v); u8(f, (unsigned)v >> 8); }
static void u32(fixture *f, uint32_t v) { u16(f, (int)(v & 65535)); u16(f, (int)(v >> 16)); }
static void header(fixture *f, unsigned version, int actions, int bones)
{
    unsigned i;
    memset(f, 0, sizeof *f);
    u8(f, 'M'); u8(f, 'T'); u8(f, version); u8(f, 0);
    u16(f, actions); u16(f, bones);
    for (i = 0; i < 10; i++) u16(f, 0); /* Reference ignores allocation hints. */
}
static void vector(fixture *f, int time, int x, int y, int z)
{
    u16(f, time); u16(f, x); u16(f, y); u16(f, z);
}
static void constant_direction(fixture *f, int x, int y, int z)
{
    u16(f, 1); vector(f, 0, x, y, z);
}
static void translate(fixture *f)
{
    u16(f, 2); vector(f, 0, 0, 0, 0); vector(f, 4, 80, -40, 20);
}
static void rolling(fixture *f)
{
    u16(f, 2); u16(f, 0); u16(f, 0); u16(f, 4); u16(f, 1024);
}
static void make_all(fixture *f)
{
    unsigned i;
    header(f, 5, 2, 7); u16(f, 10);
    u8(f, 0);
    for (i = 0; i < 12; i++) u16(f, i == 3 ? 10 : i == 7 ? -20 : i == 11 ? 30 : identity[i]);
    u8(f, 1);
    u8(f, 2); translate(f);
    u16(f, 2); vector(f, 0, 4096, 4096, 4096); vector(f, 4, 8192, 2048, 4096);
    u16(f, 2); vector(f, 0, 0, 0, 4096); vector(f, 4, 4096, 0, 0); rolling(f);
    u8(f, 3); u16(f, 7); u16(f, 8); u16(f, 9);
    constant_direction(f, 0, 0, 4096); u16(f, 1024);
    u8(f, 4); constant_direction(f, 0, 0, 4096); rolling(f);
    u8(f, 5); constant_direction(f, 4096, 0, 0);
    u8(f, 6); translate(f); constant_direction(f, 0, 0, 4096);
    u16(f, 1); u16(f, 0); u16(f, 0);
    u16(f, 4);
    u16(f, 0); u32(f, UINT32_C(0x80000001));
    u16(f, 2); u32(f, 17); u16(f, 2); u32(f, 19);
    u16(f, 5); u32(f, UINT32_MAX);
    u16(f, 65535);
    for (i = 0; i < 7; i++) u8(f, 1);
    u16(f, 0);
}
static size_t fill(const fixture *f, uint8_t *blob, size_t capacity)
{
    size_t bytes = 0;
    assert(nj_mascot_anim_query(f->data, f->n, &bytes) == 0);
    assert(bytes == 24 + 8 * (size_t)f->data[4] +
           4 * (size_t)f->data[4] * f->data[6] + f->n);
    assert(bytes <= capacity);
    assert(nj_mascot_anim_fill(f->data, f->n, blob, capacity) == 0);
    return bytes;
}
static void pose(const uint8_t *blob, size_t size, int bone, int32_t frame,
                 const int32_t *base, const int32_t *expected)
{
    int32_t out[12];
    unsigned i;
    assert(nj_mascot_anim_evaluate(blob, size, 0, bone, frame, base, out) == 0);
    for (i = 0; i < 12; i++) {
        if (out[i] != expected[i]) {
            fprintf(stderr, "bone %d frame %ld component %u: %ld != %ld\n",
                    bone, (long)frame, i, (long)out[i], (long)expected[i]);
            assert(out[i] == expected[i]);
        }
    }
}
static void test_types(void)
{
    fixture f;
    uint8_t storage[4097], *blob = storage + 1; /* Explicitly unaligned blob. */
    size_t n;
    int32_t value, expected[12], base[12], alias[12];
    static const int32_t quarter[12] = {0,-4096,0,7, 4096,0,0,8, 0,0,4096,9};
    static const int32_t xdirection[12] = {0,0,4096,0, 0,4096,0,0, -4096,0,0,0};
    static const int32_t scaled[12] = {0,0,4096,80, 8192,0,0,-40, 0,2048,0,20};
    make_all(&f); n = fill(&f, blob, 4096);
    assert(nj_mascot_anim_action_count(blob, n) == 2);
    assert(nj_mascot_anim_bone_count(blob, n) == 7);
    assert(nj_mascot_anim_frames(blob, n, 0, &value) == 0 && value == 655360);
    assert(nj_mascot_anim_frames(blob, n, 1, &value) == 0 && value == -65536);
    memcpy(expected, identity, sizeof expected);
    expected[3] = 10; expected[7] = -20; expected[11] = 30;
    pose(blob, n, 0, 0, NULL, expected);
    memcpy(base, identity, sizeof base); base[3] = 100; base[7] = 200; base[11] = -300;
    expected[3] += 100; expected[7] += 200; expected[11] -= 300;
    pose(blob, n, 0, 0, base, expected);
    memcpy(alias, base, sizeof alias);
    assert(nj_mascot_anim_evaluate(blob, n, 0, 0, 0, alias, alias) == 0);
    assert(memcmp(alias, expected, sizeof alias) == 0);
    pose(blob, n, 1, INT32_MIN, NULL, identity);
    pose(blob, n, 1, INT32_MAX, base, base);
    pose(blob, n, 2, 0, NULL, identity);
    pose(blob, n, 2, 4 * 65536, NULL, scaled);
    pose(blob, n, 2, INT32_MAX, NULL, scaled);
    pose(blob, n, 3, 0, NULL, quarter);
    memcpy(expected, quarter, sizeof expected); expected[3] = expected[7] = expected[11] = 0;
    pose(blob, n, 4, 4 * 65536, NULL, expected);
    pose(blob, n, 5, 0, NULL, xdirection);
    memcpy(expected, identity, sizeof expected);
    expected[3] = 50; expected[7] = -25; expected[11] = 12;
    pose(blob, n, 6, 2 * 65536 + 32768, NULL, expected);
    /* Equality compares the integer key: frame 0.5 intentionally stays at key0. */
    pose(blob, n, 6, 32768, NULL, identity);
    pose(blob, n, 6, -1, NULL, identity);
    pose(blob, n, 6, INT32_MIN, NULL, identity);
    expected[3] = 20; expected[7] = -10; expected[11] = 5;
    pose(blob, n, 6, 65536 + 15, NULL, expected); /* frame>>4 quantization */
    assert(nj_mascot_anim_pattern(blob, n, 0, -1, 23, &value) == 0 && value == INT32_MIN + 1);
    assert(nj_mascot_anim_pattern(blob, n, 0, 2 * 65536 + 1, 23, &value) == 0 && value == 19);
    assert(nj_mascot_anim_pattern(blob, n, 0, 5 * 65536, 23, &value) == 0 && value == -1);
    assert(nj_mascot_anim_pattern(blob, n, 1, 0, 23, &value) == 0 && value == 23);
    /* Caller can release/change raw bytes after fill. */
    memset(f.data, 0, sizeof f.data);
    pose(blob, n, 5, 0, NULL, xdirection);
}
static void test_validation(void)
{
    fixture f, bad;
    uint8_t blob[4096], before[4096];
    size_t n, i, bytes = 999;
    int32_t out[12], value = 123;
    make_all(&f); n = fill(&f, blob, sizeof blob);
    for (i = 0; i < f.n; i++)
        assert(nj_mascot_anim_query(f.data, i, &bytes) == NJ_MASCOT_ANIM_INVALID);
    assert(nj_mascot_anim_query(NULL, f.n, &bytes) == NJ_MASCOT_ANIM_INVALID);
    assert(nj_mascot_anim_query(f.data, f.n, NULL) == NJ_MASCOT_ANIM_INVALID);
    assert(nj_mascot_anim_query(f.data, SIZE_MAX, &bytes) == NJ_MASCOT_ANIM_INVALID);
    memcpy(before, blob, sizeof blob);
    assert(nj_mascot_anim_fill(f.data, f.n, blob, n - 1) == NJ_MASCOT_ANIM_SPACE);
    assert(memcmp(before, blob, sizeof blob) == 0);
    bad = f; bad.data[30] = 7;
    assert(nj_mascot_anim_fill(bad.data, bad.n, blob, sizeof blob) == NJ_MASCOT_ANIM_INVALID);
    assert(memcmp(before, blob, sizeof blob) == 0);
    bad = f; bad.data[0] = 'X'; assert(nj_mascot_anim_query(bad.data, bad.n, &bytes) < 0);
    bad = f; bad.data[2] = 6; assert(nj_mascot_anim_query(bad.data, bad.n, &bytes) < 0);
    bad = f; bad.data[3] = 1; assert(nj_mascot_anim_query(bad.data, bad.n, &bytes) < 0);
    bad = f; bad.data[4] = bad.data[5] = 255;
    assert(nj_mascot_anim_query(bad.data, bad.n, &bytes) < 0);
    assert(nj_mascot_anim_action_count(blob, n - 1) == -1);
    assert(nj_mascot_anim_bone_count(NULL, n) == -1);
    memcpy(out, identity, sizeof out);
    assert(nj_mascot_anim_evaluate(blob, n, -1, 0, 0, NULL, out) == NJ_MASCOT_ANIM_RANGE);
    assert(nj_mascot_anim_evaluate(blob, n, 0, 7, 0, NULL, out) == NJ_MASCOT_ANIM_RANGE);
    assert(nj_mascot_anim_evaluate(blob, n, 2, 0, 0, NULL, out) == NJ_MASCOT_ANIM_RANGE);
    assert(memcmp(out, identity, sizeof out) == 0);
    assert(nj_mascot_anim_frames(blob, n, 2, &value) == NJ_MASCOT_ANIM_RANGE && value == 123);
    assert(nj_mascot_anim_pattern(blob, n, -1, 0, 0, &value) == NJ_MASCOT_ANIM_RANGE && value == 123);
    assert(nj_mascot_anim_evaluate(blob, n, 0, 0, 0, NULL, NULL) == NJ_MASCOT_ANIM_INVALID);
    /* A curve count of zero and descending key times are rejected. */
    header(&bad, 5, 1, 1); u16(&bad, 10); u8(&bad, 5); u16(&bad, 0); u16(&bad, 0);
    assert(nj_mascot_anim_query(bad.data, bad.n, &bytes) < 0);
    header(&bad, 5, 1, 1); u16(&bad, 10); u8(&bad, 5); u16(&bad, 2);
    vector(&bad, 4, 0, 0, 4096); vector(&bad, 3, 0, 0, 4096); u16(&bad, 0);
    assert(nj_mascot_anim_query(bad.data, bad.n, &bytes) < 0);
    header(&bad, 5, 1, 1); u16(&bad, 10); u8(&bad, 4);
    constant_direction(&bad, 0, 0, 4096);
    u16(&bad, 2); u16(&bad, 3); u16(&bad, 0); u16(&bad, 2); u16(&bad, 0); u16(&bad, 0);
    assert(nj_mascot_anim_query(bad.data, bad.n, &bytes) < 0);
    header(&bad, 5, 1, 1); u16(&bad, 10); u8(&bad, 1);
    u16(&bad, 2); u16(&bad, 3); u32(&bad, 1); u16(&bad, 2); u32(&bad, 2);
    assert(nj_mascot_anim_query(bad.data, bad.n, &bytes) < 0);
    /* DMC resources have a 20-byte vendor trailer, which must be accepted. */
    for (i = 0; i < 20; i++) u8(&f, 0xa5);
    (void)fill(&f, blob, sizeof blob);
}
static void test_edge_curves(void)
{
    fixture f;
    uint8_t blob[4096];
    size_t n;
    int32_t expected[12], value;
    unsigned version;
    /* Zero vector normalization and -Z's special branch. */
    header(&f, 5, 1, 2); u16(&f, 10);
    u8(&f, 5); constant_direction(&f, 0, 0, 0);
    u8(&f, 5); constant_direction(&f, 0, 0, -4096); u16(&f, 0);
    n = fill(&f, blob, sizeof blob);
    pose(blob, n, 0, 0, NULL, identity);
    memcpy(expected, identity, sizeof expected); expected[5] = expected[10] = -4096;
    pose(blob, n, 1, 0, NULL, expected);
    /* Duplicate times select the last entry, without dividing by zero. */
    header(&f, 5, 1, 1); u16(&f, 10); u8(&f, 6);
    u16(&f, 3); vector(&f, 0, 1, 2, 3); vector(&f, 0, 4, 5, 6); vector(&f, 2, 8, 9, 10);
    constant_direction(&f, 0, 0, 4096); u16(&f, 1); u16(&f, 0); u16(&f, 0); u16(&f, 0);
    n = fill(&f, blob, sizeof blob);
    memcpy(expected, identity, sizeof expected); expected[3] = 4; expected[7] = 5; expected[11] = 6;
    pose(blob, n, 0, 32768, NULL, expected);
    /* Before a nonzero first time: deterministic vector clamp, scalar zero. */
    f.data[33] = 2; f.data[41] = 2; f.data[49] = 4;
    n = fill(&f, blob, sizeof blob);
    expected[3] = 1; expected[7] = 2; expected[11] = 3;
    pose(blob, n, 0, 0, NULL, expected);
    /* Unsigned 16-bit key times beyond the signed-frame API range remain valid. */
    header(&f, 5, 1, 1); u16(&f, 65535); u8(&f, 5);
    u16(&f, 2); vector(&f, 0, 0, 0, 4096); vector(&f, 65535, 0, 0, 4096); u16(&f, 0);
    n = fill(&f, blob, sizeof blob); pose(blob, n, 0, INT32_MAX, NULL, identity);
    for (version = 2; version <= 4; version++) {
        header(&f, version, 1, 1); u16(&f, 0); u8(&f, 1);
        n = fill(&f, blob, sizeof blob); pose(blob, n, 0, 0, NULL, identity);
        assert(nj_mascot_anim_frames(blob, n, 0, &value) == 0 && value == 0);
        assert(nj_mascot_anim_pattern(blob, n, 0, 0, 91, &value) == 0 && value == 91);
    }
    header(&f, 5, 0, 0); n = fill(&f, blob, sizeof blob);
    assert(nj_mascot_anim_action_count(blob, n) == 0 && nj_mascot_anim_bone_count(blob, n) == 0);
}
static void test_wrap(void)
{
    fixture f;
    uint8_t blob[4096];
    int32_t base[12] = {INT32_MAX,INT32_MIN,123456789,987654321,
                        -123456789,INT32_MAX,INT32_MIN,-987654321,
                        INT32_MIN,123456789,INT32_MAX,42};
    /* Independently generated with the unmodified Java AffineTrans oracle. */
    static const int32_t expected[12] = {-1,0,-275179,987509970,
                                       275179,-1,0,-987955729,
                                       0,-275179,-1,445802};
    size_t n;
    make_all(&f); n = fill(&f, blob, sizeof blob);
    pose(blob, n, 0, 0, base, expected);
}
int main(void)
{
    test_types(); test_validation(); test_edge_curves(); test_wrap();
    puts("MTRA compact decoder: types 0..6, interpolation, patterns, bounds, Java wrap passed");
    return 0;
}
