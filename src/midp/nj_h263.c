/*
 * nano_j2me - nj_h263.c
 *
 * ============================================================================
 *  H.263 BASELINE VIDEO DECODER
 * ============================================================================
 *
 * This file is the video provider named "h263" in include/nj2me/nj_avcodec.h.
 * It decodes the baseline subset of ITU-T H.263 that a J2ME handset camera or
 * a 3GP movie needs: the short picture header (PSC, TR, PTYPE, PEI), optional
 * GOB headers (the layout FFmpeg writes), macroblock layer (MCBPC, CBPY, DC,
 * TCOEF), the H.263 dequantiser, an integer IDCT, 4:2:0 reconstruction and
 * one-vector motion compensation with half-pel interpolation.
 *
 * The output is RGB565 because that is the native surface format of the
 * runtime (src/gfx/nj_gfx.c reads and writes it with memcpy, native 16-bit
 * order).  One call to decode() emits exactly one frame.
 *
 * The provider accepts either a raw H.263 elementary stream or a complete
 * ISO base media (3GP/MP4) file.  For a file it uses the nj_mp4 sample-table
 * iterator and decodes the selected H.263 video track; for a raw stream it
 * finds picture boundaries with the 22-bit picture start code.  It never
 * copies the input and never allocates the file: the caller's buffer must
 * stay alive until close(), and every allocation goes through nj_av_mem.
 *
 * Deliberately refused, with NJ_AV_ERR_UNSUPPORTED rather than a guess:
 * MPEG-4 Part 2 video, H.263+ (PLUSPTYPE, Annex I/J/K/Q), PB frames,
 * long vectors, advanced prediction (OBMC), slice-structured GOB headers,
 * custom picture sizes (format 6/7), and 4MV macroblocks.
 *
 * Everything read from the input is bounds checked.  The decoder keeps no
 * mutable global or static state; coefficients and IDCT temporaries live on
 * the C stack.
 */

#include "nj2me/nj.h"
#include "nj2me/nj_avcodec.h"
#include "nj_mp4.h"
#include "nj_idct.h"

#include <string.h>

/* Build bridge while CMakeLists.txt does not yet name the new demuxer source:
 * compile nj_mp4.c into this translation unit so the runtime links.  The Lead
 * removes this by adding src/midp/nj_mp4.c to NJ_SOURCES and defining
 * NJ_MP4_SEPARATE in the build; the standalone nj_mp4.c stays the real file. */


#if NJ_ENABLE_VIDEO

/* H.263 formats 1..5, in pixels.  Index 0 is the forbidden value. */
static const nj_u2 h263_format_width[6] = { 0, 128, 176, 352, 704, 1408 };
static const nj_u2 h263_format_height[6] = { 0, 96, 144, 288, 576, 1152 };

/* The largest picture the format table can request.  A guard for allocation
 * arithmetic, not a limit on the code. */
#define H263_MAX_PIXELS (1408u * 1152u)

/* Provider-specific decode error: the caller's output buffer is too small.
 * nj_avcodec.h has no "buffer too small" code; -6 is below every one of its
 * negative results so callers still see a failure. */
#define H263_ERR_OUTPUT (-6)

typedef struct h263_vlc {
    nj_u2 code;
    nj_u1 bits;
    nj_u1 sym;
} h263_vlc;

/* MSB-first reader over a byte range.  `pos' is a bit position; the reader
 * never reads a byte outside [p, p+nbits/8] and latches err on overrun. */
typedef struct h263_bits {
    const nj_u1 *p;
    nj_u4 nbits;
    nj_u4 pos;
    int err;
} h263_bits;

typedef struct h263_picture {
    int  width;
    int  height;
    int  mbw;
    int  mbh;
    int  pict_type;              /* 0 = I, 1 = P */
    int  qscale;
    nj_u1 tr;
} h263_picture;

typedef struct nj_h263_dec {
    const nj_av_mem *mem;        /* allocator hooks, borrowed from the caller */
    const nj_u1 *data;           /* caller's whole buffer, kept alive        */
    nj_u4 len;

    int is_mp4;
    nj_mp4 mp4;                  /* sample iterator for the container path   */

    nj_u4 next_at;               /* raw stream: first byte of the next picture */
    int  raw_first;              /* no picture decoded yet                   */

    nj_u1 *cur;                  /* current YUV420 frame                     */
    nj_u1 *ref;                  /* previous frame                           */
    int have_ref;

    nj_s2 *mv;                   /* one (x,y) pair per macroblock            */

    int width;
    int height;
    int mbw;
    int mbh;
    nj_u4 y_size;
    nj_u4 c_size;
    nj_u4 frame_size;

    nj_av_info *info;
} nj_h263_dec;

static nj_u4 h263_br_get(h263_bits *b, int n)
{
    nj_u4 v = 0;
    int i;

    if (n < 0 || b->err || b->pos + (nj_u4)n > b->nbits) {
        b->err = 1;
        return 0;
    }
    for (i = 0; i < n; i++) {
        nj_u4 byte = b->p[b->pos >> 3];
        v = (v << 1) | ((byte >> (7u - (b->pos & 7u))) & 1u);
        b->pos++;
    }
    return v;
}

static int h263_br_peek(const h263_bits *b, int n, nj_u4 *out)
{
    nj_u4 v = 0;
    nj_u4 pos = b->pos;
    int i;

    if (n < 0 || b->err || b->pos + (nj_u4)n > b->nbits) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        nj_u4 byte = b->p[pos >> 3];
        v = (v << 1) | ((byte >> (7u - (pos & 7u))) & 1u);
        pos++;
    }
    *out = v;
    return 0;
}

