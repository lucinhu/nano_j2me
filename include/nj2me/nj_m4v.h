/*
 * nano_j2me - include/nj2me/nj_m4v.h
 *
 * MPEG-4 Visual (Part 2) bitstream framing: start codes and the video object
 * layer header.  Decoding pictures lives in the provider registered with
 * nj_avcodec; this header is what the container reader and that provider agree
 * on.  Field order follows the reference decoder (ffmpeg
 * libavcodec/mpeg4videodec.c, decode_vol_header), not memory.
 */
#ifndef NJ_M4V_H
#define NJ_M4V_H

#include "nj2me/nj.h"

#define NJ_M4V_VOS  0xB0u
#define NJ_M4V_VO   0x00u
#define NJ_M4V_VOL  0x20u
#define NJ_M4V_VOP  0xB6u
#define NJ_M4V_GOP  0xB3u
#define NJ_M4V_VUD  0xB2u

typedef struct nj_m4v_vol {
    nj_u4 width;
    nj_u4 height;
    nj_u4 time_res;      /* vop_time_increment_resolution */
    nj_u4 fixed_inc;     /* when a fixed vop rate is declared */
    int   has_fixed_rate;
    int   shape;         /* 0 rectangular, 1 binary only, 2 grayscale */
    int   vo_type;
    /* Flags past the geometry, which change the macroblock syntax. */
    int   interlaced;
    int   obmc_disabled;
    int   sprite_usage;
    int   not_8_bit;
    int   quant_type;       /* vol_quant_type: H.263 style quantisation */
    int   quarter_sample;
    int   complexity_disabled;
    int   resync_disabled;
    int   data_partitioned;
    int   reversible_vlc;
    int   intra_dc_thr;     /* 3-bit index into ff_mpeg4_dc_threshold */
    int   flags_known;
} nj_m4v_vol;

/* Returns 1 and fills the start code and the payload offset on success. */
int nj_m4v_start_code(const nj_u1 *b, nj_u4 n, nj_u4 from, nj_u4 *code, nj_u4 *pos);

/* Parses a VOL header whose payload starts at b[pos]. */
int nj_m4v_parse_vol(const nj_u1 *b, nj_u4 n, nj_u4 pos, nj_m4v_vol *out);

typedef struct nj_m4v_vop {
    int   type;           /* 0 I, 1 P (B is not in Simple Profile)          */
    nj_u4 time_increment; /* needs the VOL's time_res to mean anything      */
    int   coded;          /* vop_coded: 0 means the frame is skipped        */
    int   qscale;         /* vop_quant, 1..31, valid when coded             */
    int   dc_vlc_thr;     /* intra_dc_vlc_thr, 0..7                         */
} nj_m4v_vop;

/* Parses a VOP header whose payload starts at b[pos]. */
int nj_m4v_parse_vop(const nj_u1 *b, nj_u4 n, nj_u4 pos, nj_u4 time_res,
                     nj_m4v_vop *out);

/* Same, but reads and then updates a bit offset: the macroblock layer starts
 * exactly where the header ended, so the byte-granular wrapper above cannot be
 * used for decoding. */
int nj_m4v_parse_vop_bits(const nj_u1 *b, nj_u4 n, nj_u4 *bitpos,
                          nj_u4 time_res, nj_m4v_vop *out);

/* Finds the next VOP start code and stores its payload offset. */
int nj_m4v_find_vop(const nj_u1 *b, nj_u4 n, nj_u4 *pos);

/* Decodes one intra AC VLC symbol at bit position *pos, advancing it.  Returns
 * the symbol (0 is the escape) or -1 when the bits match no codeword. */
int nj_m4v_decode_ac(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int *last);

/* Decodes the intra DC size delta VLC (B-13).  Returns the size 0..12 or -1. */
int nj_m4v_decode_dc(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int luma);

/* Reads the signed DC delta that follows a size code (ISO 14496-2 7.3.3).
 * Returns the delta, or -1 when the bits run out. */
int nj_m4v_dc_delta(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int size);

/* Expands an AC symbol into run/level and the last flag.  Returns 0 for the
 * escape symbol (0) and for out-of-range input. */
int nj_m4v_ac_sym(int sym, int *run, int *level, int *last);

/* Natural index of the k-th coefficient in the intra scan (14496-2 7.4.3). */
int nj_m4v_scan_index(int k);

/* Stores one coefficient at scan position k; the DC term is position 0. */
void nj_m4v_coef_store(nj_s4 *coef, int k, int level);

/* Applies one AC symbol: advances the scan cursor by its run, stores the
 * level and steps past it.  Returns 0, -1 (run past the block) or -2 for the
 * escape symbol, which the caller must decode itself. */
int nj_m4v_coef_apply(nj_s4 *coef, int *k, int sym, int *last);

/* Walks AC symbols until one carries last.  Returns the next free scan
 * position, -1 for a corrupt stream or -3 when the escape form is reached. */
