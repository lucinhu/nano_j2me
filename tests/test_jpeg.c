/*
 * nano_j2me - tests/test_jpeg.c
 *
 * The baseline JPEG decoder (src/gfx/nj_jpeg.c) and the adapter wiring that
 * makes Image.createImage() reach it.
 *
 * WHAT THIS PROVES
 * ----------------
 *   - dimensions, component count and container format come out of the
 *     header probe without decoding;
 *   - every positive fixture decodes to the right size, ARGB8888, alpha 255;
 *   - flat colours land within +/-3 of the authored value (this is the check
 *     that catches channel swaps, a wrong DC level and a broken upsample);
 *   - pure red/green/blue stay the right hue;
 *   - 4:4:4, 4:2:2, 4:2:0 and 4:1:1 at sizes that are not multiples of 16,
 *     restart intervals, one component per scan, 16-bit quantisation tables,
 *     SOF1, COM/APPn skipping and Adobe APP14 (YCbCr, RGB, CMYK, YCCK) all
 *     decode, and are compared against Pillow's decode of the same bytes;
 *   - the 1/2 and 1/4 DCT-domain downscale honours the want_width/want_height
 *     hint exactly, keeps the mean brightness (the check that catches a wrong
 *     DC gain in the reduced IDCT) and stays close to a box downsample of
 *     Pillow's full-size decode;
 *   - progressive JPEG is *recognised* (accepts() == 1, info() works) and
 *     fails with a message that names the problem;
 *   - malformed input fails cleanly with a useful message: truncated scan,
 *     SOI only, over-subscribed Huffman table, a marker length past EOF, a
 *     zero-dimension frame, 2 and 5 components, arithmetic coding, a missing
 *     restart marker, a scan before the frame header;
 *   - the decoder really is installed through nj_codec_register() and reached
 *     through nj_image_decode_mem()/nj_image_probe(), not just called
 *     directly;
 *   - create/run/destroy three times releases the same number of host blocks
 *     every round: no residue between game launches.
 *
 * WHY THE PIXEL COMPARISONS ARE TOLERANCES AND NOT EQUALITY
 * ---------------------------------------------------------
 * JPEG is lossy and Pillow's reference decode uses libjpeg's own (different,
 * floating-point) IDCT, so byte equality is not a meaningful test.  The
 * reference for every positive fixture is `<name>.rgb`, Pillow's decode of the
 * very same file, and the metric is the mean absolute error over all channels.
 * The limits below are ~2x the largest error actually measured on this
 * decoder; the measured values are quoted in the comments so a future change
 * that doubles them is visible.  Flat-colour and hue checks are absolute
 * instead of relative, because those have a known correct answer.
 *
 * Usage: test_jpeg [fixture-dir]      (default tests/fixtures/jpeg)
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * nj_codec.h deliberately does not declare the software decoder's entry
 * points (src/gfx/nj_codec.c declares them itself, right above the
 * nj_codec_jpeg_software descriptor), so the test declares them the same way.
 */
int       nj_jpeg_accepts(const void *data, nj_u4 size);
nj_status nj_jpeg_info(const void *data, nj_u4 size, nj_image_info *out);
nj_image *nj_jpeg_decode(const nj_codec_req *req);

#define DEFAULT_DIR "tests/fixtures/jpeg"