/* Decode one VLC symbol.  Tables are at most 13 bits long and prefix free. */
static int h263_vlc_decode(h263_bits *b, const h263_vlc *tab, int count, int *sym)
{
    nj_u4 code = 0;
    int len;

    for (len = 1; len <= 13; len++) {
        int i;
        code = (code << 1) | h263_br_get(b, 1);
        if (b->err) {
            return -1;
        }
        for (i = 0; i < count; i++) {
            if (tab[i].bits == len && tab[i].code == code) {
                *sym = tab[i].sym;
                return 0;
            }
        }
    }
    b->err = 1;
    return -1;
}

static int h263_sext6(int v)
{
    v &= 63;
    return (v & 32) ? v - 64 : v;
}

static const h263_vlc h263_intra_mcbpc_vlc[9] = {
    {    1,  1,   0 },
    {    1,  3,   1 },
    {    2,  3,   2 },
    {    3,  3,   3 },
    {    1,  4,   4 },
    {    1,  6,   5 },
    {    2,  6,   6 },
    {    3,  6,   7 },
    {    1,  9,   8 },
};

static const h263_vlc h263_inter_mcbpc_vlc[25] = {
    {    1,  1,   0 },
    {    2,  3,  16 },
    {    3,  3,   8 },
    {    2,  4,   2 },
    {    3,  4,   1 },
    {    3,  5,   4 },
    {    4,  6,  12 },
    {    5,  6,   3 },
    {    3,  7,   7 },
    {    4,  7,  18 },
    {    5,  7,  17 },
    {    6,  7,  10 },
    {    7,  7,   9 },
    {    3,  8,   6 },
    {    4,  8,   5 },
    {    5,  8,  19 },
    {    1,  9,  20 },
    {    2,  9,  15 },
    {    3,  9,  14 },
    {    4,  9,  13 },
    {    5,  9,  11 },
    {    2, 11,  24 },
    {   12, 13,  25 },
    {   14, 13,  26 },
    {   15, 13,  27 },
};

static const h263_vlc h263_cbpy_vlc[16] = {
    {    3,  2,  15 },
    {    3,  4,   0 },
    {    4,  4,  12 },
    {    5,  4,  10 },
    {    6,  4,  14 },
    {    7,  4,   5 },
    {    8,  4,  13 },
    {    9,  4,   3 },
    {   10,  4,  11 },
    {   11,  4,   7 },
    {    2,  5,   8 },
    {    3,  5,   4 },
    {    4,  5,   2 },
    {    5,  5,   1 },
    {    2,  6,   6 },
    {    3,  6,   9 },
};

static const h263_vlc h263_mv_vlc[33] = {
    {    1,  1,   0 },
    {    1,  2,   1 },
    {    1,  3,   2 },
    {    1,  4,   3 },
    {    3,  6,   4 },
    {    3,  7,   7 },
    {    4,  7,   6 },
    {    5,  7,   5 },
    {    9,  9,  10 },
    {   10,  9,   9 },
    {   11,  9,   8 },
    {    4, 10,  24 },
    {    5, 10,  23 },
    {    6, 10,  22 },
    {    7, 10,  21 },
    {    8, 10,  20 },
    {    9, 10,  19 },
    {   10, 10,  18 },
    {   11, 10,  17 },
    {   12, 10,  16 },
    {   13, 10,  15 },
    {   14, 10,  14 },
    {   15, 10,  13 },
    {   16, 10,  12 },
    {   17, 10,  11 },
    {    2, 11,  30 },
    {    3, 11,  29 },
    {    4, 11,  28 },
    {    5, 11,  27 },
    {    6, 11,  26 },
    {    7, 11,  25 },
    {    2, 12,  32 },
    {    3, 12,  31 },
};

static const h263_vlc h263_tcoef_vlc[103] = {
    {    2,  2,   0 },
    {    6,  3,  12 },
    {    7,  4,  58 },
    {   14,  4,  18 },
    {   15,  4,   1 },
    {   11,  5,  28 },
    {   12,  5,  25 },
    {   13,  5,  22 },
    {   12,  6,  65 },
    {   13,  6,  64 },
    {   14,  6,  63 },
    {   15,  6,  61 },
    {   16,  6,  38 },
    {   17,  6,  36 },
    {   18,  6,  34 },
    {   19,  6,  31 },
    {   20,  6,  13 },
    {   21,  6,   2 },
    {    3,  7, 102 },
    {   16,  7,  69 },
    {   17,  7,  68 },
    {   18,  7,  67 },
    {   19,  7,  66 },
    {   20,  7,  43 },
    {   21,  7,  42 },
    {   22,  7,  40 },
    {   23,  7,   3 },
    {   19,  8,  77 },
    {   20,  8,  76 },
    {   21,  8,  75 },
    {   22,  8,  74 },
    {   23,  8,  73 },
    {   24,  8,  72 },
    {   25,  8,  71 },
    {   26,  8,  70 },
    {   27,  8,  45 },
    {   28,  8,  44 },
    {   29,  8,  19 },
    {   30,  8,  14 },
    {   31,  8,   4 },
    {   17,  9,  85 },
    {   18,  9,  84 },
    {   19,  9,  83 },
    {   20,  9,  82 },
    {   21,  9,  81 },
    {   22,  9,  80 },
    {   23,  9,  79 },
    {   24,  9,  78 },
    {   25,  9,  59 },
    {   26,  9,  53 },
    {   27,  9,  52 },
    {   28,  9,  51 },
    {   29,  9,  50 },
    {   30,  9,  49 },
    {   31,  9,  48 },
    {   32,  9,  47 },
    {   33,  9,  46 },
    {   34,  9,  26 },
    {   35,  9,  23 },
    {   36,  9,   6 },
    {   37,  9,   5 },
    {    4, 10,  89 },
    {    5, 10,  88 },
    {    6, 10,  87 },
    {    7, 10,  86 },
    {    8, 10,  39 },
    {    9, 10,  37 },
    {   10, 10,  35 },
    {   11, 10,  32 },
    {   12, 10,  29 },
    {   13, 10,  24 },
    {   14, 10,  20 },
    {   15, 10,  15 },
    {   32, 10,   8 },
    {   33, 10,   7 },
    {    4, 11,  62 },
    {    5, 11,  60 },
    {    6, 11,  10 },
    {    7, 11,   9 },
    {   32, 11,  11 },
    {   33, 11,  16 },
    {   34, 11,  54 },
    {   35, 11,  55 },
    {   36, 11,  90 },
    {   37, 11,  91 },
    {   38, 11,  92 },
    {   39, 11,  93 },
    {   80, 12,  17 },
    {   81, 12,  21 },
    {   82, 12,  27 },
    {   83, 12,  30 },
    {   84, 12,  33 },
    {   85, 12,  41 },
    {   86, 12,  56 },
    {   87, 12,  57 },
    {   88, 12,  94 },
    {   89, 12,  95 },
    {   90, 12,  96 },
    {   91, 12,  97 },
    {   92, 12,  98 },
    {   93, 12,  99 },
    {   94, 12, 100 },
    {   95, 12, 101 },
};