int nj_m4v_qmul(int qscale);
/* Third escape form: reads "11" form bits plus last/run/markers/level. */
int nj_m4v_escape1(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                   int *last, int *run, int *level);
int nj_m4v_escape2(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                   int *last, int *run, int *level);
int nj_m4v_escape3(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int qscale,
                   int *last, int *run, int *level);
int nj_m4v_qadd(int qscale);

int nj_m4v_ac_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos, nj_s4 *coef, int k,
                    int qscale);

/* Decodes one intra 8x8 block: DC size+delta on top of dc_pred, then AC.
 * Returns 0, -1 for a corrupt stream or -3 for an escape not decoded yet. */
int nj_m4v_decode_intra_block(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                              nj_s4 *coef, int luma, int dc_pred, int qscale);

/* The inverse transform shared with the H.263 decoder, for one 8x8 block. */
void nj_m4v_idct(const nj_s4 *coef, int *out);

/* Decodes one intra block to 8-bit pixels (IDCT plus the +128 level shift). */
int nj_m4v_decode_intra_pixels(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                               nj_u1 *pix, int luma, int dc_pred, int qscale);

/* Macroblock raster size of a picture, rounded up to whole 16x16 units. */
int nj_m4v_frame_macroblocks(nj_u4 width, nj_u4 height, nj_u4 *mbw, nj_u4 *mbh);

/* Top-left pixel of macroblock block blk (0..3 luma in raster order, 4 Cb,
 * 5 Cr) in a 4:2:0 picture. */
int nj_m4v_block_origin(nj_u4 mbx, nj_u4 mby, int blk, nj_u4 *x, nj_u4 *y);

/* Intra DC quantiser scale for VOP quantiser q (1..31), luma or chroma. */
int nj_m4v_dc_scale(int q, int luma);

/* 4:2:0 chroma plane size: half in each direction, rounded up. */
int nj_m4v_plane_size(nj_u4 width, nj_u4 height, nj_u4 *cw, nj_u4 *ch);

/* Scales a neighbour block DC to this block quantiser (14496-2 7.4.3.4). */
int nj_m4v_dc_predict(int prev_dc, int prev_scale, int cur_scale);

/* DC prediction (14496-2 7.4.3.4): the reference decoder picks the top-left
 * neighbour when |a-b| < |b-c| and the left one otherwise, then divides the
 * dequantised prediction by the quantiser scale with rounding.  Negative
 * arguments mean "outside the picture" and become the 1024 neutral value. */
int nj_m4v_dc_predict_q(int a, int b, int c, int scale, int *dir);

/* Adds the transmitted level to the prediction and returns the quantised DC,
 * writing the dequantised, clamped value the next block will predict from. */
int nj_m4v_dc_level(int delta, int pred_dequant, int scale, int *stored);

/* Decode one block's DC into a dequantised neighbour grid of `wrap` columns:
 * predicts from the left/top/top-left entries (1024 outside the picture),
 * adds `delta` and stores the clamped dequantised value for later blocks.
 * Returns the quantised DC, or -1 on bad arguments. */
int nj_m4v_dc_block(nj_s2 *grid, int wrap, int bx, int by, int delta, int scale);

/* Grid position of one of the six blocks of a macroblock: luma blocks on the
 * 8x8 grid, Cb/Cr on their own quarter-resolution grid. */
int nj_m4v_block_grid(int blk, int mbx, int mby, int *gx, int *gy);

/* Which of the three predicted-from neighbours (left, top, top-left) exist for
 * the block whose grid corner is (bx, by). */
void nj_m4v_dc_avail(int bx, int by, int *has_a, int *has_b, int *has_c);

/* Intra DC coefficient in the transform domain: the DC scaler is the
 * MPEG-4 quantiser step for the DC term, matching the H.263 path which
 * multiplies intra DC by 8 before the same inverse transform. */
int nj_m4v_dc_coef(int level, int scale);

/* Decodes one I-VOP payload into 4:2:0 planes.  `grids' must hold
 * 2*mbw*mbh + 2*mbw*mbh nj_s4 values (two luma 8x8 grids plus Cb and Cr). */
int nj_m4v_decode_ivop(const nj_u1 *b, nj_u4 n, nj_u4 *pos,
                       const nj_m4v_vol *vol, const nj_m4v_vop *vop,
                       nj_s4 *grids, nj_u1 *yp, int ys,
                       nj_u1 *up, int us, nj_u1 *vp, int vs);

/* Converts a 4:2:0 picture with the BT.601 limited-range matrix into RGB565.
 * Strides are in bytes; out must hold w * h * 2. */
void nj_m4v_yuv420_to_rgb565(const nj_u1 *yp, int ys, const nj_u1 *up, int us,
                             const nj_u1 *vp, int vs, int w, int h, nj_u1 *out);

/* Read an intra macroblock header (intra MCBPC, optional dquant, CBPY). */
int nj_m4v_mb_intra_header(const nj_u1 *b, nj_u4 n, nj_u4 *pos, int *cbp, int *dq);

/* True when the buffer holds a VOS header and a parseable rectangular VOL. */
int nj_m4v_accepts(const nj_u1 *b, nj_u4 n);

#endif
