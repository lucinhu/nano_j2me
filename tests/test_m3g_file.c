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
#include "nj2me/nj.h"

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
static nj_u1 buf[512];
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
}

int main(void)
{
    nj_u4 clen;

    printf("test_m3g_file: the .m3g container and its objects\n");

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
    texture_fields();
    compressed_geometry_payloads();

    /* ---- the object layer: fields read out of a decoded object ---------- */
    {
        /*
         * The offsets here are the ones the corpus agrees on: the decoder was
         * written against 36859 objects from 975 real files, and these three
         * are the shapes it was measured from.
         */
        nj_u1 pm[12u + 3u];
        nj_m3g_fields f;
        char err[128];

        memset(pm, 0, sizeof(pm));
        pm[12] = 162u; pm[13] = 165u; pm[14] = 168u;   /* NONE, SMOOTH, CCW */
        ok(nj_m3g_decode_object(pm, sizeof(pm), 8u, &f, err, sizeof(err)) == NJ_OK, "a PolygonMode decodes");
        ok(f.a == 162u && f.b == 165u && f.c == 168u, "its culling, shading and winding come out");

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