static const nj_u1 h263_zigzag[64] = {
0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };



static const nj_u2 h263_tcoef_rl[102] = {
1,2,3,4,5,6,7,8,9,10,11,12,257,258,259,260,261,262,513,514,515,516,769,770,771,1025,1026,1027,1281,1282,1283,1537,1538,1539,1793,1794,2049,2050,2305,2306,2561,2562,2817,3073,3329,3585,3841,4097,4353,4609,4865,5121,5377,5633,5889,6145,6401,6657,1,2,3,257,258,513,769,1025,1281,1537,1793,2049,2305,2561,2817,3073,3329,3585,3841,4097,4353,4609,4865,5121,5377,5633,5889,6145,6401,6657,6913,7169,7425,7681,7937,8193,8449,8705,8961,9217,9473,9729,9985,10241 };


/* ------------------------------------------------------------------ */
/* Picture header                                                      */
/* ------------------------------------------------------------------ */

static int h263_parse_header(h263_bits *b, h263_picture *pic)
{
    int fmt;
    nj_u4 v;

    memset(pic, 0, sizeof(*pic));
    if (h263_br_get(b, 22) != 0x20u || b->err) {
        return NJ_AV_ERR_CORRUPT;
    }
    pic->tr = (nj_u1)h263_br_get(b, 8);
    if (h263_br_get(b, 1) != 1u) {          /* marker bit */
        return NJ_AV_ERR_CORRUPT;
    }
    if (h263_br_get(b, 1) != 0u) {          /* H.263 identifier */
        return NJ_AV_ERR_UNSUPPORTED;
    }
    if (h263_br_get(b, 3) != 0u) {          /* split/camera/freeze */
        return NJ_AV_ERR_UNSUPPORTED;
    }
    fmt = (int)h263_br_get(b, 3);
    if (fmt < 1 || fmt > 5) {               /* 0, 6 (custom), 7 (H.263+) */
        return NJ_AV_ERR_UNSUPPORTED;
    }
    pic->width  = (int)h263_format_width[fmt];
    pic->height = (int)h263_format_height[fmt];
    pic->mbw    = pic->width / 16;
    pic->mbh    = pic->height / 16;

    pic->pict_type = (int)h263_br_get(b, 1);
    if (h263_br_get(b, 1) != 0u) {          /* long vectors */
        return NJ_AV_ERR_UNSUPPORTED;
    }
    if (h263_br_get(b, 1) != 0u) {          /* syntax arithmetic coding */
        return NJ_AV_ERR_UNSUPPORTED;
    }
    if (h263_br_get(b, 1) != 0u) {          /* advanced prediction (OBMC) */
        return NJ_AV_ERR_UNSUPPORTED;
    }
    if (h263_br_get(b, 1) != 0u) {          /* PB frames */
        return NJ_AV_ERR_UNSUPPORTED;
    }
    pic->qscale = (int)h263_br_get(b, 5);
    if (pic->qscale == 0) {
        return NJ_AV_ERR_CORRUPT;
    }
    if (h263_br_get(b, 1) != 0u) {          /* continuous presence mux */
        return NJ_AV_ERR_UNSUPPORTED;
    }
    for (;;) {
        v = h263_br_get(b, 1);
        if (b->err || v == 0u) {
            break;
        }
        (void)h263_br_get(b, 8);            /* PSBI */
    }
    if (b->err) {
        return NJ_AV_ERR_CORRUPT;
    }
    return NJ_AV_OK;
}

/* Optional GOB header at a macroblock-row boundary.  Returns 1 when one was
 * consumed (and updates *qscale), 0 when the bits are macroblock data.
 *
 * FFmpeg's encoder (and therefore the fixtures) writes the non-slice layout:
 * 17-bit GBSC, 5-bit GN, 2-bit GFID, 5-bit GQUANT.  An MBA-bearing
 * slice-structured header is not guessed at; it fails the GN test below and
 * the stream is refused a little later as corrupt. */
