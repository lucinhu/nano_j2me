/*
 * nano_j2me - test_m3g_file.c
 *
 * The .m3g container and object framing are the parts of JSR 184 that can be
 * checked without putting a game file in the repository: every fixture below
 * is built out of bytes right here, so "truncated section" is a test case
 * instead of a hope.
 *
 * This file cannot catch the format being written down wrongly - it encodes
 * the format, so a wrong format and a matching test agree with each other.
 * That is not hypothetical: an earlier version of this test asserted a version
 * byte at offset 0, big endian section lengths and an object length that
 * included its own header, and it passed while every real file was rejected.
 * tools/check_m3g_reals.sh is the other half: it runs the same probes over
 * every .m3g file the collected games ship, and real files cannot agree with
 * anything.
 *
 * The layout below is the one in the specification and the one the real files
 * use:
 *
 *   section:  scheme(1) | total length(u32 LE) | uncompressed length(u32 LE)
 *             | objects | Adler32 checksum(u32 LE)
 *   object:   type(1) | data length(u32 LE, NOT counting this header) | data
 */
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include "nj_internal.h"
#include "nj2me/nj.h"

nj_status nj_m3g_late_init(nj_vm *vm);

nj_status nj_m3g_container_probe(const nj_u1 *p, nj_u4 n,
                                 nj_u1 *ids, nj_u4 id_count,
                                 char *err, nj_u4 errcap);
nj_status nj_m3g_objects_probe(const nj_u1 *p, nj_u4 n, nj_u4 *count,
                               nj_u1 *types, nj_u4 type_count,
                               char *err, nj_u4 errcap);

/* Scan payloads use tracked host allocations; the loader's result cleanup is
 * private, so release the same allocations directly in these scanner tests. */
void nj_host_free(nj_vm *vm, void *p);

static int checks, failures;
static char err[256];
static nj_u1 buf[1024];
static nj_u1 ids[8];

static const nj_u1 ident[12] = {
    0xabu, 0x4au, 0x53u, 0x52u, 0x31u, 0x38u,
    0x34u, 0xbbu, 0x0du, 0x0au, 0x1au, 0x0au
};

static nj_u4 le32_at(const nj_u1 *p)
{
    return (nj_u4)p[0] | ((nj_u4)p[1] << 8) | ((nj_u4)p[2] << 16) | ((nj_u4)p[3] << 24);
}

static void put32le(nj_u4 v, int at)
{
    buf[at + 0] = (nj_u1)v;
    buf[at + 1] = (nj_u1)(v >> 8);
    buf[at + 2] = (nj_u1)(v >> 16);
    buf[at + 3] = (nj_u1)(v >> 24);
}

/* One section.  Returns its total length, so callers can place the next one. */
static nj_u4 section(int at, nj_u1 scheme, nj_u4 ulen, nj_u4 objs_len, nj_u1 fill)
{
    nj_u4 clen = 9u + objs_len + 4u;
    nj_u4 i;
    buf[at] = scheme;
    put32le(clen, at + 1);
    put32le(ulen, at + 5);
    for (i = 9u; i < 9u + objs_len; i++) { buf[at + (int)i] = fill; }
    put32le(0u, at + 9 + (int)objs_len);   /* the Adler32 checksum */
    return clen;
}

/* One object: type, data length, data.  Returns the whole chunk's length. */
static nj_u4 object(int at, nj_u1 type, nj_u4 data_len, nj_u1 fill)
{
    nj_u4 i;
    buf[at] = type;
    put32le(data_len, at + 1);
    for (i = 0u; i < data_len; i++) { buf[at + 5 + (int)i] = fill; }
    return 5u + data_len;
}

static void start(void)
{
    memset(buf, 0, sizeof(buf));
    memcpy(buf, ident, sizeof(ident));
}

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("  FAIL  %s\n", what); }
}

static void expect_ok(nj_u4 n, const char *what)
{
    nj_status st = nj_m3g_container_probe(buf, n, ids, 8, err, sizeof(err));
    checks++;
    if (st != NJ_OK) {
        failures++;
        printf("  FAIL  %s: expected a well formed container, got %d (%s)\n",
               what, (int)st, err);
    }
}

static void expect_bad(nj_u4 n, const char *mentions, const char *what)
{
    nj_status st = nj_m3g_container_probe(buf, n, NULL, 0, err, sizeof(err));
    checks++;
    if (st == NJ_OK) { failures++; printf("  FAIL  %s: accepted\n", what); return; }
    if (mentions && !strstr(err, mentions)) {
        failures++;
        printf("  FAIL  %s: said \"%s\", which does not mention \"%s\"\n",
               what, err, mentions);
    }
}

static void obj_ok(nj_u4 n, nj_u4 want_count, const char *what)
{
    nj_u4 count = 0;
    nj_status st = nj_m3g_objects_probe(buf, n, &count, NULL, 0u, err, sizeof(err));
    checks++;
    if (st != NJ_OK || count != want_count) {
        failures++;
        printf("  FAIL  %s: got status %d and %lu object(s), wanted %lu (%s)\n",
               what, (int)st, (unsigned long)count, (unsigned long)want_count, err);
    }
}

static void obj_bad(nj_u4 n, const char *mentions, const char *what)
{
    nj_u4 count = 0;
    nj_status st = nj_m3g_objects_probe(buf, n, &count, NULL, 0u, err, sizeof(err));
    checks++;
    if (st == NJ_OK) { failures++; printf("  FAIL  %s: accepted\n", what); return; }
    if (mentions && !strstr(err, mentions)) {
        failures++;
        printf("  FAIL  %s: said \"%s\", which does not mention \"%s\"\n",
               what, err, mentions);
    }
}


