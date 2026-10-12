/*
 * nano_j2me - test_m3g_file.c
 *
 * The .m3g container is the one part of JSR 184 that can be checked without
 * putting a game file in the repository: every fixture below is built out of
 * bytes right here, so "truncated section" is a test case instead of a hope.
 *
 * What is being asked is narrow on purpose.  The container says two things -
 * "I am a .m3g version N and I am exactly this many bytes", and "here is a
 * chain of sections that tiles me" - and a reader that does not check both
 * will hand the payload to the object parser and let the object parser report
 * the damage, one layer away from the cause.
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

static void put32(nj_u4 v, int at)
{
    buf[at + 0] = (nj_u1)(v >> 24);
    buf[at + 1] = (nj_u1)(v >> 16);
    buf[at + 2] = (nj_u1)(v >> 8);
    buf[at + 3] = (nj_u1)v;
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

/* A header that is truthful about the length of the buffer it describes. */
static void header(nj_u1 version, nj_u4 total)
{
    memset(buf, 0, sizeof(buf));
    buf[0] = version;
    put32(total, 2);
}

int main(void)
{
    printf("test_m3g_file: the .m3g container\n");

    header(0x10u, 12u);
    expect_ok(12u, "a header with no sections at all");

    header(0x11u, 12u);
    expect_ok(12u, "version 1.1");

    header(0x10u, 12u);
    expect_bad(11u, "header", "a file shorter than its own header");

    header(0x12u, 12u);
    expect_bad(12u, "0x12", "a version we do not know");

    header(0x10u, 20u);
    expect_bad(12u, "20", "a header that overstates the file");

    /* one section, id 1, five bytes: id + length, empty payload */
    header(0x10u, 17u);
    buf[12] = 0x01u; put32(5u, 13);
    expect_ok(17u, "one empty section");
    ok(ids[0] == 0x01u, "the section id is reported back");
    ok(ids[1] == 0xffu, "and the unused entries are filled with 0xff");

    /* two sections that tile exactly */
    header(0x10u, 25u);
    buf[12] = 0x01u; put32(8u, 13);          /* 5 header + 3 payload */
    buf[20] = 0x20u; put32(5u, 21);
    expect_ok(25u, "two sections that tile the file exactly");
    ok(ids[0] == 0x01u && ids[1] == 0x20u, "both ids come back in order");

    /* a length that cannot fit its own header */
    /* 17 bytes so that five are available at offset 12: the length itself is
       the thing under test, not the room left for it. */
    header(0x10u, 17u);
    buf[12] = 0x01u; put32(4u, 13);
    expect_bad(17u, "five byte header", "a section length below 5");

    /* a length that runs off the end */
    header(0x10u, 20u);
    buf[12] = 0x03u; put32(100u, 13);
    expect_bad(20u, "100", "a section that overruns the file");

    /* a section header split by the end of the file */
    header(0x10u, 15u);
    buf[12] = 0x01u; buf[13] = 0u; buf[14] = 0u;
    expect_bad(15u, "offset 12", "a section header cut in half");

    /* no bytes at all */
    checks++;
    if (nj_m3g_container_probe(NULL, 0, NULL, 0, err, sizeof(err)) == NJ_OK) {
        failures++; printf("  FAIL  a null pointer was accepted\n");
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