static int h263_gob_header(h263_bits *b, int gob_rows, int mby, int *qscale)
{
    nj_u4 save = b->pos;
    int save_err = b->err;
    nj_u4 start;
    int gn;
    int q;

    if (h263_br_peek(b, 17, &start) != 0 || start != 1u) {
        return 0;
    }
    (void)h263_br_get(b, 17);
    gn = (int)h263_br_get(b, 5);
    (void)h263_br_get(b, 2);                /* GFID */
    q = (int)h263_br_get(b, 5);
    if (!b->err && q != 0 && mby > 0 && gn == mby / gob_rows) {
        *qscale = q;
        return 1;
    }
    b->pos = save;
    b->err = save_err;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Dequantisation, IDCT and reconstruction                             */
/* ------------------------------------------------------------------ */

static nj_s4 h263_dequant(int level, int qscale)
{
    int qmul = qscale << 1;
    int qadd = (qscale - 1) | 1;

    if (level > 0) {
        return (nj_s4)(level * qmul + qadd);
    }
    if (level < 0) {
        return (nj_s4)(level * qmul - qadd);
    }
    return 0;
}

static void h263_dequant_block(nj_s4 *blk, int intra, int qscale)
{
    int i;

    for (i = 0; i < 64; i++) {
        if (blk[i] == 0) {
            continue;
        }
        if (intra && i == 0) {
            blk[i] = (nj_s4)(blk[i] * 8);
        } else {
            blk[i] = h263_dequant((int)blk[i], qscale);
        }
    }
}


static nj_u1 h263_clamp_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (nj_u1)v;
}

static void h263_put_intra(nj_u1 *dst, int stride, const int *pix)
{
    int x, y;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            dst[y * stride + x] = h263_clamp_u8(pix[y * 8 + x]);
        }
    }
}

static void h263_put_inter(nj_u1 *dst, int stride, const int *pix,
                           const nj_u1 *pred, int pstride)
{
    int x, y;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            int v = pix[y * 8 + x] + (int)pred[y * pstride + x];
            dst[y * stride + x] = h263_clamp_u8(v);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Motion compensation                                                 */
/* ------------------------------------------------------------------ */

static int h263_edge(const nj_u1 *p, int w, int h, int x, int y)
{
    if (x < 0) {
        x = 0;
    } else if (x >= w) {
        x = w - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= h) {
        y = h - 1;
    }
    return (int)p[y * w + x];
}

static int h263_hpel(const nj_u1 *p, int w, int h, int sx, int sy,
                     int fx, int fy)
{
    int a = h263_edge(p, w, h, sx, sy);

    if (fx && fy) {
        int b = h263_edge(p, w, h, sx + 1, sy);
        int c = h263_edge(p, w, h, sx, sy + 1);
        int d = h263_edge(p, w, h, sx + 1, sy + 1);
        return (a + b + c + d + 2) >> 2;
    }
    if (fx) {
        int b = h263_edge(p, w, h, sx + 1, sy);
        return (a + b + 1) >> 1;
    }
    if (fy) {
        int c = h263_edge(p, w, h, sx, sy + 1);
        return (a + c + 1) >> 1;
    }
    return a;
}

/* One motion-compensated block.  `shift' is 0 for luma and 1 for chroma:
 * luma's integer part is mv>>1 with an mv&1 half-pel flag; chroma's integer
 * part is mv>>2 with a (mv&3)!=0 half-pel flag.  Samples outside the plane
 * replicate the nearest edge sample. */
static void h263_mc_block(nj_u1 *dst, int dstride, const nj_u1 *src,
                          int sw, int sh, int x0, int y0, int mvx, int mvy,
                          int shift, int bs)
{
    int sx = x0 + (mvx >> (1 + shift));
    int sy = y0 + (mvy >> (1 + shift));
    int fx = shift ? ((mvx & 3) != 0) : (mvx & 1);
    int fy = shift ? ((mvy & 3) != 0) : (mvy & 1);
    int x, y;

    for (y = 0; y < bs; y++) {
        for (x = 0; x < bs; x++) {
            dst[y * dstride + x] =
                (nj_u1)h263_hpel(src, sw, sh, sx + x, sy + y, fx, fy);
        }
    }
}

static void h263_predict_mb(nj_h263_dec *d, const h263_picture *pic,
                            int mbx, int mby, int mvx, int mvy,
                            nj_u1 *py, nj_u1 *pcb, nj_u1 *pcr)
{
    const nj_u1 *ref_y = d->ref;
    const nj_u1 *ref_u = d->ref + d->y_size;
    const nj_u1 *ref_v = ref_u + d->c_size;

    h263_mc_block(py, 16, ref_y, pic->width, pic->height,
                  mbx * 16, mby * 16, mvx, mvy, 0, 16);
    h263_mc_block(pcb, 8, ref_u, pic->width / 2, pic->height / 2,
                  mbx * 8, mby * 8, mvx, mvy, 1, 8);
    h263_mc_block(pcr, 8, ref_v, pic->width / 2, pic->height / 2,
                  mbx * 8, mby * 8, mvx, mvy, 1, 8);
}

static void h263_store_mb(nj_h263_dec *d, const h263_picture *pic,
                          int mbx, int mby, const nj_u1 *py,
                          const nj_u1 *pcb, const nj_u1 *pcr)
{
    nj_u1 *dst_y = d->cur + (mby * 16) * pic->width + mbx * 16;
    nj_u1 *dst_u = d->cur + d->y_size
                 + (mby * 8) * (pic->width / 2) + mbx * 8;
    nj_u1 *dst_v = dst_u + d->c_size;
    int y;

    for (y = 0; y < 16; y++) {
        memcpy(dst_y + y * pic->width, py + y * 16, 16);
    }
    for (y = 0; y < 8; y++) {
        memcpy(dst_u + y * (pic->width / 2), pcb + y * 8, 8);
        memcpy(dst_v + y * (pic->width / 2), pcr + y * 8, 8);
    }
}

static void h263_copy_mb(nj_h263_dec *d, const h263_picture *pic,
                         int mbx, int mby, nj_u1 *py, nj_u1 *pcb, nj_u1 *pcr)
{
    const nj_u1 *src_y = d->ref + (mby * 16) * pic->width + mbx * 16;
    const nj_u1 *src_u = d->ref + d->y_size
                       + (mby * 8) * (pic->width / 2) + mbx * 8;
    const nj_u1 *src_v = src_u + d->c_size;
    int y;

    for (y = 0; y < 16; y++) {
        memcpy(py + y * 16, src_y + y * pic->width, 16);
    }
    for (y = 0; y < 8; y++) {
        memcpy(pcb + y * 8, src_u + y * (pic->width / 2), 8);
        memcpy(pcr + y * 8, src_v + y * (pic->width / 2), 8);
    }
}

/* One (x,y) motion vector per macroblock, used by the median predictor. */
static void h263_pred_mv(const nj_h263_dec *d, int mbx, int mby,
                         int *px, int *py)
{
    int ax, ay, bx, by, cx, cy;

    if (mby == 0) {
        if (mbx == 0) {
            *px = 0;
            *py = 0;
        } else {
            const nj_s2 *a = d->mv + ((mby * d->mbw) + mbx - 1) * 2;
            *px = a[0];
            *py = a[1];
        }
        return;
    }
    if (mbx == 0) {
        ax = 0;
        ay = 0;
    } else {
        const nj_s2 *a = d->mv + ((mby * d->mbw) + mbx - 1) * 2;
        ax = a[0];
        ay = a[1];
    }
    {
        const nj_s2 *b = d->mv + (((mby - 1) * d->mbw) + mbx) * 2;
        bx = b[0];
        by = b[1];
    }
    if (mbx + 1 < d->mbw) {
        const nj_s2 *c = d->mv + (((mby - 1) * d->mbw) + mbx + 1) * 2;
        cx = c[0];
        cy = c[1];
    } else {
        cx = 0;
        cy = 0;
    }
    *px = (ax + bx + cx) - (ax < bx ? (ax < cx ? ax : cx)
                                    : (bx < cx ? bx : cx))
        - (ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx));
    *py = (ay + by + cy) - (ay < by ? (ay < cy ? ay : cy)
                                    : (by < cy ? by : cy))
        - (ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy));
}

