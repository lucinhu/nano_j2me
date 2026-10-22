/*
 * Compact MTRA decoding and integer pose evaluation.
 *
 * Adapted from FreeJ2ME-Plus ActionTable.java, AffineTrans.java, Vector3D.java
 * and Util3D.java (commit baca76a4dda6f30abac67fb4a3db009eed05f91c).
 * Copyright (c) 2026 Yury Kharchenko
 * Copyright (c) 2026 Roman Lahin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "nj_mascot_anim.h"
#include <limits.h>
#include <string.h>

/* Reuse the immutable quarter-wave table without depending on VM headers. */
#define nj_u2 uint16_t
#include "nj_mascot_sin.inc"
#undef nj_u2

#define ANIM_HEADER 24u
#define ANIM_MAGIC UINT32_C(0x3141544d) /* MTA1, internal blob version */

typedef struct {
    const uint8_t *data;
    size_t size, pos;
    int bad;
} anim_reader;

typedef struct {
    const uint8_t *data;
    uint32_t raw, total;
    unsigned actions, bones;
} anim_view;

static uint32_t get16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t get32(const uint8_t *p)
{
    return get16(p) | get16(p + 2) << 16;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Explicit Java int conversions and shifts: no C signed overflow or reliance
 * on implementation-defined unsigned-to-signed conversion/right shifts. */
static int32_t signed32(uint32_t v)
{
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)~v;
}

static int32_t signed16(const uint8_t *p)
{
    uint32_t v = get16(p);
    return v < 32768u ? (int32_t)v : (int32_t)v - 65536;
}

static int32_t add(int32_t a, int32_t b)
{
    return signed32((uint32_t)a + (uint32_t)b);
}

static int32_t sub(int32_t a, int32_t b)
{
    return signed32((uint32_t)a - (uint32_t)b);
}

static int32_t mul(int32_t a, int32_t b)
{
    return signed32((uint32_t)a * (uint32_t)b);
}

static int32_t shr(int32_t a, unsigned n)
{
    uint32_t v = (uint32_t)a;
    if (!n) return a;
    return signed32((v >> n) | (a < 0 ? UINT32_MAX << (32 - n) : 0));
}

static int32_t shl(int32_t a, unsigned n)
{
    return signed32((uint32_t)a << n);
}

static int32_t rounded(int32_t a)
{
    return shr(add(a, 2048), 12);
}

static const uint8_t *take(anim_reader *r, size_t count)
{
    const uint8_t *p;
    if (r->bad || count > r->size - r->pos) {
        r->bad = 1;
        return NULL;
    }
    p = r->data + r->pos;
    r->pos += count;
    return p;
}

static uint32_t read16(anim_reader *r)
{
    const uint8_t *p = take(r, 2);
    return p ? get16(p) : 0;
}

static const uint8_t *curve(anim_reader *r, unsigned stride, int validate)
{
    const uint8_t *p;
    uint32_t i, count = read16(r);
    if (!count) { r->bad = 1; return NULL; }
    p = take(r, (size_t)count * stride);
    if (!p) return NULL;
    if (validate) {
        for (i = 1; i < count; i++) {
            if (get16(p + i * stride) < get16(p + (i - 1) * stride)) {
                r->bad = 1;
                return NULL;
            }
        }
    }
    return p - 2; /* Keep the packed count with the keys. */
}

/* The header's transform totals/dataSize are exporter allocation hints and
 * are ignored by the Java reference. Validate actual records, not those hints.
 * Empty action/bone tables and zero declared frame lengths are valid; empty
 * sampled curves are invalid because their evaluator requires a last key.
 */
