/*
 * Dump the first N video frames of an MPEG-4 Part 2 (mp4v) movie as planar
 * 4:2:0 YUV with the runtime's own decoder, so they can be compared against
 * the reference decoder's output byte for byte.
 *
 * This tool links the decoder sources directly instead of going through a
 * Player, because the provider is not registered yet (that is the next
 * milestone).  Build and run:
 *
 *   cc -O2 -std=c99 -I include -I src/midp -o /tmp/m4v_dump \
 *      tools/m4v_dump.c src/midp/nj_m4v.c src/midp/nj_mp4.c
 *
 * (-std=c99 matters: the runtime headers declare their own C library
 * surface, and the hosted string.h only stays out of the way in C99 mode.)
 *   /tmp/m4v_dump clip.3gp outprefix 8
 *
 * For every frame N it writes <prefix>-NNN.y, <prefix>-NNN.u and
 * <prefix>-NNN.v (width x height, then the two half-size planes) and prints
 * "frame N type=T q=Q r=R bits=USED/TOTAL".  Pair it with
 * tools/refdecode_m4v.c + tools/compare_m4v.py for the pixel comparison.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj2me/nj.h"
#include "nj2me/nj_m4v.h"
#include "nj_mp4.h"

static void dump_plane(const char *prefix, int frame, const char *ext,
                       const nj_u1 *p, nj_u4 w, nj_u4 h, nj_u4 stride)
{
    char  path[512];
    FILE *o;
    nj_u4 y;

    snprintf(path, sizeof(path), "%s-%03d.%s", prefix, frame, ext);
    o = fopen(path, "wb");
    if (o == NULL) {
        return;
    }
    for (y = 0; y < h; y++) {
        fwrite(p + (size_t)y * stride, 1u, (size_t)w, o);
    }
    fclose(o);
}

int main(int argc, char **argv)
{
    FILE          *f;
    long           sz;
    unsigned char *b;
    nj_mp4         m;
    nj_mp4_sample  s;
    nj_m4v_vol     vol;
    nj_u4          mbw = 0, mbh = 0, cw = 0, ch = 0, pos = 0, i;
    nj_u4          ys = 0, cs = 0, crows = 0;   /* coded plane geometry */
    nj_s4         *grids;
    nj_u1         *yp, *up, *vp, *py, *pu, *pv;
    int            want = argc > 3 ? atoi(argv[3]) : 5;
    int            frame = 0;
    int            rc = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s clip.3gp outprefix [frames]\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc((size_t)sz);
    if (b == NULL) {
        fclose(f);
        return 1;
    }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) {
        free(b);
        fclose(f);
        return 1;
    }
    fclose(f);
    if (nj_mp4_open(&m, b, (nj_u4)sz) != NJ_MP4_OK) {
        fprintf(stderr, "%s: not a decodable MP4/3GP container\n", argv[1]);
        free(b);
        return 3;
    }
    if (m.cfg_size == 0u) {
        fprintf(stderr, "%s: no decoder configuration (esds)\n", argv[1]);
        free(b);
        return 3;
    }
    if (m.codec != NJ_MP4_CODEC_MP4V) {
        fprintf(stderr, "%s: sample entry is not mp4v (0x%08x)\n", argv[1],
                (unsigned)m.codec);
        free(b);
        return 3;
    }
    {
        nj_u4 code = 0, from = 0;

        while (nj_m4v_start_code(b + m.cfg_at, m.cfg_size, from, &code, &pos)) {
            if (code == NJ_M4V_VOL) {
                if (!nj_m4v_parse_vol(b + m.cfg_at, m.cfg_size, pos, &vol)) {
                    fprintf(stderr, "VOL does not parse\n");
                    free(b);
                    return 4;
                }
                break;
            }
            from = pos;
        }
    }
    if (nj_m4v_frame_macroblocks(vol.width, vol.height, &mbw, &mbh) != 0 ||
        nj_m4v_plane_size(vol.width, vol.height, &cw, &ch) != 0) {
        free(b);
        return 5;
    }
    /* The coded picture is a whole number of macroblocks; the VOL may crop it
     * (240x180, the HAWX movie).  Decode into coded-size planes and dump the
     * visible rectangle, exactly like the reference decoder's output. */
    ys = mbw * 16u;
    cs = mbw * 8u;
    crows = mbh * 8u;
    printf("codec=mp4v %ux%u samples=%u frames=%u fps=%u time_res=%u\n",
           (unsigned)vol.width, (unsigned)vol.height, (unsigned)m.sample_count,
           (unsigned)m.sample_count, (unsigned)m.fps, (unsigned)vol.time_res);
    grids = (nj_s4 *)calloc(
        (size_t)((nj_size)mbw * 2u * (nj_size)(mbh * 2u) +
                 (nj_size)mbw * (nj_size)mbh * 2u),
        sizeof(nj_s4));
    yp = (nj_u1 *)malloc((size_t)ys * mbh * 16u);
    up = (nj_u1 *)malloc((size_t)cs * crows);
    vp = (nj_u1 *)malloc((size_t)cs * crows);
    py = (nj_u1 *)malloc((size_t)ys * mbh * 16u);
    pu = (nj_u1 *)malloc((size_t)cs * crows);
    pv = (nj_u1 *)malloc((size_t)cs * crows);
    if (grids == NULL || yp == NULL || up == NULL || vp == NULL ||
        py == NULL || pu == NULL || pv == NULL) {
        rc = 6;
        goto done;
    }
    (void)i;
    while (frame < want && nj_mp4_next(&m, &s) == 1) {
        nj_m4v_vop vop;
        nj_u4      vpos;
        nj_u4      bitpos;

        if (nj_m4v_find_vop(b + s.offset, s.size, &vpos) <= 0) {
            fprintf(stderr, "frame %d: no VOP in the sample\n", frame);
            rc = 7;
            break;
        }
        bitpos = vpos * 8u;
        if (nj_m4v_parse_vop_bits_vol(b + s.offset, s.size, &bitpos, &vol, &vop) <= 0) {
            fprintf(stderr, "frame %d: VOP header does not parse\n", frame);
            rc = 7;
            break;
        }
        if (vop.type == 0) {
            rc = nj_m4v_decode_ivop(b + s.offset, s.size, &bitpos, &vol, &vop,
                                    grids, yp, (int)ys, up, (int)cs,
                                    vp, (int)cs);
        } else if (vop.type == 1) {
            rc = nj_m4v_decode_pvop(b + s.offset, s.size, &bitpos, &vol, &vop,
                                    grids, yp, (int)ys, up, (int)cs,
                                    vp, (int)cs, py, (int)ys, pu, (int)cs,
                                    pv, (int)cs);
        } else {
            fprintf(stderr, "frame %d: VOP type %d is not Simple Profile\n",
                    frame, vop.type);
            rc = 8;
            break;
        }
        printf("frame %d type=%d q=%d r=%d bits=%u/%u\n", frame, vop.type,
               vop.qscale, rc, (unsigned)bitpos, (unsigned)(s.size * 8u));
        if (rc != 0) {
            break;
        }
        dump_plane(argv[2], frame, "y", yp, vol.width, vol.height, ys);
        dump_plane(argv[2], frame, "u", up, cw, ch, cs);
        dump_plane(argv[2], frame, "v", vp, cw, ch, cs);
        memcpy(py, yp, (size_t)ys * mbh * 16u);
        memcpy(pu, up, (size_t)cs * crows);
        memcpy(pv, vp, (size_t)cs * crows);
        frame++;
    }
done:
    free(grids);
    free(yp);
    free(up);
    free(vp);
    free(py);
    free(pu);
    free(pv);
    free(b);
    return rc == 0 ? 0 : rc;
}
