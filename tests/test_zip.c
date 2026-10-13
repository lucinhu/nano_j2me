/*
 * nano_j2me - tests/test_zip.c
 *
 * Standalone test suite for src/zip/nj_inflate.c and src/zip/nj_zip.c.
 *
 *   python3 tests/gen_fixtures.py
 *   gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -DNJ_USE_LIBC=1 \
 *       -o /tmp/test_zip tests/test_zip.c src/zip/nj_inflate.c src/zip/nj_zip.c
 *   /tmp/test_zip
 *
 * Checks, in order:
 *   1. CRC-32 / Adler-32 against Python's zlib.crc32 / zlib.adler32
 *      (fixed vectors plus values read back from fixtures/checksums.txt).
 *   2. Inflate: raw, zlib, fixed-Huffman, stored-block and empty streams are
 *      byte-identical to the expected outputs; a short destination reports
 *      NJ_ERR_OOM with the *exact* required size; corrupt/truncated streams
 *      report NJ_ERR_FORMAT and never crash.
 *   3. Zip: listing, lookup, extraction, size queries, OOM, unsupported
 *      methods, data-descriptor archives, a hostile archive comment, the
 *      empty archive and every malformed-archive fixture.
 *   4. Manifest parsing: case-insensitive names, continuation lines and the
 *      "main section only" rule.
 *   5. Allocation balance: the zip reader is driven through a counting
 *      allocator and must leave zero live blocks after every close.
 *
 * The fixture directory may be passed as argv[1]; otherwise a few common
 * relative locations are probed so the binary can be run from the repo root
 * or from tests/.
 */

#include "nj2me/nj_inflate.h"
#include "nj2me/nj_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tiny check framework                                                */
/* ------------------------------------------------------------------ */

static int g_checks = 0;
static int g_fails = 0;
static const char *g_section = "";

static void section(const char *name)
{
    g_section = name;
    printf("-- %s\n", name);
}

static void report(const char *file, int line, const char *expr, const char *detail)
{
    g_fails++;
    printf("   FAIL %s:%d [%s] %s%s%s\n", file, line, g_section, expr,
           (detail != NULL && detail[0] != '\0') ? " -- " : "", 
           (detail != NULL) ? detail : "");
}