static int index_mtra(const uint8_t *data, size_t size, uint8_t *dst,
                      size_t *required)
{
    anim_reader r;
    unsigned a, b, actions, bones, version;
    uint64_t raw, total;
    if (!data || size < 28 || size > UINT32_MAX ||
        data[0] != 'M' || data[1] != 'T' || data[3] != 0 ||
        data[2] < 2 || data[2] > 5) return NJ_MASCOT_ANIM_INVALID;
    actions = (unsigned)get16(data + 4);
    bones = (unsigned)get16(data + 6);
    version = data[2];
    raw = ANIM_HEADER + (uint64_t)actions * 8 + (uint64_t)actions * bones * 4;
    total = raw + size;
    if (total > UINT32_MAX || total > SIZE_MAX) return NJ_MASCOT_ANIM_INVALID;
    /* Every action needs two frame bytes and every bone at least one type. */
    if ((uint64_t)actions * (2u + bones + (version == 5 ? 2u : 0u)) > size - 28)
        return NJ_MASCOT_ANIM_INVALID;
    r.data = data; r.size = size; r.pos = 28; r.bad = 0;
    for (a = 0; a < actions; a++) {
        if (dst) put32(dst + ANIM_HEADER + a * 8, (uint32_t)(raw + r.pos));
        (void)read16(&r);
        for (b = 0; b < bones; b++) {
            const uint8_t *p;
            if (dst) put32(dst + ANIM_HEADER + actions * 8 +
                           ((size_t)a * bones + b) * 4, (uint32_t)(raw + r.pos));
            p = take(&r, 1);
            if (!p) return NJ_MASCOT_ANIM_INVALID;
            switch (*p) {
            case 0: (void)take(&r, 24); break;
            case 1: break;
            case 2:
                (void)curve(&r, 8, 1); (void)curve(&r, 8, 1);
                (void)curve(&r, 8, 1); (void)curve(&r, 4, 1); break;
            case 3:
                (void)take(&r, 6); (void)curve(&r, 8, 1);
                (void)take(&r, 2); break;
            case 4: (void)curve(&r, 8, 1); (void)curve(&r, 4, 1); break;
            case 5: (void)curve(&r, 8, 1); break;
            case 6:
                (void)curve(&r, 8, 1); (void)curve(&r, 8, 1);
                (void)curve(&r, 4, 1); break;
            default: return NJ_MASCOT_ANIM_INVALID;
            }
            if (r.bad) return NJ_MASCOT_ANIM_INVALID;
        }
        if (dst) put32(dst + ANIM_HEADER + a * 8 + 4,
                       version == 5 ? (uint32_t)(raw + r.pos) : 0);
        if (version == 5) {
            uint32_t i, count = read16(&r);
            const uint8_t *p = take(&r, (size_t)count * 6);
            if (!p) return NJ_MASCOT_ANIM_INVALID;
            for (i = 1; i < count; i++)
                if (get16(p + i * 6) < get16(p + (i - 1) * 6))
                    return NJ_MASCOT_ANIM_INVALID;
        }
        if (r.bad) return NJ_MASCOT_ANIM_INVALID;
    }
    if (dst) {
        put32(dst, ANIM_MAGIC); put32(dst + 4, (uint32_t)total);
        put32(dst + 8, (uint32_t)raw); put32(dst + 12, (uint32_t)size);
        put32(dst + 16, actions | (uint32_t)bones << 16);
        put32(dst + 20, 0);
        memcpy(dst + (size_t)raw, data, size);
    }
    *required = (size_t)total;
    return NJ_MASCOT_ANIM_OK;
}

int nj_mascot_anim_query(const void *mtra, size_t size, size_t *blob_size)
{
    if (!blob_size) return NJ_MASCOT_ANIM_INVALID;
    return index_mtra((const uint8_t *)mtra, size, NULL, blob_size);
}

int nj_mascot_anim_fill(const void *mtra, size_t size, void *blob, size_t capacity)
{
    size_t required;
    int result = index_mtra((const uint8_t *)mtra, size, NULL, &required);
    if (result) return result;
    if (!blob || capacity < required) return NJ_MASCOT_ANIM_SPACE;
    return index_mtra((const uint8_t *)mtra, size, (uint8_t *)blob, &required);
}

