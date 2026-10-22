/*
 * MBAC layout/decoding adapted from FreeJ2ME-Plus Figure.java and Loader.java;
 * raw-normal normalization adapted from its Vector3D.java.
 * (https://github.com/TASEmulators/freej2me-plus, baca76a4dda6f30abac67fb4a3db009eed05f91c).
 * Copyright (c) 2026 Yury Kharchenko
 * Copyright (c) 2026 Roman Lahin
 *
 * MIT License
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
#include "nj_mascot_model.h"
#include <string.h>

#define ALIGNOF(T) offsetof(struct { char byte; T value; }, value)

typedef struct model_reader {
    const unsigned char *data;
    size_t size, pos;
    uint32_t cache;
    unsigned cached;
    nj_mascot_model_result error;
} model_reader;

static void fail(model_reader *r, nj_mascot_model_result error)
{
    if (r->error == NJ_MASCOT_MODEL_OK) r->error = error;
}

static uint32_t byte(model_reader *r)
{
    if (r->error != NJ_MASCOT_MODEL_OK) return 0;
    if (r->pos == r->size) {
        fail(r, NJ_MASCOT_MODEL_TRUNCATED);
        return 0;
    }
    return r->data[r->pos++];
}

static uint32_t word(model_reader *r)
{
    uint32_t low = byte(r);
    return low | (byte(r) << 8);
}

static int32_t signed_word(model_reader *r)
{
    uint32_t n = word(r);
    return n < 32768 ? (int32_t)n : (int32_t)n - 65536;
}

/* Only widths <= 16 enter here, so the cache never exceeds 23 bits. */
static uint32_t bits(model_reader *r, unsigned width)
{
    uint32_t value;
    while (r->cached < width) {
        r->cache |= byte(r) << r->cached;
        r->cached += 8;
    }
    value = r->cache & ((UINT32_C(1) << width) - 1);
    r->cache >>= width;
    r->cached -= width;
    return value;
}

static int32_t signed_bits(model_reader *r, unsigned width)
{
    uint32_t n = bits(r, width);
    return (n & (UINT32_C(1) << (width - 1))) ?
        (int32_t)n - (int32_t)(UINT32_C(1) << width) : (int32_t)n;
}

static void align_reader(model_reader *r)
{
    r->cache = 0;
    r->cached = 0;
}

static uint32_t section(model_reader *r, size_t *size, size_t count,
                        size_t stride, size_t alignment)
{
    size_t pad, start, bytes;
    if (count == 0) return 0;
    pad = (alignment - *size % alignment) % alignment;
    if (pad > SIZE_MAX - *size || count > SIZE_MAX / stride) goto overflow;
    start = *size + pad;
    bytes = count * stride;
    if (bytes > SIZE_MAX - start || start + bytes > UINT32_MAX) goto overflow;
    *size = start + bytes;
    return (uint32_t)start;
overflow:
    fail(r, NJ_MASCOT_MODEL_OVERFLOW);
    return 0;
}

static size_t layout(model_reader *r, nj_mascot_model *m)
{
    size_t size = sizeof(*m);
    m->vertices_offset = section(r, &size, m->num_vertices, 3 * sizeof(int16_t), ALIGNOF(int16_t));
    if (m->normal_format)
        m->normals_offset = section(r, &size, m->num_vertices, 3 * sizeof(int16_t), ALIGNOF(int16_t));
    m->poly_t3_offset = section(r, &size, m->num_poly_t3, 7 * sizeof(uint16_t), ALIGNOF(uint16_t));
    m->poly_t4_offset = section(r, &size, m->num_poly_t4, 9 * sizeof(uint16_t), ALIGNOF(uint16_t));
    m->poly_c3_offset = section(r, &size, m->num_poly_c3, 5 * sizeof(uint16_t), ALIGNOF(uint16_t));
    m->poly_c4_offset = section(r, &size, m->num_poly_c4, 6 * sizeof(uint16_t), ALIGNOF(uint16_t));
    m->colors_offset = section(r, &size, m->num_colors, sizeof(uint32_t), ALIGNOF(uint32_t));
    m->bones_offset = section(r, &size, m->num_bones, sizeof(nj_mascot_bone), ALIGNOF(nj_mascot_bone));
    m->patterns_offset = section(r, &size, (size_t)m->num_patterns * (m->num_textures + 1),
                                sizeof(nj_mascot_pattern_range), ALIGNOF(nj_mascot_pattern_range));
    if (r->error == NJ_MASCOT_MODEL_OK) m->total_bytes = (uint32_t)size;
    return size;
}

