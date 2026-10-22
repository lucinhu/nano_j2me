/*
 * PNG decoder: chunk CRC leniency (src/gfx/nj_png.c).
 *
 * Java's ImageIO never verifies PNG chunk CRCs, and real J2ME titles depend
 * on it.  tests/games/仙魔飞行棋_417.jar loads a 116x99 palette PNG, then its
 * loader (g.a(String, byte[])) walks the chunks and System.arraycopy()s a
 * caller-supplied palette straight over the PLTE payload before handing the
 * same buffer to Image.createImage().  The chunk CRC is left stale, so a
 * strict decoder rejects an image the phone renders - the title then throws
 * IOException at createImage and NullPointerException at drawImage and never
 * leaves its loading screen.
 *
 * The copyrighted asset cannot live in the repo, so this file builds the same
 * *structure* in memory: an 8-bit palette PNG whose PLTE payload is patched
 * after its CRC was written.  The patched file must decode with the patched
 * colours, and the structural checks (bounds, chunk order, IEND, inflate
 * errors) must still reject genuinely malformed input.
 *
 * A strict build (-DNJ_PNG_STRICT_CRC=1, passed through CMAKE_C_FLAGS) makes
 * the stale-CRC file fail again; the stale-CRC check below is compiled for the
 * policy the build selected, so a strict-build ctest run still passes.
 */

#include "nj2me/nj.h"
#include "nj2me/nj_png.h"
#include "nj2me/nj_inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

/* ------------------------------------------------------------------ */
/* PNG builder                                                         */
/* ------------------------------------------------------------------ */

#define PNG_W 4
#define PNG_H 2
#define PNG_PAL_ENTRIES 4

typedef struct png_offsets {
    nj_u4 len;            /* total file size            */
    nj_u4 plte_payload;   /* first PLTE colour byte     */
    nj_u4 idat_payload;   /* first IDAT byte            */
    nj_u4 idat_length;    /* IDAT chunk length field    */
    nj_u4 iend_length;    /* IEND chunk length field    */
} png_offsets;

static void put_be32(nj_u1 *p, nj_u4 v)
{
    p[0] = (nj_u1)(v >> 24);
    p[1] = (nj_u1)(v >> 16);
    p[2] = (nj_u1)(v >> 8);
    p[3] = (nj_u1)v;
}

static void put_le16(nj_u1 *p, nj_u4 v)
{
    p[0] = (nj_u1)v;
    p[1] = (nj_u1)(v >> 8);
}

/* type + body, as the chunk CRC is defined. */
static void put_chunk(nj_u1 *out, nj_u4 *pos, const char *type,
                      const nj_u1 *body, nj_u4 len)
{
    put_be32(out + *pos, len);
    memcpy(out + *pos + 4, type, 4u);
    if (len != 0u) {
        memcpy(out + *pos + 8, body, (size_t)len);
    }
    put_be32(out + *pos + 8 + len, nj_crc32(out + *pos + 4, len + 4u, 0u));
    *pos += 12u + len;
}

/*
 * Build the test image: 8-bit palette, no interlace, one IDAT.  The DEFLATE
 * stream uses stored (uncompressed) blocks so the test needs no compressor.
 */
static void build_png(nj_u1 *out, png_offsets *off)
{
    static const nj_u1 pal[PNG_PAL_ENTRIES * 3] = {
        0x10, 0x20, 0x30,  0x40, 0x50, 0x60,
        0x70, 0x80, 0x90,  0xA0, 0xB0, 0xC0
    };
    static const nj_u1 idx[PNG_W * PNG_H] = {
        0, 1, 2, 3,
        3, 2, 1, 0
    };
    nj_u1 ihdr[13];
    nj_u1 raw[PNG_H * (1 + PNG_W)];
    nj_u1 z[11 + sizeof(raw)];
    nj_u4 raw_len = (nj_u4)sizeof(raw);
    nj_u4 z_len = 11u + raw_len;
    nj_u4 pos = 0;
    int y, x;

    /* signature */
    out[pos++] = 0x89u; out[pos++] = 'P'; out[pos++] = 'N'; out[pos++] = 'G';
    out[pos++] = 0x0Du; out[pos++] = 0x0Au; out[pos++] = 0x1Au; out[pos++] = 0x0Au;

    /* IHDR */
    put_be32(ihdr, PNG_W);
    put_be32(ihdr + 4, PNG_H);
    ihdr[8] = 8u;    /* bit depth      */
    ihdr[9] = 3u;    /* palette        */
    ihdr[10] = 0u;   /* compression    */
    ihdr[11] = 0u;   /* filter method  */
    ihdr[12] = 0u;   /* no interlace   */
    put_chunk(out, &pos, "IHDR", ihdr, 13u);

    /* PLTE */
    off->plte_payload = pos + 8u;
    put_chunk(out, &pos, "PLTE", pal, (nj_u4)sizeof(pal));

    /* raw scanlines: filter byte 0 then one palette index per pixel */
    for (y = 0; y < PNG_H; y++) {
        raw[y * (1 + PNG_W)] = 0u;
        for (x = 0; x < PNG_W; x++) {
            raw[y * (1 + PNG_W) + 1 + x] = idx[y * PNG_W + x];
        }
    }
    /* zlib stream with one stored DEFLATE block */
    z[0] = 0x78u; z[1] = 0x01u;
    z[2] = 0x01u;                       /* BFINAL=1, BTYPE=00 */
    put_le16(z + 3, raw_len);
    put_le16(z + 5, (~raw_len) & 0xFFFFu);
    memcpy(z + 7, raw, (size_t)raw_len);
    put_be32(z + 7 + raw_len, nj_adler32(raw, raw_len));

    off->idat_length = pos;
    off->idat_payload = pos + 8u;
    put_chunk(out, &pos, "IDAT", z, z_len);

    off->iend_length = pos;
    put_chunk(out, &pos, "IEND", NULL, 0u);

    off->len = pos;
}

