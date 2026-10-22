/*
 * nano_j2me - nj_jpeg.c
 *
 * Baseline JPEG (ITU-T T.81 / ISO 10918-1) decoder for
 * javax.microedition.lcdui.Image, the software provider behind
 * nj_codec_jpeg_software (see src/gfx/nj_codec.c).
 *
 * WHAT IS SUPPORTED
 * -----------------
 *   - baseline sequential DCT (SOF0) and extended sequential with 8-bit
 *     precision (SOF1); progressive (SOF2), lossless (SOF3) and
 *     arithmetic-coded (SOF9..SOF15) files are recognised and rejected with
 *     a message that says so, so Image.createImage(byte[]) explains itself;
 *   - SOI, APPn (APP0/JFIF and APP14/Adobe are read, the rest is skipped),
 *     COM, DQT (8- and 16-bit), DHT, DRI, SOS, RSTn, EOI;
 *   - 1 component (greyscale), 3 components (YCbCr, or straight RGB when the
 *     Adobe transform says 0 or the component IDs are 'R','G','B') and 4
 *     components (CMYK/YCCK in the Adobe convention, which stores the ink
 *     values inverted);
 *   - 4:4:4, 4:2:2, 4:2:0, 4:1:1 and any other legal sampling grid (h/v in
 *     1..4, at most 10 blocks per MCU), interleaved or one component per
 *     scan, with restart intervals;
 *   - output is always ARGB8888 (0xAARRGGBB) with alpha 255, stride == width.
 *
 * DESIGN CHOICES (the ones a reviewer will ask about)
 * ---------------------------------------------------
 *   IDCT: a separable integer IDCT with a `static const` Q12 cosine table
 *   that already folds in the 1/sqrt(2) of the DC basis function.  Two passes
 *   of `sum(coef * cos) >> shift` per dimension, all in int32, no floating
 *   point anywhere in the pixel path.  Every right shift in the pixel path
 *   goes through jp_descale(), which shifts only non-negative values: C99
 *   leaves a right shift of a negative value implementation-defined, and this
 *   decoder must produce bit-identical output on every compiler.  The YCbCr
 *   matrix uses divisions by 65536 for the same reason.
 *
 *   The same routine computes the reduced (DCT-domain) output for 1/2 and 1/4
 *   scaling: it evaluates the SxS box average of the block directly in the
 *   coefficient domain, keeping every frequency and weighting it with the box
 *   kernel W(u) = sin(S*theta)/(S*sin(theta)), theta = u*PI/16 (see
 *   s_idct_kern).  The obvious first guess - drop the high frequencies and
 *   sample the survivors at consecutive output positions - is NOT a downscale:
 *   it returns the block's top-left corner low-passed, and a half-size splash
 *   screen comes out visibly cropped.  Measured against libjpeg's own
 *   reduced-size decode, the box form agrees to within one level and the naive
 *   one is off by tens.  W(0) = 1 keeps the DC term, and flat colours, exact.
 *
 *   UPSAMPLING: plain replication.  Output pixel x takes chroma sample
 *   floor(x * h / h_max) - the sample whose 2x2/4x1 block contains the pixel.
 *   It is what the hardware and libjpeg's "no fancy upsampling" mode do, it is
 *   fast, and it is the right choice for pixel art that was authored at the
 *   chroma resolution.  (Fancy (triangle) upsampling is smoother on photos,
 *   but it shifts chroma by half a pixel and costs two extra multiplies per
 *   pixel; game artwork does not want either.)
 *
 *   MEMORY: component planes and the Huffman tables go through req->alloc
 *   (planes are the only thing that genuinely outlives a function call, plus
 *   ~4 KB of Huffman tables that would otherwise bloat the stack frame of a
 *   decoder that has to run on an MCU).  Everything else - the coefficient
 *   block, the IDCT work array, the sample block - is a few hundred bytes of
 *   stack.  Every allocation is released on every error path through a single
 *   jp_free().
 *
 * SAFETY (the input is a JPEG inside an untrusted game JAR)
 * ---------------------------------------------------------
 *   - every marker length is validated against the remaining input before the
 *     segment is read;
 *   - every Huffman code walk is bounded to 16 bits and the table is
 *     validated at build time (no over-subscribed table, no more than 256
 *     codes, no empty table); an all-ones code word, which T.81 reserves as
 *     padding, simply fails the walk and is treated as end of data;
 *   - coefficient indices, block indices and plane indices are all bounded by
 *     validated geometry; the block grid is allocated MCU-padded so an
 *     interleaved scan can never write past a plane;
 *   - a restart marker that is missing, out of sequence or not a restart
 *     marker fails the decode instead of desynchronising the bit reader;
 *   - an entropy stream that ends before the last MCU fails with
 *     "scan data ended early"; only the final MCU may be completed with
 *     zero-fill, which is the usual truncated-file behaviour;
 *   - width and height are capped at 8192 and every size computation is done
 *     in 64 bits before an allocation;
 *   - decompressed coefficients are clamped before dequantisation so no
 *     intermediate can overflow int32 even for a hostile 16-bit DQT.
 */

#include "nj2me/nj_config.h"
#include "nj2me/nj_types.h"
#include "nj2me/nj_codec.h"
#include "nj2me/nj_gfx.h"

#include <string.h>   /* memcmp / memset / memcpy / strlen only */

/* ------------------------------------------------------------------ */
/* Public entry points (declared locally: see nj_codec.c, which declares   */
/* exactly these three when NJ_ENABLE_JPEG is on)                          */
/* ------------------------------------------------------------------ */
int       nj_jpeg_accepts(const void *data, nj_u4 size);
nj_status nj_jpeg_info(const void *data, nj_u4 size, nj_image_info *out);
nj_image *nj_jpeg_decode(const nj_codec_req *req);

#if NJ_ENABLE_JPEG

/* ------------------------------------------------------------------ */
/* Limits                                                             */
/* ------------------------------------------------------------------ */

/* One dimension cap; also the bound Image.createImage() is checked against. */
#define NJ_JPEG_MAX_DIM     8192

#define NJ_JPEG_MAX_COMPS   4
#define NJ_JPEG_MAX_SCANS   64     /* a sequential file needs one per component;
                                    * a progressive file holds one scan per
                                    * component per successive-approximation
                                    * step, and 64 covers every encoder seen */

/*
 * Progressive (SOF2) decoding.  It is on by default; a build that cannot
 * spare the coefficient planes can compile it out with
 * -DNJ_ENABLE_JPEG_PROGRESSIVE=0 and get exactly the old behaviour (SOF2 is
 * then refused with a clear message, as before).
 *
 * The one real cost is the coefficient store: two bytes per coefficient for
 * every block of every component, i.e. two bytes per sample for 4:4:4 and
 * 1.5 bytes per sample for 4:2:0.  A 240x320 4:2:0 image needs ~150 KiB, held
 * only while the scans are decoded and released before the image is returned.
 * A hostile file cannot make that arbitrary: files needing more than
 * NJ_JPEG_PROGRESSIVE_MAX_BYTES of coefficient store are refused up front.
 */
#ifndef NJ_ENABLE_JPEG_PROGRESSIVE
#  define NJ_ENABLE_JPEG_PROGRESSIVE 1
#endif
#ifndef NJ_JPEG_PROGRESSIVE_MAX_BYTES
#  define NJ_JPEG_PROGRESSIVE_MAX_BYTES (4u * 1024u * 1024u)
#endif

/* ------------------------------------------------------------------ */
/* Read-only tables                                                   */
/* ------------------------------------------------------------------ */

/* Zig-zag order: s_zigzag[k] is the natural (raster) index of the k-th
 * coefficient as it appears in the entropy-coded stream. */
static const nj_u1 s_zigzag[64] = {
     0u,  1u,  8u, 16u,  9u,  2u,  3u, 10u,
    17u, 24u, 32u, 25u, 18u, 11u,  4u,  5u,
    12u, 19u, 26u, 33u, 40u, 48u, 41u, 34u,
    27u, 20u, 13u,  6u,  7u, 14u, 21u, 28u,
    35u, 42u, 49u, 56u, 57u, 50u, 43u, 36u,
    29u, 22u, 15u, 23u, 30u, 37u, 44u, 51u,
    58u, 59u, 52u, 45u, 38u, 31u, 39u, 46u,
    53u, 60u, 61u, 54u, 47u, 55u, 62u, 63u
};

/*
 * IDCT kernels, Q12, with the normalisation and the reduced-size (box)
 * weights already folded in:
 *
 *     s_idct_kern[k][u][i] = round(C(u) * W(u) * cos((2i+1)u*PI/(2N)) * 4096)
 *
 * for the output size N (k = 0 -> 8 (1/1), 1 -> 4 (1/2), 2 -> 2 (1/4)),
 * C(0) = 1/sqrt(2), C(u>0) = 1, and the box weight
 *
 *     W(u) = sin(S*theta) / (S*sin(theta)),  theta = u*PI/16, S = 8/N
 *
 * which is 1 at u = 0 (so flat colours survive, see jp_idct_store()) and is
 * what turns "keep the low-frequency coefficients" into an exact SxS box
 * average of the block rather than a crop of its top-left corner.  Only the
 * first N columns of each row are used.
 */