static void header(model_reader *r, nj_mascot_model *m)
{
    uint32_t magic0 = byte(r), magic1 = byte(r), reserved, bone_format = 1;
    m->version = (uint8_t)byte(r);
    reserved = byte(r);
    if (magic0 != 'M' || magic1 != 'B' || reserved) fail(r, NJ_MASCOT_MODEL_INVALID);
    if (m->version < 2 || m->version > 5) fail(r, NJ_MASCOT_MODEL_UNSUPPORTED);
    m->vertex_format = 1;
    m->polygon_format = 1;
    if (m->version > 3) {
        m->vertex_format = (uint8_t)byte(r);
        m->normal_format = (uint8_t)byte(r);
        m->polygon_format = (uint8_t)byte(r);
        bone_format = byte(r);
    }
    if ((m->vertex_format != 1 && m->vertex_format != 2) || m->normal_format > 2 ||
        m->polygon_format < 1 || m->polygon_format > 3 || bone_format != 1)
        fail(r, NJ_MASCOT_MODEL_UNSUPPORTED);
    m->num_vertices = (uint16_t)word(r);
    m->num_poly_t3 = (uint16_t)word(r);
    m->num_poly_t4 = (uint16_t)word(r);
    m->num_bones = (uint16_t)word(r);
    m->num_textures = m->num_patterns = 1;
    if (m->polygon_format == 3) {
        m->num_poly_c3 = (uint16_t)word(r);
        m->num_poly_c4 = (uint16_t)word(r);
        m->num_textures = (uint16_t)word(r);
        m->num_patterns = (uint16_t)word(r);
        m->num_colors = (uint16_t)word(r);
    }
    if (m->num_vertices > 21845 || m->num_textures > 16 ||
        m->num_patterns == 0 || m->num_patterns > 33 || m->num_colors > 256)
        fail(r, NJ_MASCOT_MODEL_INVALID);
    if (m->version < 5 && (m->num_patterns != 1 || m->num_textures > 1))
        fail(r, NJ_MASCOT_MODEL_UNSUPPORTED);
    if ((m->num_poly_t3 || m->num_poly_t4) && !m->num_textures)
        fail(r, NJ_MASCOT_MODEL_INVALID);
    if ((m->num_poly_c3 || m->num_poly_c4) && !m->num_colors)
        fail(r, NJ_MASCOT_MODEL_INVALID);
    /* A palette without colored faces has no payload in the reference. */
    if (!(m->num_poly_c3 || m->num_poly_c4)) m->num_colors = 0;
}

static void patterns(model_reader *r, const nj_mascot_model *m, unsigned char *out)
{
    uint32_t tri[2] = {0, 0}, quad[2] = {0, 0};
    unsigned p, slot;
    nj_mascot_pattern_range *ranges = out ?
        (nj_mascot_pattern_range *)(out + m->patterns_offset) : NULL;
    for (p = 0; p < m->num_patterns; ++p) {
        for (slot = 0; slot <= m->num_textures; ++slot) {
            unsigned kind = slot != 0;
            uint32_t max_tri = kind ? m->num_poly_t3 : m->num_poly_c3;
            uint32_t max_quad = kind ? m->num_poly_t4 : m->num_poly_c4;
            nj_mascot_pattern_range range;
            range.first_tri = tri[kind];
            range.first_quad = quad[kind];
            range.num_tri = m->version == 5 ? word(r) : max_tri;
            range.num_quad = m->version == 5 ? word(r) : max_quad;
            if (range.num_tri > max_tri - tri[kind] || range.num_quad > max_quad - quad[kind]) {
                fail(r, NJ_MASCOT_MODEL_INVALID);
                return;
            }
            tri[kind] += range.num_tri;
            quad[kind] += range.num_quad;
            if (ranges) *ranges++ = range;
        }
    }
    if (tri[0] != m->num_poly_c3 || quad[0] != m->num_poly_c4 ||
        tri[1] != m->num_poly_t3 || quad[1] != m->num_poly_t4)
        fail(r, NJ_MASCOT_MODEL_INVALID);
}