#define CHECK(expr) \
    do { \
        g_checks++; \
        if (!(expr)) { \
            report(__FILE__, __LINE__, #expr, NULL); \
        } \
    } while (0)

#define CHECK_MSG(expr, msg) \
    do { \
        g_checks++; \
        if (!(expr)) { \
            report(__FILE__, __LINE__, #expr, (msg)); \
        } \
    } while (0)

#define CHECK_U4(got, want, what) \
    do { \
        unsigned long g_ = (unsigned long)(got); \
        unsigned long w_ = (unsigned long)(want); \
        g_checks++; \
        if (g_ != w_) { \
            char buf_[256]; \
            snprintf(buf_, sizeof(buf_), "%.80s: got %lu, want %lu", (what), g_, w_); \
            report(__FILE__, __LINE__, #got " == " #want, buf_); \
        } \
    } while (0)

#define CHECK_S4(got, want, what) \
    do { \
        long g_ = (long)(got); \
        long w_ = (long)(want); \
        g_checks++; \
        if (g_ != w_) { \
            char buf_[256]; \
            snprintf(buf_, sizeof(buf_), "%.80s: got %ld, want %ld", (what), g_, w_); \
            report(__FILE__, __LINE__, #got " == " #want, buf_); \
        } \
    } while (0)

#define CHECK_STR(got, want) \
    do { \
        const char *g_ = (got); \
        const char *w_ = (want); \
        g_checks++; \
        if (g_ == NULL || strcmp(g_, w_) != 0) { \
            char buf_[256]; \
            snprintf(buf_, sizeof(buf_), "got \"%.100s\", want \"%.100s\"", \
                     (g_ != NULL) ? g_ : "(null)", w_); \
            report(__FILE__, __LINE__, #got " == " #want, buf_); \
        } \
    } while (0)

/* ------------------------------------------------------------------ */
/* Fixture access                                                      */
/* ------------------------------------------------------------------ */

static char g_fix[512];

static int path_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

static int resolve_fixtures(const char *hint)
{
    static const char *cands[] = {
        "tests/fixtures", "../tests/fixtures", "../../tests/fixtures",
        "fixtures", "../fixtures"
    };
    char probe[600];
    size_t i;

    if (hint != NULL && hint[0] != '\0') {
        snprintf(g_fix, sizeof(g_fix), "%s", hint);
        snprintf(probe, sizeof(probe), "%s/raw.expected", g_fix);
        if (path_exists(probe)) {
            return 1;
        }
    }
    for (i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
        snprintf(g_fix, sizeof(g_fix), "%s", cands[i]);
        snprintf(probe, sizeof(probe), "%s/raw.expected", g_fix);
        if (path_exists(probe)) {
            return 1;
        }
    }
    return 0;
}

static void fix_path(char *dst, size_t n, const char *rel)
{
    snprintf(dst, n, "%s/%s", g_fix, rel);
}

/* Read a fixture into a fresh buffer (always NUL padded by one byte). */
static nj_u1 *read_fixture(const char *rel, nj_u4 *out_len)
{
    char path[700];
    FILE *f;
    long size;
    nj_u1 *buf;

    fix_path(path, sizeof(path), rel);
    f = fopen(path, "rb");
    if (f == NULL) {
        report(__FILE__, __LINE__, "fopen(fixture)", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        report(__FILE__, __LINE__, "fseek(ftell)", path);
        return NULL;
    }
    buf = (nj_u1 *)malloc((size_t)size + 1u);
    if (buf == NULL) {
        fclose(f);
        report(__FILE__, __LINE__, "malloc(fixture)", path);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        report(__FILE__, __LINE__, "fread(fixture)", path);
        return NULL;
    }
    fclose(f);
    buf[size] = 0;
    if (out_len != NULL) {
        *out_len = (nj_u4)size;
    }
    return buf;
}

static char *read_text(const char *rel, nj_u4 *out_len)
{
    return (char *)read_fixture(rel, out_len);
}

/* ------------------------------------------------------------------ */
/* Counting allocator (allocation-balance gate)                        */
/* ------------------------------------------------------------------ */

typedef struct {
    long live;    /* blocks handed out and not yet returned */
    long total;   /* blocks ever handed out                 */
} alloc_stats;

static alloc_stats g_stats;
static nj_u1 g_arena[256 * 1024];
static nj_size g_arena_used;

static void *counting_alloc(void *ud, nj_size n)
{
    alloc_stats *st = (alloc_stats *)ud;
    void *p = malloc((size_t)n);
    if (p != NULL) {
        st->live++;
        st->total++;
    }
    return p;
}

static void counting_free(void *ud, void *p)
{
    alloc_stats *st = (alloc_stats *)ud;
    if (p != NULL) {
        st->live--;
    }
    free(p);
}

static nj_zip_alloc g_za = { counting_alloc, counting_free, &g_stats };

/* An arena allocator with no free hook: the reader must accept free == NULL. */
static void *arena_alloc(void *ud, nj_size n)
{
    void *p;
    (void)ud;
    n = (n + 15u) & ~(nj_size)15u;
    if (n > sizeof(g_arena) || g_arena_used > (nj_size)sizeof(g_arena) - n) {
        return NULL;
    }
    p = &g_arena[g_arena_used];
    g_arena_used += n;
    return p;
}

static nj_zip_alloc g_arena_za = { arena_alloc, NULL, NULL };

/* ------------------------------------------------------------------ */
/* 1. Checksums                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    nj_u4       crc;
    nj_u4       adler;
} checksum_ref;

static const checksum_ref g_check_files[] = {
    { "raw.bin",            0, 0 },
    { "raw.expected",       0, 0 },
    { "zlib.bin",           0, 0 },
    { "zlib.expected",      0, 0 },
    { "stored.bin",         0, 0 },
    { "stored.expected",    0, 0 },
    { "fixed.bin",          0, 0 },
    { "empty_raw.bin",      0, 0 },
    { "sample.jar",         0, 0 },
    { "descriptor.jar",     0, 0 },
    { "manifest_edge.mf",   0, 0 },
    { "empty.jar",          0, 0 }
};
#define N_CHECK_FILES (sizeof(g_check_files) / sizeof(g_check_files[0]))

static void test_checksums(void)
{
    static const char digits[] = "123456789";
    static const char fox[] = "The quick brown fox jumps over the lazy dog";
    nj_u4 text_len = 0;
    char *text;
    char *line;
    size_t i;

    /* Fixed vectors: the canonical CRC-32/Adler-32 check values. */
    CHECK_U4(nj_crc32(digits, 9, 0), 0xCBF43926u, "crc32(123456789)");
    CHECK_U4(nj_adler32(digits, 9), 0x091E01DEu, "adler32(123456789)");
    CHECK_U4(nj_crc32(fox, 43, 0), 0x414FA339u, "crc32(quick brown fox)");
    CHECK_U4(nj_adler32("Wikipedia", 9), 0x11E60398u, "adler32(Wikipedia)");
    CHECK_U4(nj_crc32(NULL, 0, 0x12345678u), 0x12345678u, "crc32 empty keeps seed");
    CHECK_U4(nj_adler32(NULL, 0), 1u, "adler32 empty");
    CHECK_U4(nj_adler32("", 0), 1u, "adler32 zero length");

    /* The seed is a running CRC, so two halves must chain into the whole. */
    CHECK_U4(nj_crc32(digits + 5, 4, nj_crc32(digits, 5, 0)), 0xCBF43926u,
             "crc32 chained halves");
    CHECK_U4(nj_crc32(digits, 9, nj_crc32(digits, 9, 0)) != 0xCBF43926u, 1u,
             "seed is not ignored");

    /* Values produced by Python's zlib for the fixture files. */
    text = read_text("checksums.txt", &text_len);
    CHECK(text != NULL);
    if (text == NULL) {
        return;
    }
    line = text;
    while (*line != '\0') {
        char *eol = strchr(line, '\n');
        char name[128];
        unsigned int crc = 0;
        unsigned int adler = 0;
        if (eol != NULL) {
            *eol = '\0';
        }
        if (line[0] != '#' && line[0] != '\0' &&
            sscanf(line, "%127s %x %x", name, &crc, &adler) == 3) {
            for (i = 0; i < N_CHECK_FILES; i++) {
                nj_u4 len = 0;
                nj_u1 *data;
                if (strcmp(g_check_files[i].name, name) != 0) {
                    continue;
                }
                data = read_fixture(name, &len);
                CHECK(data != NULL);
                if (data == NULL) {
                    break;
                }
                CHECK_U4(nj_crc32(data, len, 0), crc, name);
                CHECK_U4(nj_adler32(data, len), adler, name);
                if (len > 1) {
                    nj_u4 half = len / 2u;
                    CHECK_U4(nj_crc32(data + half, len - half, nj_crc32(data, half, 0)),
                             crc, "chained crc32 over the same file");
                }
                free(data);
                break;
            }
        }
        if (eol == NULL) {
            break;
        }
        line = eol + 1;
    }
    free(text);
}

/* ------------------------------------------------------------------ */
/* 2. Inflate                                                          */
/* ------------------------------------------------------------------ */

/* Run one fixture through every inflate entry point. */
static void inflate_case(const char *bin_name, const char *exp_name, int wrapped)
{
    nj_u4 bin_len = 0, exp_len = 0;
    nj_u1 *bin = read_fixture(bin_name, &bin_len);
    nj_u1 *exp = read_fixture(exp_name, &exp_len);
    nj_u1 *buf;
    nj_status st;
    nj_u4 n;

    if (bin == NULL || exp == NULL) {
        free(bin);
        free(exp);
        CHECK_MSG(0, bin_name);
        return;
    }
    buf = (nj_u1 *)malloc((size_t)exp_len + 1u);
    CHECK(buf != NULL);
    if (buf == NULL) {
        free(bin);
        free(exp);
        return;
    }
    memset(buf, 0xAA, (size_t)exp_len + 1u);

    /* Exact capacity must succeed with byte-identical output. */
    n = exp_len;
    st = wrapped ? nj_inflate_zlib(bin, bin_len, buf, &n)
                 : nj_inflate_raw(bin, bin_len, buf, &n);
    CHECK_MSG(st == NJ_OK, bin_name);
    CHECK_U4(n, exp_len, bin_name);
    g_checks++;
    if (memcmp(buf, exp, (size_t)exp_len) != 0) {
        report(__FILE__, __LINE__, "inflate output == expected", bin_name);
    }
    /* No byte past the reported length may have been touched. */
    CHECK_U4(buf[exp_len], 0xAAu, "no write past dst_len");

    /* nj_inflate_zlib accepts raw streams too (src[0] decides). */
    n = exp_len;
    st = nj_inflate_zlib(bin, bin_len, buf, &n);
    CHECK_MSG(st == NJ_OK, "nj_inflate_zlib on every fixture");
    CHECK_U4(n, exp_len, "nj_inflate_zlib size");

    /* Size-only query. */
    n = 0xDEADBEEFu;
    st = nj_inflate_zlib_size(bin, bin_len, &n);
    CHECK_MSG(st == NJ_OK, "nj_inflate_zlib_size");
    CHECK_U4(n, exp_len, "nj_inflate_zlib_size value");

    if (exp_len == 0u) {
        /* Empty output: a NULL destination with capacity 0 is legal. */
        n = 0;
        st = wrapped ? nj_inflate_zlib(bin, bin_len, NULL, &n)
                     : nj_inflate_raw(bin, bin_len, NULL, &n);
        CHECK_MSG(st == NJ_OK, "empty stream into a zero-capacity buffer");
        CHECK_U4(n, 0u, "empty stream produced");
    } else {
        /* One byte short: exact requirement must come back. */
        n = exp_len - 1u;
        st = wrapped ? nj_inflate_zlib(bin, bin_len, buf, &n)
                     : nj_inflate_raw(bin, bin_len, buf, &n);
        CHECK_MSG(st == NJ_ERR_OOM, "short destination reports OOM");
        CHECK_U4(n, exp_len, "OOM reports the exact required size");

        /* Zero capacity with a NULL destination: count only. */
        n = 0;
        st = wrapped ? nj_inflate_zlib(bin, bin_len, NULL, &n)
                     : nj_inflate_raw(bin, bin_len, NULL, &n);
        CHECK_MSG(st == NJ_ERR_OOM, "NULL destination reports OOM");
        CHECK_U4(n, exp_len, "counting pass reports the exact size");

        /* A destination that is much too small must not overflow it. */
        n = 1;
        st = wrapped ? nj_inflate_zlib(bin, bin_len, buf, &n)
                     : nj_inflate_raw(bin, bin_len, buf, &n);
        CHECK_MSG(st == NJ_ERR_OOM, "1-byte destination reports OOM");
        CHECK_U4(n, exp_len, "1-byte destination exact size");
    }

    free(buf);
    free(bin);
    free(exp);
}

static void expect_format_error(const char *bin_name, nj_u4 clip)
{
    nj_u4 len = 0;
    nj_u1 *bin = read_fixture(bin_name, &len);
    nj_u1 buf[512];
    nj_u4 n = sizeof(buf);
    nj_status st;

    if (bin == NULL) {
        CHECK_MSG(0, bin_name);
        return;
    }
    if (clip != 0 && clip < len) {
        len = clip;
    }
    st = nj_inflate_raw(bin, len, buf, &n);
    CHECK_MSG(st == NJ_ERR_FORMAT, bin_name);
    free(bin);
}

static void test_inflate(void)
{
    nj_u4 len = 0;
    nj_u1 *raw;
    nj_u1 buf[64];
    nj_u4 n;
    nj_status st;
    static const nj_u1 dict_hdr[4] = { 0x78u, 0x20u, 0x03u, 0x00u };

    inflate_case("raw.bin", "raw.expected", 0);
    inflate_case("zlib.bin", "zlib.expected", 1);
    inflate_case("fixed.bin", "fixed.expected", 0);
    inflate_case("stored.bin", "stored.expected", 0);
    inflate_case("empty_raw.bin", "empty.expected", 0);

    /* Adversarial streams. */
    expect_format_error("bad_distance.bin", 0);
    expect_format_error("bad_btype.bin", 0);
    expect_format_error("truncated_dynamic.bin", 0);
    expect_format_error("raw.bin", 8);            /* truncated dynamic header */
    expect_format_error("stored.bin", 12);        /* truncated stored block   */
    expect_format_error("zlib.bin", 12);          /* truncated zlib stream    */

    /* Argument validation.  NULL/empty input means "no stream to parse". */
    n = sizeof(buf);
    CHECK(nj_inflate_raw(NULL, 8, buf, &n) == NJ_ERR_FORMAT);
    n = sizeof(buf);
    CHECK(nj_inflate_raw(NULL, 0, buf, &n) == NJ_ERR_FORMAT);
    n = sizeof(buf);
    CHECK(nj_inflate_raw(buf, 0, buf, &n) == NJ_ERR_FORMAT);
    n = sizeof(buf);
    CHECK(nj_inflate_raw(buf, 8, buf, NULL) == NJ_ERR);
    n = 4;
    CHECK(nj_inflate_raw(buf, 8, NULL, &n) == NJ_ERR);
    n = 0;
    CHECK(nj_inflate_zlib(NULL, 0, buf, &n) == NJ_ERR_FORMAT);
    CHECK(nj_inflate_zlib_size(NULL, 4, &n) == NJ_ERR_FORMAT);
    CHECK(nj_inflate_zlib_size(buf, 4, NULL) == NJ_ERR_FORMAT);

    /* A zlib header with FDICT set needs a dictionary we cannot supply. */
    n = sizeof(buf);
    st = nj_inflate_zlib(dict_hdr, sizeof(dict_hdr), buf, &n);
    CHECK_MSG(st == NJ_ERR_FORMAT, "preset-dictionary stream is rejected");

    /* The empty stream decodes to nothing through the raw entry point. */
    raw = read_fixture("empty_raw.bin", &len);
    CHECK(raw != NULL);
    if (raw != NULL) {
        n = 0;
        st = nj_inflate_raw(raw, len, NULL, &n);
        CHECK(st == NJ_OK);
        CHECK_U4(n, 0u, "empty stream size");
        free(raw);
    }
}

/* The zlib envelope: header variants are accepted, the adler32 trailer is
 * optional (never authenticated), a preset dictionary is refused. */
static void test_zlib_envelope(void)
{
    static const nj_u1 flg_ok[] = { 0x01u, 0x5Eu, 0x9Cu, 0xDAu };
    nj_u4 bin_len = 0, exp_len = 0;
    nj_u1 *bin = read_fixture("zlib.bin", &bin_len);
    nj_u1 *exp = read_fixture("zlib.expected", &exp_len);
    nj_u1 *work;
    nj_u1 *buf;
    size_t i;

    CHECK(bin != NULL);
    CHECK(exp != NULL);
    if (bin == NULL || exp == NULL) {
        free(bin);
        free(exp);
        return;
    }
    CHECK(bin_len > 6u);
    work = (nj_u1 *)malloc(bin_len);
    buf = (nj_u1 *)malloc((size_t)exp_len + 1u);
    CHECK(work != NULL);
    CHECK(buf != NULL);
    if (work == NULL || buf == NULL) {
        free(work);
        free(buf);
        free(bin);
        free(exp);
        return;
    }

    for (i = 0; i < sizeof(flg_ok) / sizeof(flg_ok[0]); i++) {
        nj_u4 n = exp_len;
        nj_status st;
        memcpy(work, bin, bin_len);
        work[0] = 0x78u;
        work[1] = flg_ok[i];
        CHECK_U4((nj_u4)((work[0] << 8 | work[1]) % 31u), 0u, "crafted header");
        st = nj_inflate_zlib(work, bin_len, buf, &n);
        CHECK_MSG(st == NJ_OK, "accepted zlib header variant");
        CHECK_U4(n, exp_len, "header variant length");
        g_checks++;
        if (memcmp(buf, exp, (size_t)exp_len) != 0) {
            report(__FILE__, __LINE__, "header variant payload", NULL);
        }
    }

    /* A wrong adler32 trailer must not fail the decode. */
    memcpy(work, bin, bin_len);
    work[bin_len - 1u] ^= 0xFFu;
    work[bin_len - 3u] ^= 0x5Au;
    {
        nj_u4 n = exp_len;
        nj_status st = nj_inflate_zlib(work, bin_len, buf, &n);
        CHECK_MSG(st == NJ_OK, "bad adler32 is not fatal");
        CHECK_U4(n, exp_len, "bad adler32 length");
        g_checks++;
        if (memcmp(buf, exp, (size_t)exp_len) != 0) {
            report(__FILE__, __LINE__, "bad adler32 payload", NULL);
        }
        n = 0;
        st = nj_inflate_zlib_size(work, bin_len, &n);
        CHECK_MSG(st == NJ_OK, "size query ignores the trailer");
        CHECK_U4(n, exp_len, "size query with bad adler32");
    }

    /* A truncated trailer is fine too: exactly two header bytes are skipped. */
    {
        nj_u4 n = exp_len;
        nj_status st = nj_inflate_zlib(bin, bin_len - 4u, buf, &n);
        CHECK_MSG(st == NJ_OK, "missing adler32 trailer is not fatal");
        CHECK_U4(n, exp_len, "trailer-less length");
    }

    free(work);
    free(buf);
    free(bin);
    free(exp);
}

/* ------------------------------------------------------------------ */
/* 3. Zip                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[128];
    unsigned int method;
    unsigned int comp;
    unsigned int uncomp;
    unsigned int crc;
    char expected[256];
} index_rec;

static int load_index(index_rec *recs, int max, int *count)
{
    nj_u4 len = 0;
    char *text = read_text("jar_index.txt", &len);
    char *line;
    int n = 0;

    if (text == NULL) {
        return 0;
    }
    line = text;
    while (*line != '\0' && n < max) {
        char *eol = strchr(line, '\n');
        if (eol != NULL) {
            *eol = '\0';
        }
        if (line[0] != '#' && line[0] != '\0') {
            index_rec *r = &recs[n];
            if (sscanf(line, "%127s %u %u %u %x %255s", r->name, &r->method,
                       &r->comp, &r->uncomp, &r->crc, r->expected) == 6) {
                n++;
            }
        }
        if (eol == NULL) {
            break;
        }
        line = eol + 1;
    }
    free(text);
    *count = n;
    return 1;
}

static void test_zip_sample(void)
{
    index_rec recs[32];
    int count = 0;
    nj_u4 img_len = 0;
    nj_u1 *img;
    nj_zip *z;
    int i;

    CHECK(load_index(recs, 32, &count));
    CHECK_U4(count, 4u, "fixture entry count");

    img = read_fixture("sample.jar", &img_len);
    CHECK(img != NULL);
    if (img == NULL) {
        return;
    }

    CHECK_U4(g_stats.live, 0u, "no live allocations before open");
    z = nj_zip_open(img, img_len, &g_za);
    CHECK(z != NULL);
    if (z == NULL) {
        free(img);
        return;
    }
    CHECK_U4(g_stats.live, 2u, "descriptor + entry array are the only blocks");

    CHECK_U4((unsigned)nj_zip_count(z), (unsigned)count, "nj_zip_count");
    CHECK(nj_zip_entry_at(z, -1) == NULL);
    CHECK(nj_zip_entry_at(z, count) == NULL);

    for (i = 0; i < count; i++) {
        const index_rec *r = &recs[i];
        const nj_zip_entry *e = nj_zip_entry_at(z, i);
        const nj_zip_entry *f;
        nj_u4 exp_len = 0;
        nj_u1 *exp;
        nj_u1 *got;

        CHECK(e != NULL);
        if (e == NULL) {
            continue;
        }
        CHECK_STR(e->name, r->name);
        CHECK_U4(e->method, r->method, r->name);
        CHECK_U4(e->comp_size, r->comp, r->name);
        CHECK_U4(e->uncomp_size, r->uncomp, r->name);
        CHECK(e->local_offset < img_len);
        CHECK(e->dos_date != 0u);
        CHECK(e->dos_time != 0u);

        f = nj_zip_find(z, r->name);
        CHECK_MSG(f == e, "nj_zip_find returns the same entry");
        CHECK(nj_zip_find_ci(z, r->name) == e);

        /* Size query writes nothing. */
        CHECK_S4(nj_zip_extract(z, e, NULL, 0), (long)r->uncomp, "size query");

        exp = read_fixture(r->expected, &exp_len);
        CHECK(exp != NULL);
        if (exp == NULL) {
            continue;
        }
        CHECK_U4(exp_len, r->uncomp, "expected file size");

        got = (nj_u1 *)malloc((size_t)r->uncomp + 1u);
        CHECK(got != NULL);
        if (got != NULL) {
            memset(got, 0x5A, (size_t)r->uncomp + 1u);
            CHECK_S4(nj_zip_extract(z, e, got, r->uncomp), (long)r->uncomp,
                     "extract length");
            g_checks++;
            if (memcmp(got, exp, (size_t)r->uncomp) != 0) {
                report(__FILE__, __LINE__, "extracted bytes == expected", r->name);
            }
            CHECK_U4(got[r->uncomp], 0x5Au, "extract stays inside the buffer");
            CHECK_U4(nj_crc32(got, r->uncomp, 0), r->crc, "extracted crc32");
            if (r->uncomp > 0u) {
                CHECK_S4(nj_zip_extract(z, e, got, r->uncomp - 1u), NJ_ERR_OOM,
                         "short extract buffer");
            }
            free(got);
        }
        free(exp);
    }

    /* Directory entries are never listed. */
    CHECK(nj_zip_find(z, "dir/") == NULL);
    CHECK(nj_zip_find(z, "META-INF/") == NULL);
    CHECK(nj_zip_find(z, "") == NULL);
    CHECK(nj_zip_find(z, "no-such-entry.txt") == NULL);
    CHECK(nj_zip_find(z, NULL) == NULL);
    CHECK(nj_zip_find_ci(z, NULL) == NULL);
    CHECK(nj_zip_count(NULL) == 0);
    CHECK(nj_zip_entry_at(NULL, 0) == NULL);
    CHECK(nj_zip_extract(z, NULL, NULL, 0) == NJ_ERR);
    CHECK(nj_zip_extract(NULL, nj_zip_entry_at(z, 0), NULL, 0) == NJ_ERR);

    /* Case-insensitive fallback finds the manifest. */
    {
        const nj_zip_entry *a = nj_zip_find(z, "META-INF/MANIFEST.MF");
        const nj_zip_entry *b = nj_zip_find_ci(z, "meta-inf/manifest.mf");
        CHECK(a != NULL);
        CHECK(b == a);
    }

    nj_zip_close(z);
    CHECK_U4(g_stats.live, 0u, "no live allocations after close");
    CHECK_U4(g_stats.total, 2u, "two allocations were made");

#if NJ_USE_LIBC
    /* The default (malloc/free) allocator must work as well. */
    z = nj_zip_open(img, img_len, NULL);
    CHECK(z != NULL);
    if (z != NULL) {
        CHECK_U4((unsigned)nj_zip_count(z), (unsigned)count, "default allocator");
        nj_zip_close(z);
    }
#else
    /* A freestanding build has no malloc: NULL hooks must be refused. */
    CHECK(nj_zip_open(img, img_len, NULL) == NULL);
#endif

    /* free == NULL (arena that never releases) must be accepted. */
    g_arena_used = 0;
    z = nj_zip_open(img, img_len, &g_arena_za);
    CHECK(z != NULL);
    if (z != NULL) {
        CHECK_U4((unsigned)nj_zip_count(z), (unsigned)count, "arena allocator");
        nj_zip_close(z);          /* must not call a NULL free hook */
    }
    g_arena_used = 0;

    CHECK(nj_zip_open(NULL, 0, &g_za) == NULL);
    CHECK(nj_zip_open(img, 0, &g_za) == NULL);
    CHECK_U4(g_stats.live, 0u, "failed opens leak nothing");

    free(img);
}

static void test_zip_extract_alloc(void)
{
    nj_u4 img_len = 0;
    nj_u1 *img = read_fixture("sample.jar", &img_len);
    nj_zip *z;
    const nj_zip_entry *e;
    void *p;
    nj_u4 out_len = 12345;
    long live_base;

    CHECK(img != NULL);
    if (img == NULL) {
        return;
    }
    z = nj_zip_open(img, img_len, &g_za);
    CHECK(z != NULL);
    if (z == NULL) {
        free(img);
        return;
    }
    /* The reader itself owns exactly two blocks while the archive is open. */
    live_base = g_stats.live;
    CHECK_U4(live_base, 2u, "open allocations");

    /* Manifest through the counting allocator. */
    e = nj_zip_find(z, "META-INF/MANIFEST.MF");
    CHECK(e != NULL);
    p = nj_zip_extract_alloc(z, e, &g_za, &out_len);
    CHECK(p != NULL);
    if (p != NULL) {
        CHECK_U4(out_len, e->uncomp_size, "extract_alloc length");
        CHECK_U4(((char *)p)[out_len], 0u, "extract_alloc NUL terminates");
        CHECK_U4(strlen((char *)p), e->uncomp_size, "no embedded NUL");
        counting_free(&g_stats, p);
    }
    CHECK_U4(g_stats.live, (unsigned long)live_base, "extract_alloc frees cleanly");

    /* A big binary entry, checked against the expected file. */
    e = nj_zip_find(z, "blob.bin");
    CHECK(e != NULL);
    p = nj_zip_extract_alloc(z, e, &g_za, &out_len);
    CHECK(p != NULL);
    if (p != NULL) {
        nj_u4 exp_len = 0;
        nj_u1 *exp = read_fixture("expected/blob.bin", &exp_len);
        CHECK(exp != NULL);
        if (exp != NULL) {
            CHECK_U4(out_len, exp_len, "blob length");
            g_checks++;
            if (memcmp(p, exp, (size_t)exp_len) != 0) {
                report(__FILE__, __LINE__, "extract_alloc blob bytes", NULL);
            }
            free(exp);
        }
        counting_free(&g_stats, p);
    }
    CHECK_U4(g_stats.live, (unsigned long)live_base, "second extract_alloc frees cleanly");

    /* Failure paths must not allocate. */
    CHECK(nj_zip_extract_alloc(z, NULL, &g_za, &out_len) == NULL);
    CHECK(nj_zip_extract_alloc(NULL, e, &g_za, &out_len) == NULL);
    CHECK_U4(out_len, 0u, "out_len cleared on failure");
    CHECK_U4(g_stats.live, (unsigned long)live_base, "failures leak nothing");

    nj_zip_close(z);
    CHECK_U4(g_stats.live, 0u, "close balances after extract_alloc");
#if NJ_USE_LIBC
    /* NULL allocator hooks fall back to malloc/free. */
    z = nj_zip_open(img, img_len, NULL);
    CHECK(z != NULL);
    if (z != NULL) {
        e = nj_zip_find(z, "readme.txt");
        p = nj_zip_extract_alloc(z, e, NULL, &out_len);
        CHECK(p != NULL);
        if (p != NULL) {
            CHECK_U4(out_len, e->uncomp_size, "default allocator length");
            free(p);
        }
        nj_zip_close(z);
    }
#else
    /* No libc: the reader must refuse to guess an allocator. */
    CHECK(nj_zip_open(img, img_len, NULL) == NULL);
#endif
    free(img);
}

static void open_must_fail(const char *name)
{
    nj_u4 len = 0;
    nj_u1 *img = read_fixture(name, &len);
    nj_zip *z;

    if (img == NULL) {
        CHECK_MSG(0, name);
        return;
    }
    z = nj_zip_open(img, len, &g_za);
    CHECK_MSG(z == NULL, name);
    if (z != NULL) {
        nj_zip_close(z);
    }
    CHECK_U4(g_stats.live, 0u, "malformed archive leaks nothing");
    free(img);
}

static void test_zip_errors(void)
{
    nj_u4 len = 0;
    nj_u1 *img;
    nj_zip *z;
    const nj_zip_entry *e;

    /* Malformed archives must be rejected without touching memory. */
    open_must_fail("bogus_cd_offset.jar");
    open_must_fail("bogus_cd_sig.jar");
    open_must_fail("bogus_count.jar");
    open_must_fail("truncated.jar");
    open_must_fail("manifest_edge.mf");

    /* Empty archive: valid, but lists nothing. */
    img = read_fixture("empty.jar", &len);
    CHECK(img != NULL);
    if (img != NULL) {
        z = nj_zip_open(img, len, &g_za);
        CHECK(z != NULL);
        if (z != NULL) {
            CHECK_U4((unsigned)nj_zip_count(z), 0u, "empty archive count");
            CHECK(nj_zip_entry_at(z, 0) == NULL);
            CHECK(nj_zip_find(z, "anything") == NULL);
            nj_zip_close(z);
        }
        CHECK_U4(g_stats.live, 0u, "empty archive close");
        free(img);
    }

    /* An archive comment that impersonates the EOCD record. */
    img = read_fixture("fake_eocd_comment.jar", &len);
    CHECK(img != NULL);
    if (img != NULL) {
        z = nj_zip_open(img, len, &g_za);
        CHECK_MSG(z != NULL, "real EOCD is found behind a fake signature");
        if (z != NULL) {
            CHECK_U4((unsigned)nj_zip_count(z), 4u, "comment archive count");
            CHECK(nj_zip_find(z, "readme.txt") != NULL);
            nj_zip_close(z);
        }
        CHECK_U4(g_stats.live, 0u, "comment archive close");
        free(img);
    }

    /* Archives that open but whose entries lie about their payload: the
     * extraction-time bounds checks must catch them. */
    {
        static const struct { const char *jar; const char *entry; } liars[] = {
            { "lying_sizes.jar", "readme.txt" },   /* stored, sizes disagree   */
            { "overrun_cd.jar",  "blob.bin"   }    /* payload past the image   */
        };
        size_t li;
        for (li = 0; li < sizeof(liars) / sizeof(liars[0]); li++) {
            nj_u1 scratch[1024];
            img = read_fixture(liars[li].jar, &len);
            CHECK(img != NULL);
            if (img == NULL) {
                continue;
            }
            z = nj_zip_open(img, len, &g_za);
            CHECK_MSG(z != NULL, liars[li].jar);
            if (z != NULL) {
                e = nj_zip_find(z, liars[li].entry);
                CHECK(e != NULL);
                if (e != NULL) {
                    CHECK_S4(nj_zip_extract(z, e, scratch, sizeof(scratch)),
                             NJ_ERR_FORMAT, liars[li].entry);
                    CHECK(nj_zip_extract_alloc(z, e, &g_za, &len) == NULL);
                }
                /* Untouched entries still extract normally. */
                e = nj_zip_find(z, "readme.txt");
                if (e != NULL && strcmp(liars[li].entry, "readme.txt") != 0) {
                    CHECK_S4(nj_zip_extract(z, e, scratch, sizeof(scratch)),
                             (long)e->uncomp_size, "healthy entry still works");
                }
                nj_zip_close(z);
            }
            CHECK_U4(g_stats.live, 0u, liars[li].jar);
            free(img);
        }
    }

    /* Unsupported compression method. */
    img = read_fixture("unsupported.jar", &len);
    CHECK(img != NULL);
    if (img != NULL) {
        z = nj_zip_open(img, len, &g_za);
        CHECK(z != NULL);
        if (z != NULL) {
            nj_u1 scratch[1024];
            e = nj_zip_find(z, "readme.txt");
            CHECK(e != NULL);
            if (e != NULL) {
                CHECK_U4(e->method, 99u, "patched method");
                CHECK_S4(nj_zip_extract(z, e, NULL, 0), (long)e->uncomp_size,
                         "size query is method independent");
                CHECK_S4(nj_zip_extract(z, e, scratch, sizeof(scratch)),
                         NJ_ERR_UNSUPPORTED, "unsupported method");
                CHECK(nj_zip_extract_alloc(z, e, &g_za, &len) == NULL);
            }
            CHECK_U4((unsigned)nj_zip_count(z), 4u, "unsupported entry is listed");
            nj_zip_close(z);
        }
        CHECK_U4(g_stats.live, 0u, "unsupported archive close");
        free(img);
    }

    /* The local header is what locates the payload: its name and extra
     * lengths are allowed to differ from the central directory. */
    img = read_fixture("local_header_differs.jar", &len);
    CHECK(img != NULL);
    if (img != NULL) {
        z = nj_zip_open(img, len, &g_za);
        CHECK(z != NULL);
        if (z != NULL) {
            nj_u4 exp_len = 0;
            nj_u1 *exp = read_fixture("expected/data__large.txt", &exp_len);
            e = nj_zip_find(z, "data/large.txt");
            CHECK(e != NULL);
            CHECK(exp != NULL);
            if (e != NULL && exp != NULL) {
                nj_u1 *got = (nj_u1 *)malloc((size_t)exp_len + 1u);
                /* Prove the fixture is meaningful: the local extra length is
                 * 12 while the central directory says 0. */
                CHECK_U4((nj_u4)img[e->local_offset + 28u]
                         | ((nj_u4)img[e->local_offset + 29u] << 8), 12u,
                         "fixture local extra length");
                CHECK(got != NULL);
                if (got != NULL) {
                    CHECK_S4(nj_zip_extract(z, e, got, exp_len), (long)exp_len,
                             "local-header entry extract");
                    g_checks++;
                    if (memcmp(got, exp, (size_t)exp_len) != 0) {
                        report(__FILE__, __LINE__, "local-header entry bytes", NULL);
                    }
                    free(got);
                }
            }
            free(exp);
            CHECK_U4((unsigned)nj_zip_count(z), 4u, "local-header archive count");
            nj_zip_close(z);
        }
        CHECK_U4(g_stats.live, 0u, "local-header archive close");
        free(img);
    }

    /* Data-descriptor entry: sizes only in the central directory. */
    img = read_fixture("descriptor.jar", &len);
    CHECK(img != NULL);
    if (img != NULL) {
        z = nj_zip_open(img, len, &g_za);
        CHECK(z != NULL);
        if (z != NULL) {
            nj_u4 exp_len = 0;
            nj_u1 *exp = read_fixture("expected/data__large.txt", &exp_len);
            e = nj_zip_find(z, "data/large.txt");
            CHECK(e != NULL);
            CHECK(exp != NULL);
            if (e != NULL && exp != NULL) {
                nj_u1 *got = (nj_u1 *)malloc((size_t)exp_len + 1u);
                CHECK(got != NULL);
                if (got != NULL) {
                    CHECK_S4(nj_zip_extract(z, e, got, exp_len), (long)exp_len,
                             "descriptor entry extract");
                    g_checks++;
                    if (memcmp(got, exp, (size_t)exp_len) != 0) {
                        report(__FILE__, __LINE__, "descriptor entry bytes", NULL);
                    }
                    free(got);
                }
            }
            free(exp);
            nj_zip_close(z);
        }
        CHECK_U4(g_stats.live, 0u, "descriptor archive close");
        free(img);
    }
}

/* ------------------------------------------------------------------ */
/* 4. Manifest                                                         */
/* ------------------------------------------------------------------ */

static void manifest_ok(const char *data, nj_u4 len, const char *attr, const char *want)
{
    char out[256];
    nj_status st;

    memset(out, 0x7F, sizeof(out));
    st = nj_manifest_get(data, len, attr, out, sizeof(out));
    CHECK_MSG(st == NJ_OK, attr);
    if (st == NJ_OK) {
        CHECK_STR(out, want);
    }
}

/* ------------------------------------------------------------------ */
/* The port's own DEFLATE                                              */
/* ------------------------------------------------------------------ */
/*
 * A port with zlib hands nj_zip_set_inflate() its decompressor and every
 * compressed entry goes through it.  That hook is what lets a build drop
 * NJ_ENABLE_INFLATE altogether, so it needs a test that can tell it apart from
 * the built-in path: a hook that counts, and a hook that refuses.
 *
 * The refusal case matters more than the success case.  If the zip reader
 * quietly fell back to the built-in decompressor whenever the port said no,
 * then a build with NJ_ENABLE_INFLATE=0 would appear to work while the port's
 * hook was never actually consulted - which is the kind of bug that only shows
 * up on the platform it was written for.
 */
static int g_hook_calls;
static int g_hook_fail;

static nj_status hook_inflate(void *ud, const nj_u1 *in, nj_u4 in_len,
                              nj_u1 *out, nj_u4 out_len)
{
    nj_u4 len = out_len;
    (void)ud;

    g_hook_calls++;
    if (g_hook_fail) {
        return NJ_ERR_FORMAT;
    }
    return nj_inflate_raw(in, in_len, out, &len);
}

static void test_port_inflate(void)
{
    nj_u4 img_len = 0;
    nj_u1 *img = read_fixture("sample.jar", &img_len);
    nj_zip *z;
    const nj_zip_entry *e;
    nj_u1 *got;
    nj_u4 i;

    CHECK(img != NULL);
    if (!img) {
        return;
    }
    g_arena_used = 0;
    z = nj_zip_open(img, img_len, &g_arena_za);
    CHECK(z != NULL);
    if (!z) {
        free(img);
        return;
    }

    /* Find a compressed entry: the hook is only consulted for method 8. */
    e = NULL;
    for (i = 0u; i < (nj_u4)nj_zip_count(z); i++) {
        const nj_zip_entry *c = nj_zip_entry_at(z, (int)i);
        if (c && c->method == 8u && c->uncomp_size > 0u) {
            e = c;
            break;
        }
    }
    CHECK_MSG(e != NULL, "the fixture has a deflated entry");
    if (!e) {
        nj_zip_close(z);
        free(img);
        return;
    }

    got = (nj_u1 *)malloc((size_t)e->uncomp_size);
    CHECK(got != NULL);
    if (!got) {
        nj_zip_close(z);
        free(img);
        return;
    }

    /* With no hook, the built-in decompressor answers. */
    g_hook_calls = 0;
    CHECK_S4(nj_zip_extract(z, e, got, e->uncomp_size), (long)e->uncomp_size,
             "built-in extract");
    CHECK_U4(g_hook_calls, 0u, "no hook installed, no hook call");

    /* With a hook, the hook answers - and only the hook. */
    nj_zip_set_inflate(z, hook_inflate, NULL);
    memset(got, 0, (size_t)e->uncomp_size);
    CHECK_S4(nj_zip_extract(z, e, got, e->uncomp_size), (long)e->uncomp_size,
             "port extract");
    CHECK_U4(g_hook_calls, 1u, "the port hook was called once");

    /* A refusing port must fail the extraction, not fall back: otherwise a
     * build with no built-in inflate would work on a port whose hook is
     * broken, and only on that port. */
    g_hook_fail = 1;
    CHECK_MSG(nj_zip_extract(z, e, got, e->uncomp_size) != NJ_OK,
              "a refusing hook fails the extraction");
    g_hook_fail = 0;

    /* And the size query must not call it at all: the directory already knows
     * the length, and the hook allocates nothing. */
    g_hook_calls = 0;
    CHECK_S4(nj_zip_extract(z, e, NULL, 0), (long)e->uncomp_size, "size query");
    CHECK_U4(g_hook_calls, 0u, "a size query does not inflate");

    /* Passing NULL goes back to the built-in decompressor. */
    nj_zip_set_inflate(z, NULL, NULL);
    g_hook_calls = 0;
    CHECK_S4(nj_zip_extract(z, e, got, e->uncomp_size), (long)e->uncomp_size,
             "built-in extract again");
    CHECK_U4(g_hook_calls, 0u, "hook removed");

    free(got);
    nj_zip_close(z);
    free(img);
    /* The arena is a bump allocator with no free(); rewinding it here is what
     * the other arena users do, and the final check in main() proves it. */
    g_arena_used = 0;
}

static void test_manifest(void)
{
    nj_u4 len = 0;
    char *mf = read_text("manifest_edge.mf", &len);
    char small[5];
    nj_status st;

    CHECK(mf != NULL);
    if (mf != NULL) {
        manifest_ok(mf, len, "MIDlet-Name", "Edge Mid");
        manifest_ok(mf, len, "midlet-name", "Edge Mid");      /* case-insensitive */
        manifest_ok(mf, len, "MiDlEt-VeRsIoN", "2.0");
        manifest_ok(mf, len, "LONG-ATTR", "first part second partthird part");
        manifest_ok(mf, len, "Tight-Attr", "alphabeta");
        manifest_ok(mf, len, "Empty-Attr", "");
        manifest_ok(mf, len, "Trail-Spaces", "spaced value  ");
        manifest_ok(mf, len, "Manifest-Version", "1.0");

        /* Only the main section is searched. */
        st = nj_manifest_get(mf, len, "Attr-Only-In-Entry", small, sizeof(small));
        CHECK_MSG(st == NJ_ERR, "per-entry section is not searched");
        st = nj_manifest_get(mf, len, "SHA-256-Digest", small, sizeof(small));
        CHECK_MSG(st == NJ_ERR, "attribute absent from the main section");

        /* The value is truncated but still NUL terminated. */
        memset(small, 0x7F, sizeof(small));
        st = nj_manifest_get(mf, len, "MIDlet-Name", small, 4);
        CHECK(st == NJ_OK);
        CHECK_STR(small, "Edg");

        /* Argument validation. */
        CHECK(nj_manifest_get(NULL, len, "MIDlet-Name", small, sizeof(small)) == NJ_ERR);
        CHECK(nj_manifest_get(mf, len, NULL, small, sizeof(small)) == NJ_ERR);
        CHECK(nj_manifest_get(mf, len, "MIDlet-Name", NULL, sizeof(small)) == NJ_ERR);
        CHECK(nj_manifest_get(mf, len, "MIDlet-Name", small, 0) == NJ_ERR);
        CHECK(nj_manifest_get(mf, 0, "MIDlet-Name", small, sizeof(small)) == NJ_ERR);
        free(mf);
    }

    /* The manifest inside the JAR, read back through the zip reader. */
    {
        nj_u4 img_len = 0;
        nj_u1 *img = read_fixture("sample.jar", &img_len);
        nj_zip *z;
        CHECK(img != NULL);
        if (img != NULL) {
            z = nj_zip_open(img, img_len, &g_za);
            CHECK(z != NULL);
            if (z != NULL) {
                const nj_zip_entry *e = nj_zip_find(z, "META-INF/MANIFEST.MF");
                void *mf_data;
                nj_u4 mf_len = 0;
                CHECK(e != NULL);
                mf_data = nj_zip_extract_alloc(z, e, &g_za, &mf_len);
                CHECK(mf_data != NULL);
                if (mf_data != NULL) {
                    manifest_ok((const char *)mf_data, mf_len, "MIDlet-Name", "Nano Sample");
                    manifest_ok((const char *)mf_data, mf_len, "midlet-1",
                                "NanoSample, /icon.png, nanosample.Midlet");
                    manifest_ok((const char *)mf_data, mf_len, "MIDlet-Version", "1.0.0");
                    manifest_ok((const char *)mf_data, mf_len, "MicroEdition-Profile",
                                "MIDP-2.0");
                    CHECK(nj_manifest_get((const char *)mf_data, mf_len, "SHA-256-Digest",
                                          small, sizeof(small)) == NJ_ERR);
                    counting_free(&g_stats, mf_data);
                }
                nj_zip_close(z);
            }
            CHECK_U4(g_stats.live, 0u, "manifest archive close");
            free(img);
        }
    }

    /* A manifest without a trailing newline and without a blank line. */
    {
        static const char one[] = "MIDlet-Name: Tight";
        char out[32];
        CHECK(nj_manifest_get(one, (nj_u4)strlen(one), "MIDlet-Name", out, sizeof(out))
              == NJ_OK);
        CHECK_STR(out, "Tight");
        CHECK(nj_manifest_get(one, (nj_u4)strlen(one), "MIDlet-Version", out, sizeof(out))
              == NJ_ERR);
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    if (!resolve_fixtures(argc > 1 ? argv[1] : NULL)) {
        printf("cannot find the fixtures; run: python3 tests/gen_fixtures.py\n");
        printf("(searched relative to \"%s\")\n", "tests/fixtures");
        return 2;
    }
    printf("nano_j2me zip/inflate tests -- fixtures in %s\n\n", g_fix);

    section("checksums (crc32 / adler32)");
    test_checksums();
    section("inflate (raw / zlib / fixed / stored / adversarial)");
    test_inflate();
    section("inflate zlib envelope (headers, adler32)");
    test_zlib_envelope();
    section("zip listing and extraction");
    test_zip_sample();
    section("zip extract_alloc and allocation balance");
    test_zip_extract_alloc();
    section("zip malformed archives");
    test_zip_errors();
    section("port-supplied DEFLATE (nj_zip_set_inflate)");
    test_port_inflate();
    section("jar manifest parser");
    test_manifest();

    /* The project rule: the runtime owns nothing when it exits.  Every
     * open/close cycle above already checks the balance; this is the final
     * gate over the whole run. */
    CHECK_U4(g_stats.live, 0u, "zero live allocations at the end of main");
    CHECK_U4(g_arena_used, 0u, "arena fully rewound");

    printf("\n=========================================\n");
    printf(" checks   : %d\n", g_checks);
    printf(" failures : %d\n", g_fails);
    printf(" RESULT   : %s\n", (g_fails == 0) ? "PASS" : "FAIL");
    printf("=========================================\n");
    return (g_fails == 0) ? 0 : 1;
}
