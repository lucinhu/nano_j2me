/*
 * Decode the first video frame of a container with the system ffmpeg and print
 * its geometry.  Built with:
 *
 *   cc -O2 -o /tmp/refdecode_frame tools/refdecode_frame.c \
 *      $(pkg-config --cflags --libs libavformat libavcodec libavutil)
 *
 * Used as the oracle for the MPEG-4 Part 2 work: it decodes 1942.3gp and
 * doom.3gp to 176x144 / 320x240, which is what the runtime's frame driver has to
 * match.  Not part of the test suite (needs libavformat at build time).
 */
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <stdlib.h>
int main(int c,char**v){
  AVFormatContext*fc=NULL;
  av_log_set_level(AV_LOG_DEBUG);
  if(avformat_open_input(&fc,v[1],NULL,NULL)<0){puts("open fail");return 1;}
  avformat_find_stream_info(fc,NULL);
  int vi=-1; for(unsigned i=0;i<fc->nb_streams;i++) if(fc->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) vi=i;
  if(vi<0){puts("no video");return 1;}
  AVCodecParameters*par=fc->streams[vi]->codecpar;
  printf("codec=%s %dx%d\n",avcodec_get_name(par->codec_id),par->width,par->height);
  if (par->extradata != NULL && par->extradata_size > 0) {
      int i;

      printf("extradata %d:", (int)par->extradata_size);
      for (i = 0; i < par->extradata_size && i < 40; i++) {
          printf(" %02x", par->extradata[i]);
      }
      printf("\n");
  }
  printf("frames=%u fps=%d/%d\n",
         (unsigned)fc->streams[vi]->nb_frames,
         fc->streams[vi]->avg_frame_rate.num, fc->streams[vi]->avg_frame_rate.den);
  const AVCodec*cd=avcodec_find_decoder(par->codec_id);
  AVCodecContext*ctx=avcodec_alloc_context3(cd);
  avcodec_parameters_to_context(ctx,par);
  if(avcodec_open2(ctx,cd,NULL)<0){puts("dec open fail");return 1;}
  ctx->debug = FF_DEBUG_DCT_COEFF | FF_DEBUG_PICT_INFO;
  AVPacket*p=av_packet_alloc(); AVFrame*fr=av_frame_alloc();
  int got=0; while(av_read_frame(fc,p)>=0){
    if(p->stream_index==vi && avcodec_send_packet(ctx,p)>=0 && avcodec_receive_frame(ctx,fr)>=0){got++; av_packet_unref(p); break;}
    av_packet_unref(p);
  }
  printf("frames=%d w=%d h=%d\n",got,ctx->width,ctx->height);
  if (got && c > 2) {
      /* Write the decoded frame as a plain PPM so a runtime decode can be
       * compared against it pixel by pixel. */
      struct SwsContext *sw = sws_getContext(ctx->width, ctx->height, ctx->pix_fmt,
                                             ctx->width, ctx->height, AV_PIX_FMT_RGB24,
                                             SWS_BILINEAR, NULL, NULL, NULL);
      uint8_t *dst = (uint8_t *)malloc((size_t)ctx->width * (size_t)ctx->height * 3u);
      uint8_t *dd[4] = { dst, NULL, NULL, NULL };
      int ds[4] = { ctx->width * 3, 0, 0, 0 };
      FILE *o = fopen(v[2], "wb");
      if (sw != NULL && dst != NULL && o != NULL) {
          sws_scale(sw, (const uint8_t *const *)fr->data, fr->linesize, 0, ctx->height, dd, ds);
          fprintf(o, "P6\n%d %d\n255\n", ctx->width, ctx->height);
          fwrite(dst, 1u, (size_t)ctx->width * 3u * (size_t)ctx->height, o);
          printf("wrote %s\n", v[2]);
      }
      if (o != NULL) fclose(o);
      free(dst);
      if (sw != NULL) sws_freeContext(sw);
  }
  return 0;
}