static int view_blob(const void *blob, size_t size, anim_view *v)
{
    const uint8_t *p = (const uint8_t *)blob;
    uint64_t raw;
    if (!p || size < ANIM_HEADER || get32(p) != ANIM_MAGIC)
        return NJ_MASCOT_ANIM_INVALID;
    v->data = p; v->total = get32(p + 4); v->raw = get32(p + 8);
    v->actions = (unsigned)get16(p + 16); v->bones = (unsigned)get16(p + 18);
    raw = ANIM_HEADER + (uint64_t)v->actions * 8 +
          (uint64_t)v->actions * v->bones * 4;
    if (raw != v->raw || v->total > size || v->raw > v->total ||
        get32(p + 12) != v->total - v->raw || get32(p + 12) < 28)
        return NJ_MASCOT_ANIM_INVALID;
    return NJ_MASCOT_ANIM_OK;
}

static int valid_offset(const anim_view *v, uint32_t offset, size_t count)
{
    return offset >= v->raw && offset <= v->total && count <= v->total - offset;
}

int nj_mascot_anim_action_count(const void *blob, size_t size)
{
    anim_view v;
    return view_blob(blob, size, &v) ? -1 : (int)v.actions;
}

int nj_mascot_anim_bone_count(const void *blob, size_t size)
{
    anim_view v;
    return view_blob(blob, size, &v) ? -1 : (int)v.bones;
}

int nj_mascot_anim_frames(const void *blob, size_t size, int action, int32_t *frames)
{
    anim_view v;
    uint32_t offset;
    if (!frames || view_blob(blob, size, &v)) return NJ_MASCOT_ANIM_INVALID;
    if (action < 0 || (unsigned)action >= v.actions) return NJ_MASCOT_ANIM_RANGE;
    offset = get32(v.data + ANIM_HEADER + (unsigned)action * 8);
    if (!valid_offset(&v, offset, 2)) return NJ_MASCOT_ANIM_INVALID;
    *frames = signed32(get16(v.data + offset) << 16);
    return NJ_MASCOT_ANIM_OK;
}

int nj_mascot_anim_pattern(const void *blob, size_t size, int action,
                           int32_t frame, int32_t fallback, int32_t *pattern)
{
    anim_view v;
    uint32_t offset, count;
    int32_t integer = frame < 0 ? 0 : shr(frame, 16);
    const uint8_t *p;
    if (!pattern || view_blob(blob, size, &v)) return NJ_MASCOT_ANIM_INVALID;
    if (action < 0 || (unsigned)action >= v.actions) return NJ_MASCOT_ANIM_RANGE;
    offset = get32(v.data + ANIM_HEADER + (unsigned)action * 8 + 4);
    if (!offset) { *pattern = fallback; return NJ_MASCOT_ANIM_OK; }
    if (!valid_offset(&v, offset, 2)) return NJ_MASCOT_ANIM_INVALID;
    count = get16(v.data + offset);
    if (!valid_offset(&v, offset + 2, (size_t)count * 6))
        return NJ_MASCOT_ANIM_INVALID;
    p = v.data + offset + 2;
    while (count) {
        --count;
        if ((int32_t)get16(p + count * 6) <= integer) {
            *pattern = signed32(get32(p + count * 6 + 2));
            return NJ_MASCOT_ANIM_OK;
        }
    }
    *pattern = fallback;
    return NJ_MASCOT_ANIM_OK;
}

/* Binary search has the same last-key-wins behavior as the reference reverse
 * scan, including duplicate times and its whole-key-integer early return. */