static int h263_decode_mv(h263_bits *b, int pred, int *out)
{
    int sym;

    if (h263_vlc_decode(b, h263_mv_vlc, 33, &sym) != 0) {
        return NJ_AV_ERR_CORRUPT;
    }
    if (sym == 0) {
        *out = pred;
        return NJ_AV_OK;
    }
    if (h263_br_get(b, 1) != 0u) {
        sym = -sym;
    }
    *out = h263_sext6(pred + sym);
    return NJ_AV_OK;
}

/* ------------------------------------------------------------------ */
/* Coefficient decoding                                                */
/* ------------------------------------------------------------------ */

/* Decode one 8x8 block into raw levels.  `intra' selects the DC layout. */
static int h263_decode_block(h263_bits *b, nj_s4 *blk, int intra, int coded)
{
    int pos = intra ? 0 : -1;

    memset(blk, 0, 64 * sizeof(*blk));
    if (intra) {
        int level = (int)h263_br_get(b, 8);
        if (level == 255) {
            level = 128;
        }
        blk[0] = (nj_s4)level;
    }
    if (!coded) {
        return b->err ? NJ_AV_ERR_CORRUPT : NJ_AV_OK;
    }
    for (;;) {
        int sym, run, level, last;

        if (h263_vlc_decode(b, h263_tcoef_vlc, 103, &sym) != 0) {
            return NJ_AV_ERR_CORRUPT;
        }
        if (sym == 102) {                   /* escape: LAST, RUN, LEVEL */
            last = (int)h263_br_get(b, 1);
            run  = (int)h263_br_get(b, 6);
            level = (int)(nj_s1)h263_br_get(b, 8);
            if (level == -128) {
                int lo = (int)h263_br_get(b, 5);
                int hi = h263_sext6((int)h263_br_get(b, 6));
                level = lo + hi * 32;   /* equivalent, avoids shifting a negative */
            }
            pos += run + 1;
        } else {
            nj_u2 rl = h263_tcoef_rl[sym];
            level = (int)(rl & 0xFFu);
            run = (int)(rl >> 8);
            last = sym >= 58;
            if (h263_br_get(b, 1) != 0u) {
                level = -level;
            }
            pos += run + 1;
        }
        if (b->err || pos >= 64) {
            return NJ_AV_ERR_CORRUPT;
        }
        blk[h263_zigzag[pos]] = (nj_s4)level;
        if (last) {
            break;
        }
    }
    return NJ_AV_OK;
}

