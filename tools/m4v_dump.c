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
 *
 * Two more modes exist for the Player-path evidence:
 *
 *   /tmp/m4v_dump --provider clip.3gp outprefix 8
 *       open the movie through nj_av_video_mpeg4 (the same provider a Player
 *       uses), decode N frames to RGB565 and write <prefix>-NNN.ppm; prints
 *       "rgb565 frame N fnv 0x..." for each frame.
 *   /tmp/m4v_dump --convert-yuv W H refprefix outprefix N
 *       convert tools/refdecode_m4v.c's YUV planes for N frames to RGB565 the
 *       same way (nj_m4v_yuv420_to_rgb565), write PPMs and print the same
 *       hash lines.  Equal hash lists mean the runtime and the reference
 *       decoder produce the same RGB565 frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj2me/nj.h"
#include "nj2me/nj_m4v.h"
#include "nj_mp4.h"
#include "nj2me/nj_avcodec.h"

extern const nj_av_video_ops nj_av_video_mpeg4;

static unsigned long fnv1a(const unsigned char *p, unsigned long n)
{
    unsigned long h = 2166136261ul;

    while (n-- > 0u) {
        h ^= *p++;
        h *= 16777619ul;
        h &= 0xFFFFFFFFul;
    }
    return h;
}

/* RGB565 -> RGB24 PPM row expansion (5/6/5 with bit replication). */
static void rgb565_to_ppm(const char *path, const nj_u1 *rgb, nj_u4 w, nj_u4 h)
{
    FILE *o = fopen(path, "wb");
    nj_u4 x, y;

    if (o == NULL) {
        return;
    }
    fprintf(o, "P6\n%u %u\n255\n", (unsigned)w, (unsigned)h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            nj_u2 px = (nj_u2)(rgb[(y * w + x) * 2u] |
                               ((nj_u4)rgb[(y * w + x) * 2u + 1u] << 8));
            unsigned r = (px >> 11) & 31u, g = (px >> 5) & 63u, b = px & 31u;

            fputc((int)((r << 3) | (r >> 2)), o);
            fputc((int)((g << 2) | (g >> 4)), o);
            fputc((int)((b << 3) | (b >> 2)), o);
        }
    }
    fclose(o);
}

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


/* ------------------------------------------------------------------ */
/* Modes used for the Player-path evidence                             */
/* ------------------------------------------------------------------ */

/* Read a whole file into a malloc'ed buffer (the container stays alive for the
 * provider's lifetime, as the Player keeps its source array alive). */
static unsigned char *read_all(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    long  sz;
    unsigned char *p;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = (unsigned char *)malloc((size_t)sz);
    if (p == NULL || fread(p, 1, (size_t)sz, f) != (size_t)sz) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *size = sz;
    return p;
}

static void *prov_alloc(void *ud, nj_u4 n)
{
    unsigned char *p;

    (void)ud;
    p = (unsigned char *)malloc((size_t)n + 16u);
    if (p == NULL) {
        return NULL;
    }
    return p;
}

static void prov_free(void *ud, void *p)
{
    (void)ud;
    free(p);
}

static int provider_mode(const char *clip, const char *prefix, int want)
{
    long            sz = 0;
    unsigned char  *data = read_all(clip, &sz);
    nj_av_mem       mem;
    nj_av_info      info;
    const char     *err = NULL;
    nj_av_dec      *dec;
    nj_u1          *rgb;
    int             f;

    if (data == NULL) {
        fprintf(stderr, "cannot read %s\n", clip);
        return 1;
    }
    mem.ud = NULL;
    mem.alloc = prov_alloc;
    mem.free = prov_free;
    if (!nj_av_video_mpeg4.accepts(data, (nj_u4)sz)) {
        fprintf(stderr, "%s is not an mp4v movie the provider accepts\n", clip);
        free(data);
        return 1;
    }
    memset(&info, 0, sizeof(info));
    dec = nj_av_video_mpeg4.open(data, (nj_u4)sz, &mem, &info, &err);
    if (dec == NULL) {
        fprintf(stderr, "provider open failed: %s\n", err ? err : "?");
        free(data);
        return 1;
    }
    printf("provider=%s %ux%u frames=%u fps=%u pixfmt=%u\n",
           nj_av_video_mpeg4.name, (unsigned)info.width, (unsigned)info.height,
           (unsigned)info.frames, (unsigned)info.fps, (unsigned)info.pixfmt);
    rgb = (nj_u1 *)malloc((size_t)info.width * info.height * 2u);
    for (f = 0; rgb != NULL && f < want; f++) {
        nj_av_step step;
        char       path[1024];
        int        rc;

        memset(&step, 0, sizeof(step));
        step.out = rgb;
        step.out_cap = info.width * info.height * 2u;
        rc = nj_av_video_mpeg4.decode(dec, &step);
        if (rc == NJ_AV_ERR_END) {
            printf("end of track after %d frames\n", f);
            break;
        }
        if (rc != NJ_AV_OK || step.frames != 1u) {
            fprintf(stderr, "frame %d: decode error %d\n", f, rc);
            break;
        }
        snprintf(path, sizeof(path), "%s-%03d.ppm", prefix, f);
        rgb565_to_ppm(path, rgb, info.width, info.height);
        printf("rgb565 frame %d fnv 0x%08lx\n", f,
               fnv1a(rgb, (unsigned long)step.out_used));
    }
    free(rgb);
    nj_av_video_mpeg4.close(dec);
    free(data);
    return 0;
}