static void interpolate(const uint8_t *curve_data, unsigned dimensions,
                         int32_t frame, int32_t *out)
{
    uint32_t count = get16(curve_data), lo = 0, hi = count, idx, j;
    unsigned stride = (dimensions + 1) * 2;
    const uint8_t *keys = curve_data + 2, *p;
    int32_t integer = shr(frame, 12), delta;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if ((int32_t)get16(keys + mid * stride) <= integer) lo = mid + 1;
        else hi = mid;
    }
    if (!lo) {
        for (j = 0; j < dimensions; j++)
            out[j] = dimensions == 1 ? 0 : signed16(keys + 2 + j * 2);
        return;
    }
    idx = lo - 1;
    p = keys + idx * stride;
    if (idx == count - 1 || (int32_t)get16(p) == integer) {
        for (j = 0; j < dimensions; j++) out[j] = signed16(p + 2 + j * 2);
        return;
    }
    delta = sub(frame, shl((int32_t)get16(p), 12)) /
            (int32_t)(get16(p + stride) - get16(p));
    for (j = 0; j < dimensions; j++) {
        int32_t first = signed16(p + 2 + j * 2);
        int32_t next = signed16(p + stride + 2 + j * 2);
        out[j] = add(first, shr(mul(sub(next, first), delta), 12));
    }
}

static unsigned leading_zeros(uint32_t n)
{
    unsigned result = 32;
    while (n) { n >>= 1; --result; }
    return result;
}

static uint32_t abs_bits(int32_t n)
{
    return n < 0 ? 0u - (uint32_t)n : (uint32_t)n;
}

static uint32_t integer_sqrt(uint32_t n)
{
    uint32_t original = n, root = 0, bit = UINT32_C(1) << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    /* floor(sqrt(n)+0.5), with the reference's saturation at 65535. */
    if (root < 65535 && original - root * root > root) ++root;
    return root;
}

static void unit(int32_t v[3])
{
    int shift = (int)leading_zeros(abs_bits(v[0]) | abs_bits(v[1]) | abs_bits(v[2])) - 17;
    int32_t x = v[0], y = v[1], z = v[2];
    uint32_t length;
    if (shift > 0) {
        x = shl(x, (unsigned)shift); y = shl(y, (unsigned)shift); z = shl(z, (unsigned)shift);
    } else if (shift < 0) {
        x = shr(x, (unsigned)-shift); y = shr(y, (unsigned)-shift); z = shr(z, (unsigned)-shift);
    }
    length = integer_sqrt((uint32_t)x * (uint32_t)x +
                          (uint32_t)y * (uint32_t)y + (uint32_t)z * (uint32_t)z);
    if (length) {
        v[0] = shl(x, 12) / (int32_t)length;
        v[1] = shl(y, 12) / (int32_t)length;
        v[2] = shl(z, 12) / (int32_t)length;
    } else { v[0] = v[1] = 0; v[2] = 4096; }
}

static void direction(int32_t m[12], int32_t v[3])
{
    int32_t x, y, z, xx, yy;
    unit(v);
    x = v[0]; y = v[1]; z = v[2];
    xx = rounded(mul(x, x)); yy = rounded(mul(y, y));
    if (xx > 0 || yy > 0) {
        int32_t a = shl(sub(4096, z), 12) / add(yy, xx);
        int32_t b = shr(mul(a, sub(0, rounded(mul(x, y)))), 12);
        m[0] = add(z, rounded(mul(yy, a))); m[1] = b; m[2] = x;
        m[4] = b; m[5] = add(z, rounded(mul(xx, a))); m[6] = y;
        m[8] = sub(0, x); m[9] = sub(0, y);
    } else {
        m[0] = 4096; m[1] = m[2] = m[4] = m[6] = m[8] = m[9] = 0;
        m[5] = z;
    }
    m[10] = z;
}

static int32_t sine(uint32_t angle)
{
    unsigned phase = angle & 4095u;
    unsigned index = phase & 1023u;
    int32_t value = m3_sin_quarter[(phase & 1024u) ? 1024u - index : index];
    return phase & 2048u ? -value : value;
}

static void roll(int32_t m[12], int32_t angle)
{
    unsigned row;
    int32_t s = sine((uint32_t)angle), c = sine((uint32_t)angle + 1024u);
    for (row = 0; row < 12; row += 4) {
        int32_t x = m[row], y = m[row + 1];
        m[row] = rounded(add(mul(x, c), mul(y, s)));
        m[row + 1] = rounded(sub(mul(y, c), mul(x, s)));
    }
}

