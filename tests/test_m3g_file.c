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
#include "nj2me/nj.h"

nj_status nj_m3g_container_probe(const nj_u1 *p, nj_u4 n,
                                 nj_u1 *ids, nj_u4 id_count,
                                 char *err, nj_u4 errcap);
nj_status nj_m3g_objects_probe(const nj_u1 *p, nj_u4 n, nj_u4 *count,
                               nj_u1 *types, nj_u4 type_count,
                               char *err, nj_u4 errcap);

static int checks, failures;
static char err[256];
static nj_u1 buf[512];
static nj_u1 ids[8];

static const nj_u1 ident[12] = {
    0xabu, 0x4au, 0x53u, 0x52u, 0x31u, 0x38u,
    0x34u, 0xbbu, 0x0du, 0x0au, 0x1au, 0x0au
};

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

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
