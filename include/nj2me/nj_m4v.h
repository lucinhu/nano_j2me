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

typedef struct nj_m4v_vol {
    nj_u4 width;
    nj_u4 height;
    nj_u4 time_res;      /* vop_time_increment_resolution */
    nj_u4 fixed_inc;     /* when a fixed vop rate is declared */
    int   has_fixed_rate;
    int   shape;         /* 0 rectangular, 1 binary only, 2 grayscale */
    int   vo_type;
} nj_m4v_vol;

/* Returns 1 and fills the start code and the payload offset on success. */
int nj_m4v_start_code(const nj_u1 *b, nj_u4 n, nj_u4 from, nj_u4 *code, nj_u4 *pos);

/* Parses a VOL header whose payload starts at b[pos]. */
int nj_m4v_parse_vol(const nj_u1 *b, nj_u4 n, nj_u4 pos, nj_m4v_vol *out);

/* True when the buffer holds a VOS header and a parseable rectangular VOL. */
int nj_m4v_accepts(const nj_u1 *b, nj_u4 n);

#endif
