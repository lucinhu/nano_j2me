/*
 * nano_j2me - nj_mp3.c
 *
 * MPEG-1/2/2.5 Layer III software decoder, exposed as an nj_avcodec.h audio
 * provider.  STUB: the decoder core is being written; until it lands, accepts()
 * recognises a Layer III frame header and open() reports unsupported, which is
 * exactly the answer a trimmed build gives.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_avcodec.h"

static int mp3_accepts(const nj_u1 *data, nj_u4 len)
{
    nj_u4 h;

    if (data == NULL || len < 4u) {
        return 0;
    }
    h = ((nj_u4)data[0] << 24) | ((nj_u4)data[1] << 16)
        | ((nj_u4)data[2] << 8) | (nj_u4)data[3];
    if ((h & 0xFFE00000u) != 0xFFE00000u) {     /* frame sync          */
        return 0;
    }
    if (((h >> 17) & 3u) == 0u) {               /* layer: 0 is reserved */
        return 0;
    }
    if (((h >> 12) & 0xFu) == 0xFu) {           /* bitrate 1111 invalid */
        return 0;
    }
    if (((h >> 10) & 3u) == 3u) {               /* sample rate reserved */
        return 0;
    }
    return 1;
}

static nj_av_dec *mp3_open(const nj_u1 *data, nj_u4 len, const nj_av_mem *mem,
                           nj_av_info *info, const char **err)
{
    (void)data; (void)len; (void)mem; (void)info;
    if (err != NULL) {
        *err = "the MP3 decoder is not built into this runtime";
    }
    return NULL;
}

static int mp3_decode(nj_av_dec *dec, nj_av_step *step)
{
    (void)dec; (void)step;
    return NJ_AV_ERR_UNSUPPORTED;
}

static void mp3_close(nj_av_dec *dec)
{
    (void)dec;
}

const nj_av_audio_ops nj_av_audio_mp3 = {
    "mp3",
    NJ_AV_AUDIO_MP3,
    mp3_accepts,
    mp3_open,
    mp3_decode,
    NULL,
    mp3_close,
    4u                          /* 16-bit stereo PCM frame */
};
