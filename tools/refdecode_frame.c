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
int main(int c,char**v){
  AVFormatContext*fc=NULL;
  av_log_set_level(AV_LOG_TRACE);
  if(avformat_open_input(&fc,v[1],NULL,NULL)<0){puts("open fail");return 1;}
  avformat_find_stream_info(fc,NULL);
  int vi=-1; for(unsigned i=0;i<fc->nb_streams;i++) if(fc->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) vi=i;
  if(vi<0){puts("no video");return 1;}
  AVCodecParameters*par=fc->streams[vi]->codecpar;
  printf("codec=%s %dx%d\n",avcodec_get_name(par->codec_id),par->width,par->height);
  const AVCodec*cd=avcodec_find_decoder(par->codec_id);
  AVCodecContext*ctx=avcodec_alloc_context3(cd);
  avcodec_parameters_to_context(ctx,par);
  if(avcodec_open2(ctx,cd,NULL)<0){puts("dec open fail");return 1;}
  AVPacket*p=av_packet_alloc(); AVFrame*fr=av_frame_alloc();
  int got=0; while(av_read_frame(fc,p)>=0){
    if(p->stream_index==vi && avcodec_send_packet(ctx,p)>=0 && avcodec_receive_frame(ctx,fr)>=0){got++; av_packet_unref(p); break;}
    av_packet_unref(p);
  }
  printf("frames=%d w=%d h=%d\n",got,ctx->width,ctx->height);
  return 0;
}