/* Pixel of an ARGB8888 image decoded by nj_png_decode. */
static nj_u4 pixel(const nj_image *img, int x, int y)
{
    return img->pixels[y * img->width + x];
}

int main(void)
{
    nj_u1 buf[512];
    nj_u1 patched[512];
    png_offsets off;
    nj_image *img;
    const char *err = NULL;

    build_png(buf, &off);

    /* 1. the builder writes valid CRCs: the file must decode as-is */
    img = nj_png_decode(buf, off.len, NULL, NULL, NULL, &err);
    if (img == NULL && err != NULL && strstr(err, "not built") != NULL) {
        printf("test_png: SKIP (PNG support not built)\n");
        return 0;
    }
    check(img != NULL, "a well-formed palette PNG decodes");
    if (img != NULL) {
        check(img->width == PNG_W && img->height == PNG_H,
              "the decoded image has the IHDR geometry");
        check(pixel(img, 0, 0) == 0xFF102030u &&
              pixel(img, 1, 0) == 0xFF405060u &&
              pixel(img, 2, 0) == 0xFF708090u &&
              pixel(img, 3, 0) == 0xFFA0B0C0u,
              "row 0 uses the PLTE colours");
        check(pixel(img, 0, 1) == 0xFFA0B0C0u &&
              pixel(img, 3, 1) == 0xFF102030u,
              "row 1 uses the PLTE colours");
        nj_image_free(img, NULL, NULL);
    } else {
        check(0, "the well-formed PNG decoded (see above)");
    }
    check(err == NULL, "a valid file reports no error");

    /* 2. the regression: patch the palette, leave the PLTE CRC stale */
    memcpy(patched, buf, off.len);
    patched[off.plte_payload + 3] = 0x11u;   /* entry 1 -> 11 22 33 */
    patched[off.plte_payload + 4] = 0x22u;
    patched[off.plte_payload + 5] = 0x33u;

    {
        /* prove the fixture really carries a stale CRC */
        nj_u4 plte_len = 12u;
        nj_u4 want = nj_crc32(patched + off.plte_payload - 4u,
                              plte_len + 4u, 0u);
        nj_u4 have = 0;
        const nj_u1 *p = patched + off.plte_payload + plte_len;
        have = ((nj_u4)p[0] << 24) | ((nj_u4)p[1] << 16) |
               ((nj_u4)p[2] << 8) | (nj_u4)p[3];
        check(want != have,
              "the patched fixture has a deliberately stale PLTE CRC");
    }

    err = NULL;
    img = nj_png_decode(patched, off.len, NULL, NULL, NULL, &err);
#if defined(NJ_PNG_STRICT_CRC) && NJ_PNG_STRICT_CRC
    check(img == NULL && err != NULL,
          "a strict build rejects the stale PLTE CRC");
#else
    check(img != NULL,
          "a palette-patched PNG with a stale CRC still decodes (Java parity)");
    if (img != NULL) {
        check(pixel(img, 0, 0) == 0xFF102030u &&
              pixel(img, 1, 0) == 0xFF112233u &&
              pixel(img, 2, 0) == 0xFF708090u,
              "the patched palette colours are the ones that reach the pixels");
        nj_image_free(img, NULL, NULL);
    }
#endif

    /* 3. structure is still enforced */
    err = NULL;
    img = nj_png_decode(patched, off.len - 4u, NULL, NULL, NULL, &err);
    check(img == NULL && err != NULL,
          "a file truncated inside the IEND CRC is rejected");

    memcpy(patched, buf, off.len);
    patched[off.idat_payload + 2] ^= 0xFFu;   /* break the DEFLATE stream */
    err = NULL;
    img = nj_png_decode(patched, off.len, NULL, NULL, NULL, &err);
    check(img == NULL && err != NULL,
          "a corrupted IDAT payload is rejected");

    memcpy(patched, buf, off.len);
    put_be32(patched + off.idat_length, 0x7FFFFFFFu);
    err = NULL;
    img = nj_png_decode(patched, off.len, NULL, NULL, NULL, &err);
    check(img == NULL && err != NULL,
          "a chunk length that runs past the end is rejected");

    memcpy(patched, buf, off.len);
    put_be32(patched + off.iend_length, 4u);
    err = NULL;
    img = nj_png_decode(patched, off.len, NULL, NULL, NULL, &err);
    check(img == NULL && err != NULL,
          "a non-empty IEND is rejected");

    memcpy(patched, buf, off.len);
    patched[0] = 0x00u;
    err = NULL;
    img = nj_png_decode(patched, off.len, NULL, NULL, NULL, &err);
    check(img == NULL && err != NULL,
          "a file without the PNG signature is rejected");

    err = NULL;
    img = nj_png_decode(NULL, 0u, NULL, NULL, NULL, &err);
    check(img == NULL && err != NULL,
          "a NULL buffer is rejected");

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