static int convert_yuv_mode(int argc, char **argv)
{
    int    w, h, cw, ch, n, f;
    nj_u1 *y, *u, *v, *rgb;

    if (argc < 7) {
        fprintf(stderr, "usage: %s --convert-yuv W H REFPREFIX OUTPREFIX N\n",
                argv[0]);
        return 2;
    }
    w = atoi(argv[2]);
    h = atoi(argv[3]);
    n = atoi(argv[6]);
    cw = (w + 1) / 2;
    ch = (h + 1) / 2;
    y = (nj_u1 *)malloc((size_t)w * h);
    u = (nj_u1 *)malloc((size_t)cw * ch);
    v = (nj_u1 *)malloc((size_t)cw * ch);
    rgb = (nj_u1 *)malloc((size_t)w * h * 2u);
    if (y == NULL || u == NULL || v == NULL || rgb == NULL) {
        return 1;
    }
    for (f = 0; f < n; f++) {
        char  path[1024];
        FILE *in;
        char  out[1024];
        int   ok = 1;

        snprintf(path, sizeof(path), "%s-%03d.y", argv[4], f);
        in = fopen(path, "rb");
        ok = ok && in != NULL && fread(y, 1, (size_t)w * h, in) == (size_t)w * h;
        if (in != NULL) fclose(in);
        snprintf(path, sizeof(path), "%s-%03d.u", argv[4], f);
        in = fopen(path, "rb");
        ok = ok && in != NULL && fread(u, 1, (size_t)cw * ch, in) == (size_t)cw * ch;
        if (in != NULL) fclose(in);
        snprintf(path, sizeof(path), "%s-%03d.v", argv[4], f);
        in = fopen(path, "rb");
        ok = ok && in != NULL && fread(v, 1, (size_t)cw * ch, in) == (size_t)cw * ch;
        if (in != NULL) fclose(in);
        if (!ok) {
            fprintf(stderr, "cannot read reference planes for frame %d\n", f);
            return 1;
        }
        nj_m4v_yuv420_to_rgb565(y, w, u, cw, v, cw, w, h, rgb);
        snprintf(out, sizeof(out), "%s-%03d.ppm", argv[5], f);
        rgb565_to_ppm(out, rgb, (nj_u4)w, (nj_u4)h);
        printf("rgb565 frame %d fnv 0x%08lx\n", f,
               fnv1a(rgb, (unsigned long)w * (unsigned long)h * 2ul));
    }
    free(y);
    free(u);
    free(v);
    free(rgb);
    return 0;
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

    if (argc >= 2 && strcmp(argv[1], "--convert-yuv") == 0) {
        return convert_yuv_mode(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "--provider") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: %s --provider clip.3gp outprefix [frames]\n",
                    argv[0]);
            return 2;
        }
        return provider_mode(argv[2], argv[3], argc > 4 ? atoi(argv[4]) : 5);
    }
    if (argc < 3) {
        fprintf(stderr, "usage: %s clip.3gp outprefix [frames]\n"
                        "       %s --provider clip.3gp outprefix [frames]\n"
                        "       %s --convert-yuv W H refprefix outprefix N\n",
                argv[0], argv[0], argv[0]);
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