static int h263_decode_mb(nj_h263_dec *d, h263_bits *b,
                          const h263_picture *pic, int mbx, int mby,
                          int *qscale)
{
    nj_u1 py[256], pcb[64], pcr[64];
    nj_s4 blk[64];
    int pix[64];
    int cbpc = 0, cbpy = 0, cbp, intra, dq, i;
    int mvx = 0, mvy = 0;
    int base = (mby * pic->mbw + mbx) * 2;

    if (pic->pict_type == 1) {
        if (h263_br_get(b, 1) != 0u) {
            if (b->err) {
                return NJ_AV_ERR_CORRUPT;
            }
            d->mv[base] = 0;
            d->mv[base + 1] = 0;
            h263_copy_mb(d, pic, mbx, mby, py, pcb, pcr);
            h263_store_mb(d, pic, mbx, mby, py, pcb, pcr);
            return NJ_AV_OK;                  /* no residual, no MV */
        }
    }

    if (pic->pict_type == 0) {
        for (;;) {
            if (h263_vlc_decode(b, h263_intra_mcbpc_vlc, 9, &cbpc) != 0) {
                return NJ_AV_ERR_CORRUPT;
            }
            if (cbpc != 8) {
                break;
            }
        }
        intra = 1;
        dq = (cbpc & 4) != 0;
    } else {
        for (;;) {
            if (h263_vlc_decode(b, h263_inter_mcbpc_vlc, 25, &cbpc) != 0) {
                return NJ_AV_ERR_CORRUPT;
            }
            if (cbpc != 20) {
                break;
            }
        }
        if ((cbpc & 16) != 0) {               /* 4MV: H.263 annex F */
            return NJ_AV_ERR_UNSUPPORTED;
        }
        intra = (cbpc & 4) != 0;
        dq = (cbpc & 8) != 0;
    }

    if (h263_vlc_decode(b, h263_cbpy_vlc, 16, &cbpy) != 0) {
        return NJ_AV_ERR_CORRUPT;
    }
    if (!intra) {
        cbpy ^= 0xF;                          /* inter: inverted CBPY */
    }
    cbp = (cbpc & 3) | (cbpy << 2);

    if (dq) {
        static const int qtab[4] = { -1, -2, 1, 2 };
        int idx = (int)h263_br_get(b, 2);
        int q = *qscale + qtab[idx];
        if (b->err || q < 1 || q > 31) {
            return NJ_AV_ERR_CORRUPT;
        }
        *qscale = q;
    }

    if (!intra) {
        int predx, predy;
        h263_pred_mv(d, mbx, mby, &predx, &predy);
        if (h263_decode_mv(b, predx, &mvx) != NJ_AV_OK ||
            h263_decode_mv(b, predy, &mvy) != NJ_AV_OK) {
            return NJ_AV_ERR_CORRUPT;
        }
        d->mv[base] = (nj_s2)mvx;
        d->mv[base + 1] = (nj_s2)mvy;
        h263_predict_mb(d, pic, mbx, mby, mvx, mvy, py, pcb, pcr);
    } else {
        d->mv[base] = 0;
        d->mv[base + 1] = 0;
    }

for (i = 0; i < 6; i++) {
        int coded = (cbp & 32) != 0;
        cbp <<= 1;

        if (h263_decode_block(b, blk, intra, coded) != NJ_AV_OK) {
            return NJ_AV_ERR_CORRUPT;
        }
        h263_dequant_block(blk, intra, *qscale);
        nj_idct8x8(blk, pix);

        if (i < 4) {
            nj_u1 *dst = d->cur
                       + (mby * 16 + (i >> 1) * 8) * pic->width
                       + mbx * 16 + (i & 1) * 8;
            if (intra) {
                h263_put_intra(dst, pic->width, pix);
            } else {
                h263_put_inter(dst, pic->width, pix,
                               py + (i >> 1) * 8 * 16 + (i & 1) * 8, 16);
            }
        } else {
            nj_u1 *plane = d->cur + d->y_size + (i == 5 ? d->c_size : 0);
            nj_u1 *dst = plane + (mby * 8) * (pic->width / 2) + mbx * 8;
            if (intra) {
                h263_put_intra(dst, pic->width / 2, pix);
            } else {
                h263_put_inter(dst, pic->width / 2, pix,
                               (i == 4 ? pcb : pcr), 8);
            }
        }
    }
    return NJ_AV_OK;
}

static int h263_decode_picture(nj_h263_dec *d, const nj_u1 *data, nj_u4 len)
{
    h263_bits b;
    h263_picture pic;
    int rc, mbx, mby, q, gob_rows;

    if (data == NULL || len < 4u || len > 8u * 1024u * 1024u) {
        return NJ_AV_ERR_CORRUPT;
    }
    memset(&b, 0, sizeof(b));
    b.p = data;
    b.nbits = len * 8u;

    rc = h263_parse_header(&b, &pic);
    if (rc != NJ_AV_OK) {
        return rc;
    }
    if (pic.width != d->width || pic.height != d->height) {
        return NJ_AV_ERR_CORRUPT;             /* mid-stream resize: refused */
    }
    if (pic.pict_type == 1 && !d->have_ref) {
        return NJ_AV_ERR_CORRUPT;             /* P frame without a reference */
    }

    /* Neutral chroma, black luma.  Every macroblock then either writes itself
     * (intra) or is built from the reference (inter/skipped). */
    memset(d->cur, 0, d->y_size);
    memset(d->cur + d->y_size, 128, d->c_size * 2u);

    q = pic.qscale;
    gob_rows = pic.height <= 400 ? 1 : (pic.height <= 800 ? 2 : 4);
    for (mby = 0; mby < pic.mbh; mby++) {
        for (mbx = 0; mbx < pic.mbw; mbx++) {
            if (mbx == 0 && mby % gob_rows == 0) {
                (void)h263_gob_header(&b, gob_rows, mby, &q);
            }
            rc = h263_decode_mb(d, &b, &pic, mbx, mby, &q);
            if (rc != NJ_AV_OK) {
                return rc;
            }
        }
    }
    if (b.err) {
        return NJ_AV_ERR_CORRUPT;
    }
    return NJ_AV_OK;
}

/* ------------------------------------------------------------------ */
/* RGB565 output                                                       */
/* ------------------------------------------------------------------ */