static const nj_s2 s_idct_kern[3][8][8] = {
    /* 1/1 */
    {
        {  2896,  2896,  2896,  2896,  2896,  2896,  2896,  2896 },
        {  4017,  3406,  2276,   799,  -799, -2276, -3406, -4017 },
        {  3784,  1567, -1567, -3784, -3784, -1567,  1567,  3784 },
        {  3406,  -799, -4017, -2276,  2276,  4017,   799, -3406 },
        {  2896, -2896, -2896,  2896,  2896, -2896, -2896,  2896 },
        {  2276, -4017,   799,  3406, -3406,  -799,  4017, -2276 },
        {  1567, -3784,  3784, -1567, -1567,  3784, -3784,  1567 },
        {   799, -2276,  3406, -4017,  4017, -3406,  2276,  -799 }
    },
    /* 1/2 */
    {
        {  2896,  2896,  2896,  2896,     0,     0,     0,     0 },
        {  3711,  1537, -1537, -3711,     0,     0,     0,     0 },
        {  2676, -2676, -2676,  2676,     0,     0,     0,     0 },
        {  1303, -3146,  3146, -1303,     0,     0,     0,     0 },
        {     0,     0,     0,     0,     0,     0,     0,     0 },
        {  -871,  2102, -2102,   871,     0,     0,     0,     0 },
        { -1108,  1108,  1108, -1108,     0,     0,     0,     0 },
        {  -738,  -306,   306,   738,     0,     0,     0,     0 }
    },
    /* 1/4 */
    {
        {  2896,  2896,     0,     0,     0,     0,     0,     0 },
        {  2624, -2624,     0,     0,     0,     0,     0,     0 },
        {     0,     0,     0,     0,     0,     0,     0,     0 },
        {  -922,   922,     0,     0,     0,     0,     0,     0 },
        {     0,     0,     0,     0,     0,     0,     0,     0 },
        {   616,  -616,     0,     0,     0,     0,     0,     0 },
        {     0,     0,     0,     0,     0,     0,     0,     0 },
        {  -522,   522,     0,     0,     0,     0,     0,     0 }
    }
};

/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */

/* Entropy-coded bit reader.  `buf` holds `n` bits in its high end. */
typedef struct jp_bits {
    const nj_u1 *p;      /* next byte to read                        */
    const nj_u1 *end;    /* end of the whole file                    */
    nj_u4  buf;          /* bit buffer, left aligned                 */
    int    n;            /* valid bits in buf, 0..32                 */
    int    eof;          /* stopped on a marker or the end of data   */
    int    overrun;      /* had to synthesise bits that are not there */
} jp_bits;

/* A derived Huffman table (T.81 Annex C). */
typedef struct jp_huff {
    nj_u1 vals[256];
    nj_s4 mincode[17];
    nj_s4 maxcode[17];
    nj_s4 valoffset[17];
    int   total;
    int   defined;
} jp_huff;

/* One frame component and its decoded sample plane. */
typedef struct jp_comp {
    int    id, h, v, tq, td, ta;
    int    bw, bh;       /* real block grid (may be smaller than the plane) */
    int    pw, ph;       /* plane width/height in samples (MCU padded)      */
    nj_u1 *plane;        /* pw * ph bytes, row-major                        */
    int    cw, ch;       /* coefficient grid: max(real blocks, MCU grid)    */
    nj_s2 *coef;         /* cw * ch * 64 coefficients, progressive only     */
    int    pred;         /* DC predictor                                    */
    int    scanned;      /* covered by a scan                               */
} jp_comp;

/* Everything a DHT segment can define: 4 DC + 4 AC tables. */
typedef struct jp_tables {
    jp_huff dc[4];
    jp_huff ac[4];
} jp_tables;

/* The whole decode.  Lives on the stack; nothing here is global. */
typedef struct jp_dec {
    const nj_u1 *p, *end;

    const nj_codec_req *req;      /* allocator hooks and error slot */
    void *(*alloc)(void *ud, nj_size n);
    void  (*dealloc)(void *ud, void *p);
    void  *ud;

    jp_tables *tabs;             /* req->alloc'd: ~4 KB, not stack friendly */
    nj_u2 quant[4][64];          /* in NATURAL order, not zig-zag           */
    nj_u1 quant_ok[4];

    jp_comp comp[NJ_JPEG_MAX_COMPS];
    jp_comp *scan[NJ_JPEG_MAX_COMPS];
    int     nscan;

    int w, h, prec, ncomp;
    int hmax, vmax;
    int out_w, out_h;
    int scale, nkeep;            /* 1/2/4 and 8/4/2 */
    int adobe;                   /* APP14 transform, or -1                  */
    int jfif;
    int restart_interval;
    int seen_sof;
    int planes_ready;
    int truncated;               /* the scan ran out of entropy data        */
    int done;                    /* reached EOI                             */

    int progressive;             /* SOF2: scans fill coefficient grids      */
    int coef_ready;
    int ss, se, ah, al;          /* current scan band / approximation       */
    nj_s4 eobrun;                /* progressive AC end-of-band run left     */

    nj_u4 mcus_x, mcus_y;
    int   want_w, want_h;
} jp_dec;

/* ------------------------------------------------------------------ */
/* Small numeric helpers                                              */
/* ------------------------------------------------------------------ */

/*
 * (v + half) / 2^s, rounding, but only ever shifting a non-negative value:
 * C99 defines a right shift of a negative value as implementation-defined,
 * and this decoder promises identical output everywhere.
 */
static nj_s4 jp_descale(nj_s4 v, int s)
{
    nj_s4 half = (nj_s4)1 << (s - 1);

    if (v >= 0) {
        return (nj_s4)((v + half) >> s);
    }
    return (nj_s4)(-(((-v) + half) >> s));
}

