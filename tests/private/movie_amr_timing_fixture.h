/* MIT. Container-only fixture: real MPEG-4 I/P/P/P samples and stsd from
 * m4v_clip.h; complete AMR blocks from the existing playable movie adaptation.
 * Only sample grouping and timing/edit metadata are synthesized here. */
#ifndef NJ_TEST_MOVIE_AMR_TIMING_FIXTURE_H
#define NJ_TEST_MOVIE_AMR_TIMING_FIXTURE_H
#include "../m4v_clip.h"
#include "amr_movie_fixture.h"
#include "nj_mp4.h"
#include "nj_amr_mp4.h"

typedef struct movie_amr_fixture { nj_u1 bytes[65536]; nj_u4 n; } movie_amr_fixture;
typedef struct movie_amr_options {
    nj_u4 frames, bundle, audio_scale, audio_delta;
    nj_s4 audio_media, video_media; /* -1 disables the corresponding ELST */
    nj_u4 audio_leading, audio_segment, video_leading, video_segment;
    nj_s4 audio_ctts;
} movie_amr_options;
/* All edit durations use movie scale8000; media starts use each track's scale. */
static movie_amr_options movie_amr_defaults(void)
{
    movie_amr_options o;
    memset(&o,0,sizeof(o)); o.frames=5; o.bundle=1; o.audio_scale=8000;
    o.audio_media=o.video_media=-1;
    return o;
}
static void mat_u32(movie_amr_fixture *f,nj_u4 v)
{
    CHECK(f->n<=sizeof(f->bytes)-4);
    nj_test_amr_movie_put32(f->bytes+f->n,v); f->n+=4;
}
static void mat_bytes(movie_amr_fixture *f,const void *p,nj_u4 n)
{
    CHECK(n<=sizeof(f->bytes)-f->n); memcpy(f->bytes+f->n,p,n); f->n+=n;
}
static void mat_zero(movie_amr_fixture *f,nj_u4 n)
{ CHECK(n<=sizeof(f->bytes)-f->n); memset(f->bytes+f->n,0,n); f->n+=n; }
static nj_u4 mat_box(movie_amr_fixture *f,const char *name)
{ nj_u4 at=f->n; mat_u32(f,0); mat_bytes(f,name,4); return at; }
static void mat_end(movie_amr_fixture *f,nj_u4 at)
{ CHECK(at<=f->n-8); nj_test_amr_movie_put32(f->bytes+at,f->n-at); }
/* Bounded child walker used only on the immutable real-video fixture. */
static nj_u4 mat_child(const nj_u1 *b,nj_u4 begin,nj_u4 end,const char *tag)
{
    nj_u4 at=begin;
    while(at<end) {
        nj_u4 len;
        CHECK(end-at>=8); len=nj_test_amr_movie_be32(b+at);
        CHECK(len>=8 && len<=end-at);
        if(!memcmp(b+at+4,tag,4)) return at;
        at+=len;
    }
    CHECK(0); return 0;
}
static void mat_edit(movie_amr_fixture *f,nj_s4 media,nj_u4 leading,nj_u4 segment)
{
    nj_u4 ed,at;
    if(media<0) return;
    ed=mat_box(f,"edts"); at=mat_box(f,"elst");
    mat_u32(f,0); mat_u32(f,leading?2:1);
    if(leading) { mat_u32(f,leading); mat_u32(f,0xffffffffu); mat_u32(f,0x10000); }
    mat_u32(f,segment); mat_u32(f,(nj_u4)media); mat_u32(f,0x10000);
    mat_end(f,at); mat_end(f,ed);
}
static void mat_tables(movie_amr_fixture *f,nj_u4 samples,nj_u4 delta,
                       const nj_u4 *sizes,nj_u4 constant,nj_u4 offset,nj_s4 ctts)
{
    nj_u4 at,i;
    at=mat_box(f,"stts"); mat_u32(f,0); mat_u32(f,1);
    mat_u32(f,samples); mat_u32(f,delta); mat_end(f,at);
    if(ctts) {
        at=mat_box(f,"ctts"); mat_u32(f,0x01000000); mat_u32(f,1);
        mat_u32(f,samples); mat_u32(f,(nj_u4)ctts); mat_end(f,at);
    }
    at=mat_box(f,"stsc"); mat_u32(f,0); mat_u32(f,1);
    mat_u32(f,1); mat_u32(f,samples); mat_u32(f,1); mat_end(f,at);
    at=mat_box(f,"stsz"); mat_u32(f,0); mat_u32(f,constant); mat_u32(f,samples);
    if(!constant) for(i=0;i<samples;i++) mat_u32(f,sizes[i]);
    mat_end(f,at);
    at=mat_box(f,"stco"); mat_u32(f,0); mat_u32(f,1); mat_u32(f,offset); mat_end(f,at);
}
static void movie_amr_make(movie_amr_fixture *f,const movie_amr_options *o)
{
    static const char *path[]={"moov","trak","mdia","minf","stbl","stsd"};
    nj_u1 adapted[NJ_TEST_AMR_MOVIE_BYTES],blocks[5][13];
    nj_mp4 video; nj_mp4_sample v[4]; nj_amr_mp4 amr;
    const nj_u1 *packet; nj_u4 bytes,i,begin,end,stsd=0,vsizes[4];
    nj_u4 at,top,tr,md,mi,st,ve,ae,entry,packets,delta;
    CHECK(o->frames>0 && o->bundle>0 && o->frames%o->bundle==0);
    CHECK(o->bundle<=64 && o->audio_scale>0);
    packets=o->frames/o->bundle;
    delta=o->audio_delta?o->audio_delta:o->audio_scale/50u*o->bundle;
    CHECK((nj_u8)packets*delta<=0xffffffffu);
    CHECK(nj_mp4_open(&video,m4v_clip_3gp,sizeof(m4v_clip_3gp))==NJ_MP4_OK);
    CHECK(video.sample_count==4);
    for(i=0;i<4;i++) { CHECK(nj_mp4_next(&video,&v[i])==1); vsizes[i]=v[i].size; }
    CHECK(nj_test_amr_movie_fixture(adapted,0));
    CHECK(nj_amr_mp4_open(&amr,adapted,sizeof(adapted)));
    for(i=0;i<5;i++) {
        CHECK(nj_amr_mp4_next(&amr,&packet,&bytes)==1 && bytes==13);
        memcpy(blocks[i],packet,13);
    }
    begin=0; end=sizeof(m4v_clip_3gp);
    for(i=0;i<sizeof(path)/sizeof(path[0]);i++) {
        stsd=mat_child(m4v_clip_3gp,begin,end,path[i]);
        end=stsd+nj_test_amr_movie_be32(m4v_clip_3gp+stsd); begin=stsd+8;
    }
    memset(f,0,sizeof(*f)); at=mat_box(f,"ftyp"); mat_u32(f,0x33677034); mat_end(f,at);
    at=mat_box(f,"mdat"); ve=f->n;
    for(i=0;i<4;i++) mat_bytes(f,m4v_clip_3gp+v[i].offset,v[i].size);
    ae=f->n; for(i=0;i<o->frames;i++) mat_bytes(f,blocks[i%5],13);
    mat_end(f,at); top=mat_box(f,"moov");
    at=mat_box(f,"mvhd"); mat_zero(f,12); mat_u32(f,8000); mat_u32(f,3200); mat_zero(f,80); mat_end(f,at);
    tr=mat_box(f,"trak"); mat_edit(f,o->video_media,o->video_leading,o->video_segment);
    md=mat_box(f,"mdia"); at=mat_box(f,"mdhd"); mat_zero(f,12);
    mat_u32(f,1000); mat_u32(f,400); mat_u32(f,0); mat_end(f,at);
    at=mat_box(f,"hdlr"); mat_zero(f,8); mat_u32(f,0x76696465); mat_zero(f,12); mat_end(f,at);
    mi=mat_box(f,"minf"); st=mat_box(f,"stbl");
    mat_bytes(f,m4v_clip_3gp+stsd,end-stsd);
    mat_tables(f,4,100,vsizes,0,ve,0);
    mat_end(f,st); mat_end(f,mi); mat_end(f,md); mat_end(f,tr);
    tr=mat_box(f,"trak"); mat_edit(f,o->audio_media,o->audio_leading,o->audio_segment);
    md=mat_box(f,"mdia"); at=mat_box(f,"mdhd"); mat_zero(f,12);
    mat_u32(f,o->audio_scale); mat_u32(f,packets*delta); mat_u32(f,0); mat_end(f,at);
    at=mat_box(f,"hdlr"); mat_zero(f,8); mat_u32(f,0x736f756e); mat_zero(f,12); mat_end(f,at);
    mi=mat_box(f,"minf"); st=mat_box(f,"stbl"); at=mat_box(f,"stsd"); mat_u32(f,0); mat_u32(f,1);
    entry=mat_box(f,"samr"); mat_zero(f,28);
    f->bytes[entry+8+17]=1; /* channelcount, big endian */
    nj_test_amr_movie_put32(f->bytes+entry+8+24,8000u<<16);
    { nj_u4 damr=mat_box(f,"damr"); mat_zero(f,8); f->bytes[f->n-1]=0;
      mat_bytes(f,&(nj_u1){(nj_u1)o->bundle},1); mat_end(f,damr); }
    mat_end(f,entry); mat_end(f,at);
    mat_tables(f,packets,delta,NULL,13u*o->bundle,ae,o->audio_ctts);
    mat_end(f,st); mat_end(f,mi); mat_end(f,md); mat_end(f,tr); mat_end(f,top);
}
#endif
