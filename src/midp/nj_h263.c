/*
 * nano_j2me - nj_h263.c
 *
 * H.263 baseline / MPEG-4 Part 2 Simple Profile software decoder exposed as an
 * nj_avcodec.h video provider.  STUB: recognises a 3GP/MP4 container marker and
 * a couple of elementary-stream starts, and reports unsupported until the
 * decoder lands.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_avcodec.h"

static int h263_accepts(const nj_u1 *data, nj_u4 len)
{
    if (data == NULL || len < 8u) {
        return 0;
    }
    /* 3GP/MP4 family: ISO base media file format box header. */
    if (data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') {
        return 1;
    }
    return 0;
}

static nj_av_dec *h263_open(const nj_u1 *data, nj_u4 len, const nj_av_mem *mem,
                            nj_av_info *info, const char **err)
{
    (void)data; (void)len; (void)mem; (void)info;
    if (err != NULL) {
        *err = "the video decoder is not built into this runtime";
    }
    return NULL;
}

static int h263_decode(nj_av_dec *dec, nj_av_step *step)
{
    (void)dec; (void)step;
    return NJ_AV_ERR_UNSUPPORTED;
}

static void h263_close(nj_av_dec *dec)
{
    (void)dec;
}

const nj_av_video_ops nj_av_video_h263 = {
    "h263",
    NJ_AV_VIDEO_H263,
    h263_accepts,
    h263_open,
    h263_decode,
    h263_close
};