static void compose(const int32_t base[12], const int32_t m[12], int32_t out[12])
{
    unsigned row, col;
    for (row = 0; row < 12; row += 4) {
        for (col = 0; col < 4; col++) {
            int32_t sum = add(add(mul(base[row], m[col]),
                                 mul(base[row + 1], m[col + 4])),
                                 mul(base[row + 2], m[col + 8]));
            out[row + col] = rounded(sum);
        }
        out[row + 3] = add(out[row + 3], base[row + 3]);
    }
}

int nj_mascot_anim_evaluate(const void *blob, size_t size, int action, int bone,
                            int32_t frame, const int32_t base[12], int32_t out[12])
{
    anim_view v;
    anim_reader r;
    uint32_t offset;
    const uint8_t *p, *translate = NULL, *scale = NULL, *rotate = NULL, *rolling = NULL;
    int32_t m[12] = {4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0};
    int32_t vector[3], angle, result[12];
    unsigned type, i, j;
    if (!out || view_blob(blob, size, &v)) return NJ_MASCOT_ANIM_INVALID;
    if (action < 0 || (unsigned)action >= v.actions || bone < 0 || (unsigned)bone >= v.bones)
        return NJ_MASCOT_ANIM_RANGE;
    offset = get32(v.data + ANIM_HEADER + v.actions * 8 +
                   ((size_t)(unsigned)action * v.bones + (unsigned)bone) * 4);
    if (!valid_offset(&v, offset, 1)) return NJ_MASCOT_ANIM_INVALID;
    r.data = v.data; r.size = v.total; r.pos = offset; r.bad = 0;
    p = take(&r, 1); type = *p;
    frame = shr(frame, 4);
    switch (type) {
    case 0:
        p = take(&r, 24);
        if (!p) return NJ_MASCOT_ANIM_INVALID;
        for (i = 0; i < 12; i++) m[i] = signed16(p + i * 2);
        break;
    case 1:
        if (base) memcpy(m, base, sizeof m);
        memcpy(out, m, sizeof m);
        return NJ_MASCOT_ANIM_OK;
    case 2:
        translate = curve(&r, 8, 0); scale = curve(&r, 8, 0);
        rotate = curve(&r, 8, 0); rolling = curve(&r, 4, 0); break;
    case 3:
        translate = take(&r, 6); rotate = curve(&r, 8, 0);
        rolling = take(&r, 2); break;
    case 4: rotate = curve(&r, 8, 0); rolling = curve(&r, 4, 0); break;
    case 5: rotate = curve(&r, 8, 0); break;
    case 6:
        translate = curve(&r, 8, 0); rotate = curve(&r, 8, 0);
        rolling = curve(&r, 4, 0); break;
    default: return NJ_MASCOT_ANIM_INVALID;
    }
    if (r.bad) return NJ_MASCOT_ANIM_INVALID;
    if (type >= 2) {
        if (translate) {
            if (type == 3)
                for (i = 0; i < 3; i++) vector[i] = signed16(translate + i * 2);
            else interpolate(translate, 3, frame, vector);
            m[3] = vector[0]; m[7] = vector[1]; m[11] = vector[2];
        }
        interpolate(rotate, 3, frame, vector);
        direction(m, vector);
        if (rolling) {
            if (type == 3) angle = signed16(rolling);
            else interpolate(rolling, 1, frame, &angle);
            roll(m, angle);
        }
        if (scale) {
            interpolate(scale, 3, frame, vector);
            for (i = 0; i < 12; i += 4)
                for (j = 0; j < 3; j++) m[i + j] = rounded(mul(m[i + j], vector[j]));
        }
    }
    if (base) { compose(base, m, result); memcpy(out, result, sizeof result); }
    else memcpy(out, m, sizeof m);
    return NJ_MASCOT_ANIM_OK;
}