static void vertices(model_reader *r, const nj_mascot_model *m, unsigned char *out)
{
    int16_t *dst = out && m->vertices_offset ? (int16_t *)(out + m->vertices_offset) : NULL;
    uint32_t v = 0;
    while (v < m->num_vertices && !r->error) {
        unsigned width = 16, count = 1, i;
        if (m->vertex_format == 2) {
            uint32_t chunk = bits(r, 8);
            switch (chunk >> 6) {
            case 0: width = 8; break;
            case 1: width = 10; break;
            case 2: width = 13; break;
            default: width = 16; break;
            }
            count = (chunk & 63) + 1;
        }
        if (count > m->num_vertices - v) {
            fail(r, NJ_MASCOT_MODEL_INVALID);
            return;
        }
        for (i = 0; i < count * 3; ++i) {
            int32_t value = signed_bits(r, width);
            if (dst) *dst++ = (int16_t)value;
        }
        v += count;
    }
    align_reader(r);
}

static uint32_t root(uint32_t n)
{
    uint32_t result = 0, bit = UINT32_C(1) << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= result + bit) {
            n -= result + bit;
            result = (result >> 1) + bit;
        } else result >>= 1;
        bit >>= 2;
    }
    return result;
}

static void unit(int32_t xyz[3])
{
    uint32_t maximum = 0, length;
    int shift = 14;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        uint32_t magnitude = (uint32_t)(xyz[i] < 0 ? -xyz[i] : xyz[i]);
        if (magnitude > maximum) maximum = magnitude;
    }
    while (maximum >>= 1) --shift;
    for (i = 0; i < 3; ++i) {
        if (shift >= 0) xyz[i] *= (int32_t)(UINT32_C(1) << shift);
        else xyz[i] = xyz[i] >= 0 ? xyz[i] / 2 : -((-xyz[i] + 1) / 2);
    }
    length = root((uint32_t)(xyz[0] * xyz[0]) + (uint32_t)(xyz[1] * xyz[1]) +
                  (uint32_t)(xyz[2] * xyz[2]));
    if (!length) {
        xyz[0] = xyz[1] = 0;
        xyz[2] = 4096;
    } else {
        for (i = 0; i < 3; ++i) xyz[i] = (xyz[i] * 4096) / (int32_t)length;
    }
}

static void normals(model_reader *r, const nj_mascot_model *m, unsigned char *out)
{
    int16_t *dst = out && m->normals_offset ? (int16_t *)(out + m->normals_offset) : NULL;
    unsigned v;
    if (!m->normal_format) return;
    for (v = 0; v < m->num_vertices && !r->error; ++v) {
        int32_t xyz[3];
        unsigned i;
        if (m->normal_format == 1) {
            for (i = 0; i < 3; ++i) xyz[i] = signed_word(r);
            /* The reference accidentally writes these into vertices. Keep
             * geometry intact and put normalized values in the normal array. */
            unit(xyz);
        } else {
            uint32_t x = bits(r, 7);
            if (x == 64) {
                uint32_t axis = bits(r, 3);
                if (axis > 5) {
                    fail(r, NJ_MASCOT_MODEL_INVALID);
                    return;
                }
                xyz[0] = xyz[1] = xyz[2] = 0;
                xyz[axis % 3] = axis < 3 ? 4096 : -4096;
            } else {
                int32_t dq;
                uint32_t sign, z;
                xyz[0] = (x < 64 ? (int32_t)x : (int32_t)x - 128) * 64;
                xyz[1] = signed_bits(r, 7) * 64;
                sign = bits(r, 1);
                dq = 4096 * 4096 - xyz[0] * xyz[0] - xyz[1] * xyz[1];
                z = dq > 0 ? root((uint32_t)dq) : 0;
                if (dq > 0 && (uint32_t)dq - z * z > z) ++z;
                xyz[2] = sign ? -(int32_t)z : (int32_t)z;
            }
        }
        if (dst) for (i = 0; i < 3; ++i) *dst++ = (int16_t)xyz[i];
    }
    align_reader(r);
}