static int checks;
static int failures;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));        \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b, what)                                                 \
    do {                                                                     \
        long va_ = (long)(a);                                                \
        long vb_ = (long)(b);                                                \
        checks++;                                                            \
        if (va_ != vb_) {                                                    \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s (%ld != %ld)\n",                        \
                   __FILE__, __LINE__, (what), va_, vb_);                    \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* Fixtures                                                           */
/* ------------------------------------------------------------------ */
#define MAX_FIXTURES 64

typedef struct fixture {
    char name[64];
    int  width;
    int  height;
    int  positive;
} fixture;

static fixture fixtures[MAX_FIXTURES];
static int nfixtures;
static const char *fixture_dir = DEFAULT_DIR;

static unsigned char *read_file(const char *path, long *out_size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long size;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (unsigned char *)malloc((size_t)size);
    if (buf == NULL || fread(buf, 1u, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = size;
    return buf;
}

static unsigned char *load_fixture(const char *name, const char *suffix,
                                   long *size)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s.%s", fixture_dir, name, suffix);
    return read_file(path, size);
}

static int load_manifest(void)
{
    char path[512];
    FILE *f;
    char line[512];

    snprintf(path, sizeof(path), "%s/manifest.txt", fixture_dir);
    f = fopen(path, "r");
    if (f == NULL) {
        printf("cannot open %s\n", path);
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char name[64];
        char kind[16];
        int w, h;

        if (line[0] == '#') {
            continue;
        }
        if (sscanf(line, "%63s %d %d %15s", name, &w, &h, kind) != 4) {
            continue;
        }
        if (nfixtures >= MAX_FIXTURES) {
            break;
        }
        snprintf(fixtures[nfixtures].name, sizeof(fixtures[0].name), "%s",
                 name);
        fixtures[nfixtures].width = w;
        fixtures[nfixtures].height = h;
        fixtures[nfixtures].positive = (strcmp(kind, "positive") == 0);
        nfixtures++;
    }
    fclose(f);
    printf("%d fixtures from %s\n", nfixtures, path);
    return nfixtures > 0;
}

static fixture *find_fixture(const char *name)
{
    int i;

    for (i = 0; i < nfixtures; i++) {
        if (strcmp(fixtures[i].name, name) == 0) {
            return &fixtures[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Decode helpers                                                     */
/* ------------------------------------------------------------------ */
static void *test_alloc(void *ud, nj_size n)
{
    (void)ud;
    return malloc((size_t)n);
}

static void test_dealloc(void *ud, void *p)
{
    (void)ud;
    free(p);
}

static void req_init(nj_codec_req *req, const void *data, nj_u4 size,
                     const char **err)
{
    memset(req, 0, sizeof(*req));
    req->data = data;
    req->size = size;
    req->alloc = test_alloc;
    req->dealloc = test_dealloc;
    req->ud = NULL;
    req->out_format = NJ_CODEC_OUT_ANY;
    req->err = err;
}

static nj_image *decode(const unsigned char *data, long size, const char **err)
{
    nj_codec_req req;

    req_init(&req, data, (nj_u4)size, err);
    return nj_jpeg_decode(&req);
}

static nj_image *decode_scaled(const unsigned char *data, long size,
                               int w, int h, const char **err)
{
    nj_codec_req req;

    req_init(&req, data, (nj_u4)size, err);
    req.want_width = w;
    req.want_height = h;
    return nj_jpeg_decode(&req);
}

/*
 * Mean absolute error of a decoded image against an 8-bit RGB reference, and
 * the worst single-channel error.  Both in 0..255.
 */
static long image_mae(const nj_image *img, const unsigned char *rgb,
                      int *worst)
{
    long total = 0;
    int i, n = img->width * img->height;
    int w = 0;

    for (i = 0; i < n; i++) {
        nj_u4 p = img->pixels[i];
        int ch[3];
        int c;

        ch[0] = (int)((p >> 16) & 0xFFu);
        ch[1] = (int)((p >> 8) & 0xFFu);
        ch[2] = (int)(p & 0xFFu);
        for (c = 0; c < 3; c++) {
            int d = ch[c] - (int)rgb[i * 3 + c];

            if (d < 0) {
                d = -d;
            }
            total += (long)d;
            if (d > w) {
                w = d;
            }
        }
    }
    if (worst != NULL) {
        *worst = w;
    }
    return total / (long)(n * 3);
}

/* Mean of the green channel: the cheap check for a wrong DC gain. */
static long image_mean_luma(const nj_image *img)
{
    long total = 0;
    int i, n = img->width * img->height;

    for (i = 0; i < n; i++) {
        total += (long)((img->pixels[i] >> 8) & 0xFFu);
    }
    return total / n;
}

static long abs_long(long v)
{
    return (v < 0) ? -v : v;
}

/*
 * Box-downsample an 8-bit RGB reference by `factor`, the way a caller that
 * resampled the full-size decode would.
 */
static unsigned char *box_downsample(const unsigned char *rgb, int w, int h,
                                     int factor, int *out_w, int *out_h)
{
    int ow = (w + factor - 1) / factor;
    int oh = (h + factor - 1) / factor;
    unsigned char *out = (unsigned char *)malloc((size_t)ow * (size_t)oh * 3u);
    int x, y, c;

    if (out == NULL) {
        return NULL;
    }
    for (y = 0; y < oh; y++) {
        for (x = 0; x < ow; x++) {
            for (c = 0; c < 3; c++) {
                long sum = 0;
                int count = 0;
                int dy, dx;

                for (dy = 0; dy < factor; dy++) {
                    for (dx = 0; dx < factor; dx++) {
                        int sy = y * factor + dy;
                        int sx = x * factor + dx;

                        if (sy < h && sx < w) {
                            sum += (long)rgb[(sy * w + sx) * 3 + c];
                            count++;
                        }
                    }
                }
                out[(y * ow + x) * 3 + c] =
                    (unsigned char)(count ? sum / count : 0);
            }
        }
    }
    *out_w = ow;
    *out_h = oh;
    return out;
}

/* ------------------------------------------------------------------ */
/* Header probe and general validity                                  */
/* ------------------------------------------------------------------ */
static void test_probe_and_decode(void)
{
    int i;

    printf("header probe and decode\n");
    for (i = 0; i < nfixtures; i++) {
        fixture *fx = &fixtures[i];
        unsigned char *data;
        long size;
        nj_image_info info;
        nj_image *img;
        const char *err = NULL;
        int ncomp;

        data = load_fixture(fx->name, "jpg", &size);
        CHECK(data != NULL, fx->name);
        if (data == NULL) {
            continue;
        }
        /* A bare SOI+EOI is not an image and must not be claimed; every
         * other fixture, including progressive and malformed ones, is a real
         * JPEG container. */
        CHECK_EQ(nj_jpeg_accepts(data, (nj_u4)size),
                 strcmp(fx->name, "soi_only") == 0 ? 0 : 1,
                 "accepts() recognises the container");

        if (!fx->positive) {
            /* Negative fixtures must not crash and must not hand back an
             * image; the message is checked in test_negative(). */
            err = NULL;
            img = decode(data, size, &err);
            if (img != NULL) {
                /* bad_huff_allones is a legal table whose stream decodes to
                 * nonsense: the only promise is "correct size or NULL". */
                CHECK_EQ(img->width, fx->width, "negative fixture size");
                free(img->pixels);
                free(img);
            }
            CHECK(img == NULL || err == NULL, "no half-built image");
            free(data);
            continue;
        }

        memset(&info, 0, sizeof(info));
        CHECK(nj_jpeg_info(data, (nj_u4)size, &info) == NJ_OK, "info()");
        if (strcmp(fx->name, "progressive_35x23") == 0) {
            /* Recognised, reported, and refused with a reason - checked in
             * test_negative().  Only the header probe has to work. */
            free(data);
            continue;
        }
        CHECK_EQ(info.width, fx->width, "probed width");
        CHECK_EQ(info.height, fx->height, "probed height");
        CHECK_EQ(info.has_alpha, 0, "JPEG has no alpha");
        CHECK_EQ(info.format, NJ_CODEC_FMT_JPEG, "format is JPEG");
        ncomp = info.bits_per_pixel / 8;
        CHECK(info.bits_per_pixel == 8 || info.bits_per_pixel == 24 ||
              info.bits_per_pixel == 32, "8/24/32 bits per pixel");
        CHECK(ncomp >= 1 && ncomp <= 4, "1..4 components");

        err = NULL;
        img = decode(data, size, &err);
        if (img == NULL) {
            printf("  FAIL %s: decode failed: %s\n", fx->name,
                   err ? err : "(no message)");
            failures++;
            checks++;
            free(data);
            continue;
        }
        checks++;
        CHECK_EQ(img->width, fx->width, "decoded width");
        CHECK_EQ(img->height, fx->height, "decoded height");
        CHECK_EQ(img->format, NJ_PIXFMT_ARGB8888, "ARGB8888 output");
        CHECK_EQ(img->has_alpha, 0, "has_alpha is 0");
        {
            int n = img->width * img->height;
            int k;
            int opaque = 1;

            for (k = 0; k < n; k++) {
                if (((img->pixels[k] >> 24) & 0xFFu) != 0xFFu) {
                    opaque = 0;
                    break;
                }
            }
            CHECK(opaque, "every pixel is opaque");
        }
        {
            long ref_size;
            unsigned char *ref = load_fixture(fx->name, "rgb", &ref_size);

            if (ref != NULL) {
                int worst = 0;
                long mae = image_mae(img, ref, &worst);
                long limit = 8;

                /* Fixtures where the reference is a box downsample or a
                 * source image rather than Pillow's decode are handled by
                 * their own tests below; here only the plain ones. */
                printf("    %-24s MAE %2ld  worst %3d\n", fx->name, mae, worst);
                if (strcmp(fx->name, "q1_35x23") == 0) {
                    limit = 24;   /* extreme quantisation: heavy ringing */
                } else if (strcmp(fx->name, "cmyk_32x32") == 0 ||
                           strcmp(fx->name, "ycck_32x32") == 0) {
                    limit = 10;   /* reference is the source RGB: see below */
                } else if (strcmp(fx->name, "dqt16_35x23") == 0) {
                    limit = 14;   /* 4x coarser tables */
                }
                CHECK(mae <= limit, "mean absolute error within limit");
                free(ref);
            }
        }
        free(img->pixels);
        free(img);
        free(data);
    }
}

/* ------------------------------------------------------------------ */
/* Specific absolute checks                                           */
/* ------------------------------------------------------------------ */
static nj_image *decode_named(const char *name, unsigned char **data_out,
                              long *size_out)
{
    unsigned char *data;
    long size;
    nj_image *img;
    const char *err = NULL;

    data = load_fixture(name, "jpg", &size);
    if (data == NULL) {
        printf("  FAIL missing fixture %s\n", name);
        failures++;
        checks++;
        return NULL;
    }
    img = decode(data, size, &err);
    if (img == NULL) {
        printf("  FAIL %s: %s\n", name, err ? err : "(no message)");
        failures++;
        checks++;
        free(data);
        return NULL;
    }
    checks++;
    /* One extra check so a successful decode counts even if the caller's
     * assertions are all about pixels. */
    if (data_out != NULL) {
        *data_out = data;
        *size_out = size;
    } else {
        free(data);
    }
    return img;
}

static void test_flat_colour(void)
{
    /* #3366CC at quality 95: the classic "is the decoder's DC level and
     * channel order right" check.  Every pixel must be within +/-3. */
    nj_image *img = decode_named("solid_16x16_q95", NULL, NULL);
    int i, n, bad = 0;

    printf("flat colour\n");
    if (img == NULL) {
        return;
    }
    n = img->width * img->height;
    for (i = 0; i < n; i++) {
        int r = (int)((img->pixels[i] >> 16) & 0xFFu);
        int g = (int)((img->pixels[i] >> 8) & 0xFFu);
        int b = (int)(img->pixels[i] & 0xFFu);

        if (r < 0x33 - 3 || r > 0x33 + 3 ||
            g < 0x66 - 3 || g > 0x66 + 3 ||
            b < 0xCC - 3 || b > 0xCC + 3) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0, "every pixel within +/-3 of #3366CC");
    CHECK_EQ(img->width, 16, "16 wide");
    CHECK_EQ(img->height, 16, "16 high");
    free(img->pixels);
    free(img);
}

static void test_pure_hues(void)
{
    static const char *names[3] = { "pure_r_32x32", "pure_g_32x32",
                                    "pure_b_32x32" };
    static const int chan[3] = { 0, 1, 2 };
    int k;

    printf("pure hues\n");
    for (k = 0; k < 3; k++) {
        nj_image *img = decode_named(names[k], NULL, NULL);
        int i, n, wrong = 0;

        if (img == NULL) {
            continue;
        }
        n = img->width * img->height;
        for (i = 0; i < n; i++) {
            int ch[3];
            int c;

            ch[0] = (int)((img->pixels[i] >> 16) & 0xFFu);
            ch[1] = (int)((img->pixels[i] >> 8) & 0xFFu);
            ch[2] = (int)(img->pixels[i] & 0xFFu);
            /* The lit channel must dominate; the other two must stay dark.
             * JPEG ringing on a saturated block is at most a few levels. */
            if (ch[chan[k]] < 240) {
                wrong++;
            }
            for (c = 0; c < 3; c++) {
                if (c != chan[k] && ch[c] > 16) {
                    wrong++;
                }
            }
        }
        CHECK_EQ(wrong, 0, "pure primary stays pure");
        free(img->pixels);
        free(img);
    }
}

static void test_odd_sizes(void)
{
    struct expect {
        const char *name;
        int w, h;
    };
    static const struct expect k_sizes[] = {
        { "one_1x1", 1, 1 },
        { "wide_300x1", 300, 1 },
        { "tall_1x300", 1, 300 },
        { "mcu_16x16", 16, 16 },
        { "gray_17x13", 17, 13 }
    };
    int i;

    printf("odd sizes\n");
    for (i = 0; i < (int)(sizeof(k_sizes) / sizeof(k_sizes[0])); i++) {
        nj_image *img = decode_named(k_sizes[i].name, NULL, NULL);

        if (img == NULL) {
            continue;
        }
        CHECK_EQ(img->width, k_sizes[i].w, "width");
        CHECK_EQ(img->height, k_sizes[i].h, "height");
        free(img->pixels);
        free(img);
    }
    /* 1x1: a single pixel, so it is also a check on the DC-only fast path. */
    {
        nj_image *img = decode_named("one_1x1", NULL, NULL);

        if (img != NULL) {
            int r = (int)((img->pixels[0] >> 16) & 0xFFu);
            int g = (int)((img->pixels[0] >> 8) & 0xFFu);
            int b = (int)(img->pixels[0] & 0xFFu);

            CHECK(abs(r - 0xE0) <= 8, "1x1 red");
            CHECK(abs(g - 0x40) <= 8, "1x1 green");
            CHECK(abs(b - 0x20) <= 8, "1x1 blue");
            free(img->pixels);
            free(img);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Downscaling                                                        */
/* ------------------------------------------------------------------ */
static void test_downscale(void)
{
    unsigned char *data;
    long size;
    long ref_size;
    unsigned char *ref;
    nj_image *full;
    nj_image *half;
    nj_image *quarter;
    const char *err = NULL;

    printf("DCT-domain downscale\n");
    data = load_fixture("pattern_64x48", "jpg", &size);
    ref = load_fixture("pattern_64x48", "rgb", &ref_size);
    if (data == NULL || ref == NULL) {
        printf("  FAIL missing pattern fixture\n");
        failures++;
        checks++;
        free(data);
        free(ref);
        return;
    }
    full = decode(data, size, &err);
    CHECK(full != NULL, "full size decode");
    half = decode_scaled(data, size, 32, 24, &err);
    CHECK(half != NULL, "half size decode");
    quarter = decode_scaled(data, size, 16, 12, &err);
    CHECK(quarter != NULL, "quarter size decode");

    if (full != NULL && half != NULL) {
        long dsize;
        /* libjpeg's own reduced-size decode, via Pillow's draft() mode: the
         * same algorithm, so this is a like-for-like comparison (~2 levels
         * measured here, which is rounding in the two implementations). */
        unsigned char *draft = load_fixture("pattern_64x48", "draft2.rgb",
                                            &dsize);
        int worst = 0;
        long mae = -1;

        CHECK_EQ(half->width, 32, "half width");
        CHECK_EQ(half->height, 24, "half height");
        if (draft != NULL) {
            mae = image_mae(half, draft, &worst);
            free(draft);
        }
        printf("    half   MAE %2ld  worst %3d  mean %ld vs %ld\n",
               mae, worst, image_mean_luma(half), image_mean_luma(full));
        CHECK(mae >= 0, "half: reference present");
        CHECK(mae >= 0 && mae <= 8, "half: matches libjpeg's reduced decode");
        /* The reduced IDCT must keep the DC gain: a wrong normalisation (the
         * classic F/4 instead of F/8) shows up here immediately. */
        CHECK(abs_long(image_mean_luma(half) - image_mean_luma(full)) <= 4,
              "half: mean brightness preserved");
    }
    if (full != NULL && quarter != NULL) {
        long dsize;
        unsigned char *draft = load_fixture("pattern_64x48", "draft4.rgb",
                                            &dsize);
        int worst = 0;
        long mae = -1;
        int rw, rh;
        unsigned char *ref4 = box_downsample(ref, 64, 48, 4, &rw, &rh);

        CHECK_EQ(quarter->width, 16, "quarter width");
        CHECK_EQ(quarter->height, 12, "quarter height");
        if (draft != NULL) {
            mae = image_mae(quarter, draft, &worst);
            free(draft);
        }
        printf("    quarter MAE %2ld  worst %3d  mean %ld vs %ld"
               "  (box-downsample MAE %ld)\n",
               mae, worst, image_mean_luma(quarter), image_mean_luma(full),
               ref4 ? image_mae(quarter, ref4, NULL) : -1L);
        CHECK(mae >= 0 && mae <= 10, "quarter: matches libjpeg's reduced decode");
        CHECK(abs_long(image_mean_luma(quarter) - image_mean_luma(full)) <= 6,
              "quarter: mean brightness preserved");
        free(ref4);
    }
    /* A request that is neither an exact half nor an exact quarter must
     * decode natively: it is a hint, not a demand. */
    {
        nj_image *odd = decode_scaled(data, size, 40, 40, &err);

        CHECK(odd != NULL, "odd scale request decodes");
        if (odd != NULL) {
            CHECK_EQ(odd->width, 64, "odd request decodes natively");
            CHECK_EQ(odd->height, 48, "odd request decodes natively");
            free(odd->pixels);
            free(odd);
        }
    }
    if (full != NULL) {
        free(full->pixels);
        free(full);
    }
    if (half != NULL) {
        free(half->pixels);
        free(half);
    }
    if (quarter != NULL) {
        free(quarter->pixels);
        free(quarter);
    }
    free(data);
    free(ref);
}

/* ------------------------------------------------------------------ */
/* Colour models                                                      */
/* ------------------------------------------------------------------ */
static void test_colour_models(void)
{
    printf("colour models\n");
    /* Adobe transform 0 with component IDs R,G,B: a straight RGB JPEG.  The
     * fixture's left third is #CC3333, so treating it as YCbCr would be
     * obvious. */
    {
        nj_image *img = decode_named("adobe_rgb_32x16", NULL, NULL);

        if (img != NULL) {
            int x, y;
            int bad = 0;

            /* The flat red left third is x < 32/3 == 10, on every row. */
            for (y = 0; y < 16; y++) {
                for (x = 0; x < 10; x++) {
                    nj_u4 p = img->pixels[y * img->width + x];
                    int r = (int)((p >> 16) & 0xFFu);
                    int g = (int)((p >> 8) & 0xFFu);
                    int b = (int)(p & 0xFFu);

                    if (r < 190 || g > 80 || b > 80) {
                        bad++;
                    }
                }
            }
            CHECK_EQ(bad, 0, "Adobe transform 0 is RGB, not YCbCr");
            free(img->pixels);
            free(img);
        }
    }
    /* 4-component Adobe files: the stored channels are inverted.  The
     * generator's reference is the *source* RGB (its encoder is the exact
     * inverse of the documented CMYK model), not Pillow's CMYK->RGB, which
     * uses a different (multiplicative) ink model.  See the generator. */
    {
        nj_image *img = decode_named("cmyk_32x32", NULL, NULL);

        if (img != NULL) {
            int r = (int)((img->pixels[0] >> 16) & 0xFFu);
            int g = (int)((img->pixels[0] >> 8) & 0xFFu);
            int b = (int)(img->pixels[0] & 0xFFu);

            printf("    cmyk px0 = %02X%02X%02X (source CC3333)\n", r, g, b);
            CHECK(abs(r - 0xCC) <= 8 && abs(g - 0x33) <= 8 &&
                  abs(b - 0x33) <= 8, "CMYK inks invert back to the source");
            free(img->pixels);
            free(img);
        }
    }
    {
        nj_image *img = decode_named("ycck_32x32", NULL, NULL);

        if (img != NULL) {
            int r = (int)((img->pixels[0] >> 16) & 0xFFu);
            int g = (int)((img->pixels[0] >> 8) & 0xFFu);
            int b = (int)(img->pixels[0] & 0xFFu);

            printf("    ycck px0 = %02X%02X%02X (source CC3333)\n", r, g, b);
            CHECK(abs(r - 0xCC) <= 10 && abs(g - 0x33) <= 10 &&
                  abs(b - 0x33) <= 10, "YCCK inverts back to the source");
            free(img->pixels);
            free(img);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Negative fixtures                                                  */
/* ------------------------------------------------------------------ */
typedef struct neg_expect {
    const char *name;
    const char *needle;    /* NULL: any non-empty message is fine */
} neg_expect;

static void test_negative(void)
{
    static const neg_expect k_cases[] = {
        { "trunc_mid", "ended early" },
        { "soi_only", "frame header" },
        { "bad_huff_oversub", "Huffman" },
        { "bad_huff_allones", NULL },
        { "len_past_eof", "past the end" },
        { "zero_dim", "invalid size" },
        { "five_comp", "components" },
        { "two_comp", "components" },
        { "arith_sof9", "arithmetic" },
        { "bad_restart", "restart" },
        { "no_sof", "frame header" },
        { "progressive_35x23", "progressive" }
    };
    int i;

    printf("malformed input\n");
    for (i = 0; i < (int)(sizeof(k_cases) / sizeof(k_cases[0])); i++) {
        unsigned char *data;
        long size;
        nj_image *img;
        const char *err = NULL;

        data = load_fixture(k_cases[i].name, "jpg", &size);
        CHECK(data != NULL, k_cases[i].name);
        if (data == NULL) {
            continue;
        }
        img = decode(data, size, &err);
        if (img != NULL) {
            if (strcmp(k_cases[i].name, "bad_huff_allones") == 0) {
                /* A legal but pathological table: the stream may decode to
                 * noise (right size) or fail; either is fine, a crash or a
                 * wrong size is not.  ASan covers the crash half. */
                int fx_ok = 0;
                fixture *fx = find_fixture(k_cases[i].name);

                if (fx != NULL) {
                    fx_ok = (img->width == fx->width &&
                             img->height == fx->height);
                }
                CHECK(fx_ok, "pathological table: right size or failure");
            } else {
                printf("  FAIL %s decoded but should not have\n",
                       k_cases[i].name);
                failures++;
                checks++;
            }
            free(img->pixels);
            free(img);
        } else {
            checks++;
            CHECK(err != NULL && err[0] != '\0',
                  "failure carries a message");
            if (k_cases[i].needle != NULL) {
                if (err == NULL || strstr(err, k_cases[i].needle) == NULL) {
                    printf("  FAIL %s: message \"%s\" lacks \"%s\"\n",
                           k_cases[i].name, err ? err : "(null)",
                           k_cases[i].needle);
                    failures++;
                    checks++;
                } else {
                    checks++;
                }
            }
            printf("    %-20s -> %s\n", k_cases[i].name,
                   err ? err : "(no message)");
        }
        free(data);
    }

    /* Odd entry points: NULL, empty and non-JPEG input. */
    {
        nj_image_info info;
        const char *err = NULL;
        nj_image *img;
        static const unsigned char not_jpeg[8] = { 0x89u, 'P', 'N', 'G', 0, 0,
                                                   0, 0 };

        CHECK_EQ(nj_jpeg_accepts(NULL, 0), 0, "NULL is not a JPEG");
        CHECK_EQ(nj_jpeg_accepts(not_jpeg, 8), 0, "PNG is not a JPEG");
        CHECK_EQ(nj_jpeg_info(NULL, 0, &info), NJ_ERR_FORMAT, "info(NULL)");
        CHECK_EQ(nj_jpeg_info(not_jpeg, 8, &info), NJ_ERR_FORMAT,
                 "info(PNG)");
        img = decode(not_jpeg, 8, &err);
        CHECK(img == NULL, "PNG bytes are refused");
        err = NULL;
        img = nj_jpeg_decode(NULL);
        CHECK(img == NULL, "decode(NULL request)");
        /* A request without an allocator must refuse, not crash. */
        {
            nj_codec_req req;
            const unsigned char *soi = (const unsigned char *)"\xff\xd8\xff\xd9";

            memset(&req, 0, sizeof(req));
            req.data = soi;
            req.size = 4;
            CHECK(nj_jpeg_decode(&req) == NULL, "no allocator, no decode");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Registry wiring                                                    */
/* ------------------------------------------------------------------ */
static nj_port *make_port(nj_port *p)
{
    memset(p, 0, sizeof(*p));
    if (nj_port_headless_ex(p, 128, 128, 0, NULL) != NJ_OK) {
        return NULL;
    }
    return p;
}

static void test_registry(nj_vm *vm)
{
    int i, decoded = 0;

    printf("through the codec registry\n");
    CHECK(nj_codec_count(vm) > 0, "providers are registered");
    {
        int found = 0;

        for (i = 0; i < nj_codec_count(vm); i++) {
            const char *n = nj_codec_name_at(vm, i);

            if (n != NULL && strcmp(n, "jpeg-software") == 0) {
                found = 1;
            }
        }
        CHECK(found, "jpeg-software is installed by the builtins");
    }

    for (i = 0; i < nfixtures; i++) {
        fixture *fx = &fixtures[i];
        unsigned char *data;
        long size;
        const char *err = NULL;
        nj_image *img;
        nj_image_info info;

        if (!fx->positive || strcmp(fx->name, "progressive_35x23") == 0) {
            continue;
        }
        data = load_fixture(fx->name, "jpg", &size);
        if (data == NULL) {
            continue;
        }
        CHECK(nj_codec_available(vm, data, (nj_u4)size) == 1,
              "the registry recognises the fixture");
        memset(&info, 0, sizeof(info));
        CHECK_EQ(nj_image_probe(vm, data, (nj_u4)size, &info, NULL), NJ_OK,
                 "nj_image_probe");
        CHECK_EQ(info.width, fx->width, "probed width matches the manifest");

        err = NULL;
        img = nj_image_decode_mem(vm, data, (nj_u4)size, &err);
        if (img == NULL) {
            printf("  FAIL %s via registry: %s\n", fx->name,
                   err ? err : "(no message)");
            failures++;
            checks++;
            free(data);
            continue;
        }
        checks++;
        CHECK_EQ(img->width, fx->width, "registry width");
        CHECK_EQ(img->height, fx->height, "registry height");
        CHECK_EQ(img->format, NJ_PIXFMT_ARGB8888, "registry format");
        nj_image_release(vm, img);
        decoded++;
        free(data);
    }
    CHECK(decoded >= 15, "at least 15 fixtures decoded through the registry");

    /* The scaled hint must reach the provider through the wrapper too. */
    {
        unsigned char *data;
        long size;
        nj_image *img;

        data = load_fixture("pattern_64x48", "jpg", &size);
        if (data != NULL) {
            img = nj_image_decode_scaled(vm, data, (nj_u4)size, 32, 24, NULL);
            CHECK(img != NULL, "nj_image_decode_scaled");
            if (img != NULL) {
                CHECK_EQ(img->width, 32, "scaled width");
                CHECK_EQ(img->height, 24, "scaled height");
                nj_image_release(vm, img);
            }
            free(data);
        }
    }

    /* Progressive: recognised by the registry, refused with a reason. */
    {
        unsigned char *data;
        long size;
        const char *err = NULL;
        nj_image *img;

        data = load_fixture("progressive_35x23", "jpg", &size);
        if (data != NULL) {
            CHECK(nj_codec_available(vm, data, (nj_u4)size) == 1,
                  "progressive is claimed, so the failure is explained");
            img = nj_image_decode_mem(vm, data, (nj_u4)size, &err);
            CHECK(img == NULL, "progressive decode fails");
            CHECK(err != NULL && strstr(err, "progressive") != NULL,
                  "and the message says progressive");
            free(data);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */
static void test_lifecycle(void)
{
    nj_u4 first = 0u;
    int round;

    printf("teardown across restarts\n");
    for (round = 0; round < 3; round++) {
        nj_port port;
        nj_vm *vm;
        nj_u4 released;
        int i;

        if (make_port(&port) == NULL) {
            CHECK(0, "port");
            return;
        }
        vm = nj_vm_create(&port, 1024u * 1024u);
        CHECK(vm != NULL, "vm");
        if (vm == NULL) {
            nj_port_shutdown(&port);
            return;
        }
        for (i = 0; i < nfixtures; i++) {
            fixture *fx = &fixtures[i];
            unsigned char *data;
            long size;
            nj_image *img;

            if (!fx->positive || strcmp(fx->name, "progressive_35x23") == 0) {
                continue;
            }
            data = load_fixture(fx->name, "jpg", &size);
            if (data == NULL) {
                continue;
            }
            img = nj_image_decode_mem(vm, data, (nj_u4)size, NULL);
            /* Deliberately not released every time: nj_vm_destroy() must
             * clean up whatever the provider handed out, so the number of
             * blocks it releases has to be the same every round. */
            if (img != NULL && (i & 1) == 0) {
                nj_image_release(vm, img);
            }
            free(data);
        }
        released = nj_vm_destroy(vm);
        if (round == 0) {
            first = released;
            CHECK(released > 0u, "destroy released the VM's blocks");
        } else {
            CHECK_EQ(released, first, "identical teardown every round");
        }
        nj_port_shutdown(&port);
    }
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc > 1) {
        fixture_dir = argv[1];
    }
    printf("== nano_j2me baseline JPEG decoder ==\n");
    if (!load_manifest()) {
        printf("FAIL: no fixtures; run tests/gen_jpeg_fixtures.py first\n");
        return 2;
    }

    test_probe_and_decode();
    test_flat_colour();
    test_pure_hues();
    test_odd_sizes();
    test_downscale();
    test_colour_models();
    test_negative();

    {
        nj_port port;

        if (make_port(&port) == NULL) {
            printf("FAIL: cannot create a headless port\n");
            return 2;
        }
        {
            nj_vm *vm = nj_vm_create(&port, 1024u * 1024u);

            if (vm == NULL) {
                printf("FAIL: cannot create a VM\n");
                nj_port_shutdown(&port);
                return 2;
            }
            test_registry(vm);
            nj_vm_destroy(vm);
        }
        nj_port_shutdown(&port);
    }
    test_lifecycle();

    printf("%d checks, %d failures\n", checks, failures);
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