static int jp_clamp(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void jp_fail(const nj_codec_req *req, const char *msg)
{
    if (req != NULL && req->err != NULL) {
        *req->err = msg;
    }
}

/* ------------------------------------------------------------------ */
/* Entropy-coded bit reader                                           */
/* ------------------------------------------------------------------ */

static void jp_bits_init(jp_bits *b, const nj_u1 *p, const nj_u1 *end)
{
    b->p = p;
    b->end = end;
    b->buf = 0u;
    b->n = 0;
    b->eof = 0;
    b->overrun = 0;
}

/* Next entropy-coded byte: 0xFF00 is a stuffed 0xFF, anything else after a
 * 0xFF is a marker and stops the reader without consuming it. */
static int jp_next_entropy_byte(jp_bits *b)
{
    nj_u1 c;

    if (b->p >= b->end) {
        b->eof = 1;
        return -1;
    }
    c = *b->p;
    if (c != 0xFFu) {
        b->p++;
        return (int)c;
    }
    if (b->p + 1 >= b->end) {
        b->p = b->end;
        b->eof = 1;
        return -1;
    }
    if (b->p[1] == 0x00u) {
        b->p += 2;
        return 0xFF;
    }
    b->eof = 1;                 /* a marker: leave p on its 0xFF */
    return -1;
}

static void jp_fill(jp_bits *b)
{
    while (b->n <= 24) {
        int c = jp_next_entropy_byte(b);
        if (c < 0) {
            b->eof = 1;
            break;
        }
        b->buf |= (nj_u4)(nj_u1)c << (24 - b->n);
        b->n += 8;
    }
}

/*
 * Read k bits (1 <= k <= 16).  Past the end of the entropy-coded data the
 * reader synthesises zero bits and sets `overrun`: T.81 pads with 1 bits, but
 * either padding only ever feeds the decoder the "no more coefficients" path,
 * and a flag the caller can act on is more useful than a guess.  The shift
 * count can never be 32 because k <= 16.
 */
static nj_u4 jp_get_bits(jp_bits *b, int k)
{
    nj_u4 v;

    jp_fill(b);
    if (b->n < k) {
        b->overrun = 1;
        v = (b->n > 0) ? (b->buf >> (32 - b->n)) : 0u;
        v <<= (k - b->n);
        b->buf = 0u;
        b->n = 0;
        return v;
    }
    v = b->buf >> (32 - k);
    b->buf <<= k;
    b->n -= k;
    return v;
}

/* ------------------------------------------------------------------ */
/* Huffman tables                                                     */
/* ------------------------------------------------------------------ */

/*
 * Build a derived table from the 16 code counts and `total` values.
 * Validates the two things T.81 leaves to the decoder: the table must not be
 * over-subscribed (Annex C: the codes of length n may not exhaust the 2^n
 * code space) and it must not be empty.
 */
static const char *jp_build_huff(jp_huff *h, const nj_u1 *counts,
                                 const nj_u1 *vals, int total)
{
    int code = 0;
    int k = 0;
    int len;

    if (total <= 0) {
        return "JPEG Huffman table has no codes";
    }
    for (len = 1; len <= 16; len++) {
        int bits = (int)counts[len - 1];

        if (code + bits > (1 << len)) {
            return "over-subscribed JPEG Huffman table";
        }
        h->mincode[len] = (nj_s4)code;
        h->maxcode[len] = (nj_s4)(code + bits - 1);
        h->valoffset[len] = (nj_s4)k - (nj_s4)code;
        code += bits;
        k += bits;
        code <<= 1;
    }
    h->mincode[0] = 0;
    h->maxcode[0] = -1;
    h->valoffset[0] = 0;
    memcpy(h->vals, vals, (size_t)total);
    h->total = total;
    h->defined = 1;
    return NULL;
}

/*
 * Decode one Huffman code word.  The walk is bounded to 16 bits; a code word
 * that is all ones (which T.81 reserves as the padding code and which no
 * legal table assigns) simply never matches, so the walk runs out and the
 * caller sees end-of-data rather than an out-of-bounds table index.
 */
static int jp_huff_decode(jp_bits *b, const jp_huff *h, int *symbol)
{
    int code = (int)jp_get_bits(b, 1);
    int len = 1;

    while (code > (int)h->maxcode[len]) {
        if (len >= 16) {
            return -1;
        }
        code = (code << 1) | (int)jp_get_bits(b, 1);
        len++;
    }
    {
        int idx = (int)h->valoffset[len] + code;

        if (idx < 0 || idx >= h->total) {
            return -1;
        }
        *symbol = (int)h->vals[idx];
    }
    return 0;
}

/* T.81 F.2.2.1: sign extension of a magnitude-coded coefficient. */
static int jp_receive_extend(jp_bits *b, int t)
{
    int v;

    if (t == 0) {
        return 0;
    }
    v = (int)jp_get_bits(b, t);
    if (v < (1 << (t - 1))) {
        v -= (1 << t) - 1;      /* written this way: -1 << t is UB in C99 */
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                           */
/* ------------------------------------------------------------------ */

static nj_u4 jp_ceil_div(nj_u4 a, nj_u4 b)
{
    return (a + b - 1u) / b;
}

/*
 * Honour want_width/want_height as the DCT-domain scale hint.  Only an exact
 * half or quarter request in both axes is taken (either rounding of an odd
 * dimension is accepted); every other request decodes natively and the caller
 * resamples, as nj_codec.h specifies.  Returns 1, 2 or 4.
 */
static int jp_choose_scale(int w, int h, int want_w, int want_h)
{
    if (want_w <= 0 || want_h <= 0) {
        return 1;
    }
    if ((want_w == w / 2 || want_w == (w + 1) / 2) &&
        (want_h == h / 2 || want_h == (h + 1) / 2)) {
        return 2;
    }
    if ((want_w == w / 4 || want_w == (w + 3) / 4) &&
        (want_h == h / 4 || want_h == (h + 3) / 4)) {
        return 4;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Allocation                                                         */
/* ------------------------------------------------------------------ */

/* Allocate and zero the output image through the runtime helper, which does
 * the 64-bit size check and stores NJ_PIXFMT_ARGB8888 (an nj_image::format is
 * an NJ_PIXFMT_* value, not an NJ_CODEC_CAN() capability bit). */
static nj_image *jp_image_new(jp_dec *d, int w, int h)
{
    return nj_codec_image_new(d->req, w, h, NJ_PIXFMT_ARGB8888, 0);
}

static void jp_image_free(jp_dec *d, nj_image *img)
{
    if (img != NULL) {
        d->dealloc(d->ud, img->pixels);
        d->dealloc(d->ud, img);
    }
}

/* The single release path: safe to call twice, safe after a partial setup. */
static void jp_free(jp_dec *d)
{
    int i;

    for (i = 0; i < NJ_JPEG_MAX_COMPS; i++) {
        if (d->comp[i].plane != NULL) {
            d->dealloc(d->ud, d->comp[i].plane);
            d->comp[i].plane = NULL;
        }
        if (d->comp[i].coef != NULL) {
            d->dealloc(d->ud, d->comp[i].coef);
            d->comp[i].coef = NULL;
        }
    }
    if (d->tabs != NULL) {
        d->dealloc(d->ud, d->tabs);
        d->tabs = NULL;
    }
}

/*
 * Allocate one sample plane per component, MCU padded so that an interleaved
 * scan can write every block it decodes without a bounds test.  The padding
 * is never sampled: the upsampler only reaches ceil(w*h/h_max) samples, and
 * every component is required to appear in a scan before the image is built.
 *
 * The planes are filled with 128 - the neutral level of the JPEG level shift,
 * and therefore also exactly what the IDCT produces for a missing block.  It
 * is one linear pass, negligible next to the transform, and it means a path
 * that ever fails to write a sample yields a sane deterministic pixel instead
 * of whatever malloc handed back.
 */
static const char *jp_alloc_planes(jp_dec *d)
{
    int i;

    for (i = 0; i < d->ncomp; i++) {
        jp_comp *c = &d->comp[i];
        nj_u8 bytes = (nj_u8)c->pw * (nj_u8)c->ph;

        if (bytes == 0u || bytes > (nj_u8)0x7FFFFFFFu) {
            return "JPEG is too large";
        }
        c->plane = (nj_u1 *)d->alloc(d->ud, (nj_size)bytes);
        if (c->plane == NULL) {
            return "out of memory for the JPEG component planes";
        }
        memset(c->plane, 128, (size_t)bytes);
        c->pred = 0;
    }
    d->planes_ready = 1;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Marker segments                                                    */
/* ------------------------------------------------------------------ */

static const char *jp_parse_sof(jp_dec *d, const nj_u1 *seg, nj_u4 len)
{
    int nf, i, j, sum = 0;

    if (d->seen_sof) {
        return "duplicate JPEG frame header";
    }
    if (len < 6u) {
        return "truncated JPEG frame header";
    }
    d->prec = (int)seg[0];
    d->h = ((int)seg[1] << 8) | (int)seg[2];
    d->w = ((int)seg[3] << 8) | (int)seg[4];
    nf = (int)seg[5];

    if (d->prec != 8) {
        return "only 8-bit JPEG sample precision is supported";
    }
    if (d->w <= 0 || d->h <= 0) {
        return "JPEG frame has an invalid size";
    }
    if (d->w > NJ_JPEG_MAX_DIM || d->h > NJ_JPEG_MAX_DIM) {
        return "JPEG is too large";
    }
    if (nf != 1 && nf != 3 && nf != 4) {
        return "unsupported number of JPEG components";
    }
    if (len < (nj_u4)(6 + 3 * nf)) {
        return "truncated JPEG frame header";
    }
    d->ncomp = nf;
    for (i = 0; i < nf; i++) {
        jp_comp *c = &d->comp[i];
        int hv = (int)seg[7 + 3 * i];

        c->id = (int)seg[6 + 3 * i];
        c->h = (hv >> 4) & 15;
        c->v = hv & 15;
        c->tq = (int)seg[8 + 3 * i];
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4) {
            return "invalid JPEG component sampling factors";
        }
        if (c->tq > 3) {
            return "invalid JPEG quantisation table selector";
        }
        sum += c->h * c->v;
        if (c->h > d->hmax) {
            d->hmax = c->h;
        }
        if (c->v > d->vmax) {
            d->vmax = c->v;
        }
    }
    if (sum > 10) {
        return "unsupported JPEG sampling factors";
    }
    for (i = 0; i < nf; i++) {
        for (j = i + 1; j < nf; j++) {
            if (d->comp[i].id == d->comp[j].id) {
                return "duplicate JPEG component id";
            }
        }
    }

    d->scale = jp_choose_scale(d->w, d->h, d->want_w, d->want_h);
    d->nkeep = 8 / d->scale;
    d->out_w = (d->w + d->scale - 1) / d->scale;
    d->out_h = (d->h + d->scale - 1) / d->scale;
    d->mcus_x = jp_ceil_div((nj_u4)d->w, (nj_u4)(8 * d->hmax));
    d->mcus_y = jp_ceil_div((nj_u4)d->h, (nj_u4)(8 * d->vmax));
    for (i = 0; i < nf; i++) {
        jp_comp *c = &d->comp[i];

        c->bw = (int)jp_ceil_div((nj_u4)(d->w * c->h), (nj_u4)(d->hmax * 8));
        c->bh = (int)jp_ceil_div((nj_u4)(d->h * c->v), (nj_u4)(d->vmax * 8));
        c->pw = (int)(d->mcus_x * (nj_u4)c->h) * d->nkeep;
        c->ph = (int)(d->mcus_y * (nj_u4)c->v) * d->nkeep;
        /* The coefficient grid has to hold both the real block grid (used
         * by single-component scans) and the MCU-padded grid (used by
         * interleaved scans, which decode whole MCUs including the padding
         * blocks). */
        c->cw = c->bw;
        c->ch = c->bh;
        if ((int)(d->mcus_x * (nj_u4)c->h) > c->cw) {
            c->cw = (int)(d->mcus_x * (nj_u4)c->h);
        }
        if ((int)(d->mcus_y * (nj_u4)c->v) > c->ch) {
            c->ch = (int)(d->mcus_y * (nj_u4)c->v);
        }
    }
    d->seen_sof = 1;
    return NULL;
}

static const char *jp_parse_dqt(jp_dec *d, const nj_u1 *seg, nj_u4 len)
{
    nj_u4 i = 0;

    while (i < len) {
        int pq = ((int)seg[i] >> 4) & 15;
        int tq = (int)seg[i] & 15;
        int k;

        i++;
        if (tq > 3) {
            return "invalid JPEG quantisation table selector";
        }
        if (pq != 0 && pq != 1) {
            return "invalid JPEG quantisation table precision";
        }
        if (len - i < (pq ? 128u : 64u)) {
            return "truncated JPEG quantisation table";
        }
        for (k = 0; k < 64; k++) {
            int v;

            if (pq != 0) {
                v = ((int)seg[i] << 8) | (int)seg[i + 1];
                i += 2u;
            } else {
                v = (int)seg[i];
                i += 1u;
            }
            /* Stored in natural order; the segment is in zig-zag order. */
            d->quant[tq][s_zigzag[k]] = (nj_u2)v;
        }
        d->quant_ok[tq] = 1u;
    }
    return NULL;
}

static const char *jp_parse_dht(jp_dec *d, const nj_u1 *seg, nj_u4 len)
{
    nj_u4 i = 0;

    while (i < len) {
        int tc, th, total = 0, k;
        const nj_u1 *counts;
        const char *e;

        if (len - i < 17u) {
            return "truncated JPEG Huffman table";
        }
        tc = ((int)seg[i] >> 4) & 15;
        th = (int)seg[i] & 15;
        i++;
        if (tc > 1) {
            return "invalid JPEG Huffman table class";
        }
        if (th > 3) {
            return "invalid JPEG Huffman table selector";
        }
        counts = seg + i;
        for (k = 0; k < 16; k++) {
            total += (int)counts[k];
        }
        i += 16u;
        if (total > 256) {
            return "JPEG Huffman table has too many codes";
        }
        if (len - i < (nj_u4)total) {
            return "truncated JPEG Huffman table";
        }
        e = jp_build_huff((tc == 0) ? &d->tabs->dc[th] : &d->tabs->ac[th],
                          counts, seg + i, total);
        if (e != NULL) {
            return e;
        }
        i += (nj_u4)total;
    }
    return NULL;
}

static const char *jp_parse_dri(jp_dec *d, const nj_u1 *seg, nj_u4 len)
{
    if (len < 2u) {
        return "truncated JPEG restart interval";
    }
    d->restart_interval = ((int)seg[0] << 8) | (int)seg[1];
    return NULL;
}

static const char *jp_parse_sos(jp_dec *d, const nj_u1 *seg, nj_u4 len)
{
    int ns, i, j;

    if (!d->seen_sof) {
        return "JPEG scan appears before the frame header";
    }
    if (len < 1u) {
        return "truncated JPEG scan header";
    }
    ns = (int)seg[0];
    if (ns < 1 || ns > d->ncomp) {
        return "invalid JPEG scan component count";
    }
    if (len < (nj_u4)(1 + 2 * ns + 3)) {
        return "truncated JPEG scan header";
    }
    /* Ss/Se/Ah,Al: a sequential scan covers the whole band with no
     * successive approximation; a progressive scan carries a band and an
     * approximation step (T.81 G.1.1.1.1). */
    if (d->progressive) {
        int ss = (int)seg[1 + 2 * ns];
        int se = (int)seg[2 + 2 * ns];
        int aa = (int)seg[3 + 2 * ns];
        int ah = (aa >> 4) & 15;
        int al = aa & 15;

        if (ss > se || se > 63 || ah > 13 || al > 13) {
            return "invalid progressive JPEG scan parameters";
        }
        if (ss == 0 && se != 0) {
            return "invalid progressive JPEG DC scan band";
        }
        if (ss > 0 && ns != 1) {
            return "progressive JPEG AC scans must be non-interleaved";
        }
        d->ss = ss;
        d->se = se;
        d->ah = ah;
        d->al = al;
    } else if (seg[1 + 2 * ns] != 0u || seg[2 + 2 * ns] != 63u ||
               seg[3 + 2 * ns] != 0u) {
        return "only baseline sequential JPEG scans are supported";
    }

    for (i = 0; i < ns; i++) {
        int cs = (int)seg[1 + 2 * i];
        int td = ((int)seg[2 + 2 * i] >> 4) & 15;
        int ta = (int)seg[2 + 2 * i] & 15;
        jp_comp *c = NULL;

        if (td > 3 || ta > 3) {
            return "invalid JPEG Huffman table selector";
        }
        for (j = 0; j < d->ncomp; j++) {
            if (d->comp[j].id == cs) {
                c = &d->comp[j];
                break;
            }
        }
        if (c == NULL) {
            return "JPEG scan references an unknown component";
        }
        for (j = 0; j < i; j++) {
            if (d->scan[j] == c) {
                return "duplicate component in JPEG scan";
            }
        }
        if (d->progressive) {
            /* A DC scan needs the DC table only, an AC scan the AC table
             * only: encoders leave the unused one undefined on purpose. */
            if ((d->ss == 0 && !d->tabs->dc[td].defined) ||
                (d->ss != 0 && !d->tabs->ac[ta].defined)) {
                return "JPEG scan uses an undefined Huffman table";
            }
        } else if (!d->tabs->dc[td].defined || !d->tabs->ac[ta].defined) {
            return "JPEG scan uses an undefined Huffman table";
        }
        if (d->quant_ok[c->tq] == 0u) {
            return "JPEG scan uses an undefined quantisation table";
        }
        c->td = td;
        c->ta = ta;
        c->scanned = 1;
        d->scan[i] = c;
    }
    d->nscan = ns;
    return NULL;
}

/*
 * Walk markers up to and including the next SOS, leaving d->p on the
 * entropy-coded data.  Sets d->done at EOI (or at the end of the input).
 */
static const char *jp_next_scan(jp_dec *d)
{
    for (;;) {
        int marker;
        nj_u4 len;

        if (d->p >= d->end) {
            d->done = 1;
            return NULL;
        }
        if (*d->p != 0xFFu) {
            return "expected a JPEG marker";
        }
        while (d->p < d->end && *d->p == 0xFFu) {
            d->p++;                       /* fill bytes */
        }
        if (d->p >= d->end) {
            d->done = 1;
            return NULL;
        }
        marker = (int)*d->p++;

        if (marker == 0xD9) {             /* EOI */
            d->done = 1;
            return NULL;
        }
        if (marker == 0xD8) {             /* stray SOI */
            continue;
        }
        if (marker == 0x00) {
            return "unexpected stuffed byte outside a JPEG scan";
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;                     /* TEM and stray RSTn */
        }
        if (d->end - d->p < 2) {
            return "truncated JPEG segment header";
        }
        len = ((nj_u4)d->p[0] << 8) | (nj_u4)d->p[1];
        if (len < 2u) {
            return "invalid JPEG segment length";
        }
        if ((nj_u4)(d->end - d->p) < len) {
            return "JPEG segment runs past the end of the data";
        }
        {
            const nj_u1 *seg = d->p + 2;
            nj_u4 seglen = len - 2u;
            const char *e = NULL;

            switch (marker) {
            case 0xC0:                    /* baseline sequential */
            case 0xC1:                    /* extended sequential, 8-bit */
                e = jp_parse_sof(d, seg, seglen);
                break;
            case 0xC2:                    /* progressive */
#if NJ_ENABLE_JPEG_PROGRESSIVE
                e = jp_parse_sof(d, seg, seglen);
                if (e == NULL) {
                    d->progressive = 1;
                }
                break;
#else
                return "progressive JPEG is not supported by this decoder";
#endif
            case 0xC3:
                return "lossless JPEG is not supported by this decoder";
            case 0xC5: case 0xC6: case 0xC7:
            case 0xC9: case 0xCA: case 0xCB:
            case 0xCD: case 0xCE: case 0xCF:
                return "arithmetic-coded or differential JPEG is not "
                       "supported by this decoder";
            case 0xC4:
                e = jp_parse_dht(d, seg, seglen);
                break;
            case 0xDB:
                e = jp_parse_dqt(d, seg, seglen);
                break;
            case 0xDD:
                e = jp_parse_dri(d, seg, seglen);
                break;
            case 0xDA:
                e = jp_parse_sos(d, seg, seglen);
                if (e == NULL) {
                    d->p += len;          /* now at the scan data */
                    return NULL;
                }
                break;
            case 0xE0:
                if (seglen >= 5u && memcmp(seg, "JFIF\0", 5u) == 0) {
                    d->jfif = 1;
                }
                break;
            case 0xEE:
                if (seglen >= 12u && memcmp(seg, "Adobe", 5u) == 0) {
                    d->adobe = (int)seg[11];   /* colour transform */
                }
                break;
            default:
                break;                    /* APPn, COM, DNL: skipped */
            }
            if (e != NULL) {
                return e;
            }
        }
        d->p += len;
    }
}

/* ------------------------------------------------------------------ */
/* Entropy-coded scan                                                 */
/* ------------------------------------------------------------------ */

/*
 * Consume the RSTn marker that ends a restart interval.  The bit buffer is
 * dropped (the encoder pads the last byte with 1 bits), the marker must be the
 * one the sequence expects, and the reader resumes after it.
 */
static int jp_restart(jp_bits *b, int *expected)
{
    const nj_u1 *p = b->p;
    int code;

    b->buf = 0u;
    b->n = 0;
    b->eof = 0;
    while (p < b->end && *p == 0xFFu) {
        p++;
    }
    if (p >= b->end) {
        return -1;
    }
    code = (int)*p;
    if (code < 0xD0 || code > 0xD7) {
        return -1;
    }
    if (code != *expected) {
        return -2;
    }
    *expected = 0xD0 + ((code - 0xD0 + 1) & 7);
    b->p = p + 1;
    return 0;
}

/*
 * Dequantise and inverse-transform one block into the component plane.
 *
 * At full size this is the plain separable 8x8 IDCT.  At 1/2 and 1/4 it is
 * the same transform evaluated for 4 (2) output positions per axis with every
 * input frequency kept and weighted by the box factor W(u) built into
 * s_idct_kern: that produces the exact SxS average of the block, which is what
 * "decode at half size" has to mean (a transform that merely dropped the
 * higher frequencies and sampled the survivors at consecutive positions would
 * return the block's top-left corner smoothed, and a game's half-size splash
 * screen would come out visibly cropped and wrong - measured against libjpeg's
 * own reduced decode, this version agrees to within one level, that one did
 * not).  W(0) = 1 keeps the DC term, and therefore flat colours, exact.
 */
static void jp_idct_store(jp_dec *d, const nj_s4 *coef, jp_comp *c,
                          int bx, int by)
{
    const int n = d->nkeep;
    const int k = (d->scale == 1) ? 0 : ((d->scale == 2) ? 1 : 2);
    const nj_s2 (*K)[8] = s_idct_kern[k];
    const nj_u2 *q = d->quant[c->tq];
    nj_s4 blk[64];
    nj_s4 tmp[64];
    nj_u1 out[64];
    nj_u1 *dst;
    int i, j, u, v, flat;

    /* Dequantise (clamped: a hostile 16-bit table must not overflow). */
    flat = 1;
    for (v = 0; v < 8; v++) {
        for (u = 0; u < 8; u++) {
            nj_s4 cv = coef[v * 8 + u];
            nj_s4 val = 0;

            if (cv != 0) {
                if (cv > 32767) {
                    cv = 32767;
                } else if (cv < -32767) {
                    cv = -32767;
                }
                val = cv * (nj_s4)q[v * 8 + u];
                /* Clamped, not wrapped: an extreme (or hostile) 16-bit
                 * quantisation value must not overflow the transform. */
                val = (val > 16384) ? 16384 : ((val < -16384) ? -16384 : val);
                if (v != 0 || u != 0) {
                    flat = 0;
                }
            }
            blk[v * 8 + u] = val;
        }
    }

    if (flat) {
        /* DC only: the whole block is one level, which is what most of a
         * game's background art consists of.  Exactly the same fixed-point
         * path as below, with only u = v = 0 surviving. */
        nj_s4 dc = jp_descale(blk[0] * (nj_s4)K[0][0], 14);

        dc = jp_descale(dc * (nj_s4)K[0][0], 12) + 128;
        v = jp_clamp((int)dc, 0, 255);
        memset(out, v, (size_t)(n * n));
    } else {
        /* Pass 1: horizontal, every u, n output positions per frequency row. */
        for (v = 0; v < 8; v++) {
            const nj_s4 *row = &blk[v * 8];

            for (i = 0; i < n; i++) {
                nj_s4 acc = 0;

                for (u = 0; u < 8; u++) {
                    nj_s4 cv = row[u];

                    if (cv != 0) {
                        acc += cv * (nj_s4)K[u][i];
                    }
                }
                tmp[v * n + i] = jp_descale(acc, 14);
            }
        }
        /* Pass 2: vertical, then the level shift. */
        for (i = 0; i < n; i++) {
            for (j = 0; j < n; j++) {
                nj_s4 acc = 0;

                for (v = 0; v < 8; v++) {
                    nj_s4 cv = tmp[v * n + i];

                    if (cv != 0) {
                        acc += cv * (nj_s4)K[v][j];
                    }
                }
                out[j * n + i] = (nj_u1)jp_clamp(jp_descale(acc, 12) + 128,
                                                 0, 255);
            }
        }
    }

    dst = c->plane + (nj_size)by * (nj_size)n * (nj_size)c->pw +
          (nj_size)bx * (nj_size)n;
    for (j = 0; j < n; j++) {
        memcpy(dst + (nj_size)j * (nj_size)c->pw, out + j * n, (size_t)n);
    }
}

/*
 * Decode one 8x8 block into (bx, by) of the component's plane.  Returns 0 on
 * success (including "the data ran out inside this block", which leaves the
 * block neutral and sets d->truncated) or -1 for a corrupt stream.
 */
static int jp_decode_block(jp_dec *d, jp_bits *b, jp_comp *c, int bx, int by)
{
    const jp_huff *dc = &d->tabs->dc[c->td];
    const jp_huff *ac = &d->tabs->ac[c->ta];
    nj_s4 coef[64];
    int k, s, r, t, v;

    memset(coef, 0, sizeof(coef));

    if (jp_huff_decode(b, dc, &t) != 0 || t > 15) {
        if (b->overrun) {
            d->truncated = 1;
            jp_idct_store(d, coef, c, bx, by);
            return 0;
        }
        return -1;
    }
    v = jp_receive_extend(b, t);
    /* Bounded so a hostile stream cannot overflow the predictor. */
    c->pred = jp_clamp(c->pred + v, -32767, 32767);
    coef[0] = c->pred;

    k = 1;
    while (k < 64) {
        if (b->overrun) {
            d->truncated = 1;
            break;
        }
        if (jp_huff_decode(b, ac, &s) != 0) {
            if (b->overrun) {
                d->truncated = 1;
                break;
            }
            return -1;
        }
        r = s >> 4;
        s &= 15;
        if (s == 0) {
            if (r != 15) {
                break;                    /* EOB */
            }
            if (k + 16 > 64) {
                if (b->overrun) {
                    d->truncated = 1;
                    break;
                }
                return -1;
            }
            k += 16;                      /* ZRL: sixteen zero coefficients */
            continue;
        }
        k += r;
        if (k > 63) {
            if (b->overrun) {
                d->truncated = 1;
                break;
            }
            return -1;
        }
        v = jp_receive_extend(b, s);
        coef[(int)s_zigzag[k]] = (nj_s4)v;
        k++;
    }

    jp_idct_store(d, coef, c, bx, by);
    return 0;
}

#if NJ_ENABLE_JPEG_PROGRESSIVE
static const char *jp_decode_scan_prog(jp_dec *d);
#endif

static const char *jp_decode_scan(jp_dec *d)
{
    jp_bits b;
    nj_u4 mx, my, mcu = 0u;
    nj_u4 cols, rows, total;
    int expected_rst = 0xD0;

    d->truncated = 0;      /* per scan: each one ends with its own padding */
#if NJ_ENABLE_JPEG_PROGRESSIVE
    if (d->progressive) {
        return jp_decode_scan_prog(d);
    }
#endif
    if (!d->planes_ready) {
        const char *e = jp_alloc_planes(d);
        if (e != NULL) {
            return e;
        }
    }

    if (d->nscan == 1) {
        /* A single-component scan is a plain block raster, not an MCU grid. */
        cols = (nj_u4)d->scan[0]->bw;
        rows = (nj_u4)d->scan[0]->bh;
    } else {
        cols = d->mcus_x;
        rows = d->mcus_y;
    }
    total = cols * rows;
    if (total == 0u) {
        return "JPEG scan has no blocks";
    }

    jp_bits_init(&b, d->p, d->end);
    for (my = 0u; my < rows; my++) {
        for (mx = 0u; mx < cols; mx++, mcu++) {
            int i;

            if (d->truncated) {
                /* Only the final MCU of the image may be short; anything
                 * earlier means the file was cut in half.  The blocks the
                 * missing bytes would have filled still go through the
                 * decoder below - as zero blocks, which the IDCT turns into
                 * neutral grey - so that every sample of the plane is written
                 * and no uninitialised memory can reach the output. */
                if (mcu + 1u < total) {
                    return "JPEG scan data ended early";
                }
            } else if (d->restart_interval > 0 &&
                       mcu > 0u && (mcu % (nj_u4)d->restart_interval) == 0u) {
                int r = jp_restart(&b, &expected_rst);

                if (r == -2) {
                    return "JPEG restart marker is out of sequence";
                }
                if (r != 0) {
                    return "JPEG restart marker is missing";
                }
                for (i = 0; i < d->ncomp; i++) {
                    d->comp[i].pred = 0;
                }
            }
            for (i = 0; i < d->nscan; i++) {
                jp_comp *c = d->scan[i];
                int bh = (d->nscan == 1) ? 1 : c->h;
                int bv = (d->nscan == 1) ? 1 : c->v;
                int bxx, byy;

                for (byy = 0; byy < bv; byy++) {
                    for (bxx = 0; bxx < bh; bxx++) {
                        int cx, cy;

                        if (d->nscan == 1) {
                            cx = (int)mx;
                            cy = (int)my;
                        } else {
                            cx = (int)mx * c->h + bxx;
                            cy = (int)my * c->v + byy;
                        }
                        if (jp_decode_block(d, &b, c, cx, cy) != 0) {
                            return "corrupt JPEG scan data";
                        }
                    }
                }
            }
            if (b.overrun) {
                d->truncated = 1;
            }
        }
    }
    d->p = b.p;
    return NULL;
}

#if NJ_ENABLE_JPEG_PROGRESSIVE
/* ------------------------------------------------------------------ */
/* Progressive (SOF2) scans                                            */
/* ------------------------------------------------------------------ */

/*
 * Progressive files describe the same blocks in several scans.  Each scan
 * either sends the DC coefficient (successive approximation on the DC value)
 * or a band of AC coefficients (first pass sends them at a reduced precision,
 * later passes send one refinement bit per coefficient already present).  The
 * coefficients therefore have to survive between scans: they are collected in
 * one grid per component and the transform runs once at the end, through the
 * same jp_idct_store() the sequential path uses.
 */
static const char *jp_alloc_coefs(jp_dec *d)
{
    nj_u8 total = 0;
    int i;

    for (i = 0; i < d->ncomp; i++) {
        jp_comp *c = &d->comp[i];
        nj_u8 blocks = (nj_u8)c->cw * (nj_u8)c->ch;

        if (blocks == 0u) {
            return "progressive JPEG has no blocks";
        }
        total += blocks * 128u;      /* 64 coefficients, 2 bytes each */
    }
    if (total > (nj_u8)NJ_JPEG_PROGRESSIVE_MAX_BYTES) {
        return "progressive JPEG needs more coefficient memory than this "
               "decoder allows";
    }
    for (i = 0; i < d->ncomp; i++) {
        jp_comp *c = &d->comp[i];
        nj_u8 bytes = (nj_u8)c->cw * (nj_u8)c->ch * 128u;

        c->coef = (nj_s2 *)d->alloc(d->ud, (nj_size)bytes);
        if (c->coef == NULL) {
            return "out of memory for the progressive JPEG coefficients";
        }
        memset(c->coef, 0, (size_t)bytes);
    }
    d->coef_ready = 1;
    return NULL;
}

/* One DC coefficient: first pass sends a difference, refinements send a bit. */
static int jp_prog_dc_block(jp_dec *d, jp_bits *b, jp_comp *c, nj_s2 *cf)
{
    if (d->ah == 0) {
        int t;
        nj_s4 v;

        if (jp_huff_decode(b, &d->tabs->dc[c->td], &t) != 0) {
            return b->overrun ? 1 : -1;
        }
        if (t > 15) {
            return -1;
        }
        c->pred = jp_clamp(c->pred + jp_receive_extend(b, t), -32767, 32767);
        v = (nj_s4)c->pred << d->al;
        cf[0] = (nj_s2)jp_clamp((int)v, -32767, 32767);
    } else {
        /*
         * The DC point transform is an arithmetic shift (T.81 G.1.2.1:
         * "the DC coefficient is processed by a point transform with Al"
         * applied to the difference coding, which libjpeg - and therefore
         * every encoder - implements as coef >> Al).  The low bits kept
         * back by that shift are the low bits of the two's complement value,
         * so a set refinement bit *adds* 2^Al for positive and negative
         * coefficients alike.  AC coefficients are different: the Huffman
         * code carries a sign and a magnitude, so their refinement is
         * sign-magnitude (see jp_prog_ac_block()).
         */
        if (jp_get_bits(b, 1) != 0u) {
            nj_s4 v = (nj_s4)cf[0] + ((nj_s4)1 << d->al);

            cf[0] = (nj_s2)jp_clamp((int)v, -32767, 32767);
        }
        if (b->overrun) {
            return 1;
        }
    }
    return 0;
}

/*
 * One AC band.  The first pass sends runs of zeros (RS = run/size) and
 * end-of-band runs; refinements send one bit per coefficient that is already
 * non-zero and can introduce a new coefficient at 1 << al.  Magnitudes are
 * refined symmetrically (T.81 G.1.2.3): a positive coefficient gains +p1, a
 * negative one loses p1.
 */
static int jp_prog_ac_block(jp_dec *d, jp_bits *b, jp_comp *c, nj_s2 *cf)
{
    const jp_huff *ac = &d->tabs->ac[c->ta];
    const nj_s4 p1 = (nj_s4)1 << d->al;
    int k = d->ss;

    if (d->ah == 0) {
        if (d->eobrun > 0) {
            d->eobrun -= 1;
            return 0;
        }
        while (k <= d->se) {
            int s, r;

            if (jp_huff_decode(b, ac, &s) != 0) {
                return b->overrun ? 1 : -1;
            }
            r = s >> 4;
            s &= 15;
            if (s == 0) {
                if (r != 15) {
                    d->eobrun = (nj_s4)1 << r;
                    if (r != 0) {
                        d->eobrun += (nj_s4)jp_get_bits(b, r);
                    }
                    d->eobrun -= 1;
                    break;
                }
                k += 16;
                if (k > 64) {
                    return -1;
                }
                continue;
            }
            k += r;
            if (k > d->se) {
                return -1;
            }
            {
                nj_s4 v = (nj_s4)jp_receive_extend(b, s) << d->al;

                cf[(int)s_zigzag[k]] = (nj_s2)jp_clamp((int)v, -32767, 32767);
            }
            k++;
        }
        return b->overrun ? 1 : 0;
    }

    /* Refinement.  A pending end-of-band run still refines the remaining
     * coefficients in this band. */
    if (d->eobrun > 0) {
        for (; k <= d->se; k++) {
            nj_s2 *q = &cf[(int)s_zigzag[k]];

            if (*q != 0 && jp_get_bits(b, 1) != 0u) {
                nj_s4 v = (nj_s4)*q + ((*q > 0) ? p1 : -p1);

                *q = (nj_s2)jp_clamp((int)v, -32767, 32767);
            }
        }
        if (b->overrun) {
            return 1;
        }
        d->eobrun -= 1;
        return 0;
    }
    while (k <= d->se) {
        int s, r, run;
        nj_s4 newval = 0;

        if (jp_huff_decode(b, ac, &s) != 0) {
            return b->overrun ? 1 : -1;
        }
        r = s >> 4;
        s &= 15;
        if (s == 0 && r != 15) {
            d->eobrun = (nj_s4)1 << r;
            if (r != 0) {
                d->eobrun += (nj_s4)jp_get_bits(b, r);
            }
            d->eobrun -= 1;
            for (; k <= d->se; k++) {
                nj_s2 *q = &cf[(int)s_zigzag[k]];

                if (*q != 0 && jp_get_bits(b, 1) != 0u) {
                    nj_s4 v = (nj_s4)*q + ((*q > 0) ? p1 : -p1);

                    *q = (nj_s2)jp_clamp((int)v, -32767, 32767);
                }
            }
            return b->overrun ? 1 : 0;
        }
        if (s != 0 && s != 1) {
            return -1;               /* a refinement can only add magnitude 1 */
        }
        /*
         * T.81 G.1.2.3: when a refinement symbol introduces a new non-zero
         * coefficient its *sign* bit comes immediately after the Huffman
         * code, before the correction bits of the coefficients the run
         * walks past.  Reading it after the walk consumes one bit too late
         * and desynchronises the rest of the block.
         */
        if (s != 0 && jp_get_bits(b, 1) == 0u) {
            newval = -p1;
        } else if (s != 0) {
            newval = p1;
        }
        /*
         * T.81 G.1.2.3 walk.  `r` counts the *zero* coefficients this symbol
         * still has to pass, so the walk stops at the (r+1)-th zero: that is
         * the newly non-zero coefficient for s != 0, and the 16th zero of a
         * ZRL (r == 15) for s == 0.  Corrections apply to already-non-zero
         * coefficients met on the way; a ZRL that reaches its 16th zero next
         * to a non-zero coefficient must stop *there*, because that
         * coefficient's correction bit belongs to the next symbol's walk (it
         * is read after the next Huffman code), not to this one.
         */
        run = r;
        while (k <= d->se) {
            nj_s2 *q = &cf[(int)s_zigzag[k]];

            if (*q != 0) {
                if (jp_get_bits(b, 1) != 0u) {
                    nj_s4 v = (nj_s4)*q + ((*q > 0) ? p1 : -p1);

                    *q = (nj_s2)jp_clamp((int)v, -32767, 32767);
                }
            } else if (run == 0) {
                break;                /* target zero of this symbol */
            } else {
                run -= 1;
            }
            k++;
        }
        if (k > d->se) {
            return -1;
        }
        if (s == 0) {
            k += 1;                   /* ZRL ends on its 16th zero */
            continue;
        }
        cf[(int)s_zigzag[k]] = (nj_s2)jp_clamp((int)newval, -32767, 32767);
        k++;
        if (b->overrun) {
            return 1;
        }
    }
    return 0;
}

static int jp_prog_block(jp_dec *d, jp_bits *b, jp_comp *c, int cx, int cy)
{
    nj_s2 *cf = c->coef +
        ((nj_size)cy * (nj_size)c->cw + (nj_size)cx) * 64;
    int r;

    if (d->ss == 0) {
        r = jp_prog_dc_block(d, b, c, cf);
    } else {
        r = jp_prog_ac_block(d, b, c, cf);
    }
    return r;
}

static const char *jp_decode_scan_prog(jp_dec *d)
{
    jp_bits b;
    nj_u4 mx, my, mcu = 0u;
    nj_u4 cols, rows, total;
    int expected_rst = 0xD0;

    d->truncated = 0;
    d->eobrun = 0;
    if (!d->coef_ready) {
        const char *e = jp_alloc_coefs(d);

        if (e != NULL) {
            return e;
        }
    }
    if (d->nscan == 1) {
        /* A single-component scan is a plain block raster (T.81 A.2.3). */
        cols = (nj_u4)d->scan[0]->bw;
        rows = (nj_u4)d->scan[0]->bh;
    } else {
        cols = d->mcus_x;
        rows = d->mcus_y;
    }
    total = cols * rows;
    if (total == 0u) {
        return "JPEG scan has no blocks";
    }

    jp_bits_init(&b, d->p, d->end);
    for (my = 0u; my < rows && !d->truncated; my++) {
        for (mx = 0u; mx < cols && !d->truncated; mx++, mcu++) {
            int i;

            if (d->restart_interval > 0 && mcu > 0u &&
                (mcu % (nj_u4)d->restart_interval) == 0u) {
                int r = jp_restart(&b, &expected_rst);

                if (r == -2) {
                    return "JPEG restart marker is out of sequence";
                }
                if (r != 0) {
                    return "JPEG restart marker is missing";
                }
                for (i = 0; i < d->ncomp; i++) {
                    d->comp[i].pred = 0;
                }
                d->eobrun = 0;
            }
            for (i = 0; i < d->nscan && !d->truncated; i++) {
                jp_comp *c = d->scan[i];
                int bh = (d->nscan == 1) ? 1 : c->h;
                int bv = (d->nscan == 1) ? 1 : c->v;
                int bxx, byy;

                for (byy = 0; byy < bv && !d->truncated; byy++) {
                    for (bxx = 0; bxx < bh && !d->truncated; bxx++) {
                        int cx, cy, r;

                        if (d->nscan == 1) {
                            cx = (int)mx;
                            cy = (int)my;
                        } else {
                            cx = (int)mx * c->h + bxx;
                            cy = (int)my * c->v + byy;
                        }
                        r = jp_prog_block(d, &b, c, cx, cy);
                        if (r < 0) {
                            return "corrupt progressive JPEG scan data";
                        }
                        if (r > 0) {
                            d->truncated = 1;
                        }
                    }
                }
            }
        }
    }
    d->p = b.p;
    return NULL;
}

/*
 * Turn the coefficient grids into the sample planes the sequential path also
 * produces, then everything downstream (upsampling, colour conversion) is the
 * same code.  jp_idct_store() keeps the encoded scale, so a progressive file
 * requested at half or quarter size lands in the normal downscaled planes.
 */
static const char *jp_prog_reconstruct(jp_dec *d)
{
    const char *e = jp_alloc_planes(d);
    int i;

    if (e != NULL) {
        return e;
    }
    for (i = 0; i < d->ncomp; i++) {
        jp_comp *c = &d->comp[i];
        int bx, by;

        if (c->coef == NULL) {
            continue;
        }
        for (by = 0; by < c->bh; by++) {
            for (bx = 0; bx < c->bw; bx++) {
                const nj_s2 *src =
                    c->coef + ((nj_size)by * (nj_size)c->cw + (nj_size)bx) * 64;
                nj_s4 coef[64];
                int k;

                for (k = 0; k < 64; k++) {
                    coef[k] = (nj_s4)src[k];
                }
                jp_idct_store(d, coef, c, bx, by);
            }
        }
    }
    return NULL;
}
#endif /* NJ_ENABLE_JPEG_PROGRESSIVE */

/* ------------------------------------------------------------------ */
/* Colour conversion and output                                       */
/* ------------------------------------------------------------------ */

/*
 * YCbCr -> RGB with the JFIF (T.81 Table J.1) matrix in 16.16 fixed point.
 * Divisions rather than shifts: see jp_descale().
 */
static void jp_ycc_to_rgb(int y, int cb, int cr, int *r, int *g, int *b)
{
    int x = cb - 128;
    int z = cr - 128;

    *r = jp_clamp(y + (91881 * z) / 65536, 0, 255);
    *g = jp_clamp(y - (22554 * x + 46802 * z) / 65536, 0, 255);
    *b = jp_clamp(y + (116130 * x) / 65536, 0, 255);
}

/*
 * Upsampling is replication: output pixel (x, y) reads sample
 * (x * h / h_max, y * v / v_max) of the component plane.  Since the plane is
 * stored at the same scale as the output, that index is independent of the
 * scale factor.
 */
static void jp_emit(jp_dec *d, nj_image *img)
{
    const int ow = img->width;
    const int oh = img->height;
    const jp_comp *c0 = &d->comp[0];
    int x, y;

    if (d->ncomp == 1) {
        for (y = 0; y < oh; y++) {
            const nj_u1 *row = c0->plane +
                (nj_size)((y * c0->v) / d->vmax) * (nj_size)c0->pw;
            nj_u4 *dst = img->pixels + (nj_size)y * (nj_size)ow;

            for (x = 0; x < ow; x++) {
                nj_u4 v = (nj_u4)row[(x * c0->h) / d->hmax];

                dst[x] = 0xFF000000u | (v << 16) | (v << 8) | v;
            }
        }
        return;
    }

    {
        const jp_comp *c1 = &d->comp[1];
        const jp_comp *c2 = &d->comp[2];
        const jp_comp *c3 = (d->ncomp == 4) ? &d->comp[3] : NULL;
        int direct = 0;

        if (d->ncomp == 3) {
            /* Adobe transform 0 means "no colour transform".  Without an
             * APP14 marker the component IDs R,G,B are the other hint real
             * files carry - but an APP0/JFIF marker wins, because JFIF
             * mandates YCbCr whatever the IDs happen to be. */
            if (d->adobe == 0) {
                direct = 1;
            } else if (d->adobe < 0 && !d->jfif && c0->id == 0x52 &&
                       c1->id == 0x47 && c2->id == 0x42) {
                direct = 1;
            }
        }
        for (y = 0; y < oh; y++) {
            const nj_u1 *r0 = c0->plane + (nj_size)((y * c0->v) / d->vmax) * (nj_size)c0->pw;
            const nj_u1 *r1 = c1->plane + (nj_size)((y * c1->v) / d->vmax) * (nj_size)c1->pw;
            const nj_u1 *r2 = c2->plane + (nj_size)((y * c2->v) / d->vmax) * (nj_size)c2->pw;
            const nj_u1 *r3 = NULL;
            nj_u4 *dst = img->pixels + (nj_size)y * (nj_size)ow;

            if (c3 != NULL) {
                r3 = c3->plane + (nj_size)((y * c3->v) / d->vmax) * (nj_size)c3->pw;
            }
            for (x = 0; x < ow; x++) {
                int a = (int)r0[(x * c0->h) / d->hmax];
                int b = (int)r1[(x * c1->h) / d->hmax];
                int c = (int)r2[(x * c2->h) / d->hmax];
                int r, g, bl;

                if (c3 != NULL) {
                    /* Adobe CMYK/YCCK: the stored channels are inverted, and
                     * YCCK has the first three transformed like YCbCr. */
                    int k = (int)r3[(x * c3->h) / d->hmax];

                    if (d->adobe == 2) {
                        int rr, gg, bb;

                        jp_ycc_to_rgb(a, b, c, &rr, &gg, &bb);
                        a = rr;
                        b = gg;
                        c = bb;
                    }
                    a = 255 - a;                 /* C */
                    b = 255 - b;                 /* M */
                    c = 255 - c;                 /* Y */
                    k = 255 - k;                 /* K */
                    r = 255 - jp_clamp(a + k, 0, 255);
                    g = 255 - jp_clamp(b + k, 0, 255);
                    bl = 255 - jp_clamp(c + k, 0, 255);
                } else if (direct != 0) {
                    r = a;
                    g = b;
                    bl = c;
                } else {
                    jp_ycc_to_rgb(a, b, c, &r, &g, &bl);
                }
                dst[x] = 0xFF000000u | ((nj_u4)r << 16) |
                         ((nj_u4)g << 8) | (nj_u4)bl;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int nj_jpeg_accepts(const void *data, nj_u4 size)
{
    const nj_u1 *p = (const nj_u1 *)data;
    int marker;

    /* SOI, then a marker that can legitimately follow it: a table, a frame
     * header, a scan, a restart interval or an application/comment segment.
     * A bare FF D8 followed by anything else is not worth claiming. */
    if (p == NULL || size < 4u) {
        return 0;
    }
    if (p[0] != 0xFFu || p[1] != 0xD8u || p[2] != 0xFFu) {
        return 0;
    }
    marker = (int)p[3];
    if (marker == 0xFF) {
        return 1;                 /* fill byte; the marker follows */
    }
    if (marker >= 0xC0 && marker <= 0xCF) {
        return 1;                 /* SOFn, DHT, JPG, DAC, DRI */
    }
    if (marker >= 0xDA && marker <= 0xDF) {
        return 1;                 /* SOS, DQT, DNL, DRI, DHP, EXP */
    }
    if (marker >= 0xE0 && marker <= 0xEF) {
        return 1;                 /* APPn */
    }
    return (marker == 0xFE) ? 1 : 0;   /* COM */
}

nj_status nj_jpeg_info(const void *data, nj_u4 size, nj_image_info *out)
{
    const nj_u1 *p = (const nj_u1 *)data;
    const nj_u1 *end;

    if (p == NULL || out == NULL || size < 4u) {
        return NJ_ERR_FORMAT;
    }
    if (p[0] != 0xFFu || p[1] != 0xD8u) {
        return NJ_ERR_FORMAT;
    }
    end = p + size;
    p += 2;

    for (;;) {
        int marker, len;

        while (p < end && *p == 0xFFu) {
            p++;
        }
        if (p >= end) {
            return NJ_ERR_FORMAT;
        }
        marker = (int)*p++;
        if (marker == 0xD9 || marker == 0x00) {
            return NJ_ERR_FORMAT;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (end - p < 2) {
            return NJ_ERR_FORMAT;
        }
        len = ((int)p[0] << 8) | (int)p[1];
        if (len < 2 || (end - p) < len) {
            return NJ_ERR_FORMAT;
        }
        /* SOFn are C0..CF except DHT (C4), JPG (C8) and DAC (CC). */
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
            marker != 0xC8 && marker != 0xCC) {
            const nj_u1 *s = p + 2;

            if (len - 2 < 6) {
                return NJ_ERR_FORMAT;
            }
            {
                int w = ((int)s[3] << 8) | (int)s[4];
                int h = ((int)s[1] << 8) | (int)s[2];
                int nf = (int)s[5];

                if (w <= 0 || h <= 0 || w > NJ_JPEG_MAX_DIM ||
                    h > NJ_JPEG_MAX_DIM) {
                    return NJ_ERR_FORMAT;
                }
                if (nf < 1 || nf > NJ_JPEG_MAX_COMPS ||
                    len - 2 < 6 + 3 * nf) {
                    return NJ_ERR_FORMAT;
                }
                out->width = w;
                out->height = h;
                out->has_alpha = 0;
                out->bits_per_pixel = nf * (int)s[0];
                out->format = NJ_CODEC_FMT_JPEG;
                return NJ_OK;
            }
        }
        p += len;
    }
}

nj_image *nj_jpeg_decode(const nj_codec_req *req)
{
    jp_dec d;
    nj_image *img = NULL;
    const char *err = NULL;
    int scans = 0;

    if (req == NULL) {
        return NULL;
    }
    if (req->err != NULL) {
        *(req->err) = NULL;
    }
    if (req->data == NULL || req->size < 4u) {
        jp_fail(req, "truncated image data");
        return NULL;
    }
    if (req->alloc == NULL || req->dealloc == NULL) {
        jp_fail(req, "the JPEG decoder needs an allocator");
        return NULL;
    }
    /* This provider advertises NJ_CODEC_CAN_ARGB8888 and nothing else.  A
     * zero-initialised request asked for "any layout" before out_format was
     * spelled as an NJ_PIXFMT_*, so 0 is honoured as NJ_CODEC_OUT_ANY. */
    if (req->out_format != NJ_CODEC_OUT_ANY &&
        req->out_format != (nj_u4)NJ_PIXFMT_ARGB8888 &&
        req->out_format != 0u) {
        jp_fail(req, "this JPEG decoder only produces ARGB8888");
        return NULL;
    }

    memset(&d, 0, sizeof(d));
    d.req = req;
    d.p = (const nj_u1 *)req->data;
    d.end = d.p + req->size;
    d.alloc = req->alloc;
    d.dealloc = req->dealloc;
    d.ud = req->ud;
    d.want_w = req->want_width;
    d.want_h = req->want_height;
    d.adobe = -1;
    d.scale = 1;
    d.nkeep = 8;

    d.tabs = (jp_tables *)req->alloc(req->ud, (nj_size)sizeof(jp_tables));
    if (d.tabs == NULL) {
        jp_fail(req, "out of memory for the JPEG tables");
        return NULL;
    }
    memset(d.tabs, 0, sizeof(jp_tables));

    for (;;) {
        err = jp_next_scan(&d);
        if (err != NULL || d.done) {
            break;
        }
        err = jp_decode_scan(&d);
        if (err != NULL) {
            break;
        }
        if (++scans > NJ_JPEG_MAX_SCANS) {
            err = "too many JPEG scans";
            break;
        }
    }
#if NJ_ENABLE_JPEG_PROGRESSIVE
    if (err == NULL && d.progressive && !d.planes_ready) {
        err = jp_prog_reconstruct(&d);
    }
#endif
    if (err == NULL) {
        int i;

        if (!d.seen_sof) {
            err = "JPEG has no frame header";
        } else {
            for (i = 0; i < d.ncomp; i++) {
                if (!d.comp[i].scanned) {
                    err = "not all JPEG components were decoded";
                    break;
                }
            }
        }
    }
    if (err == NULL) {
        img = jp_image_new(&d, d.out_w, d.out_h);
        if (img == NULL) {
            err = "out of memory for the JPEG image";
        } else {
            jp_emit(&d, img);
        }
    }
    jp_free(&d);
    if (err != NULL) {
        jp_image_free(&d, img);
        jp_fail(req, err);
        return NULL;
    }
    return img;
}

#else  /* !NJ_ENABLE_JPEG */

/*
 * The decoder is compiled out.  These stubs keep the file a valid translation
 * unit (ISO C forbids an empty one) and let a build that wires the functions
 * up by hand fail predictably; nj_codec.c installs nj_codec_jpeg_unavailable
 * instead of calling them.
 */
int nj_jpeg_accepts(const void *data, nj_u4 size)
{
    (void)data;
    (void)size;
    return 0;
}

nj_status nj_jpeg_info(const void *data, nj_u4 size, nj_image_info *out)
{
    (void)data;
    (void)size;
    (void)out;
    return NJ_ERR_UNSUPPORTED;
}

nj_image *nj_jpeg_decode(const nj_codec_req *req)
{
    if (req != NULL && req->err != NULL) {
        *(req->err) = "this build has no JPEG decoder";
    }
    return NULL;
}

#endif /* NJ_ENABLE_JPEG */
