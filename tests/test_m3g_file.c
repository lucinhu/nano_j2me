/*
 * nano_j2me - test_m3g_file.c
 *
 * The .m3g container is the one part of JSR 184 that can be checked without
 * putting a game file in the repository: every fixture below is built out of
 * bytes right here, so "truncated section" is a test case instead of a hope.
 *
 * The layout these fixtures encode was read off 975 real .m3g files collected
 * under tests/games/, not off a specification.  The first version of this file
 * encoded the opposite - a version byte at offset 0 and big endian section
 * lengths - and it passed, because a test written from the same wrong memory
 * as the code agrees with it.  tests/test_m3g_file_reals.sh is what catches
 * that class of mistake: it runs the probe over every real file.
 *
 * What is being asked is narrow on purpose.  The container says two things -
 * "I am a JSR 184 file" and "here is a chain of sections that tiles me exactly"
 * - and a reader that does not check both will hand the payload to the object
 * parser and let the object parser report the damage, one layer away from the
 * cause.
 */
#include <stdio.h>
#include <string.h>
#include "nj2me/nj.h"

/* Declared here rather than in a header: it is called by this test and by the
   loader, and nothing else in the project has any business reading it. */
nj_status nj_m3g_container_probe(const nj_u1 *p, nj_u4 n,
                                 nj_u1 *ids, nj_u4 id_count,
                                 char *err, nj_u4 errcap);

static int checks, failures;
static char err[256];
static nj_u1 buf[512];
static nj_u1 ids[8];

/* The twelve bytes every real file starts with. */
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

static void put32be(nj_u4 v, int at)
{
    buf[at + 0] = (nj_u1)(v >> 24);
    buf[at + 1] = (nj_u1)(v >> 16);
    buf[at + 2] = (nj_u1)(v >> 8);
    buf[at + 3] = (nj_u1)v;
}

/* One section: id, compressed length, uncompressed length, payload bytes. */
static void section(int at, nj_u1 id, nj_u4 clen, nj_u4 ulen, nj_u1 fill)
{
    int i;
    buf[at] = id;
    put32le(clen, at + 1);
    put32le(ulen, at + 5);
    for (i = 9; i < (int)clen; i++) { buf[at + i] = fill; }
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

int main(void)
{
    printf("test_m3g_file: the .m3g container\n");

    /* A file that is exactly what the real ones are: identifier, then one
       section whose compressed length includes its own nine byte header. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    expect_ok(42u, "a minimal well formed file");
    ok(ids[0] == 0u, "the section id comes back");
    ok(ids[1] == 0xffu, "unused id slots are padded, not left as garbage");

    /* Two sections, in order. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    section(42, 1u, 20u, 9u, 0x22u);
    expect_ok(62u, "two sections");
    ok(ids[0] == 0u && ids[1] == 1u, "both ids come back in file order");

    /* The identifier is the first thing a reader must believe. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    buf[3] = 0x00u;
    expect_bad(42u, "not a JSR 184 file", "a wrong identifier");

    /* ...and a file too short to hold one is a different answer. */
    start();
    expect_bad(12u, "only", "a file with no sections at all");

    /* A section header that is itself cut short: the first section is whole
       and four bytes of the next one are there.  A file with fewer than
       thirty bytes never gets this far - it is refused as too short, which is
       a different and more useful answer. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    expect_bad(46u, "cut short", "a section header that runs off the end");

    /* A length that cannot even cover the header it lives in. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    put32le(4u, 13);
    expect_bad(42u, "header included", "a section shorter than its own header");

    /* A length that runs past the end of the file. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    put32le(64u, 13);
    expect_bad(42u, "past the end", "a section longer than the file");

    /* A section that declares nothing to inflate into. */
    start();
    section(12, 0u, 30u, 0u, 0x11u);
    expect_bad(42u, "nothing to inflate", "an empty uncompressed length");

    /* One byte left over after the last section is the same answer, and it is
       the right one: what is left cannot be a section, so the file does not
       tile and the reader says which offset it got stuck at. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    expect_bad(43u, "cut short", "a trailing byte after the last section");

    /* The old, wrong reading of this format: big endian lengths.  A reader
       that quietly accepted these would accept files no device makes. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    put32be(30u, 13);
    put32be(17u, 17);
    expect_bad(42u, NULL, "big endian section lengths");

    /* No bytes at all. */
    checks++;
    if (nj_m3g_container_probe(NULL, 0u, NULL, 0u, err, sizeof(err)) == NJ_OK) {
        failures++;
        printf("  FAIL  a null pointer was accepted\n");
    }

    /* The loader asks with no room for ids; that must not be a crash. */
    start();
    section(12, 0u, 30u, 17u, 0x11u);
    checks++;
    if (nj_m3g_container_probe(buf, 42u, NULL, 0u, err, sizeof(err)) != NJ_OK) {
        failures++;
        printf("  FAIL  a caller that wants no ids was refused\n");
    }

    /* More sections than id slots: the first are reported, the rest dropped,
       and nothing is written past the caller's array. */
    start();
    section(12, 0u, 14u, 5u, 0x11u);
    section(26, 1u, 14u, 5u, 0x22u);
    section(40, 2u, 14u, 5u, 0x33u);
    expect_ok(54u, "three sections");
    ok(ids[0] == 0u && ids[1] == 1u && ids[2] == 2u && ids[3] == 0xffu,
       "three ids fit and the rest are padded");

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