/*
 * The object layer, with a compressed section.
 *
 * The headless port fills in the inflate hook, so a zlib section has to read
 * back exactly like a stored one - that is the entire point of the hook.  The
 * fixture is built here rather than checked in, and the compression is done
 * by zlib itself so the test cannot agree with a wrong idea of the format.
 */
static void objects_in_a_compressed_section(void)
{
    nj_port port;
    nj_vm *vm;
    nj_u1 raw[16], comp[128];
    uLongf clen = sizeof(comp);
    nj_u4 zlen, total, n, count = 0;
    nj_m3g_object objs[8];
    nj_status st;

    /* a Mesh with four bytes, then an external reference with two */
    raw[0] = 20u; raw[1] = 4u; raw[2] = 0u; raw[3] = 0u; raw[4] = 0u;
    raw[5] = 1u; raw[6] = 2u; raw[7] = 3u; raw[8] = 4u;
    raw[9] = 255u; raw[10] = 2u; raw[11] = 0u; raw[12] = 0u; raw[13] = 0u;
    raw[14] = 7u; raw[15] = 8u;
    if (compress(comp, &clen, raw, 16u) != Z_OK) {
        ok(0, "zlib compresses the fixture");
        return;
    }
    zlen = (nj_u4)clen;

    start();
    memcpy(buf, ident, sizeof(ident));
    section(12, 0u, 0u, 5u, 0x11u);              /* the file header section */
    total = 9u + zlen + 4u;
    buf[30] = 1u;
    put32le(total, 31);
    put32le(16u, 35);
    memcpy(buf + 39, comp, zlen);
    n = 39u + zlen + 4u;

    if (nj_port_headless(&port) != NJ_OK) { ok(0, "a headless port exists"); return; }
    vm = nj_vm_create(&port, 1u << 20);
    if (!vm) { ok(0, "a VM exists"); return; }

    st = nj_m3g_scan_objects(vm, buf, n, objs, 8u, &count, NULL, err, sizeof(err));
    ok(st == NJ_OK && count == 2u, "a compressed section yields its two objects");
    if (st == NJ_OK && count == 2u) {
        /* the fixture's first object holds 1 2 3 4, so its userID is 0x04030201 */
        ok(objs[0].type == 20u && objs[0].user_id == 0x04030201u,
           "an object's first four data bytes are its userID");
        ok(objs[1].type == 255u && objs[1].user_id == 0u,
           "an external reference is a bare string and has no userID");
    }

    port.inflate = NULL;
    st = nj_m3g_scan_objects(vm, buf, n, objs, 8u, &count, NULL, err, sizeof(err));
#if NJ_ENABLE_INFLATE
    ok(st == NJ_OK && count == 2u && objs[0].user_id == 0x04030201u,
       "built-in inflate decodes compressed objects without a port hook");
    put32le(17u, 35);
    st = nj_m3g_scan_objects(vm, buf, n, NULL, 0u, &count, NULL, err, sizeof(err));
    ok(st != NJ_OK, "built-in inflate rejects a mismatched decoded section length");
#else
    ok(st != NJ_OK, "without any decompressor a compressed section is refused");
#endif

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

/* A Texture2D has two transform flags, then twelve sampler bytes.  Use a
 * multi-byte image index and distinct sampler bytes to expose offset errors. */
static nj_u4 texture_object(int at, int transformed)
{
    int data = at + 5;
    int base = data + (transformed ? 54 : 14);
    nj_u4 size = object(at, 17u, transformed ? 66u : 26u, 0u);

    if (transformed) {
        buf[data + 12] = 1u;
        put32le(0x40000000u, data + 13);  /* translation: 2, -3, 4 */
        put32le(0xc0400000u, data + 17);
        put32le(0x40800000u, data + 21);
        put32le(0x3f800000u, data + 25);  /* scale: 1, 600, 1 */
        put32le(0x44160000u, data + 29);
        put32le(0x3f800000u, data + 33);
        put32le(0x42b40000u, data + 37);  /* angle: 90; axis: 0, 0, 1 */
        put32le(0x3f800000u, data + 49);
    }
    put32le(0x01020304u, base);
    buf[base + 4] = 0x12u; buf[base + 5] = 0x34u; buf[base + 6] = 0x56u;
    buf[base + 7] = 224u;                /* FUNC_MODULATE */
    buf[base + 8] = 240u;                /* WRAP_CLAMP */
    buf[base + 9] = 241u;                /* WRAP_REPEAT */
    buf[base + 10] = 208u;               /* FILTER_BASE_LEVEL */
    buf[base + 11] = 210u;               /* FILTER_LINEAR */
    return size;
}

static void texture_fields(void)
{
    nj_m3g_fields f;
    int transformed;

    for (transformed = 0; transformed <= 1; transformed++) {
        nj_u4 size = texture_object(0, transformed);
        nj_status st = nj_m3g_decode_object(buf + 5, size - 5u, 17u,
                                           &f, err, sizeof(err));
        ok(st == NJ_OK && f.decoded == 1u,
           transformed ? "a 66-byte component-transform Texture2D decodes"
                       : "a 26-byte untransformed Texture2D decodes");
        ok(f.nrefs == 1u && f.refs[0] == 0x01020304u,
           "Texture2D reads its image index after both transform flags");
        ok(f.a == 0xff123456u && f.b == 0xd0f1f0e0u && f.c == 210u,
           "Texture2D reads blend color, function, wrapping and filters after the image");
        ok(f.owned_data == NULL, "direct Texture2D decoding borrows its payload");
        if (transformed) {
            ok(f.data == buf + 18 && f.data_len == 40u,
               "Texture2D exposes exactly the forty component-transform bytes");
            if (f.data && f.data_len == 40u) {
                ok(memcmp(f.data, buf + 18, 40u) == 0 &&
                   le32_at(f.data + 12) == 0x3f800000u &&
                   le32_at(f.data + 16) == 0x44160000u &&
                   le32_at(f.data + 20) == 0x3f800000u,
                   "Texture2D preserves component scale 1, 600, 1");
            }
        } else {
            ok(f.data == NULL && f.data_len == 0u,
               "an untransformed Texture2D claims no component-transform bytes");
        }
    }
    /*
     * The general (4x4) transform: no component transform, the general flag,
     * a row-major 4x4, then the sampler.  Five Counter-Strike map textures are
     * the only objects in the corpus that take this path, and all five are
     * eighteen-plus-seventy-two bytes of this exact shape; the matrix below is
     * the 15x/-7 translation one from construction.m3g.
     */
    {
        nj_u4 size = object(0, 17u, 90u, 0u);
        int payload = 5, base = payload + 14;      /* matrix follows two flags */
        int sampler = payload + 78;
        nj_m3g_fields f;

        buf[payload + 12] = 0u;                    /* no component transform */
        buf[payload + 13] = 1u;                    /* general transform follows */
        put32le(0x41700000u, base);                /*  15 */
        put32le(0xc0e00000u, base + 12);           /* -7 in the translation */
        put32le(0x41700000u, base + 20);           /*  15 */
        put32le(0xc0e00000u, base + 28);           /*  -7 */
        put32le(0x3f800000u, base + 40);           /*   1 */
        put32le(0x3f800000u, base + 60);           /*   1 */
        put32le(0x0a0b0c0du, sampler);             /* image index */
        buf[sampler + 4] = 0x11u; buf[sampler + 5] = 0x22u;
        buf[sampler + 6] = 0x33u;
        buf[sampler + 7] = 224u;                   /* FUNC_MODULATE */
        buf[sampler + 8] = 240u;                   /* WRAP_CLAMP */
        buf[sampler + 9] = 241u;                   /* WRAP_REPEAT */
        buf[sampler + 10] = 208u;                  /* FILTER_BASE_LEVEL */
        buf[sampler + 11] = 210u;                  /* FILTER_LINEAR */

        ok(nj_m3g_decode_object(buf + payload, size - 5u, 17u, &f, err,
                                sizeof(err)) == NJ_OK && f.decoded == 1u,
           "a 90-byte general-transform Texture2D decodes");
        ok(f.data == buf + base && f.data_len == 64u,
           "and exposes exactly its sixty-four matrix bytes");
        if (f.data && f.data_len == 64u) {
            ok(le32_at(f.data) == 0x41700000u &&
               le32_at(f.data + 12) == 0xc0e00000u &&
               le32_at(f.data + 20) == 0x41700000u &&
               le32_at(f.data + 28) == 0xc0e00000u,
               "the 15x scale and -7 translation of the CS construction map survive");
        }
        ok(f.refs[0] == 0x0a0b0c0du && f.a == 0xff112233u && f.c == 210u,
           "the sampler after the matrix decodes too");
    }
}

/* The scanner frees its inflated scratch buffer before returning.  Assert
 * ownership before reading payloads, so a regression fails without a dangling
 * pointer read.  Both the index data and its strip-length tail must survive. */
static void compressed_geometry_payloads(void)
{
    nj_port port;
    nj_vm *vm;
    nj_m3g_object objs[3];
    nj_u1 raw[150], comp[256];
    uLongf zlen = sizeof(comp);
    nj_u4 raw_len, n, count = 0u, sections = 0u, i;
    nj_status st;
    int owns_strips, owns_transform;

    memset(objs, 0, sizeof(objs));
    raw_len = object(0, 11u, 43u, 0u);
    buf[17] = 130u;                     /* UInt16 indices */
    put32le(7u, 18);
    for (i = 0u; i < 7u; i++) {
        static const nj_u1 indices[7] = { 0u, 1u, 2u, 2u, 3u, 4u, 5u };
        buf[22u + 2u * i] = indices[i];
    }
    put32le(2u, 36);                    /* strip count after fourteen index bytes */
    put32le(3u, 40);
    put32le(4u, 44);
    raw_len += texture_object((int)raw_len, 0);
    raw_len += texture_object((int)raw_len, 1);
    memcpy(raw, buf, raw_len);
    if (compress(comp, &zlen, raw, raw_len) != Z_OK) {
        ok(0, "zlib compresses geometry and Texture2D fixtures");
        return;
    }

    start();
    section(12, 0u, 0u, 5u, 0u);
    section(30, 1u, raw_len, (nj_u4)zlen, 0u);
    memcpy(buf + 39, comp, zlen);
    n = 39u + (nj_u4)zlen + 4u;
    if (nj_port_headless(&port) != NJ_OK) { ok(0, "a headless port exists"); return; }
    vm = nj_vm_create(&port, 1u << 20);
    if (!vm) { ok(0, "a VM exists"); return; }

    st = nj_m3g_scan_objects(vm, buf, n, objs, 3u, &count, &sections,
                             err, sizeof(err));
    ok(st == NJ_OK && count == 3u && sections == 2u,
       "the actual scanner inflates a section with geometry and both Texture2D layouts");
    memset(buf, 0xa5, sizeof(buf));      /* the input container is no longer usable */
    if (st == NJ_OK && count == 3u) {
        const nj_m3g_fields *f = &objs[0].fields;
        ok(objs[0].type == 11u && f->decoded == 1u &&
           f->a == 130u && f->b == 7u && f->c == 2u,
           "the scanned TriangleStripArray retains encoding, index and strip counts");
        ok(f->data_len == 26u && f->ref_off == 31u,
           "TriangleStripArray payload includes indices, strip count and both lengths");
        owns_strips = f->owned_data != NULL && f->data == f->owned_data;
        ok(owns_strips, "TriangleStripArray owns its bytes after inflated scratch is freed");
        if (owns_strips && f->data_len == 26u) {
            ok(memcmp(f->data, raw + 22, 26u) == 0,
               "all TriangleStripArray payload bytes survive scanner return");
            ok(le32_at(f->data + 14) == 2u && le32_at(f->data + 18) == 3u &&
               le32_at(f->data + 22) == 4u,
               "retained TriangleStripArray data still contains strip count and lengths");
        }
        for (i = 1u; i < 3u; i++) {
            f = &objs[i].fields;
            ok(objs[i].type == 17u && f->decoded == 1u &&
               f->nrefs == 1u && f->refs[0] == 0x01020304u,
               "scanned Texture2D keeps its image reference after section release");
        }
        ok(objs[1].fields.data == NULL && objs[1].fields.owned_data == NULL &&
           objs[1].fields.data_len == 0u,
           "scanning an untransformed Texture2D invents no transform");
        f = &objs[2].fields;
        owns_transform = f->owned_data != NULL && f->data == f->owned_data;
        ok(owns_transform && f->data_len == 40u,
           "scanned Texture2D owns its forty transform bytes after scratch is freed");
        if (owns_transform && f->data_len == 40u) {
            ok(memcmp(f->data, raw + 97, 40u) == 0,
               "scanned Texture2D retains translation, scale, angle and axis exactly");
            ok(le32_at(f->data + 12) == 0x3f800000u &&
               le32_at(f->data + 16) == 0x44160000u &&
               le32_at(f->data + 20) == 0x3f800000u,
               "scanned Texture2D retains component scale 1, 600, 1");
        }
    }
    for (i = 0u; i < 3u; i++) {
        nj_host_free(vm, objs[i].fields.owned_data);
        nj_host_free(vm, objs[i].fields.extra);
    }
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

static void object_header_bounds(void)
{
    nj_u1 data[38];
    nj_m3g_fields f;
    memset(data, 0, sizeof(data));
    data[0] = 73u;
    data[4] = 1u; data[8] = 4u; /* one AnimationTrack */
    data[12] = 1u; data[16] = 9u; data[20] = 3u; /* one user parameter */
    data[24] = 0xa1u; data[25] = 0xa2u; data[26] = 0xa3u;
    data[27] = 1u; data[28] = 3u; data[30] = 2u; /* VertexArray: 2 x 3 bytes */
    memset(data + 32, 0x35, 6u);
    ok(nj_m3g_decode_object(data, sizeof(data), 20u, &f, err, sizeof(err)) == NJ_OK &&
       f.user_id == 73u && f.a == 1u && f.b == 3u && f.data_len == 6u &&
       f.data && f.data[0] == 0x35u,
       "animation and user parameters preserve VertexArray payload offsets");
    data[4] = 0xffu; data[5] = 0xffu; data[6] = 0xffu; data[7] = 0xffu;
    ok(nj_m3g_decode_object(data, sizeof(data), 20u, &f, err, sizeof(err)) == NJ_ERR_FORMAT,
       "overflowing animation count is rejected before reading indices");
    data[4] = 1u; data[5] = 0u; data[6] = 0u; data[7] = 0u;
    data[20] = 0xffu; data[21] = 0xffu; data[22] = 0xffu; data[23] = 0xffu;
    ok(nj_m3g_decode_object(data, sizeof(data), 20u, &f, err, sizeof(err)) == NJ_ERR_FORMAT,
       "oversized user parameter is rejected before advancing payload");
    data[12] = 2u; data[20] = 3u; data[21] = 0u; data[22] = 0u; data[23] = 0u;
    ok(nj_m3g_decode_object(data, 27u, 20u, &f, err, sizeof(err)) == NJ_ERR_FORMAT,
       "truncated user parameter table is rejected");
}

#if NJ_ENABLE_M3G
/* A stored ZIP entry exercises external resources through a real app. */
static void zip32(nj_u1 *p, nj_u4 v)
{
    p[0] = (nj_u1)v; p[1] = (nj_u1)(v >> 8);
    p[2] = (nj_u1)(v >> 16); p[3] = (nj_u1)(v >> 24);
}

/* All geometry and animation objects are referenced; only the Group is a
 * Loader root. File indices include the header object (first object is 2). */
static void referenced_roots(void)
{
    nj_u4 at = 39u, n, data;
    nj_port port;
    nj_vm *vm;
    nj_ref bytes, roots, group, mesh;
    nj_value args[2], ret;
    nj_status st;

    start();
    section(12, 0u, 5u, 5u, 0u);
    object(21, 0u, 0u, 0u);
    at += object((int)at, 19u, 12u, 0u); /* KeyframeSequence */
    at += object((int)at, 1u, 12u, 0u);  /* AnimationController */
    data = at + 5u;
    at += object((int)at, 2u, 24u, 0u);  /* AnimationTrack */
    put32le(2u, (int)data + 12); put32le(3u, (int)data + 16);
    put32le(256u, (int)data + 20);
    at += object((int)at, 21u, 48u, 0u); /* VertexBuffer */
    at += object((int)at, 11u, 21u, 0u); /* TriangleStripArray */
    at += object((int)at, 3u, 33u, 0u);  /* Appearance */
    data = at + 5u;
    at += object((int)at, 14u, 38u, 0u); /* Mesh */
    put32le(0xffffffffu, (int)data + 16);
    put32le(5u, (int)data + 22); put32le(1u, (int)data + 26);
    put32le(6u, (int)data + 30); put32le(7u, (int)data + 34);
    data = at + 5u;
    at += object((int)at, 9u, 34u, 0u);  /* Group with one animation track */
    put32le(73u, (int)data);
    put32le(1u, (int)data + 4); put32le(4u, (int)data + 8);
    put32le(1u, (int)data + 26); put32le(8u, (int)data + 30);
    n = at + 4u;
    buf[30] = 0u; put32le(n - 30u, 31); put32le(at - 39u, 35);
    put32le(0u, (int)at);
    if (nj_port_headless(&port) != NJ_OK) { ok(0, "root fixture port"); return; }
    vm = nj_vm_create(&port, 1u << 20);
    if (!vm) { ok(0, "root fixture VM"); return; }
    bytes = nj_root(vm, nj_array_new(vm, "B", n));
    ok(bytes != NULL, "root fixture byte array exists");
    if (bytes) {
        memcpy(bytes->u.bytes, buf, n);
        args[0].r = bytes; args[1].i = 0; ret.r = NULL;
        st = nj_call(vm, NULL, "javax/microedition/m3g/Loader", "load",
                     "([BI)[Ljavax/microedition/m3g/Object3D;", args, &ret);
        ok(st == NJ_OK && !vm->exception, "Loader accepts geometry with animation references");
        roots = st == NJ_OK ? nj_root(vm, ret.r) : NULL;
        ok(roots && nj_array_length(roots) == 1u,
           "referenced geometry, tracks, sequences and controllers are not Loader roots");
        group = roots && nj_array_length(roots) == 1u ? nj_array_get_ref(roots, 0u) : NULL;
        if (group) {
            st = nj_call(vm, group, "javax/microedition/m3g/Object3D", "getUserID", "()I", NULL, &ret);
            ok(st == NJ_OK && ret.i == 73, "the sole root retains the Group identity");
            st = nj_call(vm, group, "javax/microedition/m3g/Group", "getChildCount", "()I", NULL, &ret);
            ok(st == NJ_OK && ret.i == 1, "variable Object3D header preserves Group children");
            args[0].i = 0;
            st = nj_call(vm, group, "javax/microedition/m3g/Group", "getChild",
                         "(I)Ljavax/microedition/m3g/Node;", args, &ret);
            mesh = st == NJ_OK ? ret.r : NULL;
            ok(mesh != NULL, "Group still links its Mesh");
            if (mesh) {
                st = nj_call(vm, mesh, "javax/microedition/m3g/Mesh", "getVertexBuffer",
                             "()Ljavax/microedition/m3g/VertexBuffer;", NULL, &ret);
                ok(st == NJ_OK && ret.r != NULL, "Mesh retains its referenced VertexBuffer");
            }
        }
    }
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

static void overflow_roots(void)
{
    nj_u4 at = 39u, data, i, n, children = NJ_M3G_MAX_REFS + 1u;
    nj_port port;
    nj_vm *vm;
    nj_ref bytes, roots;
    nj_value args[2], ret;
    nj_status st;
    start();
    section(12, 0u, 5u, 5u, 0u); object(21, 0u, 0u, 0u);
    for (i = 0u; i < children; i++) { at += object((int)at, 9u, 26u, 0u); }
    data = at + 5u;
    at += object((int)at, 9u, 26u + 4u * children, 0u);
    put32le(children, (int)data + 22);
    for (i = 0u; i < children; i++) { put32le(i + 2u, (int)data + 26 + (int)i * 4); }
    n = at + 4u;
    buf[30] = 0u; put32le(n - 30u, 31); put32le(at - 39u, 35); put32le(0u, (int)at);
    if (nj_port_headless(&port) != NJ_OK) { ok(0, "overflow fixture port"); return; }
    vm = nj_vm_create(&port, 1u << 20);
    if (!vm) { ok(0, "overflow fixture VM"); nj_port_shutdown(&port); return; }
    bytes = nj_root(vm, nj_array_new(vm, "B", n));
    ok(bytes != NULL, "overflow fixture byte array exists");
    if (bytes) {
        memcpy(bytes->u.bytes, buf, n);
        args[0].r = bytes; args[1].i = 0; ret.r = NULL;
        st = nj_call(vm, NULL, "javax/microedition/m3g/Loader", "load",
                     "([BI)[Ljavax/microedition/m3g/Object3D;", args, &ret);
        roots = st == NJ_OK ? nj_root(vm, ret.r) : NULL;
        ok(st == NJ_OK && roots && nj_array_length(roots) == 1u,
           "Group overflow children are referenced, not additional Loader roots");
        if (roots && nj_array_length(roots) == 1u) {
            st = nj_call(vm, nj_array_get_ref(roots, 0u), "javax/microedition/m3g/Group",
                         "getChildCount", "()I", NULL, &ret);
            ok(st == NJ_OK && ret.i == (nj_s4)children, "Group keeps all overflow children");
        }
    }
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

static void external_container(void)
{
    static const char uri[] = "shared/textures.m3g";
    nj_u1 zip[1024];
    nj_u4 n, cd, end, crc, outer;
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_ref bytes, roots, image;
    nj_value args[2], ret;
    nj_status st;

    start();
    section(12, 0u, 5u, 5u, 0u);
    object(21, 0u, 0u, 0u);
    n = 30u + section(30, 0u, 53u, 53u, 0u);
    object(39, 10u, 48u, 0u);
    put32le(73u, 44);
    buf[56] = 99u; /* immutable RGB, no palette */
    put32le(3u, 58); put32le(2u, 62);
    put32le(18u, 70);
    memset(buf + 74, 0x35, 18u);
    memset(zip, 0, sizeof(zip));
    crc = (nj_u4)crc32(0u, buf, n);
    zip32(zip, 0x04034b50u); zip[4] = 20u;
    zip32(zip + 14, crc); zip32(zip + 18, n); zip32(zip + 22, n);
    zip[26] = (nj_u1)(sizeof(uri) - 1u);
    memcpy(zip + 30, uri, sizeof(uri) - 1u);
    memcpy(zip + 30 + sizeof(uri) - 1u, buf, n);
    cd = 30u + sizeof(uri) - 1u + n;
    zip32(zip + cd, 0x02014b50u); zip[cd + 4] = 20u; zip[cd + 6] = 20u;
    zip32(zip + cd + 16, crc); zip32(zip + cd + 20, n); zip32(zip + cd + 24, n);
    zip[cd + 28] = (nj_u1)(sizeof(uri) - 1u);
    memcpy(zip + cd + 46, uri, sizeof(uri) - 1u);
    end = cd + 46u + sizeof(uri) - 1u;
    zip32(zip + end, 0x06054b50u); zip[end + 8] = 1u; zip[end + 10] = 1u;
    zip32(zip + end + 12, end - cd); zip32(zip + end + 16, cd);
    if (nj_port_headless(&port) != NJ_OK) { ok(0, "external-resource port"); return; }
    vm = nj_vm_create(&port, 1u << 20);
    if (!vm) { ok(0, "external-resource VM"); return; }
    app = nj_app_open(vm, zip, end + 22u);
    ok(app != NULL, "shared M3G resource opens as an application");
    if (!app) { nj_vm_destroy(vm); return; }
    start();
    section(12, 0u, 5u, 5u, 0u);
    object(21, 0u, 0u, 0u);
    outer = 30u + section(30, 0u, 5u + sizeof(uri), 5u + sizeof(uri), 0u);
    object(39, 255u, sizeof(uri), 0u);
    memcpy(buf + 44, uri, sizeof(uri));
    bytes = nj_root(vm, nj_array_new(vm, "B", outer));
    ok(bytes != NULL, "referring M3G byte array exists");
    if (bytes) {
        memcpy(bytes->u.bytes, buf, outer);
        args[0].r = bytes; args[1].i = 0;
        ret.r = NULL;
        st = nj_call(vm, NULL, "javax/microedition/m3g/Loader", "load",
                     "([BI)[Ljavax/microedition/m3g/Object3D;", args, &ret);
        ok(st == NJ_OK && !vm->exception, "Loader resolves an external M3G container");
        roots = st == NJ_OK ? nj_root(vm, ret.r) : NULL;
        ok(roots && nj_array_length(roots) == 1u, "external container returns its root");
        image = roots && nj_array_length(roots) ? nj_array_get_ref(roots, 0u) : NULL;
        ok(image != NULL, "external root is a live object");
        if (image) {
            st = nj_call(vm, image, "javax/microedition/m3g/Image2D", "getWidth", "()I", NULL, &ret);
            ok(st == NJ_OK && ret.i == 3, "external Image2D retains width");
            st = nj_call(vm, image, "javax/microedition/m3g/Image2D", "getHeight", "()I", NULL, &ret);
            ok(st == NJ_OK && ret.i == 2, "external Image2D retains height");
            st = nj_call(vm, image, "javax/microedition/m3g/Object3D", "getUserID", "()I", NULL, &ret);
            ok(st == NJ_OK && ret.i == 73, "external Image2D retains identity");
        }
    }
    nj_app_close(vm, app);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}
#endif

int main(void)
{
    nj_u4 clen;

    printf("test_m3g_file: the .m3g container and its objects\n");
    object_header_bounds();

    /* A file shaped like the real ones: identifier, one stored section. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    expect_ok(12u + clen, "a minimal well formed file");
    ok(ids[0] == 0u, "the section id comes back");
    ok(ids[1] == 0xffu, "unused id slots are padded, not left as garbage");

    /* Two sections, in order. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    section(12 + (int)clen, 1u, 5u, 5u, 0x22u);
    expect_ok(12u + clen + 9u + 5u + 4u, "two sections");
    ok(ids[0] == 0u && ids[1] == 1u, "both ids come back in file order");

    /* The identifier is the first thing a reader must believe. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    buf[3] = 0x00u;
    expect_bad(12u + clen, "not a JSR 184 file", "a wrong identifier");

    start();
    expect_bad(12u, "only", "a file with no sections at all");

    /* A section header cut short. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    expect_bad(12u + clen + 5u, "cut short", "a section header that runs off the end");

    /* A scheme that is not 0 or 1 must be refused: the spec reserves the rest
       and says a conforming loader reports an error. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    buf[12] = 2u;
    expect_bad(12u + clen, "compression scheme", "compression scheme 2");

    /* A section too small to hold its own header and a checksum. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    put32le(9u, 13);
    expect_bad(12u + clen, "smallest possible", "a section of nine bytes");

    /* A section longer than the file. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    put32le(64u, 13);
    expect_bad(12u + clen, "past the end", "a section longer than the file");

    /* An empty section is legal: the spec says the section is simply ignored.
       Rejecting it would refuse files that devices accept. */
    start();
    clen = section(12, 0u, 0u, 5u, 0x00u);
    expect_ok(12u + clen, "a section that declares nothing");
    ok(ids[0] == 0u, "an empty section still has an id");

    /* Sections that do not tile the file. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    expect_bad(12u + clen + 1u, NULL, "a trailing byte after the last section");

    /* The old, wrong reading of the section header: big endian lengths. */
    start();
    clen = section(12, 0u, 5u, 5u, 0x11u);
    buf[13] = 0u; buf[14] = 0u; buf[15] = 0u; buf[16] = (nj_u1)clen;
    expect_bad(12u + clen, NULL, "big endian section lengths");

    /* No bytes at all. */
    checks++;
    if (nj_m3g_container_probe(NULL, 0u, NULL, 0u, err, sizeof(err)) == NJ_OK) {
        failures++;
        printf("  FAIL  a null pointer was accepted\n");
    }

    /* More sections than id slots: nothing is written past the caller's array. */
    start();
    clen = section(12, 0u, 0u, 5u, 0x00u);
    {
        nj_u4 at = 12u + clen;
        at += section((int)at, 1u, 0u, 5u, 0x00u);
        at += section((int)at, 1u, 0u, 5u, 0x00u);
        expect_ok(at, "three sections");
    }
    ok(ids[0] == 0u && ids[1] == 1u && ids[2] == 1u && ids[3] == 0xffu,
       "three ids fit and the rest are padded");

    /*
     * The object stream.  This is the layer that was wrong for a whole round:
     * the length field was read as including its own five byte header, so the
     * walk drifted five bytes further out of step with every object, and 36859
     * real objects came back as nonsense types.
     */
    printf("test_m3g_file: the objects inside a section\n");

    /* One Mesh with three bytes of data: 5 + 3 = 8 bytes of stream. */
    start();
    object(0, 14u, 3u, 0xaau);
    obj_ok(8u, 1u, "one object");

    /* Two objects back to back, the second an external reference. */
    start();
    clen = object(0, 14u, 3u, 0xaau);
    object((int)clen, 255u, 0u, 0x00u);
    obj_ok(clen + 5u, 2u, "two objects");

    /* An object whose data runs past the end of the stream. */
    start();
    object(0, 14u, 3u, 0xaau);
    put32le(64u, 1);
    obj_bad(8u, "past the end", "an object longer than its section");

    /* Reserved object types are errors, not "unknown objects to skip". */
    start();
    object(0, 23u, 3u, 0xaau);
    obj_bad(8u, "reserved", "object type 23");

    /* A header cut short at the end of the stream. */
    start();
    object(0, 14u, 3u, 0xaau);
    obj_bad(10u, "cut short", "a truncated object header");

    /* An empty object stream is a section with no objects in it. */
    start();
    obj_ok(0u, 0u, "no objects at all");

    /* The type bytes come back in order, which is what the loader needs to
       build its object table before it decodes anything. */
    start();
    {
        nj_u1 types[4];
        nj_u4 count = 0;
        clen = object(0, 14u, 1u, 0x01u);
        object((int)clen, 9u, 1u, 0x02u);
        checks++;
        if (nj_m3g_objects_probe(buf, clen + 6u, &count, types, 4u,
                                 err, sizeof(err)) != NJ_OK ||
            count != 2u || types[0] != 14u || types[1] != 9u) {
            failures++;
            printf("  FAIL  the object types did not come back in order\n");
        }
    }

    objects_in_a_compressed_section();
#if NJ_ENABLE_M3G
    external_container();
    referenced_roots();
    overflow_roots();
#else
    ok(nj_lib_m3g_class_count == 0, "disabled M3G registers no classes");
    ok(nj_lib_m3g_count == 0, "disabled M3G registers no natives");
    ok(nj_m3g_late_init(NULL) == NJ_OK, "disabled M3G late init is a no-op");
#endif
    texture_fields();
    compressed_geometry_payloads();

    /* ---- the object layer: fields read out of a decoded object ---------- */
    {
        /*
         * The offsets here are the ones the corpus agrees on: the decoder was
         * written against 36859 objects from 975 real files, and these three
         * are the shapes it was measured from.
         */
        nj_u1 pm[12u + 6u];
        nj_m3g_fields f;
        char err[128];

        memset(pm, 0, sizeof(pm));
        pm[12] = 162u; pm[13] = 165u; pm[14] = 168u;   /* NONE, SMOOTH, CCW */
        ok(nj_m3g_decode_object(pm, 12u + 3u, 8u, &f, err, sizeof(err)) == NJ_OK, "a PolygonMode decodes");
        ok(f.a == 162u && f.b == 165u && f.c == 168u, "its culling, shading and winding come out");
        ok(f.d == 0u, "a payload that ends after the enums says the flags were absent");

        /*
         * The shape every real file has: eighteen bytes, the three
         * enumerations then two-sided lighting, local camera lighting and
         * perspective correction.  Measured over the corpus: all 1513
         * PolygonMode objects do this, bytes 15..17 are only 0 or 1, and
         * two-sided is set on 43 of them.
         */
        memset(pm, 0, sizeof(pm));
        pm[12] = 162u; pm[13] = 165u; pm[14] = 168u;
        pm[15] = 1u; pm[16] = 1u; pm[17] = 0u;
        ok(nj_m3g_decode_object(pm, sizeof(pm), 8u, &f, err, sizeof(err)) == NJ_OK,
           "an eighteen-byte PolygonMode (the real files' shape) decodes");
        ok(f.a == 162u && f.b == 165u && f.c == 168u,
           "and the enumerations in front of the flags still come out");
        ok(f.d == 1u && f.f[0] == 1.0f && f.f[1] == 1.0f && f.f[2] == 0.0f,
           "its two-sided, local-camera and perspective flags come out");

        pm[13] = 200u;                                  /* not a shading mode */
        ok(nj_m3g_decode_object(pm, sizeof(pm), 8u, &f, err, sizeof(err)) != NJ_OK, "a shading mode that does not exist is refused");

        {
            /*
             * An Image2D is thirty bytes of header and then its pixels: the
             * format at 12, the width a u32 at 14, the height a u32 at 18 and
             * the palette size at 22.  A shape that does not consume the
             * object exactly is left undecoded rather than refused, so an
             * untextured surface still draws - the corpus check counts a
             * refusal as a failure.
             */
            nj_u1 img[30u + 4u * 2u * 3u];
            memset(img, 0, sizeof(img));
            img[12] = 99u;                  /* RGB */
            img[14] = 4u;                   /* width, u32 at 14 */
            img[18] = 2u;                   /* height, u32 at 18 */
            ok(nj_m3g_decode_object(img, sizeof(img), 10u, &f, err, sizeof(err)) == NJ_OK, "an Image2D decodes");
            ok(f.decoded == 1u && f.a == 99u && f.b == 4u && f.c == 2u, "its format, width and height come out");
            ok(f.d == 0u, "and an image with no palette says so");
            {
                nj_u1 indexed[40] = {0};
                indexed[12] = 100u;
                indexed[14] = 2u; indexed[18] = 1u;
                indexed[22] = 8u;
                indexed[26] = 17u; indexed[27] = 34u;
                indexed[28] = 51u; indexed[29] = 255u;
                indexed[30] = 68u; indexed[31] = 85u;
                indexed[32] = 102u; indexed[33] = 128u;
                indexed[34] = 2u; /* pixel byte count follows the palette */
                indexed[38] = 0u; indexed[39] = 1u;
                ok(nj_m3g_decode_object(indexed, sizeof(indexed), 10u, &f, err, sizeof(err)) == NJ_OK && f.decoded,
                   "a paletted RGBA image decodes");
                ok(f.d == 8u && f.data == indexed + 26u &&
                   f.data[0] == 17u && f.data[7] == 128u &&
                   f.data[f.d + 4u] == 0u && f.data[f.d + 5u] == 1u,
                   "palette bytes precede the pixel count and indices");
            }
            img[22] = 3u;                   /* a palette that is not whole texels */
            memset(&f, 0, sizeof(f));
            ok(nj_m3g_decode_object(img, sizeof(img), 10u, &f, err, sizeof(err)) == NJ_OK &&
               f.decoded == 0u, "a palette that is not a whole number of texels is left undecoded");
        }
        {
            nj_u1 va[12u + 4u];
            memset(va, 0, sizeof(va));
            va[12] = 2u; va[13] = 3u; va[14] = 1u;      /* 2 bytes, 3 each, SHORT */
            ok(nj_m3g_decode_object(va, sizeof(va), 20u, &f, err, sizeof(err)) == NJ_OK, "a VertexArray decodes");
            ok(f.a == 2u && f.b == 3u && f.c == 1u, "its size, count and type come out");
            va[12] = 3u;                                /* not 1, 2 or 4 bytes */
            ok(nj_m3g_decode_object(va, sizeof(va), 20u, &f, err, sizeof(err)) != NJ_OK, "a component size that is not 1, 2 or 4 is refused");
        }
    }

    /*
     * A Group's children are the last thing in its data: a UInt32 count and
     * then that many UInt32 object indices.  Node's own fields sit in front of
     * them and their length depends on which transform representation the file
     * chose, so the tail is located by requiring the count and the indices to
     * consume the data exactly - this is the layout the loader needs to build
     * a scene graph instead of handing the game null children.
     */
    {
        nj_m3g_fields f;
        nj_u1 g[12u + 4u + 8u];
        memset(g, 0, sizeof(g));
        g[16] = 3u;                     /* first child: object index 3 */
        g[20] = 7u;                     /* second child: object index 7 */
        g[12] = 2u;                     /* and there are two of them */
        ok(nj_m3g_decode_object(g, sizeof(g), 9u, &f, err, sizeof(err)) == NJ_OK,
           "a Group decodes");
        ok(f.nrefs == 2u && f.ref_off == 16u && f.nrefs_total == 2u,
           "its two children are found");
        ok(le32_at(g + f.ref_off) == 3u && le32_at(g + f.ref_off + 4u) == 7u,
           "and they are the indices the file holds");
        /* the loader needs the numbers after the file bytes are gone, so they
           are copied into the object rather than pointed at */
        ok(f.refs[0] == 3u && f.refs[1] == 7u,
           "the indices travel with the object, not with the bytes");
    }
    {
        /* a tail that cannot be a child list is not an error: the group simply
           has no children we can locate, which draws nothing rather than
           refusing a file whose other objects are fine */
        nj_m3g_fields f;
        nj_u1 g[12u + 5u];
        memset(g, 0, sizeof(g));
        ok(nj_m3g_decode_object(g, sizeof(g), 9u, &f, err, sizeof(err)) == NJ_OK,
           "a Group whose children cannot be located still decodes");
        ok(f.nrefs == 0u, "with no children claimed");
    }

    /* ---- a VertexArray's bytes are reachable --------------------------- */
    {
        /*
         * The rasteriser needs the geometry, and the geometry was the one part
         * of a VertexArray the decoder did not expose: the three header bytes
         * say how the bytes are to be read, a UInt16 vertex count follows
         * them, and the bytes themselves run from there to the end of the
         * object.  Measured across the corpus - 5,210 of 5,210 VertexArrays
         * in 975 real files fit this reading and consume the object exactly -
         * before being relied on.
         *
         * This test used to place the bytes at offset 15, one field too early,
         * which is how the decoder was written at first: every value then sat
         * half a component out of step with the vertex it belonged to.
         */
        nj_u1 va[12 + 5 + 8];
        nj_m3g_fields f;
        char err[128];
        int i;

        memset(va, 0, sizeof(va));
        va[12] = 2u;    /* componentSize  */
        va[13] = 3u;    /* componentCount */
        va[14] = 1u;    /* encoding       */
        va[15] = 3u;    /* vertexCount, a UInt16 */
        va[16] = 0u;
        for (i = 0; i < 8; i++) { va[17 + i] = (nj_u1)(0x40 + i); }

        memset(&f, 0, sizeof(f));
        ok(nj_m3g_decode_object(va, sizeof(va), 20u, &f, err, sizeof(err)) == NJ_OK,
              "a VertexArray carrying geometry decodes");
        ok(f.a == 2u && f.b == 3u && f.c == 1u,
              "its component size, count and type are read");
        ok(f.data_len == 8u, "its bytes run to the end of the object");
        ok(f.data != NULL && f.data[0] == 0x40u && f.data[7] == 0x47u,
              "and they are the bytes that were in the object");
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