static void h263_to_rgb565(const nj_h263_dec *d, nj_u1 *out)
{
    int x, y;
    int w = d->width;
    int cw = w / 2;
    const nj_u1 *yp = d->cur;
    const nj_u1 *up = d->cur + d->y_size;
    const nj_u1 *vp = up + d->c_size;

    for (y = 0; y < d->height; y++) {
        for (x = 0; x < w; x++) {
            int yy = (int)yp[y * w + x] - 16;
            int u = (int)up[(y >> 1) * cw + (x >> 1)] - 128;
            int v = (int)vp[(y >> 1) * cw + (x >> 1)] - 128;
            int r, g, bl;
            nj_u2 px;

            if (yy < 0) {
                yy = 0;
            }
            r  = (298 * yy + 409 * v + 128) >> 8;
            g  = (298 * yy - 100 * u - 208 * v + 128) >> 8;
            bl = (298 * yy + 516 * u + 128) >> 8;
            r  = r < 0 ? 0 : (r > 255 ? 255 : r);
            g  = g < 0 ? 0 : (g > 255 ? 255 : g);
            bl = bl < 0 ? 0 : (bl > 255 ? 255 : bl);
            px = (nj_u2)(((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3));
            memcpy(out + (nj_u4)(y * w + x) * 2u, &px, sizeof(px));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Provider glue                                                       */
/* ------------------------------------------------------------------ */

static int h263_is_psc(const nj_u1 *p, nj_u4 len, nj_u4 at)
{
    if (p == NULL || at > len || len - at < 3u) {
        return 0;
    }
    return p[at] == 0 && p[at + 1] == 0 && (p[at + 2] & 0xFCu) == 0x80u;
}

static nj_u4 h263_raw_next(const nj_u1 *p, nj_u4 len, nj_u4 at)
{
    nj_u4 i;

    if (at >= len || len - at < 6u) {
        return len;
    }
    for (i = at + 3u; len - i >= 3u; i++) {
        if (h263_is_psc(p, len, i)) {
            return i;
        }
    }
    return len;
}

static void h263_free_state(nj_h263_dec *d)
{
    const nj_av_mem *m = d->mem;

    if (m == NULL || m->free == NULL) {
        return;
    }
    if (d->cur != NULL) {
        m->free(m->ud, d->cur);
    }
    if (d->ref != NULL) {
        m->free(m->ud, d->ref);
    }
    if (d->mv != NULL) {
        m->free(m->ud, d->mv);
    }
    m->free(m->ud, d);
}

static int h263_alloc_state(nj_h263_dec *d, int width, int height)
{
    nj_u4 mv_bytes;

    d->width = width;
    d->height = height;
    d->mbw = width / 16;
    d->mbh = height / 16;
    d->y_size = (nj_u4)width * (nj_u4)height;
    d->c_size = d->y_size / 4u;
    d->frame_size = d->y_size + 2u * d->c_size;

    d->cur = (nj_u1 *)d->mem->alloc(d->mem->ud, d->frame_size);
    d->ref = (nj_u1 *)d->mem->alloc(d->mem->ud, d->frame_size);
    mv_bytes = (nj_u4)d->mbw * (nj_u4)d->mbh * 2u * (nj_u4)sizeof(nj_s2);
    d->mv = (nj_s2 *)d->mem->alloc(d->mem->ud, mv_bytes);
    if (d->cur == NULL || d->ref == NULL || d->mv == NULL) {
        return NJ_AV_ERR_MEMORY;
    }
    memset(d->cur, 0, d->y_size);
    memset(d->cur + d->y_size, 128, d->c_size * 2u);
    memset(d->ref, 0, d->y_size);
    memset(d->ref + d->y_size, 128, d->c_size * 2u);
    memset(d->mv, 0, mv_bytes);
    d->have_ref = 0;
    return NJ_AV_OK;
}

static int h263_accepts(const nj_u1 *data, nj_u4 len)
{
    if (data == NULL || len < 3u) {
        return 0;
    }
    if (h263_is_psc(data, len, 0)) {
        return 1;
    }
    if (len >= 8u && memcmp(data + 4, "ftyp", 4) == 0) {
        return 1;
    }
    return 0;
}

static nj_av_dec *h263_open(const nj_u1 *data, nj_u4 len,
                            const nj_av_mem *mem, nj_av_info *info,
                            const char **err)
{
    nj_h263_dec *d;
    int rc;

    if (err != NULL) {
        *err = NULL;
    }
    if (data == NULL || len < 4u || mem == NULL ||
        mem->alloc == NULL || mem->free == NULL) {
        return NULL;
    }
    d = (nj_h263_dec *)mem->alloc(mem->ud, (nj_u4)sizeof(*d));
    if (d == NULL) {
        if (err != NULL) {
            *err = "H.263: out of memory";
        }
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    d->mem = mem;
    d->data = data;
    d->len = len;
    d->info = info;

    if (h263_is_psc(data, len, 0)) {
        h263_bits b;
        h263_picture pic;

        memset(&b, 0, sizeof(b));
        b.p = data;
        b.nbits = (len > 1024u ? 1024u : len) * 8u;
        rc = h263_parse_header(&b, &pic);
        if (rc != NJ_AV_OK) {
            if (err != NULL) {
                *err = rc == NJ_AV_ERR_UNSUPPORTED
                     ? "H.263: only baseline H.263 is decoded"
                     : "H.263: corrupt picture header";
            }
            h263_free_state(d);
            return NULL;
        }
        d->is_mp4 = 0;
        d->raw_first = 1;
        d->next_at = 0;
        if (h263_alloc_state(d, pic.width, pic.height) != NJ_AV_OK) {
            if (err != NULL) {
                *err = "H.263: out of memory";
            }
            h263_free_state(d);
            return NULL;
        }
        if (info != NULL) {
            memset(info, 0, sizeof(*info));
            info->format = NJ_AV_VIDEO_H263;
            info->width = (nj_u4)pic.width;
            info->height = (nj_u4)pic.height;
            info->fps = 25;
            info->pixfmt = NJ_AV_PIX_RGB565;
        }
        return (nj_av_dec *)d;
    }

    if (len >= 8u && memcmp(data + 4, "ftyp", 4) == 0) {
        nj_mp4_sample s;
        nj_u8 ms;

        rc = nj_mp4_open(&d->mp4, data, len);
        if (rc != NJ_MP4_OK) {
            if (err != NULL) {
                if (rc == NJ_MP4_ERR_NO_VIDEO) {
                    *err = "H.263: the file has no video track";
                } else if (rc == NJ_MP4_ERR_CODEC) {
                    *err = "H.263: the video track is not H.263";
                } else if (rc == NJ_MP4_ERR_FEATURE) {
                    *err = "H.263: unsupported movie layout";
                } else {
                    *err = "H.263: corrupt ISO base media file";
                }
            }
            h263_free_state(d);
            return NULL;
        }
        d->is_mp4 = 1;
        if (d->mp4.codec == NJ_MP4_CODEC_MP4V) {
            if (err != NULL) {
                *err = "H.263: MPEG-4 Part 2 video is not decoded";
            }
            h263_free_state(d);
            return NULL;
        }
        if (d->mp4.codec != NJ_MP4_CODEC_S263 &&
            d->mp4.codec != NJ_MP4_CODEC_H263 &&
            d->mp4.codec != NJ_MP4_CODEC_H263LC) {
            if (err != NULL) {
                *err = "H.263: the video track is not H.263";
            }
            h263_free_state(d);
            return NULL;
        }
        if (d->mp4.width < 16u || d->mp4.height < 16u ||
            d->mp4.width > H263_MAX_PIXELS / d->mp4.height ||
            (d->mp4.width & 15u) != 0 || (d->mp4.height & 15u) != 0) {
            if (err != NULL) {
                *err = "H.263: unsupported picture size";
            }
            h263_free_state(d);
            return NULL;
        }
        rc = nj_mp4_next(&d->mp4, &s);
        if (rc != 1 || s.size < 4u || s.size > 8u * 1024u * 1024u) {
            if (err != NULL) {
                *err = "H.263: the video track has no usable samples";
            }
            h263_free_state(d);
            return NULL;
        }
        nj_mp4_rewind(&d->mp4);
        if (h263_alloc_state(d, (int)d->mp4.width,
                             (int)d->mp4.height) != NJ_AV_OK) {
            if (err != NULL) {
                *err = "H.263: out of memory";
            }
            h263_free_state(d);
            return NULL;
        }
        if (info != NULL) {
            memset(info, 0, sizeof(*info));
            info->format = NJ_AV_VIDEO_H263;
            info->width = d->mp4.width;
            info->height = d->mp4.height;
            info->fps = d->mp4.fps != 0 ? d->mp4.fps : 25u;
            info->frames = d->mp4.sample_count;
            info->pixfmt = NJ_AV_PIX_RGB565;
            if (d->mp4.timescale != 0) {
                ms = ((nj_u8)d->mp4.duration * 1000u
                      + d->mp4.timescale / 2u) / d->mp4.timescale;
                info->duration_ms = ms > 0xFFFFFFFFu
                                  ? 0xFFFFFFFFu : (nj_u4)ms;
            }
        }
        return (nj_av_dec *)d;
    }

    if (err != NULL) {
        *err = "H.263: neither an H.263 stream nor a 3GP/MP4 file";
    }
    h263_free_state(d);
    return NULL;
}

static int h263_decode(nj_av_dec *dec, nj_av_step *step)
{
    nj_h263_dec *d = (nj_h263_dec *)dec;
    const nj_u1 *frame = NULL;
    nj_u4 frame_len = 0;
    nj_u4 used = 0;
    int rc;

    if (d == NULL || step == NULL || d->cur == NULL) {
        return NJ_AV_ERR_CORRUPT;
    }
    if (step->out == NULL ||
        (nj_u8)step->out_cap < (nj_u8)d->width * d->height * 2u) {
        return H263_ERR_OUTPUT;
    }
    step->frames = 0;
    step->out_used = 0;
    step->time_us = -1;

    if (d->is_mp4) {
        nj_mp4_sample s;

        rc = nj_mp4_next(&d->mp4, &s);
        if (rc == 0) {
            return NJ_AV_ERR_END;
        }
        if (rc < 0) {
            return NJ_AV_ERR_CORRUPT;
        }
        if (s.offset >= d->len || s.size > d->len - s.offset) {
            return NJ_AV_ERR_CORRUPT;
        }
        frame = d->data + s.offset;
        frame_len = s.size;
        step->time_us = (nj_s8)(((nj_u8)s.dts * 1000000u
                                + d->mp4.timescale / 2u)
                               / d->mp4.timescale);
    } else {
        nj_u4 start = d->next_at;
        nj_u4 next;

        if (start >= d->len) {
            return NJ_AV_ERR_END;
        }
        next = h263_raw_next(d->data, d->len, start);
        if (next <= start || next - start > 8u * 1024u * 1024u) {
            return NJ_AV_ERR_CORRUPT;
        }
        frame = d->data + start;
        frame_len = next - start;
        used = frame_len;
        d->next_at = next;
    }

    rc = h263_decode_picture(d, frame, frame_len);
    if (rc != NJ_AV_OK) {
        return rc;
    }
    h263_to_rgb565(d, step->out);
    step->out_used = (nj_u4)d->width * (nj_u4)d->height * 2u;
    step->frames = 1;
    step->in_used = used;

    {
        nj_u1 *tmp = d->ref;
        d->ref = d->cur;
        d->cur = tmp;
    }
    d->have_ref = 1;
    return NJ_AV_OK;
}

static void h263_close(nj_av_dec *dec)
{
    if (dec != NULL) {
        h263_free_state((nj_h263_dec *)dec);
    }
}

const nj_av_video_ops nj_av_video_h263 = {
    "h263",
    NJ_AV_VIDEO_H263,
    h263_accepts,
    h263_open,
    h263_decode,
    h263_close
};

#endif /* NJ_ENABLE_VIDEO */
