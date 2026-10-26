/*
 * Reference decoder for the MPEG-4 Part 2 work: decode the first N video
 * frames of a container with the system FFmpeg and write each one as planar
 * 4:2:0 YUV, for byte-exact comparison with tools/m4v_dump.c.
 *
 *   cc -O2 -o /tmp/refdecode_m4v tools/refdecode_m4v.c \
 *      $(pkg-config --cflags --libs libavformat libavcodec libavutil)
 *   /tmp/refdecode_m4v clip.3gp outprefix 8
 *
 * Writes <prefix>-NNN.y, <prefix>-NNN.u and <prefix>-NNN.v (one file per
 * plane per frame) and one manifest line per frame:
 *
 *   frame N W H CW CH pixfmt
 *
 * This tool is not part of the test suite: libavformat/libavcodec are not
 * build dependencies of the runtime.  It exists because the MPEG-4 Part 2
 * decoder has to agree with a real decoder, not with a synthetic reference.
 * Only the video stream is decoded, and the output is the decoder's own
 * pixel format (YUV 4:2:0 for MPEG-4 Simple Profile), so no colour
 * conversion can hide a difference.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

static int write_plane(const char *prefix, int frame, const char *ext,
                       const uint8_t *p, int w, int h, int stride)
{
    char  path[1024];
    FILE *o;
    int   y;

    snprintf(path, sizeof(path), "%s-%03d.%s", prefix, frame, ext);
    o = fopen(path, "wb");
    if (o == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    for (y = 0; y < h; y++) {
        fwrite(p + (size_t)y * (size_t)stride, 1u, (size_t)w, o);
    }
    fclose(o);
    return 0;
}

int main(int argc, char **argv)
{
    AVFormatContext *fc = NULL;
    AVCodecContext  *ctx = NULL;
    const AVCodec   *cd;
    AVPacket        *pkt;
    AVFrame         *fr;
    int              vi = -1, got = 0, max_frames = 5;
    unsigned         i;
    int              w = 0, h = 0, cw, ch;

    if (argc < 3) {
        fprintf(stderr, "usage: %s clip outprefix [max_frames]\n", argv[0]);
        return 2;
    }
    if (argc > 3) {
        max_frames = atoi(argv[3]);
    }
    av_log_set_level(AV_LOG_ERROR);
    if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0) {
        fprintf(stderr, "%s: cannot open\n", argv[1]);
        return 1;
    }
    avformat_find_stream_info(fc, NULL);
    for (i = 0; i < fc->nb_streams; i++) {
        if (fc->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vi = (int)i;
        }
    }
    if (vi < 0) {
        fprintf(stderr, "%s: no video stream\n", argv[1]);
        avformat_close_input(&fc);
        return 1;
    }
    {
        AVCodecParameters *par = fc->streams[vi]->codecpar;

        printf("codec=%s %dx%d\n", avcodec_get_name(par->codec_id), par->width,
               par->height);
        cd = avcodec_find_decoder(par->codec_id);
        if (cd == NULL) {
            fprintf(stderr, "no decoder for %s\n", avcodec_get_name(par->codec_id));
            avformat_close_input(&fc);
            return 1;
        }
        ctx = avcodec_alloc_context3(cd);
        avcodec_parameters_to_context(ctx, par);
        if (avcodec_open2(ctx, cd, NULL) < 0) {
            fprintf(stderr, "cannot open the decoder\n");
            avformat_close_input(&fc);
            return 1;
        }
        w = ctx->width;
        h = ctx->height;
    }
    cw = (w + 1) / 2;
    ch = (h + 1) / 2;
    pkt = av_packet_alloc();
    fr = av_frame_alloc();
    while (got < max_frames && av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index == vi) {
            if (avcodec_send_packet(ctx, pkt) >= 0) {
                while (got < max_frames && avcodec_receive_frame(ctx, fr) >= 0) {
                    if (fr->format != AV_PIX_FMT_YUV420P) {
                        fprintf(stderr, "unexpected pixel format %d\n", fr->format);
                        return 1;
                    }
                    if (write_plane(argv[2], got, "y", fr->data[0], w, h,
                                    fr->linesize[0]) != 0 ||
                        write_plane(argv[2], got, "u", fr->data[1], cw, ch,
                                    fr->linesize[1]) != 0 ||
                        write_plane(argv[2], got, "v", fr->data[2], cw, ch,
                                    fr->linesize[2]) != 0) {
                        return 1;
                    }
                    printf("frame %d %d %d %d %d yuv420p\n", got, w, h, cw, ch);
                    got++;
                    av_frame_unref(fr);
                }
            }
            av_packet_unref(pkt);
        } else {
            av_packet_unref(pkt);
        }
    }
    printf("frames=%d\n", got);
    av_packet_free(&pkt);
    av_frame_free(&fr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fc);
    return got > 0 ? 0 : 1;
}