static uint16_t flat_material(const nj_mascot_model *m, const unsigned char *out,
                              uint32_t mat, const uint16_t *indices, unsigned n)
{
    if (out && m->normals_offset) {
        const int16_t *normal = (const int16_t *)(out + m->normals_offset);
        unsigned i;
        for (i = 1; i < n; ++i) {
            if (memcmp(normal + 3 * indices[0], normal + 3 * indices[i],
                       3 * sizeof(int16_t))) break;
        }
        if (i == n) mat |= NJ_MASCOT_MAT_FLAT_NORMAL;
    }
    return (uint16_t)mat;
}

static void polygons(model_reader *r, nj_mascot_model *m, unsigned char *out, int colored)
{
    uint32_t counts[2], offsets[2];
    unsigned mb = 0, vb = 0, ub = 0, cb = 0, cib = 0, type;
    counts[0] = colored ? m->num_poly_c3 : m->num_poly_t3;
    counts[1] = colored ? m->num_poly_c4 : m->num_poly_t4;
    offsets[0] = colored ? m->poly_c3_offset : m->poly_t3_offset;
    offsets[1] = colored ? m->poly_c4_offset : m->poly_t4_offset;
    if (!counts[0] && !counts[1]) return;
    if (colored) {
        unsigned c;
        uint32_t *palette = out ? (uint32_t *)(out + m->colors_offset) : NULL;
        mb = byte(r); vb = byte(r); cb = byte(r); cib = byte(r);
        (void)byte(r);
        if (mb > 16 || vb > 16 || cb == 0 || cb > 8 || cib > 8) {
            fail(r, NJ_MASCOT_MODEL_INVALID);
            return;
        }
        for (c = 0; c < m->num_colors; ++c) {
            uint32_t red = 255 * bits(r, cb) / ((1u << cb) - 1);
            uint32_t green = 255 * bits(r, cb) / ((1u << cb) - 1);
            uint32_t blue = 255 * bits(r, cb) / ((1u << cb) - 1);
            if (palette) palette[c] = UINT32_C(0xff000000) | (red << 16) | (green << 8) | blue;
        }
    } else if (m->polygon_format != 1) {
        mb = bits(r, 8); vb = bits(r, 8);
        ub = 7;
        if (m->polygon_format == 3) {
            ub = bits(r, 8);
            (void)bits(r, 8);
        }
        if (mb > 16 || vb > 16 || ub > 8) {
            fail(r, NJ_MASCOT_MODEL_INVALID);
            return;
        }
    }
    for (type = 0; type < 2; ++type) {
        unsigned n = type + 3;
        uint16_t *dst = out && counts[type] ? (uint16_t *)(out + offsets[type]) : NULL;
        uint32_t p;
        for (p = 0; p < counts[type] && !r->error; ++p) {
            uint16_t indices[4], uv[4], material;
            uint32_t mat, color = 0;
            unsigned i;
            if (!colored && m->polygon_format == 1) {
                mat = word(r);
                if ((mat & ~UINT32_C(7)) || (mat & 1) != type)
                    fail(r, NJ_MASCOT_MODEL_INVALID);
                mat = ((mat & 4) ? NJ_MASCOT_MAT_DOUBLE_FACE : 0) |
                      ((mat & 2) ? NJ_MASCOT_MAT_COLORKEY : 0);
            } else {
                mat = bits(r, mb);
                if (colored) mat <<= 1;
                if (mat & (colored ? ~UINT32_C(0x3f6) :
                    m->polygon_format == 2 ? ~UINT32_C(0x77) : ~UINT32_C(0x3f7)))
                    fail(r, NJ_MASCOT_MODEL_INVALID);
            }
            for (i = 0; i < n; ++i) {
                uint32_t index = !colored && m->polygon_format == 1 ? word(r) : bits(r, vb);
                if (index >= m->num_vertices) fail(r, NJ_MASCOT_MODEL_INVALID);
                indices[i] = (uint16_t)index;
            }
            if (colored) {
                color = bits(r, cib);
                if (color >= m->num_colors) fail(r, NJ_MASCOT_MODEL_INVALID);
            } else {
                for (i = 0; i < n; ++i) {
                    uint32_t u = m->polygon_format == 1 ? byte(r) : bits(r, ub);
                    uint32_t v = m->polygon_format == 1 ? byte(r) : bits(r, ub);
                    uv[i] = (uint16_t)((u << 8) | v);
                }
            }
            if (r->error) return; /* Do not dereference a bad vertex index. */
            material = flat_material(m, out, mat, indices, n);
            m->materials_and &= material;
            m->materials_or |= material;
            if (dst) {
                *dst++ = material;
                if (colored) *dst++ = (uint16_t)color;
                for (i = 0; i < n; ++i) *dst++ = indices[i];
                if (!colored) for (i = 0; i < n; ++i) *dst++ = uv[i];
            }
        }
    }
}

static void bones(model_reader *r, const nj_mascot_model *m, unsigned char *out)
{
    nj_mascot_bone *dst = out && m->bones_offset ? (nj_mascot_bone *)(out + m->bones_offset) : NULL;
    uint32_t first = 0;
    unsigned i, j;
    for (i = 0; i < m->num_bones && !r->error; ++i) {
        nj_mascot_bone bone;
        bone.num_vertices = (uint16_t)word(r);
        bone.parent = signed_word(r);
        bone.start_vertex = (uint16_t)first;
        if ((bone.parent != -1 && (bone.parent < 0 || (uint32_t)bone.parent >= i)) ||
            bone.num_vertices > m->num_vertices - first) {
            fail(r, NJ_MASCOT_MODEL_INVALID);
            return;
        }
        for (j = 0; j < 12; ++j) bone.matrix[j] = signed_word(r);
        first += bone.num_vertices;
        if (dst) dst[i] = bone;
    }
    if (first != m->num_vertices) fail(r, NJ_MASCOT_MODEL_INVALID);
}

nj_mascot_model_result nj_mascot_model_decode(const void *source,
    size_t source_bytes, void *storage, size_t capacity, size_t *output_bytes)
{
    model_reader r;
    nj_mascot_model m;
    unsigned char *out = NULL;
    size_t required;
    if (!output_bytes) return NJ_MASCOT_MODEL_ARGUMENT;
    *output_bytes = 0;
    if (!source || (!storage && capacity)) return NJ_MASCOT_MODEL_ARGUMENT;
    if (storage) {
        uintptr_t s = (uintptr_t)source, d = (uintptr_t)storage;
        if (d % ALIGNOF(nj_mascot_model) ||
            (d >= s ? d - s < source_bytes : s - d < capacity))
            return NJ_MASCOT_MODEL_ARGUMENT;
    }
    memset(&r, 0, sizeof(r));
    memset(&m, 0, sizeof(m));
    r.data = (const unsigned char *)source;
    r.size = source_bytes;
    m.materials_and = UINT16_MAX;
    header(&r, &m);
    if (r.error) return r.error;
    required = layout(&r, &m);
    if (r.error) return r.error;
    if (storage && capacity >= required) {
        out = (unsigned char *)storage;
        memset(out, 0, required);
    }
    patterns(&r, &m, out);
    if (!r.error) vertices(&r, &m, out);
    if (!r.error) normals(&r, &m, out);
    if (!r.error) polygons(&r, &m, out, 1);
    if (!r.error) polygons(&r, &m, out, 0);
    align_reader(&r);
    if (!r.error) bones(&r, &m, out);
    if (r.error) return r.error;
    if (r.pos > UINT32_MAX) return NJ_MASCOT_MODEL_OVERFLOW;
    m.source_bytes = (uint32_t)r.pos;
    if (out) memcpy(out, &m, sizeof(m));
    *output_bytes = required;
    return storage && capacity < required ? NJ_MASCOT_MODEL_STORAGE : NJ_MASCOT_MODEL_OK;
}
